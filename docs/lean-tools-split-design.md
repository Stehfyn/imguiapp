# Lean/Mean Split — `imguiapp_internal.h` + `IMGUIX_DISABLE_TOOLS`  (Phase A design)

Goal: a clean core/tool boundary so a release build compiles the authoring tools (Composer, Previewer,
Debugger — graph model, canvas, codegen, preview backends) out entirely. Result: a true "lean & mean"
runtime, and — because the tools are what bake control source + generated-code strings — no source-code
text in the shipped `.exe`. One master switch, mirroring how `imconfig.h` / `IMGUI_DISABLE` gate imgui.

Design-first (gates the code); nothing built before this doc is agreed.

## 1. Precedent (imgui)

imgui ships: public `imgui.h` + `imconfig.h`; internal `imgui_internal.h`; impl split by topic
(`imgui.cpp` / `imgui_draw.cpp` / `imgui_tables.cpp` / `imgui_widgets.cpp` / `imgui_demo.cpp`). We mirror
this: keep the per-topic `.cpp` split (NO `imguiapp_widgets.cpp` — `imguiapp_canvas.cpp` + `imguiapp_nodes.cpp`
stay separate), add an `imguiapp_internal.h`, and gate the tool TUs behind one macro.

## 2. Core vs tool classification

**THE RULE (decided): if it's UI, it's a tool; anything else is NOT a tool.** So the switch
(`IMGUIX_DISABLE_TOOLS`) gates UI only; all non-UI machinery stays core.

**CORE — always on (everything that is not UI):**
- `imguiapp.h` (already `[SECTION]`-structured like imgui.h) + `imguiapp.cpp`: the runtime — `ImGuiApp`,
  layers, controls, `RegisterAppStorage`, `UpdateApp`/`RenderApp`, `ImGuiAppStateHistory`, `ImGuiAppWAL`,
  commands, `GetAppCompositionID`, `ImGuiAppPlatform`/backends. `imguiapp_config.h` (gains the switch, §4).
- **Graph MODEL + CODEGEN** (not UI): `ImGuiAppGraph`/`AppGraph*` (add/serialize/validate/topo),
  `GenerateApp*Code` + the emitter/importer. (Lives in `imguiapp_nodes.cpp` today, intertwined with the
  editor UI — §MIXED.)
- **RECORDER + encoder** (records with tools OFF): `AppRecord*`, `AppMetaRecord*`, `ImGuiAppRecorder`,
  `ImGuiAppMetaRecorder`, `ImGuiAppAVEncoder` seam, `ImGuiAppAVMeta*` write, frame capture. Impl may move
  into `imguiapp.cpp`; the backends' encoder-hook is core.
- **DECODER / loader / playback read side** (not UI): `AppRunOpen`/`Close`/`TickCount`/`TickAt`/
  `StateAtTick`, `ImGuiAppRunIndex`, the frame decode. (F62–F65 read machinery — core; only the transport
  *UI* is tool.)
- **Interpreter CORE** (F67 headless build+run in `imguiapp_preview.cpp`): `AppPreviewCreate`/`Frame`/
  `Reconcile` + the evaluator — not UI.
- anim builtins (`ImApp*`); the AV codec (`qoi`/`libav`/`mediafoundation`).

**TOOL — UI only (gated by `IMGUIX_DISABLE_TOOLS`):**
- The Composer (`imguiapp_demo.cpp`); the graph EDITOR UI (`ShowAppGraphEditor`/`ShowAppGraphTree` + inspector
  / panel rendering in `imguiapp_nodes.cpp`); the canvas UI (`imguiapp_canvas.cpp`, entirely UI); the preview
  SURFACE (F68 widget surface + brushing in `imguiapp_preview.cpp` / `imguiapp_preview_dll.cpp`); the playback
  transport UI + bug-button UI (in the demo).

**⚠ MIXED files (the real work):** `imguiapp_nodes.cpp` (model+codegen = core / editor UI = tool) and
`imguiapp_preview.cpp` (interpreter core = core / F68 surface = tool) each hold both → the UI must be SPLIT
out (own gated TU), not gated wholesale. `imguiapp_canvas.cpp` + `imguiapp_demo.cpp` are UI-only → gate
wholesale. `imguiapp_av.cpp` is record+decode, no UI → core.

