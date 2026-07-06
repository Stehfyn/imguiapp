# Feature Checklist (v2 — post-100)

Forward work items, one deliverable + acceptance gate each, detailed to the file / struct / function /
test level. The completed F01–F78.5 series is in [archive/feature-complete-checklist.md](archive/feature-complete-checklist.md)
(closure: [feature-audit-2026-07-05.md](feature-audit-2026-07-05.md)). "Green" = imguix-tests +
imguix-core-tests + imguix-headless-verify. Build: `cmake --build build/vs2026 --config Release --target
imguix-tests imguix-core-tests imguix-headless-verify`. Naming: `AppXxx` free functions live in
`namespace ImGui`; graph-model types are aggregates (no ctors); tests are `stepNN_*` / `canvas_*` (nodes),
`Test_*` (core), `composer_*` (headless).

## V1 — see the DLL preview live in-panel  (IN PROGRESS)

- [ ] **V1 DLL preview in-panel render** — the F78 copy-marshalling preview compiles + runs real code but
  renders in the module's OWN `ImGuiContext` (headless, no GPU); its frame must cross the C-ABI as bytes and
  display in the Composer's Preview panel. Closes the F78/F78.5 in-panel residual.
  - **Emitted module ABI** (`GenerateAppPreviewModuleCode`, `imguix/imguiapp/imguiapp_nodes.cpp`): add an
    `extern "C" __declspec(dllexport) int ImGuiAppPreview_CopyFrame(void* h, unsigned char* rgba, int cap,
    int* out_w, int* out_h)` alongside `_Create/_Destroy/_Tick/_CopyIn/_CopyOut/_ABI`; keep it additive so
    the codegen corpus (`ProofDrift`/`ProofControlDrift`) stays byte-identical for bodyless graphs.
  - **Offscreen render in the module** (preferred): reuse the offscreen render+readback the harness uses —
    grep `tests/imguiapp_headless_verify.cpp` + `imguix/imguiapp/backends/` (the vulkan offscreen +
    `imguiapp_impl_qoi`); `_Create` sizes an offscreen target, `_Tick` renders the DLL's `ImDrawData` into
    it, `_CopyFrame` reads the RGBA back. Fallback: serialize `ImDrawData` + font atlas across and replay
    into the panel draw list with a TexID remap.
  - **Session** (`ImGuiAppPreviewDll`, `imguix/imguiapp/imguiapp_preview_dll.{h,cpp}`): add
    `AppPreviewDllCopyFrame(session, rgba, cap, &w, &h)` forwarding to the module proc.
  - **Display** (`imguix/imguiapp/imguiapp_demo.cpp`, the `"Preview"` `BeginTabItem` ~line 2692): when a
    toolset exists + DLL backend active, upload the frame as an `ImTextureData` and `ImGui::Image()` it in
    the panel — the same texture path F63 uses for decoded run frames; else keep the interpreter surface
    (`AppPreviewCreate`/`AppPreviewFrame`) as the fallback.
  - **State**: on `ImGuiAppEditorState` (no TU globals). Preview-tab backend toggle: DLL when
    `AppPreviewDllToolsetAvailable()`, interpreter otherwise.
  *Accept: a headless core test (skip-with-note without a toolset) builds a one-control graph with a visible
  widget (or a `MethodBody` OnRender drawing a filled rect), compiles+loads via the DLL backend, ticks,
  `CopyFrame`s, and asserts the frame is NON-BLANK (a pixel differs from the clear colour). ProofDrift green.*

## V2 — author control method bodies in-app

- [ ] **V2 method-body editor** — F78.5 stores + compiles hand-written bodies
  (`ImGuiAppNodeDraft::MethodBody[ImGuiAppControlMethod_COUNT][IMGUIAPP_CONTROL_BODY_MAX]`,
  `imguix/imguiapp/imguiapp_nodes.h`) with no UI. Add the authoring surface + wire the reload.
  - **Inspector section** (`imguix/imguiapp/imguiapp_nodes.cpp`, near `EditAppControlEvents` /
    `EditAppNodeFieldSection`): a collapsible "Code" section on a design Control with one multiline
    `ImGui::InputTextMultiline` per `ImGuiAppControlMethod_` (`###body_<method>`), writing into
    `Draft.MethodBody[m]`. Section-collapse persistence via the `AppInspectorSection` `persist_seed` idiom.
  - **Reload**: an edit bumps the doc dirty bit; the Preview tab calls `AppPreviewDllReload(session, graph,
    err, sz)` (already preserves Persist by copy) so the new body runs next frame with state kept.
  - **Interpreter reflection**: `imguix/imguiapp/imguiapp_preview.cpp` should mark a control carrying any
    non-empty `MethodBody` as REFLECTED (field-widget card + "runs in the DLL preview / after Generate")
    rather than interpreting it — per previewer-design.md §9.
  *Accept: `stepNN_method_body_editor` (nodes) types C++ into the OnRender field and asserts it lands in
  `Draft.MethodBody` + round-trips (save/load model-equal via the archived `AppGraphModelEqual` extension);
  a headless test rewires a body and asserts the DLL preview reflects it next frame without losing state.*

