# Composer Studio — Implementation Checklist (gen 4)

Tracker for [composer-studio-design.md](composer-studio-design.md). One line per task; stable
source identifiers are the key, line numbers are 2026-07-09 audit anchors (drift expected —
identifier wins). Slices land in order; every task ships its phase contract (bug-classes.md) and
its exit step. Keep this doc open-items-honest.

Gen-3 landed work (ST0 coherence floor, ST1 grammar closure, ST2 one transport, ST3 WYSIWYG
interpreter) is inherited plumbing — history in
[archive/composer-studio-checklist-gen3.md](archive/composer-studio-checklist-gen3.md). Gen-3
unlanded slices (ST4/ST5/ST6) are rescoped into G4/G6/G7 below.

Files: **N** = imguiapp_nodes.cpp · **D** = imguiapp_demo.cpp · **C** = imguiapp_canvas.cpp ·
**P** = imguiapp_preview.cpp · **PD** = imguiapp_preview_dll.cpp · **H** = imguiapp_internal.h ·
**A** = imguiapp.h · **T** = tests/imguiapp_nodes_tests.cpp ·
**HV** = tests/imguiapp_headless_verify.cpp.

## Pinned decisions (settled up front; relitigating one reopens the design doc)

- **P1 View state**: one `ComposerView` enum {Design, Logic, Code, Replay} lives beside
  `RevealPanel` (D:395) on the composer state; persisted per document in the layout sidecar
  (`ComposerLayoutLoad` D:434 / `ComposerLayoutSaveIfChanged` D:469). Views are **not** presets:
  the preset enum `ImGuiAppComposerLayoutPreset_` (D:511), `ComposerApplyLayoutPreset` (D:544)
  and the `##layout_presets` dropdown (D:1593) are deleted, not wrapped.
- **P2 Bottom bar endgame**: Console + Project only. Code (D:2966), Preview (D:3108), Replay
  (D:3386) tabs leave for the center; Project (D:3026) stays — it lists document artifacts
  (files, runs) and is the G6.4 handoff surface. Tab-bar flags site D:2963.
- **P3 String table**: `ImGuiAppComposerStrings` beside the style tables in D; rows tagged
  L0/L1/L2. Today's scattered labels (D:2966, 3026, 3108, 3309 `"Output###output"`, 3386) move
  in. There is no central table today — this creates it.
- **P4 Veil ownership**: `PreviewEdit` flag (D:3158) + `##pveditveil` window (D:2104) invert
  polarity and move to the mode object: Design view = veiled always, Play = unveiled. The `E`
  toggle dies (▶ replaces it); `Esc` = stop Play.
- **P5 Sidecar, not ini**: welcome-dismissed flag, current view, per-view panel state — all
  sidecar rows (D:434/469). Nothing new in imgui.ini.
- **P6 One drag payload**: Library drags reuse the tree's `"APPNODE"` payload (N:16146) — one
  payload grammar for tree, Library, and subject drops.
- **P7 Lanes are new work**: no phase-band rendering exists on the canvas today (design notes
  only, N:4892, N:10046) — G3.4 *builds* the lanes, and they ship with gen-4 captions from the
  first pixel (no internal-word interim).
- **P8 Sentence rows are the event model's first UI**: `ImGuiAppEventDesc` (H:522) + edge enum
  (H:502) + codegen (N:1383) exist with zero editor surface. G4.3 is UI-only over the existing
  model — any model gap found there is a defect note, not silent scope growth.
- **P9 Test numbering**: GUI steps continue from `step111_transport_rail` (T:8477); headless
  battery additions land in HV beside `composer_layout_presets` (HV:732) and
  `composer_preview_wysiwyg` (HV:790).
- **P10 Banned lexicon v1** (G3.3 ratchet list; L0/L1 rows): persist, temp, tempdata, scope,
  drill, push, pop, mirror, WAL, tick, DAG, posture, altitude, mutate; "node" banned on
  Design-view surfaces only. Approved replacements: Saved, Input, inside, open, order,
  "next frame", run.

## G0 — The frame (subject inversion; land first, alone)

- [ ] **G0.1** `ComposerView` per P1: enum + sidecar row + F6 cycle + palette verbs "View: …"
      (registry road N:4286); preset enum/apply/dropdown (D:511, 544, 1593) deleted.
- [ ] **G0.2** View tabs top-center in the toolbar (D:1185–1644): always visible, one lit; tab =
      subject switch only — side columns persist, selection stays one object across views.
