// ImGuiAppLayer canvas engine. Invariant: node geometry lives in MODEL units; the camera
// (pan + zoom) is the only transform, applied at draw; measurements are captured the same frame in
// the same units they are consumed in. docs/canvas-engine-design.md, docs/phase-coherence.md.

#pragma once

#include "imgui.h"

struct ImGuiCanvasStyle
{
  // Colors: set directly, no push/pop stack. CanvasCreate derives them from the current
  // ImGuiStyle theme (CanvasStyleFromTheme); hosts may overwrite after.
  ImU32 GridBg;
  ImU32 GridLine;
  ImU32 GridLinePrimary;
  ImU32 NodeBg;
  ImU32 NodeBgHovered;
  ImU32 NodeBgSelected;
  ImU32 NodeOutline;
  ImU32 NodeOutlineSelected;
  ImU32 TitleBar;       // per-node override via CanvasNextNodeTitleColor
  ImU32 TitleBarHovered;
  ImU32 TitleBarSelected;
  ImU32 TitleText;
  ImU32 TitleEditBg;    // InputText field bg while renaming a node title
  ImU32 Wire;
  ImU32 WireHovered;
  ImU32 WireSelected;
  ImU32 PinData;        // circle pins (data edges)
  ImU32 PinContainment; // square pins (containment edges)
  ImU32 PinHovered;
  ImU32 MiniMapBg;
  ImU32 MiniMapBgHovered;
  ImU32 MiniMapOutline;
  ImU32 MiniMapNodeBg;
  ImU32 MiniMapNodeBgHovered;
  ImU32 MiniMapNodeBgSelected;
  ImU32 MiniMapNodeOutline;
  ImU32 MiniMapLink;
  ImU32 MiniMapCanvas;  // the current-view rect inside the map
  ImU32 MiniMapCanvasOutline;

  // Metrics in MODEL units (the engine zooms them; hosts never pre-scale).
  float  GridSpacing;
  float  NodeRounding;
  ImVec2 NodePadding;
  float  NodeBorder;
  float  WireThickness;
  float  PinRadius;
  float  PinHoverRadius;

  bool GridLines;
  bool GridSnap; // snap node origins to GridSpacing while dragging
};

struct ImGuiCanvasIO
{
  // Bindings policy.
  bool  LmbPansEmptyCanvas; // LMB-drag on empty canvas pans (no box select)
  bool  RmbPans;            // RMB-drag pans; a short RMB click reports CanvasMenuRequest*
  bool  WheelZooms;         // cursor-centered; Ctrl+wheel zooms even over nodes/items
  float ZoomMin, ZoomMax;   // clamp (defaults 0.3 / 2.5)
};

struct ImGuiCanvasState;   // opaque; created/destroyed by the engine; passed explicitly, no context stack

namespace ImGui
{
  IMGUI_API ImGuiCanvasState* CanvasCreate();
  IMGUI_API void              CanvasDestroy(ImGuiCanvasState* c);
  IMGUI_API ImGuiCanvasStyle* CanvasGetStyle(ImGuiCanvasState* c);
  IMGUI_API ImGuiCanvasIO*    CanvasGetIO(ImGuiCanvasState* c);
  // Recompute all style colors from the current ImGuiStyle theme (requires a live context).
  // Called by CanvasCreate; call again after switching themes.
  IMGUI_API void              CanvasStyleFromTheme(ImGuiCanvasStyle* style);

  // ---- frame ----------------------------------------------------------------------------------
  // CanvasBegin opens the child (fills the available region unless size is given), draws the grid,
  // and applies camera input per the IO policy. Submit nodes/pins/wires between Begin/End;
  // CanvasEnd draws wires + pins from THIS frame's geometry, resolves hover, runs the interaction
  // FSM, and latches events (valid until the next CanvasBegin).
  IMGUI_API void CanvasBegin(ImGuiCanvasState* c, const char* str_id, ImVec2 size /*= 0,0*/);
  IMGUI_API void CanvasEnd(ImGuiCanvasState* c);

  // Decoration hook: returns the canvas draw list switched to the BACKGROUND channel (above the grid,
  // below node plates; wires land in the same channel later, so they draw over these decorations).
  // Call between CanvasBegin and the first node; the next CanvasBeginNode restores the content channel.
  IMGUI_API ImDrawList* CanvasBackgroundDrawList(ImGuiCanvasState* c);

  // Annotation hook: the canvas child's draw list AFTER CanvasEnd. Appended commands render above
  // every merged channel (grid, plates, content) but stay inside the child's z-order -- annotations
  // ride the canvas, they never paint over other windows (the viewport foreground list does).
  IMGUI_API ImDrawList* CanvasAnnotationDrawList(ImGuiCanvasState* c);

