// ImGuiAppLayer canvas engine. Invariant: node geometry is STORED in model units; the camera is
// applied exactly once, at draw/hit-test time, always with THIS frame's values; sizes are measured
// the same frame and same zoom they rendered with. docs/canvas-engine-design.md,
// docs/phase-coherence.md.
//
// Index of this file (search for "[SECTION]"):
// [SECTION] State
// [SECTION] Camera + spaces
// [SECTION] Frame begin: input FSM (pan / drag / zoom / menu)
// [SECTION] Nodes (begin/end, same-frame measurement, drawing)
// [SECTION] Frame end + queries

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imguiapp_canvas.h"
#include "imgui_internal.h"

#include <string.h>

//-----------------------------------------------------------------------------
// [SECTION] State
//-----------------------------------------------------------------------------

// Scale a color's alpha channel (disabled-look node plates).
static inline ImU32 CanvasColMulAlpha(ImU32 col, float a)
{
  if (a >= 1.0f)
    return col;
  const ImU32 ca = (ImU32)(((col >> IM_COL32_A_SHIFT) & 0xFF) * a);
  return (col & ~IM_COL32_A_MASK) | (ca << IM_COL32_A_SHIFT);
}

struct ImGuiCanvasNodeRec
{
  int    Id;
  ImVec2 Pos;        // model units
  ImVec2 Size;       // model units, measured same-frame at submission
  ImU32  TitleColor; // 0 = style default
  char   Title[64];
  char   KindTag[24];  // muted right-aligned title-bar tag (the node's kind word); empty = none
  char   Badge[16];    // small framed title-bar text after the name; empty = none
  ImU32  OriginDot;    // leading title-bar dot color; 0 = none
  bool   DotRing;      // origin dot drawn as a ring instead of filled
  float  Rounding;     // corner rounding in MODEL units; < 0 = style default
  float  FixedWidth;   // normalized plate width in MODEL units; <= 0 = content-sized
  float  NeededW;      // content-derived width need (MODEL units), measured before FixedWidth applies
  int    HeaderRule;   // rule under the title band: 0 none, 1 solid, 2 dashed
  ImU32  HeaderRuleColor;
  int    StripeSide;   // ImGuiCanvasPinSide_ of an edge stripe; -1 = none
  ImU32  StripeColor;
  float  StripeThick;  // stripe thickness, MODEL units
  float  Alpha;        // plate + content opacity multiplier (disabled look); 1 = normal
  bool   Draggable;
  bool   Solid;      // cannot be dragged into overlap with other Solid nodes (slide to contact)
  int    LastFrame;  // ImGui frame count of the last submission (cull/hit bookkeeping)
};

struct ImGuiCanvasPinRec
{
  int    Id;
  int    NodeId;
  int    Kind;       // ImGuiCanvasPin_In / _Out (interaction role)
  int    Side;       // ImGuiCanvasPinSide_ (which node edge; Left/Right = data, Top/Bottom = containment)
  int    Shape;      // circle (data) / square (containment)
  ImU32  Color;      // 0 = style (by shape); set via CanvasNextPinColor
  ImVec2 Anchor;     // MODEL units: pin center at the node edge, row-centered
  int    LastFrame;
  int    WiredCount; // wires touching this pin THIS frame (filled pin glyph)
};

struct ImGuiCanvasWireRec  // per-frame submission, rebuilt every frame
{
  int   Id;
  int   PinA;
  int   PinB;
  ImU32 Color;  // 0 = style
  bool  Dashed; // optional dependency: dashed body (form carrier; dimming is the color coat)
};

enum ImGuiCanvasInteraction_
{
  ImGuiCanvasInteraction_None = 0,
  ImGuiCanvasInteraction_Pan,
  ImGuiCanvasInteraction_DragNodes,
  ImGuiCanvasInteraction_DragWire,      // from FromPin; release on a pin = created, on empty = dropped
  ImGuiCanvasInteraction_MenuPending,   // RMB down, not yet travelled: release = menu, travel = pan
};

struct ImGuiCanvasState
{
  ImGuiCanvasStyle Style;
  ImGuiCanvasIO    IO;

  // Camera
  ImVec2 Pan;
  float  Zoom;
  float  FontRatio; // host font scale (DPI * user scale) captured at CanvasBegin; geometry scale = Zoom * FontRatio

  // Nodes
  ImVector<ImGuiCanvasNodeRec> Nodes;
  ImGuiStorage                 NodeIdx;        // id -> index + 1
  ImVector<int>                SubmitOrder;    // last frame's submission order (z-order; hit-test walks it backward)
  ImVector<int>                SubmitOrderNow; // rebuilt during the current frame

  // Pins + wires
  ImVector<ImGuiCanvasPinRec>  Pins;
  ImGuiStorage                 PinIdx;      // id -> index + 1
  ImVector<ImGuiCanvasWireRec> Wires;       // rebuilt per frame between CanvasBegin/End
  ImVector<ImGuiCanvasWireRec> WiresPrev;   // last frame's wires: press decisions run at CanvasBegin
  ImVector<int>                CurNodePins; // pin indices submitted inside the current node (anchor.x resolves at EndNode)

  // Host-declared solid regions (model units, x0 y0 x1 y1): obstacles for solid-node drags.
  // Submitted between Begin/End, consumed by the NEXT frame's drag FSM -- the same T+1 posture
  // as the node geometry the drag itself runs on.
  ImVector<ImVec4> SolidRects;
  ImVector<ImVec4> SolidRectsPrev;

  // Selection + hover
  ImVector<int> Selection;
  int           HoveredNode;  // resolved against current camera + model geometry
  int           HoveredPin;   // resolved in CanvasEnd (needs this frame's wires/pins); -1 = none
  int           HoveredWire;
  int           SelectedWire; // single wire selection (click); -1 = none

  // Per-frame canvas geometry
  ImVec2             Origin;   // canvas child top-left (screen)
  ImVec2             CanvasSize;
  ImDrawList*        DrawList;
  ImDrawListSplitter Splitter; // ch0 = grid + wires, ch1 = node plates, ch2 = node content
  bool               InsideCanvas;

  // Submission intent -- this canvas's per-frame TempData: latched by the CanvasNext* calls,
  // consumed by the next Begin*, reset there. Per instance, never file scope.
  int    CurNode;       // index into Nodes during Begin/EndNode, -1 otherwise
  ImVec2 CurNodeScreen; // this frame's screen pos of the node origin
  int    CurPin;        // index into Pins during Begin/EndPin, -1 otherwise
  float  CurPinY0;      // row top at CanvasBeginPin (row center resolves at EndPin)
  char   NextTitle[64];
  ImU32  NextTitleColor;
  char   NextTitleTag[24];
  char   NextBadge[16];
  ImU32  NextOriginDot;
  bool   NextDotRing;
  float  NextRounding;      // model units; < 0 = style default
  float  NextFixedWidth;    // model units; <= 0 = content-sized
  int    NextHeaderRule;    // 0 none, 1 solid, 2 dashed
  ImU32  NextHeaderRuleColor;
  int    NextStripeSide;    // ImGuiCanvasPinSide_; -1 none
  ImU32  NextStripeColor;
  float  NextStripeThick;   // model units
  float  NextAlpha;         // plate + content opacity multiplier; 1 = normal
  ImU32  NextPinColor;
  int    NextPinSide;       // -1 -> derive from Kind (In->Left, Out->Right)
  char*  NextEditBuf;
  int    NextEditBufSize;
  bool*  NextEditFlag;
  bool   NextWireDashed;

  // Interaction FSM
  int              Interaction;
  ImVec2           GestureStartMouse;
  ImVec2           GestureStartPan;
  ImVector<ImVec2> DragStartPos;    // model pos of each selected node at drag start
  ImVector<int>    DragNodes;
  ImVec2           DragAppliedDisp; // solid drags: displacement actually granted so far (greedy catch-up)
  int              DragWireFromPin; // DragWire: the fixed end

  // Rename hook: per-frame pointers captured by CanvasNextNodeTitleEditable (host-owned storage).
  char* EditBuf;
  int   EditBufSize;
  bool* EditFlag;
  int   EditNodeIdx;
  bool  EditFocusPending;
  int   LastEditingNodeId; // focus-once tracking across frames

  // Minimap
  bool   MiniMapReq;
  float  MiniMapFraction;
  ImVec2 MiniRectMin;    // background rect incl. padding ring (screen); the FSM keeps out of it
  ImVec2 MiniRectMax;
  ImVec2 MiniContentMin; // content rect origin (screen); the mapping's anchor
  ImVec2 MiniModelMin;   // content bounds min (model); the mapping's other anchor
  float  MiniScale;      // model units -> minimap pixels

  // Latched events (valid from CanvasEnd until the next CanvasBegin)
  bool   NodeDblClickReq;
  int    NodeDblClickId;
  bool   MenuNodeReq;
  int    MenuNodeId;
  bool   MenuWireReq;
  int    MenuWireId;
  bool   MenuEmptyReq;
  ImVec2 MenuEmptyModel;
  bool   WireCreatedReq;
  int    CreatedPinA;
  int    CreatedPinB;
  bool   WireDroppedReq;
  int    DroppedFromPin;
  ImVec2 DroppedModel;
  bool   WireDetachedReq;
  int    DetachedWireId;
  int    DetachedGrabbedPin;

  ImGuiCanvasState()
  {
    memset(&Style, 0, sizeof(Style));   // colors filled by CanvasStyleFromTheme in CanvasCreate
    Style.GridSpacing     = 24.0f;
    Style.NodeRounding    = 4.0f;
    Style.NodePadding     = ImVec2(8.0f, 6.0f);
    Style.NodeBorder      = 1.0f;
    Style.WireThickness   = 2.5f;
    Style.PinRadius       = 4.0f;
    Style.PinHoverRadius  = 8.0f;
    Style.GridLines       = true;
    Style.GridSnap        = false;

    IO.LmbPansEmptyCanvas = true;
    IO.RmbPans            = true;
    IO.WheelZooms         = true;
    IO.ZoomMin            = 0.3f;
    IO.ZoomMax            = 2.5f;

    Pan               = ImVec2(0.0f, 0.0f);
    Zoom              = 1.0f;
    FontRatio         = 1.0f;
    HoveredNode = HoveredPin = HoveredWire = SelectedWire = -1;
    Origin = CanvasSize = ImVec2(0.0f, 0.0f);
    DrawList          = nullptr;
    InsideCanvas      = false;
    CurNode           = -1;
    CurNodeScreen     = ImVec2(0.0f, 0.0f);
    CurPin            = -1;
    CurPinY0          = 0.0f;
    NextTitle[0]      = 0;
    NextTitleColor    = 0;
    NextTitleTag[0]   = 0;
    NextBadge[0]      = 0;
    NextOriginDot     = 0;
    NextDotRing       = false;
    NextRounding      = -1.0f;
    NextFixedWidth    = -1.0f;
    NextHeaderRule    = 0;
    NextHeaderRuleColor = 0;
    NextStripeSide    = -1;
    NextStripeColor   = 0;
    NextStripeThick   = 0.0f;
    NextAlpha         = 1.0f;
    NextPinColor      = 0;
    NextPinSide       = -1;
    NextEditBuf       = nullptr;
    NextEditBufSize   = 0;
    NextEditFlag      = nullptr;
    NextWireDashed    = false;
    Interaction       = ImGuiCanvasInteraction_None;
    GestureStartMouse = GestureStartPan = ImVec2(0.0f, 0.0f);
    DragWireFromPin   = -1;
    EditBuf = nullptr; EditBufSize = 0; EditFlag = nullptr; EditNodeIdx = -1; EditFocusPending = false;
    LastEditingNodeId = -1;
    MiniMapReq        = false;
    MiniMapFraction   = 0.2f;
    MiniRectMin = MiniRectMax = ImVec2(0.0f, 0.0f);
    MiniContentMin = MiniModelMin = ImVec2(0.0f, 0.0f);
    MiniScale         = 0.0f;
    NodeDblClickReq = false; NodeDblClickId = -1;
    MenuNodeReq = MenuWireReq = MenuEmptyReq = false;
    MenuNodeId = MenuWireId = -1;
    MenuEmptyModel    = ImVec2(0.0f, 0.0f);
    WireCreatedReq = WireDroppedReq = WireDetachedReq = false;
    CreatedPinA = CreatedPinB = DroppedFromPin = DetachedWireId = DetachedGrabbedPin = -1;
    DroppedModel      = ImVec2(0.0f, 0.0f);
  }
};

