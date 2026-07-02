// ImGuiAppLayer canvas engine, slice C1: canvas child + native camera (pan/zoom) + grid + nodes
// with same-frame model-unit measurement + selection + drag + menu requests.
// Design: docs/canvas-engine-design.md. Coherence rules: docs/phase-coherence.md.
//
// The core invariant, restated where it is enforced: node geometry is STORED in model units; the
// camera is applied exactly once, at draw/hit-test time, always with THIS frame's values. Sizes are
// measured in the same frame and same zoom they were rendered with, so the model value is exact --
// no cross-frame pixel ever meets a fresh transform.
//
// Index of this file (search for "[SECTION]"):
// [SECTION] State
// [SECTION] Camera + spaces
// [SECTION] Frame begin: input FSM (pan / drag / zoom / menu)
// [SECTION] Nodes (begin/end, same-frame measurement, drawing)
// [SECTION] Frame end + queries

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_applayer_canvas.h"
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

enum ImGuiCanvasInteraction_
{
  ImGuiCanvasInteraction_None = 0,
  ImGuiCanvasInteraction_Pan,
  ImGuiCanvasInteraction_DragNodes,
  ImGuiCanvasInteraction_MenuPending,   // RMB down, not yet travelled: release = menu, travel = pan
};

struct ImGuiCanvasState
{
  ImGuiCanvasStyle Style;
  ImGuiCanvasIO    IO;

  // Camera
  ImVec2 Pan;
  float  Zoom;

  // Nodes
  ImVector<ImGuiCanvasNodeRec> Nodes;
  ImGuiStorage                 NodeIdx;        // id -> index + 1
  ImVector<int>                SubmitOrder;    // last frame's submission order (z-order; hit-test walks it backward)
  ImVector<int>                SubmitOrderNow; // rebuilt during the current frame

  // Selection + hover
  ImVector<int> Selection;
  int           HoveredNode;                   // resolved against current camera + model geometry

  // Per-frame canvas geometry
  ImVec2      Origin;         // canvas child top-left (screen)
  ImVec2      CanvasSize;
  ImDrawList* DrawList;
  ImDrawListSplitter Splitter;                 // ch0 = grid + node plates, ch1 = node content
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

  // Latched events (valid from CanvasEnd until the next CanvasBegin)
  bool   MenuNodeReq;   int    MenuNodeId;
  bool   MenuEmptyReq;  ImVec2 MenuEmptyModel;

  ImGuiCanvasState()
  {
    memset(&Style, 0, sizeof(Style));
    Style.GridBg          = IM_COL32(28, 29, 32, 255);
    Style.GridLine        = IM_COL32(52, 53, 58, 110);
    Style.GridLinePrimary = IM_COL32(64, 66, 72, 140);
    Style.NodeBg          = IM_COL32(46, 47, 52, 255);
    Style.NodeBgHovered   = IM_COL32(52, 54, 60, 255);
    Style.NodeBgSelected  = IM_COL32(54, 56, 63, 255);
    Style.NodeOutline     = IM_COL32(18, 18, 20, 200);
    Style.TitleBar        = IM_COL32(66, 68, 76, 255);
    Style.TitleBarHovered = IM_COL32(76, 79, 88, 255);
    Style.TitleBarSelected= IM_COL32(88, 92, 104, 255);
    Style.Wire            = IM_COL32(160, 165, 175, 220);
    Style.WireHovered     = IM_COL32(230, 232, 238, 255);
    Style.WireSelected    = IM_COL32(230, 200, 120, 255);
    Style.PinData         = IM_COL32(120, 170, 230, 255);
    Style.PinContainment  = IM_COL32(230, 170, 110, 255);
    Style.PinHovered      = IM_COL32(250, 250, 252, 255);
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
    HoveredNode = -1;
    Origin = CanvasSize = ImVec2(0.0f, 0.0f);
    DrawList = nullptr;
    InsideCanvas = false;
    CurNode = -1;
    CurNodeScreen = ImVec2(0.0f, 0.0f);
    NextTitle[0] = 0;
    NextTitleColor = 0;
    Interaction = ImGuiCanvasInteraction_None;
    GestureStartMouse = GestureStartPan = ImVec2(0.0f, 0.0f);
    MenuNodeReq = MenuEmptyReq = false;
    MenuNodeId = -1;
    MenuEmptyModel = ImVec2(0.0f, 0.0f);
  }
};