  // ---- camera (pan in pixels, zoom unitless; screen = origin + pan + model * zoom) -------------
  IMGUI_API ImVec2 CanvasGetPan(const ImGuiCanvasState* c);
  IMGUI_API void   CanvasSetPan(ImGuiCanvasState* c, ImVec2 pan);
  IMGUI_API float  CanvasGetZoom(const ImGuiCanvasState* c);
  // Model -> screen scale (Zoom x host font DPI/user factor). Use for ALL model<->pixel
  // conversions; CanvasGetZoom is the logical camera only (font pushes, persisted view state).
  IMGUI_API float  CanvasGetScale(const ImGuiCanvasState* c);
  IMGUI_API void   CanvasSetZoom(ImGuiCanvasState* c, float zoom, ImVec2 keep_screen_pos);   // keeps that screen point fixed
  IMGUI_API ImVec2 CanvasToScreen(const ImGuiCanvasState* c, ImVec2 model);
  IMGUI_API ImVec2 CanvasFromScreen(const ImGuiCanvasState* c, ImVec2 screen);
  IMGUI_API void   CanvasCenterOn(ImGuiCanvasState* c, ImVec2 model_pos);
  IMGUI_API void   CanvasFitRect(ImGuiCanvasState* c, ImVec2 model_min, ImVec2 model_max, float margin_px);
  IMGUI_API void   CanvasFitNodes(ImGuiCanvasState* c, const int* node_ids, int count, float margin_px);
  IMGUI_API void   CanvasFitAll(ImGuiCanvasState* c, float margin_px);                       // over nodes submitted last frame

  // Minimap: call between CanvasBegin/CanvasEnd; drawn by CanvasEnd as a bottom-right inset fitted
  // to the node content's aspect within size_fraction of the canvas. Holding LMB over the map
  // continuously recenters the camera. Its rect is excluded from the FSM.
  IMGUI_API void   CanvasMiniMap(ImGuiCanvasState* c, float size_fraction);

  // ---- nodes (geometry in MODEL units, always) --------------------------------------------------
  // Between CanvasBeginNode/CanvasEndNode submit ordinary ImGui widgets; the engine renders them
  // under the zoomed font + scaled layout metrics and measures the node the SAME frame.
  IMGUI_API void   CanvasNextNodeTitle(ImGuiCanvasState* c, const char* title, ImU32 title_color /*= 0 -> style*/);
  // Per-node presentation intent, all consumed by the next CanvasBeginNode: kind word (muted,
  // right-aligned), origin dot (leading; ring form for promoted), framed badge after the name,
  // corner rounding in model units (< 0 = style), rule under the title band (0 none / 1 solid /
  // 2 dashed), and an edge stripe on one ImGuiCanvasPinSide_.
  IMGUI_API void   CanvasNextNodeTitleTag(ImGuiCanvasState* c, const char* tag);
  IMGUI_API void   CanvasNextNodeOriginDot(ImGuiCanvasState* c, ImU32 color, bool ring);
  IMGUI_API void   CanvasNextNodeTitleBadge(ImGuiCanvasState* c, const char* badge);
  IMGUI_API void   CanvasNextNodeRounding(ImGuiCanvasState* c, float model_rounding);
  IMGUI_API void   CanvasNextNodeWidth(ImGuiCanvasState* c, float model_width);          // normalized plate width; <= 0 = content-sized
  IMGUI_API float  CanvasNodeNeededWidth(const ImGuiCanvasState* c, int node_id);        // content-derived width need (0 until measured)
  IMGUI_API void   CanvasNextNodeHeaderRule(ImGuiCanvasState* c, int rule, ImU32 color);
  IMGUI_API void   CanvasNextNodeEdgeStripe(ImGuiCanvasState* c, int side, ImU32 color, float model_thickness);
  IMGUI_API void   CanvasNextNodeAlpha(ImGuiCanvasState* c, float alpha);   // < 1 dims plate + content (disabled look); consumed by the next CanvasBeginNode
  // Editable variant (host-driven rename): while *editing, the engine renders an InputText in the
  // title bar bound to buf and clears *editing when it deactivates. Pair with CanvasNodeDoubleClicked
  // to enter the state (the host decides whether a double-click renames or drills).
  IMGUI_API void   CanvasNextNodeTitleEditable(ImGuiCanvasState* c, char* buf, int buf_size, bool* editing, ImU32 title_color);
  IMGUI_API bool   CanvasBeginNode(ImGuiCanvasState* c, int node_id);   // false if culled (off-screen): body may be skipped, geometry persists
  IMGUI_API void   CanvasEndNode(ImGuiCanvasState* c);
  IMGUI_API ImVec2 CanvasNodePos(const ImGuiCanvasState* c, int node_id);        // model
  IMGUI_API void   CanvasSetNodePos(ImGuiCanvasState* c, int node_id, ImVec2 model_pos);
  IMGUI_API ImVec2 CanvasNodeSize(const ImGuiCanvasState* c, int node_id);       // model; this frame if submitted, else last known
  IMGUI_API const char* CanvasNodeTitleBadge(const ImGuiCanvasState* c, int node_id);   // title-bar badge text ("" if none / unknown node)
  IMGUI_API void   CanvasSetNodeDraggable(ImGuiCanvasState* c, int node_id, bool draggable);
  IMGUI_API void   CanvasSetNodeSolid(ImGuiCanvasState* c, int node_id, bool solid);   // solid nodes cannot be dragged into overlap with other solid nodes (slide to contact)
  // Declare a solid region (model units) for THIS frame: solid-node drags cannot enter it (the
  // next frame's drag consumes it, like node geometry). Call between CanvasBegin/CanvasEnd.
  IMGUI_API void   CanvasAddSolidRect(ImGuiCanvasState* c, ImVec2 model_min, ImVec2 model_max);