**`imguiapp_internal.h`** holds the TOOL-UI + internal-helper interfaces; its tool-UI declarations are
wrapped in `#ifndef IMGUIX_DISABLE_TOOLS` so the macro hides the UI in the header too. The core
model/codegen/recorder/decoder/interpreter/anim interfaces stay in CORE headers (`imguiapp.h` / a core model
header / a core `imguiapp_av.h`), unconditional — so a lean build still links model+codegen+recorder+decoder+
interpreter+anim (a generated app still `PushAppControl<ImAppTween>` (F56) and still records/opens runs); it
drops only the UI + Phase-B's embedded source. "No source in the `.exe`" comes from gating the embed
(Phase B) — codegen skeleton strings are small and stay.

## 3. `imguiapp_internal.h`

New header, the `imgui_internal.h` analog. It AGGREGATES the TOOL-UI interfaces + internal-only helpers:
the graph editor UI (split out of `imguiapp_nodes.cpp`), the canvas UI (`imguiapp_canvas.h`), the preview
SURFACE UI (split out of `imguiapp_preview.cpp` + `imguiapp_preview_dll.h`), and the demo/transport/bug-button
UI decls. It does NOT hold the core machinery: the graph MODEL + CODEGEN, the RECORDER, the DECODER/loader
(`AppRun*`), the interpreter CORE, and anim all keep CORE headers (`imguiapp.h` / a core model header / a
core `imguiapp_av.h`). `imguiapp_internal.h`'s tool-UI decls are wrapped in `#ifndef IMGUIX_DISABLE_TOOLS`
(the macro hides the UI in the header too); the core headers it does NOT own stay unconditional. The deep
reorder is Phase C; Phase A is header re-home + the mixed-file UI split, not a rewrite.

## 4. The gate

- `imguiapp_config.h`: `// #define IMGUIX_DISABLE_TOOLS` documented; when defined, the tools compile out.
- `imguiapp_internal.h`'s tool-UI declarations are themselves wrapped in `#ifndef IMGUIX_DISABLE_TOOLS` …
  `#endif` — the macro HIDES the UI in the HEADER too (a lean consumer that includes `internal.h` sees no UI
  API), not just in the impl. Any non-UI internal helper decls stay unconditional. This does NOT strand the
  lean runtime: the UI=tool rule already moved anim / decoder / model / codegen / interpreter interfaces OUT
  of `internal.h` into CORE headers, which stay unconditional — so a lean build still links them.
- The UI-only impl TUs likewise wrap their bodies (imgui's `IMGUI_DISABLE` pattern): `imguiapp_canvas.cpp`,
  `imguiapp_demo.cpp`, the graph-editor-UI TU split out of `imguiapp_nodes.cpp`, and the preview-surface UI
  split out of `imguiapp_preview.cpp` / `imguiapp_preview_dll.cpp`. The MIXED files' CORE remainder
  (`imguiapp_nodes.cpp` model+codegen; `imguiapp_preview.cpp` interpreter core; `imguiapp_av.cpp` record+decode)
  is NOT gated.
- CMake (`imguix/CMakeLists.txt`): the tool sources stay in `IMGUIX_SOURCES` (they self-empty); add an
  option `IMGUIX_ENABLE_TOOLS` (default ON) that, when OFF, adds `IMGUIX_DISABLE_TOOLS` to
  `IMGUIX_COMPILE_DEFINITIONS`. The demo/composer executable target is not built in the lean config.

## 5. Acceptance

- Normal build (tools ON): nodes / core / headless suites unchanged + green — this is a pure re-home + gate,
  zero behavior change.
- Lean build (`-DIMGUIX_ENABLE_TOOLS=OFF`): the core library + a minimal host compile and link with ZERO
  tool symbols; a `strings` scan of the lean binary shows none of the control/source/generated-code text the
  tools embed.
- The core/tool boundary (this doc's §2) is the contract Phase B (embed) and Phase C (schema) build on.

## 6. Non-goals (deferred)

Deep declaration/definition reordering + comment discipline = Phase C. Source embedding = Phase B. This
phase only moves interfaces into `imguiapp_internal.h` and adds the gate.
