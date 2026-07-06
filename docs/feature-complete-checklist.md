# Feature Checklist (v2 — post-100)

Forward work, detailed to the file / struct / function / test level. The completed F01–F78.5 series is in
[archive/feature-complete-checklist.md](archive/feature-complete-checklist.md) (closure:
[feature-audit-2026-07-05.md](feature-audit-2026-07-05.md)). Design-first for high-blast-radius phases
(matching how F53/F61/F66/F76 gated code on a doc). "Green" = imguix-tests + imguix-core-tests +
imguix-headless-verify (`cmake --build build/vs2026 --config Release --target imguix-tests imguix-core-tests
imguix-headless-verify`). Naming: `AppXxx` free functions in `namespace ImGui`; graph-model types are
aggregates (no ctors); tests `stepNN_*`/`canvas_*` (nodes), `Test_*` (core), `composer_*` (headless).

The v2 arc, in order (later phases gated on earlier): **A** lean/mean split → **B** embed control source +
real-code presence + write-back fold → **C** refactor toward the imgui.h canonical schema → **D** UI
design-language canon + HTML help + bug-button → **E** phase-coherence re-audit/hardening → full UI redesign.

## Done since 100%

- [x] **V0 DLL preview in-panel render** — the compiled DLL app's frame renders inside the Composer Preview
  panel, closing the F78/F78.5 "see it live" residual. Drawdata-serialization + host CPU-rasterization: the
  emitted module gained three additive `extern "C"` exports (`ImGuiAppPreview_SetDisplaySize` /
  `_CopyFontAtlas` / `_CopyDrawData`; ABI `20260705→20260706`, required at load); after `_Tick` the host
  copies `ImDrawData` + atlas out and `AppPreviewDllRasterizeFrame` (`imguiapp_preview_dll.cpp`) fills a
  panel RGBA buffer, blitted via `ComposerUploadRgbaTexture` + `ImGui::Image()` (`imguiapp_demo.cpp`) behind
  an Interp/DLL toggle. `Test_dll_preview_inpanel_render` (core). core 414/0, nodes 112/112, headless 31/31.
  Residual → folds into B: host input forwarding (DLL widgets not yet interactive in-panel).

## A — "lean & mean" split + opt-out  (design-first: `lean-tools-split-design.md`)

Ordered tasks (build order; one deliverable + one gate each). Design doc: `lean-tools-split-design.md`.
Keep the per-topic `.cpp` split (imgui precedent) — NO `imguiapp_widgets.cpp`. **Facts below are verified
against the tree so an implementor edits, not explores.**

**THE CLASSIFICATION RULE (decided): if it's UI, it's a tool; anything else is NOT a tool.**
- **TOOL (gated by `IMGUIX_DISABLE_TOOLS`) = UI only:** the Composer (`imguiapp_demo.cpp`), the graph EDITOR
  UI (`ShowAppGraphEditor`/`ShowAppGraphTree` etc. in `imguiapp_nodes.cpp`), the canvas UI
  (`imguiapp_canvas.cpp`), the preview SURFACE (F68 widget surface in `imguiapp_preview.cpp` +
  `imguiapp_preview_dll.cpp`), the playback transport UI + bug-button UI (in `imguiapp_demo.cpp`).
- **CORE (always on, NOT UI):** the graph MODEL + validate + serialize + CODEGEN (`AppGraph*`, `GenerateApp*`);
  the RECORDER + encoder + meta-write (`AppRecord*`, `AppMetaRecord*`, `ImGuiAppRecorder`,
  `ImGuiAppMetaRecorder`, `ImGuiAppAVEncoder`); the DECODER/LOADER/playback read side (`AppRunOpen`/`Close`/
  `TickCount`/`TickAt`/`StateAtTick`, `ImGuiAppRunIndex`/`ImGuiAppRunTransport`); the interpreter CORE (F67
  headless build+run in `imguiapp_preview.cpp`); the anim builtins; the AV codec (`qoi`/`libav`/`mediafoundation`).
