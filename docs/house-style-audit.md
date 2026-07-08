# House-Style Audit — imguiapp vs imgui-house-style.md (outstanding items)

**Spec**: [imgui-house-style.md](imgui-house-style.md) (181 rules: N1-N24, F1-F28, A1-A32, G1-G24,
B1-B18, I1-I41, D1-D9, C1-C3) + the Δ registry at the bottom of this doc.
**Corpus**: `imguiapp.h`, `imguiapp_internal.h`, `imguiapp.cpp`, `imappconfig.h`, `backends/*`
(`imguiapp_reflect.h` body exempt). All counts grep-measured; method: parallel evidence-cited
audits per dimension. This doc tracks OPEN items only — anything absent is conformant or resolved.
**Gate for every fix wave**: imguix-tests + imguix-core-tests + imguix-headless-verify green,
codegen corpus byte-identical, style ratchet monotonically down.

## Scorecard (current)

| Dimension | Conform | Partial | Violation | Top open item |
|---|---|---|---|---|
| §2 Naming (N1-N24) | 7 | 10 | 7 | R1 dialect renames pending (Δ8 decision) |
| §3 Formatting (F1-F28) | 10 | 8 | 3 (+6 gate-covered) | — (gates green) |
| §4 Architecture (A1-A32) | 8 | 6 | 7 (+2 n/a) | deprecation/breaking-change machinery absent (S5) |
| §5 API grammar (G1-G24) | 4 | 5 | 2 | Canvas conditional-End contract nuances |
| §6 Backends (B1-B18) | hosts ✓ | B5 | — | — |
| §7 Idioms (I1-I41) | 8 | 5 | 3 | dialect breach (43 `auto`, 33 capturing lambdas) |
| §8 Demo (D1-D9) | 3 | 2 | 3 slips | — (showcase re-registered as public-API user code) |

---

## Decisions needed (a Δ or a plan, then steps)

### R1. C++ dialect — I29 (SEVERE; largest unratified divergence)
`imguiapp.cpp`: 43 `auto` bindings, **33 capturing lambdas** (`[&]`, `[c]`, `[&changed]` —
`:2204-2260`, `:5261`, `:16583-16648`, `:22208`), 10/11 range-fors `auto&`; backends add 2 capturing
lambdas (sdl2_wgpu `:99,:131`) + `auto` casts (win32_vulkan `:670,:844`). Canon: 0 `auto`, lambdas
captureless-C-callback-only, range-for explicit-typed. Capturing-lambda-as-local-function is
structural in the editor UI — **decide Δ8 (license capturing lambdas as scoped local functions in
tool/editor regions, core stays clean) or schedule the rewrite** (`struct Func` + explicit types,
~90 sites). (Range-for `auto&` → explicit element types: done.)

---

## Systemic items

### S5. Deprecation + breaking-change machinery — A14/A15 (absent; retrofit cost compounding)
0 `OBSOLETED`, no Obsolete tail section, no `API BREAKING CHANGES` block — while a real ABI break
is encoded ad hoc (`IMGUIAPP_PREVIEW_ABI 20260706u`, `internal.h:56-58`). Stand up: guarded empty
Obsolete section, breaking-changes block seeded with the known preview-ABI break, staged lifecycle
on the next rename, version-stamp grammar (M38).

---

## Mechanical fixes (M-table)

| # | Rule | Finding | Fix |
|---|---|---|---|
| M29 | I20 | ListClipper ×1; 0 `IM_MSVC_RUNTIME_CHECKS_*` | low priority; after profiling |
| M41b | A26 | 6 residual rank descents (region-scoped def order vs header decl order) | with the Phase C section restructure |

## Sequencing (each wave gated)

1. **Wave R — decisions**: R1 (Δ8 capturing-lambda license or rewrite).
2. **Wave F — remainder**: S5 deprecation machinery; M41b/M29 with the Phase C restructure /
   profiling pass.

---

## Ratified deviations — the Δ registry


