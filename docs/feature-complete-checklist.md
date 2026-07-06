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

- [ ] **A. imguiapp_internal.h + `IMGUIX_DISABLE_TOOLS`** — carve a clean core/tool boundary so a release
  build can compile the authoring tools out entirely (true lean release; side benefit: no source-code text
  baked into the `.exe`).
  - **New `imguiapp_internal.h`** (analog of `imgui_internal.h`): the INTERFACES of the Composer, Previewer,
    Debugger, canvas, and nodes fold here (today they live across `imguiapp_nodes.h` / public headers). The
    always-on runtime API stays in `imguiapp.h`.
  - **`IMGUIX_DISABLE_TOOLS`** in `imguiapp_config.h` (imconfig analog): ONE master switch (not per-tool).
    Defined → the tools compile to nothing.
  - **Gate the tool TUs**: `imguiapp_nodes.cpp`, `imguiapp_canvas.cpp`, `imguiapp_preview.cpp`,
    `imguiapp_preview_dll.cpp`, `imguiapp_demo.cpp` (+ `imguiapp_av.cpp`/`imguiapp_qoi.cpp` if tool-only) wrap
    their bodies in `#ifndef IMGUIX_DISABLE_TOOLS` so the file compiles empty when disabled. Keep the per-topic
    `.cpp` split (imgui precedent: `imgui.cpp`/`imgui_draw.cpp`/`imgui_widgets.cpp`/… — NOT one blob); do NOT
    introduce `imguiapp_widgets.cpp`, keep `imguiapp_canvas.cpp` + `imguiapp_nodes.cpp` as-is.
  - **`imguiapp.cpp`** stays the always-on core runtime (app / layers / controls / storage / `ImGuiAppStateHistory`).
  - **CMake** (`imguix/CMakeLists.txt`): the tool sources still listed (they self-empty under the macro); a
    lean-release option surfaces `IMGUIX_DISABLE_TOOLS`.
  *Accept: a normal build stays green (nodes/core/headless unchanged); a `-DIMGUIX_DISABLE_TOOLS` build
  compiles + links the host with zero tool symbols and no control/source strings in the binary; document the
  core/tool boundary in the design doc.*

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
