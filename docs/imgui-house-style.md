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

- **N1.** `ImGui*` prefix = GUI/context-layer types (`ImGuiContext`, `ImGuiIO`, `ImGuiStyle`, `ImGuiWindow`, `ImGuiTable`). `Im*` bare prefix = lower-level standalone types: draw/font/math/containers (`ImVec2`, `ImRect`, `ImDrawList`, `ImFont`, `ImVector<>`, `ImPool<>`). (`imgui.h:172-217`)
- **N2.** Scalar aliases: `Im` + `S`/`U` + bit width: `ImS8`…`ImU64` (`imgui.h:162-170`). Domain index aliases use `Idx` suffix over a scalar alias: `typedef ImS16 ImGuiTableColumnIdx;` (`imgui_internal.h:223-224`).
- **N3.** Function-pointer typedefs: `*Callback` = user-supplied hook (`ImDrawCallback`, `ImGuiInputTextCallback`); `*Func` = settable function slot (`ImGuiMemAllocFunc`); `*Fn` suffix on function-pointer *members* of platform IO (`Platform_GetClipboardTextFn`, `imgui.h:4231`); `Platform_`/`Renderer_` domain prefixes on viewport handler members (`imgui.h:4284`).

### 2.3 Enums

- **N4.** Two shapes: (a) `typedef int ImGuiXxx;` + `enum ImGuiXxx_` (trailing underscore) — the default, used for all flags and most indices; (b) real typed `enum ImGuiXxx : int` only when a storage size matters or values are stored typed (`ImGuiKey`, `ImGuiDir`, `ImGuiSortDirection : ImU8`; `imgui.h:225-228`). Stated rationale: strongly typed enums "add constraints (can't extend in private code, can't store typed in bit fields, extra casting on iteration)" (`imgui.h:220`).
- **N5.** Values named `ImGuiTypename_ValueName` (full type stem + `_` + PascalCase descriptor): `ImGuiWindowFlags_NoTitleBar` (`imgui.h:1217`).
- **N6.** Every enum starts `ImGuiXxx_None = 0` (exception: `ImGuiDir_None = -1`, §12). Index enums end with `ImGuiXxx_COUNT` (uppercase). Ranges use `_BEGIN`/`_END` sentinels (`ImGuiKey_NamedKey_BEGIN`, `imgui.h:1629`).
- **N7.** Flag enums use `1 << n`, no `_COUNT`. Named OR-combinations reuse the prefix (`ImGuiWindowFlags_NoDecoration = ...|...`, `imgui.h:1238`). Masks and internal default sentinels take a **trailing underscore**: `ImGuiMod_Mask_`, `ImGuiButtonFlags_PressedOnMask_`, `ImGuiButtonFlags_PressedOnDefault_` (`imgui.h:1741`, `imgui_internal.h:1075-1076`).
- **N8.** Internal enum extension: public enums end with an `// [Internal]` block ("Don't use!" comments), or a separate `enum ImGuiXxxFlagsPrivate_` in imgui_internal.h whose *values keep the public* `ImGuiXxxFlags_` prefix, preceded by `// Extend ImGuiXxxFlags_` (`imgui_internal.h:1054-1076`).

### 2.4 Functions

- **N9.** Public API lives in `namespace ImGui`, unprefixed, PascalCase, verb-first. Matched-pair families: `Begin/End`, `Push/Pop`, `Get/Set`, `Open/Close`; query prefixes `Is`/`Has`/`Want`; debug windows `Show*`; domain groups share a noun prefix (`Table*`, `Tree*`, `Dock*`).
- **N10.** Suffixes: `Ex` = richer-parameter engine variant (usually internal; public `Button` wraps internal `ButtonEx`, `imgui_widgets.cpp:828-831`); `V` = va_list twin of every variadic function (`TextV`); axis suffixes `X`/`Y` (`SetScrollHereX/Y`, `PushStyleVarX/Y`); arity digits (`DragFloat2/3/4`); `Scalar`/`ScalarN` for type-erased variants; `V` prefix for vertical orientation (`VSliderFloat`).
- **N11.** Low-level helpers use bare `Im` prefix and are a strictly lower layer: "ImGui functions or the ImGui context are never called/used from other ImXXX functions" (`imgui_internal.h:356-358`). Families: `ImHash*`, `ImStr*`, `ImText*`, `ImFormat*`, `ImMin/ImMax/ImClamp/ImLerp/ImTrunc/ImFloor`.
- **N12.** File-local static helpers: PascalCase; take `Im` prefix when low-level (`static const char* ImAtoi`); template helpers take `T` suffix (`DataTypeClampT`); getter tables use underscore sub-namespacing (`Items_ArrayGetter`).

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