static ImGuiCanvasNodeRec* CanvasFindNode(ImGuiCanvasState* c, int node_id)
{
  const int idx = c->NodeIdx.GetInt((ImGuiID)node_id, 0) - 1;
  return idx >= 0 ? &c->Nodes.Data[idx] : nullptr;
}

static ImGuiCanvasPinRec* CanvasFindPin(ImGuiCanvasState* c, int pin_id)
{
  const int idx = c->PinIdx.GetInt((ImGuiID)pin_id, 0) - 1;
  return idx >= 0 ? &c->Pins.Data[idx] : nullptr;
}

static ImGuiCanvasPinRec* CanvasFindOrCreatePin(ImGuiCanvasState* c, int pin_id)
{
  if (ImGuiCanvasPinRec* p = CanvasFindPin(c, pin_id))
    return p;
  ImGuiCanvasPinRec rec;
  memset(&rec, 0, sizeof(rec));
  rec.Id = pin_id;
  rec.LastFrame = -1;
  c->Pins.push_back(rec);
  c->PinIdx.SetInt((ImGuiID)pin_id, c->Pins.Size);
  return &c->Pins.back();
}

// Model -> screen scale. Zoom is the logical camera; FontRatio folds in the host font's DPI/user
// scale so model units stay DPI-invariant (persisted layouts render the same on any monitor).
static float CanvasScale(const ImGuiCanvasState* c)
{
  return c->Zoom * c->FontRatio;
}

// Outward normal of a pin's edge: the direction its wire leaves the node.
static ImVec2 CanvasSideNormal(int side)
{
  switch (side)
  {
  case ImGui::ImGuiCanvasPinSide_Left:   return ImVec2(-1.0f, 0.0f);
  case ImGui::ImGuiCanvasPinSide_Top:    return ImVec2(0.0f, -1.0f);
  case ImGui::ImGuiCanvasPinSide_Bottom: return ImVec2(0.0f, 1.0f);
  case ImGui::ImGuiCanvasPinSide_Right:
  default:                               return ImVec2(1.0f, 0.0f);
  }
}

// The facing edge (used for the free end of a drag preview, which has no real pin).
static int CanvasOppositeSide(int side)
{
  switch (side)
  {
  case ImGui::ImGuiCanvasPinSide_Left:   return ImGui::ImGuiCanvasPinSide_Right;
  case ImGui::ImGuiCanvasPinSide_Right:  return ImGui::ImGuiCanvasPinSide_Left;
  case ImGui::ImGuiCanvasPinSide_Top:    return ImGui::ImGuiCanvasPinSide_Bottom;
  case ImGui::ImGuiCanvasPinSide_Bottom:
  default:                               return ImGui::ImGuiCanvasPinSide_Top;
  }
}

// Wire bezier controls: tangents leave each pin along its edge's outward normal. Distance is measured
// along each pin's own axis, so a Left/Right pair reproduces the classic horizontal S-curve exactly and
// a Top/Bottom pair bows vertically.
static void CanvasWireControls(const ImGuiCanvasState* c, ImVec2 a, int side_a, ImVec2 b, int side_b, ImVec2* c0, ImVec2* c1)
{
  const ImVec2 na = CanvasSideNormal(side_a);
  const ImVec2 nb = CanvasSideNormal(side_b);
  const float  min_d = 50.0f * CanvasScale(c);
  const float  da = ImMax(min_d, ImFabs((b.x - a.x) * na.x + (b.y - a.y) * na.y) * 0.5f);
  const float  db = ImMax(min_d, ImFabs((a.x - b.x) * nb.x + (a.y - b.y) * nb.y) * 0.5f);
  *c0 = ImVec2(a.x + na.x * da, a.y + na.y * da);
  *c1 = ImVec2(b.x + nb.x * db, b.y + nb.y * db);
}

static float CanvasWireDistanceSq(ImVec2 p, ImVec2 a, ImVec2 c0, ImVec2 c1, ImVec2 b)
{
  float best = FLT_MAX;
  ImVec2 prev = a;
  const int segs = 24;
  for (int i = 1; i <= segs; i++)
  {
    const float t = (float)i / (float)segs;
    const float u = 1.0f - t;
    const ImVec2 pt(u * u * u * a.x + 3.0f * u * u * t * c0.x + 3.0f * u * t * t * c1.x + t * t * t * b.x,
                    u * u * u * a.y + 3.0f * u * u * t * c0.y + 3.0f * u * t * t * c1.y + t * t * t * b.y);
    // point-to-segment
    const ImVec2 ab = pt - prev;
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    float tt = len2 > 0.0f ? ((p.x - prev.x) * ab.x + (p.y - prev.y) * ab.y) / len2 : 0.0f;
    tt = ImClamp(tt, 0.0f, 1.0f);
    const ImVec2 q(prev.x + ab.x * tt, prev.y + ab.y * tt);
    const float d2 = (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y);
    best = ImMin(best, d2);
    prev = pt;
  }
  return best;
}

static ImGuiCanvasNodeRec* CanvasFindOrCreateNode(ImGuiCanvasState* c, int node_id)
{
  if (ImGuiCanvasNodeRec* n = CanvasFindNode(c, node_id))
    return n;
  ImGuiCanvasNodeRec rec;
  memset(&rec, 0, sizeof(rec));
  rec.Id = node_id;
  rec.Draggable = true;
  rec.LastFrame = -1;
  rec.Rounding = -1.0f;
  rec.FixedWidth = -1.0f;
  rec.StripeSide = -1;
  rec.Alpha = 1.0f;
  c->Nodes.push_back(rec);
  c->NodeIdx.SetInt((ImGuiID)node_id, c->Nodes.Size);   // index + 1
  return &c->Nodes.back();
}

//-----------------------------------------------------------------------------
// [SECTION] Camera + spaces
//-----------------------------------------------------------------------------

// Blend of two theme anchors with an explicit alpha.
static ImU32 CanvasThemeMix(ImVec4 a, ImVec4 b, float t, float alpha)
{
  ImVec4 c = ImLerp(a, b, t);
  c.w = alpha;
  return ImGui::ColorConvertFloat4ToU32(c);
}

static ImU32 CanvasThemeCol(ImVec4 c, float alpha)
{
  c.w = alpha;
  return ImGui::ColorConvertFloat4ToU32(c);
}

namespace ImGui
{
  ImGuiCanvasState* CanvasCreate()
  {
    ImGuiCanvasState* c = IM_NEW(ImGuiCanvasState)();
    CanvasStyleFromTheme(&c->Style);
    return c;
  }
  void              CanvasDestroy(ImGuiCanvasState* c)   { IM_DELETE(c); }
  ImGuiCanvasStyle* CanvasGetStyle(ImGuiCanvasState* c)  { return &c->Style; }
  ImGuiCanvasIO*    CanvasGetIO(ImGuiCanvasState* c)     { return &c->IO; }

  void CanvasStyleFromTheme(ImGuiCanvasStyle* style)
  {
    IM_ASSERT(GetCurrentContext() != nullptr && "CanvasStyleFromTheme reads the current ImGuiStyle");
    const ImVec4 bg    = GetStyleColorVec4(ImGuiCol_WindowBg);
    const ImVec4 ink   = GetStyleColorVec4(ImGuiCol_Text);
    const ImVec4 dark  = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);

    // Semantic accents, pulled toward ink so they stay legible on light themes.
    const ImVec4 sel  = ImLerp(ImVec4(0.90f, 0.78f, 0.47f, 1.0f), ink, 0.15f);   // selection (gold)
    const ImVec4 data = ImLerp(ImVec4(0.47f, 0.67f, 0.90f, 1.0f), ink, 0.15f);   // data pins (circle)
    const ImVec4 cont = ImLerp(ImVec4(0.90f, 0.67f, 0.43f, 1.0f), ink, 0.15f);   // containment pins (square)

    style->GridBg              = CanvasThemeMix(bg, ink, 0.06f,  1.00f);
    style->GridLine            = CanvasThemeMix(bg, ink, 0.16f,  0.43f);
    style->GridLinePrimary     = CanvasThemeMix(bg, ink, 0.21f,  0.55f);
    style->NodeBg              = CanvasThemeMix(bg, ink, 0.135f, 1.00f);
    style->NodeBgHovered       = CanvasThemeMix(bg, ink, 0.16f,  1.00f);
    style->NodeBgSelected      = CanvasThemeMix(bg, ink, 0.175f, 1.00f);
    style->NodeOutline         = CanvasThemeMix(dark, bg, 0.07f, 0.78f);
    style->NodeOutlineSelected = CanvasThemeCol(sel, 1.00f);
    style->TitleBar            = CanvasThemeMix(bg, ink, 0.215f, 1.00f);
    style->TitleBarHovered     = CanvasThemeMix(bg, ink, 0.26f,  1.00f);
    style->TitleBarSelected    = CanvasThemeMix(bg, ink, 0.31f,  1.00f);
    style->TitleText           = CanvasThemeMix(bg, ink, 0.95f,  1.00f);
    style->TitleEditBg         = CanvasThemeMix(bg, ink, 0.02f,  1.00f);
    style->Wire                = CanvasThemeMix(bg, ink, 0.61f,  0.86f);
    style->WireHovered         = CanvasThemeMix(bg, ink, 0.92f,  1.00f);
    style->WireSelected        = CanvasThemeCol(sel, 1.00f);
    style->PinData             = CanvasThemeCol(data, 1.00f);
    style->PinContainment      = CanvasThemeCol(cont, 1.00f);
    style->PinHovered          = CanvasThemeMix(bg, ink, 0.98f,  1.00f);
    style->MiniMapBg           = CanvasThemeMix(bg, ink, 0.04f,  0.59f);
    style->MiniMapBgHovered    = CanvasThemeMix(bg, ink, 0.04f,  0.78f);
    style->MiniMapOutline      = CanvasThemeMix(bg, ink, 0.56f,  0.39f);
    style->MiniMapNodeBg       = CanvasThemeMix(bg, ink, 0.77f,  0.39f);
    style->MiniMapNodeBgHovered  = CanvasThemeMix(bg, ink, 0.77f, 1.00f);
    style->MiniMapNodeBgSelected = CanvasThemeMix(ImLerp(bg, ink, 0.77f), data, 0.35f, 1.00f);
    style->MiniMapNodeOutline  = CanvasThemeMix(bg, ink, 0.77f,  0.39f);
    style->MiniMapLink         = CanvasThemeMix(bg, ink, 0.61f,  0.78f);
    style->MiniMapCanvas       = CanvasThemeMix(bg, ink, 0.77f,  0.10f);
    style->MiniMapCanvasOutline= CanvasThemeMix(bg, ink, 0.77f,  0.78f);
  }

  ImVec2 CanvasGetPan(const ImGuiCanvasState* c)              { return c->Pan; }
  void   CanvasSetPan(ImGuiCanvasState* c, ImVec2 pan)        { c->Pan = pan; }
  float  CanvasGetZoom(const ImGuiCanvasState* c)             { return c->Zoom; }
  float  CanvasGetScale(const ImGuiCanvasState* c)            { return CanvasScale(c); }

  ImVec2 CanvasToScreen(const ImGuiCanvasState* c, ImVec2 model)
  {
    return c->Origin + c->Pan + model * CanvasScale(c);
  }

  ImVec2 CanvasFromScreen(const ImGuiCanvasState* c, ImVec2 screen)
  {
    return (screen - c->Origin - c->Pan) / CanvasScale(c);
  }

  void CanvasSetZoom(ImGuiCanvasState* c, float zoom, ImVec2 keep_screen_pos)
  {
    zoom = ImClamp(zoom, c->IO.ZoomMin, c->IO.ZoomMax);
    const ImVec2 anchor_model = CanvasFromScreen(c, keep_screen_pos);
    c->Zoom = zoom;
    c->Pan = keep_screen_pos - c->Origin - anchor_model * CanvasScale(c);
  }

  void CanvasCenterOn(ImGuiCanvasState* c, ImVec2 model_pos)
  {
    c->Pan = c->CanvasSize * 0.5f - model_pos * CanvasScale(c);
  }

  void CanvasFitRect(ImGuiCanvasState* c, ImVec2 model_min, ImVec2 model_max, float margin_px)
  {
    const ImVec2 span(ImMax(1.0f, model_max.x - model_min.x), ImMax(1.0f, model_max.y - model_min.y));
    const ImVec2 avail(ImMax(1.0f, c->CanvasSize.x - margin_px * 2.0f), ImMax(1.0f, c->CanvasSize.y - margin_px * 2.0f));
    c->Zoom = ImClamp(ImMin(avail.x / span.x, avail.y / span.y) / c->FontRatio, c->IO.ZoomMin, c->IO.ZoomMax);
    CanvasCenterOn(c, (model_min + model_max) * 0.5f);
  }

  void CanvasFitNodes(ImGuiCanvasState* c, const int* node_ids, int count, float margin_px)
  {
    ImVec2 mn(FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX);
    for (int i = 0; i < count; i++)
      if (const ImGuiCanvasNodeRec* n = CanvasFindNode(c, node_ids[i]))
      {
        mn = ImMin(mn, n->Pos);
        mx = ImMax(mx, n->Pos + n->Size);
      }
    if (mn.x <= mx.x)
      CanvasFitRect(c, mn, mx, margin_px);
  }

  void CanvasFitAll(ImGuiCanvasState* c, float margin_px)
  {
    const int frame = GetFrameCount();
    ImVec2 mn(FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX);
    for (int i = 0; i < c->Nodes.Size; i++)
    {
      const ImGuiCanvasNodeRec* n = &c->Nodes.Data[i];
      if (n->LastFrame < frame - 1)
        continue;   // fit what the canvas is showing, not every node ever seen
      mn = ImMin(mn, n->Pos);
      mx = ImMax(mx, n->Pos + n->Size);
    }
    if (mn.x <= mx.x)
      CanvasFitRect(c, mn, mx, margin_px);
  }

  void CanvasMiniMap(ImGuiCanvasState* c, float size_fraction)
  {
    IM_ASSERT(c->InsideCanvas);
    c->MiniMapReq = true;
    c->MiniMapFraction = ImClamp(size_fraction, 0.05f, 0.5f);
  }
}

