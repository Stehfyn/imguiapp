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

## Refresh 2026-07-05 (F11): the new placement writers

Second-order writers landed with the P0 scope-composition push (steps 41-46) and F07's explode
factoring. The question is NOT "does the fact depend on the camera" (subsequent dependence is not
the bug class); it is the temporal one from §1: **does any value MEASURED in a prior frame (T-1)
get combined with a TRANSFORM or INPUT from THIS frame (T)** — `f(measured(T-1), transform(T))` —
or run a measure→apply→measure loop (§1b)? A T-1 value is safe across the boundary iff it is in
transform-invariant (model) units; the defect is a T-1 *pixel/screen* value meeting T's camera.

For each writer: its cross-frame reads (what was measured in another frame), the units, and the
phase relation.

| Writer (nodes.cpp) | Reads that cross a frame boundary | Phase relation | Verdict |
|---|---|---|---|
| `AppScopeComposeNewNode` (~5241) | `ScopeWallRect` (published by last frame's interior submission) | MODEL units (invariant); combined only with fixed model offsets + `owner->GridPos` (persistent state, not a measurement). No T transform meets it. | coherent — T-1 value is invariant-unit |
| `AppScopeComposeImported` (~5266) | none — `mn` is over the imported nodes' own GridPos (persistent state set at import, same call) | no measured geometry crosses a boundary | coherent |
| scoped tidy (`AppScopeTidy*` ~4894) | none — `AppLayoutPureSize` recomputes a pure model size each call (zoom-idempotent), it is not a cached measurement | no cross-frame measurement | coherent — step45 asserts placements move, GridPos intact |
| nudge (~7589) | `AppCanvasNodePos` (the engine's CURRENT model position, updated by this frame's input FSM before this handler) | model + a model-unit delta; no prior-frame pixel meets a transform | coherent — step46 covers the altitude routing |
| group drag (~6259) | (a) `_GroupFramesPrev.MinM/MaxM` — T-1 published obstacle frames; (b) `col_geom.BoxMin/Max` — the layer box | (a) T-1 but MODEL units → safe across the boundary; (b) VERIFIED T-frame: `col_geom` is populated by `AppDrawLayerGroupBox` at ~6132 THIS frame from pre-submission-owned layer rows, so `CanvasFromScreen(col_geom.BoxMin)` is T-screen⊗T-camera (same phase), not T-1-screen⊗T-camera. Mouse is `CanvasFromScreen(MousePos_now)` (T⊗T). `kEps=1.0` is the §1b deadband on republished-frame variance. | coherent — the one site that had to be *read* (T-1 vs T of `col_geom`), and it is T |
| explode anchors (`AppGraphAddExplodedField` ~2166) | none — `owner->GridPos` + fixed model slot offsets; persistent state, not a measurement | no cross-frame measurement | coherent |
| fit (`fit_all`/`fit_ids` ~7419) | `AppNodeModelSize` (T-1 measured node size) | MODEL units (the invariant cache, audit #1/#7); the bbox stays in model and only `CanvasFitRect` turns it into a camera. A T-1 model size never meets a T camera as a fact. | coherent — camera is the output |

`AddPopupGrid` crosses frames as a MODEL grid position (captured once as `CanvasFromScreen(screen)`
at palette-open, stored, consumed later) — invariant, so the later consume is not de-phased.

Correction note: an earlier draft of this section classified writers by "is the camera a model
input" and called that phase coherence. It is not — that is the subsequent-dependence framing, and
it would pass a genuinely mixed-phase writer that reads a T-1 SCREEN rect and multiplies this
frame's zoom. The table above is redone against the actual T/T-1 measure-vs-transform relation; the
only writer where the distinction bites is group drag's `col_geom` read, verified T-frame above.

Test backing (§3: a "conforming" verdict must be read, not sampled): step45/46/43 + canvas_c1/c4
exercise these writers at zoom≠1 and drilled altitudes — a T-1-pixel⊗T-transform slip would flash.
GAP entered as a finding: the group-drag obstacle-clamp path (the `col_geom`/`_GroupFramesPrev`
reads) has no direct zoom≠1 regression test; it rides the `CanvasFromScreen(mouse)` mechanism
canvas_c1/c4 prove, but the clamp arithmetic itself is unexercised at zoom≠1.

## Completed checklist items — phase-coherence sweep (2026-07-05)

Phase coherence is a property of PER-FRAME render code that sizes / places / styles UI from
MEASURED geometry (immediate mode measures as it draws; §1). Code that does not measure geometry
each frame has no surface for the bug. Every completed checklist item classified by that surface:

| Item | Production code added | Phase surface? | Verdict |
|---|---|---|---|
| F01 round-trip | test + `AppGraphModelEqual` | none | n/a — model-equality over serialized fields |
| F02 Init=/Dock= | `AppEmitNodeRecord`/`AppGraphDeserialize` keys | none | serialize/parse of persistent `GridPos`/flags as DATA; never measured |
| F03 events | test only | none | n/a |
| F04 prefabs | sidecar serialize/deserialize + `AppGraphSeedStarterPrefabs` | none | serialization + scratch-graph seeder; positions are persistent data |
| F05 undo roads | test only | none | n/a |
| F06 ScopeCams sweep | 3-line erase in `AppGraphRemoveNode` | none | erases transient camera records on delete; no measurement |
| F07 exploded-field routing | `AppGraphAddExplodedField` (place), `EditAppNodeFieldSection` (render) | **yes (read)** | placement is a ONE-SHOT mutation: `owner->GridPos` (persistent model) + fixed `field_off` (model, slot index) → member `GridPos`/placement; no per-frame measurement, no transform; `_NeedsPlace` is the deliberate-T+1 idiom (rule 4). Inspector render is em-scaled widget rows (this-frame `GetFontSize`), no cached measurement. Coherent. |
| F08 history jump | test only | none | n/a |
| F11 audit refresh | doc only | none | this document |
| F12 dep validator | binding check in `AppGraphValidate` | none | pure model validation |
| F13 canvas input tests | test only | none | tests ASSERT engine coherence (drag delta == px/zoom at zoom≠1) — no new render code |
| F14 one feedback slot | REMOVED the canvas toast; status-hint expiry 3.0→2.5s | removal only | deletes a per-frame draw (which was itself this-frame `GetItemRect`, coherent); the retained status hint is a text notice on a `GetTime()` timeout — no geometry, no measure→apply loop |
| F15 assert ring dump | av registry + `AppDumpAssertRings` + assert call | none | AV/assert path; `AvEmitPlaceholder` synthesizes a frame from the locked size, no cross-frame measurement |

Conclusion: only F07 added placement/render code, and it carries no frame-phase dimension (a
one-shot model mutation + an em-scaled widget list). The rest are serialization, validation, model
mutation, AV, or tests — no measured-geometry surface. No phase-coherence defect introduced by any
completed item. (The open GAP above — group-drag clamp at zoom≠1 — predates these items; it is
existing editor code the F11 refresh surfaced, not something a completed feature added.)

## Render-phase mutation sweep (2026-07-05) — and a correction to the F11 refresh

The F11 refresh audited the new writers by their READS (all model-unit — correct as far as it went)
but did not ask WHICH PHASE does the write. The group-drag row was marked "coherent" on that basis.
It was not: the drag mutated member positions INSIDE the canvas render pass (between `CanvasBegin`
and `CanvasEnd`), so its slide-to-contact clamp could only read last frame's complete obstacle set
(`_GroupFramesPrev`) while this frame's (`_GroupFrames`) was mid-publication — the third species
(phase-coherence.md §1c). Symptom: a group could not reach contact with the layer column and clipped
past neighbours. Fixed: the drag records `_GroupDragPending` in the pass; the clamp + writes run after
`CanvasEnd` against this frame's `_GroupFrames` + `_LayerBox`. The F11 GAP (untested clamp) is now a
real bug the fix closes; step33/34/35 stay green, core 87/0, headless 10/10.

Sweep of every model mutation between `CanvasBegin` and `CanvasEnd` in `ShowAppGraphEditor`:

| Site (nodes.cpp) | Write | Verdict |
|---|---|---|
| group-drag clamp (was ~6259) | member `GridPos` / placements / `AppCanvasSetNodePos` | VIOLATION (§1c) — moved to the post-`CanvasEnd` update pass |
| add / delete / duplicate / reparent / explode / collapse / paste | node & link mutations | OK — recorded as `pending_act` / `c->Act` / `added_id`, applied AFTER `CanvasEnd` |
| `_NeedsPlace` + `AppCanvasSetNodePos(g, n, AppNodeScopePos(g,n))` (~6612) | engine seat from stored placement | OK — pre-submission SEATING (persistent model → engine, before the node draws; the input-FSM class, not a mutation of a measured fact) |
| `GroupCollapsed = !GroupCollapsed` (fold-click, ~6269) | transient view toggle | OK-ish — discrete UI state read next frame, feeds no measurement; not the measured-geometry class |

Standing audit rule (now in phase-coherence.md §1c): grep the render pass for model writes; each must
be a recorded intent applied post-submission, except pre-submission seating and measurement-free view
toggles.
