# House-Style Compliance Audit — imguiapp vs imgui-house-style.md (2026-07-07)

**Spec**: [imgui-house-style.md](imgui-house-style.md) (rule IDs P/N/F/A/G/B/I/D/C cited throughout).
**Audited**: `imguiapp.h` (1341 L), `imguiapp_internal.h` (2384 L), `imguiapp.cpp` (27343 L),
`imappconfig.h` (8 L; `imapp_config.h` until the M4 exact-form rename), `backends/imguiapp_impl_*` (11 backends); `imguiapp_reflect.h` skeleton only
(body = vendored qlibs/reflect port, exempt like `imstb_*`). Method: six parallel audits, one per
spec dimension, every verdict evidence-cited. Mechanical layout was pre-screened by the style gate
(`tests/style/`, baseline `imguiapp-baseline.json`: 132 pinned, 28 real).

**Verdict in one line**: imguiapp reproduces imgui's *structural grammar* faithfully — type-prefix
layering, enum shapes, Push/Pop symmetry, pointer-param prefixes, allocator/math discipline, header
alignment tables — and diverges in three tiers: (1) a small set of **deliberate architectural
departures** that need ratifying or reversing, not patching; (2) a **comment/marker/meta layer**
that drifted (narrative prose, no FIXME system, no deprecation machinery, index drift); (3) a long
tail of **mechanical fixes**, most a sed away.

## Scorecard

| Dimension | Conform | Partial | Violation | Top finding |
|---|---|---|---|---|
| §2 Naming (N1-N20) | 10 | 6 | 2 | `k`-prefix constants (212 sites); macro namespace split 4 ways |
| §3 Formatting (F1-F23) | 8 | 6 | 2 | 2-space indent regions; FIXME family absent |
| §4 Architecture (A1-A21) | 5 | 6 | 8 | .cpp order ⊥ header order; STL includes; no deprecation/breaking-log |
| §5 API grammar (G1-G22) | 12 | 5 | 3 | STL in public header; no recoverable-error machinery; no V twins |
| §6 Backends (B1-B18) | 3 | 7 | 5 | no `IMGUI_DISABLE` guards; no CHANGELOGs; global-singleton Data |
| §7 Impl idioms (I1-I25) | 12 | 9 | 2 | zero DebugNode introspection; context idiom substituted |

Strong credits first, so they are not lost: **I5 allocator discipline perfect** (0 raw new/malloc,
77 IM_* sites), **I11 math perfect** (0 std::min/max/floorf; 170 Im* helpers), **I24 zero goto**,
**I25 nullptr 100% consistent**, **N14/N16 pointer-prefix + underscore-internal exemplary**,
**F7-F10/F16 header alignment tables conform strongly** (79-char separators pixel-uniform),
**A2 header index↔body 1:1 exact** on all three headers, **Phase A tool-decl eviction complete**
(refactor-plan admission (d) resolved), **I23 forward-decl `// fwd` discipline exemplary**,
codec backends (qoi/libav/mediafoundation) closest to backend canon incl. dependency hygiene.

---

## Tier 1 — Architectural departures (ratify or reverse; a decision, then steps)

These are not cleanups. Each needs an explicit decision recorded in a new
`docs/style-deltas.md` (create it as Fix step 0 — every ratified deviation gets an entry with
rationale; everything NOT ratified must conform). Recommendation given per item.

### T1. Virtual class hierarchy in the app object model — A19/P1 (imgui: zero virtuals)
94 `virtual` in `imguiapp.h` (`ImGuiAppInterface` `:660`, `ImGuiAppLayerBase`/`ItemBase`/
`ControlMirrorBase` `:728-797`). Polymorphic ownership is the framework's composition mechanism.
**Recommend: RATIFY.** Reversal = full redesign for no user benefit.
**Steps**: (1) entry in `style-deltas.md`: "Δ1: app object model uses virtual interfaces; imgui's
no-virtual rule applies to everything else — new value/POD types MUST NOT add virtuals." (2) Add a
one-line pointer at the `ImGuiAppInterface` decl citing Δ1.

