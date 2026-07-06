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

**CORE — slim public runtime (`imguiapp.h`, always on):**
- `imguiapp.h` (1503 lines) — already `[SECTION]`-structured like imgui.h; the runtime API: `ImGuiApp`,
  `ImGuiAppLayer(Base)`, `ImGuiAppControl(Base)`, `RegisterAppStorage`, `UpdateApp`/`RenderApp`, the
  push/pop helpers + `ImGuiAppControl<>` templates, `ImGuiAppStateHistory` (snapshot/restore/replay),
  `ImGuiAppWAL`, commands, `GetAppCompositionID`, `ImGuiAppPlatform`/backends. Does NOT include any tool
  header today.
- `imguiapp_config.h` — the config header (imconfig analog); gains the switch (§4).
- `imguiapp.cpp` — the core impl.

**INTERNAL / EXTENDED — folded into `imguiapp_internal.h`** (the `imgui_internal.h` analog; §3):
- The tool interfaces: `imguiapp_nodes.h` (graph model + editor + codegen), `imguiapp_canvas.h`,
  `imguiapp_preview.h` + `imguiapp_preview_dll.h`.
- **`imguiapp_anim.h`** — the animation builtins' interface (DECIDED: fold in).
- **`imguiapp_av.h`** — the run recorder / meta / container INTERFACE (DECIDED: fold in). The AV **codec**
  stays OUT of `internal.h` as its own backend: `imguiapp_qoi.{h,cpp}` + `backends/imguiapp_impl_qoi.*`
  (+ any libav backend).

**IMPL TUs gated by `IMGUIX_DISABLE_TOOLS`:** `imguiapp_nodes.cpp`, `imguiapp_canvas.cpp`,
`imguiapp_preview.cpp`, `imguiapp_preview_dll.cpp`, `imguiapp_demo.cpp` (the Composer), and the AV/anim impl
where tool-only.

**Open sub-question (crux of "lean"):** is `imguiapp_internal.h` itself gated by `IMGUIX_DISABLE_TOOLS`
(so anim + av + the tool API all compile OUT of a lean build — smallest binary, but a lean-built *generated
app* that used `ImAppTween` could not link it), OR is `internal.h` always-available (imgui_internal.h-style)
with only the Composer/Previewer/Debugger IMPL `.cpp`s gated (so anim/av stay linkable, just re-homed)?
The anim builtins are emitted into generated apps (F56 `PushAppControl<ImAppTween>`), which argues for the
latter. Needs the user's call before code.

## 3. `imguiapp_internal.h`

New header, the `imgui_internal.h` analog. It AGGREGATES the interfaces today declared across
`imguiapp_nodes.h` / `imguiapp_canvas.h` / `imguiapp_preview*.h`, plus `imguiapp_anim.h` and the
`imguiapp_av.h` recorder INTERFACE (the AV codec stays a separate backend), and the internal-only helpers.
The public
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
