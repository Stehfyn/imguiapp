# Phase-coherence audit — ImGuiApp editor, 2026-07-03

Updated same day, second pass: dependency ORDER + Data/TempData/LastTempData conformance.

Method: every geometry read in `imguiapp_nodes.cpp` / `imguiapp_canvas.cpp` classified by
(a) draw phase (pre-submission vs post-submission consumer), (b) units crossing frame
boundaries, (c) producer of the fact and whether consumers re-derive it. Rules from
[phase-coherence.md](phase-coherence.md). Line numbers as of this commit.

## Fixed in this pass

| # | Site | Violation | Fix |
|---|------|-----------|-----|
| 1 | `AppGroupAccumulate` (nodes.cpp ~4630) | group bounds from `GridPos` = last frame's read-back; frames lagged any node drag one frame | bounds read `AppCanvasNodePos` (input FSM runs before submission) + settled size; model units, no camera term |
| 2 | `group_box` chrome (nodes.cpp ~5389) | pad/title band measured under the pushed font (rounds + clamps per zoom) then divided back by scale → published frame extents changed per zoom step | chrome from `FontSizeBase` (the model-unit em), camera-free |
| 3 | group-frame publication (nodes.cpp ~5399) | model rect derived via screen round-trip (`CanvasFromScreen(CanvasToScreen(model) + chrome_px)`) | derived model-side; screen rect is the draw-time transform of the published fact (rule 5) |
| 4 | title-bar-drag clamp vs measurement variance | T+1 measurements carry bounded sub-unit variance (font rounding, content wobble); republished frame extents breathe by it, and the old all-or-nothing gate turned any hairline overlap into free interpenetration | clamp treats penetration within `kEps = 1.0` (the variance bound) as contact and resolves it back to contact; sizes stay plain T+1 engine measurements — no settledness state, no acceptance rules (a "settle" mechanism is itself the bug class) |
| 5 | title-bar-drag clamp gate (nodes.cpp ~5470) | ANY start overlap disabled the whole slide-to-contact → groups drove through each other | per-obstacle drop + `kEps` float tolerance; contact never reads as penetration |
| 6 | section packer during layer drag (nodes.cpp ~3747) | packer ran post-submission only → seated windows/sidebars trailed the dragged Window layer row by one frame | packer also runs in the pre-submission pass with the constrain, same frame as the row move |
| 7 | `AppNodeModelSize` (nodes.cpp ~1336) | read through `g_editor_pool_graph` TU static → dangling pointer segfault when another graph replaces the editor's | caller passes its live graph |
| 8 | layer-box published bounds (nodes.cpp ~3576) | same pushed-font chrome defect as #2 for `BoxMin/BoxMax`/`SilRight` | chrome from unzoomed metrics x zoom |
| 9 | layer-drag transients (nodes.cpp ~3690) | 5 function-local statics carried per-graph drag state | moved to `ImGuiAppGraph::_LayerDrag*` |

## Fixed in the second pass

| # | Site | Violation | Fix |
|---|------|-----------|-----|
| 10 | re-layout cascade on first submission / content change | derived facts updated ONE PER FRAME (sizes at read-back, shared width the NEXT pre-submission pass, pack/seat after that): a multi-frame visible re-layout on live toggle | the whole update stack runs post-read-back in dependency order — width -> column pack -> section seat -> drag-stick — all consuming this frame's measurements; content arrival re-layouts exactly once. Render is never suppressed: render measures (TempData), update derives, display displays. |
| 11 | `g_app_layer_uniform_w` | ratchet fact shared across ALL graphs: one wide graph permanently widened every other graph's layer column (tests cross-contaminated) | per-graph `_LayerUniformW` + `AppLayerUniformW(g)` floor accessor |

## Order verification (the stack)

