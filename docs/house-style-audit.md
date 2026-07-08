# House-Style Audit — imguiapp vs imgui-house-style.md (outstanding items)

**Spec**: [imgui-house-style.md](imgui-house-style.md) (181 rules: N1-N24, F1-F28, A1-A32, G1-G24,
B1-B18, I1-I41, D1-D9, C1-C3) + the Δ registry at the bottom of this doc (Δ6 pending — R7).
**Corpus**: `imguiapp.h`, `imguiapp_internal.h`, `imguiapp.cpp`, `imappconfig.h`, `backends/*`
(`imguiapp_reflect.h` body exempt). All counts grep-measured; method: parallel evidence-cited
audits per dimension. This doc tracks OPEN items only — anything absent is conformant or resolved.
**Gate for every fix wave**: imguix-tests + imguix-core-tests + imguix-headless-verify green,
codegen corpus byte-identical, style ratchet monotonically down.

## Scorecard (current)

| Dimension | Conform | Partial | Violation | Top open item |
|---|---|---|---|---|
| §2 Naming (N1-N24) | 7 | 10 | 7 | k-tier (243 uses); ~55 unprefixed TU-local types; `ImGuiCanvas*` squat |
| §3 Formatting (F1-F28) | 10 | 8 | 3 (+6 gate-covered) | bare `default:` 50:1 (S9) |
| §4 Architecture (A1-A32) | 7 | 7 | 7 (+2 n/a) | no `IMGUI_DISABLE` wrap anywhere |
| §5 API grammar (G1-G24) | 4 | 5 | 2 | silent overpop in all four `PopApp*`; no V twins |
| §6 Backends (B1-B18) | seam ✓ | B5/B15 | B1/B2/B16/B17 | anatomy/CHANGELOG/guards/prefix wave |
| §7 Idioms (I1-I41) | 8 | 5 | 3 | dialect breach (43 `auto`, 33 capturing lambdas) |
| §8 Demo (D1-D9) | 3 | 2 | 3 slips | sample controls not in user register |

---

## Decisions needed (a Δ or a plan, then steps)

### R1. C++ dialect — I29 (SEVERE; largest unratified divergence)
`imguiapp.cpp`: 43 `auto` bindings, **33 capturing lambdas** (`[&]`, `[c]`, `[&changed]` —
`:2204-2260`, `:5261`, `:16583-16648`, `:22208`), 10/11 range-fors `auto&`; backends add 2 capturing
lambdas (sdl2_wgpu `:99,:131`) + `auto` casts (win32_vulkan `:670,:844`). Canon: 0 `auto`, lambdas
captureless-C-callback-only, range-for explicit-typed. Capturing-lambda-as-local-function is
structural in the editor UI — **decide Δ8 (license capturing lambdas as scoped local functions in
tool/editor regions, core stays clean) or schedule the rewrite** (`struct Func` + explicit types,
~90 sites). Range-for `auto&` → explicit element types is mechanical either way (13 sites, F26);
fix regardless of the Δ8 outcome.

### R2. `ImGuiCanvas*` tier squat — N1 (HIGH)
6 structs + 4 enums (`imguiapp_internal.h:119-124`, `:1786-1898`, `:2170-2172`; `ImGuiCanvasState`
178 uses) claim upstream imgui's `ImGui*` tier — collision surface with future imgui symbols.
**Ratify Δ9 or rename `ImGuiAppCanvas*`** (AST rename, one TU + headers). Same decision batch:
`ImStackTrace` → `ImAppStackTrace` (`internal.h:1171`, bare-Im foundation-tier squat, fails N11
points 1-2; recommend rename, NOT ratification).

### R3. Sample-control register + member casing — D3/D8 + M20 (MED)
RandomTime/Breathing (the user-register showcase) use `ImStrncpy`/`ImFormatString`/`ImHashData` +
`std::string_view` + snake_case members + `kDemo*` palette (`imguiapp.cpp:3505-3740`). Either
re-register as user code (public API only, PascalCase members) or amend A23's demo arrow.
Bundled: `ImAppTween/Timer/Spring/Pulse` `*Data`/`*TempData` snake_case members
(`internal.h:1057-1131`) — same reflection/codegen blast radius, decide together. Also update
N1/A22's description of the animation four if their tier claim changes (they are `ImGuiAppControl<>`
subclasses, not standalone value types).

