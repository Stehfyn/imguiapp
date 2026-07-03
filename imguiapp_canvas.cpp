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

struct ImGuiCanvasNodeRec
{
  int    Id;
  ImVec2 Pos;              // model units
  ImVec2 Size;             // model units, measured same-frame at submission
  ImU32  TitleColor;       // 0 = style default
  char   Title[64];
  bool   Draggable;
  int    LastFrame;        // ImGui frame count of the last submission (cull/hit bookkeeping)
};

struct ImGuiCanvasPinRec
{
  int    Id;
  int    NodeId;
  int    Kind;             // ImGuiCanvasPin_In / _Out
  int    Shape;            // circle (data) / square (containment)
  ImU32  Color;            // 0 = style (by shape); set via CanvasNextPinColor
  ImVec2 Anchor;           // MODEL units: pin center at the node edge, row-centered
  int    LastFrame;
  int    WiredCount;       // wires touching this pin THIS frame (filled pin glyph)
};

struct ImGuiCanvasWireRec  // per-frame submission, rebuilt every frame
{
  int   Id;
  int   PinA;
  int   PinB;
  ImU32 Color;             // 0 = style
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
  float  FontRatio;    // host font scale (DPI * user scale) captured at CanvasBegin; geometry scale = Zoom * FontRatio

  // Nodes
  ImVector<ImGuiCanvasNodeRec> Nodes;
  ImGuiStorage                 NodeIdx;        // id -> index + 1
  ImVector<int>                SubmitOrder;    // last frame's submission order (z-order; hit-test walks it backward)
  ImVector<int>                SubmitOrderNow; // rebuilt during the current frame

  // Pins + wires
  ImVector<ImGuiCanvasPinRec> Pins;
  ImGuiStorage                PinIdx;          // id -> index + 1
  ImVector<ImGuiCanvasWireRec> Wires;          // rebuilt per frame between CanvasBegin/End
  ImVector<ImGuiCanvasWireRec> WiresPrev;      // last frame's wires: press decisions run at CanvasBegin
  ImVector<int>               CurNodePins;     // pin indices submitted inside the current node (anchor.x resolves at EndNode)

  // Selection + hover
  ImVector<int> Selection;
  int           HoveredNode;                   // resolved against current camera + model geometry
  int           HoveredPin;                    // resolved in CanvasEnd (needs this frame's wires/pins); -1 = none
  int           HoveredWire;
  int           SelectedWire;                  // single wire selection (click); -1 = none

  // Per-frame canvas geometry
  ImVec2      Origin;         // canvas child top-left (screen)
  ImVec2      CanvasSize;
  ImDrawList* DrawList;
  ImDrawListSplitter Splitter;                 // ch0 = grid + wires, ch1 = node plates, ch2 = node content
  bool        InsideCanvas;

  // Node submission scratch
  int    CurNode;             // index into Nodes during Begin/EndNode, -1 otherwise
  ImVec2 CurNodeScreen;       // this frame's screen pos of the node origin
  char   NextTitle[64];
  ImU32  NextTitleColor;

  // Interaction FSM
  int    Interaction;
  ImVec2 GestureStartMouse;
  ImVec2 GestureStartPan;
  ImVector<ImVec2> DragStartPos;               // model pos of each selected node at drag start
  ImVector<int>    DragNodes;
  int    DragWireFromPin;                      // DragWire: the fixed end

  // Rename hook: per-frame pointers captured by CanvasNextNodeTitleEditable (host-owned storage).
  char*  EditBuf;
  int    EditBufSize;
  bool*  EditFlag;
  int    EditNodeIdx;
  bool   EditFocusPending;
  int    LastEditingNodeId;    // focus-once tracking across frames

  // Minimap
  bool   MiniMapReq;
  float  MiniMapFraction;
  ImVec2 MiniRectMin;          // background rect incl. padding ring (screen); the FSM keeps out of it
  ImVec2 MiniRectMax;
  ImVec2 MiniContentMin;       // content rect origin (screen); the mapping's anchor
  ImVec2 MiniModelMin;         // content bounds min (model); the mapping's other anchor
  float  MiniScale;            // model units -> minimap pixels

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