static ImGuiCanvasNodeRec* CanvasFindNode(ImGuiCanvasState* c, int node_id)
{
  const int idx = c->NodeIdx.GetInt((ImGuiID)node_id, 0) - 1;
  return idx >= 0 ? &c->Nodes.Data[idx] : nullptr;
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

namespace ImGui
{
  ImGuiCanvasState* CanvasCreate()                       { return IM_NEW(ImGuiCanvasState)(); }
  void              CanvasDestroy(ImGuiCanvasState* c)   { IM_DELETE(c); }
  ImGuiCanvasStyle* CanvasGetStyle(ImGuiCanvasState* c)  { return &c->Style; }
  ImGuiCanvasIO*    CanvasGetIO(ImGuiCanvasState* c)     { return &c->IO; }

  ImVec2 CanvasGetPan(const ImGuiCanvasState* c)              { return c->Pan; }
  void   CanvasSetPan(ImGuiCanvasState* c, ImVec2 pan)        { c->Pan = pan; }
  float  CanvasGetZoom(const ImGuiCanvasState* c)             { return c->Zoom; }

  ImVec2 CanvasToScreen(const ImGuiCanvasState* c, ImVec2 model)
  {
    return c->Origin + c->Pan + model * c->Zoom;
  }

  ImVec2 CanvasFromScreen(const ImGuiCanvasState* c, ImVec2 screen)
  {
    return (screen - c->Origin - c->Pan) / c->Zoom;
  }

  void CanvasSetZoom(ImGuiCanvasState* c, float zoom, ImVec2 keep_screen_pos)
  {
    zoom = ImClamp(zoom, c->IO.ZoomMin, c->IO.ZoomMax);
    const ImVec2 anchor_model = CanvasFromScreen(c, keep_screen_pos);
    c->Zoom = zoom;
    c->Pan = keep_screen_pos - c->Origin - anchor_model * zoom;
  }

  void CanvasCenterOn(ImGuiCanvasState* c, ImVec2 model_pos)
  {
    c->Pan = c->CanvasSize * 0.5f - model_pos * c->Zoom;
  }

  void CanvasFitRect(ImGuiCanvasState* c, ImVec2 model_min, ImVec2 model_max, float margin_px)
  {
    const ImVec2 span(ImMax(1.0f, model_max.x - model_min.x), ImMax(1.0f, model_max.y - model_min.y));
    const ImVec2 avail(ImMax(1.0f, c->CanvasSize.x - margin_px * 2.0f), ImMax(1.0f, c->CanvasSize.y - margin_px * 2.0f));
    c->Zoom = ImClamp(ImMin(avail.x / span.x, avail.y / span.y), c->IO.ZoomMin, c->IO.ZoomMax);
    CanvasCenterOn(c, (model_min + model_max) * 0.5f);
  }
}

//-----------------------------------------------------------------------------
// [SECTION] Frame begin: input FSM (pan / drag / zoom / menu)
//-----------------------------------------------------------------------------

// Top-most node under a screen point, walking last frame's submission order backward (later = on
// top -- z-order IS submission order, exactly like the draw). Model positions are current, sizes
// are last frame's MODEL measurement: zoom-invariant, so a zoom change never mis-hits.
static int CanvasHitNode(const ImGuiCanvasState* c, ImVec2 screen)
{
  for (int i = c->SubmitOrder.Size - 1; i >= 0; i--)
  {
    const int idx = c->NodeIdx.GetInt((ImGuiID)c->SubmitOrder.Data[i], 0) - 1;
    if (idx < 0)
      continue;
    const ImGuiCanvasNodeRec* n = &c->Nodes.Data[idx];
    const ImVec2 mn = ImGui::CanvasToScreen(c, n->Pos);
    const ImVec2 mx = mn + n->Size * c->Zoom;
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

  // Hover resolution: current camera x current model. Valid for this whole frame.
  c->HoveredNode = canvas_item_hovered || c->Interaction != ImGuiCanvasInteraction_None ? CanvasHitNode(c, mouse) : -1;

  // Wheel zoom, cursor-anchored: empty canvas plain; Ctrl anywhere over the canvas (node widgets
  // keep the plain wheel for their own behaviors, e.g. value scrubbing).
  if (c->IO.WheelZooms && io.MouseWheel != 0.0f
      && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)
      && (io.KeyCtrl || (canvas_item_hovered && c->HoveredNode == -1)))
  {
    ImGui::CanvasSetZoom(c, c->Zoom * ImPow(1.15f, io.MouseWheel), mouse);
  }

  // LMB press on the canvas catch-all: node -> select (+drag), empty -> pan (per policy).
  if (canvas_item_activated && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
  {
    c->GestureStartMouse = mouse;
    c->GestureStartPan = c->Pan;
    if (c->HoveredNode >= 0)
    {
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
    else if (c->IO.LmbPansEmptyCanvas)
    {
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
      const ImVec2 delta_model = (mouse - c->GestureStartMouse) / c->Zoom;   // pixels -> model, once, here
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

  case ImGuiCanvasInteraction_MenuPending:
    if (c->IO.RmbPans && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
      const ImVec2 travel = mouse - c->GestureStartMouse;
      if (travel.x * travel.x + travel.y * travel.y > 9.0f)
        c->Pan = c->GestureStartPan + travel;   // became a pan; stays in MenuPending, travel keeps panning
    }
    else if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
      const ImVec2 travel = mouse - c->GestureStartMouse;
      if (travel.x * travel.x + travel.y * travel.y <= 9.0f)
      {
        if (c->HoveredNode >= 0) { c->MenuNodeReq = true; c->MenuNodeId = c->HoveredNode; }
        else                     { c->MenuEmptyReq = true; c->MenuEmptyModel = ImGui::CanvasFromScreen(c, mouse); }
      }
      c->Interaction = ImGuiCanvasInteraction_None;
    }
    else
    {
      c->Interaction = ImGuiCanvasInteraction_None;
    }
    break;

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
    c->MenuNodeReq = c->MenuEmptyReq = false;

    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    PushStyleColor(ImGuiCol_ChildBg, c->Style.GridBg);
    BeginChild(str_id, size, ImGuiChildFlags_None,
               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    c->Origin = GetCursorScreenPos();
    c->CanvasSize = GetWindowSize();
    c->DrawList = GetWindowDrawList();
    c->Splitter.Split(c->DrawList, 2);
    c->Splitter.SetCurrentChannel(c->DrawList, 0);

    // Grid: model spacing x zoom, offset by pan -- all current-frame values, nothing cached.
    if (c->Style.GridLines && c->Style.GridSpacing > 0.0f)
    {
      const float step = c->Style.GridSpacing * c->Zoom;
      if (step >= 4.0f)
      {
        for (float x = ImFmod(c->Pan.x, step); x < c->CanvasSize.x; x += step)
          c->DrawList->AddLine(ImVec2(c->Origin.x + x, c->Origin.y), ImVec2(c->Origin.x + x, c->Origin.y + c->CanvasSize.y), c->Style.GridLine);
        for (float y = ImFmod(c->Pan.y, step); y < c->CanvasSize.y; y += step)
          c->DrawList->AddLine(ImVec2(c->Origin.x, c->Origin.y + y), ImVec2(c->Origin.x + c->CanvasSize.x, c->Origin.y + y), c->Style.GridLine);
      }
    }

    // The interaction catch-all: one invisible item spanning the canvas. Node-body widgets submit
    // AFTER it, so they win hover per imgui's last-wins rule; the catch-all receives exactly the
    // empty-canvas and node-plate gestures (plates are draw-list, not items -- deliberate).
    SetNextItemAllowOverlap();
    InvisibleButton("##canvas_input", ImVec2(ImMax(1.0f, c->CanvasSize.x), ImMax(1.0f, c->CanvasSize.y)));
    const bool item_hovered = IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool item_activated = IsItemActivated() || (item_hovered && (IsMouseClicked(ImGuiMouseButton_Left) || IsMouseClicked(ImGuiMouseButton_Right)));
    CanvasUpdateInput(c, item_hovered, item_activated);

    SetCursorScreenPos(c->Origin);
    c->SubmitOrderNow.resize(0);
  }

  // Next-node scratch (submission-scoped, like SetNextWindow*; consumed by CanvasBeginNode).
  static char  s_next_title[64] = { 0 };
  static ImU32 s_next_title_color = 0;

  void CanvasNextNodeTitle(const char* title, ImU32 title_color)
  {
    if (title != nullptr)
      ImStrncpy(s_next_title, title, IM_ARRAYSIZE(s_next_title));
    else
      s_next_title[0] = 0;
    s_next_title_color = title_color;
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

    c->CurNode = c->NodeIdx.GetInt((ImGuiID)node_id, 0) - 1;
    c->SubmitOrderNow.push_back(node_id);
    c->CurNodeScreen = CanvasToScreen(c, n->Pos);

    // Content renders in the FG channel, under the zoomed font + zoom-scaled layout metrics -- the
    // engine owns the scaling so hosts submit plain widgets (docs/phase-coherence.md rule 1).
    c->Splitter.SetCurrentChannel(c->DrawList, 1);
    const float z = c->Zoom;
    const ImGuiStyle& gs = GetStyle();
    PushStyleVar(ImGuiStyleVar_FramePadding,     ImVec2(gs.FramePadding.x * z, gs.FramePadding.y * z));
    PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2(gs.ItemSpacing.x * z, gs.ItemSpacing.y * z));
    PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(gs.ItemInnerSpacing.x * z, gs.ItemInnerSpacing.y * z));
    PushStyleVar(ImGuiStyleVar_IndentSpacing,    gs.IndentSpacing * z);
    PushStyleVar(ImGuiStyleVar_FrameRounding,    gs.FrameRounding * z);
    PushFont(nullptr, GetFontSize() * z);

    const float title_h = n->Title[0] ? GetFrameHeight() : 0.0f;
    const ImVec2 content_origin = c->CurNodeScreen + c->Style.NodePadding * z + ImVec2(0.0f, title_h);
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
    const float  z = c->Zoom;
    const float  title_h = n->Title[0] ? GetFrameHeight() : 0.0f;   // still under the zoomed font

    // Same-frame measurement in the same zoom the content rendered with: the model size is exact.
    const ImVec2 content_px(ImMax(content_mx.x - content_mn.x, GetFontSize() * 2.0f), ImMax(content_mx.y - content_mn.y, 0.0f));
    n->Size.x = (content_px.x + c->Style.NodePadding.x * z * 2.0f) / z;
    n->Size.y = (content_px.y + c->Style.NodePadding.y * z * 2.0f + title_h) / z;

    // Plate + title into the BG channel, under the content just submitted.
    const ImVec2 mn = c->CurNodeScreen;
    const ImVec2 mx = mn + n->Size * z;
    const bool hovered = c->HoveredNode == n->Id;
    const bool selected = CanvasIsSelected(c, n->Id);
    const float rounding = c->Style.NodeRounding * z;
    c->Splitter.SetCurrentChannel(c->DrawList, 0);
    c->DrawList->AddRectFilled(mn, mx, selected ? c->Style.NodeBgSelected : hovered ? c->Style.NodeBgHovered : c->Style.NodeBg, rounding);
    if (title_h > 0.0f)
    {
      const ImU32 tb = n->TitleColor != 0 ? n->TitleColor
                     : selected ? c->Style.TitleBarSelected : hovered ? c->Style.TitleBarHovered : c->Style.TitleBar;
      c->DrawList->AddRectFilled(mn, ImVec2(mx.x, mn.y + title_h), tb, rounding, ImDrawFlags_RoundCornersTop);
      c->DrawList->AddText(ImVec2(mn.x + c->Style.NodePadding.x * z, mn.y + (title_h - GetFontSize()) * 0.5f),
                           IM_COL32(235, 236, 240, 255), n->Title);
    }
    c->DrawList->AddRect(mn, mx, selected ? IM_COL32(230, 200, 120, 255) : c->Style.NodeOutline, rounding, 0,
                         ImMax(1.0f, c->Style.NodeBorder * z));
    c->Splitter.SetCurrentChannel(c->DrawList, 1);

    PopID();
    PopFont();
    PopStyleVar(5);
    c->CurNode = -1;
  }

//-----------------------------------------------------------------------------
// [SECTION] Frame end + queries
//-----------------------------------------------------------------------------

  void CanvasEnd(ImGuiCanvasState* c)
  {
    IM_ASSERT(c->InsideCanvas && c->CurNode == -1);
    c->Splitter.Merge(c->DrawList);
    c->SubmitOrder = c->SubmitOrderNow;   // this frame's z-order becomes the hit-test order
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
  int CanvasHoveredWire(const ImGuiCanvasState* c) { IM_UNUSED(c); return -1; }   // C2
  int CanvasHoveredPin(const ImGuiCanvasState* c)  { IM_UNUSED(c); return -1; }   // C2

  bool CanvasMenuRequestNode(const ImGuiCanvasState* c, int* out_node_id)
  {
    if (c->MenuNodeReq && out_node_id != nullptr)
      *out_node_id = c->MenuNodeId;
    return c->MenuNodeReq;
  }

  bool CanvasMenuRequestEmpty(const ImGuiCanvasState* c, ImVec2* out_model_pos)
  {
    if (c->MenuEmptyReq && out_model_pos != nullptr)
      *out_model_pos = c->MenuEmptyModel;
    return c->MenuEmptyReq;
  }
}