### R4. Forward-decl ordering direction — A28 (MED)
Both headers order fwd decls semantically (`ImGuiAppLayerBase` before `ImGuiAppLayer`,
`imguiapp.h:46-51`; `internal.h:80-99`) vs A28's alphabetical-within-group. **Conform or ratify
a stated dependency-order delta** — an ordered list with unstated direction is a spec bug by A28.

### R7. Δ6 ratification — singleton accessors vs ad-hoc statics (carried)
Accessor pattern (`AppAssert()`/`AppPacer()`/`AppTypeSchemas()`) is the framework idiom; ratify it.
Open regardless: 9 ad-hoc mutable statics (`imguiapp.cpp:63,:74,:116,:240,:285,:369,:396,:473`;
`mediafoundation.cpp:30`) + file-scope mutable `GAppThreadFuncs` (`:24794`) — each moves into its
owning singleton or gets a process-wide justification comment. Two use `s_` Hungarian
(`s_monitor_hz`, `s_freq`) — a third register; eliminate in the same pass.

---

## Systemic items

### S5. Deprecation + breaking-change machinery — A14/A15 (absent; retrofit cost compounding)
0 `OBSOLETED`, no Obsolete tail section, no `API BREAKING CHANGES` block — while a real ABI break
is encoded ad hoc (`IMGUIAPP_PREVIEW_ABI 20260706u`, `internal.h:56-58`). Stand up: guarded empty
Obsolete section, breaking-changes block seeded with the known preview-ABI break, staged lifecycle
on the next rename, version-stamp grammar (M38).

### S6. Work-marker system — F20 (absent)
Zero FIXME/TODO/HACK/XXX corpus-wide; deferred work lives only in docs. Adopt `FIXME-<AREA>`
(`FIXME-CANVAS/-GRAPH/-CODEGEN/-AV/-PREVIEW/-OPT`), drop tags when touching doc-tracked debt, add
the tag-census check to `tests/style` (fail TODO/HACK/XXX outside string literals).

### S7. Backend anatomy wave — B1/B2/B16/B17 (HIGH)
0 header anatomy blocks, 0 CHANGELOGs, 0 `#ifndef IMGUI_DISABLE` guards (18 files); ~45 unprefixed
backend-internal functions (22 static + ~23 in anon namespaces — canon never uses anon namespaces);
`GBackend` identifier unprefixed; AV vtable callbacks contracted (`ImGuiAppLibav_*` vs
`ImGuiApp_ImplLibav_*`); `std::max` ×8 + `<algorithm>` in SDL backends (Δ2 doesn't license
backends); `IMGUIX_API`/`IMGUI_API` export split unstated (B4); B15 viewport hooks unbannered,
named bare `Hook_*`; UTF-8 BOMs on 6 headers.

### S8. Recoverable-error machinery — G4/G19 (HIGH)
**Silent overpop**: all four `PopApp*` `if (empty) return;` — no assert, no WAL record
(`imguiapp.cpp:988-1031`) — the exact "100%-silent recovery" canon refuses; G4's named overpop
assert missing. 0 `IM_ASSERT_USER_ERROR`; 11/152 asserts messaged (7%); invisible-trailing-comment
anti-pattern persists (`:482`). Route Pop/registration misuse through `IM_ASSERT_USER_ERROR` + WAL;
sweep user-reachable asserts into sentence-with-hint form.

