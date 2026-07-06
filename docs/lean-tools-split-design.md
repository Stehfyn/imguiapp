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

Verified against the current headers.

**CORE — always on (the runtime a shipped app links):**
- `imguiapp.h` (1503 lines) — already `[SECTION]`-structured like imgui.h; the runtime API: `ImGuiApp`,
  `ImGuiAppLayer(Base)`, `ImGuiAppControl(Base)`, `RegisterAppStorage`, `UpdateApp`/`RenderApp`, the
  push/pop helpers + `ImGuiAppControl<>` templates, `ImGuiAppStateHistory` (snapshot/restore/replay),
  `ImGuiAppWAL`, commands, `GetAppCompositionID`, `ImGuiAppPlatform`/backends. Does NOT include any tool
  header today — good.
- `imguiapp_config.h` — the config header (imconfig analog); gains the switch (§4).
- `imguiapp.cpp` — the core impl.
- `imguiapp_anim.h` — the animation builtins (`ImAppTween`/`Timer`/`Spring`/`Pulse`) are runtime
  `ImGuiAppControl`s a shipped app can use → **CORE** (proposed; confirm).

**TOOL — gated by `IMGUIX_DISABLE_TOOLS`:**
- `imguiapp_nodes.{h,cpp}` — graph model + editor + codegen (the biggest tool surface;
  `AppGraph*`, `ShowAppGraphEditor`, `GenerateApp*Code`, the emitter/importer).
- `imguiapp_canvas.{h,cpp}` — the canvas engine.
- `imguiapp_preview.{h,cpp}` + `imguiapp_preview_dll.{h,cpp}` — the interpreter + DLL preview backends.
- `imguiapp_demo.cpp` — the Composer itself.
- `imguiapp_av.{h,cpp}` + `imguiapp_qoi.{h,cpp}` + `backends/imguiapp_impl_qoi.*` — the run recorder + image
  encode (used by the harness, the previewer time-travel tie, and the D3 bug-button). A lean shipped app
  does not record → **TOOL** (proposed; confirm — `ImGuiAppStateHistory` in `imguiapp.h` stays core; only
  the AV *container/meta/frame* layer is tool).

Open boundary calls for the user: (a) `imguiapp_anim.h` core vs tool; (b) `imguiapp_av`/`qoi` tool vs core.
Default above: anim = core, av/qoi = tool.

## 3. `imguiapp_internal.h`

New header, the `imgui_internal.h` analog. It AGGREGATES the tool interfaces that are today declared across
`imguiapp_nodes.h` / `imguiapp_canvas.h` / `imguiapp_preview*.h` (and the internal-only helpers). The public
`imguiapp.h` keeps only the always-on runtime API. Tool `.cpp`s `#include "imguiapp_internal.h"` instead of
each other's public headers. `imguiapp_internal.h` is itself only meaningful in a tools build (its content
sits under the same gate, §4). No behavior change — this is a header re-home, not a rewrite (the deep
reorder is Phase C).

## 4. The gate

- `imguiapp_config.h`: `// #define IMGUIX_DISABLE_TOOLS` documented; when defined, tools compile out.
- Each tool TU wraps its body: `#ifndef IMGUIX_DISABLE_TOOLS` … `#endif` — so `imguiapp_nodes.cpp` etc.
  compile to an empty object when disabled (imgui's own pattern for `IMGUI_DISABLE`). The tool declarations
  in `imguiapp_internal.h` are likewise gated, so a consumer that includes it under the macro sees nothing.
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