//-----------------------------------------------------------------------------
// [SECTION] Frame begin: input FSM (pan / drag / zoom / menu)
//-----------------------------------------------------------------------------

// Top-most node under a screen point: walks last frame's submission order backward (z-order IS
// submission order). Positions are current, sizes are last frame's MODEL measurement (zoom-invariant).
static int CanvasHitNode(const ImGuiCanvasState* c, ImVec2 screen)
{
  for (int i = c->SubmitOrder.Size - 1; i >= 0; i--)
  {
    const int idx = c->NodeIdx.GetInt((ImGuiID)c->SubmitOrder.Data[i], 0) - 1;
    if (idx < 0)
      continue;
    const ImGuiCanvasNodeRec* n = &c->Nodes.Data[idx];
    const ImVec2 mn = ImGui::CanvasToScreen(c, n->Pos);
    const ImVec2 mx = mn + n->Size * CanvasScale(c);
    if (screen.x >= mn.x && screen.x < mx.x && screen.y >= mn.y && screen.y < mx.y)
      return n->Id;
  }
  return -1;
}

static void CanvasSelectSolo(ImGuiCanvasState* c, int node_id)
{
  c->Selection.resize(0);
  c->Selection.push_back(node_id);
}

static bool CanvasIsSelected(const ImGuiCanvasState* c, int node_id)
{
  for (int i = 0; i < c->Selection.Size; i++)
    if (c->Selection.Data[i] == node_id)
      return true;
  return false;
}

// Wire drags ORIGINATE only from a press on the pin glyph itself; the looser PinHoverRadius
// stays for hover highlight and mid-drag snap targets. An origin as loose as the hover radius
// turned title grabs near an edge pin into a wire rubber band instead of a node drag.
static bool CanvasPinPressOnGlyph(ImGuiCanvasState* c, int pin_id, ImVec2 mouse)
{
  const ImGuiCanvasPinRec* p = CanvasFindPin(c, pin_id);
  if (p == nullptr)
    return false;
  const ImVec2 s = ImGui::CanvasToScreen(c, p->Anchor);
  const float r = c->Style.PinRadius * 1.5f * CanvasScale(c);
  return (mouse.x - s.x) * (mouse.x - s.x) + (mouse.y - s.y) * (mouse.y - s.y) <= r * r;
}

static void CanvasBeginNodeDrag(ImGuiCanvasState* c)
{
  c->Interaction = ImGuiCanvasInteraction_DragNodes;
  c->DragNodes.resize(0);
  c->DragStartPos.resize(0);
  c->DragAppliedDisp = ImVec2(0.0f, 0.0f);
  for (int i = 0; i < c->Selection.Size; i++)
    if (const ImGuiCanvasNodeRec* n = CanvasFindNode(c, c->Selection.Data[i]))
      if (n->Draggable)
      {
        c->DragNodes.push_back(n->Id);
        c->DragStartPos.push_back(n->Pos);
      }
}

// Solid-drag containment: dragged Solid nodes slide to contact against every Solid node outside
// the drag set and keep tracking the mouse the moment it retreats (placement is ABSOLUTE from the
// drag start; both axis orders are tried and the one landing closest to the mouse wins -- same
// policy as the editor's group drag). kNoiseM bounds T+1 size-republication wobble (font rounding,
// sub-unit content growth): penetration within it clamps back to contact; a deeper pre-existing
// overlap drops that mover/obstacle pair so one overlap cannot freeze the whole drag.
static bool CanvasDragHasSolid(ImGuiCanvasState* c)
{
  for (int i = 0; i < c->DragNodes.Size; i++)
    if (const ImGuiCanvasNodeRec* n = CanvasFindNode(c, c->DragNodes.Data[i]))
      if (n->Solid && n->Size.x > 0.0f && n->Size.y > 0.0f)
        return true;
  return false;
}

static ImVec2 CanvasResolveSolidDrag(ImGuiCanvasState* c, ImVec2 disp)
{
  const float kNoiseM = 1.0f;

  if (!CanvasDragHasSolid(c))
    return disp;

  auto overlap = [](float a0, float a1, float b0, float b1) { return a0 < b1 && a1 > b0; };
  auto dragged = [c](int id)
  {
    for (int i = 0; i < c->DragNodes.Size; i++)
      if (c->DragNodes.Data[i] == id)
        return true;
    return false;
  };

  // Obstacles (x0 y0 x1 y1): solid nodes outside the drag set + host-declared solid regions
  // (last frame's, the same T+1 posture as the node geometry the movers themselves run on).
  ImVector<ImVec4> ob;
  ob.reserve(c->Nodes.Size + c->SolidRectsPrev.Size);
  for (int o = 0; o < c->Nodes.Size; o++)
  {
    const ImGuiCanvasNodeRec* n = &c->Nodes.Data[o];
    if (!n->Solid || n->Size.x <= 0.0f || n->Size.y <= 0.0f || dragged(n->Id))
      continue;
    ob.push_back(ImVec4(n->Pos.x, n->Pos.y, n->Pos.x + n->Size.x, n->Pos.y + n->Size.y));
  }
  for (int o = 0; o < c->SolidRectsPrev.Size; o++)
    ob.push_back(c->SolidRectsPrev.Data[o]);
  if (ob.Size == 0)
    return disp;

  // Clamp dx for every (dragged solid mover, obstacle) pair. Movers stand at their CURRENT
  // placement (start + DragAppliedDisp): the step being resolved is this frame's remaining
  // catch-up toward the mouse, not the whole displacement since the drag started. dy_ctx
  // shifts the movers' y-band by the step already granted on the other axis.
  auto slide_x = [&](float dx, float dy_ctx) -> float
  {
    for (int i = 0; i < c->DragNodes.Size; i++)
    {
      const ImGuiCanvasNodeRec* m = CanvasFindNode(c, c->DragNodes.Data[i]);
      if (m == nullptr || !m->Solid || m->Size.x <= 0.0f || m->Size.y <= 0.0f)
        continue;
      const ImVec2 s = c->DragStartPos.Data[i] + c->DragAppliedDisp;
      const float mx0 = s.x;
      const float mx1 = s.x + m->Size.x;
      for (int o = 0; o < ob.Size; o++)
      {
        const float ox0 = ob.Data[o].x;
        const float oy0 = ob.Data[o].y;
        const float ox1 = ob.Data[o].z;
        const float oy1 = ob.Data[o].w;
        if (overlap(mx0, mx1, ox0 + kNoiseM, ox1 - kNoiseM) && overlap(s.y, s.y + m->Size.y, oy0 + kNoiseM, oy1 - kNoiseM))
          continue;
        if (!overlap(s.y + dy_ctx, s.y + m->Size.y + dy_ctx, oy0, oy1))
          continue;
        if (dx > 0.0f && mx1 <= ox0 + kNoiseM && mx1 + dx > ox0)
          dx = ox0 - mx1;
        if (dx < 0.0f && mx0 >= ox1 - kNoiseM && mx0 + dx < ox1)
          dx = ox1 - mx0;
      }
    }
    return dx;
  };
  auto slide_y = [&](float dy, float dx_ctx) -> float
  {
    for (int i = 0; i < c->DragNodes.Size; i++)
    {
      const ImGuiCanvasNodeRec* m = CanvasFindNode(c, c->DragNodes.Data[i]);
      if (m == nullptr || !m->Solid || m->Size.x <= 0.0f || m->Size.y <= 0.0f)
        continue;
      const ImVec2 s = c->DragStartPos.Data[i] + c->DragAppliedDisp;
      const float my0 = s.y;
      const float my1 = s.y + m->Size.y;
      for (int o = 0; o < ob.Size; o++)
      {
        const float ox0 = ob.Data[o].x;
        const float oy0 = ob.Data[o].y;
        const float ox1 = ob.Data[o].z;
        const float oy1 = ob.Data[o].w;
        if (overlap(s.x, s.x + m->Size.x, ox0 + kNoiseM, ox1 - kNoiseM) && overlap(my0, my1, oy0 + kNoiseM, oy1 - kNoiseM))
          continue;
        if (!overlap(s.x + dx_ctx, s.x + m->Size.x + dx_ctx, ox0, ox1))
          continue;
        if (dy > 0.0f && my1 <= oy0 + kNoiseM && my1 + dy > oy0)
          dy = oy0 - my1;
        if (dy < 0.0f && my0 >= oy1 - kNoiseM && my0 + dy < oy1)
          dy = oy1 - my0;
      }
    }
    return dy;
  };

  ImVec2 a;
  a.x = slide_x(disp.x, 0.0f);
  a.y = slide_y(disp.y, a.x);
  ImVec2 b;
  b.y = slide_y(disp.y, 0.0f);
  b.x = slide_x(disp.x, b.y);
  const float ea = (a.x - disp.x) * (a.x - disp.x) + (a.y - disp.y) * (a.y - disp.y);
  const float eb = (b.x - disp.x) * (b.x - disp.x) + (b.y - disp.y) * (b.y - disp.y);
  return ea <= eb ? a : b;
}

