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

- [x] **ST1.1** Literal color sweep → tables: all listed demo.cpp chrome literals moved into
      `ImGuiAppComposerStyle` rows (`StatusOk / HealthOk / HealthStale / HealthBlocked /
      RunTintWash / RunTintBorder / RecordArmed`, seeded from `APP_HUE_*`); warn/err text unified
      onto existing `SevWarn`/`ErrorText`; codegen chip onto `Gold`; the `GridSpacing != 26`
      sentinel replaced by an editor-state applied flag (view-side — a doc flag would be a
      render-phase model write). Remaining `ImVec4(0.` in demo.cpp = the 4-line DEMO host palette,
      pinned by baseline. GREEN (`imguix-chrome-literal-tests`).
- [x] **ST1.2** One pill grammar: `AppBlStatusPill` exported (draw-list AppBl family, "###id"
      identity preserved so pill test addresses survive); `ComposerStatusPill` delegates; the
      SmallButton idiom + local color triple deleted.
- [x] **ST1.3** One icon-button: `AppTreeRowIcon` → `AppBlIconButton` (family member carrying the
      ChildWindows/AllowWhenBlockedByActiveItem rule); outliner rows + gizmo column ride it.
- [x] **ST1.4** Zoom pill: bottom-right, stacks above the minimap inset (corner-tenant rule);
      honest readout, click = fit-all, right-click = 100%; rect republished (`ZoomPillRect`).
      Exit: `step109_zoom_pill` GREEN.
- [x] **ST1.5** Status hint revived — at the §4.1 altitude (one line inside the canvas bottom
      edge, opaque plate, yields the corner to the minimap), not the strip: the strip keeps facts,
      the canvas teaches. Error override arrives pre-colored via `AppGraphStatusHint` severity.
      Exit: `step108_status_hint_line` GREEN.
- [x] **ST1.6** Keymap editor reachable: project inspector **Shortcuts** section hosts
      `AppGraphShowKeymapEditor`; palette verb "View: Rebind shortcuts…" (host cmd) empties the
      selection + opens the inspector. `step92_keymap_rebind` already drives the editor UI.
- [x] **ST1.7** One outliner default: strip reveal now uses the em·16 default.
- [x] **ST1.8** Derive-and-update pass:
  - [x] **a** `AppDeriveNodePlateSize(g, n)` — update-phase plate derivation in model units
        (host metrics ÷ FontRatio; zoom never enters); `AppLayoutNodeSize`'s per-kind constants
        deleted — the fallback IS the derivation, so first-frame estimate-vs-measure settle
        flicker is structurally gone at all 16 call sites. Pin rows derived kind-agnostically
        (op operands, struct providers, control deps — mirroring submission).
  - [x] **b** `AppScopeChromeEm(g)` — ONE producer of the post-CanvasEnd chrome em formula
        (strip + portals consume it; F2's class can't recompose per site).
  - [x] **c** `step110_derived_size_matches_measured` — derived ≍ measured within the engine
        noise band for window / control identity / op / drilled struct, and under zoom acid
        (heights matched to 0.0–0.7 model units). Engine read-back stays authoritative for
        arbitrary widget content, commented as the §3.7 exception.
- [x] **ST1.9** Literal ratchet landed (`tests/style/chrome_literal_check.py` + baseline, ctest
      `imguix-chrome-literal-tests`). Grammar-dump + ladder/quantum audits ride ST6's table-test
      group (they want the final CUD-re-anchored tables).

## ST2 — One transport

- [x] **ST2.1** `AppBlTransportRail` exported (draw-list AppBl family): one notch grammar
      (input faint / snapshot gold / command green / divergence error), cursor, click/drag scrub,
      record-armed static dot; all colors from the style table. `ComposerPlaybackTimeline` now
      builds marks and delegates. Exit: `step111_transport_rail` (compact + full renderings,
      proportional click, exact edge landing) GREEN.
- [x] **ST2.2** Toolbar App-time scrub: `SliderInt` replaced by the compact rail — LIVE and FILE
      scrub through the same widget.
- [x] **ST2.3** Replay docked: `ComposerRenderPlayback` window → `ComposerReplayTabBody` in the
      bottom tab bar (`ImGuiAppComposerPanel_Replay`, RevealPanel-reachable, palette verb
      "Panel: Replay"); toolbar FILE eye + source switch reveal the tab; `PlaybackOpen` deleted.
      Tab bar wears FittingPolicyResizeDown (five tabs shrink, never scroll-hide). The floating
      window is gone.
- [x] **ST2.4** Identity gate single-sourced: `AppRunIdentityMatches` extracted in av.cpp,
      used inside `AppRunStateAtTick` AND by the state-at-tick readout — the inline UI compare
      is deleted.
- [x] **ST2.5** One RGBA upload path: `ComposerSyncFrameTexture` delegates to
      `ComposerUploadRgbaTexture` behind its tick guard (per-scrub upload preserved).
