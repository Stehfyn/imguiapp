// ImGuiAppLayer canvas engine: a lean, phase-coherent node canvas (imnodes replacement).
// Design: docs/canvas-engine-design.md. Core invariant: node geometry lives in MODEL units; the
// camera (pan + zoom) is the only transform, applied at draw; measurements are captured the same
// frame in the same units they are consumed in -- the out-of-phase bug class of
// docs/phase-coherence.md is unrepresentable by construction.
//
// Status: API contract for slices C1-C3 (see the design doc). Implementation lands slice by slice;
// nothing in the Composer includes this header until C4 (migration).

#pragma once

#include "imgui.h"

struct ImGuiCanvasStyle
{
  // Colors (subset of what the Composer themes today; set directly, no push/pop stack).
  ImU32 GridBg;
  ImU32 GridLine;
  ImU32 GridLinePrimary;
  ImU32 NodeBg;
  ImU32 NodeBgHovered;
  ImU32 NodeBgSelected;
  ImU32 NodeOutline;
  ImU32 TitleBar;          // per-node override via CanvasNextNodeTitleColor
  ImU32 TitleBarHovered;
  ImU32 TitleBarSelected;
  ImU32 Wire;
  ImU32 WireHovered;
  ImU32 WireSelected;
  ImU32 PinData;           // circle pins (data edges)
  ImU32 PinContainment;    // square pins (containment edges)
  ImU32 PinHovered;

  // Metrics in MODEL units (the engine zooms them; hosts never pre-scale).
  float GridSpacing;
  float NodeRounding;
  ImVec2 NodePadding;
  float NodeBorder;
  float WireThickness;
  float PinRadius;
  float PinHoverRadius;

  bool  GridLines;
  bool  GridSnap;          // snap node origins to GridSpacing while dragging
};

struct ImGuiCanvasIO
{
  // Bindings policy (the Composer's field-tested defaults are the defaults here).
  bool LmbPansEmptyCanvas;     // LMB-drag on empty canvas pans (box select intentionally absent)
  bool RmbPans;                // RMB-drag pans; a short RMB click reports CanvasMenuRequest*
  bool WheelZooms;             // cursor-centered; Ctrl+wheel zooms even over nodes/items
  float ZoomMin, ZoomMax;      // clamp (defaults 0.3 / 2.5)
};

// One canvas instance (the Composer has exactly one; the API still takes the state explicitly --
// pointers, no hidden globals, no context stack).
struct ImGuiCanvasState;   // opaque; created/destroyed by the engine

namespace ImGui
{
  IMGUI_API ImGuiCanvasState* CanvasCreate();
  IMGUI_API void              CanvasDestroy(ImGuiCanvasState* c);
  IMGUI_API ImGuiCanvasStyle* CanvasGetStyle(ImGuiCanvasState* c);
  IMGUI_API ImGuiCanvasIO*    CanvasGetIO(ImGuiCanvasState* c);

  // ---- frame ----------------------------------------------------------------------------------
  // BeginCanvas opens the child (fills the available region unless size is given), draws the grid,
  // and applies camera input per the IO policy. All node/pin/wire submission happens between
  // Begin/End; EndCanvas draws wires + pins from THIS frame's geometry, resolves hover, runs the
  // interaction FSM, and latches the frame's events (queried until the next BeginCanvas).
  IMGUI_API void CanvasBegin(ImGuiCanvasState* c, const char* str_id, ImVec2 size /*= 0,0*/);
  IMGUI_API void CanvasEnd(ImGuiCanvasState* c);

  // Decoration hook: returns the canvas draw list switched to the BACKGROUND channel (above the grid,
  // below node plates; wires land in the same channel later, so they draw over these decorations).
  // Call between CanvasBegin and the first node; the next CanvasBeginNode restores the content channel.
  IMGUI_API ImDrawList* CanvasBackgroundDrawList(ImGuiCanvasState* c);

  // ---- camera (pan in pixels, zoom unitless; screen = origin + pan + model * zoom) -------------
  IMGUI_API ImVec2 CanvasGetPan(const ImGuiCanvasState* c);
  IMGUI_API void   CanvasSetPan(ImGuiCanvasState* c, ImVec2 pan);
  IMGUI_API float  CanvasGetZoom(const ImGuiCanvasState* c);
  IMGUI_API void   CanvasSetZoom(ImGuiCanvasState* c, float zoom, ImVec2 keep_screen_pos);   // keeps that screen point fixed
  IMGUI_API ImVec2 CanvasToScreen(const ImGuiCanvasState* c, ImVec2 model);
  IMGUI_API ImVec2 CanvasFromScreen(const ImGuiCanvasState* c, ImVec2 screen);
  IMGUI_API void   CanvasCenterOn(ImGuiCanvasState* c, ImVec2 model_pos);                    // MoveToNode successor
  IMGUI_API void   CanvasFitRect(ImGuiCanvasState* c, ImVec2 model_min, ImVec2 model_max, float margin_px);
  IMGUI_API void   CanvasFitNodes(ImGuiCanvasState* c, const int* node_ids, int count, float margin_px);
  IMGUI_API void   CanvasFitAll(ImGuiCanvasState* c, float margin_px);                       // over nodes submitted last frame