static void CanvasUpdateInput(ImGuiCanvasState* c, bool canvas_item_hovered, bool canvas_item_activated)
{
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 mouse = io.MousePos;

  // The minimap owns its rect: LMB held over it recenters the camera; the canvas FSM stays out.
  const bool in_minimap = mouse.x >= c->MiniRectMin.x && mouse.x < c->MiniRectMax.x
                       && mouse.y >= c->MiniRectMin.y && mouse.y < c->MiniRectMax.y;
  if (in_minimap && c->Interaction == ImGuiCanvasInteraction_None)
  {
    c->HoveredNode = -1;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && canvas_item_hovered && c->MiniScale > 0.0f)
    {
      const ImVec2 target = (mouse - c->MiniContentMin) / c->MiniScale + c->MiniModelMin;
      ImGui::CanvasCenterOn(c, target);
    }
    return;
  }

  // Hover: current camera x current model, valid for this whole frame.
  c->HoveredNode = canvas_item_hovered || c->Interaction != ImGuiCanvasInteraction_None ? CanvasHitNode(c, mouse) : -1;

  // Double-click is reported, not interpreted; the host decides.
  if (c->HoveredNode >= 0 && canvas_item_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
  {
    c->NodeDblClickReq = true;
    c->NodeDblClickId = c->HoveredNode;
  }

  // Wheel zoom, cursor-anchored: plain wheel on empty canvas only; Ctrl+wheel anywhere over the
  // canvas (node widgets keep the plain wheel).
  if (c->IO.WheelZooms && io.MouseWheel != 0.0f
      && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)
      && (io.KeyCtrl || (canvas_item_hovered && c->HoveredNode == -1)))
  {
    ImGui::CanvasSetZoom(c, c->Zoom * ImPow(1.15f, io.MouseWheel), mouse);
  }

  // LMB press hit priority: pin -> wire -> node -> empty. Pin and wire hover come from the last
  // CanvasEnd resolution.
  if (canvas_item_activated && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
  {
    c->GestureStartMouse = mouse;
    c->GestureStartPan = c->Pan;
    if (c->HoveredPin >= 0 && CanvasPinPressOnGlyph(c, c->HoveredPin, mouse))
    {
      c->Interaction = ImGuiCanvasInteraction_DragWire;
      c->DragWireFromPin = c->HoveredPin;
    }
    else if (c->HoveredWire >= 0)
    {
      // Grab near an endpoint DETACHES the wire (drag continues from the surviving end); a click
      // that never travels SELECTS it.
      c->SelectedWire = c->HoveredWire;
      for (int i = 0; i < c->WiresPrev.Size; i++)
        if (c->WiresPrev.Data[i].Id == c->HoveredWire)
        {
          const ImGuiCanvasPinRec* pa = CanvasFindPin(c, c->WiresPrev.Data[i].PinA);
          const ImGuiCanvasPinRec* pb = CanvasFindPin(c, c->WiresPrev.Data[i].PinB);
          if (pa != nullptr && pb != nullptr)
          {
            const ImVec2 sa = ImGui::CanvasToScreen(c, pa->Anchor);
            const ImVec2 sb = ImGui::CanvasToScreen(c, pb->Anchor);
            const float da = (mouse.x - sa.x) * (mouse.x - sa.x) + (mouse.y - sa.y) * (mouse.y - sa.y);
            const float db = (mouse.x - sb.x) * (mouse.x - sb.x) + (mouse.y - sb.y) * (mouse.y - sb.y);
            const float grab = c->Style.PinHoverRadius * CanvasScale(c) * 3.0f;
            if (ImMin(da, db) <= grab * grab)
            {
              const bool grab_a = da <= db;
              c->WireDetachedReq = true;
              c->DetachedWireId = c->WiresPrev.Data[i].Id;
              c->DetachedGrabbedPin = grab_a ? pa->Id : pb->Id;
              c->Interaction = ImGuiCanvasInteraction_DragWire;
              c->DragWireFromPin = grab_a ? pb->Id : pa->Id;   // drag continues from the SURVIVING end
            }
          }
          break;
        }
    }
    else if (c->HoveredNode >= 0)
    {
      c->SelectedWire = -1;
      if (io.KeyCtrl)
      {
        // Additive toggle.
        bool removed = false;
        for (int i = 0; i < c->Selection.Size; i++)
          if (c->Selection.Data[i] == c->HoveredNode)
          {
            c->Selection.erase(c->Selection.Data + i);
            removed = true;
            break;
          }
        if (!removed)
          c->Selection.push_back(c->HoveredNode);
      }
      else if (!CanvasIsSelected(c, c->HoveredNode))
      {
        CanvasSelectSolo(c, c->HoveredNode);
      }
      CanvasBeginNodeDrag(c);
    }
    else
    {
      // Click on empty canvas deselects; Ctrl preserves the selection.
      if (!io.KeyCtrl)
        c->Selection.resize(0);
      c->SelectedWire = -1;
      if (c->IO.LmbPansEmptyCanvas)
        c->Interaction = ImGuiCanvasInteraction_Pan;
    }
  }

  // RMB: pan on travel, menu on a short click (release under 3px of total travel).
  if (canvas_item_activated && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && c->Interaction == ImGuiCanvasInteraction_None)
  {
    c->GestureStartMouse = mouse;
    c->GestureStartPan = c->Pan;
    c->Interaction = ImGuiCanvasInteraction_MenuPending;
  }

  switch (c->Interaction)
  {
  case ImGuiCanvasInteraction_Pan:
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
      c->Pan = c->GestureStartPan + (mouse - c->GestureStartMouse);
    else
      c->Interaction = ImGuiCanvasInteraction_None;
    break;

  case ImGuiCanvasInteraction_DragNodes:
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
      ImVec2 delta_model = (mouse - c->GestureStartMouse) / CanvasScale(c);   // pixels -> model, once, here
      const bool solid_drag = CanvasDragHasSolid(c);
      if (solid_drag)
      {
        if (c->Style.GridSnap && c->Style.GridSpacing > 0.0f && c->DragNodes.Size > 0)
        {
          // Snap the SHARED displacement before the solid clamp (per-node re-snap after the clamp
          // could round back into overlap); the clamped contact position wins over the grid.
          const ImVec2 s0 = c->DragStartPos.Data[0];
          const ImVec2 t(ImFloor((s0.x + delta_model.x) / c->Style.GridSpacing + 0.5f) * c->Style.GridSpacing,
                         ImFloor((s0.y + delta_model.y) / c->Style.GridSpacing + 0.5f) * c->Style.GridSpacing);
          delta_model = t - s0;
        }
        // Greedy catch-up: seek this frame's mouse-anchored target from the placement actually
        // reached so far. Granted progress accumulates -- a diagonal along an obstacle edge
        // slides now and continues later, and a blocked frame never resets earlier progress
        // (the absolute re-derivation from the drag start snapped the node back whenever both
        // axis orders clamped). In free space the step equals the full remainder, so the node
        // stays exactly mouse-anchored.
        const ImVec2 want = delta_model - c->DragAppliedDisp;
        c->DragAppliedDisp += CanvasResolveSolidDrag(c, want);
        delta_model = c->DragAppliedDisp;
      }
      for (int i = 0; i < c->DragNodes.Size; i++)
        if (ImGuiCanvasNodeRec* n = CanvasFindNode(c, c->DragNodes.Data[i]))
        {
          ImVec2 p = c->DragStartPos.Data[i] + delta_model;
          if (!solid_drag && c->Style.GridSnap && c->Style.GridSpacing > 0.0f)
            p = ImVec2(ImFloor(p.x / c->Style.GridSpacing + 0.5f) * c->Style.GridSpacing,
                       ImFloor(p.y / c->Style.GridSpacing + 0.5f) * c->Style.GridSpacing);
          n->Pos = p;
        }
    }
    else
    {
      c->Interaction = ImGuiCanvasInteraction_None;
    }
    break;

  case ImGuiCanvasInteraction_DragWire:
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
      // Release over a pin (other than the origin) = created; anything else = dropped at the model position.
      if (c->HoveredPin >= 0 && c->HoveredPin != c->DragWireFromPin)
      {
        c->WireCreatedReq = true;
        // Normalize to (out, in) when the kinds disagree: hosts get a stable orientation.
        const ImGuiCanvasPinRec* pf = CanvasFindPin(c, c->DragWireFromPin);
        const ImGuiCanvasPinRec* pt = CanvasFindPin(c, c->HoveredPin);
        if (pf != nullptr && pt != nullptr && pf->Kind == ImGui::ImGuiCanvasPin_In && pt->Kind == ImGui::ImGuiCanvasPin_Out)
        {
          c->CreatedPinA = pt->Id;
          c->CreatedPinB = pf->Id;
        }
        else
        {
          c->CreatedPinA = c->DragWireFromPin;
          c->CreatedPinB = c->HoveredPin;
        }
      }
      else
      {
        c->WireDroppedReq = true;
        c->DroppedFromPin = c->DragWireFromPin;
        c->DroppedModel = ImGui::CanvasFromScreen(c, mouse);
      }
      c->DragWireFromPin = -1;
      c->Interaction = ImGuiCanvasInteraction_None;
    }
    break;

  case ImGuiCanvasInteraction_MenuPending:
  {
    // Click-vs-pan slop in screen px, font-derived so it tracks DPI/font scale.
    const float slop = ImGui::GetFontSize() * 0.1875f;
    if (c->IO.RmbPans && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
      const ImVec2 travel = mouse - c->GestureStartMouse;
      if (travel.x * travel.x + travel.y * travel.y > slop * slop)
        c->Pan = c->GestureStartPan + travel;   // became a pan; stays in MenuPending, travel keeps panning
    }
    else if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
      const ImVec2 travel = mouse - c->GestureStartMouse;
      if (travel.x * travel.x + travel.y * travel.y <= slop * slop)
      {
        if (c->HoveredNode >= 0)      { c->MenuNodeReq = true; c->MenuNodeId = c->HoveredNode; }
        else if (c->HoveredWire >= 0) { c->MenuWireReq = true; c->MenuWireId = c->HoveredWire; }
        else                          { c->MenuEmptyReq = true; c->MenuEmptyModel = ImGui::CanvasFromScreen(c, mouse); }
      }
      c->Interaction = ImGuiCanvasInteraction_None;
    }
    else
    {
      c->Interaction = ImGuiCanvasInteraction_None;
    }
    break;
  }

  default:
    break;
  }
}

//-----------------------------------------------------------------------------
// [SECTION] Nodes (begin/end, same-frame measurement, drawing)
//-----------------------------------------------------------------------------