### S9. Micro-lexicon + constant tier — F25/F26/F14/F28 + N22/N23 (sed-class in aggregate)
Bare `default:` 50 vs 1 merged; index name `i` ×398 vs `n` ×1 (decide once); 15 C++ casts
(13 `static_cast`: cpp `:3826,:6581`, h `:986-:1213`, backends ×6; 2 `const_cast`: `:22198,:23245`);
13 `sizeof(Type)` in mem* (`:1472,:1509,:7309,:7312,:24733-:26459`); 1 `while (true)` (`:23818`);
the single `ImQsort` comparator = anonymous non-`IMGUI_CDECL` lambda, `a/b` params (`:27183` —
N22's exact fenced hazard → `struct Func { static int IMGUI_CDECL SampleComparerByValue(...) }`);
k-tier: 56 defs / 61 names / 243 uses, 0 UPPER_SNAKE tunables (86% in the graph-editor region) —
target N23 form, ID-key blocks get rationale comments.

### S10. TU-local type prefixes — A23/N12 (HIGH)
~55 unprefixed TU-local types in engine/tool regions (of 71; 16 demo-register sanctioned): the
12-type `AppPv*` interpreter family (`:23368-24419`), Composer chrome ~20 (`GraphDocData :3791`,
`ComposerTransport :3771`, `ToolbarControl :4612`, `StatusStripControl :5135`,
`EditorBodyControl :5631`, `ProjFile :5487`, `SavedControl :7136`, `AppOpDesc :8720`,
`AppTreeCtx :22548`), pacer `CachedHz :280`, `AppBlStyleScope :7743`, + 7 local enums
(`ComposerPanel_/ComposerHostCmd_/ComposerLayoutPreset_/ComposerLayoutVis_/ComposerPillState/`
`AppOpClass_/AppAlignMode_`). Rename to `ImGuiApp*` ("App"/"Composer" are not tier prefixes).

---

## Mechanical fixes (M-table)

| # | Rule | Finding | Fix |
|---|---|---|---|
| M1 | gate | style gate still pins 26 `text`-class findings (missing `} // namespace` closers) | add closers; re-pin baseline down |
| M3 | N18 | macro namespace split 3 ways over 7 macros (`IMGUI_APPLAYER_*`/`IMGUIAPP_*`/`IMGUIX_*`); `imappconfig.h` mixes two | rule: `IMGUIAPP_` = library defines, `IMGUIX_` = umbrella switches; rename `IMGUI_APPLAYER_VERSION*` → `IMGUIAPP_VERSION*`; document in imappconfig.h |
| M5 | A1 | 0 file banners, 0 `IMGUI_DISABLE` wraps, all 22 files | banner + role line + guard + echoed endif |
| M6 | A3 | pragma block MSVC-only, 1 warning, imguiapp.h only | 3-compiler push/pop in all three TUs |
| M7 | A8 | `internal.h:43-48` + cpp `:25-30` includes uncommented; `<string>` (`internal.h:46`) outside Δ2's licensed list | add symbol comments; license or drop `<string>` |
| M8 | A9 | config include 3rd (`imguiapp.h:24`); redundant `imgui_internal.h` re-include (cpp `:26`); cpp spine jumbled | config first; delete; reorder |
| M9 | A10 | no CHECKVERSION analog | `IMGUIAPP_CHECKVERSION()` + sizeof asserts, called in `InitializeApp` |
| M10 | A13 | version `"0.4.1"`/401: no ` WIP`, no scheme comment | ` WIP` suffix + monotonic NUM + scheme comment (with M3 rename) |
| M11 | A17 | no cpp `[SECTION] Forward Declarations`; stray fwd decl `:13170` | add per Δ7 region |
| M13 | G7 | `AppRegisterSidebar` flags no `= 0` (`imguiapp.h:163`) | add `= 0` |
| M14 | G9/N10 | `AppWALWrite` (`imguiapp.h:212`) + `AppGraphNotify` (cpp `:13170`) variadic, no V twins, 0 `IM_FMTLIST` | add `*V(va_list)` twins, re-implement over them |
| M15 | G4 | Push*Control "No Pop" decl comment absent (`imguiapp.h:128-130`) | add comment |
| M16 | N4 | 7 enums unpaired (`ImGuiAppControlMethod_`, `ImGuiAppCmdSurface_`, `ImAppEase_`, `ImGuiCanvasInteraction_`, 3 pin enums); dup typedefs `h:90/294`, `h:91/302` | add `typedef int` pairs, retype raw-int fields, delete dupes |
| M17 | N5 | 3 stem mismatches (`ImGuiCanvasPinKind_`→`ImGuiCanvasPin_*` `internal.h:2170`; `AppAlignMode_` cpp `:14764`; `ComposerPillState` cpp `:5079`, also no trailing `_`) | rename stems (fold into R2/S10 batches) |
| M18 | N6 | flags `ImGuiAppCmdSurface_` (`internal.h:693`) + `ComposerLayoutVis_` (cpp `:3934`) lack `_None = 0`; `AVMetaRecordType_` starts at 1 | add `_None = 0` |
| M19 | N8 | `ImGuiAppCommandPrivate` in public header (`imguiapp.h:272-274`), drops public stem | move to internal.h; `ImGuiAppCommand_PrivateBegin_ = ImGuiAppCommand_COUNT` |
| M21 | F21 | 8 bare `#endif` on >20-line guards incl. one 512-line (cpp `:7379`; sdl2_wgpu `:147,:216`) | add condition echoes |
| M22 | F11 | public typedefs (h `:85-91`) lack 3rd column; internal (`:130-138`) lack `Enum:`/`Flags:` prefix; `internal.h:139` uncommented | complete the triple-column |
| M26 | I21/G18 | 11/152 asserts messaged; trailing-comment hint at `:482` invisible in dialogs | sentence-with-hint in-expression for user-reachable asserts |
| M28 | I22 | 0 DebugNode introspection | `DebugNodeAppGraph/AppNode/Canvas` under `IMGUI_DISABLE_DEBUG_TOOLS`, hooked to a Tools window |
| M29 | I20 | ListClipper ×1; 0 `IM_MSVC_RUNTIME_CHECKS_*` | low priority; after profiling |
| M31 | N3 | fn-ptr members lack `*Fn` on all 3 vtables (`ImGuiAppAVEncoder` h:374-377, `ImGuiAppThreadFuncs` h:444-452, `ImGuiAppPlatformBackend` h:637-641); sole typedef uses `Fn` where `Func` belongs | suffix members `*Fn`; typedef → `*Func` |
| M32 | N24 | 28/259 internal decls verb-first (`BeginAppNode`, `EditAppNodeDraft*`, `GenerateApp*Code`, `Save/LoadAppGraph` clusters, `internal.h:1360-1670`) | noun-first renames in the AST batch; `Request` (not `Queue`) recorded as the deferred infix |
| M33 | N9 | `ImGuiApp_GetPlatformBackend` underscore grammar in public header (`imguiapp.h:644`); `PushAppControl`/`AppControlPush` grammar-flipped twins unnamed | rename; name the seam-vs-front pattern in Δ2 |
| M34 | F12 | backend public definitions unpadded (0/12); `IMGUI_API` repeated on definitions (libav `:325+`, qoi `:150+`) | pad name column; strip repeats |
| M35 | F27 | 83/90 off-census buffers uncommented; de-facto keys (560 paths ×8, 1200 cmdlines ×7) undocumented | reason comments or document the census |
| M36 | F18 | 7 lowercase sentence-starts (h `:716`, internal `:1241`, cpp ×5) | capitalize |
| M37 | A18 | `kAppDockDirNames[4]` (cpp `:12192`) unlocked; `OutlinerKindVis` hand-9 initializer (`internal.h:918`) zero-fills on enum growth | `IM_STATIC_ASSERT` locks |
| M38 | A32 | no version-stamp grammar; `IMGUIAPP_PREVIEW_ABI 20260706u` ad-hoc date tag | adopt "Since 0.Y.Z (Month Year, NUM)" with S5 |
| M39 | — | dead empty `#ifdef IMGUIX_HAS_LIBAV` pair (cpp `:3468-3469`); BOMs on 6 backend headers | delete; strip |
| M40 | A24 | `internal.h` Macros section precedes Forward declarations (inverted vs canon) | swap sections |
| M41 | A26 | `ShowAppGraphEditor` defined in Scope-interior section (`:15090`) not editor-render; 6 residual rank descents | move with the Phase C section restructure |
| M42 | I40 | 2,835-line `ShowAppGraphEditor` without a `[Part N]` spine (local `// Pass 1/2/3` at `:15620` shows the form known); also `AppGraphValidate` 323 L, `CanvasEnd` 297 L | number the parts |
| M43 | I33 | `clicked`/`changed` + `saved_`/`prev_`/`old_` (14 sites, 0 `backup_`) vs canon `pressed`/`value_changed`/`backup_` | rename locals |
| M44 | I35 | 3 minority `held ? Active : Hovered` picks (cpp `:7531,:7553,:7778`) — spec wart 7 form | `(held && hovered)` form |
| M45 | G2 | `CanvasBegin` void + unconditional `CanvasEnd`; `CanvasBeginNode` bool ignored by own consumer (`:15794`); no pairing comments on the 4 decls | pairing comments + decide the conditional-End contract |

## Sequencing (each wave gated)

1. **Wave R — decisions**: R1-R4, R7. Everything below assumes the outcomes.
2. **Wave L — lint first**: S6 tag census + N22/N23 greps into `tests/style`, so no fixed class
   regresses again.
3. **Wave M — sed batch**: M13, M15, M18, M21, M22, M26, M34, M36, M37, M39, M44, F26 explicit-type
   range-fors, F14 casts, F28 sizeof, F25 default merges, M8, M7.
4. **Wave N — AST rename batch**: S9 k-tier → UPPER_SNAKE, S10 type prefixes, M3+M10, M16, M17,
   M19, M31, M32, M33, S7 prefix half (backend statics + de-anon-namespace), R2 outcome.
5. **Wave B — backend anatomy**: S7 remainder (B1 blocks, B2 CHANGELOGs, B16 guards, B15 banner,
   B7 accessors, B8 assert note).
6. **Wave F — functional adds**: S8 (USER_ERROR + overpop + WAL), M9, M14, S5+M38, S6 adoption,
   M28, M42, M45, M11/M40/M41 with the section restructure.

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

### Δ3 — `g` names the graph, not the ImGui context (departs I1-I3/N17)

imguiapp threads its document explicitly: `ImGuiAppGraph* g` parameters (~283 sites) with editor
session state behind `AppGraphEditorState(g)`. There is no `GImGui` access and no hidden current
document — explicit threading is load-bearing for a multi-document editor (and for the tests).
**Scope + rule**: `g` means the graph in `AppGraph*`/editor/canvas code. A function that also
touches the ImGui context reaches it through accessors (`ImGui::GetCurrentWindow()` etc.) and
never names anything else `g`. Code operating on the ImGui context itself (no graph in scope)
keeps imgui's meaning of `g`.

### Δ4 — Host backends compose upstream imgui backends behind the ImGuiX seam (adapts B6/B9-B14)

`imguiapp_impl_{win32,sdl2}_{opengl3,vulkan,wgpu}` do not re-implement the imgui backend
contract; they wrap the real `imgui_impl_*` backends behind an exposed `ImGuiApp_ImplXXX_*`
lifecycle (`_Init(const InitInfo*)`/`_Shutdown`/`_NewFrame`/`_RenderDrawData`/`_PresentFrame`,
imgui impl pattern; InitInfo public in the header). The lifecycle rides the
`ImGuiApp_GetPlatformBackend()` vtable, driven by the app core's frame phases; the ImGuiX seam
optionally binds the same vtable via `ImGuiX::Initialize()`. Neither imguiapp core nor backend
TUs include imguix.h; the platform host owns the ImGui context it creates (none existed) and
destroys it in ShutdownPlatform. Consequences, ratified:

- `io.BackendPlatformUserData`/`BackendRendererUserData` belong to the wrapped upstream backends;
  the host's `ImGuiApp_ImplXXX_Data` is `IM_NEW`-allocated and reached (by the lifecycle and by
  context-free viewport hooks alike) through a `ImGuiApp_ImplXXX_GetBackendData()` accessor over
  a single instance pointer (one backend per process; init asserts "Already initialized").
- io wiring, render-state backup/restore, texture lifecycle, and shaders (B9-B14) are audited at
  the wrapped backend, not the wrapper.

**Not ratified**: file-scope mutable Data *values* and secondary globals (the pre-Δ4 `GBackend`
value + `GState` pattern) — fixed to the allocated-instance form above.

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
