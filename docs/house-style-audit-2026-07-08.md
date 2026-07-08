# House-Style Compliance Re-Audit — imguiapp vs imgui-house-style.md (2026-07-08)

**Spec**: [imgui-house-style.md](imgui-house-style.md) at 181 rules (post spec-corrections 1-9: N1-N24,
F1-F28, A1-A32, G1-G24, B1-B18, I1-I41, D1-D9, C1-C3) + [style-deltas.md](style-deltas.md) Δ1-Δ7
(Δ5/Δ6 still pending). **Baseline**: [house-style-audit-2026-07-07.md](house-style-audit-2026-07-07.md).
**Audited**: `imguiapp.h` (1335 L), `imguiapp_internal.h` (2300 L), `imguiapp.cpp` (27241 L),
`imappconfig.h`, `backends/*` (18 files); `imguiapp_reflect.h` body exempt. Method: four parallel
evidence-cited audits (naming / formatting / architecture+API / idioms+backends+demo), every count
grep-measured.

**Verdict in one line**: the 07-07 structural work held — S1/S4 resolved, T4 fixed, core
allocator/math/string/nullptr discipline perfect, Δ3/Δ4/Δ7 compliance clean — but the never-audited
rule families expose one large unratified divergence (the C++ dialect: capturing lambdas + `auto`),
the S2 index-drift class is REGRESSING because its promised lint never landed, and the Wave-1/Wave-5
mechanical batches (banners, guards, backend anatomy, assert quality) remain almost untouched.

## Scorecard

| Dimension | Conform | Partial | Violation | Top finding |
|---|---|---|---|---|
| §2 Naming (N1-N24) | 7 | 10 | 7 | k-tier grew (243 uses); ~55 unprefixed TU-local types; `ImGuiCanvas*` tier squat |
| §3 Formatting (F1-F28) | 8 | 8 | 5 (+6 gate-covered) | S3: 44% of cpp lines off-grid; bare `default:` 50:1 inverted |
| §4 Architecture (A1-A32) | 6 | 7 | 8 (+2 n/a) | no `IMGUI_DISABLE` wrap anywhere; S2 regressing; S5 still absent |
| §5 API grammar (G1-G24) | 4 | 5 | 2 | silent overpop in all four `PopApp*`; no V twins |
| §6 Backends (B1-B18) | Δ4 seam + Data/asserts ✓ | B5/B7/B15 | B1/B2/B16/B17 | T4 FIXED; anatomy/CHANGELOG/guards/prefix wave unmoved |
| §7 Idioms (I1-I41) | 8 | 5 | 3 | I29 dialect breach (43 `auto`, 33 capturing lambdas) |
| §8 Demo (D1-D9) | 3 | 2 | 3 slips | sample controls not in user register |

**Credits (hold the line)**: I5/I6/I11 core perfect (0 raw alloc, 0 std::min/max, 0 std::sort);
nullptr 100%; `IM_UNUSED` 76 vs `(void)` 0; 0 goto, 0 `[[fallthrough]]`; F20 zero stray work
markers; F24 literals near-perfect (95:1 hex, 0 bare `N.f`); G23 deferred-mutation grammar CONFORM
(typed sentinels, post-CanvasEnd application); I38 ownership comments real; I41 AV encoder vtable
matches the ImFontLoader shape; A22 placement 8/10 rows conform; headers' index↔body exact (A2);
T2 core residual CLEARED (0 `std::sort`, 0 core `snprintf`); T4 verified fixed in all four hosts.

---

## Tier 1 — Decisions needed (ratify or rewrite; a Δ or a plan, then steps)

### R1. C++ dialect breach — I29 (SEVERE; the largest unratified divergence)
`imguiapp.cpp`: 43 `auto` bindings, **33 capturing lambdas** (`[&]`, `[c]`, `[&changed]` —
`:2204-2260`, `:5261`, `:16583-16648`, `:22208`), 10/11 range-fors `auto&`; backends add 2 capturing
lambdas (sdl2_wgpu `:99,:131`) + `auto` casts (win32_vulkan `:670,:844`). Canon: 0 `auto`, lambdas
captureless-C-callback-only, range-for explicit-typed. The capturing-lambda-as-local-function idiom
is structural in the editor UI — **decide Δ8 (license capturing lambdas as scoped local functions in
tool/editor regions, keep core clean) or schedule the rewrite** (struct Func + explicit types,
~90 sites). Range-for `auto&` → explicit element types is mechanical either way (13 sites total,
F26); recommend fixing those regardless of the Δ8 decision.