- [ ] **G0.3** Design view subject: interpreter WYSIWYG surface (`AppPreviewFrame`/
      `AppPreviewRender` D:3269–3270; DLL blit `AppPreviewDllTick`/`AppPreviewDllRasterizeFrame`
      D:3251–3254) rehosted from the Preview bottom tab (D:3108) to the center region, full
      bleed, veiled per P4; pasteboard dot grid + model-named window plates; "not running —
      press ▶ Play to interact" corner watermark (string-table row); zoom/pan grammar + zoom
      pill shared with Logic (`ZoomPillRect` republish precedent H:906, N:8048).
- [ ] **G0.4** Logic view subject: existing canvas editor unchanged, reached by tab (reopens
      last scope — per-branch camera memory exists) and by the G4.5 door; never the default.
- [ ] **G0.5** Code + Replay rehosted as center subjects (tabs D:2966, D:3386 leave the bottom
      bar); bottom bar = Console (D:3309) + Project (D:3026) per P2.
- [ ] **G0.6** Panel self-labels: LIBRARY / APP / INSPECTOR / CONSOLE / PROJECT headers with
      one-line role subtitles (design law 2); literals acceptable until G3.1 moves them into the
      table (noted here so the debt is owned).
- [ ] **G0.7** Exit: `composer_cold_open` v0 in HV — panel names render, view tabs present, F6
      cycles all four, Design is the default view on a fresh document.

## G1 — Library

- [ ] **G1.1** Palette panel → **Library**: icon grid over `ImGuiAppNodeKind_` (A:802) +
      `AppNodeKindName` (N:4505) + kind silhouettes (N:5876–5937) at grid size; search field;
      plain categories (Windows: Window/Sidebar/Layout · Controls: the Control vocabulary ·
      Data: Struct/Field · Logic: Op/Command/Task) — exact row→category map pinned at review
      against the vocabulary registry, plain words only.
- [ ] **G1.2** Drag Library → Design subject: `"APPNODE"` payload (P6); drop on window/host =
      scoped add via `AppScopeComposeNewNode` (N:9885); drop on pasteboard = new window at root;
      insertion caret while dragging (ST3.5a grammar); named undo "Add X".
- [ ] **G1.3** Drag Library → App tree row: same payload + add road, kind-gated before highlight
      (ST0.3 gate — illegal target never lights).
- [ ] **G1.4** Space palette + gizmo `+` unchanged (keyboard floor); toolbar **+ Add ▾** opens
      the Library category menu — same registry as `##cmdpalette` (N:7735), no second verb list.
- [ ] **G1.5** Exit: GUI step (post-step111 per P9) — Library-grid drag into Design creates the
      node in the hovered scope; undo rail reads "Add X"; illegal drop refuses without mutation.

## G2 — Play mode

- [ ] **G2.1** ▶ Play toolbar center-right (primary prominence; replaces the Live eye as the
      run affordance); ⏹ + Esc exit; state = `ImGuiAppComposerTransport` (D:354),
      `Source = ImGuiAppTransportSource_LiveRing` (D:362) — Play is a transport posture, no new
      machinery.
- [ ] **G2.2** Input gating per P4: veil on in Design always, off in Play only; `E` toggle and
      panel-local Interact/Edit header control deleted with it.
- [ ] **G2.3** Mode chrome: RunTint wash + accent border (ST1.1 rows) on the subject;
      **PLAYING — Esc to stop** pill top-center via `AppBlStatusPill` (ST1.2); side columns dim
      but stay live.
- [ ] **G2.4** Time rail docks beside ▶ only while playing or a run is loaded (compact
      `AppBlTransportRail`, ST2.1/2.2 already one widget); hidden at rest — no dead chrome.
- [ ] **G2.5** Ambient meta-record on Play (`AppMetaRecordBegin/End` av.cpp:1252–1345, pump
      D:752–798); stop → **"Review this run"** transient → `ComposerOpenRun` road (ST2.6) +
      view=Replay at the stop tick.
- [ ] **G2.6** Inspector **"live — edits keep"** chip during Play (live write-through seam
      landed; the chip states it; string-table row).
- [ ] **G2.7** Exit: `composer_play_loop` in HV — ▶ → input reaches the app → live edit sticks →
      ⏹ → "Review this run" lands in Replay at the stop tick.

## G3 — Language

- [ ] **G3.1** `ImGuiAppComposerStrings` per P3: every L0/L1 chrome string (panel names + role
      subtitles, view tab names, watermark, empty states, status verbs, welcome copy, mode
      pills) in one tagged table; G0.6's literals move in.
- [ ] **G3.2** Rename pass: outliner→**App** · palette→**Library** · Generate→**Build** with
      compile-as-button states ✓ built / ● needs build / ⚠ warnings / ✖ failed (health rows
      landed ST1.1; wording changes) · Output→**Console** (D:3309) · toolbar phase captions
      (compose/iterate/persist/produce, D:1185–1644) deleted.
