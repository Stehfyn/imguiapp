# DLL preview design (F76)

The live preview should run the **real generated program**, not an interpretation of the graph.
F67/F68 build a second in-process backend that *interprets* the AppEventExprCheck grammar and
manifest-bound storage; faithful, but it is a re-implementation of the framework's semantics and can
drift from what `GenerateAppGraphCode` actually emits. A DLL preview closes that gap: emit the app's
C++, compile it, load it, and tick the **same code the user will ship**. The interpreter stays as the
instant / no-compiler-present fallback (F66 §9 already frames the previewer as pluggable backends).

This doc fixes the mechanism before any code, per the "spec it first" decision.

## 1. The crux: one runtime, not two

The generated app subclasses framework types (`ImGuiApp`, `ImGuiAppControl<P,T>`, `ImGuiAppWindow<T>`),
calls `ImGui::*` widgets, and its `PushAppLayer/Window/Control` helpers touch framework state. For the
host to *run* an object the DLL *creates*, host and DLL must agree on, and SHARE, exactly one of each:

- **ImGui context** — the DLL's widgets must draw into the host's context (same `GImGui`, same atlas,
  same draw lists). Two contexts = the preview draws nowhere the host can see.
- **Allocator** — an object `IM_NEW`'d in the DLL and `IM_DELETE`'d in the host (or vice-versa) must use
  one heap. Two allocators = cross-heap free = corruption.
- **Framework globals** — imguix has process-global state (e.g. the DisplayLayer's ini settings handler;
  `flow3` already documents the double-register hazard). Two copies of imguix = duplicate globals.
- **Type layout + vtable ABI** — the host reads the DLL-built object through framework base pointers, so
  struct layouts and vtable order must be identical. Guaranteed when both compile the *same* headers with
  the *same* MSVC toolset (the preview compiles against this repo's headers, and we pin the compiler — §4).

The first three are the design problem. Two candidate resolutions:

### Verdict: **shared framework core** (build change), NOT static-relink-with-bridging

- **Rejected — static relink + runtime bridging.** Let the DLL statically link imgui+imguix, then have the
  host inject its context (`ImGui::SetCurrentContext`) and allocator (`ImGui::SetAllocatorFunctions`) into
  the DLL at load. This shares the *imgui* context and heap, but does NOT share imguix's own globals: the
  DLL gets its own copy of the settings handler, the AV registry, every file-static. The preview app would
  double-register process-global handlers the moment it composes a DisplayLayer — the exact bug `flow3`
  guards. Rejectable on that alone; also fragile (every future imguix global is a latent duplicate).

- **Chosen — a shared `imguix-core` DLL.** Split the framework runtime (imgui + the `imguiapp*` core, minus
  the demo/composer/editor) into a shared library that BOTH the host exe and every preview DLL link against.
  One context, one allocator, one copy of every global, one set of vtables — by construction, not by
  bridging. The preview DLL contains only the *generated* translation unit; it is thin and fast to compile.
  Cost: a build-system change (a shared target + `IMGUI_API`/`IMGUIX_API` decorated as
  `__declspec(dllexport/dllimport)`). That cost is the correct one — it is the only option that makes
  "the DLL's object and the host are the same program" TRUE rather than approximately true.

## 2. What the emitter adds: a preview entry, not a `main()`

`GenerateAppShellCode` today appends an `AppShell : ImGuiApp` + `int main()`. Add a sibling emitter
`GenerateAppPreviewModuleCode` that appends, instead of `main()`, a C-ABI surface (extern "C", so no C++
name-mangling contract across the boundary — the objects still cross as framework base pointers):

```cpp
extern "C" __declspec(dllexport) ImGuiApp* ImGuiAppPreview_Create(ImGuiViewport* vp)
{
  AppShell* app = IM_NEW(AppShell)();   // shared allocator via imguix-core
  IM_UNUSED(vp);
  return app;                            // composes lazily on its first initialized OnDrawFrame (as today)
}
extern "C" __declspec(dllexport) void ImGuiAppPreview_Destroy(ImGuiApp* app) { IM_DELETE(app); }
extern "C" __declspec(dllexport) unsigned int ImGuiAppPreview_ABI() { return IMGUIAPP_PREVIEW_ABI; }
```

`ImGuiAppPreview_ABI()` returns a compile-time constant baked into both host and generated module; a
mismatch (stale headers, wrong toolset) is refused at load with a clear message rather than crashing on a
layout skew. No context/allocator init export is needed — imguix-core owns the single copy.

## 3. Reload lifecycle (per-instance, no TU globals)

State lives on the composer's `ImGuiAppPreviewDll` session object (rides the editor state, mirroring F68).

1. **Edit → dirty.** The composer already tracks `AppGraphSignature`; a change (layout-excluded, as F68)
   marks the DLL preview dirty.
2. **Compile (async, off the frame thread).** Regenerate the module `.cpp`, write it to a scratch dir,
   invoke the compiler (§4) into `preview_<n>.dll` (monotonic name — never overwrite a loaded DLL; Windows
   locks it). Compilation runs on a worker; the UI keeps ticking the *last-good* DLL. No frame stalls.
3. **Swap when ready.** On success: snapshot the running app's state (reuse F68's capture — Persist+LastTemp
   by (sanitized name, type, size) manifest), `ImGuiAppPreview_Destroy` the old app, `FreeLibrary` the old
   DLL, `LoadLibrary` the new, `ImGuiAppPreview_Create`, then restore the snapshot slot-for-slot (new/retyped
   fields keep their zero default — identical policy to the interpreter's reconcile, so behavior matches).
4. **Failure keeps the last good.** A compile error never tears down the running preview; see §5.

`FreeLibrary` ordering matters: destroy the app (its vtables live in the DLL) BEFORE unloading, or the
`IM_DELETE` virtual dispatch reads freed code. The monotonic-name scheme means an in-flight compile can't
clobber the DLL currently mapped.

## 4. Compiler invocation

- **Locate the toolset once.** Resolve `cl.exe` + the environment via `vswhere` → `VsDevCmd`/`vcvars64`,
  cached on the session. If none is found, the DLL backend is unavailable and the preview silently uses the
  F67 interpreter — the fallback is not an error, it is the no-compiler path.
- **Command.** `cl /LD /std:c++20 /O2 /MD <module.cpp> /I<repo include dirs> /link imguix-core.lib` →
  `preview_<n>.dll`. `/MD` (dynamic CRT) so host and DLL share one CRT heap — required for cross-boundary
  new/delete even with the shared allocator, because framework code may allocate through the CRT.
- **Pin the toolset** to the one that built the host (record its version; the ABI constant in §2 encodes
  it). A preview built by a different compiler is refused, not run.
- Compilation is the latency cost of fidelity: a thin generated TU against a prebuilt `imguix-core.lib`
  import lib compiles in well under a second; that is the "runtime" in "runtime live preview".

## 5. Error surfacing

Capture the compiler's stderr. On failure, the preview panel shows the diagnostics (file:line from the
generated `.cpp`, mapped back to the offending node via the existing `ImGuiAppCodeSpan` source-map so a
compile error highlights the node, not an opaque line number) and a "using last good build" banner. The
previously-loaded DLL keeps running. This is strictly better than the interpreter, which can only refuse
edits it cannot model; the DLL preview refuses only edits that do not COMPILE, and says exactly why.

