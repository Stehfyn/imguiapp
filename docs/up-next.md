# Up Next

Living document. Rewrite freely; history lives in git. Priorities follow the project's spine:
**framework first — the Composer is flagship dogfood proving the idioms. Interaction / creation /
generation now; live debugging deliberately parked.**
(See [big-idea.md](big-idea.md) for the concepts these items serve.)

## North star: the Lifecycle view (authoring-UI direction)

Unity's script-lifecycle flowchart (docs.unity3d.com/Manual/execution-order.html) is the model, upgraded
from documentation to authoring surface. The Composer's root becomes a generated, editable lifecycle chart
of the USER'S composed app:

- **One vertical spine = the frame**, loop-back edge included; the four layers are labeled phase bands
  (ingest -> command -> publish -> render), bracketed by **Initialization** (push order) and
  **Decommissioning** (reverse-pop) bands.
- **Event slots inside each band, in true call order**: Task band lists every control's
  `OnUpdate(dt, temp, last_temp)` in dependency order; Command band `OnGetCommand` collection ->
  `OnExecuteCommand` dispatch; Window band per-window `Begin -> OnRender -> hosted OnRender -> End`.
- **Framework internals as grey context rows** (Unity's "internal subsystem" boxes): `TempData = {}`,
  the `LastTemp = Temp` swap -- the phase fix drawn where it happens.
- **The one-frame skew arrow**: TempData recorded in frame N crossing the frame boundary into frame N+1's
  update. Unity's chart has no such edge; it is this architecture's signature.
- **Editable, not just legible**: dragging a slot reorders the push; codegen emits the new order. The chart
  is both the document and the input.

## Now

- **UI coherence program** — [composer-ui-design.md](composer-ui-design.md): lateral interaction
  ties (brushing & linking across outliner/canvas/inspector/code/problems) + canvas refinement
  measured against Blender/UE/Unity, grounded in Cognitive Dimensions of Notations. S1 landed;
  next slice S2 (annotation frames, align/distribute; minimap already existed via imnodes).

- **Sequence-order editing** — drag execution badges to reorder pushes inside a scope (plus a
  click-nudge fallback); order is model state, codegen emits it. (Reading the sequence exists;
  writing it doesn't.) Hard constraint: the four core phase layers never reorder relative to one
  another — only pushes within a scope / custom layers move.
- **Scope-local tidy polish** — per-scope sequence layout (arrange members left-to-right in
  execution order on demand), remember camera per scope.

## Soon

- **Round-trip tests for events + scopes** — save/load/undo/copy of `ImGuiAppEventDesc`; scope
  navigation invariants (validate-on-mutate, breadcrumb chain == scope-parent chain).
- **Import round-trip growth** — parse full control skeletons (not just structs) back into nodes;
  the inverse of `AppEmitControlWithDeps`.
- **Prefabs on disk** — the in-memory prefab registry serialized; a starter library of composed
  idioms (producer/consumer pair, event→command control).
- **Edge helpers, considered** — `ImAppRising/Falling/Changed` exist but are tabled; codegen emits
  raw operators (`temp ^ last` stays visible and greppable). Ideas when revisited: a codegen style
  toggle (raw vs named); or an `ImAppEdge<T>` watcher that pairs a temp field with its last value
  and exposes `.Rising()/.Falling()/.Changed()` — worth it only if it stays a zero-cost view over
  the existing TempData/LastTempData pair, never a third copy.

## Horizon (named, not scheduled)

- **Module interop** — the layer model's full shape: independent modules (worker threads, async IO,
  external processes) whose status the Task layer ingests, whose commands the Command layer
  receives, and who query the status the Status layer publishes. Graph-side this wants a
  Source/Module node kind; runtime-side a status/command exchange. Single-app authoring comes first.
- **Status layer model** — replace "the status bar" with a queryable published-status structure
  other modules (and the Composer) read.
- **Command payloads** — commands are bare enums, interim by intent; arguments belong in a queue,
  not the enum.
- **Live debugging / mirror investment** — `GetAppCompositionID` is ready to gate
  `BuildAppLiveGraph` reconciliation when this resumes.
- **Edit-intent bus** — the doc-control's "const dependency that panels mutate" escape hatch is
  acknowledged tech debt; fold into the command pipeline once payloads exist.

## Recently landed (2026-07-01)

- **Viewport chrome** (composer-ui-design.md §7) — chrome sorted by what it acts on: document
  toolbar (Generate carries graph health as its state, UE-Compile-style; file/edit verbs; problems
  chip + panel/live/App-time toggles right-aligned), view gizmo column overlaid top-right on the
  canvas (add/frame/fit/tidy/snap + overlays popover: grid/bands/frames/minimap), and a real status
  bar at the window bottom (live keymap hints via `AppGraphStatusHint` + breadcrumb/counts/mirror
  facts). The old dead strip died; the grid no longer cuts through group caption chips.
- **S1 coherence core** (from composer-ui-design.md) — brushing hover sync (`AppGraphHoverNode/Link`
  + `AppGraphHovered*`, one-frame latency): outliner row hover halos the canvas node, canvas hover
  tints the outliner row, wire hover halos both endpoints, inspector binding rows light their wire,
  problems rows preview their node. Status line along the canvas bottom states what the mouse does
  right now (hover-target keymap hints, Blender-style) and absorbs link-rejection messages. Ambient
  problem marks from a signature-keyed validation cache (`AppGraphIssuesCached` /
  `AppGraphNodeSeverity`): severity dot on the canvas title bar, underline on the outliner row,
  issue rows at the top of the inspector.

- **Event expression checking** — `AppEventExprCheck`: tiny grammar (field refs
  `temp_data->x` / `last_temp_data->x` / `data->x` / `<dep_param>-><field>`, struct members via
  `.`, literals, parens, scalar operators at C precedence; `^` pairs bools — the change idiom — or
  ints) parsed + type-checked against the effective field lists, result checked against DstField.
  Wired into AppGraphValidate (error severity) and the events editor (live inline diagnostic).
  Expr is still emitted verbatim, but a checked one is analyzable data, not a string.

- **Core phases are immutable; Custom layers are authorable.** The four core layers lost their type combo
  (they were never interchangeable) and cannot be added, deleted, or retyped. New `ImGuiAppLayerType_Custom`:
  a renamable, deletable layer node whose name IS its generated `ImGuiAppLayer` subclass -- codegen emits the
  skeleton + `ImGui::PushAppLayer<Name>` at its stack position; palettes offer only "Custom Layer".
- **Canvas de-noise**: grid dropped to a whisper (it cut through the pipeline box, bands and caption plates),
  the "App Layers" chip went opaque, and phase bands hug the nodes instead of running through the margin.

- **Time travel (flagship)** — `ImGuiAppStateHistory` in core: byte-snapshot ring over snapshottable
  storage (push helpers auto-register sizes for trivially-copyable instance data), keyed to
  `GetAppCompositionID`. Contract 7 proves restore-and-replay reproduces trajectories exactly; the
  Composer toolbar's "App time" scrubber freezes and rewinds the mirrored app live.
- **Codegen proof** — canonical graph → generated header byte-diffed against `tests/data/` (drift),
  then that header is COMPILED and RUN: the `^` event mutates PersistData, the rising event
  dispatches a real AppCommand. First catch: emitter now qualifies `ImGui::Push*` so output
  compiles standalone.
- **Executable contract** — `tests/imgui_applayer_core_tests.cpp`: the pipeline semantics as checks
  (UCR phase order, edge-once, same-frame command latch, dedup dispatch, pop symmetry, render
  purity, time travel). Standalone runner; the ODR collision with the test engine's sample
  `ImGuiApp` is resolved for real by renaming that identifier in exactly the two sample TUs
  (`ImGuiApp=ImGuiTeSampleApp` compile definition — the submodule is untouched).
- **ImGuiAppWAL** — write-ahead logger in core: records flushed BEFORE the operation they name, so a
  crash's forensics are `tail`-deep. Attach to `ImGuiApp::WAL`; Lifecycle level logs composition
  changes/storage/commands, Frame level logs every phase begin.
- **Assert forensics** — `IM_ASSERT` routed (via `imguix_imconfig.h` / `IMGUI_USER_CONFIG`) to a
  sink that writes expr + location + symbolized `ImStackTrace` to the assert WAL + stderr, then
  debug-breaks or exits. No CRT popup, no debugger required — it localized its first two bugs the
  day it landed.

- Renamed the dogfooded editor: **Metrics/Debugger → Composer** (title now matches its job).
- Layer vocabulary aligned to the module-interop loop across node roles, scope captions, and docs:
  ingest & update → handle intents → publish → render (pure).
- Drill-down scopes (Tab/Esc/breadcrumb, sequence badges, scoped adds, outliner auto-navigation).
- Authored events: `when <temp> <edge> → set field / emit command`; codegen emits the `^` blocks
  and the same-frame command latch.
- Core hardening: command dispatch was dead code — now collects all controls; control updates moved
  to the Task layer (update→command is same-frame); pop symmetry (`UnregisterAppStorage`);
  `ForEachAppControl`, `GetAppCompositionID`, `ImAppRising/Falling/Changed`.
- Structural invariants in `AppGraphValidate`: forest containment, edge kind-pairing,
  one-producer-per-type, event confluence.