- [x] **ST2.6** Project tab lists recorded takes (`.meta`/`.mp4` under the artifact dir) with
      **Open in Replay** (opens via the one `ComposerOpenRun`/`AppRunOpen` road — which now also
      opens raw `.meta` takes — sets FILE source, reveals the tab); Record→Stop in Preview also
      reveals Replay (the handoff is visible, not implicit).

Landed alongside (found by the suites): `APP_HUE_RUN_TINT` pinned to exact 210/150/40 255ths (the
headless tint probe matches that RGB — a rounding drift left the app frozen mid-suite and cascaded
six tests); the headless error-row probe now reads the style table's `ErrorText` instead of a
hard-coded triple; ST1.8a hardened with a context-free per-kind floor (model-only callers run
between frames where CalcTextSize has no baked font — caught by a core-test segfault stack).

## ST3 — Observe posture + WYSIWYG (interpreter)

- [x] **ST3.1** Postures Author / Observe / Replay (Review folded into Author): masks
      Tree|Insp|Code / Insp|Code|Live / Code; applying a posture reveals its SUBJECT tab
      (Observe→Preview, Replay→Replay); F6 cycles; palette verbs "Posture: …"; dropdown renamed.
      Headless `composer_layout_presets` rewritten to the new postures (and it restores the
      full-height viewport for downstream geometry tests). GREEN.
- [x] **ST3.2** Subject-sized preview: the Observe posture opens the bottom panel ON the Preview
      tab (the doc's Observe description); full right-column rehost deferred to the panel-contract
      rework (noted, not forgotten).
- [x] **ST3.3** Interact/Edit mode: header segmented toggle (amber when editing), `E` enters at
      the panel / exits at the veil, `Esc` exits (rename/drag cancel first); mode chrome =
      RunTint border + EDIT chip; the app receives NO input while editing (the veil is a topmost
      overlay window spanning the manifest-rect union — hosted controls render in the preview
      app's own windows, so a child-local veil was structurally wrong).
- [x] **ST3.4** Overlay anchors from the manifest: `Surface.Rects` published per frame
      (same-frame pair, cleared at frame start), exported via `AppPreviewSurfaceRectCount/At`;
      hover halos ride the brush bus; click = select promotion writing ALL THREE selection
      stores (doc + graph + canvas engine — a partial write gets stomped by the canvas readback).
- [x] **ST3.5** Gesture grammar (each through the normal mutation + named-undo roads):
  - [x] **a** drag-within-host → `AppScopeOrderMoveMember` bracketed into the host scope;
        insertion caret at the mouse; verified end-to-end (order record head flips).
  - [x] **b** "Move to ▸" context verb → exported `AppGraphReparent` (refusals notify). The
        drag-across form waits for window sections on the surface (deferred, noted).
  - [x] **c** dbl-click → inline rename editor at the instance rect; undo label
        "Rename Alpha → Gamma" verified in the rail.
  - [ ] **d** Region/Split divider — deferred: the interpreter does not interpret Layout nodes
        yet; lands with interpreter-layout support.
  - [ ] **e** "Capture as default" — deferred: `ImGuiAppFieldDesc` has no default-value
        vocabulary; needs the model field first (flagged as a model gap).
  - [x] **f** "Add control to this host" context verb → AddNode + Reparent (adopts at root
        altitude; editor seats via the auto-place road).
  - [x] **g** RMB → preview context menu: Inspect here (N), Rename, Reorder earlier/later,
        Move to ▸, Add control — four-roads parity core set.
- [x] **ST3.6** `N`/Inspect-here from the preview selects + pins the quick inspector
      (canvas-anchored; preview-rect anchoring rides the same rework as ST3.2's rehost).
- [x] **ST3.7** Exit: `composer_preview_wysiwyg` (headless, full demo) drives toggle → veiled
      click-select → dbl-click rename (+ named undo) → drag reorder (order record) → E exit,
      end-to-end through the real mouse path. GREEN (33/33 headless, 8/8 battery).

Landed alongside (surfaced by the exit test — two latent demo-wide bugs):
- **Undo-rail flood fixed**: the undo/save serializer included LIVE-MIRROR twins, so the mirror's
  per-frame link upsert pushed one "Rewire" snapshot per frame, evicting every real operation
  name (the rail was useless in any live session). Serialization now excludes live nodes/links/
  bindings/placements/orders (derived state belongs in no snapshot), undo snapshots drop the
  `NextId` allocator watermark (restore never rewinds ids), and the label derive diffs
  snapshot-vs-snapshot instead of snapshot-vs-live-graph. The rail now reads
  `Open · Add BadCtrl · Delete BadCtrl · … · Rename Alpha → Gamma`.
- **External-selection protocol pinned**: a selection write from outside the canvas must reach
  doc + graph + canvas engine together (the strip already knew; the overlay now does too).

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