Frame order in `ShowAppGraphEditor`, producers before consumers:
input FSM (engine positions) -> pre-submission layer drag: constrain + seat (row + members final)
-> layer box (consumes engine pos + T-1 sizes; publishes col_geom) -> group frames (same inputs
+ `_GroupFramesPrev`; publishes `_GroupFrames`) -> submission (renders + measures: T's sample)
-> read-back (`GridPos`) -> update stack: width -> constrain -> seat -> drag-stick.
One law throughout: T's measurement is consumed at T+1, in model units, by every consumer alike.

## Conforming (pattern check)

- `_GroupFrames` / `_GroupFramesPrev`: publish/read pair, single producer, swap at pass start.
- Node sizes: engine record = T's measurement, read at T+1; no state machine on top.
- Hover bus (`AppGraphHover*`): record-current / read-previous / rotate-on-first-touch — the
  TempData/LastTempData shape; storage is an app-singleton by design (brushing across views).
- Issues cache: signature-keyed recompute, severity map cleared on rebuild; alternating two graphs
  thrashes recompute (perf only, results correct).
- Drag captures (`_GroupDragOrig`, `_DragStick`, `_LayerDrag*`): originals at grab, absolute
  re-derivation per frame.

## Open findings (not fixed here)

| # | Site | Finding |
|---|------|---------|
| A | live-toggle first submission | new live nodes enter at per-kind ESTIMATES (`AppLayoutNodeSize`), measured size lands T+1 → visible settle flicker when the Window layer starts empty. Violates rule 4 (T+1 must produce no visual artifact). Needs a hidden measure pass or estimate == measured invariant. |
| B | node content width wobbles on hover | raw re-measure moved 0.56 px at fixed zoom when the pointer left the node (szdbg trace, controls id 13/25). Some node-body row renders hover-dependent width. Deadband (#4) hides it; the row itself should be found and made hover-invariant. |
| C | test-driven zoom never applies | `CanvasSetZoom` from outside is overwritten next frame by the view-state mirror (~5233 / ~6411). Any harness zoom test must go through the same channel the app uses, or the view-state owner must accept external camera writes. |
| D | editor state migration (2026-07-04: DONE for file scope) | All editor-session file statics moved to `ImGuiAppEditorState` riding `ImGuiAppGraph::_Ed` (hover bus, issues cache, status hint, pool ids, host commands, one-shot requests, view state, drag-detach, per-graph canvas — `g_editor_pool_graph`/`g_app_canvas` deleted, canvas is per-document). BL edit/drag FSM and the rename scaffold latch moved to ImGui per-window state storage. Public API threads the graph (`AppGraphHover*`, `AppGraphViewState`, `AppGraphEditorCanvas`, host commands, requests, status hint); demo + tests updated; `AppLayerDemoGraph(host)` resolves the demo doc via the app storage registry. 2026-07-04 second pass: the ~20 function-local UI latches (`s_help`, `s_quick_insp`, `s_title_editing`, `ctx_node/link_id`, `add_popup_grid`, `s_fit_all_countdown`, toast/error latches, `s_rename*`, `s_kind_vis`, all three text filters) and `g_app_undo` now live on `ImGuiAppEditorState`. `_Ed` is a lazily created POINTER member: `ImGuiAppGraph` is a reflected model type, and session state embedded by value enters the reflection decomposition (build break) and would leak into schema/serialization — `AppGraphEditorState(g)` is the accessor. Third pass, same day: zero `g_` symbols remain in the editor/canvas/demo. Style-section clipboard, graph copy/paste clipboard, and the prefab registry moved onto `_Ed` (`AppGraphPrefabCount/Name` and `SetAppCodeFont` now take `g`; `AppNodeStyleSection`, `AppGraphClipboardHasData` thread `g`). The dead `g_AppCodeFont` static became `_Ed->CodeFont`. The composer-style theme cache sits behind function-local-static accessors — the framework's own derived-cache idiom (`AppTypeSchemas`, imguiapp.cpp). The single remaining process global is `g_app_assert_wal` in core `imguiapp.cpp`: the assert-failure WAL hook, a crash-callback slot with no context available at fault time (signal-handler class). |
| E | canvas engine per-frame latches | `s_next_title/_color/_edit*` (canvas.cpp ~790) are fine single-canvas but interleave wrong with two canvases mid-frame. Belongs on `ImGuiCanvasState`. |
| F | `AppDrawLayerGroupBox` row geometry | reads `GridPos` pre-submission; coherent ONLY because layer rows and seated members are position-owned by pre-submission passes (constrain + packer). Any future user-draggable node folded into the section band re-introduces one-frame lag. |

## Pattern conformance

The framework mechanism (TempData / LastTempData: record raw, compare, mutate once, publish) is
applied in the editor as `_GroupFrames` / `_GroupFramesPrev` and now `_ModelSize` (+ per-graph
drag transients). Finding D is the list of facts still outside the mechanism.
