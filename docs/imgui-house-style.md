# The Dear ImGui House Style — Canonical Specification

**Purpose.** This document is the normative specification of Dear ImGui's "house style" — every explicit and implicit convention observed in `imgui.h`, `imgui_internal.h`, the core implementation files (`imgui.cpp`, `imgui_widgets.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_demo.cpp`), and the platform/renderer backends (`imgui/backends/`). It exists so that imguiapp development can be criticized against it: code that claims to extend or sit beside Dear ImGui should be judged rule-by-rule against this spec.

**Surveyed checkout.** `imgui/` submodule, **v1.92.9 WIP** (`IMGUI_VERSION_NUM 19284`), **docking/multi-viewport branch** with the **new texture system** (`IMGUI_HAS_TEXTURES`, `ImTextureData`). Evidence citations are `file:line` against this checkout. (`imgui.h:32-37`)

**Rule strength.** Rules are written as MUST / SHOULD / MAY. "MUST" = uniform in the codebase or explicitly stated by the maintainer; "SHOULD" = dominant pattern with rare deviations; "MAY" = accepted option. §12 lists imgui's own self-acknowledged violations — deviations matching those are warts to avoid imitating, not licenses.

**The one explicit written rule.** Everything else in this spec is derived from observation, but this line is stated canon (`docs/CONTRIBUTING.md:73`):

> "Make sure your code follows the coding style already used in the codebase: 4 spaces indentations (no tabs), `local_variable`, `FunctionName()`, `MemberName`, `// Text Comment`, `//CodeComment();`, C-style casts, etc.. We don't use modern C++ idioms and tend to use only a minimum of C++11 features."

---

## 1. Design Philosophy (the "why" behind every rule)

The mission statement (`imgui.cpp:112-129`, verbatim bullets):

- Easy to use to create code-driven and data-driven tools.
- Easy to use to create ad hoc short-lived tools and long-lived, more elaborate tools.
- Easy to hack and improve.
- Minimize setup and maintenance.
- Minimize state storage on user side. Minimize state synchronization.
- Portable, minimize dependencies, run on target (consoles, phones, etc.).
- Efficient runtime and memory consumption.

**P1. C-ish C++ subset.** "This is a pragmatic C-ish codebase: we don't use fancy C++ features, we don't include C++ headers, and ImGui:: is a namespace. We rarely use member functions" (`imgui.cpp:229-231`). Minimum of C++11; no modern idioms (`CONTRIBUTING.md:73`). No exceptions, no RTTI, no STL, no virtual functions (verified: zero `virtual` in imgui.h/imgui_internal.h). Polymorphism via C function pointers only (`imgui.h:291-294`).

**P2. Debug-build performance is a feature.** "A typical idle frame should never call malloc/free... We put particular energy in making sure performances are decent with typical Debug build settings. We tend to avoid over-relying on 'zero-cost abstraction' as they aren't zero-cost at all" (`imgui.cpp:222-226`). Constant-time or O(N) algorithms only.

**P3. No build system; build from source; static link.** "We don't use nor mandate a build system for the main library" (`imgui.cpp:237-240`). "It is recommended you build and statically link the .cpp files as part of your project and NOT as a shared library" (`imgui.cpp:269`). No ABI guarantee (`imconfig.h:24`).

**P4. Asserts are load-bearing.** "Compiling with NDEBUG... is NOT recommended because we use asserts to notify of programmer mistakes" (`imconfig.h:19`). Error handling is asserts + recoverable-error machinery, never exceptions or error codes (§6.8).

**P5. Dependency avoidance as configurable feature.** Compile-time opt-outs shrink linkage (`IMGUI_DISABLE_WIN32_FUNCTIONS` etc., `imconfig.h:40-55`); each opt-out documents the KB saved.

**P6. The source is the documentation.** Docs live in header comments, the demo, and the FAQ; "Your programming IDE is your friend" (`imgui.cpp:1165`). FAQ answers are copied into imgui.cpp "to facilitate searching in code" (`imgui.cpp:1147`).

**P7. Candor.** Weaknesses stated openly next to strengths (`imgui.cpp:125-129`); regrets admitted in comments ("I am mostly regretting it now", `imgui.cpp:230`; "perhaps the worst breaking change in our history :(", `imgui.cpp:422`). Known warts are labelled in-source rather than hidden (§12).

**P8. Raw data, zero-memset.** "Our implementation does NOT call C++ constructors/destructors, we treat everything as raw data! This is intentional... Many of the structures used by dear imgui can be safely initialized by a zero-memset" (`imgui_internal.h:2295-2296`).

---

## 2. Naming Conventions

### 2.1 Casing (the four core rules)

| Element | Casing | Example |
|---|---|---|
| Local variables & parameters | `snake_case` | `label_size`, `backup_padding_y`, `text_end` |
| Functions (public, internal, static) | `PascalCase` | `ButtonEx`, `CalcItemSize`, `ImStricmp` |
| Struct/class members | `PascalCase` | `FontSizeBase`, `Size`, `Data` |
| Macro parameters | `_UPPERCASE` w/ leading underscore | `IM_ASSERT(_EXPR)`, `IM_COUNTOF(_ARR)` |

(`CONTRIBUTING.md:73`; observed universally.)

### 2.2 Type prefixes

- **N1.** `ImGui*` prefix = GUI/context-layer types (`ImGuiContext`, `ImGuiIO`, `ImGuiStyle`, `ImGuiWindow`, `ImGuiTable`). `Im*` bare prefix = lower-level standalone types: draw/font/math/containers (`ImVec2`, `ImRect`, `ImDrawList`, `ImFont`, `ImVector<>`, `ImPool<>`). (`imgui.h:172-217`) imguiapp analog: `ImGuiApp*` = applayer/context types; bare `ImApp*` = standalone applayer value types (`ImAppTween/ImAppTimer/ImAppSpring/ImAppPulse`), the `// Helper:` tier one level up.
- **N2.** Scalar aliases: `Im` + `S`/`U` + bit width: `ImS8`…`ImU64` (`imgui.h:162-170`). Domain index aliases use `Idx` suffix over a scalar alias: `typedef ImS16 ImGuiTableColumnIdx;` (`imgui_internal.h:223-224`).
- **N3.** Function-pointer typedefs: `*Callback` = user-supplied hook (`ImDrawCallback`, `ImGuiInputTextCallback`); `*Func` = settable function slot (`ImGuiMemAllocFunc`); `*Fn` suffix on function-pointer *members* of platform IO (`Platform_GetClipboardTextFn`, `imgui.h:4231`); `Platform_`/`Renderer_` domain prefixes on viewport handler members (`imgui.h:4284`).

### 2.3 Enums

- **N4.** Two shapes: (a) `typedef int ImGuiXxx;` + `enum ImGuiXxx_` (trailing underscore) — the default, used for all flags and most indices; (b) real typed `enum ImGuiXxx : int` only when a storage size matters or values are stored typed (`ImGuiKey`, `ImGuiDir`, `ImGuiSortDirection : ImU8`; `imgui.h:225-228`). Stated rationale: strongly typed enums "add constraints (can't extend in private code, can't store typed in bit fields, extra casting on iteration)" (`imgui.h:220`).
- **N5.** Values named `ImGuiTypename_ValueName` (full type stem + `_` + PascalCase descriptor): `ImGuiWindowFlags_NoTitleBar` (`imgui.h:1217`).
- **N6.** Flag enums and nullable discriminators start `ImGuiXxx_None = 0` (exception: `ImGuiDir_None = -1`, §12). Pure-mode enums — every value a valid state, no empty/absent case — open with a semantic zero (`ImGuiCol_Text` first, `imgui.h:1825`; `ImGuiMouseCursor_Arrow = 0` with `_None = -1` for the nullable variant, `imgui.h:2056-2057`). Index enums end with `ImGuiXxx_COUNT` (uppercase). Ranges use `_BEGIN`/`_END` sentinels (`ImGuiKey_NamedKey_BEGIN`, `imgui.h:1629`).
- **N7.** Flag enums use `1 << n`, no `_COUNT`. Named OR-combinations reuse the prefix (`ImGuiWindowFlags_NoDecoration = ...|...`, `imgui.h:1238`). Masks and internal default sentinels take a **trailing underscore**: `ImGuiMod_Mask_`, `ImGuiButtonFlags_PressedOnMask_`, `ImGuiButtonFlags_PressedOnDefault_` (`imgui.h:1741`, `imgui_internal.h:1075-1076`).
- **N8.** Internal enum extension: public enums end with an `// [Internal]` block ("Don't use!" comments), or a separate `enum ImGuiXxxFlagsPrivate_` in imgui_internal.h whose *values keep the public* `ImGuiXxxFlags_` prefix, preceded by `// Extend ImGuiXxxFlags_` (`imgui_internal.h:1054-1076`).

### 2.4 Functions

- **N9.** Public API lives in `namespace ImGui`, unprefixed, PascalCase, verb-first. Matched-pair families: `Begin/End`, `Push/Pop`, `Get/Set`, `Open/Close`; query prefixes `Is`/`Has`/`Want`; debug windows `Show*`; domain groups share a noun prefix (`Table*`, `Tree*`, `Dock*`).
- **N10.** Suffixes: `Ex` = richer-parameter engine variant (usually internal; public `Button` wraps internal `ButtonEx`, `imgui_widgets.cpp:828-831`); `V` = va_list twin of every variadic function (`TextV`); axis suffixes `X`/`Y` (`SetScrollHereX/Y`, `PushStyleVarX/Y`); arity digits (`DragFloat2/3/4`); `Scalar`/`ScalarN` for type-erased variants; `V` prefix for vertical orientation (`VSliderFloat`).
- **N11.** The foundation tier: bare-`Im*` free functions. Membership is a five-point test — the name prefix alone does NOT confer membership:
  1. **Placement**: declared at global scope (never inside `namespace ImGui`) in the `[SECTION] Generic helpers` block (`imgui_internal.h:355+`), under a `// Helpers: <Family>` sub-label (value types under `// Helper: <Type>`).
  2. **Name grammar**: `Im<Family><Operation>`, family stem = the sub-label stem: `ImHash*`, `ImStr*`/`ImMem*`, `ImFormat*`/`ImParse*`, `ImText*`, `ImFile*`, math verbs (`ImMin/ImMax/ImClamp/ImLerp/ImTrunc/ImFloor/ImFabs/...`), `ImBezier*`/`ImLine*`/`ImTriangle*`, `ImAlphaBlendColors`, `ImQsort`.
  3. **Signature is context-free**: parameters/returns are primitives, caller-owned buffers with explicit sizes/end pointers, caller-owned state (seed pointers, handles), or bare-`Im*` value types — never `ImGuiContext*` and never a type that presumes one (`ImGuiWindow*`, `ImGuiTable*`).
  4. **Body reads no framework state**: no `GImGui`, no `ImGui::` call, no mutable file-scope state (const lookup tables fine); libc/OS calls, `IM_ALLOC`/`IM_FREE`, and other tier members are the only dependencies ("ImGui functions or the ImGui context are never called/used from other ImXXX functions", `imgui_internal.h:356-358`).
  5. **Linkage**: `IMGUI_API` with the definition in a core `.cpp`, or `static inline`/template (or macro wrapper, Maths) in the header.
  Canon's own deviations are quarantined, not license: `ImFormatStringToTempBuffer[V]` reads `GImGui->TempBuffer` (a convenience wart — do not replicate); the "High-level text functions" sub-block is banner-marked `DO NOT USE!!!`; `ImFontAtlas*`/`ImFontCalc*` free functions share the bare prefix but live OUTSIDE Generic helpers and touch the context — font-system internals, not tier members. Anything failing the test is either `ImGui::` API or a plain-PascalCase file-local static (N12).
  **imguiapp third tier**: `ImApp<Family><Op>` passes the same five points one level up — placement in the applayer helper regions (`imguiapp.h` Helpers, `imguiapp_reflect.h` constexpr type-identity layer, the animation section for `ImAppEase`); "framework state" additionally means `ImGuiApp*` and the `ImGui::App*` API; the imgui `Im*` tier below is a legal dependency. Members: `ImAppHashType` (Hash), `ImAppRising/ImAppFalling/ImAppChanged` (state-delta), `ImAppRandom/ImAppRandomFloat/ImAppRandomInt` (RNG), `ImAppEase` (maths), `ImAppFormatLabel/ImAppNulTerminate/ImAppTypeDisplayName/ImAppDataReflectable` (type identity). Sole sanctioned exception: `ImAppAssertFail`, the IM_ASSERT sink — forensics infrastructure whose job is reaching the WAL/ring dump (context-free `ImGui::App*` calls only).
- **N12.** File-local static FUNCTIONS: PascalCase; take the `Im` prefix ONLY when they pass N11's points 2-4 verbatim — i.e. promotable to Generic helpers unchanged (`static const char* ImAtoi`). A static function that touches the context, the style/ID stacks, or any framework state stays plain PascalCase (render-loop bookkeeping is not tier material). Template helpers take `T` suffix (`DataTypeClampT`); getter tables use underscore sub-namespacing (`Items_ArrayGetter`). This license is functions-only — TU-local TYPES always keep full tier prefixes (A23).
- **N21.** The subsystem-object function tier: free functions operating on one `Im*` subsystem object are named `<TypeName><Operation>` at GLOBAL scope — `ImFontAtlasBuildInit/BuildMain/BuildSetupFontLoader`, `ImFontAtlasGetFontLoaderForStbTruetype` (`imgui_internal.h:4176`, `:4272-4275`) — `IMGUI_API`, declared in that type's own internal.h `[SECTION]` (`[SECTION] ImFontAtlas internal API`, `:41`), defined in the type's owning TU (`imgui_draw.cpp`). NOT N11 tier members despite the bare-Im-looking prefix: the "prefix" is the full TYPE name, and context access is permitted (`ImFontAtlasBuildNotifySetFont` reaches `ImGui::`). Discriminate mechanically: N11 member ⇔ passes the five-point test; subsystem-object function ⇔ name starts with a declared `Im*` type name and its first parameter is that type. imguiapp analog: adopt this exact shape if an `ImApp*` value type ever grows a free-function API.
- **N22.** qsort comparators: `static int IMGUI_CDECL <Thing>ComparerBy<Key>(const void* lhs, const void* rhs)` — params ALWAYS `lhs/rhs`, `IMGUI_CDECL` mandatory ("in case compilation settings changed the default to e.g. __vectorcall", `imgui_internal.h:310-312`); 17 definitions corpus-wide. Function-local scoping uses `struct Func { static int IMGUI_CDECL ...Comparer(...) }` (`imgui.cpp:22596`) — the house substitute for local functors. Core sorts ONLY via `ImQsort` (11 sites; even stb hooks it: `#define STBRP_SORT ImQsort`, `imgui_draw.cpp:130`), zero raw `qsort` outside the demo; count arg cast `(size_t)`.
- **N23.** TU-local tunables: `static const` UPPER_SNAKE, topic-grouped, aligned trailing rationale comments — file scope (`NAV_WINDOWING_HIGHLIGHT_DELAY`, `imgui.cpp:1352`) or function-local (`MOUSE_INVALID`, `:10726`); tables prefixes its block `TABLE_` under a `// Configuration` banner (`imgui_tables.cpp:266-272`); the draw layer expresses the same tier as `#define IM_*` macros with issue refs baked into the comment (`IM_FIXNORMAL2F_MAX_INVLEN2 ... // 500.0f (see #4053, #3366)`, `imgui_draw.cpp:817`). Function-local `static const` lookup ARRAYS are permitted when immutable + hot (UTF-8 decoder masks, `imgui.cpp:2685-2688`) — consistent with I17's no-mutable-statics.
- **N24.** Subsystem-internal function grammar: only the public scope pair stays verb-first (`BeginTable`/`EndTable`); every other function is noun-first `<Subsystem><Noun><Verb>` including begin/end pairs (`TableBeginRow`/`TableEndRow`/`TableBeginCell`/`TableEndCell`); deferred variants insert `Queue` (`TableQueueSetColumnDisplayOrder` vs `TableSetColumnDisplayOrder`, the immediate form cross-referencing the queued one, `imgui_tables.cpp:730`); internal GC pairs may overload one name over sibling types (`TableGcCompactTransientBuffers(ImGuiTable*/ImGuiTableTempData*)`, `:4259/:4274`).


### 2.5 Variables

- **N13.** Globals: `GIm*` prefix (`GImGui`, `GImAllocatorAllocFunc`; `imgui.cpp:1507-1509`). Never `g_` Hungarian in core (backends killed their `g_` globals in 2021; §8.3).
- **N14.** Pointer parameter prefixes: `p_` = in/out pointer whose pointee is read+written (`bool* p_open`, `void* p_data`); `out_` = write-only (`bool* out_hovered`); `in_` = read-only input range (`in_text`, `in_text_end`) (`imgui_internal.h:4019`, `:438`).
- **N15.** NULL-able out-pointers mean "feature off": `p_open == NULL` = no close button (`imgui.h:432-433`).
- **N16.** Underscore-prefixed members/methods = internal, do-not-use (`_MainScale`, `_PathArcToN`; `imgui.h:2461`, `:3596`).
- **N17.** Conventional locals: `g` (context ref), `window`, `bd` (backend data), `vd` (viewport data), `bb` (bounding box), `draw_list`.

### 2.6 Macros

- **N18.** `IM_` = utility/value macros (`IM_ASSERT`, `IM_COL32`, `IM_ALLOC`, `IM_TRUNC`); `IMGUI_` = library config/versioning/API markers (`IMGUI_API`, `IMGUI_VERSION`, `IMGUI_DISABLE_*`, `IMGUI_DEBUG_LOG_*`); `IMGUI_IMPL_` = backend config (`IMGUI_IMPL_OPENGL_ES2`).
- **N19.** Headers use `#pragma once`, not include guards (`imgui.h:63`). Function-like macros wrap bodies in `do { ... } while (0)` (`imgui_internal.h:252-260`).

### 2.7 Files

- **N20.** Core: `imgui*.{h,cpp}` — EXCEPT the config header, which uses the contracted library prefix with no underscore (`imconfig.h`, not `imgui_config.h`; imguiapp analog: `imappconfig.h`); backends: `imgui_impl_<sdk>.{h,cpp}` (+ sidecars `_loader.h`, `_shaders.h`); vendored stb: `imstb_*` prefix with `[DEAR IMGUI]` modification markers and grep anchor (`imstb_truetype.h:1-4`).

---

## 3. Formatting & Spacing

Machine-enforced baseline (`.editorconfig`): 4-space indent, no tabs, final newline, trim trailing whitespace (`imstb_*` exempt at 3 spaces; Makefile tabs). **No `.clang-format`, no `.clang-tidy`** — formatting is manual convention.

### 3.1 Indentation & braces

- **F1.** 4 spaces, never tabs. Preprocessor directives always at column 0, even nested or inside bodies (`imgui.cpp:2388-2392`).
- **F2.** Allman braces for functions, structs, enums, namespaces, and multi-line control blocks; `else` on its own line.
- **F3.** Single-statement `if`/`for`/`while`: no braces, body on next line — OR the whole guard on one line (`if (count < 1) return;`), especially in aligned groups (`imgui.cpp:2815-2818`). Braces ARE added when a comment sits inside the block (`imgui_impl_win32.cpp:289-293`).
- **F4.** Short loop bodies may collapse to one line with inline braces and inner spaces: `while (...) { str1++; str2++; }` (`imgui.cpp:2239`).
- **F5.** `switch`: `case` labels at the same column as the braces (not indented an extra level); trivial cases align statement + `break;` in columns (`imgui_impl_win32.cpp:300-310`).
- **F6.** Empty bodies: `{ }` (with space) for inline ctors; `{}` for empty function stubs.

### 3.2 Column alignment (headers are tabular documents)