- [ ] **G3.3** Lexicon ratchet: banned-word test over the table's L0/L1 rows (mechanism:
      tests/style/chrome_literal_check.py + baseline precedent, ctest
      `imguix-lexicon-tests`); banned list = P10, growable only.
- [ ] **G3.4** Lifecycle lanes **land** (P7): phase bands on the root canvas — bands + frame
      spine + loop-back edge labeled "next frame" + skew arrow "input arrives next Update";
      captions per design §4.2 (Startup / 1·Update / 2·Commands / 3·Report / 4·Draw / Shutdown
      + contract sentences); ontology names live in lane tooltips only; slot drag = order write
      (F58–60 road); lane geometry derived in update (§3.7 discipline, ST1.8 precedent).
- [ ] **G3.5** Logic header sentence: "What `<App>` does every frame, top to bottom…" — one
      line, opaque plate at the canvas top edge, scope-aware, corner-tenant rules (minimap/zoom
      pill precedent ST1.4/1.5).
- [ ] **G3.6** Exit: lexicon ratchet green; lane captions + header sentence asserted in a GUI
      step; `composer_cold_open` re-run green (strings moved, nothing regressed).

## G4 — Inspector (sentences; absorbs ST4.5)

- [ ] **G4.1** Section reorg over the component-section machinery (N:8302–8532): **This is… /
      Looks / State / Behavior / Wiring**; identity renders as a sentence ("Mixer — a Control
      inside MainWindow") from kind + placement; kind chip keeps hue+word double-encoding.
- [ ] **G4.2** State framing: Persist rows under "**Saved** — kept between runs", Temp rows
      under "**Input** — read fresh every frame"; storage names demote to tooltips (lexicon
      rule); live echo unchanged.
- [ ] **G4.3** Behavior sentence rows (P8 — first UI over the existing event model): When
      [Temp field ▾] [starts/stops/changes ▾ = `ImGuiAppEventEdge_` H:502] → [reaction ▾], rows
      backed by `ImGuiAppEventDesc` (H:522), round-tripping the codegen emit (N:1383); add/
      remove/reorder rows; palette verbs mirror ("Add behavior…"); refusals ride the notify
      channel.
- [ ] **G4.4** Body editor (ST4.5 rehomed): Behavior section "Edit update code…" → per-method
      `InputTextMultiline` (`MethodBody` H:389–398, enum H:377–387, cap H:136), live counter,
      compile chip ok/compiling/error-tail from `PreviewDllErr`; named undo covers body edits;
      serialize/parse already landed (N:13045–13049, N:13379). Exit: extend F78.5 core tests
      (imguiapp_core_tests.cpp:1799–1882) with the UI write path.
- [ ] **G4.5** Wiring section: reads/writes chips from the link model; **"Edit logic…"** →
      view=Logic + enter the node's scope (same road as Tab-enter, N:7290) — the one L2 door;
      double-click a Data chip anywhere = same door.
- [ ] **G4.6** Exit: GUI steps — sentence row edit → generated guard block byte-matches the
      hand-written idiom; body edited in-app runs in Play; "Edit logic…" lands drilled at the
      node.

## G5 — First contact

- [ ] **G5.1** Welcome overlay: modal over the dimmed shell — product-statement line + three
      cards (Blank = File→New road · Mixer example = demo graph load · guided counter = G7.1
      tour, card hidden until G7.1 lands); dismiss-forever = sidecar row (P5); reopens from `?`.
- [ ] **G5.2** `?` region map: panels republish their rects (ZoomPillRect precedent H:906) →
      overlay draws numbered plates + one-sentence roles (string-table rows); Esc closes; `?`
      also hosts "Show welcome again".
- [ ] **G5.3** Empty states per design §7.4 — all six surfaces (Design pasteboard ghost, App,
      Inspector, Console, Replay, Behavior), copy from the string table, each with its trigger
      condition asserted (empty ≠ loading).
- [ ] **G5.4** Status-line teacher on all views: left slot narrates hovered thing's verbs —
      `AppGraphStatusHint` compose (N:4754) + canvas render (N:8076, ST1.5) generalized to a
      shell-level channel fed by Design-subject hover and panel hover; right slot facts at the
      fixed em offsets (D:1837–1840).
- [ ] **G5.5** Exit: `composer_cold_open` full — product statement, panel names, six empty-state
      verbs, zero banned words, ▶ and + Add enabled, `?` map opens and closes.

## G6 — Replay & DLL depth (absorbs ST5.1–5.4, ST4.1–4.4)

- [ ] **G6.1** State-at-tick via inspector grammar: `ComposerRenderStateAtTick` (D:998–1053)
      renders `AppLiveFieldsTable` (N:8144) sections — Data(tick)/Temp(tick) under the G4.2
      Saved/Input framing.
- [ ] **G6.2** Divergence surfaced: `FirstDivergence` (av.cpp:1863–1963) → rail mark (⬢ + error
      hue) + Problems row ("state hash diverges at tick N"), click scrubs; WAL chips (D:724–734)
      click → Console filtered to that tick.
- [ ] **G6.3** Record split-button in Play chrome: ⏺ Record ▾ → Meta only / With video
      (`AppRecordBegin` av.cpp:956 — first UI wiring of the video path); armed = static red dot
      + tick count on the rail; preview-panel Record (D:2817–2833) folds in.
- [ ] **G6.4** Artifact handoff: stop → transient confirm + Project-tab entry + Open in Replay
      (ST2.6 road). Exit: record→stop→open→click-divergence end-to-end step.
- [ ] **G6.5** Rect-report ABI (ST4.1): per-tick (instance id, rect) export tick-stamped with
      its frame blob (fn table PD:59–92, decode PD:479–596); `IMGUIAPP_PREVIEW_ABI` bump
      H:138–139; emitter in `AppPreviewModuleCodeGenerate` (N:12761). Image-space rects —
      coherent pair by construction.
- [ ] **G6.6** Edit overlay on the DLL blit (ST4.2): draw in image space (D:2851–2874);
      structural edits → signature dirty → recompile loop (D:2767–2780); Design-view gestures
      queue against the model while compiling.
- [ ] **G6.7** Compiling UX (ST4.3): stale wash + "compiling…" pill from `PreviewDllErr`/
      sig-dirty (D:2836–2843) — fixed echo zone, no toast.
- [ ] **G6.8** One preservation rule (ST4.4): DLL `CopyOut/CopyIn` (PD:370–401) adopts the
      interpreter's (name,type) reconcile (P:1135–1167).

## G7 — Guided tour + accessibility (absorbs ST6.1–6.6)

- [ ] **G7.1** Replay-driven counter tutorial: 10 steps through real verbs (Library drag,
      rename, one Behavior sentence, ▶ Play, Build), each gated on the user's own action via
      the command registry; unhides the G5.1 tutorial card; headless completion test.
- [ ] **G7.2** CUD hue re-anchor (ST6.1): `APP_HUE_*` (N:280–302) → Okabe–Ito, 15% Text pull
      kept (N:274–277); CVD pair table test (protan/deutan/tritan ΔE over kinds + warn/error).
- [ ] **G7.3** Double-encoding glyphs (ST6.2): severity ▲/⬢ at `AppSeverityColor` consumers
      (canvas dot N:7638–7642, outliner underline→leading glyph N:15701–15707, problems list,
      status pill); origin ✎/👁/✓ at `AppGraphOriginColor` (N:4445–4450) consumers + legend
      (D:2465–2494).
- [ ] **G7.4** Contrast rungs (ST6.3): empty-pill α×0.4 (N:830), kind-tag α0x66 (C:1078),
      muted-on-field pairs re-derived to APCA Lc 45/60; computed contrast table test over
      `ImGuiAppComposerStyle` at default theme.
- [ ] **G7.5** Targets (ST6.4): 1.5 em hit clamp at frame-height `InvisibleButton` sites
      (N:166/169/188/434/437/791–794); pin hover radius (H:2105–2119) gets an 8 px screen-space
      floor in the C hover pass.
- [ ] **G7.6** Canvas focus walk (ST6.5): arrows = nearest-neighbor node cursor (model space),
      Enter select, menu key = context menu; dashed accent focus ring (new `ImGuiAppCanvasStyle`
      slot, distinct from selection gold); registry entries so the palette teaches it.
- [ ] **G7.7** A11y test group (ST6.6): contrast rows, target asserts, focus-walk GUI step, CVD
      table — plus one logged 60-second protocol pass (design §9.2) per milestone from G5 on.

## Standing gates (every slice)

- [ ] Phase contract (Facts/Derived/Writes/Loops/Crossings) in each task's PR description.
- [ ] Review grep = bug-classes.md smell table; new screen-pos/dimensions call sites
      phase-classified; geometry verbs altitude-routed.
- [ ] Zoom + drill-in acid tests on touched surfaces; style/indent/section ratchets green after
      header edits; chrome-literal ratchet green.
- [ ] From G3.3 on: lexicon ratchet green; new chrome strings enter the table, never inline.
- [ ] `composer_cold_open` green from G0.7 onward — it only ever grows.