namespace ImGui
{
  void CanvasBegin(ImGuiCanvasState* c, const char* str_id, ImVec2 size)
  {
    IM_ASSERT(c != nullptr && !c->InsideCanvas);
    c->InsideCanvas = true;

    // Latched events die at the next frame's begin.
    c->MenuNodeReq = c->MenuWireReq = c->MenuEmptyReq = false;
    c->WireCreatedReq = c->WireDroppedReq = c->WireDetachedReq = false;
    c->NodeDblClickReq = false;
    c->Wires.resize(0);
    c->SolidRects.resize(0);
    for (int i = 0; i < c->Pins.Size; i++)
      c->Pins.Data[i].WiredCount = 0;

    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    PushStyleColor(ImGuiCol_ChildBg, c->Style.GridBg);
    BeginChild(str_id, size, ImGuiChildFlags_None,
               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    c->Origin = GetCursorScreenPos();
    c->CanvasSize = GetWindowSize();
    c->DrawList = GetWindowDrawList();
    // Captured once per frame: every camera conversion this frame uses the same scale, even if
    // the window changed monitors this frame (docs/phase-coherence.md).
    c->FontRatio = GetStyle().FontSizeBase > 0.0f ? GetFontSize() / GetStyle().FontSizeBase : 1.0f;
    c->Splitter.Split(c->DrawList, 3);   // 0 = grid + wires, 1 = node plates, 2 = node content
    c->Splitter.SetCurrentChannel(c->DrawList, 0);

    // Grid: model spacing x zoom, offset by pan; all current-frame values.
    if (c->Style.GridLines && c->Style.GridSpacing > 0.0f)
    {
      const float step = c->Style.GridSpacing * CanvasScale(c);
      if (step >= GetFontSize() * 0.25f)   // hide the grid when lines would pack too densely on screen
      {
        for (float x = ImFmod(c->Pan.x, step); x < c->CanvasSize.x; x += step)
          c->DrawList->AddLine(ImVec2(c->Origin.x + x, c->Origin.y), ImVec2(c->Origin.x + x, c->Origin.y + c->CanvasSize.y), c->Style.GridLine);
        for (float y = ImFmod(c->Pan.y, step); y < c->CanvasSize.y; y += step)
          c->DrawList->AddLine(ImVec2(c->Origin.x, c->Origin.y + y), ImVec2(c->Origin.x + c->CanvasSize.x, c->Origin.y + y), c->Style.GridLine);
      }
    }

    // Interaction catch-all: one invisible item spanning the canvas. Node-body widgets submit
    // AFTER it and win hover (last-wins); the catch-all receives empty-canvas and node-plate
    // gestures (plates are draw-list primitives, not items).
    SetNextItemAllowOverlap();
    InvisibleButton("##canvas_input", ImVec2(ImMax(1.0f, c->CanvasSize.x), ImMax(1.0f, c->CanvasSize.y)));
    const bool item_hovered = IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool item_activated = IsItemActivated() || (item_hovered && (IsMouseClicked(ImGuiMouseButton_Left) || IsMouseClicked(ImGuiMouseButton_Right)));
    CanvasUpdateInput(c, item_hovered, item_activated);

    SetCursorScreenPos(c->Origin);
    c->SubmitOrderNow.resize(0);
  }

  ImDrawList* CanvasBackgroundDrawList(ImGuiCanvasState* c)
  {
    IM_ASSERT(c->InsideCanvas && c->CurNode == -1);
    c->Splitter.SetCurrentChannel(c->DrawList, 0);
    return c->DrawList;
  }

  ImDrawList* CanvasAnnotationDrawList(ImGuiCanvasState* c)
  {
    IM_ASSERT(!c->InsideCanvas && c->DrawList != nullptr && "post-CanvasEnd only: appends above the merged channels");
    return c->DrawList;
  }

  void CanvasNextNodeTitle(ImGuiCanvasState* c, const char* title, ImU32 title_color)
  {
    if (title != nullptr)
      ImStrncpy(c->NextTitle, title, IM_ARRAYSIZE(c->NextTitle));
    else
      c->NextTitle[0] = 0;
    c->NextTitleColor = title_color;
    c->NextEditBuf = nullptr;
    c->NextEditBufSize = 0;
    c->NextEditFlag = nullptr;
  }

  void CanvasNextNodeTitleTag(ImGuiCanvasState* c, const char* tag)
  {
    ImStrncpy(c->NextTitleTag, tag != nullptr ? tag : "", IM_ARRAYSIZE(c->NextTitleTag));
  }

  void CanvasNextNodeOriginDot(ImGuiCanvasState* c, ImU32 color, bool ring)
  {
    c->NextOriginDot = color;
    c->NextDotRing = ring;
  }

  void CanvasNextNodeTitleBadge(ImGuiCanvasState* c, const char* badge)
  {
    ImStrncpy(c->NextBadge, badge != nullptr ? badge : "", IM_ARRAYSIZE(c->NextBadge));
  }

  void CanvasNextNodeRounding(ImGuiCanvasState* c, float model_rounding)
  {
    c->NextRounding = model_rounding;
  }

  void CanvasNextNodeWidth(ImGuiCanvasState* c, float model_width)
  {
    c->NextFixedWidth = model_width;
  }

  float CanvasNodeNeededWidth(const ImGuiCanvasState* c, int node_id)
  {
    const ImGuiCanvasNodeRec* n = CanvasFindNode(const_cast<ImGuiCanvasState*>(c), node_id);
    return n != nullptr ? n->NeededW : 0.0f;
  }

  void CanvasNextNodeHeaderRule(ImGuiCanvasState* c, int rule, ImU32 color)
  {
    c->NextHeaderRule = rule;
    c->NextHeaderRuleColor = color;
  }

  void CanvasNextNodeEdgeStripe(ImGuiCanvasState* c, int side, ImU32 color, float model_thickness)
  {
    c->NextStripeSide = side;
    c->NextStripeColor = color;
    c->NextStripeThick = model_thickness;
  }

  void CanvasNextNodeAlpha(ImGuiCanvasState* c, float alpha)
  {
    c->NextAlpha = ImClamp(alpha, 0.0f, 1.0f);
  }

  void CanvasNextNodeTitleEditable(ImGuiCanvasState* c, char* buf, int buf_size, bool* editing, ImU32 title_color)
  {
    // An editable title always occupies its band, even when blank.
    ImStrncpy(c->NextTitle, buf != nullptr && buf[0] ? buf : " ", IM_ARRAYSIZE(c->NextTitle));
    c->NextTitleColor = title_color;
    c->NextEditBuf = buf;
    c->NextEditBufSize = buf_size;
    c->NextEditFlag = editing;
  }

  bool CanvasBeginNode(ImGuiCanvasState* c, int node_id)
  {
    IM_ASSERT(c->InsideCanvas && c->CurNode == -1);
    ImGuiCanvasNodeRec* n = CanvasFindOrCreateNode(c, node_id);
    n->LastFrame = GetFrameCount();
    ImStrncpy(n->Title, c->NextTitle, IM_ARRAYSIZE(n->Title));
    n->TitleColor = c->NextTitleColor;
    ImStrncpy(n->KindTag, c->NextTitleTag, IM_ARRAYSIZE(n->KindTag));
    ImStrncpy(n->Badge, c->NextBadge, IM_ARRAYSIZE(n->Badge));
    n->OriginDot = c->NextOriginDot;
    n->DotRing = c->NextDotRing;
    n->Rounding = c->NextRounding;
    n->FixedWidth = c->NextFixedWidth;
    n->HeaderRule = c->NextHeaderRule;
    n->HeaderRuleColor = c->NextHeaderRuleColor;
    n->StripeSide = c->NextStripeSide;
    n->StripeColor = c->NextStripeColor;
    n->StripeThick = c->NextStripeThick;
    n->Alpha = c->NextAlpha;
    c->NextTitle[0] = 0;
    c->NextTitleColor = 0;
    c->NextTitleTag[0] = 0;
    c->NextBadge[0] = 0;
    c->NextOriginDot = 0;
    c->NextDotRing = false;
    c->NextRounding = -1.0f;
    c->NextFixedWidth = -1.0f;
    c->NextHeaderRule = 0;
    c->NextHeaderRuleColor = 0;
    c->NextStripeSide = -1;
    c->NextStripeColor = 0;
    c->NextStripeThick = 0.0f;
    c->NextAlpha = 1.0f;

    // Rename hook: capture the host's edit binding for THIS node (pointers live for the frame only).
    c->EditNodeIdx = -1;
    if (c->NextEditFlag != nullptr && c->NextEditBuf != nullptr)
    {
      c->EditBuf = c->NextEditBuf;
      c->EditBufSize = c->NextEditBufSize;
      c->EditFlag = c->NextEditFlag;
      c->EditNodeIdx = c->NodeIdx.GetInt((ImGuiID)node_id, 0) - 1;
      if (*c->NextEditFlag && c->LastEditingNodeId != node_id)
      {
        c->EditFocusPending = true;      // focus once, when the edit state ARRIVES on this node
        c->LastEditingNodeId = node_id;
      }
      else if (!*c->NextEditFlag && c->LastEditingNodeId == node_id)
      {
        c->LastEditingNodeId = -1;
      }
    }
    c->NextEditBuf = nullptr;
    c->NextEditBufSize = 0;
    c->NextEditFlag = nullptr;

    c->CurNode = c->NodeIdx.GetInt((ImGuiID)node_id, 0) - 1;
    c->SubmitOrderNow.push_back(node_id);
    c->CurNodeScreen = CanvasToScreen(c, n->Pos);
    c->CurNodePins.resize(0);

    // Content renders under the zoomed font + zoom-scaled layout metrics; the engine owns the
    // scaling, hosts submit plain widgets (docs/phase-coherence.md rule 1).
    c->Splitter.SetCurrentChannel(c->DrawList, 2);
    // Host style metrics and font already carry the DPI/user font scale, so they take the LOGICAL
    // zoom only; canvas-style model metrics take the full scale (Zoom * FontRatio).
    const float z = c->Zoom;
    const ImGuiStyle& gs = GetStyle();
    PushStyleVar(ImGuiStyleVar_FramePadding,     ImVec2(gs.FramePadding.x * z, gs.FramePadding.y * z));
    PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2(gs.ItemSpacing.x * z, gs.ItemSpacing.y * z));
    PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(gs.ItemInnerSpacing.x * z, gs.ItemInnerSpacing.y * z));
    PushStyleVar(ImGuiStyleVar_IndentSpacing,    gs.IndentSpacing * z);
    PushStyleVar(ImGuiStyleVar_FrameRounding,    gs.FrameRounding * z);
    PushStyleVar(ImGuiStyleVar_Alpha,            gs.Alpha * n->Alpha);   // content shares the plate's disabled look
    PushFont(nullptr, GetFontSize() * z);

