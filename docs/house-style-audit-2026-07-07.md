# House-Style Compliance Audit — imguiapp vs imgui-house-style.md (2026-07-07)

**Spec**: [imgui-house-style.md](imgui-house-style.md) (rule IDs P/N/F/A/G/B/I/D/C cited throughout).
**Audited**: `imguiapp.h` (1341 L), `imguiapp_internal.h` (2384 L), `imguiapp.cpp` (27343 L),
`imappconfig.h` (8 L; `imapp_config.h` until the M4 exact-form rename), `backends/imguiapp_impl_*` (11 backends); `imguiapp_reflect.h` skeleton only
(body = vendored qlibs/reflect port, exempt like `imstb_*`). Method: six parallel audits, one per
spec dimension, every verdict evidence-cited. Mechanical layout was pre-screened by the style gate
(`tests/style/`, baseline `imguiapp-baseline.json`: 132 pinned, 28 real).

> **Maintenance 2026-07-08**: resolved items pruned to one-line records; live status moves to
> [house-style-audit-2026-07-08.md](house-style-audit-2026-07-08.md) (re-audit against the
> expanded 181-rule spec). This file remains the 2026-07-07 baseline record.

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

### T1. Virtual class hierarchy — A19/P1
**RATIFIED Δ1** (style-deltas.md); decl-site pointer citing Δ1 in place at `ImGuiAppInterface`. Closed.

### T2. STL in public surface — A8/G20/I6
**Δ2 ratified** (template front / reflection layer / OS glue, scoped — style-deltas.md); recorder
thread state now behind the `ImGuiAppRecorderThread*` pimpl over `ImGuiAppThreadFuncs`; `imguiapp.h`
includes no threading headers. **Residual**: unlicensed glue slips (`std::sort`, `snprintf` sites)
— carried to the 2026-07-08 re-audit.

### T3. `g` names the graph — I1-I3/N17
**RATIFIED Δ3** with the no-other-`g` rule (style-deltas.md). Closed.

### T4. Backend seam state — B6/B7
**Δ4 ratified**; pre-Δ4 `GBackend`/`GState` file-values fixed to the `IM_NEW`-allocated
`Backend.UserData` + accessor form per style-deltas.md. Closed (re-audit verifies B8 asserts).

### T5. `*_state.h` shared platform-state sidecars — B6
**Δ5 proposed, NOT yet ratified** (style-deltas.md Pending). Open steps: ratify or conform; give
each `_state.h` a B1-style header block (2 files, 6 lines each).

### T6. Singleton accessors vs `GIm*` globals — N13/I17
**Δ6 proposed, NOT yet ratified** (style-deltas.md Pending). Open: the ~9 ad-hoc mutable statics
(`sym_ready`, `in_assert`, `s_monitor_hz`, `s_freq`, `epoch`, `s_fallback_ready`, `s_vcvars`,
`version`, `seeded_version`) each move into the owning singleton or get a process-wide
justification comment (`MfStartupRefs` precedent).

---

## Tier 2 — Systemic drift (real work, mostly already planned as Phase C)

### S1. `imguiapp.cpp` definition order ⊥ header order — A16 (was SEVERE)
> **RESOLVED 2026-07-07** — Δ7 ratified (unity file kept), ordering fixed to header decl order,
> indexes rebuilt 1:1. Residual 6 rank descents (2 fold-boundary Δ7-fine, 4 across
> `IMGUIX_DISABLE_TOOLS` boundaries in nodes `Scope interior`) deferred to Phase C proper.
> Gate: suites green, codegen corpus byte-identical.

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

### S4. Narrative comment layer — C2/F18 register
> **RESOLVED 2026-07-08** (de-narrative pass, Phase C pass 2): 189 blocks → 62 (1076 → 397 lines);
> `imguiapp.h` 16→6, `internal.h` 52→20, `imguiapp.cpp` 121→36. Survivors are sanctioned forms
> (indexes, banners ≤3 desc lines, labels+≤3, two invariant tables, Δ7 fold headers, one 5-line
> IoFrame contract). Stale `imguiapp_av.h` refs fixed. Gate: suites green, corpus byte-identical,
> style ratchet OK.

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
| M4 | N20 | **RESOLVED 2026-07-08 — finding was wrong** (imgui's own config is `imconfig.h`: contracted prefix is canon for the config header) | Exact-form `git mv imapp_config.h imappconfig.h` applied; include/CMake/style-gate updated; spec correction 4 fixed N20 |
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

## Spec corrections (all APPLIED to imgui-house-style.md; kept as one-line records)

1. I17 stale parenthetical (WAL global → `AppAssert()` singleton) — fixed.
2. I1 framing notes the Δ3 substitution — fixed.
3. B-section applicability note for composed/host backends (audit at the seam) — fixed.
4. N20: config header uses the contracted prefix (`imconfig.h`; analog `imappconfig.h`) — fixed.
5. N11/N12 rewritten as five-point membership test; `ImApp*` third tier asserted;
   `ImAppItemStyle` → `ImGuiAppItemStyleScope` enforced (9 sites) — fixed.
6. A22/A23 placement matrix + TU-local rules added — fixed.
7. A24-A28 ordering rules (cardinal, ordinal, directions) added — fixed.
8. N21 + I16-ext + I26-I28 implementation-file content coverage — fixed.
9. Four-agent .cpp survey integrated: N22-N24, F24-F28, A29-A32, G23-G24, I29-I41, D7-D9,
   warts 6-10; spec now 181 rules — fixed.

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