  // Minimap: call between CanvasBegin/CanvasEnd; drawn by CanvasEnd in the given corner at
  // size_fraction of the canvas. Click/drag inside it recenters the camera; its rect is excluded
  // from the canvas FSM.
  IMGUI_API void   CanvasMiniMap(ImGuiCanvasState* c, float size_fraction);

  // ---- nodes (geometry in MODEL units, always) --------------------------------------------------
  // Between BeginCanvasNode/EndCanvasNode submit ordinary ImGui widgets; the engine renders them
  // under the zoomed font + scaled layout metrics and measures the node the SAME frame.
  IMGUI_API void   CanvasNextNodeTitle(const char* title, ImU32 title_color /*= 0 -> style*/);
  // Editable variant (host-driven rename): while *editing, the engine renders an InputText in the
  // title bar bound to buf and clears *editing when it deactivates. Pair with CanvasNodeDoubleClicked
  // to enter the state (the host decides whether a double-click renames or drills).
  IMGUI_API void   CanvasNextNodeTitleEditable(char* buf, int buf_size, bool* editing, ImU32 title_color);
  IMGUI_API bool   CanvasBeginNode(ImGuiCanvasState* c, int node_id);   // false if culled (off-screen): body may be skipped, geometry persists
  IMGUI_API void   CanvasEndNode(ImGuiCanvasState* c);
  IMGUI_API ImVec2 CanvasNodePos(const ImGuiCanvasState* c, int node_id);        // model
  IMGUI_API void   CanvasSetNodePos(ImGuiCanvasState* c, int node_id, ImVec2 model_pos);
  IMGUI_API ImVec2 CanvasNodeSize(const ImGuiCanvasState* c, int node_id);       // model; this frame if submitted, else last known
  IMGUI_API void   CanvasSetNodeDraggable(ImGuiCanvasState* c, int node_id, bool draggable);

  // ---- pins + wires -----------------------------------------------------------------------------
  enum ImGuiCanvasPinKind_ { ImGuiCanvasPin_In = 0, ImGuiCanvasPin_Out = 1 };
  enum ImGuiCanvasPinShape_ { ImGuiCanvasPinShape_Circle = 0, ImGuiCanvasPinShape_Square = 1 };
  IMGUI_API void   CanvasNextPinColor(ImU32 color);   // 0 -> style (by shape); consumed by the next CanvasBeginPin
  IMGUI_API void   CanvasBeginPin(ImGuiCanvasState* c, int pin_id, int kind /*In|Out*/, int shape);
  IMGUI_API void   CanvasEndPin(ImGuiCanvasState* c);
  IMGUI_API void   CanvasWire(ImGuiCanvasState* c, int wire_id, int pin_a, int pin_b, ImU32 color /*= 0 -> style*/);
  IMGUI_API ImVec2 CanvasPinPos(const ImGuiCanvasState* c, int pin_id);           // model

  // ---- selection + hover ------------------------------------------------------------------------
  IMGUI_API int    CanvasNumSelectedNodes(const ImGuiCanvasState* c);
  IMGUI_API void   CanvasGetSelectedNodes(const ImGuiCanvasState* c, int* out, int cap);
  IMGUI_API void   CanvasSelectNode(ImGuiCanvasState* c, int node_id, bool additive);
  IMGUI_API void   CanvasClearSelection(ImGuiCanvasState* c);
  IMGUI_API int    CanvasHoveredNode(const ImGuiCanvasState* c);                  // -1 = none (valid after CanvasEnd)
  IMGUI_API int    CanvasHoveredWire(const ImGuiCanvasState* c);
  IMGUI_API int    CanvasHoveredPin(const ImGuiCanvasState* c);
  IMGUI_API int    CanvasSelectedWire(const ImGuiCanvasState* c);                 // single click-selected wire; -1 = none
  IMGUI_API void   CanvasClearWireSelection(ImGuiCanvasState* c);

  // ---- events (latched by CanvasEnd; valid until the next CanvasBegin) ---------------------------
  IMGUI_API bool   CanvasWireCreated(const ImGuiCanvasState* c, int* out_pin_a, int* out_pin_b);   // drag completed pin->pin (snap or release)
  IMGUI_API bool   CanvasWireDropped(const ImGuiCanvasState* c, int* out_from_pin, ImVec2* out_model_pos);   // released on empty canvas
  IMGUI_API bool   CanvasWireDetached(const ImGuiCanvasState* c, int* out_wire_id, int* out_grabbed_end_pin); // endpoint dragged off a pin
  IMGUI_API bool   CanvasNodeDoubleClicked(const ImGuiCanvasState* c, int* out_node_id); // LMB double-click on a node
  IMGUI_API bool   CanvasMenuRequestNode(const ImGuiCanvasState* c, int* out_node_id);   // short RMB click resolution
  IMGUI_API bool   CanvasMenuRequestWire(const ImGuiCanvasState* c, int* out_wire_id);
  IMGUI_API bool   CanvasMenuRequestEmpty(const ImGuiCanvasState* c, ImVec2* out_model_pos);
}