Registry of imguiapp's deliberate departures from [imgui-house-style.md](imgui-house-style.md).
Each entry: the spec rule it departs from, the scope of the license, and the rationale. Anything
NOT ratified here must conform; audits cite these as Δ-numbers. Matching a delta outside its
stated scope is a violation, not consistency. (Origin: Tier 1 of the initial audit.)

### Δ1 — Virtual interfaces in the app object model (departs A19/P1)

The composition model (`ImGuiAppInterface`, `ImGuiAppLayerBase`, `ImGuiAppItemBase`,
`ImGuiAppControlMirrorBase`, and the template adapters over them) uses virtual dispatch.
Polymorphic ownership of user-defined layers/windows/controls is the framework's mechanism; a
function-pointer vtable would re-implement C++ virtuals by hand for no user benefit.
**Scope**: the app object model hierarchy only. New value types, POD data structs, and engine
code MUST NOT introduce virtuals (imgui rule stands everywhere else).

### Δ2 — C++ standard library in bounded layers (departs A8/P1/G20/I6)

imgui's "no STL, ever" is held for engine, editor, canvas, and model code (ImVector/ImStr*/Im*
throughout — audit confirms 0 raw allocation, 0 std::min/max). Three bounded layers are licensed:

- **Template composition front** (`imguiapp.h` bottom sections): `<type_traits>` powers the
  typed dependency injection (`PushAppControl<T>`); the fan-out runs over an opaque `void*`
  slot array + a local index sequence (no `<tuple>`/`std::apply`, no STL members). The
  C-callable type-erased seam beneath it (`AppRegisterLayer/Window/Sidebar`,
  `AppControlRegisterStorage` + `AppControlPush`) is the binding surface; the template front
  is C++ convenience only.
- **Reflection layer** (`imguiapp_reflect.h` port + the field-render templates in
  `imguiapp_internal.h`): `<format>/<string_view>/<type_traits>` — constexpr metaprogramming is
  the point of the layer.
- **OS/harness glue** (`imguiapp.cpp` harness + file scan): `std::filesystem` where no Im*
  equivalent exists. Plain-libc/algorithm slips (`std::sort`, `snprintf`) are NOT licensed — use
  `ImQsort`/`ImFormatString`.

Naming note (the seam-vs-front grammar): PascalCase verb-first names the template FRONT
(`PushAppControl`), noun-first names the C seam TAIL beneath it (`AppControlPush`); the
linked-backend accessor `ImGuiAppGetPlatformBackend()` follows the front grammar.

**Not licensed anywhere**: STL types as public struct members visible to every consumer. The
recorder's encoder-thread state lives behind the opaque `ImGuiAppRecorderThread*` pimpl over the
`ImGuiAppThreadFuncs` seam (`SetAppThreadFuncs`, default = std::thread, strippable via
`IMGUIAPP_DISABLE_DEFAULT_THREAD_FUNCS`); `imguiapp.h` includes no threading headers.

### Δ3 — `g` names the graph, not the ImGui context (departs I1-I3/N17)

imguiapp threads its document explicitly: `ImGuiAppGraph* g` parameters (~283 sites) with editor
session state behind `AppGraphEditorState(g)`. There is no `GImGui` access and no hidden current
document — explicit threading is load-bearing for a multi-document editor (and for the tests).
**Scope + rule**: `g` means the graph in `AppGraph*`/editor/canvas code. A function that also
touches the ImGui context reaches it through accessors (`ImGui::GetCurrentWindow()` etc.) and
never names anything else `g`. Code operating on the ImGui context itself (no graph in scope)
keeps imgui's meaning of `g`.

### Δ7 — Unity `imguiapp.cpp` instead of topical satellite files (departs A16)

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

### Pending (not yet ratified — audit T5/T6 recommendations)

- Δ6 (proposed): Meyers-singleton accessors for process-wide services (`AppAssert()`,
  `AppPacer()`, `AppTypeSchemas()`); ad-hoc mutable function-local statics remain violations.