### R2. `ImGuiCanvas*` tier squat — N1 (HIGH)
6 structs + 4 enums (`imguiapp_internal.h:119-124`, `:1786-1898`, `:2170-2172`; `ImGuiCanvasState`
alone 178 uses) claim upstream imgui's `ImGui*` tier from imguiapp code — collision surface with
future imgui symbols. **Decide: ratify Δ9 (canvas family keeps `ImGuiCanvas*` as a deliberate
"engine-grade, imgui-adjacent" tier) or rename to `ImGuiAppCanvas*`** (AST rename, one TU + headers).
Same decision covers `ImStackTrace` → `ImAppStackTrace` (`internal.h:1171` — bare-Im foundation-tier
squat, N11 fails points 1-2; rename is 1 symbol, recommend NO ratification there).

### R3. Sample-control register — D3/D8 (MED)
RandomTime/Breathing (the user-register showcase) use `ImStrncpy`/`ImFormatString`/`ImHashData` +
`std::string_view` + snake_case members + `kDemo*` palette (`imguiapp.cpp:3505-3740`). Either
re-register as user code (public API only, PascalCase members — but members feed reflection/codegen,
so casing has blast radius shared with M20) or amend A23's demo arrow to state the exception.
Bundle the M20 decision here (ImAppTween/Timer/Spring/Pulse snake_case members, same blast radius).

### R4. Forward-decl ordering direction — A28 (MED)
Both headers order fwd decls semantically/dependency-wise (`ImGuiAppLayerBase` before
`ImGuiAppLayer`, `imguiapp.h:46-51`; `internal.h:80-99`) vs A28's alphabetical-within-group.
**Conform or ratify a "dependency order, stated" delta** — an ordered list with unstated direction
is a spec bug by A28's own closing clause.

### R5. Semantic-zero enums — N6 (LOW)
17 mode/discriminator enums open with semantic zeros (`_Off/_Auto/_Task`) matching canon's
`ImGuiCol_` shape but not N6 as written. Amend N6 (allow semantic zero for pure-mode enums;
require `_None = 0` for flags and nullable discriminators) — cheaper and truer than 17 renames.
Hard N6 violations stay violations: see M-table below.

### Carried pending: Δ5 (`*_state.h` sidecars — now also carry public `RunLoop` decls for the two
headerless host TUs; ratify with scope or give those TUs real headers) and Δ6 (singleton accessors;
9 ad-hoc mutable statics persist incl. two `s_` Hungarian — `s_monitor_hz :285`, `s_freq :369` — a
third register beyond both canon and the Δ6 proposal).

---

## Tier 2 — Systemic drift

### S2. Section-index drift + decorated banners — A2/F19 (SEVERE — REGRESSING, third audit)
Zero movement; classes regrow freely because the promised `tests/style` index-sync check never
landed. Current state: demo sub-index missing `[SECTION] Playback debugger` (body `:4129`) +
final-entry title mismatch (`:3466` vs `:6661`); nodes sub-index missing 3 sections
(`Theme-derived colors :7566`, `Op operator vocabulary :9354`, `Window section :9615`) + 2 order
swaps + 3 title drifts; AV sub-index missing 5; DLL-preview region (`:6807-7385`) has ZERO
`[SECTION]`s; QOI (`:26647`) and testharness (`:26908`) regions have no index; all 10 decorated
`====`/`####` fold banners persist. **Fix order: (1) land the index↔body 1:1 lint in `tests/style`
FIRST, (2) regenerate indexes from body markers, (3) convert banners to dashed `[SECTION]` form.**

### S3. 2-space indent regions — F1 (bulk; quantified)
`imguiapp.cpp` 4,141 exact-2-space lines (11,987 non-mult-4 = 44% of file; 231 regions ≥20 lines —
every folded TU from `:1826` on), `internal.h` 1,321 (largest run 600 lines), `impl_libav` 226,
`impl_mediafoundation` 156, `impl_qoi` 113. Clean: public headers' non-folded parts, sdl2*/win32*/
wgpu/vulkan backends. Migration pipeline already exists (S3 plan: clang-format APPLY + suites +
corpus byte-compare + baseline re-pin + indent lexical check).

### S7 (new). Backend anatomy wave — B1/B2/B16/B17 + M23/M24/M25 (HIGH, unmoved)
0 header anatomy blocks, 0 CHANGELOGs, 0 `#ifndef IMGUI_DISABLE` guards (18 files), ~45 unprefixed
backend-internal functions (22 static + ~23 in anon namespaces — canon never uses anon namespaces),
`GBackend` identifier itself unprefixed, AV vtable callbacks contracted (`ImGuiAppLibav_*` vs
`ImGuiApp_ImplLibav_*`), `std::max` ×8 + `<algorithm>` in SDL backends (Δ2 does not license
backends), `IMGUIX_API`/`IMGUI_API` export split unstated (B4), B15 viewport hooks unbannered,
6 backend headers carry UTF-8 BOMs.