- **F7.** Struct members in imgui.h: type col 5, name col ~17, trailing `//` comment col ~45 — columns re-chosen **per sub-group** to fit the widest type (`imgui.h:2377-2458`). imgui_internal.h uses wider columns (~33).
- **F8.** Enum values: `=` aligned (~col 45), trailing comment aligned (~col 57). Enums carrying defaults use a **double comment column**: `// false    // description` (`imgui.h:1289`).
- **F9.** Function declarations: `IMGUI_API` col 5, return type col ~15, name col ~29; trailing doc comments aligned per functional group (`imgui.h:399-423`).
- **F10.** Overflow rule: when a name outgrows its column, drop the gap on that line (comment/`=` abuts the name) rather than re-widening the whole block (`imgui.h:2420`, `:1231`). Intentional and pervasive.
- **F11.** Typedef triple-column: `typedef int Name;` + `// -> enum Name_` pointer comment (~col 40) + `// Enum:`/`// Flags:` description (~col 73) (`imgui.h:229-273`). The `-> enum` comments exist so IDE symbol-follow works (`imgui.h:221-224`).
- **F12.** Backend **public** function *definitions* pad the name column (`void    ImGui_ImplWin32_Shutdown()`); core .cpp definitions do NOT align names (single space after return type). Header declarations align; cpp definitions don't (backends' public API excepted).

### 3.3 Pointers, casts, operators

- **F13.** Left-attached pointers/references: `char* p`, `const ImVec2& size`. (656 left vs 13 right in the corpus — all 13 in vendored stb code.) West const.
- **F14.** C-style casts, glued: `(float)x`, `(void)unused_var;`. Never `static_cast`. `-Wold-style-cast` is deliberately suppressed: "yes, they are more terse" (`imgui_widgets.cpp:71`).
- **F15.** Space after control keywords (`if (`), none inside parens, spaces around binary operators, comma+space. Ternaries spaced and chainable (§7.11).

### 3.4 Line length & wrapping

- **F16.** No column limit. Long lines (200+ chars) are preferred over wrapping — declarations with trailing doc comments, long conditionals with inline explanation. Multi-line parameter lists essentially never occur; when call arguments do wrap, they column-align (`imgui_impl_opengl3.cpp:412-414`).

### 3.5 Comments

- **F17.** `//` line comments; `/* */` only for the top-of-file index banner and rare `/*param*/` tags.
- **F18.** Prose comments: space after `//`, sentence-capitalized. **Commented-out code: `//` glued, no space** (`//CodeComment();`), indentation preserved inside the comment (`imgui_widgets.cpp:821-822`).
- **F19.** Section separator: full-width dashed rule + `// [SECTION] NAME` + rule, one blank line each side. No boxed/decorated sub-separators — sub-grouping via plain `// Group name` labels.
- **F20.** Tag census (normative): `FIXME` and `FIXME-<AREA>` are the house markers (`FIXME-OPT`, `FIXME-NAV`, `FIXME-TABLE`, `FIXME-DPI`...; 258 bare + hundreds qualified). `NB:` for emphasis. **`TODO` is rare (9) and `HACK`/`BUG` are unused (0)** — use `FIXME-<AREA>`, not `TODO`/`HACK`.
- **F21.** `#endif` echoes its condition: `#endif // #ifndef IMGUI_DISABLE`.
- **F22.** `// About X:` blocks introduce usage instructions as `//` bullet lists (`imgui.h:1258`).

### 3.6 Blank lines

- **F23.** Exactly one blank line between functions; single blanks separate logical phases within functions (often introduced by a `// Phase` label like `// Render`); double blanks only at rare major boundaries.

### 3.7 Micro-lexicon (literals, switches, loops, buffers)

- **F24.** Numeric literals: UPPERCASE hex (151 vs 20 lowercase, the lowercase confined to vendored decoder/stb regions); format strings use `%08X/%04X/%02X` (174 vs 4 lowercase); floats always carry both digits and the suffix — `N.Nf` (2,314 sites; bare `N.f` is a 13-line wart cluster); unsuffixed decimals ONLY in genuine `double` contexts (the FLOATTYPE=double slider path, `imgui_widgets.cpp:3093`); halving is `* 0.5f`, not `/ 2`; `(size_t)` casts at qsort/alloc seams (47 sites) and the `(size_t)-1` sentinel.
- **F25.** switch discipline: first-party cases NEVER fall through — zero `[[fallthrough]]`/`IM_FALLTHROUGH`/fall-through comments in all five TUs (the only fallthrough suppressions fence vendored stb); no bare `default:` — it merges onto a named case in one line (`case ImGuiInputFlags_RepeatRateDefault: default:`, `imgui.cpp:10200`); exhaustive enum-to-string switches fall OUT of the switch to `IM_ASSERT(0); return "Unknown";` (`imgui.cpp:3911-3913`).
- **F26.** Loops: `for (;;)` is the only infinite-loop form in library code (3 sites; `while (true)` appears solely inside teaching comments); default index name is `n` (58 `for (int n = 0` vs 34 `for (int i = 0`). Range-based `for` is fully adopted (~174 uses) ALWAYS with the explicit element type (`for (ImGuiViewportP* viewport : g.Viewports)`) — never `auto` (I29).
- **F27.** Stack buffers are purpose-keyed: `[5]` UTF-8 codepoint encode, `[16]` short ids, `[20]` `##`-composed window names, `[32]` dominant (labels + format-sanitize, ~35 sites), `[64]` formatted values, `[128]` long labels/paths, `[256]` titles/user input; formatted via `ImFormatString(buf, IM_COUNTOF(buf), ...)`. Off-census sizes carry a reason (console `[1024]` vsnprintf).
- **F28.** memset/memcpy self-size from the expression — `sizeof(X)` / `sizeof(*ptr)` (~20 sites), never the type name (1 live `sizeof(ImDrawChannel)` outlier; the adjacent line already prefers `sizeof(_Channels[i])`, `imgui_draw.cpp:2148`).


---

## 4. File & Header Architecture

### 4.1 Universal file skeleton

- **A1.** Every file: `// dear imgui, vX.Y.Z WIP` + parenthetical role line → Help/resource links block (dotted-leader aligned table, `imgui.cpp:9-23`) → `Index of this file:` comment listing every `[SECTION]` in order → (`#pragma once` for headers) → `#ifndef IMGUI_DISABLE` wrapping the entire body → warning-pragma push block → sections → pragma pop → `#endif // #ifndef IMGUI_DISABLE`.
- **A2.** `[SECTION]` markers recur verbatim in the body matching the index 1:1; the instruction "search for [SECTION] in the code to find them" is embedded (`imgui.cpp:63`).
- **A3.** Warning-pragma blocks, per compiler, order MSVC → clang → gcc, each disabled warning with a trailing reason comment; clang block guards `-Wunknown-warning-option` via `__has_warning` (`imgui_tables.cpp:207-247`). Push/pop is BY SURFACE, not universal: HEADERS push at top and pop at tail in reverse compiler order (`imgui.h:135/4518-4524`, `imgui_internal.h:78/4364-4370`) so suppressions never leak into user TUs; implementation TUs disable with NO push and NO pop — suppression is TU-lifetime (zero pops in imgui.cpp/imgui_widgets.cpp/imgui_tables.cpp/imgui_demo.cpp); vendored-include fences push/pop LOCALLY around the stb block only (`imgui_draw.cpp:167-175`).

### 4.2 Layering

- **A4.** `imgui.h` = stable public API. `imgui_internal.h` = "No guarantee of forward compatibility here!" (`imgui_internal.h:3529`). Both reopen the same `namespace ImGui`. Internal APIs graduate: candidates flagged in-comment ("aimed to be public", `[EXPERIMENTAL]`).
- **A5.** Extension mechanism: users add functions into `namespace ImGui` from their own files; "Please don't modify imgui source files!" (`imgui.h:390`). **This is imguiapp's contract: extend via own files in the namespace, never patch the submodule.**
- **A6.** `IMGUI_API` on every exported function (core and internal alike); `IMGUI_IMPL_API` for backends; both default to empty, overridable for DLL scenarios (`imgui.h:84-92`). Trivial internal accessors are `inline` in-header.
- **A7.** imgui.h section order is by dependency and audience: config include → header mess (macros/attributes) → forward decls + scalar types → end-user API → flags/enums → Style/IO → misc/helpers → draw/font/viewport/platform → **obsolete last**.

### 4.3 Dependencies

- **A8.** imgui.h includes exactly four C headers (`float.h`, `stdarg.h`, `stddef.h`, `string.h`), each with a trailing comment naming the symbols it supplies (`imgui.h:79-82`). Every `#include` everywhere carries such a comment. No STL, ever.
- **A9.** .cpp include order: (own config guard) `imgui.h` → `imgui_internal.h` → C stdlib with symbol comments → conditional extras. Each core .cpp self-defines `IMGUI_DEFINE_MATH_OPERATORS` before including headers (`imgui_widgets.cpp:39-41`); a hard `#error` enforces define-before-include (`imgui_internal.h:112-116`).

### 4.4 Configuration

- **A10.** All compile-time config flows through `imconfig.h` (editable) or `IMGUI_USER_CONFIG` (redirect), included before everything (`imgui.h:67-70`). Config must be "defined consistently _everywhere_" — enforced at runtime by `IMGUI_CHECKVERSION()` → `DebugCheckVersionAndDataLayout` asserting struct `sizeof`s (`imgui.h:106`, `imgui.cpp:11524-11535`).
- **A11.** Configurables are commented-out templates with one-line rationales: assert handler, API export, feature opt-outs (`IMGUI_DISABLE_*`), linkage reducers (with KB savings), type overrides (`ImDrawIdx`, `IM_VEC2_CLASS_EXTRA`), debug hooks. Compile-time = opt-out of features/linkage; runtime options go through `ImGuiIO`.

### 4.5 Forward declarations

- **A12.** One large forward-decl block after the header mess, grouped by layer, column-aligned comments: scalar typedefs → draw/font-layer structs → GUI-layer structs → enums → flags → callback typedefs (`imgui.h:158-294`).

### 4.6 Backward compatibility machinery

- **A13.** Version triple: `IMGUI_VERSION` string (with ` WIP` suffix in progress), `IMGUI_VERSION_NUM` as XYYZZ integer for `#if` gating, and `IMGUI_HAS_*` feature-presence macros (`imgui.h:30-37`).
- **A14.** Staged deprecation lifecycle: (1) live `inline` redirect wrapper tagged `// OBSOLETED in X.YY.Z (from Month Year)`, newest first, in the guarded Obsolete section; (2) commented-out tombstone ("so they are not reported in IDE", `imgui.h:4384`); (3) removal. Modern additions: old signature marked `= delete` when `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` is on (`imgui.cpp:413-414`); renamed `#define`s emit `#error` pointing to the new name (`imgui.h:4511-4513`).
- **A15.** Every breaking change logged in the `API BREAKING CHANGES` block in imgui.cpp: reverse-chronological `- YYYY/MM/DD (X.YY.Z) - description` with mandatory migration hints / before-after signatures (`imgui.cpp:391-1138`).

### 4.7 .cpp organization

- **A16.** One giant core file + topical satellites: imgui.cpp (context/engine), imgui_widgets.cpp (one `[SECTION]` per widget family), imgui_draw.cpp (drawlist/fonts/stb), imgui_tables.cpp (tables+columns). Subsystems with subtle contracts open with a prose `[SECTION] Commentary` describing the whole flow before any code (`imgui_tables.cpp:32-187`).
- **A17.** Static forward decls in a `[SECTION] Forward Declarations` block near the top of each .cpp.
- **A18.** Compile-time checks: `IM_STATIC_ASSERT` locks enum/array parallelism (`IM_COUNTOF(GStyleVarsInfo) == ImGuiStyleVar_COUNT`, `imgui.cpp:3778`). `IM_COUNTOF` never on pointers (`imgui.h:100`).

### 4.8 Struct design

- **A19.** Plain structs, all-public PascalCase members, no getters/setters for data, no virtuals. Small value types get `constexpr` inline ctors (`ImVec2`, `imgui.h:303`); large config structs declare ctor in header, define in .cpp; POD-ish structs zero themselves: `memset((void*)this, 0, sizeof(*this));` with the `(void*)` cast to silence `-Wclass-memaccess` (`imgui.cpp:1663`).

- **A20.** Trivial one-liner methods inline in the struct with `{ ... }` inner-spaced; real logic declared `IMGUI_API`, defined in .cpp.
- **A21.** Storage shrinking via bitfields (`unsigned int Field : 1`) and narrow index typedefs (N2).

### 4.9 Placement — where every symbol is declared and defined

- **A22.** The placement matrix. Every symbol kind has exactly ONE declaration home and ONE definition home; anything unlisted resolves via A23's closing rule. imguiapp analog follows "→" per row (Δ7: every imguiapp definition site is the matching `[SECTION]` region of the unity `imguiapp.cpp`).

| Kind | Declared | Defined |
|---|---|---|
| Public API functions (`ImGui::`, N9) | `imgui.h` end-user API `[SECTION]` → `imguiapp.h` | TU owning the subsystem: widgets → `imgui_widgets.cpp`; tables/columns → `imgui_tables.cpp`; drawlist/fonts → `imgui_draw.cpp`; demo showcase → `imgui_demo.cpp`; context/windows/nav/dock/settings/viewports/inputs/everything else → `imgui.cpp` → `imguiapp.cpp` region |
| Internal API functions (`ImGui::`) | `imgui_internal.h` internal-API `[SECTION]` → `imguiapp_internal.h` | same subsystem ownership map → `imguiapp.cpp` region |
| Foundation helper functions (bare `Im*`, N11) | `imgui_internal.h` `[SECTION] Generic helpers` under `// Helpers: <Family>`; one-liners/templates `static inline` in place → `ImApp*`: public-surface in `imguiapp.h` Helpers, type-identity in `imguiapp_reflect.h`, internal-only in `imguiapp_internal.h` | out-of-line in the TU owning the family's consumers: `ImHashData`/`ImQsort`/`ImFileOpen`/`ImTriangleContainsPoint`/`ImAlphaBlendColors` → `imgui.cpp`; `ImBezierCubicCalc` → `imgui_draw.cpp` → `imguiapp.cpp` |
| Public-currency `Im*` value types (appear in public signatures/members: `ImVec2/ImVec4`, `ImVector<>`, `ImColor`, `ImDrawList`, `ImFont`) | `imgui.h` (`ImVector`, `imgui.h:2301`) | trivial methods inline (A20); real logic in the owning TU (drawlist/fonts → `imgui_draw.cpp`) |
| Internal `Im*` helper types (`ImRect`, `ImPool<>`, `ImSpan<>`, `ImBitArray`, `ImChunkStream<>`) | `imgui_internal.h` under `// Helper: <Type>` → `ImAppTween/Timer/Spring/Pulse` in `imguiapp_internal.h` animation section | inline; heavy methods in the owning TU |
| Public context-layer types (`ImGuiIO`, `ImGuiStyle`, callback-data structs) | `imgui.h` structs `[SECTION]` → `ImGuiApp`, `ImGuiAppRecorder` in `imguiapp.h` | ctors + logic `imgui.cpp` → `imguiapp.cpp` |
| Internal context-layer types (`ImGuiContext`, `ImGuiWindow`, `ImGuiTable`) | `imgui_internal.h` → `ImGuiAppGraph`, `ImGuiAppEditorState` in `imguiapp_internal.h` | logic in the owning TU |
| Enums/flags (N4-N8) | the same header as their consumer type/API, directly beside it | n/a |
| Scalar + index typedefs (N2) | `ImS8..ImU64` → `imgui.h` basic types; domain index typedefs → `imgui_internal.h` beside their domain | n/a |
| Function-pointer typedefs (N3) | beside their first consumer, same header | n/a |
| Config/overridable macros (`IM_ASSERT`, `IM_VEC2_CLASS_EXTRA`) | override point `imconfig.h`, default in `imgui.h` → `imappconfig.h` / `imguiapp.h` | n/a |
| Value macros (`IM_COL32`, `IM_ALLOC/IM_NEW`, `IM_ARRAYSIZE`, `IM_FMTARGS`) | `imgui.h` | n/a |
| Internal macros (`IM_PI`, `IM_NEWLINE`, `IM_MSVC_RUNTIME_CHECKS_OFF`) | `imgui_internal.h` `[SECTION] Macros` → `imguiapp_internal.h` `[SECTION] Macros` | n/a |
| Process globals (`GIm*`, N13) | `extern IMGUI_API` in `imgui_internal.h` (`GImGui`, `:232`) → banned in imguiapp (Δ3; Δ6 singleton accessors instead) | `imgui.cpp` context section (`:1494`) |
| Backend symbols (§6) | public lifecycle API in `backends/imgui_impl_<sdk>.h` ONLY; Data struct + everything else TU-local in the `.cpp` → `backends/imguiapp_impl_*` | `imgui_impl_<sdk>.cpp` |
| Vendored code | `imstb_*.h` → `imguiapp_reflect.h` port; body exempt from all rules | n/a |

- **A23.** TU-local symbols — the rules for INSIDE every implementation file (`imgui.cpp`, `imgui_widgets.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_demo.cpp`, `backends/*.cpp`):
  - Never declared in any header; static forward decls live in the TU's `[SECTION] Forward Declarations` (A17).
  - **Types are ALWAYS tier-prefixed, even TU-local**: `ImGuiResizeGripDef`/`ImGuiDockRequest` (`imgui.cpp:7198/17816`), `ImGuiPlotArrayGetterData`/`ImGuiTabBarSection` (`imgui_widgets.cpp:9026/9744`). Only FUNCTIONS may drop the prefix when file-local (N12). → applayer TU-local types: `ImGuiApp*` when context/render-coupled, `ImApp*` only if N11-promotable.
  - Static data tables: `G`-prefixed PascalCase const arrays, locked by `IM_STATIC_ASSERT` beside them (A18 — present in all four core TUs).
  - `imgui_demo.cpp` is a deliberate REGISTER EXCEPTION: includes `imgui.h` + libc only — never `imgui_internal.h`, never the `Im*` math wrappers ("imgui_demo.cpp does _not_ use them to keep the code easy to copy", `imgui_internal.h:494`); demo-local symbols read as user code: `ShowExampleApp*` statics, `Example*` structs, plain `HelpMarker`. The Im grammar is suspended there and ONLY there. → imguiapp demo region keeps applayer grammar for framework chrome; sample controls showcase the user register.
  - Closing rule for anything unlisted: (1) visibility decides the header — end user → public header; framework/tools → internal header; one TU → that TU only, never a header. (2) The definition lives in the TU that owns the declaring subsystem — never a third place. (3) Growing the file set itself requires a spec amendment (N20 names the closed set).


### 4.10 Ordering — cardinal anchors, ordinal sequences, directions