### T2. STL in public surface — A8/G20/I6 (imgui: "no STL, ever")
Three distinct exposures, different treatments:
- `imguiapp.h:32-38` includes `<mutex> <tuple> <type_traits> <string_view> <thread>
  <condition_variable>`; `std::thread/mutex/condition_variable` are **public members** of
  `ImGuiAppRecorder` (`:501-504`). **Recommend: FIX.** Steps: (1) move the recorder's thread state
  behind an opaque `ImGuiAppRecorderThread* _Thread;` pimpl allocated with `IM_NEW` in
  `imguiapp.cpp`; (2) `<tuple>/<type_traits>` serve the template composition front — move those
  includes + the template bodies to the bottom "inline composition" section behind a
  `// [SECTION] Template composition front (C++ convenience; C seam is AppRegister*)` banner, or
  into a new `imguiapp_cpp.h` convenience header; (3) after both, `imguiapp.h` includes shrink to
  imgui-class deps; gate with a lexical include-allowlist check in `tests/style`.
- `imguiapp_internal.h:45-48` `<format> <string> <string_view>` for the reflection field-render
  layer. **Recommend: RATIFY narrowly** (Δ2: reflection/formatting layer may use C++20 facilities;
  scoped to the `[SECTION]` that hosts it), since the reflect port is constexpr-metaprogramming by
  nature.
- Glue uses (`std::filesystem` scan `imguiapp.cpp:5739`, `std::sort` `:27286`, `snprintf`
  `:27051-27079`). **Recommend: FIX cheap ones** — `std::sort`→`ImQsort`, `snprintf`→
  `ImFormatString` (3 sites); ratify `std::filesystem` under Δ2 (OS glue) or replace with the
  Win32/posix wrappers already used elsewhere.

### T3. Context idiom: `ImGuiAppGraph* g` substitutes `ImGuiContext& g = *GImGui` — I1-I3/N17
283 `ImGuiAppGraph* g` params; 241 `AppGraphEditorState(g)` calls; 0 `GImGui`. Explicit threading
is *better* than a hidden global for a multi-document editor — but it overloads imgui's most
load-bearing conventional local.
**Recommend: RATIFY** (Δ3), with one rule added: functions that also touch the ImGui context MUST
NOT name anything `g` other than the graph, and use `ImGui::GetCurrentWindow()` accessors (current
practice — 3 sites, all clean). No rename (283 sites, churn ≫ benefit).

### T4. Backend seam: state in `GBackend` file-globals + ImGuiX vtable, not `io.BackendXxxUserData` — B6/B7
`win32_opengl3.cpp:50`, `win32_vulkan.cpp:98`, `sdl2_opengl3.cpp:33`, `sdl2_wgpu.cpp:44` each hold
a file-scope `GBackend` value (plus `GState` in win32_opengl3); accessor absent; spec calls this
the retired-2021 pattern. The hosts wrap real imgui backends behind the `ImGuiX` seam, so io slots
are already occupied by the wrapped backends.
**Recommend: PARTIAL FIX** — keep the seam (Δ4), fix the pattern: (1) replace each `GBackend`
value with `IM_NEW`'d struct stored in the seam's `Backend.UserData` (already plumbed —
`win32_opengl3.cpp:308`) and reached via a
`static ImGuiApp_ImplXXX_Data* ImGuiApp_ImplXXX_GetBackendData()` accessor; (2) `IM_DELETE` in
`ShutdownPlatform`; (3) fold `GState` into the Data struct; (4) add
`IM_ASSERT(<slot> == nullptr && "Already initialized a platform backend!")` at each `InitPlatform`
entry (B8). Mechanical; 4 files.

### T5. `*_state.h` shared platform-state sidecars — B6 single-struct rule
`ImGuiAppPlatformState` shared across sibling renderers (win32: GL3+Vulkan; sdl2: GL3+WGPU) is a
coherent invention with no imgui analogue; it is *more* B6-compliant (IM_NEW + userdata slot) than
the GBackend structs beside it. **Recommend: RATIFY** (Δ5) + give each `_state.h` a B1-style header
comment block (2 files, 6 lines each).