## V3 — Lifecycle view (north-star authoring surface)

- [ ] **V3 lifecycle chart** — the Composer root as a generated, editable lifecycle chart of the user's app
  (Unity execution-order model, upgraded to an authoring surface).
  - **Substrate** (exists): `ImGuiAppScopeOrder` + `ImGuiAppGraph::ScopeOrders` + `AppScopeSequenceIds` /
    `AppScopeApplyAuthoredOrder` (F58) are the order model; `AppRebuildUpdateOrder` (`imguiapp.cpp`) is the
    live per-control OnUpdate order; `AppScopeOrderMoveMember` / `AppScopeOrderNudge` are the write verbs.
  - **Render** (new, e.g. `imguix/imguiapp/imguiapp_lifecycle.cpp` or a canvas mode): one vertical spine =
    the frame with a loop-back edge; the five core layers (`ImGuiAppLayerType_Task/Command/Status/Layout/
    Display`) as labeled bands bracketed by Initialization (push order) + Decommissioning (reverse pop);
    per-control `OnUpdate(dt,temp,last_temp)` slots inside the Task band in dependency order; grey
    framework-internal rows (`TempData = {}`, `LastTemp = Temp`); the one-frame skew arrow.
  - **Editable**: dragging a slot calls `AppScopeOrderMoveMember`; codegen emits the new push order (F59).
  *Accept: `composer_lifecycle_view` (headless) renders the bands + slots in `AppRebuildUpdateOrder` order and
  a drag reorders a push (order record + emission change), core layers refuse reorder (`AppLayerIsCore`).*

## V4 — module interop

- [ ] **V4 module node + runtime exchange** — the layer model's full shape: independent modules (worker
  threads / async IO / external processes) the Task layer ingests, the Command layer drives, the Status
  layer publishes for.
  - **Graph**: new `ImGuiAppNodeKind_Module` (append to the enum in `imguix/imguiapp/imguiapp_nodes.h`,
    serialized as int — mind the F54/F57 enum-slot discipline); palette legality via
    `AppScopeKindComposable`; ports for status-out / command-in.
  - **Runtime** (`imguix/imguiapp/imguiapp.h`): a status/command exchange (e.g. `ImGuiAppModule` with a
    thread-safe status snapshot + command inbox) the Task layer reads each frame.
  - **Codegen**: emit the module bring-up + the Task-layer ingest.
  *Accept: a compiled+run test — a module node's status reaches a consumer control and a command reaches the
  module; F01/F05 cover the new records.*

## V5 — status-layer model

- [ ] **V5 queryable status** — replace the status-bar strings with a published structure other modules +
  the Composer read. New `ImGuiAppStatus` (published by the `ImGuiAppLayerType_Status` layer); `AppGetStatus`
  accessor; the Composer's status chrome reads it instead of ad-hoc strings.
  *Accept: a control publishes a status field and another reads it through the layer; a test asserts the
  published value.*

## V6 — command payloads

- [ ] **V6 command arguments** — commands are bare `ImGuiAppCommand` enums today (interim by intent).
  Arguments belong in a queue. Add `ImGuiAppCommandQueue` (typed payloads) alongside the enum; extend
  `OnGetCommand` / `OnExecuteCommand` (`imguix/imguiapp/imguiapp.h`) + codegen to carry a payload.
  *Accept: a compiled+run test dispatches a command with an argument and the handler receives it; contract
  suite (archived contracts 3/4: same-frame latch + dedup) extended for payload identity.*

## V7 — edit-intent bus

- [ ] **V7 fold the doc-control escape hatch** — the composer doc-control's "const dependency that panels
  mutate" is acknowledged tech debt (see the host command bus in `imguix/imguiapp/imguiapp_demo.cpp`). Once
  V6 payloads exist, route panel edits as commands through the one dispatcher (sibling to the shipped
  input→command binding, archived F74/F75: `AppGraphConsumeHostCommand` / the `Keymap`).
  *Accept: a panel mutation flows as a payload command, no direct const-dep write; a test asserts the edit
  applies via the command path.*

## V8 — reliability + tracked residuals

- [ ] **V8a step93 flake** — `step93_order_roundtrip` (nodes) is flaky (~1-in-2). Root-cause the `Order=`
  path in `AppGraphDeserialize` (`imguix/imguiapp/imguiapp_nodes.cpp` ~line 13033) + the surrounding
  `ImGuiAppScopeOrder` handling (F60 fixed one use-after-free; another nondeterminism remains).
  *Accept: step93 green 100 consecutive runs.*
- [ ] **V8b canvas/scope residuals** — the archived F47/F48 follow-ons: interactive Note resize handle +
  per-note colour read (`ImGuiAppNodeKind_Note` / `NoteColor` / `AppDrawScopePortals` already exist);
  outliner note ordering; heavier scope-chrome pixel-extent tests.
  *Accept: each residual has a test; the note colour/resize round-trip.*
- [ ] **V8c chrome test-debt** — the archived F40 low-value residuals (undo/redo/history clicks,
  Diff-in-panel mode, theme desc tables).
  *Accept: each has a click-path or draw-scan test.*
