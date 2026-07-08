# Style Deltas — ratified deviations from the imgui house style

Registry of imguiapp's deliberate departures from [imgui-house-style.md](imgui-house-style.md).
Each entry: the spec rule it departs from, the scope of the license, and the rationale. Anything
NOT ratified here must conform; audits cite these as Δ-numbers. Matching a delta outside its
stated scope is a violation, not consistency. (Origin: Tier 1 of
[house-style-audit-2026-07-07.md](house-style-audit-2026-07-07.md).)

## Δ1 — Virtual interfaces in the app object model (departs A19/P1)

The composition model (`ImGuiAppInterface`, `ImGuiAppLayerBase`, `ImGuiAppItemBase`,
`ImGuiAppControlMirrorBase`, and the template adapters over them) uses virtual dispatch.
Polymorphic ownership of user-defined layers/windows/controls is the framework's mechanism; a
function-pointer vtable would re-implement C++ virtuals by hand for no user benefit.
**Scope**: the app object model hierarchy only. New value types, POD data structs, and engine
code MUST NOT introduce virtuals (imgui rule stands everywhere else).

## Δ2 — C++ standard library in bounded layers (departs A8/P1/G20/I6)

imgui's "no STL, ever" is held for engine, editor, canvas, and model code (ImVector/ImStr*/Im*
throughout — audit confirms 0 raw allocation, 0 std::min/max). Three bounded layers are licensed:

- **Template composition front** (`imguiapp.h` bottom sections): `<tuple>`/`<type_traits>` power
  the typed dependency injection (`PushAppControl<T>`, `std::apply` fan-out). The C-callable
  type-erased seam beneath it (`AppRegisterLayer/Window/Sidebar`, `AppControlRegisterStorage` +
  `AppControlPush`) is the binding surface; the template front is C++ convenience only.
- **Reflection layer** (`imguiapp_reflect.h` port + the field-render templates in
  `imguiapp_internal.h`): `<format>/<string_view>/<type_traits>` — constexpr metaprogramming is
  the point of the layer.
- **OS/harness glue** (`imguiapp.cpp` harness + file scan): `std::filesystem` where no Im*
  equivalent exists. Plain-libc/algorithm slips (`std::sort`, `snprintf`) are NOT licensed — use
  `ImQsort`/`ImFormatString`.

**Not licensed anywhere**: STL types as public struct members visible to every consumer. The
recorder's encoder-thread state lives behind the opaque `ImGuiAppRecorderThread*` pimpl over the
`ImGuiAppThreadFuncs` seam (`SetAppThreadFuncs`, default = std::thread, strippable via
`IMGUIAPP_DISABLE_DEFAULT_THREAD_FUNCS`); `imguiapp.h` includes no threading headers.

## Δ3 — `g` names the graph, not the ImGui context (departs I1-I3/N17)

imguiapp threads its document explicitly: `ImGuiAppGraph* g` parameters (~283 sites) with editor
session state behind `AppGraphEditorState(g)`. There is no `GImGui` access and no hidden current
document — explicit threading is load-bearing for a multi-document editor (and for the tests).
**Scope + rule**: `g` means the graph in `AppGraph*`/editor/canvas code. A function that also
touches the ImGui context reaches it through accessors (`ImGui::GetCurrentWindow()` etc.) and
never names anything else `g`. Code operating on the ImGui context itself (no graph in scope)
keeps imgui's meaning of `g`.

## Δ4 — Host backends compose upstream imgui backends behind the ImGuiX seam (adapts B6/B9-B14)

`imguiapp_impl_{win32,sdl2}_{opengl3,vulkan,wgpu}` do not re-implement the imgui backend
contract; they wrap the real `imgui_impl_*` backends and register lifecycle callbacks on the
`ImGuiXBackend` vtable. Consequences, ratified:

- `io.BackendPlatformUserData`/`BackendRendererUserData` belong to the wrapped upstream backends;
  the host's `ImGuiApp_ImplXXX_Data` is `IM_NEW`-allocated, handed to the seam as
  `Backend.UserData`, and reached by context-free viewport hooks through a
  `ImGuiApp_ImplXXX_GetBackendData()` accessor over a single instance pointer (the seam supports
  one backend per process; init asserts "Already initialized").
- io wiring, render-state backup/restore, texture lifecycle, and shaders (B9-B14) are audited at
  the wrapped backend, not the wrapper.

**Not ratified**: file-scope mutable Data *values* and secondary globals (the pre-Δ4 `GBackend`
value + `GState` pattern) — fixed to the allocated-instance form above.

## Δ7 — Unity `imguiapp.cpp` instead of topical satellite files (departs A16)

imgui's satellite model (`imgui.cpp` + `imgui_widgets.cpp` + `imgui_draw.cpp` + `imgui_tables.cpp`)
is not reproduced; the canvas, nodes, preview/interpreter, and AV subsystems are folded into one
`imguiapp.cpp` as embedded sub-files, each with its own top-of-region index. The fold was
deliberate (single TU, one include spine, no cross-file internal headers for subsystems that share
statics).
**Scope**: file count only. Everything *inside* each embedded sub-file region must still conform:
region-local `[SECTION]` index kept 1:1 with body banners (A2), definitions ordered to match
header declaration order within the region (A16/refactor-plan Phase C pass 4), forward decls
collected per region (A17). A future re-split to satellites stays open; this delta removes the
obligation, not the option.

## Pending (not yet ratified — audit T5/T6 recommendations)

- Δ5 (proposed): `*_state.h` shared platform-state sidecars across sibling renderers.
- Δ6 (proposed): Meyers-singleton accessors for process-wide services (`AppAssert()`,
  `AppPacer()`, `AppTypeSchemas()`); ad-hoc mutable function-local statics remain violations.