### T6. Singleton accessors instead of `GIm*` globals — N13(part)/I17
`AppAssert()` (`imguiapp.cpp:57`), `AppPacer()` (`:243`), `AppTypeSchemas()` (`:477`) etc. —
documented pattern (`:47-50`), zero `g_`/TU globals. **Recommend: RATIFY** (Δ6) — it is the
framework's own derived-cache idiom. But FIX the ~9 ad-hoc mutable statics that are NOT the
documented pattern (`:78 sym_ready`, `:120 in_assert`, `:290 s_monitor_hz`, `:375 s_freq`,
`:402 epoch`, `:6696 s_fallback_ready`, `:6899 s_vcvars`, `:7673 version`, `:7761 seeded_version`):
each either (a) moves into the owning singleton's struct, or (b) gets a one-line justification
comment naming why it is process-wide (the `MfStartupRefs` precedent, `mediafoundation.cpp:24-27`).

---

## Tier 2 — Systemic drift (real work, mostly already planned as Phase C)

### S1. `imguiapp.cpp` definition order ⊥ header order; five files folded into one — A16 (SEVERE)
> **RESOLVED 2026-07-07** (unity file kept — Δ7 ratified in style-deltas.md; ordering fixed).
> Public API rank sequence is now `1..37,39,40` in core (+`41` demo, `38` av — region-local per Δ7).
> Core tail rebuilt in header order under new banners (App bring-up → snapshots → Frame pacing →
> WAL → Authored style/color mods; top index updated, 10 sections 1:1). Folded regions: defs
> permuted into internal.h decl order within each `[SECTION]` (gate-scope aware); statics relocated
> above their earliest caller where the permutation demanded (~60 moves). Residual (6 descents):
> 2 fold-boundary artifacts (Δ7-fine), 4 in nodes `Scope interior` where `ShowAppGraphEditor`-class
> gated editor fns sit across `IMGUIX_DISABLE_TOOLS` boundaries — deferred to Phase C proper
> (section restructure + S4). Gate: suites green, codegen corpus byte-identical.
Quantified: header decl ranks map to cpp definition order as
`34,29,28,…,1,2,3,4,37` — grossly non-monotonic; lifecycle (`InitializeApp` etc., header ranks
1-4) is defined at `:1693-1777` *after* mid-header subsystems defined at `:65-1106`. File is
27343 lines (> imgui.cpp itself) with 5 embedded sub-indexes.
**Fix (= refactor-plan Phase C pass 4, now with the drift map done)**:
1. Decide file split first: either re-split into `imguiapp.cpp` + `imguiapp_canvas.cpp` +
   `imguiapp_nodes.cpp` + `imguiapp_preview.cpp` + `imguiapp_av.cpp` (imgui's satellite model,
   A16), or keep unity file and fix ordering only. Spec-conformant = split. (The fold was
   deliberate; if ratified, record Δ7 and still fix ordering within sub-files.)
2. Within each (sub-)file: reorder definitions to match header declaration order under matching
   `[SECTION]` banners. Mechanical move-only edits; use the AST identifier dump (decl-order table)
   to generate the move plan.
3. Gate after every file: suites green + codegen corpus byte-identical (refactor-plan §4).

### S2. Section-index drift + decorated fold banners in imguiapp.cpp — A2/F19
Demo sub-index missing `[SECTION] Playback debugger` (`:4141`) + renamed final entry (`:6682`);
nodes sub-index missing 3 sections, order swapped (`:11024`/`:11963`/`:12833`/`:13373`); AV
sub-index missing 5; preview/interpreter block (`:23377-24440`) has NO index; ~10 `====`/`####`
fold banners (`:1822`, `:7415`, `:23494`, …) violate F19's no-decorated-separators.
**Fix**: (1) regenerate each sub-index from actual body markers (script: grep `[SECTION]`, paste);
(2) add an index banner for the preview block; (3) convert every `====`/`####` banner to the
standard 79-char dashed `[SECTION]` form; (4) add index↔body 1:1 sync check to `tests/style`
lint (trivial: parse index block, parse body markers, diff) so drift can't recur.