- **`imguiapp_internal.h`** (Option-1, always-available) holds the TOOL-UI + internal-helper interfaces; the
  core model/codegen/recorder/decoder/interpreter interfaces stay in core headers (`imguiapp.h` / a core
  model header / core `imguiapp_av.h`).

**⚠ MIXED FILES — the real work.** `imguiapp_nodes.cpp` (graph model + codegen = core; editor UI = tool) and
`imguiapp_preview.cpp` (interpreter core = core; F68 surface = tool) each hold BOTH. Under the rule they must
be split UI-out, not gated wholesale (A2/A3 below). `imguiapp_canvas.cpp` + `imguiapp_demo.cpp` are UI-only →
gate wholesale. `imguiapp_av.cpp` is record+decode (core) with no UI → stays core (its record impl may move
into `imguiapp.cpp`); any transport/record-button UI it holds moves to the demo.

**Parallelism (Phase A):** A1 (config+CMake) is INDEPENDENT. The UI-extraction splits (A2 nodes, A3 preview)
are per-file independent → parallel. A4 (gate the UI-only TUs) parallelizes per file. A5→A6 serial tail.
Lane picture: `A1 ∥ (A2 ∥ A3) → A4×n → A5 → A6`.

**STATUS — DONE (2026-07-06), gated-region mechanism (imgui's `IMGUI_DISABLE` pattern).**
- **A1**: `IMGUIX_DISABLE_TOOLS` documented in `imapp_config.h` (switch-only imconfig.h analog; the frame/app
  config structs live in `imguiapp.h`); CMake `IMGUIX_ENABLE_TOOLS` (default ON) → OFF appends the define.
- **A2**: 26 `#ifndef IMGUIX_DISABLE_TOOLS` UI spans in `imguiapp_nodes.cpp` (row widgets, theme→blender
  widgets, geometry/placement, bindings/hover, editor render, inspector, scope interior, outliner, composer
  control methods); model+codegen+persistence+import+undo+prefabs+live-mirror stay ungated. Canvas-coupled
  seams (`AppGraphPlaceNode`, `AppNodeModelSize`, `AppCanvasNodePos/SetNodePos/Zoom/Scale`) get lean `#else`
  stubs so the CORE factory / tidy / live-mirror / import paths link with no canvas.
- **A3**: F68 preview SURFACE gated in `imguiapp_preview.cpp` (3 spans: field widgets, the OnRender surface
  call, the surface+brushing public API); F67 interpreter core (`AppPreviewCreate/Frame/Reconcile`, evaluator)
  ungated. 5 surface decls moved `imguiapp_preview.h` → `imguiapp_internal.h`.
- **A4**: `imguiapp_internal.h` (imgui_internal.h analog) holds the relocated tool-UI decls (from imguiapp.h:
  `AppLayerDemoGraph`, `SetAppCodeFont`; + preview surface) under `#ifndef IMGUIX_DISABLE_TOOLS`; UI-only TUs
  wholesale-gated (`imguiapp_canvas.{cpp,h}`, `imguiapp_demo.cpp`, `imguiapp_preview_dll.{cpp,h}`); consumers
  rewired to `internal.h`.
- **A5**: lean lib (`-DIMGUIX_ENABLE_TOOLS=OFF`) compiles with ZERO undefined UI symbols. Symbol scan:
  editor/surface (`ShowAppGraphEditor`, `AppPreviewRender`, `AppDrawScopeWalls`, `AppGraphShowKeymapEditor`)
  = 0 in the lean `imguix.lib`; core (`GenerateAppGraphCode`, `AppGraphValidate`, `AppPreviewCreate`) in both.
  Lean lib −3.9 MB.
- **A6**: both configs green — tools-ON suites **112/112 · 414/0 · 31/31** (codegen byte-locks intact,
  behavior-neutral); lean core lib compiles + archives. Residual: a dedicated minimal lean *host exe* +
  `strings` scan not built (verified at lib/symbol level; demo/composer are the only exe, fully tool-gated).

- [x] **A1. switch + CMake option** — `// #define IMGUIX_DISABLE_TOOLS` documented in `imguiapp_config.h`;
  CMake option `IMGUIX_ENABLE_TOOLS` (default ON) → OFF appends `IMGUIX_DISABLE_TOOLS` to
  `IMGUIX_COMPILE_DEFINITIONS` (`imguix/CMakeLists.txt`). Independent of the header work.
  *Accept: default build unchanged + green; `-DIMGUIX_ENABLE_TOOLS=OFF` shows the define in the compile defs.*
- [x] **A2. split the graph editor UI out of `imguiapp_nodes.cpp`** — separate the UI (`ShowAppGraphEditor`,
  `ShowAppGraphTree`, canvas/inspector/panel rendering — grep `ShowAppGraph|EditApp|^\s*static void .*Draw`)
  from the MODEL/CODEGEN (`AppGraph*`, `GenerateApp*`, serialize/validate — stay core). UI goes to a gated
  `.cpp` (e.g. `imguiapp_nodes_ui.cpp`) or a gated region; model/codegen stay in a core TU. Move UI decls to
  `imguiapp_internal.h`, model/codegen decls to a core header.
  *Accept: tools-ON green + codegen corpus byte-identical; with `IMGUIX_DISABLE_TOOLS` the model+codegen still
  compile (a core-only TU links `AppGraphAddNode`/`GenerateAppGraphCode`), the editor UI is gone.*
- [x] **A3. split the preview SURFACE out of `imguiapp_preview.cpp`** — the F68 widget surface + brushing UI
  (grep `AppPreviewSetSurface|AppPreviewRender|ImGui::` widget calls) is tool; the F67 interpreter core
  (`AppPreviewCreate`/`Frame`/`Reconcile`, the evaluator) is core. Same split shape as A2.
  *Accept: tools-ON green; lean build keeps the interpreter core linkable, surface UI gone.*
- [x] **A4. `imguiapp_internal.h` + gate the UI-only TUs** — create `internal.h` (imgui_internal.h analog):
  fold in the tool-UI interfaces (from A2/A3 + `imguiapp_canvas.h` + the demo/transport/bug UI decls), and
  wrap those UI decls in `#ifndef IMGUIX_DISABLE_TOOLS` so the macro HIDES the UI in the header too (a lean
  consumer sees no UI API). The core model/codegen/recorder/decoder/interpreter/anim interfaces do NOT live in
  `internal.h` (they stay in unconditional core headers). Move `imguiapp.h`'s 3 tool-coupled decls out (`:71`
  fwd `ImGuiAppGraph`, `:457` `AppLayerDemoGraph`, `:483` `SetAppCodeFont`). Wrap the UI-only TUs in
  `#ifndef IMGUIX_DISABLE_TOOLS`:
  `imguiapp_canvas.cpp`, `imguiapp_demo.cpp`, the A2/A3 UI TUs, `imguiapp_preview_dll.cpp` surface. Rewire
  includes (grep `-rln '#include "imguiapp_nodes.h"'` etc. across `imguix/imguiapp tests tools imguix-demo`;
  includers today: nodes→{nodes,canvas,demo,preview.h,preview_dll.h,core/flow/nodes/codegen_proof tests,
  headless_verify}; canvas→{canvas,nodes,demo,nodes_tests,headless_verify}; preview→{preview,demo,core/flow/
  nodes tests}; preview_dll→{preview_dll,demo,core_tests}; anim→{core_tests}).
  *Accept: tools-ON green; `grep -nE 'ShowAppGraph|AppPreviewSetSurface' imguiapp.h` empty; a UI-only TU
  spot-compiled with the macro yields no symbols.*
