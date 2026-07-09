# Composer Studio — Implementation Checklist (gen 3)

Tracker for [composer-studio-design.md](composer-studio-design.md). One line per task: stable
source identifiers are the key; line numbers are 2026-07-09 audit anchors (drift expected — the
identifier wins). Phases land in order; every task ships its §3.7 phase contract and its exit
step. Check items off here; keep this doc open-items-honest (a checked item with no test is not
checked).

Files: **N** = imguiapp_nodes.cpp · **D** = imguiapp_demo.cpp · **C** = imguiapp_canvas.cpp ·
**P** = imguiapp_preview.cpp · **PD** = imguiapp_preview_dll.cpp · **H** = imguiapp_internal.h ·
**T** = tests/imguiapp_nodes_tests.cpp.

## ST0 — Coherence floor (audited defects; fix before any feature)

- [x] **ST0.1** Group-frame altitude fallback: `AppGroupAccumulate` N:8961 — unsubmitted-member
      fallback `n->GridPos` → `AppNodeScopePos(g, n)`, mirroring `fit_all`/`fit_ids`/drag-origin.
      Exit: `step104_group_frame_drill_altitude` (member added while drilled: frame hugs the
      placement, never the root sentinel; stays tight after settle). GREEN.
- [x] **ST0.2** FontRatio² chip text: `AppDrawScopeOrderStrip` + `AppDrawScopePortals` — em =
      `GetFontSize() * AppCanvasZoom(g)` (GetFontSize already carries FontRatio). Exit:
      `step107_strip_em_fontratio` (1.5× host font: first chip sits at the 2.25-em indent
      computed from base × ratio × zoom). GREEN.
- [x] **ST0.3** Silent reparent refusal: outliner drop gate kind-filters BEFORE accept (illegal
      pair never highlights nor completes — structurally unofferable, no notice owed);
      `AppGraphReparent` notifies on the kind-mismatch branch via `AppGraphNotify` for every
      non-gesture caller (defense in depth). Exit: `step105_reparent_gate_kind_aware`
      (Control→Struct drag: zero mutation, zero spurious notice) + step103's illegal-drop case
      still green. GREEN.
- [x] **ST0.4** `_TrunkRoutes` orphan: owner-keyed erase joined `AppGraphRemoveNode`'s sweeps.
      Exit: `step106_trunk_route_sweep` (owner's entry dies, survivor's stays). GREEN.
- [x] **ST0.5** DLL preview paused + resize — decided: paused = frozen frame; display size
      follows the panel only while ticking, so draw data + authored size stay a coherent pair
      (paused resize letterboxes, never re-maps stale geometry); commented at the site
      (D preview panel, SetDisplaySize/Tick gated together).

## ST1 — Grammar closure + derive-and-update

- [ ] **ST1.1** Literal color sweep → tables: D:1363–1365 (Generate triple), D:1423 (problems),
      D:1470 (freeze amber), D:2116/2141/2144/2647/2728/2837/2928–2930/2944/2950/2972 (inspector/
      console greens-ambers-reds), D:2550–2552 (frozen wash), D:2409–2422 (canvas theme block +
      its `GridSpacing != 26.0f` sentinel — dies). New `ImGuiAppComposerStyle` rows: `HealthOk/
      HealthStale/HealthBlocked/RunTint/RecordArmed` (H:745–795, seed N:322–367). Exit: literal
      ratchet (grep `ImVec4(`/`IM_COL32(` in chrome sections == 0) joins the style-ratchet suite.
- [ ] **ST1.2** One pill grammar: `ComposerStatusPill`/`ComposerPillColor` D:1650–1683 re-render
      through the `AppBl*` family (N:454 pill, N:812 filter); delete the SmallButton idiom.