    const float title_h = n->Title[0] ? GetFrameHeight() : 0.0f;
    const ImVec2 content_origin = c->CurNodeScreen + c->Style.NodePadding * CanvasScale(c) + ImVec2(0.0f, title_h);
    SetCursorScreenPos(content_origin);
    PushID(node_id);
    BeginGroup();
    // An EMPTY group inherits the surrounding child's cursor extents as its item rect, and the
    // same-frame measurement would adopt that as the node size. Anchor the group so a bodiless
    // node measures title-only.
    Dummy(ImVec2(0.0f, 0.0f));
    SameLine(0.0f, 0.0f);
    Dummy(ImVec2(0.0f, 0.0f));
    return true;
  }

  void CanvasEndNode(ImGuiCanvasState* c)
  {
    IM_ASSERT(c->InsideCanvas && c->CurNode >= 0);
    ImGuiCanvasNodeRec* n = &c->Nodes.Data[c->CurNode];

    EndGroup();
    const ImVec2 content_mn = GetItemRectMin();
    const ImVec2 content_mx = GetItemRectMax();
    const float  z = CanvasScale(c);
    const float  title_h = n->Title[0] ? GetFrameHeight() : 0.0f;   // still under the zoomed font

    // Same-frame measurement at the same model->screen scale the content rendered with: the node
    // font is pushed at GetFontSize() * Zoom and GetFontSize already carries FontRatio, so content
    // screen size is model * (Zoom * FontRatio). Dividing by CanvasScale is exact, not c->Zoom.
    ImVec2 content_px(ImMax(content_mx.x - content_mn.x, GetFontSize() * 2.0f), ImMax(content_mx.y - content_mn.y, 0.0f));
    // The title band participates in width: dot + name + badge + kind word must fit the plate,
    // whatever the body measures.
    if (n->Title[0])
    {
      float title_need = CalcTextSize(n->Title).x;
      if (n->OriginDot != 0)
        title_need += GetFontSize() * 0.6f;
      if (n->Badge[0])
        title_need += CalcTextSize(n->Badge).x + GetFontSize() * 0.9f;
      if (n->KindTag[0])
        title_need += CalcTextSize(n->KindTag).x + GetFontSize() * 1.2f;
      content_px.x = ImMax(content_px.x, title_need);
    }
    ImVec2 fresh((content_px.x + c->Style.NodePadding.x * z * 2.0f) / z,
                 (content_px.y + c->Style.NodePadding.y * z * 2.0f + title_h) / z);
    n->NeededW = fresh.x;
    if (n->FixedWidth > 0.0f)
      fresh.x = n->FixedWidth;
    // Deadband (kNoiseM): zoom changes perturb glyph rasterization and pixel snapping, so the
    // px/scale round-trip re-measures a hair differently per wheel tick. The stored model size
    // moves only when the measurement exceeds the noise bound -- every consumer (layout, group
    // frames, wire routing) reads a zoom-idempotent size, and genuine content growth still
    // propagates in one frame (docs/phase-coherence.md section 1b).
    const float kNoiseM = 2.0f;
    if (n->Size.x <= 0.0f || n->Size.y <= 0.0f
        || ImFabs(fresh.x - n->Size.x) > kNoiseM || ImFabs(fresh.y - n->Size.y) > kNoiseM)
      n->Size = fresh;

    // Pin anchors resolve NOW, with the final node size known; model units, this frame. Left/Right sit on
    // a vertical edge at their row's center (y already set by CanvasEndPin). Top/Bottom are edge-centered
    // singletons (row-less CanvasEdgePin): centered on x, pinned to the top/bottom edge on y.
    for (int i = 0; i < c->CurNodePins.Size; i++)
    {
      ImGuiCanvasPinRec* p = &c->Pins.Data[c->CurNodePins.Data[i]];
      switch (p->Side)
      {
      case ImGuiCanvasPinSide_Left:   p->Anchor.x = n->Pos.x; break;
      case ImGuiCanvasPinSide_Right:  p->Anchor.x = n->Pos.x + n->Size.x; break;
      case ImGuiCanvasPinSide_Top:    p->Anchor.x = n->Pos.x + n->Size.x * 0.5f; p->Anchor.y = n->Pos.y; break;
      case ImGuiCanvasPinSide_Bottom: p->Anchor.x = n->Pos.x + n->Size.x * 0.5f; p->Anchor.y = n->Pos.y + n->Size.y; break;
      }
    }

    // Plate + title into the plate channel, under the content.
    const ImVec2 mn = c->CurNodeScreen;
    const ImVec2 mx = mn + n->Size * z;
    const bool hovered = c->HoveredNode == n->Id;
    const bool selected = CanvasIsSelected(c, n->Id);
    const float rounding = (n->Rounding >= 0.0f ? n->Rounding : c->Style.NodeRounding) * z;
    const float na = n->Alpha;
    c->Splitter.SetCurrentChannel(c->DrawList, 1);
    c->DrawList->AddRectFilled(mn, mx, CanvasColMulAlpha(selected ? c->Style.NodeBgSelected : hovered ? c->Style.NodeBgHovered : c->Style.NodeBg, na), rounding);
    const bool editing_title = c->EditNodeIdx == c->CurNode && c->EditFlag != nullptr && *c->EditFlag && title_h > 0.0f;
    if (title_h > 0.0f)
    {
      const ImU32 tb = n->TitleColor != 0 ? n->TitleColor
                     : selected ? c->Style.TitleBarSelected : hovered ? c->Style.TitleBarHovered : c->Style.TitleBar;
      c->DrawList->AddRectFilled(mn, ImVec2(mx.x, mn.y + title_h), CanvasColMulAlpha(tb, na), rounding, ImDrawFlags_RoundCornersTop);
      if (!editing_title)
      {
        float text_x = mn.x + c->Style.NodePadding.x * z;
        if (n->OriginDot != 0)
        {
          const float r = GetFontSize() * 0.17f;
          const ImVec2 dc(text_x + r, mn.y + title_h * 0.5f);
          if (n->DotRing)
            c->DrawList->AddCircle(dc, r, CanvasColMulAlpha(n->OriginDot, na), 0, ImMax(1.2f, r * 0.7f));
          else
            c->DrawList->AddCircleFilled(dc, r, CanvasColMulAlpha(n->OriginDot, na));
          text_x += r * 2.0f + GetFontSize() * 0.25f;
        }
        c->DrawList->AddText(ImVec2(text_x, mn.y + (title_h - GetFontSize()) * 0.5f), CanvasColMulAlpha(c->Style.TitleText, na), n->Title);
        float after_x = text_x + CalcTextSize(n->Title).x;
        if (n->Badge[0])
        {
          const ImVec2 bs = CalcTextSize(n->Badge);
          const float pad = GetFontSize() * 0.25f;
          const ImVec2 bmn(after_x + GetFontSize() * 0.4f, mn.y + (title_h - (bs.y + pad)) * 0.5f);
          const ImVec2 bmx(bmn.x + bs.x + pad * 2.0f, bmn.y + bs.y + pad);
          c->DrawList->AddRectFilled(bmn, bmx, CanvasColMulAlpha(IM_COL32(0, 0, 0, 70), na), 3.0f * z);
          c->DrawList->AddRect(bmn, bmx, CanvasColMulAlpha(IM_COL32(255, 255, 255, 40), na), 3.0f * z);
          c->DrawList->AddText(ImVec2(bmn.x + pad, bmn.y + pad * 0.5f), CanvasColMulAlpha((c->Style.TitleText & 0x00FFFFFF) | 0xCC000000, na), n->Badge);
          after_x = bmx.x;
        }
        if (n->KindTag[0])
        {
          // The kind word, muted, right-aligned; dropped when the title leaves it no room.
          const ImVec2 ts = CalcTextSize(n->KindTag);
          const float tag_x = mx.x - c->Style.NodePadding.x * z - ts.x;
          if (tag_x > after_x + GetFontSize())
            c->DrawList->AddText(ImVec2(tag_x, mn.y + (title_h - GetFontSize()) * 0.5f),
                                 CanvasColMulAlpha((c->Style.TitleText & 0x00FFFFFF) | 0x66000000, na), n->KindTag);
        }
      }
      if (n->HeaderRule != 0)
      {
        const float ry = mn.y + title_h;
        const ImU32 rc = CanvasColMulAlpha(n->HeaderRuleColor != 0 ? n->HeaderRuleColor : c->Style.NodeOutline, na);
        if (n->HeaderRule == 1)
          c->DrawList->AddLine(ImVec2(mn.x, ry), ImVec2(mx.x, ry), rc, ImMax(1.0f, 2.0f * z));
        else
          for (float x = mn.x; x < mx.x; x += 8.0f * z)
            c->DrawList->AddLine(ImVec2(x, ry), ImVec2(ImMin(x + 4.5f * z, mx.x), ry), rc, ImMax(1.0f, 1.5f * z));
      }
    }
    if (n->StripeSide >= 0 && n->StripeThick > 0.0f)
    {
      const float t = n->StripeThick * z;
      const ImU32 sc = CanvasColMulAlpha(n->StripeColor, na);
      switch (n->StripeSide)
      {
      case ImGuiCanvasPinSide_Left:   c->DrawList->AddRectFilled(mn, ImVec2(mn.x + t, mx.y), sc, rounding, ImDrawFlags_RoundCornersLeft); break;
      case ImGuiCanvasPinSide_Right:  c->DrawList->AddRectFilled(ImVec2(mx.x - t, mn.y), mx, sc, rounding, ImDrawFlags_RoundCornersRight); break;
      case ImGuiCanvasPinSide_Top:    c->DrawList->AddRectFilled(mn, ImVec2(mx.x, mn.y + t), sc, rounding, ImDrawFlags_RoundCornersTop); break;
      case ImGuiCanvasPinSide_Bottom: c->DrawList->AddRectFilled(ImVec2(mn.x, mx.y - t), mx, sc, rounding, ImDrawFlags_RoundCornersBottom); break;
      }
    }
    c->DrawList->AddRect(mn, mx, CanvasColMulAlpha(selected ? c->Style.NodeOutlineSelected : c->Style.NodeOutline, na), rounding, 0,
                         ImMax(1.0f, c->Style.NodeBorder * z));
    c->Splitter.SetCurrentChannel(c->DrawList, 2);

    // Rename in place: an InputText over the title band, bound to the host's buffer; deactivation
    // clears the host's edit flag. Still under the zoomed font.
    if (editing_title)
    {
      SetCursorScreenPos(ImVec2(mn.x + c->Style.NodePadding.x * z, mn.y + ImMax(0.0f, (title_h - GetFrameHeight()) * 0.5f)));
      SetNextItemWidth(ImMax(GetFontSize() * 3.0f, (mx.x - mn.x) - c->Style.NodePadding.x * z * 2.0f));
      if (c->EditFocusPending)
      {
        SetKeyboardFocusHere();
        c->EditFocusPending = false;
      }
      PushStyleColor(ImGuiCol_FrameBg, c->Style.TitleEditBg);
      InputText("##canvas_title_edit", c->EditBuf, (size_t)c->EditBufSize, ImGuiInputTextFlags_AutoSelectAll);
      const bool done = IsItemDeactivated();
      PopStyleColor();
      if (done)
        *c->EditFlag = false;
    }

    PopID();
    PopFont();
    PopStyleVar(6);
    c->CurNode = -1;
  }

  // Pin submission (inside a node): Begin marks the row start, End records the row's vertical
  // center in MODEL units; the horizontal edge resolves in CanvasEndNode once the width is known.

  void CanvasNextWireDashed(ImGuiCanvasState* c)
  {
    c->NextWireDashed = true;
  }

  void CanvasNextPinColor(ImGuiCanvasState* c, ImU32 color)
  {
    c->NextPinColor = color;
  }

  void CanvasNextPinSide(ImGuiCanvasState* c, int side)
  {
    c->NextPinSide = side;
  }

  // Side default: In->Left, Out->Right unless CanvasNextPinSide overrode it. Back-compat: every existing
  // In/Out caller stays on its Left/Right edge with no change.
  static int CanvasResolvePinSide(ImGuiCanvasState* c, int kind)
  {
    const int side = c->NextPinSide >= 0 ? c->NextPinSide
                   : (kind == ImGuiCanvasPin_In ? ImGuiCanvasPinSide_Left : ImGuiCanvasPinSide_Right);
    c->NextPinSide = -1;
    return side;
  }

  void CanvasBeginPin(ImGuiCanvasState* c, int pin_id, int kind, int shape)
  {
    IM_ASSERT(c->InsideCanvas && c->CurNode >= 0 && c->CurPin == -1);
    ImGuiCanvasPinRec* p = CanvasFindOrCreatePin(c, pin_id);
    p->NodeId = c->Nodes.Data[c->CurNode].Id;
    p->Kind = kind;
    p->Side = CanvasResolvePinSide(c, kind);
    p->Shape = shape;
    p->Color = c->NextPinColor;
    c->NextPinColor = 0;
    p->LastFrame = GetFrameCount();
    c->CurPin = c->PinIdx.GetInt((ImGuiID)pin_id, 0) - 1;
    c->CurPinY0 = GetCursorScreenPos().y;
  }

  // Row-less edge pin: no widget, no cursor use. Anchor.y for Top/Bottom is filled in CanvasEndNode once
  // the node's size is known; here we only record identity + side and enlist it for that resolution.
  void CanvasEdgePin(ImGuiCanvasState* c, int pin_id, int kind, int shape, int side)
  {
    IM_ASSERT(c->InsideCanvas && c->CurNode >= 0);
    ImGuiCanvasPinRec* p = CanvasFindOrCreatePin(c, pin_id);
    p->NodeId = c->Nodes.Data[c->CurNode].Id;
    p->Kind = kind;
    p->Side = side;
    p->Shape = shape;
    p->Color = c->NextPinColor;
    c->NextPinColor = 0;
    c->NextPinSide = -1;
    p->LastFrame = GetFrameCount();
    c->CurNodePins.push_back(c->PinIdx.GetInt((ImGuiID)pin_id, 0) - 1);
  }

  void CanvasEndPin(ImGuiCanvasState* c)
  {
    IM_ASSERT(c->InsideCanvas && c->CurPin >= 0);
    ImGuiCanvasPinRec* p = &c->Pins.Data[c->CurPin];
    float y1 = GetCursorScreenPos().y;
    if (y1 <= c->CurPinY0)
      y1 = c->CurPinY0 + GetTextLineHeight();
    const float yc = (c->CurPinY0 + y1 - GetStyle().ItemSpacing.y) * 0.5f;    // row center, minus the trailing spacing
    p->Anchor.y = (yc - c->Origin.y - c->Pan.y) / CanvasScale(c);             // screen -> model, this frame's camera
    c->CurNodePins.push_back(c->CurPin);
    c->CurPin = -1;
  }

  void CanvasWire(ImGuiCanvasState* c, int wire_id, int pin_a, int pin_b, ImU32 color)
  {
    IM_ASSERT(c->InsideCanvas);
    ImGuiCanvasWireRec rec;
    rec.Id = wire_id;
    rec.PinA = pin_a;
    rec.PinB = pin_b;
    rec.Color = color;
    rec.Dashed = c->NextWireDashed;
    c->NextWireDashed = false;
    c->Wires.push_back(rec);
    if (ImGuiCanvasPinRec* pa = CanvasFindPin(c, pin_a)) pa->WiredCount++;
    if (ImGuiCanvasPinRec* pb = CanvasFindPin(c, pin_b)) pb->WiredCount++;
  }

  bool CanvasWireExists(const ImGuiCanvasState* c, int wire_id)
  {
    for (int i = 0; i < c->Wires.Size; i++)
      if (c->Wires.Data[i].Id == wire_id)
        return true;
    return false;
  }

  ImVec2 CanvasPinPos(const ImGuiCanvasState* c, int pin_id)
  {
    const ImGuiCanvasPinRec* p = CanvasFindPin(const_cast<ImGuiCanvasState*>(c), pin_id);
    return p != nullptr ? p->Anchor : ImVec2(0.0f, 0.0f);
  }