- [x] **A5. verify core→tool severance** — attempt the lean link; anything UI pulled from core is relocated.
  Baseline: `imguiapp.cpp` had ZERO tool refs pre-split.
  *Accept: lean config links with zero unresolved UI symbols from core.*
- [x] **A6. lean build config + verify** — CMake lean path (`-DIMGUIX_ENABLE_TOOLS=OFF`): core lib (model +
  codegen + recorder + decoder + interpreter core) + a minimal host, no composer/demo. Build + link.
  *Accept: lean build compiles + links; `strings <lean-binary>` shows NO embedded control/source text; the
  tools-ON suites still 112/409/31 green (both configs pass).*

## B — embed control source + real-code presence + the write-back fold  (design-first: `source-embed-design.md`)

- [ ] **B. real source, not fake skeletons** — with control source baked into the tools build, the Code tab
  and debugger show the REAL function bodies (kill the perceived generated skeleton), and edits flow back to
  disk. Gated on A's switch (lean build excludes it).
  - **Embed scope (decided)**: the CONTROLS' source only (where F78.5 bodies + write-back live) — NOT the
    framework or whole repo. Format (byte-array `.cpp` vs resource) + finer subset: B's doc.
  - **Symbol→source index**: map a control / command / node → its real file + declaration + body span (the
    lookup behind hover, the Code tab, and write-back).
  - **Real-code presence**: Code tab shows the real body (vs generated); rich contextual presence — hover a
    node/control → its actual source; the debugger shows real code at a tick; design-time preview references
    real impls. (F78.5 hand-written bodies are already real; this generalizes to embedded control source.)
  - **THE FINAL FOLD (write-back)**: edit a control's OnRender (F78.5) body → test live in the previewer/DLL
    (V0) → the tool uses the source index to update the REAL file on disk at the mapped span. Inverse of the
    F22–F24 import edge → full bidirectional source↔graph↔source. Tiers: (1) **method bodies** map to one
    span, surgical replace — first target; (2) **structural** edits (field/dep/event) regenerate skeleton —
    harder, later. Destructive → a **diff/confirm gate** before touching disk + index-staleness handling
    (source edited outside the tool).
  - Also folds V0's residual: forward host input into the DLL preview so in-panel widgets are interactive.
  *Accept: (per B's doc) real control bodies render in the Code tab from the embedded index; a tested
  method-body edit writes back to the real source at the correct span behind a diff/confirm; a headless test
  drives the write-back + re-reads the file.*

## C — refactor toward the imgui.h canonical schema  (design-first: `refactor-plan.md`)

- [ ] **C. re-canonicalize structure + comments** — `imguiapp.h` drifted from the `imgui.h` spirit it
  originally emulated (structure, ordering, comment discipline). Realign; pure structural, ZERO behavior change.
  - **Target schema** = `imgui.h`'s: `[SECTION]` banners, fixed decl order (context → main API → config/style
    → … → structs last), the public/internal split (now backed by A's `imguiapp_internal.h`), terse
    behavior-stating comments.
  - **Reorder** declarations in the headers + definitions in every `.cpp` to match; **consolidate/remove**
    dead + duplicated code; **strip AI-written / narrative comments** (keep behavior/constraint only).
  - **Reference** (OPEN fork): the "canonical" commit era to aim at — `git log` for the last point
    `imguiapp.h` closely mirrored `imgui.h`; map the drift in `refactor-plan.md` before the big diff.
  *Accept: all suites green + codegen corpus byte-identical after the reshuffle (behavior-neutral, F77-style);
  the drift map + target section layout + comment/order rules land in the doc first.*

## D — UI design-language canon · HTML help · bug-button  (design-first per item)

- [ ] **D1. design-language canon** — formalize icon↔meaning + the standing idioms (no glyph buttons, fixed
  metric columns, theme/DPI-invariant chrome, the F38 motion table) as a spec + likely a code-level
  icon/semantic registry so usage can't drift.
  *Accept: a design-language doc + (if built) a registry a test iterates for icon/semantic uniqueness.*
- [ ] **D2. rich HTML help** — generated help. OPEN fork: source of truth (the F34 command registry +
  node-kind/tooltip metadata → generated HTML) and embedded-in-tools (rides B) vs external artifact.
  *Accept: generated HTML covers the command/verb + node-kind surface; regenerates from the registry.*
- [ ] **D3. bug-button** — a toolbar toggle: ON records EVERYTHING (inputs + frames + state — the F61
  container the harness already writes via `AppMetaRecordBegin`/`AppMetaRecordEnd`, `imguiapp_av.*`) until
  toggled OFF; tag the segment with a typed description + metadata; save as a playable bug artifact the
  playback debugger (F62–F65) opens. Encoding = ground truth of what happened. Rides existing AV/playback
  rails → mostly a toggle + a description field + a save path + a small UI.
  *Accept: `composer_bug_button` records a session, tags it, writes the container, and `AppRunOpen` reads it
  back with the description metadata; scrub matches.*

## E — phase-coherence re-audit + hardening → full UI redesign  (final)

- [ ] **E1. phase-coherence hardening** — re-audit `phase-coherence.md` after C reshuffles structure; consider
  formalizing the coherence invariant as a TEST (not just an audit doc); enforce "no tolerance constant
  without a named noise source."
  *Accept: coherence re-audit doc updated; the invariant, where formalizable, is a green test.*
- [ ] **E2. full UI redesign** — redesign the entire Composer UI once everything above is stable/final.
  Deliberately open until then.
  *Accept: TBD when E1 lands.*

## Outstanding ambiguities (B–E) — must resolve before each phase's code

Phase A is fully specified (design doc + A1–A6, no open forks). Everything below is UNRESOLVED.

**B — source embed + write-back** (`source-embed-design.md`):
- Embed FORMAT: generated byte-array/string-table `.cpp` (compiled into the tools build) vs a loaded resource.
- Embed SUBSET: "controls' source" = bodies only, or the whole control TU + its headers/struct defs?
- INDEX build mechanism: clang tooling vs a light C++ range-scan keyed off the reflected type/method names vs
  a build-emitted manifest. Must regenerate at build (survive Phase C's reorder — spans are offsets).
- CODE TAB presentation: show the real body only, generated only, or both side-by-side (real="what runs",
  generated="what ships")?
- WRITE-BACK conflict UX: behavior when the on-disk source drifted from the index (refuse? re-index? merge?).
- WRITE-BACK tier 2 (structural edits — add field / rewire dep / new event): how to reconcile regenerated
  skeleton against hand-touched real source (tier 1 = method-body span-replace is clear; tier 2 is not).
- INTERACTIVE preview input (folds V0's residual): how host input reaches the DLL preview so in-panel widgets
  are interactive (needed for "test it live" to be real interaction).

**C — refactor to imgui.h schema** (`refactor-plan.md`; reference commit `9616693` `imgui_applayer.h` RESOLVED):
- The DRIFT MAP itself is not yet produced (per-file, per-`[SECTION]` enumeration of out-of-order decls +
  bloat/dup + AI-comment spans vs `9616693`).
- PASS GRANULARITY: per-file vs per-`[SECTION]` commits (proposed reviewable passes; not locked).
- COMMENT CRITERIA: the exact test for "AI-written / narrative comment to strip" vs "behavior/constraint to
  keep" — needs a rule an implementor applies uniformly.
- Does C also CONSOLIDATE `.cpp` topic-file boundaries, or only reorder WITHIN existing files?
- Relationship to A's mixed-file split: does C do any further model/UI separation beyond A2/A3, or is that
  fully A's job?

**D — UI canon · HTML help · bug-button** (no design docs yet — each needs one):
- D1: is the icon↔semantic registry a CODE artifact (a table a test iterates) or a doc? What is the icon set
  / source of the mapping? Which existing idioms are formalized vs left as convention?
- D2: HTML help SOURCE OF TRUTH (command registry F34 + node-kind/tooltip metadata? the design docs?);
  EMBEDDED in the tools build (rides B) vs external artifact; the generator + output format.
- D3: bug-artifact STORAGE (path/naming); the metadata SCHEMA (description + what else — repro steps? env?);
  the UI shape (toolbar toggle + description field — where); reuse the composer record path or a dedicated
  one; confirm "everything" = the F61 container (inputs + frames + state) is the full capture set.

**E — phase-coherence + UI redesign** (no design docs yet):
- E1: WHICH coherence invariants are formalizable as a TEST vs audit-only? What is the test's shape (a
  headless assertion? a static check)?
- E2: the full UI redesign is entirely open — no constraints, scope, or "what triggers stable/final" defined.