- [ ] **ST1.3** One icon-button: promote `AppTreeRowIcon` N:15503–15521 → `AppBlIconButton`
      (carries the `ImGuiHoveredFlags_ChildWindows|AllowWhenBlockedByActiveItem` rule); migrate
      gizmo column N:7679–7788 + outliner rows N:15709–15776 call sites.
- [ ] **ST1.4** Zoom pill: overlay beside minimap (`CanvasMiniMap` C:310); reads `CanvasGetZoom`
      C:245–256; click = fit-all (cmd 11), right-click = 100%. Claims the §4.1 corner-stack rule.
- [ ] **ST1.5** Status hint revival: `ImGuiAppStatusStripControl` D:1687–1935 left zone renders
      `AppGraphStatusHint` N:4509 (composed at N:7647–7677); refused-link override keeps its 2.5 s
      error window. Exit: T step asserting hint text per hover target.
- [ ] **ST1.6** Keymap editor reachable: host `AppGraphShowKeymapEditor` N:10741–10808 in the
      project inspector (`ShowComposerProjectInspector` D:2101–2214, new Shortcuts section) +
      palette verb. Exit: extend `step92_keymap_rebind` T:7005 to drive the UI path.
- [ ] **ST1.7** One outliner default: D:1815 `220.0f` → the em·16 default of D:2450.
- [ ] **ST1.8** Derive-and-update pass (§3.7 law, F2's class structurally retired):
  - [ ] **a** node plate sizes derived in update — new `AppDeriveNodePlateSize(g, n)` from row
        model (fields/pins/labels) × type ladder (`ImGuiAppComposerMotion` H:807–810) × paddings
        (`ImGuiAppCanvasStyle` H:1873–1918); seat before submission (N:5850–5854 path).
  - [ ] **b** band/strip/chip metrics derived in update from the ladder (kills the class of
        N:9963/10103 for good); `AppScopeCaption` row heights published as facts, not re-derived
        per draw site.
  - [ ] **c** `CanvasEndNode` measurement C:967–1013 demoted to verification: assert
        `derived ≍ measured` (model-unit tolerance = existing `NOISE_M` deadband) under zoom acid.
        Read-back stays authoritative ONLY for host-submitted arbitrary widget content (comment
        the exception per §3.7).
- [ ] **ST1.9** Grammar ratchets: literal grep (ST1.1), grammar dump test (style tables
      serialized+diffed), type-ladder/quantum audit (off-ladder font size or off-quantum gap = fail).

## ST2 — One transport

- [ ] **ST2.1** `AppBlTransportRail` widget: generalize `ComposerPlaybackTimeline` D:931–984
      (notches: input faint / snapshot gold / command green / divergence error+⬢; cursor; step;
      play/freeze; source badge). One widget, three renderings (toolbar compact / Replay full /
      record armed).
- [ ] **ST2.2** Toolbar App-time scrub D:1477–1492 (`SliderInt ###apptimescrub` + step buttons)
      replaced by the compact rail; corner time badge (`edit −3 · run −47f`) unified.
- [ ] **ST2.3** Replay docks: `ComposerRenderPlayback` window D:1055–1156 → bottom-panel tab
      (tabs at D:2623–2985); joins `RevealPanel`/`ImGuiAppComposerPanel_` (D:327–339); "Open
      run…" moves to Project tab + palette. Floating window dies.
- [ ] **ST2.4** Identity gate single-source: inline re-check D:1014–1021 renders the core
      verdict struct from `AppRunStateAtTick`/`ImGuiAppRunMeta` (av.cpp:1880–1886, H:229–239)
      with severity glyphs — no second implementation.
- [ ] **ST2.5** One RGBA upload helper: merge `ComposerSyncFrameTexture` D:851–890 +
      `ComposerUploadRgbaTexture` D:895–926.
- [ ] **ST2.6** Run artifacts in Project tab (rescan cadence D:2298–2326) with **Open in Replay**
      riding the existing `AppRunOpen` reopen path D:778–792.

## ST3 — Observe posture + WYSIWYG (interpreter)

- [ ] **ST3.1** Postures: `ComposerApplyLayoutPreset` D:496–542 presets → Author/Observe/Replay
      (Review folds into Author); F6 cycle chord in the registry; posture = panel states +
      `ImGuiAppComposerTransport::Source` D:344–362.
- [ ] **ST3.2** Preview promoted to Observe subject panel: preview tab body D:2744–2901 rehosted;
      both backends in-panel (interpreter widgets / DLL blit D:2851–2874).
- [ ] **ST3.3** Interact/Edit mode: state on editor Preview fields H:938–953; Edit routes input
      to the design overlay (app receives none), accent frame + pencil cursor + mode chip
      (RunTint idiom); `E` toggle / `Esc` exit as registry chords (`s_editor_commands`
      N:10478–10528). Mode is a §4.4 echo-zone citizen.
- [ ] **ST3.4** Overlay anchor/halo from the surface manifest: `AppPvSurface` P:109–118,
      `AppPvDrawFields` P:892–938, `AppPreviewSetBrush`/`HoveredNode`/`TakeClickedNode`
      P:1328–1340 — click = select promotion; halo both directions (existing brush bus).
- [ ] **ST3.5** Gesture grammar — each gesture rides the normal mutation + `AppUndoPush`/
      `AppUndoDeriveLabel` N:14428–14558 path (§1c: intents only):
  - [ ] **a** drag-within-host → sequence order (F58–60 write path; chip machinery
        `AppHandleScopeStripDrag` N:9410 as the model precedent). Undo "Reorder X".
  - [ ] **b** drag-across → `AppGraphReparent` N:15350 + ST0.3 notice. Undo "Move X".
  - [ ] **c** double-click text → inline rename path (N:15641–15656 grammar). Undo "Rename A→B".
  - [ ] **d** Region/Split divider drag → layout node param (F53–57 vocabulary). Undo "Edit Split".
  - [ ] **e** context verb "Capture as default" → live value → Persist default. Undo "Edit X".
  - [ ] **f** Add-palette drop onto host → scoped add (`AppScopeComposeNewNode` road; adoption
        pinned by `step19_scoped_add_adopts` T:1687).
  - [ ] **g** RMB → node context menu (four-roads parity with canvas/tree).
- [ ] **ST3.6** Quick inspector at the widget: `AppGraphInspectHere` N:4545 anchored to the
      preview widget rect (this-frame manifest rect — §3.7). `N` chord works over the preview.
- [ ] **ST3.7** Exit: one T step per gesture row (a–g), each asserting model write + named undo
      label + refusal notice where applicable.

## ST4 — DLL parity + method bodies

- [ ] **ST4.1** Rect-report ABI: fn-pointer table `ImGuiAppPreviewDll` PD:59–92 grows a per-tick
      (instance id, rect) export, tick-stamped with its frame blob (magic 0x494D4444 decode
      PD:479–596); `IMGUIAPP_PREVIEW_ABI` bump H:138–139; emitter side in
      `AppPreviewModuleCodeGenerate` N:12761. Rects are image-space — coherent pair with the
      frame by construction (§3.7 table).
- [ ] **ST4.2** Edit overlay on the DLL blit: draw on the rasterized image in its own space
      D:2851–2874; structural edits mark signature dirty → existing recompile loop D:2767–2780.
- [ ] **ST4.3** Compiling UX: stale wash + "compiling…" pill from `PreviewDllErr`/sig-dirty
      states (D:2836–2843) — fixed echo zone, no toast.
- [ ] **ST4.4** One preservation rule: DLL label-keyed `CopyOut/CopyIn` PD:370–401 adopts the
      interpreter's (name,type) reconcile P:1135–1167.
- [ ] **ST4.5** Body editor: inspector Body section per method — `MethodBody` storage H:389–398,
      cap `IMGUIAPP_CONTROL_BODY_MAX` H:136, enum H:377–387; `InputTextMultiline` + live counter;
      compile chip (ok/compiling/error tail); palette verbs "Edit OnUpdate body…"; serialize
      already done (N:13045–13049, parse N:13379). Undo covers body edits. Exit: extend the
      F78.5 core tests (imguiapp_core_tests.cpp:1799–1882) with the UI write path.

## ST5 — Replay depth + recorder

- [ ] **ST5.1** State-at-tick via inspector grammar: `ComposerRenderStateAtTick` D:998–1053
      renders `AppLiveFieldsTable` N:8144 component sections (Data(tick)/Temp(tick)) instead of
      bespoke text.
- [ ] **ST5.2** Divergence surfaced: `FirstDivergence` (av.cpp:1863–1963) → rail mark (⬢ +
      error hue) + Problems row ("state hash diverges at tick N"), click scrubs; WAL chips
      (`ComposerLoadWalCommandTicks` D:724–734) click → Output filtered to tick.
- [ ] **ST5.3** Record split-button in the transport cluster: `⏺ Record ▾` → Meta only
      (`AppMetaRecordBegin/End` av.cpp:1252–1345, pump D:752–798) / With video (`AppRecordBegin`
      av.cpp:956 — first UI wiring of the video path). Armed = static red dot + tick count on
      the rail. Preview-panel Record button D:2817–2833 folds in.
- [ ] **ST5.4** Artifact handoff: stop → status transient confirm + Project-tab entry + Open in
      Replay (ST2.6). Exit: T step record→stop→open→click-divergence end-to-end.

## ST6 — Accessibility

- [ ] **ST6.1** CUD hue re-anchor: `APP_HUE_*` N:280–302 onto Okabe–Ito coordinates (keep the
      15% Text pull, N:274–277). Exit: CVD pair test (protan/deutan/tritan ΔE over the kind set
      + warn/error pair) as a table test.
- [ ] **ST6.2** Double-encoding glyphs: severity ▲/⬢ at `AppSeverityColor` consumers (canvas dot
      N:7638–7642, outliner underline→leading glyph N:15701–15707, problems list, status pill);
      origin ✎/👁/✓ at `AppGraphOriginColor` N:4445–4450 consumers + legend D:2465–2494.
- [ ] **ST6.3** Contrast rungs: empty-pill α×0.4 N:830, kind-tag α0x66 C:1078,
      `TextMuted(0.60)`-on-`FieldBg(0.30)` pairs re-derived to APCA Lc 45/60; pinned by a
      computed contrast table test over `ImGuiAppComposerStyle` at default theme.
- [ ] **ST6.4** Targets: frame-height `InvisibleButton` sites (N:166/169/188/434/437/791–794)
      hit-clamped to 1.5 em (visuals unchanged); pin hover radius (H:2105–2119 `PinHoverRadius`)
      gets an 8 px screen-space floor in the hover resolve (C hover pass).
- [ ] **ST6.5** Canvas focus walk: arrows = nearest-neighbor node cursor (model space), Enter =
      select, menu key = context menu; dashed accent focus ring (new `ImGuiAppCanvasStyle` slot,
      distinct from `NodeOutlineSelected` gold); registry entries so the palette teaches it.
- [ ] **ST6.6** A11y test group: contrast rows (ST6.3), target asserts (ST6.4), focus-walk GUI
      step, CVD table (ST6.1) — the suite's first non-zero a11y coverage.

## Standing gates (every phase)

- [ ] §3.7 phase contract present in each task's PR description (Facts/Derived/Writes/Loops/
      Crossings).
- [ ] Review grep = the bug-classes.md smell table; every new screen-pos/dimensions call site
      phase-classified; every geometry verb altitude-routed.
- [ ] Zoom acid test on every touched surface; drill-in acid test (ST0.1's step) stays green.
- [ ] Style/indent/section ratchets green after every header edit.