- **A24.** Cardinal order, `imgui_internal.h`: Header mess -> Forward declarations -> Context pointer -> STB includes -> Macros -> Generic helpers -> ImDrawList/Style/Data-types/Widgets support -> per-subsystem support sections (Popup -> Inputs -> Clipper -> Navigation -> Typing-select -> Columns -> Box-select -> Multi-select -> Docking -> Viewport -> ...) -> the `namespace ImGui` internal-API declarations toward the tail (`imgui_internal.h:10-31` index; internal API from `:3529`). Complements A7 (imgui.h section order); A1 fixes every file's absolute top/bottom anchors (banner first; pragma pops + echoed `#endif` last). imguiapp analog: `imguiapp_internal.h` keeps its index in the same shape; deprecation scaffolding tail-anchors when it lands (audit S5).
- **A25.** Cardinal order, implementation files: `imgui.cpp` opens with the DOCUMENTATION block (MISSION STATEMENT -> CONTROLS GUIDE -> PROGRAMMER GUIDE -> API BREAKING CHANGES -> FAQ) BEFORE any code; code runs INCLUDES -> FORWARD DECLARATIONS -> CONTEXT AND MEMORY ALLOCATORS -> USER FACING STRUCTURES -> MISC HELPERS/UTILITIES (five blocks in Generic-helpers family order) -> subsystem sections (`imgui.cpp:63-94` index). Satellite TUs open includes/pragmas -> `[SECTION] Forward Declarations` -> subsystem sections. imguiapp analog: `imguiapp.cpp` top index + Δ7 per-region indexes, same shape.
- **A26.** Ordinal rule — definitions follow declarations: a TU's `[SECTION]` sequence mirrors the order of the API groups it implements in its header. `imgui_widgets.cpp` sections track imgui.h's `// Widgets:` group order verbatim (Text -> Main -> Combo -> Drag -> Slider -> Input -> Color -> Tree -> Selectable -> ListBox -> Plot -> Menu -> Tab; `imgui_widgets.cpp:8-31` vs `imgui.h:623-802`); support sections insert directly before their first consumer (Data Type helpers before Drag; Typing/Box/Multi-select support beside their widgets). WITHIN a section, definitions follow the header's declaration order. Audit mechanically: map header declaration ranks onto definition positions — the rank sequence must be monotonic ASCENDING modulo the stated insertions (the check that caught imguiapp's S1 drift).
- **A27.** Ordinal rule — additions: a new declaration joins its functional group, never the file tail. Pair members sit adjacent (`End` beside `Begin`, the `V` twin beside its variadic, the `Ex` engine beside its public wrapper, `Pop` beside `Push`; `imgui.h:436-440`, `:626-627`); new members append at their group's END; new groups slot into the audience order of A7/A24. The ONLY sanctioned tail-append is the guarded Obsolete section (A14). Definitions then land per A26 in the owning TU.
- **A28.** Sequence directions — every ordered list carries an explicit sort key AND direction; the normative set:
  - Chronological lists run DESCENDING (newest first): `API BREAKING CHANGES` (A15), backend `// CHANGELOG`s (B2), `OBSOLETED` entries (A14, "newest first"), CHANGELOG.txt releases (C3).
  - Enum values run ASCENDING: index enums count 0..`_COUNT`; flag enums allocate bits low-to-high (`1 << 0` upward, N7); `_BEGIN`/`_END` ranges are half-open with `_BEGIN < _END` (N6).
  - Forward declarations: ASCENDING case-sensitive alphabetical WITHIN each layer group (`imgui.h:176-190`: ImDraw* -> ImFont* -> ImTexture*); deprecated entries sink to the group tail regardless of alphabet (`ImColor` last, tagged `*OBSOLETE*`, `imgui.h:191`).
  - Includes: fixed dependency order, most fundamental first — config -> `imgui.h` -> `imgui_internal.h` -> libc -> SDK/extras (A9).
  - Version gates compare `IMGUI_VERSION_NUM` ASCENDING (A13); warning-pragma blocks list compilers MSVC -> clang -> gcc (A3); monitor lists put the primary FIRST (B10).
  - A sequence with no stated direction is a spec bug, not an author's choice: state the key + direction when introducing any new ordered list. imguiapp analog: identical directions; the S5 breaking-log adopts reverse-chronological on landing.

### 4.11 Debug-switch, suppression & annotation registers

- **A29.** Debug switches come in exactly three registers: (a) 0/1-VALUED macros — `#define IMGUI_DEBUG_NAV_SCORING 0` (`imgui.cpp:1345-1346`), value-form so they work in `#if` AND inline in boolean expressions (`(IMGUI_DEBUG_NAV_SCORING && g.NavWindow != NULL)`, `:14413`); never commented-out `//#define` in TUs (those live only in the imconfig/internal config blocks); (b) never-defined opt-in `#ifdef`s (`IMGUI_DEBUG_BOXSELECT`), the heavy ones carrying the all-caps reminder "DO NOT COMMENT OUT THIS MESSAGE" (`imgui.cpp:4564`); (c) commented-out `//IMGUI_DEBUG_LOG*` calls left in place as reactivatable probes (30 sites). Throwaway draw-debug stays in-tree as single-line `//GetForegroundDrawList()->AddRect(...)` comments, optionally tagged `// [DEBUG]`.
- **A30.** Static-analysis suppressions are self-documenting, three forms: PVS-Studio `//-VNNNN` trailing the offending line (28 lines; a repeated code gets one leading decoder comment stating what the code means, `imgui.cpp:11730`); `IM_MSVC_WARNING_SUPPRESS(NNNN);` on its OWN line immediately before, quoting the warning text in its comment (`imgui.cpp:6417`, `imgui_draw.cpp:4527`); assert-as-suppression with the purpose stated ("only to silence a false-positive in XCode Static Analysis", `imgui.cpp:10723-10725`).
- **A31.** Embedded binary data records provenance + exact regeneration command: `// File: 'ProggyClean.ttf' (41208 bytes)` + `// Exported using binary_to_compressed_c.exe -u8 ...` (`imgui_draw.cpp:6379-6382`), twin `static const` size + sized array, ~160-char packed decimal rows, exposed only through a static accessor (`GetDefaultCompressedFontDataProggyClean(int* out_size)`), whole block behind its feature opt-outs.
- **A32.** In-body version annotations use the triple form "Until/Since X.YY.Z (Month Year, IMGUI_VERSION_NUM < NNNNN)" (`imgui.cpp:11537`; 14 sites) so readers can gate with `#if IMGUI_VERSION_NUM`; GitHub issue refs are parenthesized at EOL — `(#8800, #3421)` (141 sites).



---

## 5. Public API Design Grammar

### 5.1 Immediate-mode contract

- **G1.** Functions are called every frame; `bool` returns mean "interacted/changed this frame" (clicked, edited, open) (`imgui.h:641-642`, `:1052`). Never return handles.
- **G2.** `Begin*` returns visibility. **End-pairing is asymmetric and this exact asymmetry is canon:** `End()` and `EndChild()` MUST be called regardless of the Begin return; every other `EndXxx` MUST be called **only if** its `BeginXxx` returned true — and each such `EndXxx` declaration carries the inline comment `// only call EndXxx() if BeginXxx() returns true!` (`imgui.h:436-440`, `:671`, `:826`, `:914`...). New Begin/End pairs MUST follow the conditional-End rule (the unconditional ones are self-described legacy).
- **G3.** Popup visibility is retained internally, unlike regular Begin state (`imgui.h:850-851`).

### 5.2 Stacks

- **G4.** Every scoped mutation is a symmetric pair: `Begin/End`, `Push/Pop`, `TreePush/TreePop`, `BeginDisabled/EndDisabled`. Style stacks take `int count = 1` on Pop; overpop is a recoverable user error with a named assert (`"Calling PopStyleColor() too many times!"`, `imgui.cpp:3709`). Single-axis variants suffix the var name (`PushStyleVarX/Y`), not a new verb.

### 5.3 Labels and IDs

- **G5.** Vocabulary is fixed (`imgui.h:611-612`): `label` = displayed + used as ID; `str_id` = ID only, not displayed; `name` = window title; `desc_id` for special cases. `##suffix` hides the suffix from display; `###suffix` replaces the whole ID seed. FAQ pointers embedded at point of use ("Read the FAQ about why and how to use ID", `imgui.h:753`).
- **G6.** ID-only overloads (`ImGuiID id`) exist beside string forms to bypass hashing / enable nested-stack calls (`imgui.h:464`, `:869`).

### 5.4 Parameters

- **G7.** **All flags default to 0** — stated general policy (`imgui.cpp:425`) — typed as the family's `typedef int` alias, placed late in the signature.
- **G8.** Out-params: `p_` pointers, NULL = feature disabled (N14/N15).
- **G9.** Text: `const char* text, const char* text_end = NULL` range pairs everywhere. Variadic: `fmt, ...` + `IM_FMTARGS(n)` attribute + a `V` va_list twin with `IM_FMTLIST(n)` — no exceptions to the twin rule (`imgui.h:626-627`).
- **G10.** Size sentinel grammar (must hold for any size-taking API): `0.0f` = auto/default, `> 0` = explicit pixels, `< 0` (incl. `-FLT_MIN`) = right/bottom-align to remaining space (`imgui.h:452-455`, `:795-796`, `:550`).
- **G11.** Drag/Slider ordering fixed: `label, v, [v_speed,] v_min, v_max, format, flags`; format defaults `"%.3f"` / `"%d"`; `format = NULL` on Scalar variants means "use type default" (`imgui.h:688-717`).
- **G12.** Array arity is documentation: `float v[4]` == `float* v`, digits suffix tells the count (`imgui.h:678-679`).
- **G13.** `ImGuiCond cond = 0` on every conditioned setter; 0 == Always; never combined as bits (`imgui.h:2083-2093`).

### 5.5 State-prefix pattern

- **G14.** `SetNextXxx` = configure the upcoming object, one-shot, consumed by the next Begin/submit; preferred over post-hoc `SetXxx` which is annotated "(not recommended)" with a reason (`imgui.h:482-495`). Any new deferred-config API MUST use the `SetNext` prefix.

### 5.6 Getters

- **G15.** Context singletons return references (`GetIO()`, `GetStyle()`); nullable/object accessors return pointers (`GetDrawData()`, `GetCurrentContext()`); non-null pointer getters say so (`// This can never be NULL`, `imgui.h:1070`).
- **G16.** Metric helpers named `Get<Thing><Metric>` with the formula documented (`GetFrameHeight() // ~ FontSize + style.FramePadding.y * 2`, `imgui.h:597-600`). Comments steer API choice ("THIS IS YOUR BEST FRIEND" vs "IT IS UNLIKELY YOU EVER NEED TO USE THIS", `imgui.h:575`, `:475`).

### 5.7 Overload vs suffix policy

- **G17.** Overload when the operation differs only by argument type (`PushID`, `PushStyleColor`, `GetColorU32`) or by presence of a semantic parameter within one family (`Selectable` ± `p_selected`). Suffix when behavior/arity/orientation differs (`DragFloat3`, `VSliderFloat`, `TextV`, `DragScalarN`).

### 5.8 Error handling

- **G18.** No exceptions, no error codes. `IM_ASSERT(expr && "Readable sentence with a hint?")` — the string rides in the expression so it shows in assert dialogs; messages often phrased as diagnostic questions ("Forgot to call Render() or EndFrame()...?", `imgui.cpp:11601`).
- **G19.** Recoverable user mistakes: `IM_ASSERT_USER_ERROR(_EXPR,_MSG)` (+ `_RET`/`_RETV`) — logs via `ErrorLog`, may recover, asserts in debug (`imgui_internal.h:2275-2278`). Error-recovery subsystem refuses 100%-silent recovery (`imgui.cpp:11612-11614`). Flag args validated against per-callsite allowed masks (`imgui.cpp:4986`).

### 5.9 Bindings friendliness

- **G20.** The API stays expressible in C: default args are convenience only; every varargs function has a `V` twin; flags are plain ints; constructs C can't express get a suggested helper documented in-comment (`imgui.h:367-368`); type-lax bindings get runtime asserts for invalid values (`imgui.cpp:419-421`).

### 5.10 Internal API conventions

- **G21.** Internal variants: `XxxEx` engines with rich parameters; functions take explicit `ImGuiWindow*`/context objects instead of implicit current-window (`imgui_internal.h:3554`, `:3642`).
- **G22.** Widget decomposition contract: **ID → Size → Add → Behavior → Render** through shared primitives `ItemSize`/`ItemAdd`/`CalcItemSize` and shared `*Behavior` functions (`ButtonBehavior`, `SliderBehavior`, `DragBehavior`, `TreeNodeBehavior`), with `*BehaviorT<>` templates backing type-erased scalars (§7.5).
- **G23.** Deferred-mutation grammar for retained subsystems: user actions never mutate live state mid-frame — they set `*Request` bools, `*NextFrame` shadow fields, or call `Queue*` variants, all drained at ONE defined point (`TableApplyQueuedRequests()` at next TableBegin, `imgui_tables.cpp:670-728`). Sentinels are typed: `-1` for indices, `FLT_MAX` for floats (`ResizedColumn != -1 && ResizedColumnNextWidth != FLT_MAX`); persistence dirtiness rides `Is*Dirty` flags. Request-vs-effective pairs stay separate and code picks explicitly ("using FreezeRowsCount, NOT FreezeRowsRequest", `imgui_tables.cpp:1158`, applied `:1864-1867`).
- **G24.** `format` parameter protocol: `NULL` means the engine default, resolved once at the engine entry (`if (format == NULL) format = DataTypeGetInfo(data_type)->PrintFmt;` — every scalar engine), then passed through VERBATIM ("so user can add prefix/suffix/decorations", `imgui_widgets.cpp:2802`); all format introspection goes through `ImParseFormat*` only. Entry-point flag sanitization is a named `inline` fixer whose body is a stanza of one-rule-one-comment `// Adjust flags: <reason>` lines (`TableFixFlags`, `imgui_tables.cpp:275-306`).


---

## 6. Backend Conventions

Everything in this section is testable against imguiapp's own `imguiapp_impl_*` backends.

### 6.1 File anatomy

- **B1.** Header comment block, fixed order, in BOTH .h and .cpp: line 1 `// dear imgui: <Platform|Renderer> Backend for XXX`; line 2 cross-reference to the complementary backend type; `// Implemented features:` checklist (`[X]` done, `[ ]` missing, `[!]`/`[x]` caveat, each item tagged `Platform:` or `Renderer:`); optional `// Missing features or Issues:`; then the verbatim "You can use unmodified imgui_impl_* files… / Learn about Dear ImGui:" footer with the four links (`imgui_impl_dx11.h:1-17`).
- **B2.** The .cpp (never the .h) carries `// CHANGELOG` with `(minor and older changes stripped away, please see git history for details)`, reverse-chronological `//  YYYY-MM-DD: Category: description. (#issue)` entries; breaking changes shouted `*BREAKING CHANGE*`; pending entries dated `20XX-XX-XX:` (`imgui_impl_dx11.cpp:19-21`).

### 6.2 API surface

- **B3.** Platform lifecycle: `ImGui_ImplXXX_Init[ForYYY]`, `_Shutdown`, `_NewFrame`, + one event-ingest entry (Win32 `WndProcHandler` copy-block, SDL `ProcessEvent`, GLFW install/chain callbacks). Renderer lifecycle: `_Init`, `_Shutdown`, `_NewFrame`, `_RenderDrawData`, `_CreateDeviceObjects`/`_DestroyDeviceObjects` (or `_InvalidateDeviceObjects`), + `_UpdateTexture(ImTextureData*)` in the new texture era.
- **B4.** All public functions prefixed `IMGUI_IMPL_API`; header includes imgui.h solely for that macro (`imgui_impl_dx11.h:20`).
- **B5.** Init parameter style: zero-clearable `ImGui_ImplXXX_InitInfo` struct when parameters are many/growing (Vulkan, WGPU — "allowing for easier further changes", `imgui_impl_wgpu.cpp:37`); positional args when 1-2 handles suffice (DX11, GL3). `RenderDrawData` takes the command buffer/encoder as trailing arg for command-based APIs only.

### 6.3 Backend data pattern

- **B6.** ALL mutable state in a single `struct ImGui_ImplXXX_Data` with a `memset((void*)this, 0, sizeof(*this));` ctor (non-zero defaults seeded after the memset); `IM_NEW` in Init, `IM_DELETE` in Shutdown; stored in `io.BackendPlatformUserData` / `io.BackendRendererUserData`. **No file-scope `g_` globals** — that pattern was retired 2021-06-29 (`imgui_impl_win32.cpp:65`).
- **B7.** Accessor: `static ImGui_ImplXXX_GetBackendData()` returning `ImGui::GetCurrentContext() ? (Data*)io.BackendXxxUserData : nullptr` (`imgui_impl_dx11.cpp:105-108`).
- **B8.** Init: `IMGUI_CHECKVERSION();` then `IM_ASSERT(io.BackendXxxUserData == nullptr && "Already initialized a <platform|renderer> backend!");`. Shutdown asserts `"No renderer backend to shutdown, or already shutdown?"`; NewFrame asserts `"Did you call ImGui_ImplXXX_Init()?"`. Shutdown is symmetric and flag-scrubbing: null the name, null the userdata, AND-out exactly the flags Init OR-ed in, clear handlers, `IM_DELETE(bd)` (`imgui_impl_dx11.cpp:685-689`).

### 6.4 io wiring

- **B9.** `io.BackendXxxName = "imgui_impl_xxx"` literal (version-annotated via snprintf into a Data member when useful). `io.BackendFlags |=` one flag per line with aligned `// We can ...` comments. New-texture renderers also set `platform_io.Renderer_TextureMaxWidth/Height` and the three `DrawCallback_*` handlers (`imgui_impl_dx11.cpp:639-647`).
- **B10.** Platform handlers assigned to `platform_io.Platform_*Fn`; monitors fill `platform_io.Monitors` with primary at front.

### 6.5 Events

- **B11.** Input flows exclusively through the event queue API (`io.AddKeyEvent`, `AddMousePosEvent`, `AddMouseSourceEvent`, `AddFocusEvent`...), never legacy arrays. Key translator named `ImGui_ImplXXX_Key[Event]ToImGuiKey`, a giant switch defaulting `ImGuiKey_None`, intentionally non-static "to allow third-party code to use that... (but undocumented)" (`imgui_impl_win32.cpp:546-548`). `ImGui_ImplXXX_UpdateKeyModifiers` submits all four mods together before key/button events.

### 6.6 Rendering

- **B12.** `RenderDrawData` skeleton is fixed: early-out on zero framebuffer → texture catch-up loop (verbatim comment "Catch up with texture updates. Most of the times, the list will have 1 element with an OK status, aka nothing to do.") → grow buffers with slack (+5000 verts / +10000 indices canon) → **exhaustive** host state backup → `SetupRenderState` (separate static fn, re-invoked on `ImDrawCallback_ResetRenderState` sentinel) → publish `Renderer_RenderState` → per-cmd loop (user callback | scissor-project + bind + draw; skip when `clip_max <= clip_min`) → null RenderState → restore backed-up state. Rationale stated: must run inside host engines that don't reset state (`imgui_impl_opengl3.cpp:457-458`).
- **B13.** `UpdateTexture` switches on `tex->Status`: `WantCreate` (assert `TexID == ImTextureID_Invalid`, format RGBA32, then `SetTexID` + `SetStatus(OK)`), `WantUpdates` (iterate `tex->Updates` rects), `WantDestroy` only when `UnusedFrames > 0` (`imgui_impl_opengl3.cpp:673-747`).
- **B14.** Shaders embedded as inline versioned source strings in the .cpp; no external files.

### 6.7 Multi-viewport

- **B15.** Separated by the fixed banner `// MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT` (+ "_advanced_ and _optional_" caveat lines). Per-viewport `ImGui_ImplXXX_ViewportData` in `viewport->PlatformUserData`/`RendererUserData`, dtor asserting handles freed. Handlers registered as `ImGui_ImplXXX_CreateWindow` etc.; setup/teardown named `InitMultiViewportSupport`/`ShutdownMultiViewportSupport` (current canon; `InitPlatformInterface` is the obsolete name).

### 6.8 Portability

- **B16.** `.cpp` starts `#include "imgui.h"` then `#ifndef IMGUI_DISABLE`; whole body guarded; closing `#endif // #ifndef IMGUI_DISABLE`. Config macros backend-scoped (`IMGUI_IMPL_XXX_*`), documented at top, set in imconfig.h ("make those changes in your imconfig.h file, not here!", `imgui_impl_vulkan.h:33-52`). SDK version differences gated by the SDK's own version macros (`SDL_VERSION_ATLEAST`, `GLFW_VERSION_COMBINED`). No third-party deps beyond the target SDK (GL3 embeds its own loader; Win32 LoadLibrary's optional APIs rather than linking).
- **B17.** Statics keep the full `ImGui_ImplXXX_` prefix even though static. Uses `IM_NEW/IM_DELETE/IM_ASSERT/IM_COUNTOF/ImVector`; `nullptr` (backends switched to C++11 nullptr in 2022 — note this differs from core, §12).
- **B18.** The null backend documents the minimum viable contract: platform = BackendFlags + DisplaySize + DeltaTime; renderer = `RendererHasVtxOffset|RendererHasTextures` + texture status resolution loop. Data-struct pattern is convention, not hard requirement, at this minimal scale (`imgui_impl_null.cpp`).

---

## 7. Implementation Idioms

### 7.1 Context access

- **I1.** `ImGuiContext& g = *GImGui;` — a reference named `g`, never a pointer, never copied (651 occurrences). The canonical widget preamble, in this order: `window = GetCurrentWindow(); if (window->SkipItems) return false; ImGuiContext& g = *GImGui;` (`imgui_widgets.cpp:788-792`). Helpers without a SkipItems guard fetch `g` first.
- **I2.** `GetCurrentWindow()` (sets WriteAccessed) for item-submitting code; `GetCurrentWindowRead()` or `g.CurrentWindow` for read-only paths (`imgui_internal.h:3542-3543`).
- **I3.** `ImGuiContext* ctx` parameter form is reserved for registered callbacks/handlers, which immediately re-seat: `ImGuiContext& g = *ctx;` (`imgui_tables.cpp:4118-4120`).
- **I4.** Draw-layer code (`imgui_draw.cpp`) is intentionally context-light: takes data via `_Data`/parameters, not `g`.