- **N20.** Core: `imgui*.{h,cpp}`; backends: `imgui_impl_<sdk>.{h,cpp}` (+ sidecars `_loader.h`, `_shaders.h`); vendored stb: `imstb_*` prefix with `[DEAR IMGUI]` modification markers and grep anchor (`imstb_truetype.h:1-4`).

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

---

## 4. File & Header Architecture

### 4.1 Universal file skeleton

- **A1.** Every file: `// dear imgui, vX.Y.Z WIP` + parenthetical role line → Help/resource links block (dotted-leader aligned table, `imgui.cpp:9-23`) → `Index of this file:` comment listing every `[SECTION]` in order → (`#pragma once` for headers) → `#ifndef IMGUI_DISABLE` wrapping the entire body → warning-pragma push block → sections → pragma pop → `#endif // #ifndef IMGUI_DISABLE`.
- **A2.** `[SECTION]` markers recur verbatim in the body matching the index 1:1; the instruction "search for [SECTION] in the code to find them" is embedded (`imgui.cpp:63`).
- **A3.** Warning-pragma blocks, per compiler, order MSVC → clang → gcc, each disabled warning with a trailing reason comment; clang block guards `-Wunknown-warning-option` via `__has_warning` (`imgui_tables.cpp:207-247`). Matching pops at EOF.

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
- **I16.** ImDrawList internals: Path API for shapes, `PrimReserve/PrimRect` fast path for sharp rects, zero-alpha early-out `if ((col & IM_COL32_A_MASK) == 0) return;` before any geometry (`imgui_draw.cpp:1523`).

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

---

## 8. Demo Conventions (`imgui_demo.cpp` deliberately differs)

The demo is documentation-you-copy-paste and inverts several core rules — stated verbatim at `imgui_demo.cpp:47-59`:

- **D1.** Never omit the `ImGui::` prefix (core omits it internally).
- **D2.** Function-local `static` variables ARE used, close to their usage — the "static sermon" explains this is a demo-only concession (`imgui_demo.cpp:33-46`).
- **D3.** Never use internal helpers/facilities — public API only.
- **D4.** Never use ImVec2/ImVec4 math operators (can't assume user enabled them).
- **D5.** `HelpMarker("...")` `(?)` tooltips as in-UI documentation; `IMGUI_DEMO_MARKER(section)` wires code locations to interactive browsers; huge `[SECTION]` index; "find a visible string and grep for it" navigation.
- **D6.** Self-contained; strippable by linker; "PLEASE DO NOT REMOVE THIS FILE" preamble.

**Implication for imguiapp:** demo/example code (imguix_demo.cpp etc.) should follow D-rules; library code must follow §7 — do not blend the registers.

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
6. `ImGuiDir_None = -1` breaking the `_None = 0` rule.
7. Section-separator width drift (79 vs 75 chars across files).
8. Default-arg `=` spacing occasionally glued (`flags=0`, `imgui.h:471-472`).
9. `GetCursorPos()` naming admitted "confusing" (`imgui.cpp:12221`).
10. `ImGuiButtonFlags_AlignTextBaseLine` — "hack, since it shouldn't be a flag" (`imgui_widgets.cpp:799`).
11. stb-interfacing code keeps upstream snake_case — vendored-boundary exception, not a precedent.
12. `NULL` (core) vs `nullptr` (backends) split — historical; match the layer.