### S3. 2-space indentation regions — F1 (thousands of lines; gate blind spot)
`imguiapp_internal.h` is entirely 2-space; folded regions of `imguiapp.cpp` (canvas `~1828+`,
nodes `7415+`, preview `23494+`, av `24818+`) are 2-space vs the 4-space core (1720 two-space
lines measured).
**Fix**: (1) run the already-built migration pipeline: `clang-format` APPLY with
`tests/style/imguix.clang-format` on the affected files (whitespace-only commit), then re-run
suites + codegen corpus byte-compare, then re-pin `imguiapp-baseline.json`; (2) close the gate
blind spot that let this slip — the classifier's tolerated `align` class absorbs indent-width
churn; add a lexical check to `tests/style` (fail any line of code indented `^(  )+[^ ]` where the
file's indent unit is 4) or gate the `align` count per file with a much tighter ratchet after the
reformat lands.

### S4. Narrative comment layer — C2/F18 register (refactor-plan admission (b), quantified)
> **RESOLVED 2026-07-08** (de-narrative pass, refactor-plan Phase C pass 2). 189 blocks → 62
> (1076 → 397 full-line comment lines): `imguiapp.h` 16→6, `internal.h` 52→20, `imguiapp.cpp`
> 121→36. Every essay condensed to ≤3 behavior/constraint lines in place (rationale already in
> docs/commits); AV byte-format essays now cite the frozen av-design record catalog. Survivors are
> the sanctioned forms: file/region `[SECTION]` indexes, section banners with ≤3 description
> lines, group labels + ≤3 statements, two invariant tables (validate relation, drag neighbor
> capture), Δ7 fold-region headers, and one 5-line contract (IoFrame capture-point, was 16). Two
> stale `imguiapp_av.h` references fixed in passing (types live in `imguiapp.h` since the fold).
> Gate: all three suites green, codegen corpus byte-locks intact, style ratchet OK (no gated growth).
~189 blocks of ≥4 consecutive full-line comment lines (~1076 lines): `imguiapp.h` 16 blocks,
`internal.h` 52 (29.5% of file is full-line comments), `imguiapp.cpp` 121 (longest 26 lines).
Inline/trailing comment register conforms; the *preamble essays* are the drift.
**Fix (= Phase C pass 2, de-AI-comment)**: per block: keep max 1-3 lines of behavior/constraint
statement; move WHY-essays worth keeping into the relevant design doc under `docs/` (or the
`[SECTION] Commentary` idiom, A16, for subsystems with subtle contracts — the tables.cpp
precedent); delete the rest. Rationale → commits/docs, per the plan's own rule. Work through
files in Phase C order; suites + corpus gate each commit.

### S5. No deprecation machinery, no breaking-changes log — A14/A15
0 `OBSOLETED`, 0 obsolete section, 0 breaking-changes log. At v0.4.x with one consumer this is
cheap to stand up and expensive to retrofit later.
**Fix**: (1) add `// [SECTION] Obsolete functions and types` at the tail of `imguiapp.h` wrapped
in `#ifndef IMGUIAPP_DISABLE_OBSOLETE_FUNCTIONS` (empty today, scaffolding tomorrow); (2) add an
`API BREAKING CHANGES` block to the top-of-file docs in `imguiapp.cpp` (reverse-chronological
`- YYYY/MM/DD (0.X.Y) - description` — seed it with the known ABI break
`20260705→20260706` from the DLL preview work); (3) adopt the staged lifecycle for the next
rename: inline redirect + `// OBSOLETED in 0.X.Y (from Month Year)`.

### S6. FIXME marker system absent — F20
0 FIXME/FIXME-`<AREA>` in ~31k lines; 11 TODO hits are all codegen string literals (not tags);
deferred work lives only in docs.
**Fix**: (1) adopt the house tags: `FIXME-<AREA>` with imguiapp areas (`FIXME-CANVAS`,
`FIXME-GRAPH`, `FIXME-CODEGEN`, `FIXME-AV`, `FIXME-PREVIEW`, `FIXME-OPT`); (2) when touching code
near a known doc-tracked debt, drop the tag at the site; (3) add F20 tag-census check to
`tests/style` lint (fail on TODO/HACK/XXX outside string literals; count FIXME for info).

---

## Tier 3 — Mechanical fixes (sed-class; batch into one or two commits)

| # | Rule | Finding | Fix |
|---|---|---|---|
| M1 | F (gate) | 28 pinned gate findings: ~26 missing `} // namespace X` closers (`imguiapp.h:240,1337`, `imguiapp.cpp` ×18, 1/backend ×5), 2 brace hits | Add closers; fix 2 braces; re-pin `imguiapp-baseline.json` (drops 132→104, vendored-only) |
| M2 | N13 | 212 `k`-prefix constants (`kAppHue*` `:7633`, `kAppGraph*` `:8874`, `kAvMetaMagic` `:24880`, …) — foreign Google convention, 0 house-form | Decide target: SCREAMING_SNAKE for tunables (house form, `DRAGDROP_HOLD_TO_OPEN_TIMER` precedent). AST-dump rename plan → clang-rename batch; block-local `const float kX` → plain snake_case locals |
| M3 | N18 | Macro namespace split: `IM_LABEL_SIZE` / `IMGUI_APPLAYER_VERSION` / `IMGUIAPP_HAS_REFLECT` / `IMGUIX_DISABLE_TOOLS` | Rule: `IM_` = value macros; `IMGUIAPP_` = everything this library defines (config/feature/version); `IMGUIX_` = umbrella build switches only. Rename `IMGUI_APPLAYER_VERSION*` → `IMGUIAPP_VERSION*`. Keep `IMGUIX_DISABLE_TOOLS` (it IS an umbrella build switch) but document the rule in imappconfig.h |
| M4 | N20 | ~~`imapp_config.h` prefix matches nothing~~ **RESOLVED 2026-07-08 — finding was wrong.** imgui's own config is `imconfig.h`: the config header is exactly where imgui uses the contracted prefix (`im` : `imgui` :: `imapp` : `imguiapp`; the `ImApp*` symbol family already sanctions the contraction). Original fix (rename to `imguiapp_config.h`) would have departed from canon. | Exact-form asserted: `git mv imapp_config.h imappconfig.h` (imconfig.h carries no underscore); include + CMake + style-gate scope updated. Spec correction 4 records the N20 fix |
| M5 | A1 | No file banners/links on headers; no `IMGUI_DISABLE` wrap | Add `// imguiapp, v0.4.1 WIP` + role line + short links block to all files; wrap bodies in `#ifndef IMGUI_DISABLE` (imguiapp cannot function without imgui — honor upstream's master switch) with `#endif // #ifndef IMGUI_DISABLE` |
| M6 | A3 | Warning pragmas MSVC-only in imguiapp.h; absent in internal.h/cpp | Copy imgui's 3-compiler push block (MSVC→clang w/ `__has_warning`→GCC, reason per line) into all three; matching pops at EOF |
| M7 | A8 | Missing trailing symbol comments on includes (`imguiapp.h:24,33-35`, `internal.h:43,45-48`) | Add `// symbol, symbol` comments to every include |
| M8 | A9 | Config not included first (`imguiapp.h:24` after imgui headers); redundant `imgui_internal.h` re-include (`imguiapp.cpp:21`) | Move config include to line ~20 (before imgui.h, matching imgui.h:67-70 order); delete redundant include |
| M9 | A10 | No CHECKVERSION analog | Add `IMGUIAPP_CHECKVERSION()` → `AppDebugCheckVersionAndDataLayout(ver, sizeof(ImGuiApp), sizeof(ImGuiAppRecorder), …)`; call it in `InitializeApp` |
| M10 | A13 | Version `0.4.1`/`401`, no WIP | Adopt XYYZZ: `#define IMGUIAPP_VERSION_NUM 419` → 5-digit `00401`-style is overkill at 0.x; minimum: add ` WIP` suffix discipline + keep NUM monotonic; document scheme beside the defines |
| M11 | A17 | No `[SECTION] Forward Declarations` block in cpp | Add one per (sub-)file during S1 reorder; collect scattered `static` fwd decls into it |
| M12 | A18 | 2 static asserts in 27k lines; 7 `_COUNT` enums with paired tables unlocked | Add `IM_STATIC_ASSERT(IM_COUNTOF(<table>) == <Enum>_COUNT)` beside each table (`ImGuiAppFieldType_`, `ImGuiAppNodeKind_`, `ImGuiAppLayerType_`, `ImGuiAppPortKind_`, `ImGuiAppEdgeKind_`, `ImGuiAppEventEdge_`, `ImGuiAppEventAction_`) |
| M13 | G7 | `AppRegisterSidebar(..., ImGuiWindowFlags flags)` no `= 0` (`imguiapp.h:169`) | Add `= 0` |
| M14 | G9/N10 | `AppWALWrite` (`imguiapp.h:220`) and `AppGraphNotify` (`imguiapp.cpp:12705`) have no V twins | Add `AppWALWriteV(..., va_list) IM_FMTLIST(3)` + `AppGraphNotifyV`; re-implement the `...` forms over them (imgui pattern) |
| M15 | G4 | `PushWindowControl`/`PushSidebarControl` have no Pop; teardown host-scoped, undocumented | Add decl-site comment: `// No Pop: hosted controls are torn down by PopAppWindow()/PopAppSidebar().` |
| M16 | N4 | `ImGuiAppCmdSurface_`, `ImGuiAppControlMethod_`, `ImAppEase_` lack `typedef int` pairing; fields typed bare `int` | Add typedefs + retype `Surfaces`/`Mods`/`ease` fields |
| M17 | N5 | Stem mismatches: `ImGuiCanvasPinKind_`→`ImGuiCanvasPin_*` (`:2249`), `AppAlignMode_`→`AppAlign_*` (`:14262`), `ComposerPillState`→`ComposerPill_*` + missing `_` (`:5099`) | Rename type stems to match values (fewer sites): `ImGuiCanvasPin_`, `AppAlign_`, `ComposerPill_` |
| M18 | N6 | `ImGuiAppCmdSurface_` flags enum lacks `_None = 0` (`internal.h:726`) | Add `ImGuiAppCmdSurface_None = 0,` |
| M19 | N8 | `ImGuiAppCommandPrivate_` drops public prefix + lives in public header (`imguiapp.h:276-279`) | Move to `imguiapp_internal.h`; value keeps public prefix: `ImGuiAppCommand_PrivateBegin_ = ImGuiAppCommand_COUNT` |
| M20 | N1/§2.1 | `ImAppTween/Timer/Spring/Pulse` data structs use snake_case multi-word members (`stiffness`, `duration`; `internal.h:1090-1139`) | Rename to PascalCase (ImVec2.x/y precedent covers single letters only) |
| M21 | F21 | Bare `#endif` on long guards (`imguiapp.h:1341`, cpp `:24835-27183` cluster); echo-format drift | Add `// #ifndef X`-form echoes on long spans; trim prose suffixes |
| M22 | F11 | Flag typedefs double-column only (no third description column); some typedefs missing the `// -> enum` pointer | Complete the triple-column on `imguiapp.h:88-94`; add pointer comments at `:298,306` |
| M23 | B1/B2 | Backend headers bare (`win32_opengl3.h:1-9` etc.); no CHANGELOGs anywhere | Per backend: add identity line + `// Implemented features:` checklist + usage footer; add `// CHANGELOG` to each .cpp seeded from git log (one entry per shipped change) |
| M24 | B16 | No `#ifndef IMGUI_DISABLE` in any backend | Same M5 treatment: `#include "imgui.h"` then guard body, `#endif // #ifndef IMGUI_DISABLE` |
| M25 | B17 | Host-backend statics unprefixed (`ShutdownBackend`, `NewFrame`, `GetClientSize`, `Hook_*` in anon namespaces) | Prefix with `ImGuiApp_ImplXXX_` even when static (4 host files) |
| M26 | I21/G18 | 10/151 asserts carry `&& "message"`; some user-reachable preconditions use trailing `//` comments invisible in assert dialogs (`imguiapp.cpp:488,1184,6711`) | Sweep the 151: every assert reachable from a public API misuse gets a sentence-with-hint in-expression; internal invariants may stay bare (imgui precedent) |
| M27 | G19 | 0 `IM_ASSERT_USER_ERROR`; recoverable misuse (double-register `:1152`, duplicate key `:1184`) hard-aborts | Route registration/compose-path misuse through `IM_ASSERT_USER_ERROR[_RET]` with recovery (skip + WAL log), keeping hard asserts for corruption |
| M28 | I22 | 0 DebugNode introspection for `ImGuiAppGraph`/`ImGuiCanvasState`/`ImGuiAppNode` | Add `DebugNodeAppGraph(g)`, `DebugNodeAppNode(g, n)`, `DebugNodeCanvas(c)` under `#ifndef IMGUI_DISABLE_DEBUG_TOOLS` (empty stubs in `#else`), hook into a `Tools → App Metrics` window beside the imgui Metrics toggle (`:6758`) |
| M29 | I20 | 0 `IM_MSVC_RUNTIME_CHECKS_OFF`; ListClipper ×1 | Low priority: bracket the canvas hot loops after profiling shows need; use ListClipper in the outliner/long lists where row counts grow |
| M30 | F23 | Double blanks between sibling functions (`imguiapp.cpp:9335`) | Fold into S3's clang-format apply (MaxEmptyLinesToKeep handles it) |

## Spec corrections (audit found the spec wrong/stale — fix imgui-house-style.md + bug-classes cross-refs)

1. **I17 parenthetical stale**: says "single remaining process global is `g_app_assert_wal`" — the
   WAL global is now the `AppAssert()` singleton + `SetAppAssertWAL()` (`imguiapp.cpp:57-67`).
   Also stale in `docs/archive/phase-coherence-audit-2026-07-03.md` finding D (archived; leave).
2. **I1 framing**: spec should note the sanctioned substitution (Δ3) once ratified, so future
   audits don't re-flag 283 sites.
3. **B-section applicability**: add a note that composed/host backends (wrapping real imgui
   backends behind a seam) satisfy B9-B14 through the wrapped backend — audit them at the seam.
4. **N20 incomplete** (found 2026-07-08, via M4): the rule's glob `imgui*.{h,cpp}` misses imgui's
   own `imconfig.h` — the config header uses the CONTRACTED library prefix, no underscore;
   everything else uses the full prefix. Fixed in the spec; imguiapp analog: `imappconfig.h`.
5. **N1/N11 third tier asserted; N11/N12 disambiguated** (2026-07-08): "low-level helpers" was
   too vague — N11 rewritten as a five-point membership test (placement in Generic helpers under
   `// Helpers: <Family>` labels; `Im<Family><Op>` grammar; context-free signature; no
   `GImGui`/`ImGui::`/mutable statics in the body; `IMGUI_API`-in-.cpp or header-inline linkage),
   derived from a mechanical scan of every bare-`Im*` definition in imgui core. Scan findings
   recorded in the spec: prefix alone ≠ membership (`ImFontAtlas*`/`ImFontCalc*` touch the
   context, live outside the section); canon's own wart `ImFormatStringToTempBuffer[V]` (reads
   `GImGui->TempBuffer`) quarantined, not license. N12 sharpened: file-local statics take `Im`
   only if promotable to the section verbatim. `ImApp*` asserted as the tier one level up
   (full member list in N11; `ImAppAssertFail` = sole sanctioned exception, IM_ASSERT sink).
   Enforcement: `ImAppItemStyle` (file-local pop-count struct riding context-touching
   `PushItemStyle`/`PopItemStyle`) renamed `ItemStyleScope` per sharpened N12, 9 sites, one TU.

## Sequencing (each wave gated: imguix-tests + imguix-core-tests + imguix-headless-verify green, codegen corpus byte-identical, style ratchet re-pinned monotonically down)

1. **Wave 0 — decisions**: write `docs/style-deltas.md` ratifying/rejecting Δ1-Δ6 (+Δ7 file split).
   Everything below assumes the recommendations above.
2. **Wave 1 — sed-class batch**: M1, M5, M6, M7, M8, M13, M15, M18, M21, M22, M30 + S2 index
   (M4 resolved 2026-07-08 as the exact-form `imappconfig.h` rename — see the M4 row)
   regeneration. One commit ("style: mechanical house-style conformance"), zero behavior.
3. **Wave 2 — format migration**: S3 clang-format APPLY on 2-space regions (whitespace-only
   commit) → re-pin baseline; add the indent lexical check to `tests/style`.
4. **Wave 3 — rename batch**: M2 (k-constants), M3 (macros), M16, M17, M19, M20, M25 via AST
   dump → clang-rename plan; public-surface renames (if any leak into imguiapp.h) get M-A14
   staged-deprecation shims instead of silent rename.
5. **Wave 4 — Phase C proper**: S1 (.cpp reorder/split), S4 (de-narrative pass), M11 — the
   refactor-plan passes, now with this audit as the drift map input.
6. **Wave 5 — functional adds**: M9 (CHECKVERSION), M12 (static-assert locks), M14 (V twins),
   M23/M24 (backend anatomy), M26/M27 (assert quality), M28 (DebugNode), S5 (deprecation
   scaffolding), S6 (FIXME adoption), T2 (recorder pimpl), T4 (backend Data pattern).
7. **Continuous**: style gate ratchet must only go down; S2's index-sync and S6's tag-census
   checks land in `tests/style` so the fixed classes cannot regress.