  // ---- pins + wires -----------------------------------------------------------------------------
  // Kind is the interaction role (which end pairs with which when wiring). Side is the node edge the
  // pin sits on and the direction its wire leaves -- orthogonal to Kind. Left/Right give the classic
  // horizontal data read; Top/Bottom give a vertical owner-over-child containment read.
  enum ImGuiCanvasPinKind_ { ImGuiCanvasPin_In = 0, ImGuiCanvasPin_Out = 1 };
  enum ImGuiCanvasPinShape_ { ImGuiCanvasPinShape_Circle = 0, ImGuiCanvasPinShape_Square = 1 };
  enum ImGuiCanvasPinSide_ { ImGuiCanvasPinSide_Left = 0, ImGuiCanvasPinSide_Right = 1, ImGuiCanvasPinSide_Top = 2, ImGuiCanvasPinSide_Bottom = 3 };
  IMGUI_API void   CanvasNextPinColor(ImGuiCanvasState* c, ImU32 color);   // 0 -> style (by shape); consumed by the next CanvasBeginPin
  IMGUI_API void   CanvasNextPinSide(ImGuiCanvasState* c, int side);   // override the next pin's edge; default derives from Kind (In->Left, Out->Right)
  IMGUI_API void   CanvasBeginPin(ImGuiCanvasState* c, int pin_id, int kind /*In|Out*/, int shape);
  IMGUI_API void   CanvasEndPin(ImGuiCanvasState* c);
  // Row-less edge pin: an at-most-one-per-edge singleton (e.g. containment parent/children) placed at
  // the center of its Side edge. Submits no widget and consumes no cursor -- call between BeginNode/EndNode.
  IMGUI_API void   CanvasEdgePin(ImGuiCanvasState* c, int pin_id, int kind /*In|Out*/, int shape, int side);
  IMGUI_API void   CanvasNextWireDashed(ImGuiCanvasState* c);   // the next CanvasWire draws dashed (optional dependency)
  IMGUI_API void   CanvasWire(ImGuiCanvasState* c, int wire_id, int pin_a, int pin_b, ImU32 color /*= 0 -> style*/);
  IMGUI_API bool   CanvasWireExists(const ImGuiCanvasState* c, int wire_id);      // wire submitted this frame (query after CanvasEnd)
  IMGUI_API ImVec2 CanvasPinPos(const ImGuiCanvasState* c, int pin_id);           // model

  // ---- selection + hover ------------------------------------------------------------------------
  IMGUI_API int    CanvasNumSelectedNodes(const ImGuiCanvasState* c);
  IMGUI_API void   CanvasGetSelectedNodes(const ImGuiCanvasState* c, int* out, int cap);
  IMGUI_API void   CanvasSelectNode(ImGuiCanvasState* c, int node_id, bool additive);
  IMGUI_API void   CanvasClearSelection(ImGuiCanvasState* c);
  IMGUI_API int    CanvasHoveredNode(const ImGuiCanvasState* c);                  // -1 = none (valid after CanvasEnd)
  IMGUI_API int    CanvasHoveredWire(const ImGuiCanvasState* c);
  IMGUI_API int    CanvasHoveredPin(const ImGuiCanvasState* c);
  IMGUI_API int    CanvasWireDragSource(const ImGuiCanvasState* c);               // pin an active wire-drag started from, else -1 (for drag-time can-link telegraph)
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