## 6. Testing (headless, on-camera)

- `dll_preview_roundtrip` (headless): author a one-control graph, drive the DLL backend through a compile +
  load + create, tick frames, drive a widget, assert the model value moved — the same assertion F68's
  `step102` makes against the interpreter, now against real compiled code. Skipped-with-note when no
  compiler is present on the box (CI without a toolset), never silently vacuous.
- `dll_preview_reload_preserves`: run, edit a field's sibling, recompile+reload, assert the unrelated
  field's bytes survived and the rewired behavior changed next frame — the F68 preserve contract, proven on
  the DLL path.
- Contract parity (F69) gains a third column: contracts 1-9 that already run on framework + interpreter also
  run on the DLL backend where a compiler exists.

## 7. Roadmap placement

- **F76 DLL preview design doc** — this file (verdict: shared `imguix-core`; C-ABI create/destroy; async
  monotonic-name reload; toolset-pinned compile; interpreter is the no-compiler fallback).
- **F77 imguix-core shared split** — carve the framework runtime into a shared target with exported
  `IMGUI_API`/`IMGUIX_API`; host + tests link it; no behavior change (a pure build refactor, all suites
  stay green). Gates F78.
- **F78 DLL preview backend** — `GenerateAppPreviewModuleCode`, the `ImGuiAppPreviewDll` session (compile /
  load / create / tick / async reload / state preserve / error surface), the Preview tab's backend toggle
  (DLL when a toolset exists, interpreter otherwise), and the §6 tests.

No code precedes this doc's acceptance; F77 precedes F78 (the shared core is the ABI foundation the whole
approach rests on).