### 7.2 Memory

- **I5.** `IM_ALLOC/IM_FREE/IM_NEW/IM_DELETE/IM_PLACEMENT_NEW` only; never raw new/malloc; allocators are user-swappable; placement-new via `ImNewWrapper` to avoid `<new>` (`imgui.h:2274-2286`).
- **I6.** `ImVector<>` is the only sequence container. Raw-data semantics: memcpy push_back, dtor frees but never destructs; `clear_delete()`/`clear_destruct()` "never called automatically! always explicit". Its std-like lowercase API is a documented one-off exception to PascalCase (`imgui.h:2293`).
- **I7.** Buffer recycling: "clear() frees memory, resize(0) keeps the allocated buffer. We use resize(0) a lot to intentionally recycle allocated buffers across frames and amortize our costs" (`imgui.h:2294`). Shared scratch = `g.TempBuffer`.
- **I8.** Specialized containers by access pattern: `ImSpan` (view), `ImSpanAllocator` (one-arena multi-alloc), `ImStableVector` (pointer-stable), `ImPool` (ID-keyed dense; the ONLY ctor/dtor-honoring container), `ImChunkStream` (variable records, few allocs) (`imgui_internal.h:694-816`).

### 7.3 Strings

- **I9.** `ImStr*` family over libc (`ImStricmp`, `ImStrncpy` "always zero terminate (strncpy doesn't)", `ImStrdup`, `ImStristr`); `ImStrlen`/`ImMemchr` are swap-point macros. Formatting through `ImFormatString[V]` and `ImFormatStringToTempBuffer`.
- **I10.** Text is `(text, text_end)` range pairs; lazy strlen fallback is flagged `// FIXME-OPT`. Display-end via `FindRenderedTextEnd(label)` (strips `##`). UTF-8 interchange; conversions via `ImText*` helpers with return-semantics comments.

### 7.4 Math

- **I11.** `ImMin/ImMax/ImClamp/ImLerp/ImSaturate/ImSwap` templates, never `<algorithm>` (justification comment at `imgui_internal.h:521-522` — templates used "exceptionally"). Pixel alignment via `ImTrunc`/`ImFloor`/`IM_TRUNC`, never floorf. `ImRect` is the rectangle currency (`const ImRect bb(pos, pos + size);`). IDs: `window->GetID(label)` → `ImHashStr(str, len, seed = IDStack.back())`; derived-ID comments annotate the hash formula.

### 7.5 The canonical widget skeleton

- **I12.** Every widget follows the pipeline (from `ButtonEx`, `imgui_widgets.cpp:786-826`; identical in Checkbox/SliderScalar/DragScalar):
  1. `window = GetCurrentWindow()`; `SkipItems` early-out
  2. `g`, `style` refs
  3. `const ImGuiID id = window->GetID(label);`
  4. `label_end = FindRenderedTextEnd(label)`; `CalcTextSize`
  5. pos from `window->DC.CursorPos`; size via `CalcItemSize`
  6. `const ImRect bb(pos, pos + size);`
  7. `ItemSize(size, style.FramePadding.y);` then `if (!ItemAdd(bb, id)) return false;` — always paired, always this order
  8. interaction via shared `*Behavior` (`ButtonBehavior(bb, id, &hovered, &held, flags)`)
  9. `// Render` phase: `GetColorU32(state ? _Active : hovered ? _Hovered : _Base)` chained ternary; `RenderNavCursor`; `RenderFrame`; `RenderTextClipped`
  10. `MarkItemEdited(id)` on value change
  11. `IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);`
  12. `return pressed;`
- **I13.** Public thin wrapper over internal `Ex` engine; variants back-up/restore style fields around the engine call (`SmallButton`, `imgui_widgets.cpp:834-842`).

### 7.6 Rendering

- **I14.** Prefer `Render*` helpers (`RenderFrame`, `RenderText[Clipped|Ellipsis]`, `RenderArrow`, `RenderCheckMark`, `RenderNavCursor`) over raw drawlist calls in widget code.
- **I15.** Colors ONLY via `GetColorU32(ImGuiCol_...)` (folds in `style.Alpha` centrally); rounding/borders from `style.*`, never hard-coded; raw `IM_COL32` literals only in debug overlays.
- **I16.** ImDrawList internals: Path API for shapes, `PrimReserve/PrimRect` fast path for sharp rects, zero-alpha early-out `if ((col & IM_COL32_A_MASK) == 0) return;` before any geometry (`imgui_draw.cpp:1523`). Draw-layer vertex post-processors are `namespace ImGui` internal API declared under ImDrawList support (`ShadeVertsLinearColorGradientKeepAlpha/LinearUV/TransformPos`, `imgui_internal.h:4071-4073` — N9/G21, not a bare-Im tier); geometry scratch structs are A23 TU-local prefixed types (`ImTriangulator/ImTriangulatorNode*`, `imgui_draw.cpp:1844-1864`).

### 7.7 State & lifecycle

- **I17.** ALL mutable state lives in `ImGuiContext` members — zero mutable function-local statics in core (verified by grep). File-scope statics are `static const` data tables and tunables only (named like `DRAGDROP_HOLD_TO_OPEN_TIMER`, `imgui_widgets.cpp:104`).
- **I18.** Frame ordering enforced by asserts with actionable question-messages; per-frame buffers recycled in NewFrame; deferred actions via context flags consumed later (`MarkItemEdited` → `IsItemDeactivatedAfterEdit`); mutation of callback lists deferred to frame start.
- **I19.** Per-widget persistent state in `ImGuiStorage` keyed by ID (`window->DC.StateStorage`, `imgui_widgets.cpp:6793-6804`).

### 7.8 Performance

- **I20.** Early-outs everywhere: SkipItems, `!ItemAdd` clip, zero alpha, zero-length text. `ImGuiListClipper` for long lists (pass known line height as micro-opt). Perf debts tagged `// FIXME-OPT`. `static inline` for tiny hot helpers. `IM_MSVC_RUNTIME_CHECKS_OFF/RESTORE` bracket hot math/data regions (`imgui.h:124-131`).

### 7.9 Debug

- **I21.** `IM_ASSERT(expr && "message")`; `IM_ASSERT_USER_ERROR` for recoverables; `IM_ASSERT_PARANOID` for expensive checks. Category debug log `IMGUI_DEBUG_LOG_<CATEGORY>(...)` (15 categories), IDs formatted `0x%08X`.
- **I22.** Every substantial data structure gets a `DebugNodeXxx(...)` Metrics function, guarded by `#ifndef IMGUI_DISABLE_DEBUG_TOOLS` with empty stubs in the `#else`. **New imguiapp subsystems should ship DebugNode-style introspection.**

### 7.10 Statics

- **I23.** File-local helpers `static`, forward-declared in the Forward Declarations section; whole static subsystem APIs declared as a cluster (docking, `imgui.cpp:17877-17908`); `static const` when the constant needs an address (comment states why, `imgui_widgets.cpp:107`).

### 7.11 Control flow

- **I24.** Guard clauses / multiple returns over single-return. Chained ternaries for state→value maps on one line. Nested single-statement `if`s when each condition is conceptually a separate gate (`imgui.cpp:12055-12058`). `for (;;)` for unbounded loops. `goto` effectively banned (one experimental use in the whole corpus).
- **I25.** **Core uses `NULL` and C-style casts** — `-Wold-style-cast`/`-Wzero-as-null-pointer-constant` silenced with "yes, they are more terse" (`imgui_widgets.cpp:71-77`). Backends use `nullptr` (post-2022 switch). Pick the convention of the layer being matched.

### 7.12 Settings / persistence anatomy

- **I26.** One `ImGuiSettingsHandler` per persisted type (`imgui_internal.h:2219-2233`): `TypeName` (chars `[` `]` disallowed — they delimit the format), `TypeHash = ImHashStr(TypeName)`, six function-pointer slots in `*Fn` naming (N3): `ClearAllFn`/`ReadInitFn`/`ReadOpenFn`/`ReadLineFn`/`ApplyAllFn`/`WriteAllFn`, plus `UserData`, zero-memset ctor (P8). Registered via `AddSettingsHandler` (`imgui_internal.h:3627`); read hooks fire in REGISTRATION order (stated on `ReadInitFn`/`ApplyAllFn`). The .ini text format is fixed: `[TypeName][EntryName]` section headers (`ReadOpenFn` receives the entry name), one `Key=Value` per line through `ReadLineFn`, `WriteAllFn` appends whole sections into an `ImGuiTextBuffer`. imguiapp analog + caveat: a handler registered after the host loaded its .ini never sees its sections — imguiapp's Composer uses a sidecar file for exactly this reason (documented at the workspace-layout persistence site); pick handler vs sidecar by registration timing.

### 7.13 Platform-dependent helpers

- **I27.** OS-default implementations live in ONE tail section of imgui.cpp (`[SECTION] PLATFORM DEPENDENT HELPERS` — "Default clipboard handlers / Default shell function handlers / Default IME handlers"): defaults that fill the `Platform_*Fn` slots when the backend supplies nothing. The pattern: two-level opt-out guards, coarse then fine (`#if defined(_WIN32) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS) && !defined(IMGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS)`); OS libraries auto-linked via `#pragma comment(lib, "user32")` under `_MSC_VER` (no build system — P3); non-supported platforms compile inert fallbacks. New OS glue joins this section behind the same two-level guard shape, never scattered through subsystem code.

### 7.14 Big-subsystem disciplines (tables as the reference)