    Pan = ImVec2(0.0f, 0.0f);
    Zoom = 1.0f;
    FontRatio = 1.0f;
    HoveredNode = HoveredPin = HoveredWire = SelectedWire = -1;
    Origin = CanvasSize = ImVec2(0.0f, 0.0f);
    DrawList = nullptr;
    InsideCanvas = false;
    CurNode = -1;
    CurNodeScreen = ImVec2(0.0f, 0.0f);
    NextTitle[0] = 0;
    NextTitleColor = 0;
    Interaction = ImGuiCanvasInteraction_None;
    GestureStartMouse = GestureStartPan = ImVec2(0.0f, 0.0f);
    DragWireFromPin = -1;
    EditBuf = nullptr; EditBufSize = 0; EditFlag = nullptr; EditNodeIdx = -1; EditFocusPending = false;
    LastEditingNodeId = -1;
    MiniMapReq = false;
    MiniMapFraction = 0.2f;
    MiniRectMin = MiniRectMax = ImVec2(0.0f, 0.0f);
    MiniContentMin = MiniModelMin = ImVec2(0.0f, 0.0f);
    MiniScale = 0.0f;
    NodeDblClickReq = false; NodeDblClickId = -1;
    MenuNodeReq = MenuWireReq = MenuEmptyReq = false;
    MenuNodeId = MenuWireId = -1;
    MenuEmptyModel = ImVec2(0.0f, 0.0f);
    WireCreatedReq = WireDroppedReq = WireDetachedReq = false;
    CreatedPinA = CreatedPinB = DroppedFromPin = DetachedWireId = DetachedGrabbedPin = -1;
    DroppedModel = ImVec2(0.0f, 0.0f);
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

// Wire bezier controls: horizontal tangents leaving each pin toward its natural side (out -> +x, in -> -x).
static void CanvasWireControls(const ImGuiCanvasState* c, ImVec2 a, int kind_a, ImVec2 b, int kind_b, ImVec2* c0, ImVec2* c1)
{
  const float dx = ImMax(50.0f * CanvasScale(c), ImFabs(b.x - a.x) * 0.5f);
  *c0 = ImVec2(a.x + (kind_a == ImGui::ImGuiCanvasPin_In ? -dx : dx), a.y);
  *c1 = ImVec2(b.x + (kind_b == ImGui::ImGuiCanvasPin_In ? -dx : dx), b.y);
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

static void CanvasBeginNodeDrag(ImGuiCanvasState* c)
{
  c->Interaction = ImGuiCanvasInteraction_DragNodes;
  c->DragNodes.resize(0);
  c->DragStartPos.resize(0);
  for (int i = 0; i < c->Selection.Size; i++)
    if (const ImGuiCanvasNodeRec* n = CanvasFindNode(c, c->Selection.Data[i]))
      if (n->Draggable)
      {
        c->DragNodes.push_back(n->Id);
        c->DragStartPos.push_back(n->Pos);
      }
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
    if (c->HoveredPin >= 0)
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
      const ImVec2 delta_model = (mouse - c->GestureStartMouse) / CanvasScale(c);   // pixels -> model, once, here
      for (int i = 0; i < c->DragNodes.Size; i++)
        if (ImGuiCanvasNodeRec* n = CanvasFindNode(c, c->DragNodes.Data[i]))
        {
          ImVec2 p = c->DragStartPos.Data[i] + delta_model;
          if (c->Style.GridSnap && c->Style.GridSpacing > 0.0f)
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

  // Next-node scratch (submission-scoped, like SetNextWindow*; consumed by CanvasBeginNode).
  static char  s_next_title[64] = { 0 };
  static ImU32 s_next_title_color = 0;
  static char* s_next_edit_buf = nullptr;
  static int   s_next_edit_size = 0;
  static bool* s_next_edit_flag = nullptr;

  void CanvasNextNodeTitle(const char* title, ImU32 title_color)
  {
    if (title != nullptr)
      ImStrncpy(s_next_title, title, IM_ARRAYSIZE(s_next_title));
    else
      s_next_title[0] = 0;
    s_next_title_color = title_color;
    s_next_edit_buf = nullptr;
    s_next_edit_size = 0;
    s_next_edit_flag = nullptr;
  }

  void CanvasNextNodeTitleEditable(char* buf, int buf_size, bool* editing, ImU32 title_color)
  {
    // An editable title always occupies its band, even when blank.
    ImStrncpy(s_next_title, buf != nullptr && buf[0] ? buf : " ", IM_ARRAYSIZE(s_next_title));
    s_next_title_color = title_color;
    s_next_edit_buf = buf;
    s_next_edit_size = buf_size;
    s_next_edit_flag = editing;
  }

  bool CanvasBeginNode(ImGuiCanvasState* c, int node_id)
  {
    IM_ASSERT(c->InsideCanvas && c->CurNode == -1);
    ImGuiCanvasNodeRec* n = CanvasFindOrCreateNode(c, node_id);
    n->LastFrame = GetFrameCount();
    ImStrncpy(n->Title, s_next_title, IM_ARRAYSIZE(n->Title));
    n->TitleColor = s_next_title_color;
    s_next_title[0] = 0;
    s_next_title_color = 0;

    // Rename hook: capture the host's edit binding for THIS node (pointers live for the frame only).
    c->EditNodeIdx = -1;
    if (s_next_edit_flag != nullptr && s_next_edit_buf != nullptr)
    {
      c->EditBuf = s_next_edit_buf;
      c->EditBufSize = s_next_edit_size;
      c->EditFlag = s_next_edit_flag;
      c->EditNodeIdx = c->NodeIdx.GetInt((ImGuiID)node_id, 0) - 1;
      if (*s_next_edit_flag && c->LastEditingNodeId != node_id)
      {
        c->EditFocusPending = true;      // focus once, when the edit state ARRIVES on this node
        c->LastEditingNodeId = node_id;
      }
      else if (!*s_next_edit_flag && c->LastEditingNodeId == node_id)
      {
        c->LastEditingNodeId = -1;
      }
    }
    s_next_edit_buf = nullptr;
    s_next_edit_size = 0;
    s_next_edit_flag = nullptr;

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
    PushFont(nullptr, GetFontSize() * z);

    const float title_h = n->Title[0] ? GetFrameHeight() : 0.0f;
    const ImVec2 content_origin = c->CurNodeScreen + c->Style.NodePadding * CanvasScale(c) + ImVec2(0.0f, title_h);
    SetCursorScreenPos(content_origin);
    PushID(node_id);
    BeginGroup();
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
    const ImVec2 content_px(ImMax(content_mx.x - content_mn.x, GetFontSize() * 2.0f), ImMax(content_mx.y - content_mn.y, 0.0f));
    n->Size.x = (content_px.x + c->Style.NodePadding.x * z * 2.0f) / z;
    n->Size.y = (content_px.y + c->Style.NodePadding.y * z * 2.0f + title_h) / z;

    // Pin anchors resolve NOW, with the final node width known: In pins on the left edge, Out pins
    // on the right, at their row's vertical center; model units, this frame.
    for (int i = 0; i < c->CurNodePins.Size; i++)
    {
      ImGuiCanvasPinRec* p = &c->Pins.Data[c->CurNodePins.Data[i]];
      p->Anchor.x = p->Kind == ImGuiCanvasPin_In ? n->Pos.x : n->Pos.x + n->Size.x;
    }

    // Plate + title into the plate channel, under the content.
    const ImVec2 mn = c->CurNodeScreen;
    const ImVec2 mx = mn + n->Size * z;
    const bool hovered = c->HoveredNode == n->Id;
    const bool selected = CanvasIsSelected(c, n->Id);
    const float rounding = c->Style.NodeRounding * z;
    c->Splitter.SetCurrentChannel(c->DrawList, 1);
    c->DrawList->AddRectFilled(mn, mx, selected ? c->Style.NodeBgSelected : hovered ? c->Style.NodeBgHovered : c->Style.NodeBg, rounding);
    const bool editing_title = c->EditNodeIdx == c->CurNode && c->EditFlag != nullptr && *c->EditFlag && title_h > 0.0f;
    if (title_h > 0.0f)
    {
      const ImU32 tb = n->TitleColor != 0 ? n->TitleColor
                     : selected ? c->Style.TitleBarSelected : hovered ? c->Style.TitleBarHovered : c->Style.TitleBar;
      c->DrawList->AddRectFilled(mn, ImVec2(mx.x, mn.y + title_h), tb, rounding, ImDrawFlags_RoundCornersTop);
      if (!editing_title)
        c->DrawList->AddText(ImVec2(mn.x + c->Style.NodePadding.x * z, mn.y + (title_h - GetFontSize()) * 0.5f),
                             c->Style.TitleText, n->Title);
    }
    c->DrawList->AddRect(mn, mx, selected ? c->Style.NodeOutlineSelected : c->Style.NodeOutline, rounding, 0,
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
    PopStyleVar(5);
    c->CurNode = -1;
  }

  // Pin submission (inside a node): Begin marks the row start, End records the row's vertical
  // center in MODEL units; the horizontal edge resolves in CanvasEndNode once the width is known.
  static int   s_cur_pin = -1;
  static float s_cur_pin_y0 = 0.0f;
  static ImU32 s_next_pin_color = 0;

  void CanvasNextPinColor(ImU32 color)
  {
    s_next_pin_color = color;
  }

  void CanvasBeginPin(ImGuiCanvasState* c, int pin_id, int kind, int shape)
  {
    IM_ASSERT(c->InsideCanvas && c->CurNode >= 0 && s_cur_pin == -1);
    ImGuiCanvasPinRec* p = CanvasFindOrCreatePin(c, pin_id);
    p->NodeId = c->Nodes.Data[c->CurNode].Id;
    p->Kind = kind;
    p->Shape = shape;
    p->Color = s_next_pin_color;
    s_next_pin_color = 0;
    p->LastFrame = GetFrameCount();
    s_cur_pin = c->PinIdx.GetInt((ImGuiID)pin_id, 0) - 1;
    s_cur_pin_y0 = GetCursorScreenPos().y;
  }

  void CanvasEndPin(ImGuiCanvasState* c)
  {
    IM_ASSERT(c->InsideCanvas && s_cur_pin >= 0);
    ImGuiCanvasPinRec* p = &c->Pins.Data[s_cur_pin];
    float y1 = GetCursorScreenPos().y;
    if (y1 <= s_cur_pin_y0)
      y1 = s_cur_pin_y0 + GetTextLineHeight();
    const float yc = (s_cur_pin_y0 + y1 - GetStyle().ItemSpacing.y) * 0.5f;   // row center, minus the trailing spacing
    p->Anchor.y = (yc - c->Origin.y - c->Pan.y) / CanvasScale(c);             // screen -> model, this frame's camera
    c->CurNodePins.push_back(s_cur_pin);
    s_cur_pin = -1;
  }

  void CanvasWire(ImGuiCanvasState* c, int wire_id, int pin_a, int pin_b, ImU32 color)
  {
    IM_ASSERT(c->InsideCanvas);
    ImGuiCanvasWireRec rec;
    rec.Id = wire_id;
    rec.PinA = pin_a;
    rec.PinB = pin_b;
    rec.Color = color;
    c->Wires.push_back(rec);
    if (ImGuiCanvasPinRec* pa = CanvasFindPin(c, pin_a)) pa->WiredCount++;
    if (ImGuiCanvasPinRec* pb = CanvasFindPin(c, pin_b)) pb->WiredCount++;
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
    IM_ASSERT(c->InsideCanvas && c->CurNode == -1 && s_cur_pin == -1);
    const float  z = CanvasScale(c);
    const ImVec2 mouse = GetIO().MousePos;
    const int    frame = GetFrameCount();

    // Hover against THIS frame's geometry: pins beat nodes beat wires. Feeds this frame's draw
    // colors and the next frame's press decisions.
    c->HoveredPin = -1;
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
    if (c->HoveredPin == -1 && c->HoveredNode == -1)
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
        CanvasWireControls(c, a, pa->Kind, b, pb->Kind, &c0, &c1);
        const float d2 = CanvasWireDistanceSq(mouse, a, c0, c1, b);
        if (d2 <= best)
        {
          best = d2;
          c->HoveredWire = c->Wires.Data[i].Id;
        }
      }
    }

    // Wires draw in channel 0: under the node plates, above the grid.
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
      CanvasWireControls(c, a, pa->Kind, b, pb->Kind, &c0, &c1);
      const bool hov = w->Id == c->HoveredWire;
      const bool sel = w->Id == c->SelectedWire;
      const ImU32 col = sel ? c->Style.WireSelected : hov ? c->Style.WireHovered : (w->Color != 0 ? w->Color : c->Style.Wire);
      c->DrawList->AddBezierCubic(a, c0, c1, b, col, ImMax(1.0f, c->Style.WireThickness * z * (hov || sel ? 1.4f : 1.0f)));
    }

    c->Splitter.Merge(c->DrawList);
    c->SubmitOrder = c->SubmitOrderNow;   // this frame's z-order becomes the hit-test order
    c->WiresPrev = c->Wires;              // press decisions at the next CanvasBegin walk these

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
        int kind_b = pf->Kind == ImGuiCanvasPin_In ? ImGuiCanvasPin_Out : ImGuiCanvasPin_In;
        if (c->HoveredPin >= 0 && c->HoveredPin != c->DragWireFromPin)
          if (const ImGuiCanvasPinRec* pt = CanvasFindPin(c, c->HoveredPin))
          {
            b = CanvasToScreen(c, pt->Anchor);
            kind_b = pt->Kind;
          }
        ImVec2 c0, c1;
        CanvasWireControls(c, a, pf->Kind, b, kind_b, &c0, &c1);
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