//-----------------------------------------------------------------------------
// [SECTION] Frame end + queries
//-----------------------------------------------------------------------------

  void CanvasEnd(ImGuiCanvasState* c)
  {
    IM_ASSERT(c->InsideCanvas && c->CurNode == -1 && c->CurPin == -1);
    const float  z = CanvasScale(c);
    const ImVec2 mouse = GetIO().MousePos;
    const int    frame = GetFrameCount();

    // Hover against THIS frame's geometry: pins beat nodes beat wires. Feeds this frame's draw
    // colors and the next frame's press decisions. The minimap is an overlay with its own
    // interaction: nothing under it hovers.
    const bool over_minimap = mouse.x >= c->MiniRectMin.x && mouse.x < c->MiniRectMax.x
                           && mouse.y >= c->MiniRectMin.y && mouse.y < c->MiniRectMax.y;
    c->HoveredPin = -1;
    if (!over_minimap)
    {
      float best = c->Style.PinHoverRadius * z;
      best *= best;
      for (int i = 0; i < c->Pins.Size; i++)
      {
        const ImGuiCanvasPinRec* p = &c->Pins.Data[i];
        if (p->LastFrame != frame)
          continue;
        const ImVec2 s = CanvasToScreen(c, p->Anchor);
        const float d2 = (mouse.x - s.x) * (mouse.x - s.x) + (mouse.y - s.y) * (mouse.y - s.y);
        if (d2 <= best)
        {
          best = d2;
          c->HoveredPin = p->Id;
        }
      }
    }
    c->HoveredWire = -1;
    if (!over_minimap && c->HoveredPin == -1 && c->HoveredNode == -1)
    {
      float reach = ImMax(GetFontSize() * 0.375f, c->Style.WireThickness * z * 2.0f);
      reach *= reach;
      float best = reach;
      for (int i = 0; i < c->Wires.Size; i++)
      {
        const ImGuiCanvasPinRec* pa = CanvasFindPin(c, c->Wires.Data[i].PinA);
        const ImGuiCanvasPinRec* pb = CanvasFindPin(c, c->Wires.Data[i].PinB);
        if (pa == nullptr || pb == nullptr)
          continue;
        const ImVec2 a = CanvasToScreen(c, pa->Anchor);
        const ImVec2 b = CanvasToScreen(c, pb->Anchor);
        ImVec2 c0, c1;
        CanvasWireControls(c, a, pa->Side, b, pb->Side, &c0, &c1);
        const float d2 = CanvasWireDistanceSq(mouse, a, c0, c1, b);
        if (d2 <= best)
        {
          best = d2;
          c->HoveredWire = c->Wires.Data[i].Id;
        }
      }
    }

    // Wires draw in channel 0: under the node plates, above the grid. A dashed body is the
    // form carrier of an optional dependency.
    c->Splitter.SetCurrentChannel(c->DrawList, 0);
    for (int i = 0; i < c->Wires.Size; i++)
    {
      const ImGuiCanvasWireRec* w = &c->Wires.Data[i];
      const ImGuiCanvasPinRec* pa = CanvasFindPin(c, w->PinA);
      const ImGuiCanvasPinRec* pb = CanvasFindPin(c, w->PinB);
      if (pa == nullptr || pb == nullptr)
        continue;
      const ImVec2 a = CanvasToScreen(c, pa->Anchor);
      const ImVec2 b = CanvasToScreen(c, pb->Anchor);
      ImVec2 c0, c1;
      CanvasWireControls(c, a, pa->Side, b, pb->Side, &c0, &c1);
      const bool hov = w->Id == c->HoveredWire;
      const bool sel = w->Id == c->SelectedWire;
      const ImU32 col = sel ? c->Style.WireSelected : hov ? c->Style.WireHovered : (w->Color != 0 ? w->Color : c->Style.Wire);
      const float th = ImMax(1.0f, c->Style.WireThickness * z * (hov || sel ? 1.4f : 1.0f));
      if (!w->Dashed)
      {
        c->DrawList->AddBezierCubic(a, c0, c1, b, col, th);
      }
      else
      {
        const int segs = 26;
        ImVec2 prev = a;
        for (int s = 1; s <= segs; s++)
        {
          const float t = (float)s / (float)segs;
          const float u = 1.0f - t;
          const ImVec2 pt(u * u * u * a.x + 3.0f * u * u * t * c0.x + 3.0f * u * t * t * c1.x + t * t * t * b.x,
                          u * u * u * a.y + 3.0f * u * u * t * c0.y + 3.0f * u * t * t * c1.y + t * t * t * b.y);
          if (s & 1)
            c->DrawList->AddLine(prev, pt, col, th);
          prev = pt;
        }
      }
    }

    c->Splitter.Merge(c->DrawList);

    // Terminal segments re-draw ABOVE the merged plates: every wire visibly leaves and lands in
    // its pin hole; only the wire's body is occluded by nodes it crosses.
    for (int i = 0; i < c->Wires.Size; i++)
    {
      const ImGuiCanvasWireRec* w = &c->Wires.Data[i];
      const ImGuiCanvasPinRec* pa = CanvasFindPin(c, w->PinA);
      const ImGuiCanvasPinRec* pb = CanvasFindPin(c, w->PinB);
      if (pa == nullptr || pb == nullptr)
        continue;
      const ImVec2 a = CanvasToScreen(c, pa->Anchor);
      const ImVec2 b = CanvasToScreen(c, pb->Anchor);
      ImVec2 c0, c1;
      CanvasWireControls(c, a, pa->Side, b, pb->Side, &c0, &c1);
      const bool hov = w->Id == c->HoveredWire;
      const bool sel = w->Id == c->SelectedWire;
      const ImU32 col = sel ? c->Style.WireSelected : hov ? c->Style.WireHovered : (w->Color != 0 ? w->Color : c->Style.Wire);
      const float th = ImMax(1.0f, c->Style.WireThickness * z * (hov || sel ? 1.4f : 1.0f));
      const int steps = 6;
      const float span = 0.12f;
      ImVec2 prev = a;
      for (int s = 1; s <= steps; s++)
      {
        const float t = span * (float)s / (float)steps;
        const float u = 1.0f - t;
        const ImVec2 pt(u * u * u * a.x + 3.0f * u * u * t * c0.x + 3.0f * u * t * t * c1.x + t * t * t * b.x,
                        u * u * u * a.y + 3.0f * u * u * t * c0.y + 3.0f * u * t * t * c1.y + t * t * t * b.y);
        c->DrawList->AddLine(prev, pt, col, th);
        prev = pt;
      }
      prev = b;
      for (int s = 1; s <= steps; s++)
      {
        const float t = 1.0f - span * (float)s / (float)steps;
        const float u = 1.0f - t;
        const ImVec2 pt(u * u * u * a.x + 3.0f * u * u * t * c0.x + 3.0f * u * t * t * c1.x + t * t * t * b.x,
                        u * u * u * a.y + 3.0f * u * u * t * c0.y + 3.0f * u * t * t * c1.y + t * t * t * b.y);
        c->DrawList->AddLine(prev, pt, col, th);
        prev = pt;
      }
    }
    c->SubmitOrder = c->SubmitOrderNow;   // this frame's z-order becomes the hit-test order
    c->WiresPrev = c->Wires;              // press decisions at the next CanvasBegin walk these
    c->SolidRectsPrev = c->SolidRects;    // next frame's solid-drag clamp walks these

    // Pins draw over everything, post-merge.
    for (int i = 0; i < c->Pins.Size; i++)
    {
      const ImGuiCanvasPinRec* p = &c->Pins.Data[i];
      if (p->LastFrame != frame)
        continue;
      const ImVec2 s = CanvasToScreen(c, p->Anchor);
      const bool hov = p->Id == c->HoveredPin;
      const ImU32 col = hov ? c->Style.PinHovered
                      : p->Color != 0 ? p->Color
                      : p->Shape == ImGuiCanvasPinShape_Square ? c->Style.PinContainment : c->Style.PinData;
      const float r = c->Style.PinRadius * z * (hov ? 1.35f : 1.0f);
      if (p->Shape == ImGuiCanvasPinShape_Square)
      {
        const ImVec2 h(r, r);
        if (p->WiredCount > 0)
          c->DrawList->AddRectFilled(s - h, s + h, col, r * 0.3f);
        else
          c->DrawList->AddRect(s - h, s + h, col, r * 0.3f, 0, ImMax(1.0f, 1.5f * z));
      }
      else
      {
        if (p->WiredCount > 0)
          c->DrawList->AddCircleFilled(s, r, col);
        else
          c->DrawList->AddCircle(s, r, col, 0, ImMax(1.0f, 1.5f * z));
      }
    }

    // Pending wire while dragging one: origin pin -> mouse (or the snap target pin).
    if (c->Interaction == ImGuiCanvasInteraction_DragWire)
    {
      if (const ImGuiCanvasPinRec* pf = CanvasFindPin(c, c->DragWireFromPin))
      {
        const ImVec2 a = CanvasToScreen(c, pf->Anchor);
        ImVec2 b = mouse;
        int side_b = CanvasOppositeSide(pf->Side);   // free (mouse) end faces the source edge so the preview bows correctly
        if (c->HoveredPin >= 0 && c->HoveredPin != c->DragWireFromPin)
          if (const ImGuiCanvasPinRec* pt = CanvasFindPin(c, c->HoveredPin))
          {
            b = CanvasToScreen(c, pt->Anchor);
            side_b = pt->Side;
          }
        ImVec2 c0, c1;
        CanvasWireControls(c, a, pf->Side, b, side_b, &c0, &c1);
        c->DrawList->AddBezierCubic(a, c0, c1, b, c->Style.WireHovered, ImMax(1.0f, c->Style.WireThickness * z));
      }
    }

    // Minimap. The mapping covers the node content bounds (the viewport when the canvas is empty).
    // Everything below draws through one mapping, which the FSM inverts next frame:
    //   mini = (model - bounds_min) * scaling + content_min
    bool minimap_drawn = false;
    if (c->MiniMapReq)
    {
      c->MiniMapReq = false;
      const int mm_frame = GetFrameCount();
      ImVec2 bmin(FLT_MAX, FLT_MAX), bmax(-FLT_MAX, -FLT_MAX);
      for (int i = 0; i < c->Nodes.Size; i++)
      {
        const ImGuiCanvasNodeRec* n = &c->Nodes.Data[i];
        if (n->LastFrame != mm_frame)
          continue;
        bmin = ImMin(bmin, n->Pos);
        bmax = ImMax(bmax, n->Pos + n->Size);
      }
      if (bmin.x > bmax.x)
      {
        // No nodes: map the current viewport.
        bmin = CanvasFromScreen(c, c->Origin);
        bmax = CanvasFromScreen(c, c->Origin + c->CanvasSize);
      }
      {
        const ImVec2 border(GetFontSize() * 0.5f, GetFontSize() * 0.5f);
        const ImVec2 offset(GetFontSize() * 0.25f, GetFontSize() * 0.25f);
        const ImVec2 max_size = ImFloor(c->CanvasSize * c->MiniMapFraction - border * 2.0f);
        const ImVec2 content_size(ImMax(1.0f, ImFloor(bmax.x - bmin.x)), ImMax(1.0f, ImFloor(bmax.y - bmin.y)));
        const float  max_aspect = max_size.x / ImMax(1.0f, max_size.y);
        const float  content_aspect = content_size.x / content_size.y;
        const ImVec2 mini_size = ImFloor(content_aspect > max_aspect
                                       ? ImVec2(max_size.x, max_size.x / content_aspect)
                                       : ImVec2(max_size.y * content_aspect, max_size.y));
        const float  scaling = mini_size.x / content_size.x;
        const ImVec2 pos = ImFloor(c->Origin + c->CanvasSize - offset - border - mini_size);
        const ImVec2 rmin = pos - border;
        const ImVec2 rmax = pos + mini_size + border;
        c->MiniRectMin = rmin;
        c->MiniRectMax = rmax;
        c->MiniContentMin = pos;
        c->MiniModelMin = bmin;
        c->MiniScale = scaling;
        minimap_drawn = true;
        auto to_mini = [&](ImVec2 m) {
          return ImVec2(pos.x + (m.x - bmin.x) * scaling, pos.y + (m.y - bmin.y) * scaling);
        };

        const bool map_hovered = mouse.x >= rmin.x && mouse.x < rmax.x && mouse.y >= rmin.y && mouse.y < rmax.y;
        c->DrawList->AddRectFilled(rmin, rmax, map_hovered ? c->Style.MiniMapBgHovered : c->Style.MiniMapBg);
        c->DrawList->AddRect(rmin, rmax, c->Style.MiniMapOutline);
        c->DrawList->PushClipRect(rmin, rmax, true);

        // Links first, under the nodes. Control points at 0.25 x length out of the Out pin.
        for (int i = 0; i < c->Wires.Size; i++)
        {
          const ImGuiCanvasPinRec* pa = CanvasFindPin(c, c->Wires.Data[i].PinA);
          const ImGuiCanvasPinRec* pb = CanvasFindPin(c, c->Wires.Data[i].PinB);
          if (pa == nullptr || pb == nullptr)
            continue;
          ImVec2 a = to_mini(pa->Anchor);
          ImVec2 b = to_mini(pb->Anchor);
          if (pa->Kind == ImGuiCanvasPin_In)   // orient start at the Out end
            ImSwap(a, b);
          const float  len = ImSqrt(ImLengthSqr(b - a));
          const ImVec2 ctrl(0.25f * len, 0.0f);
          c->DrawList->AddBezierCubic(a, a + ctrl, b - ctrl, b, c->Style.MiniMapLink,
                                      c->Style.WireThickness * scaling);
        }

        // Nodes: filled + outlined; hovered-in-map beats selected beats default. Rounding floors
        // to whole pixels.
        for (int i = 0; i < c->Nodes.Size; i++)
        {
          const ImGuiCanvasNodeRec* n = &c->Nodes.Data[i];
          if (n->LastFrame != mm_frame)
            continue;
          const ImVec2 nm = to_mini(n->Pos);
          const ImVec2 nx = to_mini(n->Pos + n->Size);
          const bool over = c->Interaction == ImGuiCanvasInteraction_None
                         && mouse.x >= nm.x && mouse.x < nx.x && mouse.y >= nm.y && mouse.y < nx.y;
          const ImU32 fill = over ? c->Style.MiniMapNodeBgHovered
                           : CanvasIsSelected(c, n->Id) ? c->Style.MiniMapNodeBgSelected
                           : c->Style.MiniMapNodeBg;
          const float rounding = ImFloor(c->Style.NodeRounding * scaling);
          c->DrawList->AddRectFilled(nm, nx, fill, rounding);
          c->DrawList->AddRect(nm, nx, c->Style.MiniMapNodeOutline, rounding);
        }

        // Current view rect through the same mapping.
        const ImVec2 vmn = to_mini(CanvasFromScreen(c, c->Origin));
        const ImVec2 vmx = to_mini(CanvasFromScreen(c, c->Origin + c->CanvasSize));
        c->DrawList->AddRectFilled(vmn, vmx, c->Style.MiniMapCanvas);
        c->DrawList->AddRect(vmn, vmx, c->Style.MiniMapCanvasOutline);

        c->DrawList->PopClipRect();
      }
    }
    if (!minimap_drawn)
    {
      c->MiniRectMin = c->MiniRectMax = ImVec2(0.0f, 0.0f);
      c->MiniScale = 0.0f;
    }

    EndChild();
    PopStyleColor();
    PopStyleVar();
    c->InsideCanvas = false;
  }

  ImVec2 CanvasNodePos(const ImGuiCanvasState* c, int node_id)
  {
    const ImGuiCanvasNodeRec* n = CanvasFindNode(const_cast<ImGuiCanvasState*>(c), node_id);
    return n != nullptr ? n->Pos : ImVec2(0.0f, 0.0f);
  }

  void CanvasSetNodePos(ImGuiCanvasState* c, int node_id, ImVec2 model_pos)
  {
    CanvasFindOrCreateNode(c, node_id)->Pos = model_pos;
  }

  ImVec2 CanvasNodeSize(const ImGuiCanvasState* c, int node_id)
  {
    const ImGuiCanvasNodeRec* n = CanvasFindNode(const_cast<ImGuiCanvasState*>(c), node_id);
    return n != nullptr ? n->Size : ImVec2(0.0f, 0.0f);
  }

  const char* CanvasNodeTitleBadge(const ImGuiCanvasState* c, int node_id)
  {
    const ImGuiCanvasNodeRec* n = CanvasFindNode(const_cast<ImGuiCanvasState*>(c), node_id);
    return n != nullptr ? n->Badge : "";
  }

  void CanvasSetNodeSolid(ImGuiCanvasState* c, int node_id, bool solid)
  {
    CanvasFindOrCreateNode(c, node_id)->Solid = solid;
  }

  void CanvasAddSolidRect(ImGuiCanvasState* c, ImVec2 model_min, ImVec2 model_max)
  {
    c->SolidRects.push_back(ImVec4(model_min.x, model_min.y, model_max.x, model_max.y));
  }

  void CanvasSetNodeDraggable(ImGuiCanvasState* c, int node_id, bool draggable)
  {
    CanvasFindOrCreateNode(c, node_id)->Draggable = draggable;
  }

  int CanvasNumSelectedNodes(const ImGuiCanvasState* c) { return c->Selection.Size; }

  void CanvasGetSelectedNodes(const ImGuiCanvasState* c, int* out, int cap)
  {
    for (int i = 0; i < c->Selection.Size && i < cap; i++)
      out[i] = c->Selection.Data[i];
  }

  void CanvasSelectNode(ImGuiCanvasState* c, int node_id, bool additive)
  {
    if (!additive)
      c->Selection.resize(0);
    if (!CanvasIsSelected(c, node_id))
      c->Selection.push_back(node_id);
  }

  void CanvasClearSelection(ImGuiCanvasState* c) { c->Selection.resize(0); }

  int CanvasHoveredNode(const ImGuiCanvasState* c) { return c->HoveredNode; }
  int CanvasHoveredWire(const ImGuiCanvasState* c) { return c->HoveredWire; }
  int CanvasHoveredPin(const ImGuiCanvasState* c)  { return c->HoveredPin; }
  int CanvasSelectedWire(const ImGuiCanvasState* c) { return c->SelectedWire; }
  void CanvasClearWireSelection(ImGuiCanvasState* c) { c->SelectedWire = -1; }

  bool CanvasWireCreated(const ImGuiCanvasState* c, int* out_pin_a, int* out_pin_b)
  {
    if (c->WireCreatedReq)
    {
      if (out_pin_a != nullptr) *out_pin_a = c->CreatedPinA;
      if (out_pin_b != nullptr) *out_pin_b = c->CreatedPinB;
    }
    return c->WireCreatedReq;
  }

  bool CanvasWireDropped(const ImGuiCanvasState* c, int* out_from_pin, ImVec2* out_model_pos)
  {
    if (c->WireDroppedReq)
    {
      if (out_from_pin != nullptr) *out_from_pin = c->DroppedFromPin;
      if (out_model_pos != nullptr) *out_model_pos = c->DroppedModel;
    }
    return c->WireDroppedReq;
  }

  bool CanvasWireDetached(const ImGuiCanvasState* c, int* out_wire_id, int* out_grabbed_end_pin)
  {
    if (c->WireDetachedReq)
    {
      if (out_wire_id != nullptr) *out_wire_id = c->DetachedWireId;
      if (out_grabbed_end_pin != nullptr) *out_grabbed_end_pin = c->DetachedGrabbedPin;
    }
    return c->WireDetachedReq;
  }

  bool CanvasNodeDoubleClicked(const ImGuiCanvasState* c, int* out_node_id)
  {
    if (c->NodeDblClickReq && out_node_id != nullptr)
      *out_node_id = c->NodeDblClickId;
    return c->NodeDblClickReq;
  }

  bool CanvasMenuRequestNode(const ImGuiCanvasState* c, int* out_node_id)
  {
    if (c->MenuNodeReq && out_node_id != nullptr)
      *out_node_id = c->MenuNodeId;
    return c->MenuNodeReq;
  }

  bool CanvasMenuRequestWire(const ImGuiCanvasState* c, int* out_wire_id)
  {
    if (c->MenuWireReq && out_wire_id != nullptr)
      *out_wire_id = c->MenuWireId;
    return c->MenuWireReq;
  }

  bool CanvasMenuRequestEmpty(const ImGuiCanvasState* c, ImVec2* out_model_pos)
  {
    if (c->MenuEmptyReq && out_model_pos != nullptr)
      *out_model_pos = c->MenuEmptyModel;
    return c->MenuEmptyReq;
  }
}