### S8 (new). Recoverable-error machinery — G4/G19 + M26/M27 (HIGH)
**Silent overpop**: all four `PopApp*` functions `if (empty) return;` — no assert, no WAL record
(`imguiapp.cpp:988-1031`) — precisely the "100%-silent recovery" canon refuses, missing G4's named
overpop assert. 0 `IM_ASSERT_USER_ERROR` anywhere; 11/152 asserts carry messages (7%); the
invisible-trailing-comment anti-pattern persists (`:482`). Route Pop/register misuse through
`IM_ASSERT_USER_ERROR` + WAL; sweep user-reachable asserts into sentence-with-hint form.

### S9 (new). Micro-lexicon batch — F25/F26/F14/F28 + N22/N23 (sed-class in aggregate)
Bare `default:` 50 vs 1 merged (invert to canon: merge onto named case or fall out to
`IM_ASSERT(0)`); index name `i` ×398 vs `n` ×1 (drift, low stakes — decide once); 15 C++ casts
(13 `static_cast`, 2 `const_cast` — sites listed in formatting report); 13 `sizeof(Type)` in mem*;
1 `while (true)` (`:23818`); the single `ImQsort` comparator is an anonymous non-`IMGUI_CDECL`
lambda with `a/b` params (`:27183` — N22's exact fenced hazard); k-tier: 56 defs / 243 uses, 0
UPPER_SNAKE tunables (M2 GREW from 212 uses; 86% in the graph-editor region — fold into the M2
rename batch, target UPPER_SNAKE per N23).

### S10 (new). TU-local type prefixes — A23/N12 (HIGH)
~55 unprefixed TU-local types in engine/tool regions of `imguiapp.cpp` (of 71; 16 demo-register
sanctioned): the 12-type `AppPv*` interpreter family (`:23368-24419`), Composer chrome (~20:
`GraphDocData :3791`, `ComposerTransport :3771`, `ToolbarControl :4612`, ...), pacer `CachedHz`
(`:280`), `AppBlStyleScope` (`:7743`), + 7 local enums (`ComposerPanel_`, `AppAlignMode_`, ...).
Rename to `ImGuiApp*` per A23 ("App"/"Composer" are not tier prefixes).

---

## Tier 3 — Mechanical fixes (M-table; carried numbers where unchanged)

| # | Rule | Finding | Fix |
|---|---|---|---|
| M5 | A1 | 0 file banners, 0 `IMGUI_DISABLE` wraps, all 22 files | banner + role line + guard + echoed endif |
| M6 | A3 | pragma block MSVC-only, 1 warning, imguiapp.h only | 3-compiler push/pop blocks all TUs |
| M7 | A8 | residual: `internal.h:43-48` + cpp `:25-30` includes uncommented; `<string>` outside Δ2 list | comment; license or drop `<string>` |
| M8 | A9 | config include still 3rd (`imguiapp.h:24`); cpp spine jumbled; redundant re-include (cpp `:26`) | reorder; delete |
| M9 | A10 | no CHECKVERSION analog | `IMGUIAPP_CHECKVERSION()` + DataLayout assert in InitializeApp |
| M10 | A13 | `IMGUI_APPLAYER_VERSION "0.4.1"`/401 — no WIP, no scheme, M3 rename pending | rename + ` WIP` + scheme comment |
| M11 | A17 | no cpp `[SECTION] Forward Declarations`; stray fwd decl `:13170` | add per Δ7 region during S2 pass |
| M13 | G7 | `AppRegisterSidebar` flags no `= 0` (`imguiapp.h:163`) — third audit | add `= 0` |
| M14 | G9/N10 | `AppWALWrite`/`AppGraphNotify` still no V twins; 0 `IM_FMTLIST` | add twins, re-implement over them |
| M15 | G4 | Push*Control "No Pop" decl comment still absent (`imguiapp.h:128-130`) | add comment |
| M16 | N4 | 7 enums unpaired (`ImGuiAppControlMethod_`, `ImGuiAppCmdSurface_`, `ImAppEase_`, canvas ×4); dup typedefs `h:90/294`, `h:91/302` | add typedefs, retype fields, delete dupes |
| M17 | N5 | 3 stem mismatches unchanged (`ImGuiCanvasPinKind_`→`ImGuiCanvasPin_*`, `AppAlignMode_`, `ComposerPillState`) | rename stems (fold into R2/S10 batches) |
| M18 | N6 | hard misses: `ImGuiAppCmdSurface_`, `ComposerLayoutVis_` flags no `_None=0`; `AVMetaRecordType_` starts at 1 | add `_None = 0` |
| M19 | N8 | `ImGuiAppCommandPrivate` still public (`imguiapp.h:272-274`) + drops public stem | move to internal.h; `ImGuiAppCommand_PrivateBegin_` |
| M21 | F21 | 8 bare `#endif` on >20-line guards incl. one 512-line (`cpp:7379`) | add echoes |
| M22 | F11 | public typedefs lack 3rd column; internal lacks `Enum:`/`Flags:` prefix; `internal.h:139` uncommented | complete columns |
| M23/24/25 | B | see S7 | — |
| M26/27 | G18/G19 | see S8 | — |
| M28 | I22 | 0 DebugNode introspection — unchanged | `DebugNodeAppGraph/Node/Canvas` under DEBUG_TOOLS |
| M29 | I20 | ListClipper ×1; 0 RUNTIME_CHECKS brackets — low priority | unchanged |
| M30 | F23 | 33 double-blank runs, 3 triples (`cpp:1838`, `:7411`, vulkan EOF) | fold into S3 format apply |
| M31 | N3 | fn-ptr members lack `*Fn` on all 3 vtables; sole typedef uses `Fn` where `Func` belongs — inverted | suffix members, rename typedef |
| M32 | N24 | 28/259 internal decls verb-first (`BeginAppNode`, `GenerateAppGraphCode`, `SaveAppGraph` clusters) | noun-first renames (batch with M2 AST plan); record `Request` (not `Queue`) as the deferred infix |
| M33 | N9 | `ImGuiApp_GetPlatformBackend` underscore grammar in public header (`imguiapp.h:644`); Push/Register grammar-flipped twins unnamed | rename; name the seam-vs-front pattern in Δ2 |
| M34 | F12 | backend public defs unpadded 0/12; `IMGUI_API` repeated on defs (libav/qoi) | pad; strip |
| M35 | F27 | 83/90 off-census buffers uncommented; de-facto keys (560 paths ×8, 1200 cmdlines ×7) undocumented | comment or document the census |
| M36 | F18 | 7 lowercase sentence-starts | capitalize |
| M37 | A18 | `kAppDockDirNames[4]` unlocked; `OutlinerKindVis` hand-9 zero-fills on enum growth | static-assert locks |
| M38 | A32 | no version-stamp form; `IMGUIAPP_PREVIEW_ABI 20260706u` = ad-hoc date tag | adopt "Since 0.Y.Z (Month Year, NUM)" with S5 |
| M39 | — | dead empty `#ifdef IMGUIX_HAS_LIBAV` pair (`cpp:3468-3469`); BOMs on 6 backend headers | delete; strip |
| M40 | A24 | internal.h Macros section precedes Forward declarations (inverted vs canon) | swap in S2 pass |
| M41 | A26 | `ShowAppGraphEditor` defined in Scope-interior section (`:15090`), not editor-render; S1's 6 descents | move with Phase C section restructure |
| M42 | I40 | 2,835-line `ShowAppGraphEditor`, no `[Part N]` spine (local `// Pass 1/2/3` shows the form known) | number the parts |
| M43 | I33 | `clicked`/`changed`/`saved_` where canon says `pressed`/`value_changed`/`backup_` (14 sites, 0 `backup_`) | rename locals |
| M44 | I35 | 3 wart-7 minority color picks (`:7531,:7553,:7778`) | switch to `(held && hovered)` form |
| M45 | G2 | `CanvasBegin` void + unconditional End; `CanvasBeginNode` bool ignored by own consumer (`:15794`); no pairing comments | pairing comments + decide conditional-End contract |

**Fixed this session**: F7/F10 EmbedRows grid defect (introduced by the S4 pass, caught by this
re-audit, realigned same day).

## Sequencing (waves gated as before: suites green + codegen corpus byte-identical + ratchet down)

1. **Wave R — decisions**: R1 (Δ8 dialect), R2 (Δ9 canvas tier + ImStackTrace), R3 (register/M20),
   R4 (fwd-decl direction), R5 (N6 amendment), Δ5/Δ6 ratification. Everything below assumes outcomes.
2. **Wave L — lint first**: S2's index↔body sync check + S3's indent check + N22/N23 greps into
   `tests/style` so no fixed class regresses again. THEN the S2 regeneration + S3 format apply.
3. **Wave M — sed batch**: M13, M15, M18, M21, M22, M26-messages, M30, M34, M36, M37, M39, M44,
   F26 explicit-type range-fors, F14 cast replacements, F28 sizeof forms, F25 default merges.
4. **Wave N — AST rename batch**: M2/N23 k-tier → UPPER_SNAKE, S10 TU-local type prefixes, M17
   stems, M31/M32/M33, I41 callback prefixes, B17 backend statics (+ de-anon-namespace).
5. **Wave B — backend anatomy**: S7 (B1 blocks, B2 CHANGELOGs, B16 guards, M24), B15 banner, Δ4/B7
   accessors in SDL hosts, B8 NewFrame asserts.
6. **Wave F — functional adds**: S8 (USER_ERROR + overpop + WAL), M9 CHECKVERSION, M14 V twins,
   S5 deprecation + breaking log (+ M38 stamps), M28 DebugNode, M42 [Part N], M45 canvas contract.
