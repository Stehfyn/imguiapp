# Canvas Engine — a lean, phase-coherent node canvas to replace imnodes

Decision (2026-07-02): hand-roll the node canvas inside ImGuiAppLayer. Rationale, in order:

1. **Phase coherence by construction.** imnodes stores node origins in screen-flavored "grid"
   pixels and measures into pixel rects, forcing our model↔pixel seam (zoom emulation, the model
   cache, per-frame style scaling, decoration transforms). Every bug in the 2026-07 series
   (docs/phase-coherence.md) was seam friction. A canvas whose CORE is model-space — one transform,
   applied at draw, measurements returned in model units — cannot express those bugs.
2. **Zoom native**, not emulated through a style/font/position trampoline.
3. **No untouchable upstream in the hot path** (the no-upstream-edits rule stays; this moves the
   canvas into code we own).
4. **Lean**: the Composer uses a fraction of imnodes; the rest is carried weight.

## 1. What the Composer actually uses (migration contract)

Everything the editor calls today — this is the API the engine must cover, no more:

| Used | Notes |
|---|---|
| BeginNodeEditor / EndNodeEditor | one canvas child, grid, interactions |
| BeginNode(+title bar) / EndNode | via BeginAppNode/BeginAppNodeRenamable wrappers |
| BeginInput/Output/StaticAttribute | pins left/right, body rows |
| Link(id, a, b) | cubic wires, hover, selection |
| IsLinkCreated / IsLinkDropped / IsLinkDestroyed + detach-drag, snap-create | CaptureAppGraphLinks |
| Node/link hover + selection (Num/Get/Set/Clear), SetNodeDraggable | selection sync, layer column lock |
| Get/SetNodeGridSpacePos, GetNodeScreenSpacePos, GetNodeDimensions | become model-space getters |
| Panning get/reset, MoveToNode | camera |
| Style colors (subset) + GridSpacing/rounding/padding/pin radii + GridSnapping/GridLines flags | |
| Pan bindings (LMB-empty / RMB drag), Ctrl multi-select | already our policy |
| MiniMap | reimplement small (rects + view box + click-jump) |

**Dropped** (carried weight today): box selector, three-button emulation machinery (bindings become
first-class), pin shape variety (circle + square only: data vs containment), attribute flags stack,
multiple editor contexts, minimap hover callbacks, auto-panning-at-edge (re-add if missed),
LinkLineSegmentsPerLength tuning, the imnodes style stack (style is a plain struct we set).

## 2. Architecture

### 2.1 Spaces — the core invariant
- **Model space**: node origins, measured node sizes, pin anchors. THE storage. Zoom-independent.
- **Camera**: `{ ImVec2 Pan; float Zoom; }` — the only transform. `screen = origin + Pan + model * Zoom`.
- No API ever returns pixels for model things: `CanvasNodePos/Size/PinPos` are model units;
  hosts transform with the camera themselves (one helper: `CanvasToScreen/ScreenToCanvas`).

### 2.2 Frame flow (single pass, same-frame geometry)
1. `BeginCanvas`: child window, grid draw, camera input (pan/zoom wheel — cursor-centered, built in).
2. Per node: `BeginCanvasNode(id)` sets the imgui cursor at `transform(origin)`, pushes the zoomed
   font + scaled layout vars (the engine owns this; hosts submit plain widgets). `EndCanvasNode`
   measures the group → size in model units stored NOW. Same frame, no read-back phase.
3. Pins: attribute begin/end capture the row's model-space anchor at the node edge.
4. `CanvasLink`: records wires; drawn in `EndCanvas` under the nodes' channel.
5. `EndCanvas`: draw wires + pins from THIS frame's model data via the camera; resolve hover
   (top-most node by submission order, then pins by radius, then wires by bezier distance); run the
   interaction FSM; report events.
- Decorations (host-drawn boxes/rails/badges) query model geometry of the CURRENT frame after the
  node's End — the stale-decoration class is unrepresentable.

### 2.3 Interaction FSM (one enum, explicit)
`None → PanCanvas | DragNodes | DragWire(detach?) | PendingMenu` — transitions from the bindings
policy (LMB-drag empty = pan, LMB node = select+drag, LMB pin = wire, RMB drag = pan, RMB click =
menu-pending, wheel = zoom, Ctrl+click = toggle-select). Drag deltas divide by Zoom once, at the FSM.

### 2.4 Measurement feedback loops
Node size = measured content each frame (imgui group rect / Zoom captured with the SAME Zoom used to
place it — exact by construction). Uniform-width constraints (layer column) stay host-side with
their deadband (§1b of phase-coherence.md); the engine gives them exact model measurements so the
loop noise shrinks to glyph rounding only.

### 2.5 Files + naming
`imgui_applayer_canvas.h/.cpp`, namespace ImGui, prefix `Canvas*` (`ImGuiCanvasStyle`,
`ImGuiCanvasIO`). No imnodes includes. nodes.cpp migrates behind a thin call-site adapter so the
diff stays reviewable; imnodes include + vendored dir dependency drop at the end.

## 3. Slices

| Slice | Contents | Exit test |
|---|---|---|
| C1 | canvas child, camera (pan/zoom native), grid, BeginCanvasNode/End with measurement, selection + drag | nodes render/drag/zoom with zero seam code in nodes.cpp |
| C2 | pins + wires + hover + create/detach/drop events | CaptureAppGraphLinks ports over |
| C3 | title bars/rename hooks, draggable locks, MoveTo/fit helpers, minimap | editor feature parity |
| C4 | Composer migration (delete the model cache, style trampoline, zoom reseat — the seam dies) | all 29 GUI tests green; zoom acid test clean |
| C5 | remove imnodes from the build | build has no imnodes |

Contract for every slice: the phase-coherence checklist passes by inspection, and the zoom acid
test (rapid wheel over every decoration) shows zero single-frame artifacts.