- **I28.** `imgui_tables.cpp` is the reference implementation for subsystems at scale, demonstrating the two sanctioned heavy disciplines beyond the A16 Commentary it pioneered: (a) deferred out-of-order draw accumulation through `ImDrawListSplitter` channels — one channel per column, `SetCurrentChannel` at each submission point (25 sites), single merge pass at end; never hand-rolled draw reordering; (b) per-column state as dense parallel arrays carved from ONE arena allocation via `ImSpan`/`ImSpanAllocator` (the I8 containers' reference user). A new subsystem of tables' complexity adopts all three (Commentary, splitter channels for deferred draw, arena spans for per-element state) rather than inventing parallel machinery.

### 7.15 Language dialect (per-feature, not blanket)

- **I29.** The C++11 subset is picked per feature, measured corpus-wide: range-based `for` EMBRACED (~174 uses, always explicit element type); captureless lambdas ONLY as C-callback initializers assigned to plain function pointers (8 total, zero capturing, `imgui.cpp:11644`); `auto` BANNED (0 declarations in five TUs); `constexpr` effectively banned in TUs (0 in imgui_widgets.cpp; header value-ctors excepted per A19); no `[[fallthrough]]` (F25). Unused suppression is `IM_UNUSED()` (58 uses) not `(void)x` (3 stragglers); idioms: `IM_UNUSED(key); // Yet unused` for reserved params, `ImGuiContext& g = *GImGui; IM_UNUSED(g);` when `g` is consumed only under conditional compilation. Branch hints rationed: `IM_LIKELY` x3, all in the glyph-lookup hot path (`imgui_draw.cpp:5349+`); `IM_UNLIKELY` 0.

### 7.16 Instance-context and hook plumbing

- **I30.** `ImGuiIO`/instance methods take the context from the instance's `Ctx` backpointer, NEVER `GImGui` — stated in caps: "THIS FUNCTION AND OTHER ADD GRABS THE CONTEXT FROM OUR INSTANCE ... WHICH IS WHY GetKeyData() HAS AN EXPLICIT CONTEXT" (`imgui.cpp:1891`); body idiom `IM_ASSERT(Ctx != NULL); ImGuiContext& g = *Ctx;` (`:1896-1899`). New instance-scoped methods on context-owned structs follow it.
- **I31.** Context-hook pattern: `AddContextHook` returns monotonic `++g.HookIdNext`; removal only RETAGS (`hook.Type = ImGuiContextHookType_PendingRemoval_` — "Deferred removal, avoiding issue with changing vector while iterating it", `imgui.cpp:4693`), swept at NewFrame; dispatch is a flat scan with the size assumption stated (`:4704`). Lifecycle call sites bracket Pre/Post around NewFrame/EndFrame/Render + Shutdown.
- **I32.** Test-engine hooks: `IMGUI_TEST_ENGINE_ITEM_ADD/ITEM_INFO` macros gate per-call on `g.TestEngineHookItems`; the compiled-out forms are `((void)0)` / `((void)g)` so an in-scope `g` is required either way (`imgui_internal.h:4353-4358`). `IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | <status bits>)` is the LAST statement before `return` on EVERY post-ItemAdd exit path, clipped early-outs included (Checkbox carries it twice with the identical expression, `imgui_widgets.cpp:1270/:1322`); widgets append semantic status bits (`Checkable|Checked`, `Openable|Opened`, `Inputable`); ButtonBehavior holds the documented alternate registration spot for non-ItemAdd callers (`:571-575`).

### 7.17 Widget-authoring contracts (beyond the I12 skeleton)

- **I33.** Fixed locals: `bool hovered, held;` declared uninitialized immediately before the Behavior call, `pressed` ALWAYS the captured return, never an out-param (15/16 sites; capture taken even when unused). Return-local naming is per family: `pressed` (button-likes), `value_changed` (data editors — and `value_changed == true` OBLIGATES `MarkItemEdited(id)` at the mutation site, 16 sites), `is_open` (tree/Begin-likes); late mutation of the return var is tagged `// return value` (`imgui_widgets.cpp:1291`). Save/restore locals take the `backup_` PREFIX, `const` where possible, symmetric reverse-order restore (19 sites; `*_backup` suffix is the 3-site wart).
- **I34.** Nav contract every interactive widget honors: `RenderNavCursor(bb, id)` is the FIRST call of the render block (compact widgets add `ImGuiNavRenderCursorFlags_Compact`); activation predicates include `g.NavActivateId == id`; manual activation is the fixed triple `SetActiveID(id, window); SetFocusID(id, window); FocusWindow(window);` plus a `g.ActiveIdUsingNavDirMask` claim for consumed arrows; widgets consuming a nav move pair `NavClearPreferredPosForAxis` with `NavMoveRequestCancel()` (`imgui_widgets.cpp:2776-2779`, `:7065-7072`).
- **I35.** Flags hygiene: defaults injected only when a whole `*Mask_` group is empty (`if ((flags & ImGuiButtonFlags_PressedOnMask_) == 0) flags |= ...`, 18 sites); cross-domain flags accumulate in a SEPARATE local (`button_flags`), never into the widget's own param. The dominant 3-state color pick is `GetColorU32((held && hovered) ? Col_Active : hovered ? Col_Hovered : Col_Base)` with the first arm parenthesized (8 sites; the `held ?` minority form is a self-flagged FIXME, `imgui_widgets.cpp:537-539` — do not copy); drag/slider frames key on `g.ActiveId == id` instead (they stay active while dragged).
- **I36.** Optional-subsystem integration (multi-select is the template): gate on an item flag injected upstream, never on subsystem state (`g.LastItemData.ItemFlags & ImGuiItemFlags_IsMultiSelect` — rationale: "so widgets can also cheaply set this before calling ItemAdd(), so we are not tied to MultiSelect api", `imgui_widgets.cpp:8221`); a Header/Footer pair brackets the Behavior call and the Footer MAY rewrite the widget's return (`MultiSelectItemHeader/Footer(id, &selected, &pressed)`); clip early-outs get the identical escape-hatch comment at each site.
- **I37.** Text-log mirror: any widget rendering text/state mirrors it behind `if (g.LogEnabled)` with class-encoding decoration glyph pairs — buttons `LogSetNextTextDecoration("[", "]")`, value fields `"{", "}"`, checkbox `LogRenderedText(..., mixed ? "[~]" : *v ? "[x]" : "[ ]")` (12 guards / 16 calls).
- **I38.** Ownership/lifetime contracts are stated in comments AT the API boundary and escalate to CAPS at the traps: "The ImDrawList are NOT owned by ImDrawData" (`imgui_draw.cpp:2290`), "DUE TO LEGACY REASON AddFontFromMemoryTTF() TRANSFERS MEMORY OWNERSHIP BY DEFAULT" (`:3754-3756`), pointer-invalidation windows spelled out ("return pointer is valid until next call to AddRect()", `:4518`); capacity-vs-size buffer semantics documented where exploited (`_Channels`, `:2189`).

### 7.18 Draw-layer and heavy-pass specifics

- **I39.** Tessellation grammar: `num_segments == 0` = auto-tessellate (asserts the shared `CurveTessellationTol`); explicit counts are CLAMPED, never trusted (`ImClamp(num_segments, 3, IM_DRAWLIST_CIRCLE_AUTO_SEGMENT_MAX)`); derived caches rebuilt in the setter, not per use (`SetCircleTessellationMaxError`, `imgui_draw.cpp:418-431`); tunables live as `IM_DRAWLIST_*` macros with derivations documented, exactly one `#ifndef`-overridable. Hot-path macros state their hazard contract inline — l-value expectation, statement-only `(void)0` tail, and WHY no `do{}while(0)` ("even that translates to runtime with debug compilers", `:813-815`). `IM_COL32` literals are for texture DATA (raw texel writes, 50 sites); themed UI color always arrives as `ImU32` params or `GetColorU32` (3 sites) — the draw layer never picks theme colors itself.
- **I40.** Monolithic passes are numbered `// [Part N] <summary>` (13 parts in `TableUpdateLayout`, `imgui_tables.cpp:909-1363`); memory-heavy entry points carry an itemized allocation ledger comment ("the average total _allocation count_ for a table is: + 0 (...pooling...) + 1 (...)", `:640-648`); one-way per-frame latches are named `Is*Locked` and asserted at order-sensitive entries ("need to call before first row!", `:852`).
- **I41.** Pluggable-backend seam inside core: a C vtable struct (`ImFontLoader`) filled from file-static callbacks that use the BACKEND naming convention even in a core TU (`ImGui_ImplStbTrueType_FontSrcInit`, `imgui_draw.cpp:4702-4875`), NULL slots allowed, whole block behind its feature `#ifdef`; per-source override resolution is a deliberately unfactored verbatim ternary at all 7 sites (`src->FontLoader ? src->FontLoader : atlas->FontLoader`). The atlas function families partition by sub-phase (`ImFontAtlas{Build|Texture|Pack|Baked}<Verb>`, 57 defs) — N21's tier with phase stems; file-statics KEEP the full prefix.


---

## 8. Demo Conventions (`imgui_demo.cpp` deliberately differs)

The demo is documentation-you-copy-paste and inverts several core rules — stated verbatim at `imgui_demo.cpp:47-59`:

- **D1.** Never omit the `ImGui::` prefix (core omits it internally).
- **D2.** Function-local `static` variables ARE used, close to their usage — the "static sermon" explains this is a demo-only concession (`imgui_demo.cpp:33-46`).
- **D3.** Never use internal helpers/facilities — public API only.
- **D4.** Never use ImVec2/ImVec4 math operators (can't assume user enabled them).
- **D5.** `HelpMarker("...")` `(?)` tooltips as in-UI documentation; `IMGUI_DEMO_MARKER(section)` wires code locations to interactive browsers; huge `[SECTION]` index; "find a visible string and grep for it" navigation.
- **D6.** Self-contained; strippable by linker; "PLEASE DO NOT REMOVE THIS FILE" preamble.
- **D7.** Demo plumbing: `IMGUI_DEMO_MARKER(section)` is a `do{}while(0)` macro calling a context-routed `ImGui::DemoMarker("imgui_demo.cpp", __LINE__, section)` behind a version gate (189 sites, hierarchical slash-paths "Widgets/Basic/Button"); not-yet-public core functions are "sneakily" extern-declared under `#if IMGUI_VERSION_NUM >=` gates rather than including imgui_internal.h (`imgui_demo.cpp:301-306`). Structure is a two-tier static TOC (`DemoWindow<Area>` -> 27 `DemoWindowWidgets<Topic>` statics) mirrored 1:1 in the `[SECTION]` index; app toggles are `bool ShowApp*` fields of one `ImGuiDemoWindowData` struct mapping 1:1 to menu labels and a column-aligned dispatch block with ordering constraints as trailing comments.
- **D8.** Demo helper structs keep the CORE register (PascalCase members, aligned columns, `IM_FMTARGS` on printf-likes, `IM_NEW`/`MemAlloc` allocation) — the user-code register governs API usage, not struct shape. Init style is vintage-split: old structs ctor-assign + memset, new structs use C++11 default member initializers. Naming families: `ExampleApp*` (apps), `Example<Thing>` (data), `ExampleTree_*` (free helpers), function-local `struct <X>Funcs`. Libc shims are per-struct one-liner statics (`Stricmp/Strdup/Strtrim`); the sole `s_` static is documented as a qsort-no-user-data workaround.
- **D9.** Demo austerity extends beyond D1-D4: local `IM_MIN/IM_MAX/IM_CLAMP` redefinitions with the exception stated ("In other imgui sources we can use ... ImMin/ImMax but not in the demo"); `IMGUI_CDECL` locally re-derived rather than pulled from internal.h; raw `qsort` (never `ImQsort`); NO raw UTF-8 in source ("WE ARE *NOT* INCLUDING RAW UTF-8 CHARACTERS IN THIS SOURCE FILE" — hex escapes instead); dead-but-instructive snippets kept as comments (`//static ImGuiOnceUponAFrame once;`); entry ritual = the file's only hand-holding context assert + `IMGUI_CHECKVERSION()`; clang deprecation warnings disabled specifically so demo code stays copy-pasteable.

**Implication for imguiapp:** demo/example code (imguix_demo.cpp etc.) should follow D-rules; library code must follow §7 — do not blend the registers. §13 specifies the demo TU in authoring depth.

---

## 9. Documentation & Process Conventions

- **C1.** Comment voice: maintainer "we"; imperative instructions to the reader; parenthetical asides; emphasis via CAPS ("is NOT recommended") and `_underscores_`; cross-refs as "See #1234", "See FAQ"; version annotations "Obsoleted in 1.90.9", "Before 1.90 (November 2023)".
- **C2.** Right-aligned column-aligned trailing `//` comments on declarations are the primary documentation surface (F7-F9).
- **C3.** CHANGELOG.txt: version headers with release dates, two-bucket grouping (`Breaking Changes:` / `Other Changes:`), nested subsystem bullets (`Windows:`, `Tables:`, `Backends:`), attribution `(#NNNN)` `[@user]`, symbols in backticks, commits folded into topical entries (`CHANGELOG.txt:4-6`).
- **C4.** Redundancy-for-discoverability: the same guidance (how to update, where docs are, resource links) duplicated verbatim across imgui.cpp / imgui.h / CHANGELOG / FAQ, so readers find it wherever they land.
- **C5.** Version-gate features with `#if IMGUI_VERSION_NUM >= N`, expose capabilities via `IMGUI_HAS_*` macros.

---

## 10. Compliance Checklist for imguiapp

Fast audit questions derived from the rules above:

**Naming**
- [ ] Locals snake_case; functions/members PascalCase; no camelCase members (N-rules, §2.1)
- [ ] Flags: `typedef int XxxFlags` + `enum XxxFlags_`, `_None = 0`, `1 << n`, masks `_Mask_` (N4-N7)
- [ ] Internal extensions via `Private_` enums or `[Internal]` blocks, not ad hoc constants (N8)
- [ ] `p_`/`out_`/`in_` pointer prefixes; NULL out-pointer = feature off (N14-N15)
- [ ] Backend functions `ImGui_ImplXXX_*` prefixed, even statics (B17)

**Formatting**
- [ ] 4 spaces; Allman; left-attached pointers; C-style casts; no line-length wrapping mania (F1-F16)
- [ ] Header declarations column-aligned per group with trailing `//` docs; overflow drops the gap (F7-F11)
- [ ] `FIXME-<AREA>` not `TODO`/`HACK`; commented-out code `//glued();` (F18, F20)

**Architecture**
- [ ] File skeleton: banner → index → `[SECTION]` markers matching 1:1 → pragma push/pop (A1-A3)
- [ ] Public/internal header split with stated stability contract; extends `namespace ImGui` without patching imgui sources (A4-A5)
- [ ] Every `#include` has a symbol comment; dependency count minimal and justified (A8)
- [ ] Deprecations staged: inline redirect + `// OBSOLETED in vX (from Month Year)` + breaking-changes log (A13-A15)

**API grammar**
- [ ] bool returns = interacted-this-frame; conditional-End rule with the inline `// only call EndXxx()...` comment (G1-G2)
- [ ] Flags default 0, late in signature; sizes obey the 0 / >0 / <0 sentinel grammar (G7, G10)
- [ ] `SetNext*` for deferred config; `label`/`str_id`/`name` vocabulary; `V` twins for varargs (G5, G9, G14)
- [ ] Asserts phrased as readable hints; `IM_ASSERT_USER_ERROR` for recoverable misuse (G18-G19)

**Implementation**
- [ ] `ImGuiContext& g = *GImGui;` idiom; no mutable static locals; state in context/Storage (I1, I17, I19)
- [ ] Widget pipeline: GetID → CalcItemSize → ItemSize → ItemAdd → Behavior → Render helpers → TEST_ENGINE_ITEM_INFO (I12)
- [ ] ImVector/IM_ALLOC family, no STL, no RAII, resize(0) recycling (I5-I8)
- [ ] Colors via GetColorU32; metrics from style; ImTrunc pixel alignment (I11, I15)
- [ ] DebugNodeXxx introspection for new subsystems (I22)

**Backends (imguiapp_impl_*)**
- [ ] Header block anatomy + CHANGELOG in .cpp (B1-B2)
- [ ] Data struct + GetBackendData accessor + "Already initialized" assert + symmetric flag-scrubbing Shutdown (B6-B8)
- [ ] RenderDrawData skeleton incl. texture catch-up loop and full state backup/restore (B12)
- [ ] `#ifndef IMGUI_DISABLE` guard; SDK version gates; no extra deps (B16)

**OS/harness glue (imguiapp)**

OS/harness glue is NOT a licensed layer: libc/STL with an Im* equivalent must use it (`ImQsort`,
`ImFormatString`, `ImFile*` incl. `ImFilePrintf`); the rest sits behind client-overridable seams
(`IMGUIAPP_ERROR_PRINTF`/`IMGUIAPP_ABORT` macros; `ImGuiAppFileSystemFuncs` via
`SetAppFileSystemFuncs`, default = libc + std::filesystem confined to the default block,
strippable via `IMGUIAPP_DISABLE_DEFAULT_FILESYSTEM_FUNCS`). No direct std::filesystem callsites
may appear outside that default block.

Not licensed anywhere: STL types as public struct members visible to every consumer. The
recorder's encoder-thread state lives behind the opaque `ImGuiAppRecorderThread*` pimpl over the
`ImGuiAppThreadFuncs` seam (`SetAppThreadFuncs`, default = std::thread, strippable via
`IMGUIAPP_DISABLE_DEFAULT_THREAD_FUNCS`); `imguiapp.h` includes no threading or filesystem headers.

---

## 11. Notable Version-Specific Facts

- Docking + multi-viewport branch: `IMGUI_HAS_VIEWPORT`, `IMGUI_HAS_DOCK`, `ImGuiConfigFlags_DockingEnable` (`imgui.h:34-37`, `:1788`).
- New texture system: `ImGuiBackendFlags_RendererHasTextures`, `ImTextureData`/`ImTextureRef`/`ImTextureStatus_*`; legacy `CreateFontsTexture` removed from backends (2025-06-11 changelog entries).
- 1.92 font system: `PushFont(font, size)` two-arg form; one-arg form removed as forced migration (`imgui.h:522-523`).
- Current multi-viewport setup naming: `InitMultiViewportSupport` (not the older `InitPlatformInterface`).

---

## 12. Known Warts (imgui's own acknowledged deviations — do not imitate)

Deviations imgui itself flags; matching one of these in imguiapp is not "consistent with imgui", it is copying a documented mistake:

1. `Begin`/`BeginChild` unconditional-End asymmetry — "legacy... Will be fixed in a future update" (`imgui.h:438-440`).
2. `ImVector` lowercase std-like API — deliberate, documented one-off (`imgui.h:2293`).
3. `ImDrawData`/`ImDrawList` naming inconsistency preserved for back-compat (`imgui.h:3600`).
4. `Id` vs `ID` capitalization drift (`NoHoldingActiveId` vs `NoHoldingActiveID`).
5. `p_min/p_max` vs `pos_min/pos_max` for the same concept in adjacent functions (`imgui_internal.h:3969` vs `:3972`).
6. InputText is self-documented as non-exemplary — a monolith with FIXMEs at its own deviations ("This quite messy/tricky, should attempt to get rid of the child window", `imgui_widgets.cpp:5681`; "Mimic some of ItemAdd() logic", `:4746`); its ALL-CAPS local and pairwise flag-exclusion asserts are one-offs. Do not copy its shape (I12 is the skeleton).
7. The `held ?` minority color form (3 sites) vs the dominant `(held && hovered) ?` — flagged by imgui itself as unstandardized (`imgui_widgets.cpp:537-539`).
8. Bare `N.f` float literals — a 13-line wart cluster (HSV converter, dock preview) against 2,314 `N.Nf` sites (F24).
9. Three `(void)x` stragglers vs `IM_UNUSED()` (I29); one cross-enum zero-init (`ImGuiButtonFlags button_flags = ImGuiTreeNodeFlags_None;`, `imgui_widgets.cpp:6987`).
10. The corpus's single `goto begin;` (`imgui_draw.cpp:6009`) — an experiment that kept its own receipts (the rejected alternative preserved in comments beside it); not a license.

6. `ImGuiDir_None = -1` breaking the `_None = 0` rule.
7. Section-separator width drift (79 vs 75 chars across files).
8. Default-arg `=` spacing occasionally glued (`flags=0`, `imgui.h:471-472`).
9. `GetCursorPos()` naming admitted "confusing" (`imgui.cpp:12221`).
10. `ImGuiButtonFlags_AlignTextBaseLine` — "hack, since it shouldn't be a flag" (`imgui_widgets.cpp:799`).
11. stb-interfacing code keeps upstream snake_case — vendored-boundary exception, not a precedent.
12. `NULL` (core) vs `nullptr` (backends) split — historical; match the layer.

---

## 13. Demo TU Authoring Specification (`imgui_demo.cpp` surveyed in depth)

§8's D1-D9 summarize the demo *register*; this section specifies the demo *file* — every structural, ordering, and idiom rule needed to author a sibling demo TU and judge it. Target: `imguiapp_demo.cpp` (currently folded into `imguiapp.cpp` as `[SECTION] Composer demo (folded from imguiapp_demo.cpp)`, `imguiapp.cpp:3483`; entry `ImGui::ShowAppDemo(bool* p_open, ImGuiApp* host)`, `imguiapp.h:276`). Surveyed against the same v1.92.9 WIP checkout; `imgui_demo.cpp` = 11,559 lines, 55 `[SECTION]`s. Continues D-rule numbering from D9.

### 13.1 File skeleton

- **D10.** Top-of-file anatomy, exact order: banner `// dear imgui, vX.Y.Z WIP` + `// (demo code)` → help/links block → `PLEASE DO NOT REMOVE THIS FILE FROM YOUR PROJECT!` preamble arguing the file is reference documentation (`imgui_demo.cpp:18-32`) → `ABOUT THE MEANING OF THE 'static' KEYWORD` sermon (`:34-46`) → `ABOUT THE CODING STYLE OF OUR DEMO CODE` manifesto stating the four register rules (`:48-59`) → grep-navigation tips → `Index of this file:` (55 `[SECTION]` lines, `:67-127`) → `_CRT_SECURE_NO_WARNINGS` guard → `#include "imgui.h"` → `#ifndef IMGUI_DISABLE` → system includes → pragma block → local shims → `#if !defined(IMGUI_DISABLE_DEMO_WINDOWS)` (`:235`). **Two nested kill-switches**: the whole-library one immediately after the single library include, the demo-specific one after the shim tier (so shims compile even when demo content is disabled).
- **D11.** Includes: the library's ONE public header + libc only — 6 unconditional libc headers + 2 conditional (`<inttypes.h>` gated `!_MSC_VER || >=1800`, `<emscripten/version.h>`), every one carrying a trailing symbol comment (8/8, `:137-148`). Caveat discovered: the symbol comments describe the header's general contents, not this TU's exact usage — `sscanf`/`atoi`/`malloc`/`free` are named yet have zero call sites. A mechanical "includes list only symbols you call" audit must not treat that as the demo contract.
- **D12.** Warning-pragma block: MSVC block, then `#if __clang__ / #elif __GNUC__` (`:150-187`), every suppression quoting the warning text + a reason. The demo's set is a licensed SUPERSET of core's, each extra tied to copy-pasteability, not code quality — `-Wdeprecated-declarations // for strdup used in demo code (so user can copy & paste the code)`, `-Wexit-time-destructors // ...ImGui coding style welcomes static/globals`. **No pragma pop/restore exists at EOF** — suppression is TU-lifetime, a deliberate divergence from A3's push/pop symmetry (demo TU ends the translation unit; there is nothing after it to protect).
- **D13.** Local shim tier — the demo re-derives instead of including internal.h. Complete `#define` census: `_CRT_SECURE_NO_WARNINGS`, `IM_NEWLINE`, `snprintf`/`vsnprintf` (MSVC), `PRId64`/`PRIu64`, `IM_MIN`/`IM_MAX`/`IM_CLAMP` with the stated exception comment ("In other imgui sources we can use... ImMin/ImMax... but not in the demo", `:213-219`), `IMGUI_CDECL` rederivation (`:221-229`), `IMGUI_DEMO_MARKER` (`:303`). Internal equivalents measured at zero uses (`ImMin`/`ImMax`/`ImClamp`/`ImQsort` = 0); the aspirational `//#include "imgui_internal.h" // NavMoveRequestTryWrapping()` at `:11137` is kept commented — the line holds even where inconvenient.
- **D14.** EOF anatomy: `// End of Demo code` → `#else` → empty one-line stubs for EXACTLY the public entry points declared in the public header (5: `ShowDemoWindow`, `ShowAboutWindow`, `ShowStyleEditor`, `ShowStyleSelector` returning `false`, `ShowUserGuide`; `:11551-11555`) → `#endif` echoing each condition. No static function gets a stub — the stub tier IS the public surface, nothing else.

### 13.2 Section system & decomposition

- **D15.** Section separators are **exactly 79 dashes** (measured: 114 of 120 dash-only lines; the only outliers are the three top-of-file banner boxes, dash-fitted to their titles). Standard shape: dash / `// [SECTION] Name` / dash / blank. A section bundling two functions gets a 5-line mini-TOC variant with `// - Func1()` / `// - Func2()` lines (2 sites: `:8529-8533`, `:8969-8973`). Index↔body 1:1 sync is a MUST (55/55 here — with the §13.8 wart).
- **D16.** Two-tier decomposition with stated rationale — "We split the contents of the big ShowDemoWindow() function into smaller functions (because the link time of very large functions tends to grow non-linearly)" (`:257-258`): `ShowDemoWindow()` → five fixed-order category functions `DemoWindowWidgets/Layout/Popups/Tables/Inputs` (hand-picked priority order matching physical file order, `:719-724`) → `DemoWindowWidgets()` dispatches a flat call list of 25 `static void DemoWindowWidgetsXxx()` topic functions in **ascending case-insensitive alphabetical** order (`:4501-4531`). All `static void`, zero-arg unless they need the shared data struct. A new topic joins alphabetically in THREE places at once: index, body `[SECTION]`, dispatcher.
- **D17.** The shared-state struct (`ImGuiDemoWindowData`, `:318-350`): plain data, C++11 default member initializers, dtor only for owned allocations; `ShowApp*` fields for "Examples"-menu apps, bare `Show*` for "Tools". The window-dispatch block is column-aligned one-liners (`{` at col 45): `if (demo_data.ShowAppX) { ShowExampleAppX(&demo_data.ShowAppX); }` — the toggle bool IS the `p_open` target. Dependency-order overrides of the default order are licensed ONLY with an inline reason: `// Important: Process the Docking app first, as explicit DockSpace() nodes needs to be submitted early...` (`:369-370`).
- **D18.** Ordering directions (per A28, every list carries key + direction) — and the demo runs FOUR independent orders by design, not drift: (a) widget-topic functions: alphabetical ascending; (b) menu items: alphabetical ascending WITHIN named `SeparatorText` sub-groups ("Mini apps" = state-owning apps, "Concepts" = one-idea windows, `:745-769`); (c) window dispatch: dependency order with mandatory comment; (d) example-app file sections: historical/narrative — new apps APPEND at file end. Struct fields mirror menu order. No rule forces these to agree; each list states its own key.
- **D19.** Helper placement: a helper graduates from its single consumer's section into a dedicated `[SECTION] Helpers: <Name> (for use by A & B demos)` block **exactly when a second independent call site exists** (`ExampleTreeNode` shared by Property Editor + Multi-Select, `:75`; `ExampleImageViewer` shared by Widgets>Images + the app) — never speculatively. Single-consumer helpers sit immediately before their sole consumer, unforward-declared (`ShowExampleAppDockSpaceAdvanced`, `:10613`). Forward decls exist only for symbols called before their definition (`:232-271`). The file's one anonymous namespace is licensed by a stated need ("pre-C++11 doesn't allow us to instantiate ImVector<MyItem> template if this structure is defined inside the demo function", `:5725`).

### 13.3 Demo-block anatomy

- **D20.** Canonical skeleton — the unit of demo authorship:
  ```cpp
  IMGUI_DEMO_MARKER("Category/Label");   // optional pre-marker form, minority
  if (ImGui::TreeNode("Label"))
  {
      IMGUI_DEMO_MARKER("Category/Label");   // canonical: FIRST statement after the brace
      ...
      ImGui::TreePop();
  }
  ```
  **No `return`/`continue`/`break`/`goto` inside a TreeNode block, ever** — control always falls through to the textually-paired `TreePop()` (exceptionless across all offsets sampled). Early `return` is licensed ONLY as the category gate: `if (!ImGui::CollapsingHeader("Widgets")) return;` (4 sites, `:4493`, `:4543`, `:5427`, `:5891` — a stated widget-construction-skip optimization). The TreeNode-with-adjacent-HelpMarker gate captures the bool first: `bool open = ImGui::TreeNode(...); ImGui::SameLine(); HelpMarker(...); if (!open) return;` (`:7796-7800`).
- **D21.** Marker grammar: 188 call sites (187 live). Path = slash-separated hierarchy, Title Case, **tail segment matches the TreeNode label verbatim**; depth 2 dominant (102), depth 3 (81), deeper never (structurally). Leaf markers for individual widgets inside one open TreeNode are re-issued mid-body, typically right after a `SeparatorText` (`:994-997`). Marker macro is `do{}while(0)` forwarding `("imgui_demo.cpp", __LINE__, section)`, version-gated (D40).
- **D22.** HelpMarker: 211 calls against the fixed definition (`TextDisabled("(?)")` + `BeginItemTooltip` + `PushTextWrapPos(GetFontSize() * 35.0f)`, `:276-288`). Placement: `ImGui::SameLine(); HelpMarker("...");` **glued on one physical line after the widget it explains** (132/211; own-line variant only when the string is long); never before its subject. Text = full second-person sentences, `\n\n` paragraph breaks, caveats admitted.
- **D23.** Static-local discipline is real, not just preached: 471 `static` declaration lines ≈ 4.5% of code lines. Declared at FIRST USE, never hoisted to function top. `static const char* items[] = {...}` only when the array must persist beside a `static int` cursor; throwaway literal arrays stay non-static locals.
- **D24.** Flag-demo idiom: `static ImGuiXxxFlags flags = ImGuiXxxFlags_None;` then one `ImGui::CheckboxFlags("ImGuiXxxFlags_Name", &flags, ImGuiXxxFlags_Name);` per flag — **the visible label is the enum member's fully-qualified name verbatim** (255 CheckboxFlags sites); per-flag explanation glues `SameLine(); HelpMarker(...)` onto that same line. Dense option blocks in the tables section are bracketed by the local `PushStyleCompact()`/`PopStyleCompact()` pair (`:5797-5808`); the tables section adds the `open_action` idiom — Expand-all/Collapse-all buttons driving `SetNextItemOpen` before every sub-demo TreeNode (`:5901-5931`).
- **D25.** Composition lexicon: `SeparatorText("Sub-heading")` (68 sites) divides one TreeNode body — not a nested TreeNode; `PushID(i)`/`PopID()` wraps widget-emitting loops with a why-comment (`:4232-4245` is the canonical annotated form); bare `ImGui::SameLine()` (296) composes rows; result-echo `ImGui::Text("name = %d", v)` is reserved for values a widget doesn't display itself (12 sites). Commented-out code is ALWAYS `//glued` (28 `//ImGui::` + 6 `//static`, zero spaced counterparts) and kept deliberately as instructive alternatives (`//static ImGuiOnceUponAFrame once;`, `:1192`).

### 13.4 Example-app anatomy

- **D26.** Banner grammar: `// [SECTION] Example App: <Title> / ShowExampleApp<Name>()`, byte-identical in index and body (16/16). A one-to-three-line `// Demonstrate <what>.` sentence directly above the entry function is SHOULD (10/16; the four newest apps lack it — gap, not license).
- **D27.** Signature: `static void ShowExampleApp<Name>(bool* p_open)` with `static` repeated at the definition (13/16 — the 3 omissions are warts). Extra parameters only for externally-owned data (`ShowExampleAppPropertyEditor(bool*, ImGuiDemoWindowData*)`); no `p_open` only when there is no closable window (`MainMenuBar`).
- **D28.** Window ritual: `if (!ImGui::Begin("Example: <Title>", p_open [, flags])) { ImGui::End(); return; }` then `IMGUI_DEMO_MARKER("Examples/<Name>")` immediately after; `SetNextWindowSize(ImVec2(w,h), ImGuiCond_FirstUseEver)` when a default size matters (6/16; fullscreen/dockspace apps set size every frame on purpose). Title prefix `"Example: "` (11/16; breaches are warts). The `if (Begin(...)) {...}` + unconditional `End()` form coexists for trivial apps — early-out is the richer apps' convention.
- **D29.** Struct-backed apps (state too big for scattered statics): struct declared DIRECTLY ABOVE its Show function in the same section; PascalCase members, `ImVector<>` members = plural nouns, methods = `<Verb><Noun>` (`AddLog`/`ClearLog`), printf-likes tagged `IM_FMTARGS(n)`; modern init form = default member initializers + explicit ctor ONLY for non-trivial setup (`ExampleAssetsBrowser() { AddItems(10000); }`, `:11211`). Show function body is two lines: `static ExampleAppX x; x.Draw("Example: X", p_open);` — `Draw(const char* title, bool* p_open)` owning its own Begin/End is the default shape (contents-only `Draw` licensed when the caller owns data/window for a reason, as Property Editor).
- **D30.** Console is the reference for C-callback plumbing: per-struct `// Portable helpers` libc shim statics (`Stricmp/Strnicmp/Strdup/Strtrim`, `:9126-9130`); the static trampoline `TextEditCallbackStub(ImGuiInputTextCallbackData*)` casting `data->UserData` back to `this`, annotated "In C++11 you'd be better off using lambdas for this sort of forwarding callbacks" (`:9336-9341`). Multi-column sort = raw `qsort` + `static const ImGuiTableSortSpecs* s_current_sort_specs;` with the verbatim workaround rationale ("qsort doesn't allow passing user data... In your own use case you would probably pass the sort specs... directly and not use a global", `:5745-5752`) — the `s_` prefix is the file's SOLE lowercase-prefixed identifier, a deliberate "this is a global-ish workaround" flag, not a naming license.

### 13.5 Register & naming (mechanics behind D1-D4)

- **D31.** The `ImGui::` prefix rule is absolute-with-two-licensed-exceptions: 4,165 qualified calls, zero using-directives; `ShowStyleEditor` and `ShowUserGuide` are 100% unqualified internally, EACH preceded by the identical disclaimer `// We omit the ImGui:: prefix in this function, as we don't expect user to be copy and pasting this code.` (`:8574`, `:8927`). The exception mechanism (defining `void ImGui::Foo()` puts the body in-namespace for lookup) is legal and owned; a sibling demo replicates the rule WITH the named-exception clause, never silently.
- **D32.** Function tiers inside the demo TU: (1) the public `ImGui::`-defined entry points (5 here); (2) `static void ShowExampleApp*` apps; (3) `static void DemoWindow*` decomposition functions (33); (4) plain PascalCase file statics (`HelpMarker`, `PushStyleCompact`, `EditTableSizingFlags`...); (5) domain-prefixed free-helper families `Example<Thing>_<VerbNoun>(<Thing>Data*...)` (`ExampleTree_CreateNode`, `ExampleImageViewer_DrawCanvas`); (6) function-local `struct Funcs { static float Sin(void*, int) ... }` — the fixed name `Funcs`, used solely to smuggle static methods into C-function-pointer slots (3 sites).
- **D33.** Type tiers: `Example*` = user-substitutable content models (11 structs — what the reader renames into their own app); `ImGuiDemo*` = plumbing structs threading state between the split pieces of the demo itself (`ImGuiDemoWindowData` — "Data to be shared across different functions of the demo", `:317`) — the split is content-vs-plumbing, and only plumbing takes the library prefix. Local enums use full `Type_Value` grammar with `_COUNT` sentinels (`Element_Fire..Element_COUNT`, `:1143`); struct members PascalCase throughout (house vocabulary includes `Childs`). Out-params beyond `p_open` keep the `p_` prefix mechanically (`p_flags` ×35 on the `EditXxxFlags(ImGuiXxxFlags* p_flags)` helpers).
- **D34.** Dialect census: `NULL` over `nullptr` 161:1 (core register, per I25); C-style casts (~130 sampled; one stray `reinterpret_cast`, wart); `auto` = 0; lambdas = 5, all captureless callback-slot initializers; range-for = 20, all explicit element types; allocation via `IM_NEW`/`IM_DELETE`/`ImGui::MemAlloc` — legal under D3 because they live in public `imgui.h`; `ImVector<>` for growable collections, plain C arrays for fixed scratch — segregated by role, never competing. Raw `qsort` (3 sites) is the demo's sanctioned sort (N22's `ImQsort` is internal and thus off-limits here).
- **D35.** UTF-8: the policy statement is shouted and self-limiting — "FOR THIS DEMO FILE ONLY, BECAUSE WE WANT TO SUPPORT OLD COMPILERS, WE ARE *NOT* INCLUDING RAW UTF-8... Don't do this in your application!" (`:3848-3853`); hex-escape strings with the `u8"..."` alternative kept as an adjacent commented line (`:3860-3863`).

### 13.6 Integration surface

- **D36.** Public-surface split by STRIPPING OWNER, not by header adjacency: the demo TU owns and stubs exactly its 5 window/block functions under `IMGUI_DISABLE_DEMO_WINDOWS`; debug tools (`ShowMetricsWindow`, `ShowDebugLogWindow`, `ShowIDStackToolWindow`) live in core under the orthogonal `IMGUI_DISABLE_DEBUG_TOOLS`. The gates' independence is historical policy — a 1.83 bug where one implied linking the other's TU was fixed with the note "we want to avoid implying that the file is required" (`CHANGELOG.txt:5300-5302`). Counter-example wart: `ShowFontSelector` is dual-gated `#if !defined(A) || !defined(B)` in `imgui.cpp:24345` with NO stub — declared-but-undefined when both macros are set (latent link error). A sibling gives every public demo-adjacent function exactly one owner + one stub.
- **D37.** Strippability posture: always compile and link the demo TU; the linker dead-strips it when the entry point is never called ("Everything in this file will be stripped out by the linker if you don't call ImGui::ShowDemoWindow()", `:27`); the disable macro is an opt-in "thorough guarantee," not the primary mechanism.
- **D38.** Dependency direction is strictly demo → core. Zero core call sites into demo-owned symbols (grep-verified across imgui.cpp) — this is what makes D37's linker-strip claim true. Audit rule: grep the library TU for every demo public symbol; expect zero.
- **D39.** Marker hook is a three-part decoupling: (a) version-gated macro in the demo TU only (`#if IMGUI_VERSION_NUM >= 19263`, `:301-304`); (b) one extern-declared forwarding function `ImGui::DemoMarker(file, line, section)` defined UNGATED in core (`imgui.cpp:5264-5269`); (c) a nullable `ImGuiDemoMarkerCallback DemoMarkerCallback` slot on the context (`imgui_internal.h:2402`, `:2749`), init NULL, invoked only if set. Tools (interactive browsers, test harnesses) opt in by filling the slot; neither demo nor library knows any concrete tool.
- **D40.** Sneaky-extern rule: the demo NEVER includes the internal header. The narrow leak surface is one labeled block — `// Sneakily forward declare functions which aren't worth putting in public API yet` re-declaring exact signatures (`ShowFontAtlas`, `TreeNodeSetOpen`, `:306-311`). Both directions auditable: every declared symbol is called in-file; every internal-symbol call is declared in the block.
- **D41.** Version-gate policy: demoed BEHAVIOR is never forked on version/feature macros — the file's single `#if IMGUI_VERSION_NUM` gate is the marker plumbing itself. Config/feature macros surface only as the About-window diagnostic echo: ~20 `#ifdef X` / `ImGui::Text("define: X")` triplets (`:8365-8447`), plus the runtime `IM_ASSERT`-is-noop heuristic and the `LogToClipboard()`/`LogText("```cpp\n")`/`LogFinish()` copy bracket (`:8354-8358`, `:8449-8462`).

### 13.7 Measured micro-lexicon (demo-specific numbers)

- Line length: max 452, p99 = 185; the 76 lines >200 chars are ONE idiom — `SameLine(); HelpMarker("teaching paragraph")` glued to a widget line. Long lines are licensed for that shape specifically.
- Blank lines: **zero double-blanks in 11,559 lines** — single blank is a hard ceiling (stricter than core F23).
- Trailing comments: ~3.5% of code lines; column-aligned only inside repeated blocks (includes, dispatch, window-flags), single-spaced for one-offs — context-dependent by design.
- Floats: 519 `N.Nf` sites (one bare `0.5` wart); halving `* 0.5f` 24 vs 2 genuine `/2`; **pi is hardcoded `3.141592f`** — `IM_PI` is internal.h and has 0 uses.
- Array sizing: **`IM_COUNTOF` 106 / `IM_ARRAYSIZE` 0** — fully migrated to the 1.92.6 rename; any note claiming demo uses `IM_ARRAYSIZE` is stale.
- IDs: `##` 138, `###` 12 (`"Animated title %c %d###AnimatedTitle"`, `:10263`); trailing `##2`-style numeric disambiguators for repeated literals.
- Tags: FIXME 28 (17 qualified `FIXME-DOCK/-OPT/-NAV/-TABLE/-MULTISELECT/-NEWATLAS`), TODO 1, `NB:` 4; `XXX` is never a review tag (all 16 hits are `DragXXX`-style family placeholders).
- Control flow: `switch` 7, `goto` 0, `for (;;)` 1; loop index `n` 48 vs `i` 44 — core F26's `n` preference is NOT enforced in the demo register.
- Buffers: `[32]` dominant (F27 census holds), `[64]`=12, `[128]`=6, `[256]`=2.

### 13.8 Demo warts (imgui_demo.cpp's own; do not imitate)

1. Three-way ordering mismatch on ONE topic pair: index + dispatcher say `ComboBoxes` before `ColorAndPickers`; the body is alphabetical (`ColorAndPickers` `:1255`, `ComboBoxes` `:1456`). The other 24 topics agree in all three places.
2. `static` dropped at 3 of 16 `ShowExampleApp*` definitions despite static forward decls (`DockSpace` `:10676`, `Documents` `:10867`, `AssetsBrowser` `:11541`) — linkage-safe, textually inconsistent.
3. `My*` placeholder types (`MyItem`, `MyDocument`, `MyTreeNode`) — older vintage of the `Example*` convention; standardize on `Example*`.
4. `ContentsType` enum declared 3× locally with abbreviated `CT_` values against the 5 sibling enums' full `Type_Value` grammar.
5. One `reinterpret_cast` (`:9799`) and one bare `0.5` float (`:2267`) — single-site dialect breaches.
6. Marker/label mismatches: pre-condition marker "Rendering more items on the same line" vs TreeNode "Multiple items..." (`:2487-2490`); `"Examples/Property editor"` vs `"...Property Editor"` casing split between the struct's Draw and its Show fn.
7. A commented-out `// IMGUI_DEMO_MARKER("Widgets");` placeholder after a category gate (`:4495`).
8. Window-title drift: DockSpace's windows `"Window with a DockSpace"` / `"Examples: Dockspace"` match neither each other nor the `"Example: <Title>"` convention; title casing splits sentence-case vs Title Case across apps; section-banner casing splits eras (apps 1-9 Title Case, 10-16 loose).
9. Two coexisting gate styles for TreeNode-with-HelpMarker (early-return `:7796` vs positive-if `:8007`); `ExampleAppLog::Draw`'s default argument `bool* p_open = NULL` that no sibling repeats.
10. Missing `// Demonstrate...` openers and missing `IMGUI_DEMO_MARKER`s on the newest apps (ImageViewer, DockSpace, Documents/AssetsBrowser partially).
11. `-Wformat-security` suppression whose "reason" merely restates the warning.
12. `ShowFontSelector`: dual-gated in core with no stub (D36) — declared-but-undefined under both disable macros.

### 13.9 imguiapp mapping (demo)

`ShowDemoWindow` → `ShowAppDemo`; when the demo region unfolds from `imguiapp.cpp` into its own `imguiapp_demo.cpp`, D10-D41 apply verbatim with these bindings: `imgui.h` → `imguiapp.h` (single public include; the imgui public API is additionally legal, per the demo's own register); `IMGUI_DISABLE_DEMO_WINDOWS` → an `IMGUIAPP_DISABLE_DEMO_WINDOWS` owning stubs for exactly the applayer demo entry points; `IMGUI_DEMO_MARKER` → an `IMGUIAPP_DEMO_MARKER` built on D39's three-part hook (macro in demo TU, forwarding function in applayer, nullable callback slot on `ImGuiApp`/context, NULL-init); sneaky-extern block names any not-yet-public `ImGui::App*` symbols one signature at a time — `imguiapp_internal.h` is never included. Register split per A23 holds: framework chrome (the Composer host window, `ShowAppDemo` plumbing structs) keeps applayer grammar (`ImGuiAppDemo*` plumbing prefix per D33); showcased sample code keeps the user register (`Example*` models, statics-at-first-use, full `ImGui::`/`ImGui::App*` qualification). The D18 four-order rule transfers: topic functions alphabetical, menus alphabetical within named groups, dispatch by dependency with inline reasons, app sections append-at-end.

---

## 14. Widgets TU Authoring Specification (`imgui_widgets.cpp` surveyed in depth)

§7's I12/I33-I37 specify the widget *code*; this section specifies the widget *file* — the structural, ordering, and register rules for authoring a sibling widget TU and judging one. Surveyed against the same v1.92.9 WIP checkout; `imgui_widgets.cpp` = 11,123 lines, 23 body `[SECTION]`s, 214 `ImGui::` definitions, 71 file-scope statics. Rule prefix W.

### 14.1 File skeleton

- **W1.** Top-of-file anatomy, exact order: banner `// dear imgui, v1.92.9 WIP` + role line `// (widgets code)` → `Index of this file:` inside a `/* */` block (`imgui_widgets.cpp:4-33`) → `_CRT_SECURE_NO_WARNINGS` guard → `IMGUI_DEFINE_MATH_OPERATORS` (ifndef-guarded, BEFORE any include, per A9) → `#include "imgui.h"` → `#ifndef IMGUI_DISABLE` → `#include "imgui_internal.h"` → `// System includes` (exactly one: `<stdint.h>     // intptr_t`, symbol-commented per A8) → `// Warnings` dashed banner + A3 pragma blocks (no push/pop, TU-lifetime). **No help/links block** — that tier belongs to imgui.cpp and the demo only. EOF is bare: last definition → blank → `#endif // #ifndef IMGUI_DISABLE` (`:11123`); no stub tier, no pragma pops.
- **W2.** Pre-section `// Data` dashed banner (`:99-132`), NOT listed in the index and NOT a `[SECTION]`: topic-grouped `static const` tunables per N23 (`DRAGDROP_HOLD_TO_OPEN_TIMER = 0.70f;` with aligned rationale comments, `:104-105` — unprefixed, unlike tables' `TABLE_`), then the pointable min/max constants `IM_S8_MIN..IM_U64_MAX` with the address-required justification stated once for the block: `// Those MIN/MAX values are not define because we need to point to them` (`:107`, the I23 citation).
- **W3.** Forward-decl geography is two-tier: the top `[SECTION] Forward Declarations` holds ONLY cross-section statics, labeled by consumer (`// For InputTextEx()`, 2 entries, `:135-140` — default args live on the static DECLARATION: `= NULL`, `= false`); everything else declares at its owning section's head. The TabBar section re-opens `namespace ImGui { static void TabBarLayout(...); ... }` mid-file (`:9754+`) and defines with `static` REPEATED: `static void ImGui::TabBarLayout(ImGuiTabBar* tab_bar)` (7 sites, `:9946+`) — file-local functions that need namespace membership (unqualified calls into internal API, `ImGui::`-qualified name at definition) take this form rather than going global-static.

### 14.2 Section system & ordering

- **W4.** Section banner: 75-char dash rule (`//` + 73 dashes; 74 of 82 dash-only lines — the §12 width-drift wart vs demo's 79) / `// [SECTION] Name` / dash rule, then a mini-TOC decl list `// - FuncName()` closed by another dash rule — present on 21/23 sections (missing: Forward Declarations, Typing-Select support). List entries are in DEFINITION order, tag internal-header functions `[Internal]` (`// - MultiSelectItemHeader() [Internal]`, `:7962`), tag templates `<>()` (`// - RoundScalarWithFormat<>()`, `:2256`). Prose annotations slot between banner and list: section-status sentences (`// This has been extracted away from Multi-Select logic in the hope that it could eventually be used elsewhere...`, `:7752`), deprecation verdicts (`// Those is not very useful, legacy API.`, `:9065` — [sic]), cross-references (`// Extra logic in MultiSelectItemFooter() and ImGuiListClipper::Step()`, `:7754`).
- **W5.** Section order is A26's reference case: imgui.h `// Widgets:` group order verbatim, support sections inserted before their first consumer (Data Type helpers before Drag; Typing-Select/Box-Select/Multi-Select cluster between Selectable and ListBox). Within a section: shared `*Behavior` engine first, then `Ex` engine, then public wrappers immediately after their engine (`ButtonBehavior` `:545` → `ButtonEx` `:786` → `Button` `:828` → style-tweak variants), per A27 adjacency.
- **W6.** Wide dash rules (146-147 chars, 8 lines, `:488-535`) exist for ONE purpose: fencing tabular doc-comments — the ButtonBehavior frame-by-frame state-transition tables, dash-fitted to column width like the demo's banner boxes. Rules of other widths are not license for decorative separators.
- **W7.** Register-quarantined enclave: `namespace ImStb` wraps the vendored text-edit engine with a TWO-PASS include — declaration pass `namespace ImStb { #include "imstb_textedit.h" }` (`:3992-3996`), implementation pass after the shim tier with `#define IMSTB_TEXTEDIT_IMPLEMENTATION` (`:4272-4274`). Inside the namespace ONLY, stb dialect is licensed: snake_case statics (`is_word_boundary_from_right`, `:4080`; `stb_textedit_replace`, `:4278`), `STB_TEXTEDIT_*` SCREAMING shim functions and `*_IMPL` redirect macros (`:4137-4138`), `#define`d `K_*` keymap constants with aligned comments (`:4254+`). The enclave closes with the echo `} // namespace ImStb` (`:4295`) and is the TU's only register exception.

### 14.3 TU-local symbols

- **W8.** Types: 2 TU-local structs, both fully tier-prefixed per A23 (`ImGuiPlotArrayGetterData`, `:9026`; `ImGuiTabBarSection` with A19 memset-zero ctor, `:9744`); zero TU-local enums. Static tables: `G`-prefixed const arrays locked by adjacent `IM_STATIC_ASSERT` per A18 (`GDataTypeInfo[]` + `IM_COUNTOF == ImGuiDataType_COUNT`, `:2264-2284`; rows column-aligned, enum-name trailing comments on alternating rows, `#ifdef _MSC_VER` fork inline for `%I64d` vs `%lld`); the file's second static-assert form locks layout invariants where code exploits them: `IM_STATIC_ASSERT(offsetof(ImGuiMultiSelectTempData, IO) == 0); // Clear() relies on that.` (`:8015`).
- **W9.** Comparators: 4, all N22-conformant (`ShrinkWidthItemComparer` `:1869`, `PairComparerByValueInt` `:8621`, `TabItemComparerBySection/ByBeginOrder` `:9777/:9788`) — `lhs`/`rhs`, `IMGUI_CDECL`, consumed only via `ImQsort`.
- **W10.** Monolith license: stateful widget engines stay ONE linear phase-labeled function, never decomposed — `InputTextEx` = 1,004 lines (`:4708+`), `TabItemEx` 306, `TreeNodeBehavior` 282, `TabBarLayout` 272. Decomposition targets are the shared `*Behavior`/`Ex` seams (W5), not intra-widget phases; phases are `// Render`-style labels (36 sites) inside the monolith.

### 14.4 Measured micro-lexicon (widgets-specific numbers)

- Context: `ImGuiContext& g = *GImGui;` 133; `ImGuiContext& g = *ctx;` 2 (both InputText plumbing where ctx is a parameter, per I3); SkipItems gates 61; `IMGUI_TEST_ENGINE_ITEM_INFO` 19.
- Dialect: `NULL` 189 / `nullptr` 0 / `auto` 0; range-for 6 (all explicit element type); lambdas 1 live (captureless callback-slot init, `AdapterIndexToStorageId = [](...)`, `:8598`) + 1 in a teaching comment (`:7569`).
- Tags: FIXME 96 (qualified 27: `-OPT` 9, `-DPI` 6, `-MULTISELECT` 3, `-STYLE` 2, `-WORDWRAP` 2, `-ALIGN/-NAV/-OSX/-TEXT/-WIP` 1 each); TODO 0 (the one hit names TODO.txt, `:10260`); HACK 0.
- Loops/math: `for (int n` 11 vs `for (int i` 7; halving `* 0.5f` 46 vs 6 integer `/ 2`; `ImTrunc` 13 + `IM_TRUNC` 25.
- Lines: max 372, p99 = 192, >200 chars 81; double-blanks 3 (`:7559`, `:9690`, `:11122` — near-zero, F23 holds); glued `//code` comment-outs 16, zero spaced.
- Asserts/suppressions: `IM_ASSERT` 135; PVS `//-V` 5; parenthesized issue refs 21 lines.

### 14.5 Widgets warts (imgui_widgets.cpp's own; do not imitate)

1. **Index↔body drift, 6 counts** — the corpus's own A2 violator: index says `Widgets: Main (Button, Image, ...)` / `Low-level Layout helpers (Spacing, ...)`, body drops both parentheticals (`:459`, `:1598`); body appends `[Internal]` to `Data Type and Data Formatting Helpers` (`:2247`) and `, InputTextWithHint` to the InputText section (`:3982`) without updating the index; body DROPS the `Widgets:` prefix on `MenuItem, BeginMenu, EndMenu, etc.` (`:9101`); and the index's final entry `Widgets: Columns, BeginColumns, EndColumns, etc.` (`:31`) is a PHANTOM — the section lives in `imgui_tables.cpp:22`, the index entry never removed after the migration. Decay mode: bodies get renamed/extended, indexes go stale — sync byte-identically, both directions, on every section touch.
2. `// Those is not very useful, legacy API.` (`:9065`) — grammar wart preserved verbatim; the Value section it heads is retained-legacy, not a pattern.
3. Commented-out popup-close logic kept in ButtonEx (`//if (pressed && ...)`, `:821-822`) — an A29(c)-style reactivatable probe, not license for dead code.
4. The `held ?` minority color-pick form self-flagged `FIXME` at ButtonBehavior's own doc block (`:537-539`, per I35).
5. `ImGuiButtonFlags_AlignTextBaseLine` "bit hacky, since it shouldn't be a flag" (`:799`, already §12.10).

### 14.6 imguiapp mapping (widgets)

The applayer control regions of `imguiapp.cpp` are widget-TU siblings; W-rules bind directly: per-region `[SECTION]` banners with mini-TOC decl lists in definition order, `[Internal]` tags on internal-header entries (W4); region order mirrors `imguiapp.h` declaration order with support-regions inserted before first consumer (W5/A26); TU tunables in a pre-section Data banner, UPPER_SNAKE + rationale columns (W2); TU-local types keep `ImGuiApp*` prefixes with static tables locked by `IM_STATIC_ASSERT` (W8); control engines follow the engine-then-wrapper adjacency (W5) and the monolith license (W10) rather than helper-function sprawl. No ImStb-equivalent enclave exists in the applayer; a vendored engine, if one ever lands, gets the W7 shape — own namespace, two-pass include, dialect quarantined inside.

---

## 15. Tables TU Authoring Specification (`imgui_tables.cpp` surveyed in depth)

§7.14/I28 and I40 name tables the reference big-subsystem; this section specifies the *file*. Surveyed against the same v1.92.9 WIP checkout; `imgui_tables.cpp` = 4,879 lines, 15 `[SECTION]`s, 98 `ImGui::` definitions, 19 file-scope statics. Rule prefix T.

### 15.1 File skeleton

- **T1.** Top-of-file anatomy: banner + role line `// (tables and columns code)` → `/* */` index → `// Navigating this file:` IDE-tips block (Ctrl+Comma / Alt+G / Ctrl+Click symbol-follow instructions, `imgui_tables.cpp:26-29` — grep-verified present only here and in `imgui_demo.cpp:61`; absent from imgui.cpp, imgui_widgets.cpp, imgui_draw.cpp) → `[SECTION] Commentary` → `[SECTION] Header mess`. Unlike widgets (W1), the includes/pragma tier is a NAMED, INDEXED section — `Header mess`, the same name imgui_internal.h uses — containing the identical guard/define/include/pragma sequence (`:192-247`; contents byte-similar to widgets' W1 tier). EOF: last definition → blank → bare closing dash rule → `#endif // #ifndef IMGUI_DISABLE` (`:4874-4879`) — tables closes with a dash rule where widgets closes bare.
- **T2.** The A16 `[SECTION] Commentary` is TWO essays, each fenced by its own dash rules: the typical call-flow tree — public API at root level, internal calls as `|`-indented children with per-line role comments (`:36-73`) — then the `TABLE SIZING` essay explaining `outer_size`/`inner_width` semantics and sizing-policy design (`:75-187`). The call-flow tree names every load-bearing internal function BEFORE any code; a subsystem TU of this weight leads with its flow, not its includes (Header mess comes AFTER Commentary).
- **T3.** No Forward Declarations section and no Data banner: tables needs zero cross-section static decls (statics define before first use), and TU tunables live INSIDE `Tables: Main code` under a `// Configuration` label — `static const` with the `TABLE_` topic prefix and aligned rationale comments (`TABLE_BORDER_SIZE ... // FIXME-TABLE: Currently hard-coded because of...`, `:266-272`; the N23 citation). One `// Helper` follows: `inline ImGuiTableFlags TableFixFlags(...)` (`:275`) — the TU's single non-static `inline` file helper.

### 15.2 Section system & ordering

- **T4.** Index↔body sync is PERFECT here: 15/15 byte-identical (measured diff = empty) — the A2 reference case, against widgets' 6-count drift (W-warts 1). Mini-TOC decl lists per W4 on 13/15 sections (all code sections; only Commentary and Header mess lack them), `[Internal]` tags throughout, banner-embedded prose slots used for section-status verdicts: `// FIXME: The binding/finding/creating flow are too confusing.` (Settings, `:3833`), `// (This is a legacy API, prefer using BeginTable/EndTable!)` + an in-banner FIXME line (Columns, `:4435-4437`).
- **T5.** Dash-rule width runs TWO era regimes: 79 chars from the Commentary (`:31`) through Row changes, 75 chars from `:2041` to EOF (measured 35 vs 38 lines) — the §12.7 width drift is INTRA-file here, not just cross-file. Match the neighborhood you edit; do not renormalize.
- **T6.** Section order is NOT header-declaration order: it follows the Commentary's lifecycle flow — Main code (Begin/End/Setup) → accessors → Row changes → Columns changes → width management → Drawing → Sorting → Headers → Context Menu → Settings → GC → Debugging — with the legacy Columns API quarantined LAST. A26's rank-monotonicity audit does not apply file-wide to tables; the stated sort key is the call-flow narrative (T2), an A28-sanctioned alternative direction. Debugging-before-legacy-tail is the fixed cardinal anchor.
- **T7.** The Settings section banner carries a numbered PIPELINE map beyond the decl list: `// [Init] 1: TableSettingsHandler_ReadXXXX() ... [Main] 2: TableLoadSettings() ... [Main] 3: TableSaveSettings() ... [Main] 4: TableSettingsHandler_WriteAll()` with per-step role comments (`:3851-3855`) — phase-tagged, numbered, in execution order. Multi-function protocols get this map in the banner, not prose in the bodies.

### 15.3 TU-local symbols & register

- **T8.** One TU-local struct, tier-prefixed per A23 (`ImGuiTableFixDisplayOrderColumnData`, `:4083`) feeding the one N22 comparator (`TableFixDisplayOrderComparer`, `:4090`); zero TU-local enums; zero `static <ret> ImGui::` namespace-member definitions (widgets' W3 form is absent — tables' statics are all global-scope). Settings handler callbacks use N12's underscore sub-namespacing, one per I26 slot actually used: `TableSettingsHandler_{ClearAll,ApplyAll,ReadOpen,ReadLine,WriteAll}` (`:3844-3849`), installed by `TableSettingsInstallHandler` filling the handler struct field-by-field.
- **T9.** Chunk-stream settings memory is constructed via `IM_PLACEMENT_NEW(settings) ImGuiTableSettings();` then per-column placement-new in a counting loop (`TableSettingsInit`, `:3858-3868`) — the sanctioned pattern for I8 variable-size records; sizes computed by a dedicated `TableSettingsCalcChunkSize(int columns_count)` beside it.
- **T10.** `IMGUI_DISABLE_DEBUG_TOOLS` stub discipline (the I22 reference shape): the whole Debugging section body inside `#ifndef`, the `#else` echoing its condition, stubs as one-line `{}` definitions with UNNAMED parameters (`void ImGui::DebugNodeTable(ImGuiTable*) {}`, `:4425-4428`); debug-only static helpers (`DebugNodeTableGetSizingPolicyDesc`) live inside the guard and get NO stub — only the internal-header-declared surface is stubbed.

### 15.4 Subsystem disciplines (file-specific evidence for I28/I40)

- **T11.** `TableUpdateLayout` runs exactly 13 `// [Part N] <summary>` phases (`:909-1363`); part summaries are full sentences, may carry sub-rationale lines, and number strictly ascending with no gaps. `TableBeginInitMemory` opens with the itemized allocation ledger (`// + 0 (for ImGuiTable instance, we are pooling...) + 1 (for table->RawData...)`, `:640-648`). `SetCurrentChannel` 25 sites, `ImSpan` carving per I28 — the disciplines are load-bearing here, not decorative.
- **T12.** Deferred-request grammar per N24: immediate `TableSetColumnDisplayOrder`-class mutators pair with `TableQueue*` twins, GC pairs overload one name over sibling types (`TableGcCompactTransientBuffers(ImGuiTable*/ImGuiTableTempData*)`, `:4259/:4274`), and commented-out `//IMGUI_DEBUG_PRINT(...)` probes sit at GC entry points per A29(c) (`:4261`).

### 15.5 Measured micro-lexicon (tables-specific numbers)

- Context: `g = *GImGui` 56; `g = *ctx` 3, all in Settings handler callbacks (`:4120/:4130/:4187` — the I3 citation); `IMGUI_TEST_ENGINE` refs 2.
- Dialect: `NULL` 81 / `nullptr` 0 / `auto` declarations 0 (28 word-hits are all prose: "auto-fit", "(auto)"); range-for 9; lambdas 0.
- Tags: FIXME 68 — `-TABLE` 28 dominant (the home area tag), `-OPT` 3, `-STYLE` 3, `-COLUMNS` 2, `-WORKRECT` 2, `-DPI/-FROZEN/-LEGACY/-RECONCILE` 1 each; TODO 0; HACK 0.
- Loops: `for (int n` 24 vs `for (int i` 2 — the strongest F26 `n`-preference of the three satellite TUs (column iteration idiom: `for (int n = 0; n < columns_count; n++, settings_column++)`).
- Lines: max 325, >200 chars 35; double-blanks 5 (`:154`, `:2335`, `:2627`, `:4302`, `:4432` — mostly at section seams); glued `//code` comment-outs 18.
- Asserts: `IM_ASSERT` 97 (≈1 per 50 lines, densest of the three); `IM_STATIC_ASSERT` 1; PVS `//-V` 0; issue refs 6.

### 15.6 Tables warts (imgui_tables.cpp's own; do not imitate)

1. Intra-file dash-width regime split 79→75 at `:2041` (T5) — era seam, not a to-restore uniformity; new sections match their neighbors.
2. GCC `-Wstrict-overflow` pragma carries NO reason comment (`:243`) while its widgets twin does (`imgui_widgets.cpp:92`) — lone bare suppression in the A3 blocks.
3. Self-flagged design debt in banners: Settings "binding/finding/creating flow are too confusing" (`:3833`); Columns sizing "lossy when columns width is very small" (`:4437`); `TABLE_BORDER_SIZE` "Currently hard-coded because of clipping assumptions" (`:269`). Documented debt ≠ pattern.
4. `TableFixFlags` as bare `inline` (not `static`) at file scope (`:275`) — single-site linkage oddity against N12's static default.

### 15.7 imguiapp mapping (tables)

For any applayer subsystem at tables' scale (the graph/editor regions of `imguiapp.cpp` are nearest): lead the region with a T2-shape Commentary — call-flow tree naming internal functions with `|`-indented children, then design essays — before any code; keep index↔banner sync at 15/15 exactness (T4); order the region by lifecycle flow and STATE that key per A28 (T6); quarantine any legacy API as the tail section with the in-banner "prefer X" verdict (T4); number monolithic passes `[Part N]`, carry allocation ledgers on the memory-owning entry point (T11); settings persistence takes the numbered `[Init]/[Main]` pipeline map + `<Thing>SettingsHandler_<Slot>` naming + I26's sidecar caveat (T7/T8); DebugNode stubs follow T10 verbatim (`IMGUIAPP` disable macro, unnamed-param `{}` stubs, statics unstubbed).

---

## 16. Draw TU Authoring Specification (`imgui_draw.cpp` surveyed in depth)

§7.18/I39-I41 specify the draw-layer *code*; this section specifies the *file* — the register of a method-heavy, context-light, vendor-hosting TU. Surveyed against the same v1.92.9 WIP checkout; `imgui_draw.cpp` = 6,824 lines, 18 body `[SECTION]`s, 138 member-method definitions vs only 16 `ImGui::` functions, 42 file-scope statics. Rule prefix R.

### 16.1 File skeleton

- **R1.** Top anatomy is the W1 shape (banner + `// (drawing and font code)` → `/* */` index → guards → includes → A3 no-push pragmas; no help/links block, no Navigating block — that block is grep-verified tables+demo only, T1) with two file-specific include facts: a THIRD library include under its feature gate (`#ifdef IMGUI_ENABLE_FREETYPE` → `misc/freetype/imgui_freetype.h`, `imgui_draw.cpp:39-41`) and two symbol-commented libc headers. Suppression set adds vendored-code accommodations (`4505 // unreferenced local function has been removed (stb stuff)`, `:49`) and the corpus's most candid reason comment (`-Wglobal-constructors ... // similar to above, not sure what the exact difference is.`, `:63`). EOF: the guarded ProggyForever accessor → `#endif` echoing its DOUBLE condition → `#endif // #ifndef IMGUI_DISABLE` (`:6819-6824`).
- **R2.** The `[SECTION] STB libraries implementation` is the A3 vendored-fence reference: a `// Compile time options:` block of commented-out redirect macros (`//#define IMGUI_STB_NAMESPACE ImStb`, filename overrides, per-lib implementation opt-outs, `:92-97`); optional namespace wrap `#ifdef IMGUI_STB_NAMESPACE` (contrast W7: widgets' ImStb enclave is unconditional); LOCAL `push`/`pop` pragma fences per compiler with per-library attribution comments (`// (stb_rectpack) Dereferencing NULL pointer...`, `:104-124`); unity-build double implementation guards, each with its reason (`#ifndef STB_RECT_PACK_IMPLEMENTATION // in case the user already have an implementation in the _same_ compilation unit (e.g. unity builds)`, `:126-127`); and stb hooks rewired to house infrastructure — `STBRP_ASSERT`/`STBTT_assert` → `do { IM_ASSERT(x); } while (0)`, `STBRP_SORT` → `ImQsort`, `STBTT_malloc` → `((void)(u), IM_ALLOC(x))` (`:129-145`).
- **R3.** No Forward Declarations section: draw's only cross-section static decls sit at the CONSUMER site, each wrapped in a DUPLICATE of its definition's guard pair (`#if !defined(IMGUI_DISABLE_DEFAULT_FONT) && !defined(IMGUI_DISABLE_DEFAULT_FONT_BITMAP)` → `static const char* GetDefaultCompressedFontDataProggyClean(int* out_size);`, `:3165-3170`) — guard equivalence is part of the declaration.

### 16.2 Section system & ordering

- **R4.** Organization is TYPE-per-section — `[SECTION] <TypeName>` (ImDrawList, ImDrawListSplitter, ImDrawData, ImFontConfig, ImTextureData, ImFontAtlas..., ImFontBaked/ImFont) — sections track the type declaration order of the draw/font layer headers per A26, with the free-function tail (`ImGui Internal Render Helpers`) and data tails (Decompression, two font blocks) last. Method definitions within a section follow the type's header declaration order.
- **R5.** Mini-TOC decl lists are the MINORITY here: 5/18 sections (ImTriangulator, ImTextureData, ImFontAtlas+Builder, glyph-ranges helpers, Internal Render Helpers) — lists accompany free-function families and mixed sections; pure method-implementation sections carry none (contrast W4's 21/23). Banner prose slots carry cross-references (`// (imstb_truetype.h in included near the top of this file, when IMGUI_ENABLE_STB_TRUETYPE is set)` [sic], `:4690`) and license banners on the data sections (MIT + upstream URL, `:6373-6374`, `:6562-6563`).
- **R6.** Dash widths: 79 dominant (52 lines), 75 on the STB/backend/glyph-ranges banners (7 — intra-file era drift again, per T5/§12.7), plus TWO bare 127-wide rules used as unlabeled group separators inside the atlas section (`:3696`, `:3727`) — the corpus's only decorated sub-separators, contra F19; do not replicate.

### 16.3 Register & TU-local symbols

- **R7.** Context-light is measurable, not aspirational (I4): TWO `GImGui` references in 6,824 lines, both in atlas debug forensics (`ImFontAtlasDebugWriteTexToDisk` `:4130`, `ImFontAtlasDebugLogTextureRequests` `:4662` with the I29 `IM_UNUSED(g)` conditional-compilation idiom). Production draw/font paths receive everything by parameter or member.
- **R8.** The N21 subsystem-object tier at scale: 57 free `ImFontAtlas<Phase><Verb>` functions partitioned by phase stem — `Build` 21, `Texture` 11, `Pack` 7, `Baked` 9, `Font` 7, `Debug` 2 — beside 32 `ImFontAtlas::` methods; the stb loader backend keeps BACKEND register inside a core TU per I41 (`struct ImGui_ImplStbTrueType_FontSrcData` + `ImGui_ImplStbTrueType_*` statics, `:4696+`, whole block under `#ifdef IMGUI_ENABLE_STB_TRUETYPE`).
- **R9.** TU-local types: the `ImTriangulator`/`ImTriangulatorNode`/`ImTriangulatorNodeSpan` trio (A23 tier-prefixed, `:1844-1864`) + the backend-register FontSrcData; zero comparators, zero TU-local enums, 12 underscore-prefixed `ImDrawList::_*` method definitions (N16's internal-method tier lives here).
- **R10.** Layout-lock discipline: `IM_STATIC_ASSERT(offsetof(...))` cluster at ImDrawList init locking the ImDrawCmd↔ImDrawCmdHeader mirror (6 asserts, `:459-464`); the vendored-type variant locks stb's context against the internal.h opaque mirror (`sizeof(stbrp_context) <= sizeof(stbrp_context_opaque)`, `:4389`). Structs exploited by memcpy/aliasing get their assumptions asserted at first use, not documented in prose.
- **R11.** Vendored-copy zones inside first-party sections: `[SECTION] Decompression code` is copied stb.h code with provenance banner (`Decompression from stb.h (public domain) by Sean Barrett` + URL, `:6252-6258`) keeping stb dialect — snake_case statics AND the TU's only mutable file-scope statics (`stb__barrier_out_e`, `:6265+`) — an I17 exemption that exists ONLY inside such a provenance-marked zone. The two font-data sections follow A31 exactly, with paired fine-grained opt-outs (`..._FONT_BITMAP` for ProggyClean, `..._FONT_VECTOR` for ProggyForever) and tail static accessors.
- **R12.** A fourth debug register beyond A29's three: a whole dev-only utility checked in behind `#if 0` — `ImFontAtlasDebugWriteTexToDisk` WITH its own `#define STB_IMAGE_WRITE_IMPLEMENTATION` + out-of-tree relative include `"../stb/stb_image_write.h"` (`:4125-4135`). Tightly fenced, self-contained, reactivate-by-editing-one-line; not a license for unfenced dead code.

### 16.4 Measured micro-lexicon (draw-specific numbers)

- Definitions: `ImDrawList::` 57, `ImFontAtlas::` 32, `ImFont::` 9 among 138 method defs; `ImGui::` 16; statics 42.
- Loops: `for (int i` 17 vs `for (int n` 4 — draw REVERSES the F26 `n` preference (geometry/index loops); range-for 44, the heaviest of the three TUs.
- Tags: FIXME 48 — `-NEWATLAS` family 17 (11 bare + 4 `-NEWATLAS-V2` + 2 `-NEWATLAS-TESTS`: qualified tags themselves take sub-qualifiers), `-OPT` 12, `-NEWFONTS` 3; TODO 1 (`// TODO: Thickness anti-aliased lines cap are missing their AA fringe.`, `:820`); HACK 0.
- Dialect: `NULL` 166 / `nullptr` 0 / `auto` 0; `IM_ASSERT` 154 + `IM_ASSERT_PARANOID` 9 (the tier's main user); `IM_LIKELY` 3 (all glyph hot path, per I29); PVS `//-V` 5; `IM_COL32` literals 7 (texture data, per I39).
- Lines: max 313, >200 chars 25; double-blanks 0 (ties demo's hard ceiling; strictest of the three); issue refs 3.

### 16.5 Draw warts (imgui_draw.cpp's own; do not imitate)

1. **Index↔body drift, 3 counts**: body appends `(for stb_truetype and stb_rect_pack)` to the STB section name (`:89` vs `:8`); the body's `[SECTION] ImTextureData` (`:2458`) is MISSING from the index entirely (added with the 1.92 texture system, index never updated); index says `(ProggyForever.ttf)`, body `(ProggyForever-Regular-minimal.ttf)` (`:6560` vs `:24`).
2. The two 127-wide unlabeled separators (`:3696/:3727`, R6).
3. Decompression banner still claims base85 encoding (`:6256`) while BOTH font arrays are `-u8` decimal exports (`:6380`, `:6569`) — the rationale line outlived the format.
4. Banner typo `"imstb_truetype.h in included near the top"` (`:4690`).
5. The corpus's single `goto begin;` (`:6009`, §12.10) lives here.
6. Mutable `stb__barrier*` statics (`:6265+`) — licensed solely by the R11 provenance fence.

### 16.6 imguiapp mapping (draw)

Draw is the register model for any applayer region that is value-type machinery rather than context-coupled UI (the `ImApp*` animation/easing tier, `imguiapp_reflect.h` consumers): organize TYPE-per-section with methods in header declaration order (R4); reserve mini-TOC lists for free-function families (R5); keep the region context-light and measurably so — context access confined to debug forensics with `IM_UNUSED(g)` (R7); free-function APIs over a subsystem object take the N21/R8 phase-stem partition; assert layout assumptions beside the structs that carry them (R10); any embedded binary asset ships the full A31/R11 anatomy (license banner, provenance + regeneration command, paired opt-out macros, tail accessor, consumer-site fwd-decl with duplicated guards); vendored code enters only through an R2-shape fence (redirect macros, local pragma push/pop, unity-build guards, hooks rewired to `IM_ASSERT`/`IM_ALLOC`/`ImQsort` equivalents).

---

## 17. Backend Header Authoring Specification (`backends/imgui_impl_*.h` surveyed in depth)

§6's B1-B18 summarize backend conventions; this section specifies the backend *header* register — surveyed individually (all 19 backend headers + 2 sidecars read in full) and collectively (measured across the set). Same v1.92.9 WIP checkout. Corpus: 19 API headers, 34-287 lines (median ~55; `imgui_impl_vulkan.h` 287 is the maximal config-header case; `imgui_impl_null.h` 34 the minimal), plus sidecars `imgui_impl_opengl3_loader.h` (958, generated) and `imgui_impl_sdlgpu3_shaders.h` (406, data). Continues B-rule numbering from B18.

### 17.1 The collective skeleton (every header, in order)

- **B19.** Banner: line 1 `// dear imgui: <Role> Backend for <SDK>` — Role ∈ `Platform` | `Renderer` | `Renderer + Platform` (allegro5) | `Null Platform+Renderer` (null); line 2 states the required complement (`// This needs to be used along with a Platform Backend (e.g. Win32)`); optional parentheticals `// (Info: <what the SDK is>)` and `// (Requires: <min version>)` (`imgui_impl_glfw.h:3-4`). Then the B1 feature checklist, then the verbatim 4-link footer (19/19), then optional topic essays (`// About GLSL version:`, `imgui_impl_opengl3.h:25-28`) and integration warnings.
- **B20.** Checklist is a CONTROLLED VOCABULARY, not free prose — census: 87 `[X]`, 5 `[x]` (works-with-caveat, caveat stated in-line: `[Desktop OpenGL only!]`, `imgui_impl_opengl3.h:8`), 1 `[!]` (needs-attention: Vulkan's AddTexture indirection, `imgui_impl_vulkan.h:5`), 28 `[ ]` under `// Missing features or Issues:`. The recurring lines are byte-cloned across backends with only the SDK type substituted: texture-binding line always names the ImTextureID currency + `Read the FAQ about ImTextureID/ImTextureRef!` (13 files), VtxOffset line, HasTextures line, RenderState-expose line, multi-viewport line. Missing-entries may carry inline FIXMEs and rationale (`imgui_impl_android.h:10`). A new backend copies the census lines verbatim and substitutes its type.
- **B21.** Directive skeleton: `#pragma once` → `#include "imgui.h"      // IMGUI_IMPL_API` (6-space gap canon; wgpu's 10-space is drift) → `#ifndef IMGUI_DISABLE` → SDK includes/fwd-decls → the sentinel line `// Follow "Getting Started" link and check examples/ folder to learn about using backends!` (19/19, verbatim, directly above the first decl block) → lifecycle decls → optional groups → `[BETA]` RenderState struct → `#endif // #ifndef IMGUI_DISABLE`. Include-before-guard is canon at 16/19; glut, sdlrenderer2, vulkan invert to guard-before-include — tolerated drift, not a second convention.
- **B22.** SDK-type policy, exactly three strategies, chosen by cost: (a) FORWARD-DECLARE handle types — including SDK-exact oddities `typedef union SDL_Event SDL_Event;`, `union ALLEGRO_EVENT;`, `struct _SDL_GameController;` (`imgui_impl_sdl2.h:29-32`); (b) INCLUDE the SDK header when the API surface consumes full types, each include symbol-commented (`#include <dxgiformat.h> // DXGI_FORMAT`, `imgui_impl_dx12.h:25-26`; vulkan, sdlgpu3, wgpu); (c) ERASE to `void*`/`unsigned int` with the true type in a trailing comment — `ImGui_ImplWin32_GetDpiScaleForHwnd(void* hwnd);       // HWND hwnd` (`imgui_impl_win32.h:46`), `unsigned int CurrentSampler; // (GLuint)` (`imgui_impl_opengl3.h:76`) — used precisely to keep `<windows.h>`-class heavyweights out of user TUs (win32's `#if 0`-wrapped `WndProcHandler` extern with COPY-the-line instructions is the extreme form, `imgui_impl_win32.h:30-37`).

### 17.2 API-surface grammar

- **B23.** Declaration blocks: `IMGUI_IMPL_API` col 1, return type col ~16, name col ~25 (column re-chosen per file to fit — vulkan pads name col to 33, per F7's per-group re-choice); lifecycle order fixed Init(s) → Shutdown → NewFrame → [ProcessEvent/handler] → RenderDrawData; then labeled optional groups, each under its own comment header with a fixed tag lexicon: `(Optional)`, `(Advanced)`, `(Advanced, for X11 users)`, `[experimental]`, `[BETA]`. The `UpdateTexture` doc line is byte-identical in all 13 headers that declare it — cross-backend doc lines are CLONED, never paraphrased.
- **B24.** Backend headers speak the C++11 dialect (per B17/I25): `nullptr` default args in decls (`glsl_version = nullptr`, `manual_gamepads_count = -1`, `pipeline = VK_NULL_HANDLE`; 6 sites); header enums are one-liners `enum ImGui_ImplXXX_<Name> { ImGui_ImplXXX_<Name>_<Value>, ... };` with full N5 value grammar, immediately followed by their setter (`SetGamepadMode`, `SetMouseCaptureMode`; sdl2/sdl3 only). Config-mode enums carry usage essays with issue refs (X11 capture-mode debugger warning + `#3650`, `imgui_impl_sdl2.h:54-57`).
- **B25.** InitInfo structs live in the HEADER, three initialization idioms by era: memset-zero ctor (`ImGui_ImplDX12_InitInfo() { memset((void*)this, 0, sizeof(*this)); }`, dx12), C++11 default member initializers + ctor only for nested-struct seeding (wgpu, sdlgpu3), raw + `[Please zero-clear before use!]` doc contract (vulkan). Members grouped by blank lines + group comments (`// For Main viewport only` / `// (Optional) Dynamic Rendering`); callback slots take N3's `Fn` suffix (`SrvDescriptorAllocFn`); columnar per-platform value tables in comments where one field means different things (`// "cocoa"   | "wayland"   | "x11"     | "win32"`, `imgui_impl_wgpu.h:112-116`).
- **B26.** Deprecation machinery in headers mirrors A14 at backend scale: obsolete OVERLOADS under `#ifndef IMGUI_DISABLE_OBSOLETE_FUNCTIONS` with migration comments (`ImGui_ImplVulkan_AddTexture(VkSampler, ...) // Ignore VkSampler`, `imgui_impl_vulkan.h:168-170`; dx12's legacy Init `Obsoleted in 1.91.5`); obsolete struct MEMBERS as commented tombstones with arrow pointers (`//VkRenderPass RenderPass; // --> Since 2025/09/26: set 'PipelineInfoMain.RenderPass' instead`, `imgui_impl_vulkan.h:128-131`) or guarded legacy members (`LegacySingleSrvCpuDescriptor`, dx12); renamed functions as commented one-line tombstones (`// Renamed in 1.91.0`, `imgui_impl_glfw.h:44`).
- **B27.** The `[BETA]` RenderState struct (9 renderer headers): fixed three-line doc block (`[BETA] Selected render state data shared with callbacks.` / storage location / `(Please open an issue if you feel you need access to more data)`), plain struct of SDK handles; commented-out members carry their replacement API (`//ID3D11SamplerState*   SamplerLinear; // Use ImDrawList::AddCallback(...)`, `imgui_impl_dx11.h:49-50`); opengl3 adds the only in-header accessor (`static inline ... GetRenderState()`, `imgui_impl_opengl3.h:80`).
- **B28.** Header-owned configuration: config macros documented as commented-out `//#define IMGUI_IMPL_<SDK>_<OPTION>` with per-line comments and the imperative `// Reminder: make those changes in your imconfig.h file, not here!` (`imgui_impl_vulkan.h:33-52`, opengl3, wgpu); ACTIVE preprocessor logic is legal in headers for auto-detection and derivation only — opengl3's ES2/ES3 platform detect (`:52-67`), wgpu's Emscripten default (`:38-41`), vulkan's loader redirect + `NOMINMAX` guard + feature-presence derivation (`IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING`, `:60-79`). Warning pragmas in headers exist ONLY as push/pop pairs (A3 header rule): vulkan + dx12, both suppressing exactly `-Wold-style-cast` for their SDK-handle casts.
- **B29.** Dual-language headers (metal, osx): `#ifdef __OBJC__` block declaring the API in ObjC dialect — `@class`/`@protocol` forward decls, `id<MTLDevice>` params, SDK-register camelCase parameter names (`renderPassDescriptor`), nullability annotations (`NSView* _Nonnull`), and (metal only) multi-line param wrapping aligned under the open paren — then a separate `#ifdef IMGUI_IMPL_METAL_CPP` / `#ifndef __OBJC__` block RE-DECLARING the identical function set with `MTL::`/`void*` types under dashed `// C++ API` banners. SDK dialect wins inside the SDK-facing block; the function SET stays identical across both.

### 17.3 Tiers, siblings, sidecars

- **B30.** Helper tiers above the lifecycle contract are explicitly disclaimed: vulkan's `ImGui_ImplVulkanH_*` tier (H-suffixed prefix) sits under a dashed banner stating `WE DO NOT PROVIDE STRONG GUARANTEES OF BACKWARD/FORWARD COMPATIBILITY` + the two reasons it exists + "use your own custom tailored code" (`imgui_impl_vulkan.h:187-206`); its structs repeat the B6 memset ctor + seeded-defaults shape IN the header (`ImGui_ImplVulkanH_Window()`, `:264-280`, with per-default rationale comments: `// Ensure we get an error if user doesn't set this.`). wgpu's `// Internal Helpers` tier is the same idea (surface/debug-print helpers, `:87-119`).
- **B31.** Sibling-clone discipline: version twins (dx10/dx11, sdl2/sdl3, sdlrenderer2/sdlrenderer3) are maintained as near-verbatim textual clones — the dx10↔dx11 normalized diff is 5 hunks, ALL substantive (DeviceContext param, RenderState section); prose, ordering, and doc lines are otherwise byte-identical. Maintenance mechanism is diff-minimality: fix one twin, port the hunk verbatim. Deliberate steering essays are allowed to diverge (`sdlrenderer2.h`'s "IMHO is now largely obsolete" advice block; glut's `!!! GLUT/FreeGLUT IS OBSOLETE PREHISTORIC SOFTWARE ... you are being abused` shout, `imgui_impl_glut.h:4-6`).
- **B32.** Sidecar headers are register exceptions with mandatory provenance: `_loader.h` = generated+vendored — dashed `// About <file>:` banner with SHOUTED user warnings (build-error triage in caps), the exact regeneration command (`python3 gl3w_gen.py --output ...`), and upstream repo links, followed by the generator's own license block (`imgui_impl_opengl3_loader.h:1-26`); `_shaders.h` = data — `#pragma once` + `IMGUI_DISABLE` guard + `<stdint.h>`, provenance command (`binary_to_compressed_c.exe -u8 -nocompress ... With some manual pasting.`), pointer to the shader-source folder, then packed-decimal `const uint8_t spirv_vertex[1732]` arrays whose lowercase snake_case names are the data-register exception (`imgui_impl_sdlgpu3_shaders.h:1-10`). Neither carries the B19 banner/checklist — they are not backends.

### 17.4 Per-header facts worth knowing (individual survey)

| Header | Lines | Distinguishing facts |
|---|---|---|
| `null.h` | 34 | No checklist (nothing to claim); THREE prefix families in one header (`ImGui_ImplNull_` aggregate + `ImGui_ImplNullPlatform_`/`ImGui_ImplNullRender_` splits), each under a role comment |
| `android.h` | 38 | Banner says "Platform Binding" not "Backend" (terminology drift); `int32_t` return on `HandleInputEvent` (SDK convention); `// Important:` steering block with FIXMEs |
| `dx9.h` | 38 | Only renderer advertising `IMGUI_USE_BGRA_PACKED_COLOR` support in checklist; no RenderState struct |
| `opengl2.h` | 45 | Post-footer DO-NOT-USE essay (`**DO NOT USE THIS CODE IF YOUR CODE/ENGINE IS USING MODERN OPENGL**`) with technical rationale |
| `allegro5.h` | 46 | The one combined `Renderer + Platform` backend; `SetDisplay` extra lifecycle; checklist interleaves Renderer/Platform tags |
| `glut.h` | 48 | Guard-order variant; obsolescence shout; callback table with dashed column header mapping GLUT names → `// ~ <Decent Name>` |
| `dx10.h`/`dx11.h` | 50/53 | B31 twins; dx11 adds DeviceContext + RenderState |
| `win32.h` | 54 | `#if 0` extern WndProcHandler (B22c extreme); optional groups: DPI helpers + alpha compositing `[experimental]` |
| `sdlrenderer2/3.h` | 56/55 | B31 twins; guard-order variant in 2 only; anti-recommendation essay |
| `sdl2.h`/`sdl3.h` | 61/57 | B31 twins; the only header enums (B24); capture-mode X11 essay |
| `osx.h` | 58 | Dual-API; nullability annotations; C++ variant erases `NSView*`→`void*` |
| `sdlgpu3.h` | 63 | Extra MANDATORY lifecycle step `PrepareDrawData` warned in banner; banner text says `ImGui_ImplSDLGPU_` while API is `ImGui_ImplSDLGPU3_` (prefix drift wart) |
| `glfw.h` | 73 | Per-decl `// Since 1.84` version stamps; 4 labeled callback groups; `#ifdef __EMSCRIPTEN__` decl; trailing double blank (wart) |
| `metal.h` | 79 | Dual-API; the only multi-line wrapped param lists (SDK dialect) |
| `opengl3.h` | 82 | Active ES detect logic; erased-GLuint RenderState; in-header `GetRenderState()` |
| `dx12.h` | 90 | SDK includes w/ symbol comments; clang push/pop; InitInfo with Fn-slots + guarded legacy members + obsolete Init overload |
| `wgpu.h` | 121 | Backend-selector config macros; unexported helper tier (9 decls missing `IMGUI_IMPL_API` — wart vs B4); CreateSurfaceInfo columnar comments |
| `vulkan.h` | 287 | Maximal: `[Configuration]` numbered blocks, volk redirect, derived feature macro, pool tunables, PipelineInfo/InitInfo with tombstones, H-tier, header pragma push/pop |

### 17.5 Backend-header warts (do not imitate)

1. Guard-order inversion in glut/sdlrenderer2/vulkan (B21) — 16/19 canon is include-first.
2. wgpu's Internal Helpers tier omits `IMGUI_IMPL_API` on all 9 decls (`imgui_impl_wgpu.h:93-118`) — violates B4; helpers are still public symbols.
3. `sdlgpu3.h` banner names the API `ImGui_ImplSDLGPU_PrepareDrawData` while declaring `ImGui_ImplSDLGPU3_PrepareDrawData` (`:22` vs `:45`).
4. `android.h` "Platform Binding" vs the corpus's "Platform Backend"; `wgpu.h` line 2 says "Platform Binding" too — older vocabulary, both uncorrected.
5. `glfw.h` double blank line before `#endif` (`:71-72`); wgpu's 10-space include-comment gap.
6. `null.h` lacks the checklist block entirely — acceptable only because nothing is implemented; not a template for real backends.

### 17.6 imguiapp mapping (backend headers)

`imguiapp_impl_*` headers bind B19-B32 directly: banner role line + complement statement + checklist with the B20 controlled vocabulary (substituting applayer feature lines); B21 skeleton with `imguiapp.h` in the include slot and include-before-guard order (the 16/19 canon, not the variant); SDK types by B22's three-strategy rule; one InitInfo struct per B25 (pick ONE init idiom — memset ctor — for consistency with A19/P8 rather than the era mix); deprecations per B26; any helper tier above the lifecycle contract gets the B30 no-guarantee banner; sidecar assets (shaders, loaders) follow B32 provenance anatomy.
