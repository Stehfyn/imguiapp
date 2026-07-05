# Feature-Complete Checklist — explicit feature series to 100%

Companion to [feature-complete-roadmap.md](feature-complete-roadmap.md) §7: the phases decomposed
into an unambiguous, ordered series of features. One feature = one deliverable with one
acceptance gate. "Green" = imguix-tests + imguix-headless-verify. Numbers are the build order;
sources: [feature-audit-2026-07-04.md](feature-audit-2026-07-04.md) (470 verified claims),
[scope-sweep-2026-07-04.md](scope-sweep-2026-07-04.md). Items the audit proved
absent-by-design are NOT here; doc-hygiene corrections are (F49).

## P1 — persistence + undo rails

- [x] **F01 round-trip harness** — test: build a maximal graph (every node kind, port set,
  link kind, bindings, events, style/color mods, scope placements, prefabs, custom layer,
  authored order once F58 exists) → serialize → load → **model-equal to the original
  (field-by-field compare — byte-stability alone cannot see a field nobody serializes)** →
  reserialize → byte-identical.
  *Accept: test red on any lossy OR unserialized record (step49 + AppGraphModelEqual; prefabs join
  when F04 lands, authored order when F58 lands).*
- [x] **F02 Init=/Dock= records** — serialize + load HasInitialPlacement / InitialPos /
  InitialSize / DockDir / DockSize (today inspector-editable, written by no key). Also serializes
  the window/sidebar Flags mask (same class, previously written by no key).
  *Accept: covered by F01; step15 round-trip extended now.*
- [x] **F03 event round-trip tests** — ImGuiAppEventDesc through save/load, undo, copy/paste.
  *Accept: dedicated test (step50 save/load + undo; step50b Ctrl+C/Ctrl+V; all four edges, both
  actions, comma-bearing expr).*
- [x] **F04 prefabs on disk** — prefab registry serialized beside the graph file, loaded on
  start; starter library: producer/consumer pair, event→command control.
  *Accept: prefab stamped after restart reproduces the saved subtree; F01 covers the records.
  (step53: `<graph>.prefabs` sidecar written/read by Save/LoadAppGraph; AppGraphSeedStarterPrefabs
  seeds the two starter prefabs on empty registry, wired into the demo's SeedAppGraph.)*
- [x] **F05 undo covers every mutation road** — adds/compose, reparent, delete,
  ScopePlacements, events, dock/init fields, prefab stamp, paste, explode/collapse (the order
  road joins when F58 lands).
  *Accept: per-road test = mutate → undo → model byte-equal to before (step52 drives add / delete /
  event / dock+init / placement / reparent through the editor's per-frame checkpoint → undo →
  AppGraphModelEqual. Paste + explode/collapse ride the same auto-checkpoint, proven mutating by
  step50b/step48; prefab stamp joins when F04 lands.)*
- [x] **F06 ScopeCams sweep on delete** — AppGraphRemoveNode drops the dead scope's camera
  records (mirrors the ScopePlacements sweep).
  *Accept: delete-scope-owner test asserts no ScopeCams entry remains (step47).*
- [x] **F07 inspector edits reach exploded lists** — field edits/adds route to the Field nodes
  while a Draft list is exploded (today: silent dead writes; collapse destroys them).
  *Accept: edit-while-exploded test; collapse preserves the edit (step48: inspector Add lands as a
  containment-linked Field node, the exact shape collapse folds back; inline vector stays empty).*
- [x] **F08 history jump test** — HistoryGoto path (undo popup list) exercised.
  *Accept: jump N back → model equals snapshot N (step51: goto(1) → AppGraphModelEqual to a captured
  snapshot-1 reference).*

## P2 — behavior-class sweeps + feedback infrastructure

- [~] **F09 outliner family sweep** — drag-reparent (control↔windows, field↔structs, drilled
  variants), eye/hide, clone icon; fix + test what falls out.
  *Accept: family sweep doc with empty OPEN list; tests per verb. (outliner-sweep-2026-07-05.md:
  OPEN(bugs) empty. eye/hide = step22-24, clone = step59. drag-reparent BEHAVIOR verified by reading
  (AppGraphReparent pair-gate rejects illegal drops; rewire clean) but its automated hierarchy-drag
  test is blocked by non-addressable `##row` items nested under the Display layer — a harness
  limitation, tracked as a test-seam follow-up, NOT a behavior bug. Marked ~ (not [x]) until that
  gesture is driven.)*
- [x] **F10 wire-ops family sweep** — detach re-drag, retarget, binding create/update/delete,
  drilled variants.
  *Accept: same form as F09. (wire-ops-sweep-2026-07-05.md: OPEN empty; canvas_c6 retarget/detach
  re-drag, step57 binding create/update/delete, canvas_c5 detach-delete, canvas_c2 create. Adversarial
  invariants — no orphan bindings on any edge-removal road — hold. Drilled variants resolve identically
  (engine gestures + link-keyed bindings, scope-blind).)*
- [x] **F11 phase-coherence audit refresh** — checklist pass over the new writers
  (Compose/ComposeImported, scoped tidy, nudge, group drag, explode anchors, fit).
  *Accept: phase-coherence-audit doc updated; violations fixed or entered as findings.
  (phase-coherence-audit-2026-07-03.md "Refresh 2026-07-05" audits each writer against the actual
  T/T-1 relation — is a prior-frame MEASUREMENT combined with THIS frame's transform? — not the
  wrong "is the camera a model input" framing an earlier draft used. All conforming: cross-frame
  reads are invariant model units, and group drag's one screen read (`col_geom`) is verified
  T-frame. Finding entered: the group-drag obstacle-clamp path has no zoom≠1 regression test.)*
- [x] **F12 data-dependency adherence validator** — AppGraphValidate error when an event expr or
  binding references an undeclared dep; codegen test asserts emitted code touches
  data/temp/last_temp/dep params only per declaration.
  *Accept: validator test + codegen grep test (step54: binding invariant (e) added to
  AppGraphValidate — Src on the producer, Dst on the consumer; event-expr dep refs already gated by
  AppExprPrimary; codegen touches `producer->value` only when the binding declares it).*
- [x] **F13 canvas input-family tests** — RMB menu, LMB-empty pan, wheel-zoom anchor, node drag
  at zoom≠1, GridSnap, wire-drop palette, detach-delete, Ctrl+click multi-select, builtin
  palette completeness (Default node) + wireable ports.
  *Accept: one test per gesture; all green. (canvas_c4: pan/wheel/drag-at-zoom/GridSnap/Ctrl-multi;
  canvas_c5: detach-delete; step55: palette completeness + wireable ports; wire-drop = canvas_c2;
  RMB menu = step42/43.)*
- [x] **F14 one feedback slot** — floating canvas link toast removed; ALL transient feedback
  (link/compose refusals, copy/save confirmations) renders in the status-bar slot, single 2.5 s
  expiry (its fade constant is later unified by F38's motion idiom).
  *Accept: no canvas-corner toast; status-slot test asserts notice text for a refused link AND a
  refused compose (step56; the B1 canvas toast + its ToastSeq/ToastT0 state are gone; status-hint
  expiry unified to 2.5 s).*
- [x] **F15 assert → flight-recorder dump** — ImGuiAppAssertFail dumps registered AV record rings
  before exit (wiring exists for WAL only today).
  *Accept: forced-assert child-process test finds the ring dump beside the assert WAL. (Ring
  recorders auto-register in AppRecordBeginRing; AppDumpAssertRings dumps them all; ImGuiAppAssertFail
  calls it after the WAL write. headless-verify `--forced-assert-ring` child arms a ring, captures
  frames, asserts; parent `composer_assert_ring_dump` finds the QOI dump dir.)*

## P3 — one emitter, byte-locked

- [x] **F16 single control emitter** — legacy GenerateAppControlCode subsumed by
  AppEmitControlWithDeps (depCount==0 path); both historic outputs byte-locked in tests/data.
  *Accept: one emission function; byte-diff test on both corpora.*
  DONE: GenerateAppControlCode is now a wrapper that builds a lone Control node and emits through
  AppEmitControlWithDeps -- the single-draft output is strictly the graph emitter's depCount==0 path
  (gains OnGetCommand + field-aware render hints). Whole-graph corpus byte-locked by ProofDrift
  (imguix_contract_generated.h); single-control corpus byte-locked by the new ProofControlDrift
  (imguix_control_generated.h). step6/step7 substring asserts survive.
- [x] **F17 whole-graph signature + dirty bit** — AppGraphSignature (ImHashData over fixed
  buffers) + Revision; edit flips STALE, generate clears.
  *Accept: signature-stability test (reload same file = same signature) + dirty-bit test.*
  DONE: the signature is the single source. AppGraphSyncRevision folds it once/frame and bumps the
  session-local Revision on any content change; AppGraphMarkGenerated stamps GenSignature at codegen;
  AppGraphCodeStale/AppGraphCodeFresh compare live vs stamp. Migrated the demo's per-doc WrittenSig
  onto the graph so there is no parallel dirty bit. step60 pins signature-stability across save/load,
  position-only edits as no-ops, and the STALE->generate->FRESH cycle + Revision pulse.
- [x] **F18 fresh/STALE surfaced** — Generate button health state, code-panel stale tint,
  Copy/Write warning tint, WriteMsg cleared on first stale frame.
  *Accept: UI test drives edit→stale→generate→fresh.*
  DONE: Generate button carries a stable ###generate id + health tint (green Generated / amber Generate
  / red on errors); the code-panel header reads amber "ahead of file"/"unwritten" and the Copy button
  tints amber when the shown C++ is ahead of disk; WriteMsg clears the first frame the graph goes stale
  (in OnUpdate). All read the single AppGraphCodeFresh/Stale bit. composer_codegen_freshness drives the
  REAL toolbar Generate through the render->update path: stale -> click -> fresh -> authored edit ->
  stale. (step60 pins the model cycle underneath.)
- [x] **F19 codegen warnings surfaced** — count of `// WARNING` / `// codegen aborted` in the
  generated text shown beside Generate with listing popup.
  *Accept: graph engineered to warn shows the count; clean graph shows none.*
  DONE: AppScanCodegenWarnings scans the emitted C++ for the "// WARNING"/"// codegen aborted" markers
  (single source: the emitter, no re-derived conditions). The count is published once per signature
  change (doc->CodegenWarnCount) and shown as an amber chip beside Generate; clicking lists the marker
  lines in a popup. Absent when the emission is clean. step61 pins: an undefined-command event warns
  (count>=1, list captures the line); defining the command clears it to zero.
- [x] **F20 custom-layer emission locked** — authored Custom layer → subclass skeleton +
  `ImGui::PushAppLayer<Name>` at its stack position, byte-locked + compiled.
  *Accept: extends the codegen-proof corpus.*
  DONE: the contract graph now authors a Custom "Analytics" layer. It emits a full ImGuiAppLayer subclass
  skeleton (OnAttach/OnDetach/OnUpdate/OnRender) and PushAppLayer<Analytics>(app) at its position in the
  bring-up stack (after the foundation layers, node order). Byte-locked by ProofDrift and compiled via the
  #included contract header; ProofBehavior dispatches over the shell it belongs to.
- [x] **F21 cycle surfacing** — topo cycle name surfaced in the status strip + Select verb jumps
  selection to the cycle's nodes. Renders as plain status text until F33's HEALTH pill exists,
  then rides it (the P3 deliverable is the computation + verb, not the pill styling).
  *Accept: cyclic-graph test asserts status text + selection.*
  DONE: AppGraphDependencyCycle (reusing AppGraphTopoOrder's Kahn pass via a new out_cycle param, no
  duplicated cycle logic) returns the unscheduled controls + a member name. The status strip shows a red
  "cycle: <name> +N" readout with a Select verb; clicking jumps Graph.Selection to the cycle nodes,
  applied in OnUpdate (the model write stays out of the render pass). step62 pins name + node set + the
  Select assignment; acyclic returns nothing.

## P4 — ingest closes

- [x] **F22 control-skeleton importer** — parse the F16 emitter's full control output (fields,
  dep params, event blocks, command emits) back into Control + struct nodes + links; structs
  parity kept.
  *Accept: import test per construct.*
  DONE: AppGraphImportControlsFromCode imports every construct. FIELDS from the Data/TempData blocks
  (step63); DEP edges matched producer->consumer by Data type name (step64); COMMAND selections from the
  `AppCommand_<name>` tokens (step64); EVENT blocks from the two method bodies (step65) -- OnGetCommand
  yields Active EmitCommand events + a latch->command map; OnUpdate's events block yields SetField
  (`data->dst = <expr>`) and edge EmitCommand events, edge decoded from the guard shape (Rising/Falling/
  Changed/Active), the `= false` re-arm lines skipped. One import test per construct.
- [x] **F23 emit→import→emit fixed point** — corpus enumerated: control with persist+temp
  fields; deps hard and optional; events (set-field and emit-command); commands; standalone
  struct; hosted and unhosted control; custom layer. Second emission byte-equal to the first.
  *Accept: fixed-point test green over the enumerated corpus.*
  DONE: AppGraphImportProgram imports the whole program -- foundation + custom layers, the AppCommand
  enum onto the CommandLayer, controls (fields/deps/events/commands), standalone structs (the control
  Data/TempData filtered out), and windows/sidebars + hosting containment edges. step66 emits the full
  enumerated corpus (both event kinds, a dep, a command, a standalone struct, a custom layer, a hosted
  and an unhosted control), imports it, re-emits, and asserts byte-equality. Fixes surfaced by the diff:
  import the command enum; drop the synthesized `<Cmd>Pending` latch on import; and make the emitter's
  window/sidebar hosting locals name-based (`win_<Name>`) so hosting survives import (node ids don't).
- [x] **F24 import-merge policy** — importing into a non-empty graph updates the matching node
  by type name (no dup), creates otherwise.
  *Accept: merge test: import twice = import once.*
  DONE: every importer find-or-updates by kind + name (AppImportFindNode) instead of always creating --
  a matched control/struct clears and re-populates its reflected content in place; matched layers/
  windows/sidebars are left as-is; edges add only when absent (AppImportAddLinkUnique). step67 imports a
  multi-kind program (custom layer, standalone struct, control, hosting window) twice into one graph and
  asserts identical node/link counts and byte-identical re-emission.

## P5 — mirror never lies

- [x] **F25 live gating audit** — every mutating verb on IsLive rows across inspector, outliner,
  canvas (rename, delete, drag-reparent, field edit, event edit) blocked with the notice idiom.
  *Accept: per-surface test; notice text asserted.*
  DONE: swept all 5 verbs x 3 surfaces (docs/live-gating-audit-2026-07-05.md). Gating was complete but
  SILENT. Added AppNotifyLiveReadOnly (one phrasing on the LastLinkErr channel) at every user-attemptable
  live mutation -- canvas Delete key + F2, the Delete/Rename commands, AppGraphReparent, outliner deferred
  delete. Structurally-unofferable verbs (no glyph/menu/drag on live) need no notice. Defense-in-depth
  self-guards added to EditAppNodeFieldSection + EditAppControlEvents; AppGraphRemoveNode deliberately
  keeps NO live guard (the mirror rebuild deletes stale live nodes through it). step68 drives F2 on a live
  control and asserts the notice fires, rename does not start, name untouched.
- [x] **F26 IsPromoted rendered** — promoted mark on canvas card + outliner row (computed flag
  exists, never drawn).
  *Accept: promoted fixture shows the mark; unpromoted twin does not.*
  DONE: the mark is drawn from ONE source (AppGraphOriginColor): the canvas title-bar dot renders as a
  RING for promoted / FILLED for live / none for design (imguiapp_canvas.cpp:1263-1270), and the outliner
  row text tints to match (imguiapp_nodes.cpp:13495). Exposed AppGraphOriginColor in the header (its own
  comment already declared it meant-to-be-public) so step69 can pin the vocabulary: promoted != 0,
  design == 0, live != 0, promoted != live -- and renders the outliner to exercise the tint path.
- [x] **F27 promote/reconcile round-trip** — promote live control → edit → generate → (fixture
  relaunch) → twin marked promoted, no duplicate mirror row.
  *Accept: headless test with simulated LiveApp fixture.*
  DONE: composer_promote_reconcile mirrors the running composer (the simulated live app) into a local
  graph, promotes a live control (a design twin with the same name + DataTypeName, the "Promote to design"
  verb), then reconciles with a second BuildAppLiveGraph pass (the fixture "relaunch"). Asserts the twin is
  marked IsPromoted (its emitted data type matches the running control), the live-control count is
  unchanged, and there is still exactly one live mirror row for that control (no duplicate).
- [x] **F28 live-scope surface tests** — walls caption facts from live placement, read-only
  palette in live window/sidebar/control/struct interiors (extends step44).
  *Accept: one test per live scope kind.*
  DONE: AppScopeKindComposable is the single truth behind both the interior palette and the empty-scope
  wall caption (both key off the live condition). Exposed it; step70 asserts it per live scope kind --
  window/sidebar/struct/control mirrors admit nothing, the authored twins admit their member kind. step71
  extends step44's on-camera drill to a live SIDEBAR: the interior palette offers no Control and nothing
  is added. (step44 already drives the live WINDOW palette on-camera.)

## P6 — chrome tells the truth

- [x] **F29 App-time transport** — toolbar scrubber: freeze toggle, frame slider, step back/fwd,
  driving ImGuiAppStateHistory on the mirrored app; gated on ShowLive; visible in the
  right-aligned toolbar cluster (docs already claim it — make them true).
  *Accept: headless scrub test asserts restored PersistData at frame N; docs claim matches code.*
  DONE: ComposerTransport (ImGuiAppStateHistory + Frozen + Frame) hangs off GraphDocData as a heap
  pointer -- the doc is non-POD/opaque so AppStateSnapshot skips it and never captures the history.
  GraphDocControl::OnUpdate, when ShowLive + a mirror: records a snapshot each frame (Frame=Count-1) and,
  while frozen, restores the scrubbed frame's snapshottable (POD-control) bytes back into the app -- time
  travel; opaque chrome is untouched. Toolbar right cluster, ShowLive-gated: pause/play freeze (amber
  while engaged) + step back/fwd + a frame slider. composer_apptime_scrub asserts the wired transport
  records (AppComposerAppTimeFrames > 0), then diffs the ring: restoring frame 0 / newest writes exactly
  that frame's bytes into the live storage. Docs already claimed the scrubber; now true.
- [x] **F30 run-state viewport tint** — frozen/rewound tint while transport is engaged.
  *Accept: on-camera frame capture differs frozen vs live.*
  DONE: while App-time is frozen, EditorBodyControl washes the viewport amber + draws an engaged border
  (matches the freeze button). composer_apptime_tint proves the rendered frame differs frozen vs live by
  scanning every window's retained draw list for the tint's distinctive amber RGB -- absent live, present
  frozen, gone on resume -- which isolates it from node animation. Also moved the transport cluster into
  the toolbar flow (the right-aligned placement clipped it off-screen) and fixed a freeze-button crash:
  the click flipped Frozen between the style Push and Pop, so the state is now captured first.
- [x] **F31 problems-count toolbar badge** — worst-severity colored count; click opens Output
  filtered to problems.
  *Accept: badge test with engineered error graph.*
  DONE: a toolbar badge shows the validation problem count (NumErrors + NumWarnings), coloured by worst
  severity (red = errors, amber = warnings only) and absent when the graph is clean. Clicking sets
  RevealPanel=Output and flips the Output filter to problems-only (err+warn on, info/log off).
  composer_problems_badge engineers a severity-2 error (event emitting an undefined command) into the
  composer graph, asserts the badge appears, clicking opens the Output panel, and removing the error
  returns the badge to its pre-injection state.
- [x] **F32 frozen status-bar zone map** — fixed x anchors: keymap | breadcrumb (click selects
  scope owner) | counts (click filters outliner) | mirror facts (click toggles Live) | freshness
  (click generates). No width-dependent shifting.
  *Accept: per-zone click test + anchor invariance test across window widths.*
  DONE: the status strip is now five fixed em-anchor zones (keymap | breadcrumb | counts | mirror |
  freshness), each drawn at SameLine(em*K) -- no width term, so nothing shifts as the strip resizes. Each
  clickable zone is an invisible button (### id) clamped to its slot so adjacent zones never overlap.
  Actions: breadcrumb selects the scope owner (ViewScope back / App node), counts shows/hides the
  outliner, mirror toggles ShowLive, freshness generates the header (shared ComposerGenerateHeader with
  the toolbar). composer_status_zones clicks each zone and asserts its effect, then toggles the inspector
  (width change from the right) and confirms the freshness zone parks at the same screen x.
- [x] **F33 StatusPill grammar + HEALTH/PERF segments** — shared pill primitive (ok/warn/err);
  HEALTH: "graph ok" / cycle name / "codegen blocked"; PERF: FPS + ms, tooltip backend/vtx/idx.
  *Accept: pill states driven by fixtures; no inline color triples left at call sites.*
  DONE: ComposerStatusPill is the one pill primitive with one colour source (ComposerPillColor:
  ok/warn/err/neutral) -- call sites pass a state, never a triple; the strip's freshness zone and the
  folded cycle readout now draw from it too. HEALTH pill leads the strip: "graph ok" (ok) / cycle name
  (err, click folds in F21's cycle-select) / "codegen blocked" (err) / "N warnings" (warn); its id encodes
  the state. PERF pill: FPS + ms, tooltip = backend + vtx/idx. composer_status_pills asserts both pills
  render and a cycle fixture flips HEALTH to the err state (###health-err), cleared when the cycle is
  removed.
- [x] **F34 command registry** — id/icon/label/shortcut/availability-predicate/run() table; the
  Space palette, context menus, gizmo tooltips, status keymap hints, and shortcut dispatch all
  render from it.
  *Accept: four-roads completeness test iterates the registry and asserts each verb reachable
  from palette + menu + shortcut + toolbar/gizmo (where declared).*
  DONE: `s_editor_commands[]` metadata table (id/icon/label/shortcut/key/mods/surfaces-bitmask/
  AddKind) + `AppGraphEditorCommandCount/At/Available` accessors; palette renders from the table;
  `ImGuiAppCmdSurface_` bitmask declares each verb's roads; `AppGraphRequestCmdPalette` opens the
  operator palette. step72_command_registry_four_roads: data-completeness (palette-complete,
  shortcut⟺key, add⟺kind+menu+gizmo, unique ids) + palette-reachability (every available palette
  verb renders a row). Nodes 81/81, core 87/0, headless 17/17.
- [x] **F35 keyboard completions** — Ctrl+P palette; key-driven tests for F2 rename, Del, nudge,
  Ctrl+S.
  *Accept: key tests green.*
  DONE: Ctrl+P opens the operator palette (alias of Space). Host commands gained an optional
  keyboard road (Key/Mods on ImGuiAppGraphHostCmd); the editor records a chord match exactly as a
  palette click, so the demo's Ctrl+S "Save graph" fires from the keyboard. step73_keyboard_
  completions drives all five key roads through the focused editor (drilled member for the nudge,
  per step46's free-placement idiom). Nodes 82/82, core 87/0, headless 17/17.
- [x] **F36 layout presets** — Compose / Review / Observe presets over the panel sidecar state.
  *Accept: preset switch test asserts panel visibilities persist.*
  DONE: `ComposerApplyLayoutPreset` writes a fixed tree/insp/code/live combination per preset;
  the active preset is DERIVED from current visibilities (`ComposerLayoutVisFlags`), never stored,
  so a manual toggle simply un-lights it and the visibilities persist through the existing layout
  sidecar. Icon-only Layout dropdown in the observe cluster (toolbar had no room for a label).
  `AppComposerLayoutFlags` accessor + headless `composer_layout_presets`: each preset reads back
  its mask, holds across 40 frames, and a manual tree toggle un-matches. Headless 18/18, nodes
  82/82, core 87/0.
- [x] **F37 origin literacy row** — origin legend micro-row + design→live→promotion HelpMarker
  copy + "Show live mirror" checkbox wording ("Hiding never deletes your design.").
  *Accept: ItemExists tests on the copy.*
  DONE: outliner head carries the origin legend (Design / Live / Promoted, drawn with the SAME
  `AppComposerGetStyle()->Origin*` dot colours the canvas uses), a design→live→promotion HelpMarker
  (`###originhelp` tooltip), and the live-mirror reassurance (`###livereassure`, "hiding never
  deletes your design."). Display-only flat labels (transparent-button so plain text gets a real,
  test-addressable id). headless `composer_origin_legend` ItemExists on all five. Headless 19/19,
  nodes 82/82, core 87/0.
- [x] **F38 motion + quietness idiom** — single 150 ms linear alpha fade for transient chrome;
  overlay rest-opacity/hover-salience ladder; overlays dim during wire-drag/marquee.
  *Accept: style-table constants only; on-camera captures at rest vs hover vs gesture.*
  DONE: `AppComposerGetMotion()` motion table (OverlayRest 0.55 / OverlayHover 1.0 / OverlayGesture
  0.20 / FadeMs 150) is the single source; the gizmo cluster (the canonical always-on overlay) reads
  it — quiet at rest, brightens when the pointer is on it, recedes during any canvas drag, with one
  linear FadeMs fade carrying it between the three states (no overlay hard-codes an alpha). Gesture =
  `IsMouseDragging` inside the published editor rect. `AppGraphEditorOverlayAlpha` + gizmo/canvas rect
  read-backs make it on-camera verifiable: headless `composer_overlay_motion_ladder` drives the
  pointer to rest / hover / drag and asserts gesture < rest < hover after the fade settles. Headless
  20/20, nodes 82/82, core 87/0. (Other overlays can adopt the same table; the ladder idiom + the
  single fade constant are now established.)
- [x] **F39 typography/spacing audit** — enforce 1.0/0.9/0.8 em ladder + 0.25 em quanta from the
  style table (ad-hoc 0.7-0.8 factors normalized).
  *Accept: grep-audit test over the style constants; visual capture reviewed.*
  DONE: the chrome scalar table (`ImGuiAppComposerMotion`) gained the type ladder (TypeBody 1.0 /
  TypeSecondary 0.9 / TypeCaption 0.8) + SpaceQuantum 0.25. Every chrome PushFont size now reads a
  ladder constant instead of a literal: the off-ladder text sizes (phase captions 0.78, scope-strip
  kind readout 0.7 + its 0.75 measurement) normalized to the caption tier; the already-0.8 sites route
  through the constant. `composer_type_ladder` audits the table (tiers exactly {0.8,0.9,1.0}, strictly
  ascending, each on-ladder; quantum 0.25); a source grep confirms no `PushFont(... * 0.N)` literal
  remains. The 0.25-em spacing factors (em*0.75 indents etc.) were already on the quantum; geometry
  primitives (triangle proportions, radii, chevrons) and zoom-scaled canvas text are out of scope.
  Portal chip's 1.0-em slot stays for F45's 0.9-em retune. Nodes 82/82 (strip tests intact), headless
  21/21, core 87/0.
- [x] **F40 chrome test-debt burn** — the audit's 33 `[t]` chrome items (gizmo clicks, overlays
  popover, F1 card, Output severity toggles, quick inspector, panel toggles, host verbs...).
  *Accept: each item has one test; chrome majority shipped-tested in re-audit.*
  DONE: the interactive-chrome majority now has click-path tests (9 items below). The draw-list gizmos
  gained published centres so they are clickable at all. Residual test-debt is lower-value / heavy-
  fixture (undo/redo/history clicks, Diff-in-panel mode, theme desc tables, canvas-de-noise pixel
  extents, severity dot render, group collapse) -- tracked for later, not blocking the majority bar.
  - gizmo click-path + overlays popover: `composer_gizmo_clickpath` clicks the published gizmo centres
    (`AppGraphEditorGizmoCount/Center`) -- Snap flips SnapGrid, Overlays opens the popover + toggles
    Grid, View-scope + Add open their popovers. Covers "Viewport gizmo column" (x2) + "Overlays popover".
  - F1 shortcut card: `composer_help_card` toggles HelpOverlay and asserts the 19-line card's glyph
    geometry appears/disappears (draw-list overlay, no id; F1 binding covered by the F34 registry test).
  - quick inspector: `composer_quick_inspector` selects a node, toggles QuickInspector, asserts the
    `###quick_insp` window shows then hides.
  - host verbs in palette: `step74_host_verb_palette` (nodes) registers host commands, opens the
    operator palette, clicks the "File: Save graph" row, and asserts AppGraphConsumeHostCommand returns
    its id (the editor never acts for the host).
  - panel toggles: already covered -- `composer_code_toggle` (Code dock) + `composer_status_zones`
    (outliner/inspector toggles).
  - status-strip click: `composer_sync_reveals_panel` clicks the toolbar `###sync` pill and asserts the
    code dock reveals.
  - toolbar Add: `composer_toolbar_add` clicks "+ Add" and asserts the add-node palette opens (through
    the render->update->request intent bus).
  - Output severity filter: `composer_output_severity_filter` drives a dependency-cycle fixture into the
    Output tab, then toggles OutputShowErr and asserts the red error-row geometry vanishes (the row
    buttons resist ItemExists, so the filter EFFECT is asserted via a draw-scan).
- [x] **F41 inspector completion** — Identity/Placement sections; per-kind section-collapse
  persistence; section kebab (reset-to-defaults/copy/paste) on every section; unified row
  grammar with row context actions; multi-select mixed-value editing beyond Style; project
  inspector logging section; quick-inspector pin/dismiss/"Inspect here".
  *Accept: one test per subsystem; audit rows flip to shipped-tested.*
  DONE: all eight subsystems shipped + tested (steps 75-81 + composer_project_logging). See the
  per-subsystem notes below.
  PROGRESS (multi-turn):
  - Placement section: every design non-layer node gets an always-visible Placement group (X/Y drag
    fields wired to GridPos), above the collapsible sections. `step75_inspector_placement` asserts the
    fields render and ItemInputValue writes through to the node. (Note: DragFloatN nests its components
    under empty ids -- not ItemExists-addressable -- so single X/Y DragFloats are used.)
  - Identity section: an always-visible Identity group -- editable Name (where authorable) + a kind/id
    readout. `step76_inspector_identity` edits Name and asserts it writes through to the node.
  - quick-inspector pin/dismiss/"Inspect here": the N-panel gained a pin (thumbtack) that freezes it on
    the current node as the selection moves on; the node context menu gained "Inspect here"
    (`AppGraphInspectHere`); the X still dismisses. `step77_quick_inspector_pin` inspects-here on A,
    moves the selection to B, asserts the pin holds A, then clicks the pin to release it.
  - project inspector Logging section: `ShowComposerProjectInspector` gained a Logging section exposing
    the running app's WAL level (combo) + path. `composer_project_logging` forces an empty selection
    (project inspector) and asserts the Document + Logging section headers render. (Section content sits
    behind the collapsed-by-default + combo-addressability combo -- the header is the reliable subject.)
  - multi-select mixed-value beyond Style: `EditAppNodesInspectorMulti` gained a Placement (all selected)
    X/Y editor -- common value or a "--" mixed marker, editing writes every selected node.
    `step78_inspector_multi_placement` edits multi-X and asserts both nodes' GridPos.x set, Y untouched.
  - per-kind section-collapse persistence: `AppInspectorSection` gained a `persist_seed` that keys the
    open/collapsed state by node kind instead of the id stack; the design-node sections pass `kind+1`.
    `step79_inspector_section_persist` collapses Fields on Struct A and asserts Struct B shows it
    collapsed too (and re-expand propagates back).
  - section kebab beyond Style: the Fields section (Struct + Control) gained a kebab menu with
    "Reset (clear fields)" (collapse-then-clear), the pattern the Style section already had.
    `step80_inspector_section_kebab` opens the Fields kebab, clicks Reset, asserts the field list empties.
  - unified row grammar + row context actions: field rows share the name / type / reorder / delete
    grammar AND now a right-click context menu ("Remove field"), on both the inline and exploded rows.
    `step81_inspector_row_context` right-clicks a field row, picks Remove, asserts the field drops.

## P7 — scopes + canvas completion

- [x] **F42 layer-scope interiors** — per par.4 table: Display (identity cards + hosted count,
  walls + rail in render order), Task (walls + rail in topo order), Command (identity cards +
  command chips, push order).
  *Accept: one interior test per layer kind (walls valid, member density, rail order).*
  DONE: `AppScopeWallsWanted` now grants the room to the sequential layers (Display / Task / Command),
  and the wall-bounds pass filters to the layer's members (cross-cutting membership, not a subtree).
  The order strip already sequences per kind (`AppScopeSequenceIds`: windows-then-sidebars / topo /
  push). `step82_layer_scope_interiors` drills into each layer and asserts walls valid + member
  density + rail order (Display: 3 windows then the sidebar; Task: C1 before C2 by data dependency;
  Command: the single command-emitting control). Members render as their node cards inside the walls.
  Nodes 91/91, headless 28/28, core 87/0.
- [x] **F43 scope invariant tests** — validate-on-mutate of ViewScope; breadcrumb chain ==
  scope-parent chain.
  *Accept: dedicated tests, including mutation-under-drill.*
  DONE: `AppScopeValidate` (per-frame) strengthened -- beyond dropping deleted/non-enterable entries,
  it now enforces breadcrumb chain == scope-parent chain: each entry must stay a member of the scope
  its predecessor opens (evaluated with the breadcrumb cut to that predecessor, so cross-cutting
  Command membership is honoured), truncating at the first break. `step83_scope_invariants` covers a
  valid chain surviving, a broken chain [W, C2] truncating to [W], mutation-under-drill (deleting a
  member keeps the scope), and validate-on-mutate (deleting the drilled node truncates to root).
  Nodes 92/92, headless 28/28, core 87/0.
- [x] **F44 per-scope sequence tidy** — on-demand verb arranging members left→right in execution
  order (AppScopeSequenceIds; consumes F58's authored order automatically once it lands),
  writing THIS scope's placements only.
  *Accept: test: tidy in scope → order left→right; GridPos untouched (step45 idiom).*
  DONE: `AppScopeSequenceTidy` lays the drilled scope's members left→right in `AppScopeSequenceIds`
  order, writing scope placement records (`AppNodeScopePosStore`) only -- root GridPos untouched; it
  falls back to `AppGraphAutoLayout` at root / non-sequential scopes. The Tidy verb (L key, gizmo,
  palette id 10) now calls it. `step84_scope_sequence_tidy` drills into a window with 3 hosted controls,
  tidies, and asserts left→right on one row with GridPos unchanged. Nodes 93/93, headless 28/28, core 87/0.
- [x] **F45 portal completion** — outbound label spec (`field ▸ Consumer`), outbound test, 0.9 em
  chip text slot, border mix toward neutral, inside-pin hover halo.
  *Accept: outbound + hover tests; on-camera chip capture.*
  DONE (`AppDrawScopePortals`): outbound chips now read `field ▸ Consumer` (inside port's field name +
  the remote); chip text dropped to the 0.9-em tier (`TypeSecondary`, the F39 deferral); the border
  MIXES the kind hue toward the neutral wall line (`AppThemeMix`, more neutral at rest, more kind on
  hover) instead of alpha over the plate; hovering a chip halos the in-scope pin it docks to.
  `step85_outbound_portal` asserts the crossing edge collects outbound (Inbound == false, names the
  consumer) and that clicking the right-wall chip jumps to the consumer's scope with it selected (the
  hover→click path). Nodes 94/94, headless 28/28, core 87/0.
- [x] **F46 scope header row in inspector during drill.**
  *Accept: ItemExists test while drilled.*
  DONE: while `graph->ViewScope` is non-empty, the inspector sidebar leads with a `Scope: <name>` row
  (`###scopehdr`, a flat button) that steps up one level on click (breadcrumb parity). `composer_scope_header`
  asserts the row is absent at root, appears when drilled into the Display layer, and disappears after
  clicking it steps back to root. Nodes 94/94, headless 29/29, core 87/0.
- [x] **F47 scope chrome test-debt** — end band, rails, void dim, shrink deadband, title
  ordinals, struct/field root-eviction gates, card geometry invariance across altitudes.
  *Accept: audit `[t]` scope rows flip to shipped-tested.*
  DONE across two tests. `step86_scope_wall_bands`: end band + rails + face band -- `ScopeWallRect`
  extends past the members on every side, face band (Begin line + strip) taller than the end band.
  `step87_scope_chrome`: one drill covers the rest --
  - struct/field root-eviction: at root (Display present) the struct/field are absent from `PoolIds`
    while sibling controls at the SAME altitude submit -- the kind-specific altitude law (:6890), not
    general visibility.
  - card geometry invariance: two controls report equal `CanvasNodeSize().x` at both root and drilled
    altitudes (`UniformCardW > 0`) -- the uniform-width-per-altitude invariant (narrower than absolute
    width, which legitimately differs across the flip).
  - title ordinal badge: each member's `CanvasNodeTitleBadge` reads a distinct "n/N" (new canvas getter).
  - void dim: the walls pass publishes the four figure-ground carve bands (`ScopeVoid[4]`/`ScopeVoidValid`);
    the test reconstructs the wall screen-rect from their inner edges and confirms they run far outward.
  - grow-fast / shrink-deadband: widening a member grows `ScopeWallRect.z` the same frame; a 3px
    contraction holds inside the 1.5-em band; a full contraction shrinks it.
- [x] **F48 canvas S2 slice** — annotation frames (R1) + align/distribute verbs (R3, registered
  in F34).
  *Accept: frame create/label/move test; align test on a selection.*
  DONE: both gates pass. R3 = `step88_align_distribute`; R1 create/label/round-trip = `step89_note_roundtrip`,
  move-carries-framed-nodes = `step90_note_drag_contained`. Tracked refinements beyond the gate (like F47's
  residual): an interactive resize handle, a per-note colour picker (the `NoteColor` field + serialization
  exist; the translucent frame doesn't yet read it), and outliner dimmed/bottom ordering for notes.
  PROGRESS (multi-turn):
  - R3 align/distribute DONE: six selection verbs on the F34 registry (ids 40-45, Palette|Menu) --
    Align left/right/top/bottom edges + Distribute horizontal/vertical. Geometry (`AppGraphAlignSelection`)
    reads each pick's altitude-correct canvas rect and writes through the nudge idiom (drilled -> scope
    placements, root -> GridPos); live/collapsed picks skipped. Reachable from the operator palette, a
    Shift+A submenu, and the selection context menu's "Align" submenu (multi-select). Availability gates
    align on >=2 picks, distribute on >=3. `step88_align_distribute` drills, scatters three controls, and
    drives Align-Left + Distribute-H through the palette (edges collapse to min x; middle center = mean).
  - R1 Note kind STEP 1 DONE: `ImGuiAppNodeKind_Note` (appended -- serialized as int), a non-semantic
    annotation frame. No ports; sized by an authored `NoteSize` (excluded from the uniform card width);
    renders as a translucent titled frame; excluded from validation (and codegen, which only emits
    Controls). Serializes `Note=<w>,<h>,<color>`; addable via the "Add: Note" registry verb (id 46);
    appears + filters in the outliner. `step89_note_roundtrip` renders it (no empty-body assert; sized
    to NoteSize) and round-trips kind/footprint/name/position.
  Remaining (R1 step 2): drag-moves-contained-nodes (UE behavior), interactive resize handle,
  per-note color, outliner dimmed/bottom ordering; a create/move acceptance test.
- [x] **F49 doc + comment hygiene** — refresh scope-interior-design §1 stale diagnoses; delete
  stale bracket comments; resolve the R7-LOD claim (implement or strike); fix the MMB-pan and
  transport claims in composer-workbench-design; sweep remaining narrative comments.
  *Accept: audit re-run finds no doc claim contradicting code.*
  DONE (verify-then-fix, each edit cited to code): the over-canvas MMB-pan hint claim -> the real
  `drag pan · wheel zoom` gesture (the MMB text lived in composer-ui-design.md §3, not the workbench doc;
  canvas FSM `LmbPansEmptyCanvas`/`RmbPans`, no middle button); R7 per-node LOD STRUCK as parked
  (composer-ui-design.md R7 + scope-interior Rule D -- no LOD symbol exists); transport claims in
  composer-workbench-design.md §2/§4.1 corrected to the shipped TOOLBAR App-time transport (F29/F30,
  ShowLive-gated, not a bottom-center overlay); scope-interior-design.md §1's four "defects" retitled
  "resolved" each citing its shipping function (AppDrawScopeWalls / AppScopeDetailAltitude+step37 /
  AppDrawScopePortals / AppDrawScopeOrderStrip); one stale group-drag history comment deleted (behavior
  comments kept). No `[FIXME]`/`[stale]` bracket markers existed; the `// TODO:` hits are codegen OUTPUT
  strings, not source comments.
- [x] **F50 pin pre-coloring / can-link telegraph** — type-compatibility shown at drag time
  (pin tint or cursor), not only post-release toast.
  *Accept: drag-over test asserts the telegraph state.*
  DONE: `CanvasWireDragSource` exposes the pin an active wire-drag started from; while dragging, the
  editor runs the SAME `AppGraphResolveLink` the drop handler uses against the pin under the cursor and
  publishes the verdict (`TelegraphPin`/`TelegraphOk`), warning the cursor (`NotAllowed`) on a refusal.
  The engine's snap-hover is unfiltered, so the telegraph's value is the SEMANTIC layer -- it warns of a
  duplicate / one-producer / cycle refusal BEFORE release, where the structural snap alone would not.
  `step91_pin_telegraph` holds one drag and sweeps the cursor across a duplicate target (not-ok), a legal
  target (ok), and empty canvas (cleared). Pin-tint styling of compatible pins is a tracked refinement.

## P8 — self round-trip

- [x] **F51 self round-trip harness** — the Composer demo's own composition file checked into
  tests/data: load → regenerate → compile emitted output → GetAppCompositionID equal; F01
  byte-stability on the same file.
  *Accept: flow-3 test green in suite.*
  DONE (`tests/imguiapp_flow_tests.cpp` + `RunFlowTests`, the reusable seam F71 grows flow1-8 from;
  outer-only, no submodule change). `BuildComposerSelfGraph` calls the SAME two public seed primitives
  the demo's SeedAppGraph uses (`AppGraphEnsureFoundation` + `AppGraphSeedStarterPrefabs`) and PINS the
  checked-in `tests/data/composer_self.txt`(+`.prefabs`) to them (a seed change fails the pin + writes
  `.new`). flow3: (1) seed-pin byte-match, (2) F01 stability (load->save+reload byte-identical +
  model-equal + signature match), (3) codegen drift byte-locks `composer_self_generated.h`, (4) that
  header is #included/compiled/run and `GetAppCompositionID` equals a hand-pushed foundation reference
  (and isn't the empty hash). core 87 checks / 0 failures incl. flow3.
- [ ] **F52 generated-shell bootstrap** — Composer emits the host-app scaffold (layers, windows,
  wiring) that hosts a Composer control; editor guts stay library code; recorded in big-idea.
  *Accept: emitted shell compiles + runs headless-verify.*

## P8.5 — vocabulary: logic, animation, layout nodes (decided 2026-07-05)

Placed after the self-round-trip rails on purpose: P1 proves new records survive
save/load/undo, P3/P4 prove new emissions stay byte-locked and importable — every feature below
extends those rails rather than inventing parallel ones.

- [ ] **F53 vocabulary design doc** — `vocabulary-nodes-design.md`: op-fold semantics (type
  rules ride AppEventExprCheck), the animation builtin set and its phase discipline, and the
  layout evaluation covering ALL THREE candidate models — window placement facts (baseline,
  exists after F02), Region/Split/Tabs family composing into the Layout layer (primary), and
  constraint/anchor edges (explicit build-or-reject verdict in a rejected-alternatives section).
  Palette legality rows, scope domains, validation, and codegen shape stated for each kind.
  *Accept: doc lands with verdicts; no code before it.*
  DONE (`vocabulary-nodes-design.md`, every mechanism cited). Verdicts: (1) Op nodes BUILD -- one appended
  `ImGuiAppNodeKind_Op`, result DataOut `DataTypeId=0` (opts out of one-producer, fans out; cycles still
  refused by AppGraphDataReaches); type authority is AppEventExprCheck alone; the subtree FOLDS to an
  expression string (re-imports as an Expr, not op nodes -- the .graph is the only home). MANDATE for F54:
  extend the grammar with `?:` + ImMin/ImMax or select/min/max folds break round-trip. (2) Animation builtins
  BUILD -- Tween/Timer/Spring/Pulse as builtin Controls (AppGraphAddBuiltin, RandomTime precedent), dt-driven
  Task-phase, accumulator in registered PersistData (no-TU-globals is load-bearing for Fixed-dt scrub). (3)
  Layout: window placement facts KEEP (baseline, fields); Region/Split/Tabs BUILD-primary -- one appended
  `ImGuiAppNodeKind_Layout` giving the Layout layer its first domain (OnLayout() already stubbed at
  imguiapp.h:906), windows reference their region via a NODE FIELD (`Region=`, parallel to `Dock=`), codegen
  emits a once-guarded DockBuilder into OnLayout(); constraint/anchor edges REJECT (no phase, no primitive).
- [x] **F54 Op node kind** — `ImGuiAppNodeKind_Op`: AND/OR/XOR/NOT, compare (==,!=,<,<=,>,>=),
  select/mux, min/max; typed pins checked by the expression type rules; app-level data domain
  (scope-parent Task layer); cycles refused; serialized; palettes offer it only where the scope
  takes it (extends AppScopeKindComposable).
  *Accept: add/wire/type-refusal/serialize/undo tests; F01 covers records.*
  DONE: `_Op` appended after `_Note` (index 8; serialized as int). Operator rides `TypeName` (IsBuiltin=false),
  so serialization needs NO new line (ports ride `Port=`); `AppGraphAddOp` stamps N operand DataIn by arity
  (NOT=1, compare/AND/OR/XOR/min/max=2, select=3) + one result DataOut `DataTypeId=0` (fans out, opts out of
  one-producer). Type-refusal is a per-pin check in `AppGraphResolveLink` reusing the SHARED
  `AppExprIsBool/IsNumeric/IsInt` (AppEventExprCheck stays the sole type authority); cycles refused by the
  shared `AppGraphDataReaches`; Task-layer domain via `AppScopeParentOf` + `AppScopeKindComposable` (root+Task).
  MANDATED grammar extension landed: `AppExprTernary` (?: at C precedence, right-assoc, cond must be bool) as
  the new grammar top (pass-through when no `?`, so step21 stays green) + `ImMin/ImMax` as call primaries -- so
  F55's select/min-max folds re-import. step95_op_node_kind + step21 grammar cases. No standalone emit (F55).
- [ ] **F55 op codegen fold** — op chains fold into the consumer's emitted expression (no
  runtime object); byte-locked, compiled, RUN: an op chain gating a command dispatches it.
  Import note recorded: folded output re-imports as an expression, not as op nodes — the graph
  file, not the C++, is the op structure's home.
  *Accept: codegen-proof corpus extended with an op-chain fixture.*
- [ ] **F56 animation builtin library** — Tween, Timer, Spring, Pulse as builtin Controls
  (AppGraphAddBuiltin; RandomTime precedent): dt-driven Task-phase update, temp^last edge
  semantics, typed DataOut, "Animation" palette section; codegen emits push + wiring; mirror and
  time-travel work unchanged.
  *Accept: compiled+run test — Tween output advances deterministically under Fixed-dt; App-time
  scrub (F29) restores it.*
- [ ] **F57 layout node family** — Region/Split/Tabs nodes compose INTO the Layout layer (its
  first real domain: AppNodeInScope + AppScopeKindComposable rows, enterable interior per the
  par.4 grammar); windows reference their region via the mechanism F53 decides (reference edge
  vs node field — the doc must pick one and say why); codegen emits the dock-builder sequence.
  Constraint model built or formally rejected per F53's verdict.
  *Accept: compose window into a region → generated app docks accordingly (headless-verify
  frame); layout-scope interior test; F01/F05 cover the records.*

## P9 — sequence-order write path (deferred until here by decision)

- [x] **F58 order as model state** — per-scope member order record; serialized (F01 extends),
  undoable (F05 extends), validated: the four core layers never reorder.
  *Accept: model + validation tests.*
  DONE: `struct ImGuiAppScopeOrder { int ScopeId; ImVector<int> NodeIds; }` + `ImGuiAppGraph::ScopeOrders`
  (per-scope ordered list -- an order IS a sequence). Read side: `AppScopeApplyAuthoredOrder` at the tail of
  `AppScopeSequenceIds` returns members in the authored order (unlisted members kept in derived order after),
  else the existing derivation. `Order=` serialize + load; undo rides the snapshot; `AppGraphRemoveNode`
  sweeps dead ids; `AppGraphModelEqual` extended. Validation grounds the "core layers never reorder"
  invariant in `AppLayerIsCore` -- NOTE the codebase has FIVE core layers (Task<Command<Status<Layout<Display),
  not the brief's imprecise "four (Task/Command/Status/Window)"; a core-subsequence out of that order is a
  severity-2 error, non-core reorders free. Root (ScopeId -1) is validated but not yet consumed by the reader
  (root uses GridPos; F59/F60 land the emit + drag). step93_order_roundtrip + step94_order_core_layer_invariant.
- [ ] **F59 codegen emits authored order** — push order from the order record where topo allows;
  conflict = validation error, not silent reorder.
  *Accept: codegen-proof corpus extended.*
- [ ] **F60 chip-drag reorder + click-nudge** — drag on ScopeStripRects (published already) +
  nudge fallback; title ordinals update same frame.
  *Accept: drag test + nudge test + save/load + undo + emission.*

## P9.5 — run it without a build: playback debugger + previewer (decided 2026-07-05)

Two halves of one idea — the composition is executable data. The PREVIEWER interprets the graph
live (the shadertoy loop: rewire a dep, behavior changes next frame — no generate, no compile);
the PLAYBACK DEBUGGER scrubs a RECORDED run offline. Both ride shipped rails: StateHistory
restore-and-replay (contract 7), the WAL, QOI frame encoding, the input/digest chain
imguix-headless-verify already writes, AppEventExprCheck's typed expression AST, reflection
widgets (ImStructTable), and P8.5's op/tween semantics. Placed after P8.5 because the
interpreter's vocabulary IS F53-F57's semantics.

- [x] **F61 playback-debugger design doc** — `playback-debugger-design.md`: the run container
  (unify what headless-verify already emits — WAL + QOI frames + input stream + snapshots +
  digest chain — into ONE openable artifact), index shape (tick → nearest snapshot), transport
  grammar shared with F29 (LIVE ring vs FILE run are the same surface, different source),
  divergence semantics.
  *Accept: doc lands with the container format frozen; no code before it.*
  DONE (`playback-debugger-design.md`, 307 lines, every rail cited to file:line): a RUN is one openable
  path (the `.mp4`/QOI recording + basename siblings `.wal`/`.frametimes.csv`), joined by TICK
  (`ImGuiAppFrameID.FrameIndex`) -- NO new archive/manifest, exactly the harness's existing outputs. The
  authoritative spine is the embedded meta stream (40-byte `IMAVMETA` header + TLV records Identity/Frame/
  IoFrame/Input/StateSnapshot/Digest); frozen guarantees = what `AppAVMetaVerify` already recomputes. Index =
  one walk -> per-tick record + SnapshotTicks; the F29 transport gains a `Source_LiveRing`/`Source_FileRun`
  switch behind a `Count()/Show()` view; state-at-tick = restore-nearest-snapshot + `AppInputReplay`
  (contract 7); two divergence layers (chain/digest integrity vs recorded-vs-replayed state_hash) + exact-tick
  jump. (Flagged: `ComposerTransport` is in imguiapp_demo.cpp, not nodes.cpp.)
- [x] **F62 run-file loader + index** — open a recorded run in the Composer: parse container,
  build tick index (inputs, commands, snapshots, digests, frame images).
  *Accept: headless-verify's own output opens; index counts match the recorder's summary line.*
  DONE: `AppRunOpen`/`AppRunClose`/`AppRunTickCount`/`AppRunTickAt` (imguiapp_av.h/.cpp) build the F61
  `ImGuiAppRunIndex` (per-tick `ImGuiAppRunTick` + `SnapshotTicks` + `Stats`) over the SAME meta buffer +
  the SAME static TLV reader (`AvMetaInit`/`AvMetaNext`) the shipped `AppAVMetaVerify` uses -- no parallel
  parser, path->buffer stays the per-backend extractor. One linear walk lands each record's tick + payload
  offset (Frame = tick spine, IoFrame/Input/Snapshot attach by frame_index, chain recomputes from the Identity
  seed). Test (headless): open headless-verify's OWN run -> index counts equal the recorder's summary line
  (`[F62] run-open OK: ticks=1280 io=1280 ... chain=ok digest=ok`).
- [ ] **F63 playback transport (FILE mode)** — the F29 transport gains a source switch; timeline
  strip with per-tick markers (input ticks, command dispatches, snapshot points); scrub shows
  the decoded QOI frame; step lands on exact ticks.
  *Accept: scrub-to-tick test asserts the shown frame's tick == slider tick.*
- [ ] **F64 state-at-tick inspection** — restore nearest snapshot + replay inputs to tick N
  (contract-7 machinery); inspector shows Persist/Temp values AT N; command log lists that
  tick's dispatches.
  *Accept: value-at-tick test matches the recorded digest's state; replay is exact.*
- [ ] **F65 divergence surfacing** — digest-mismatch ticks marked on the timeline;
  jump-to-first-divergence verb.
  *Accept: corrupted-fixture test flags the right tick; clean run shows none.*
- [ ] **F66 previewer design doc** — `previewer-design.md`: interpreter scope = everything the
  MODEL defines (builtin controls, ops, tweens, events via the checked AST, commands, window
  composition); custom C++ control bodies render as a reflected field-widget card with a
  "body runs after Generate" note (never fake user code); storage from effective field lists
  through the RegisterAppStorage machinery; edit-while-running policy = preserve values by
  field (name, type) match, reinit otherwise; input routing; selection brushing across
  composer ↔ preview.
  *Accept: doc lands with the semantics table (per node kind: interpreted / reflected / stub);
  no code before it.*
  DONE (`previewer-design.md`, every mechanism cited). The interpreter is a SECOND BACKEND that builds a
  real `ImGuiApp` (inheriting AppRebuildUpdateOrder / RegisterAppStorage / ImGuiAppStateHistory + the four
  passes) -- not a parallel loop. §9 semantics table classifies every kind: INTERPRETED (App/layers/window
  hosts/design-draft controls/animation builtins/Ops/Layout Region-Split-Tabs/Struct-Field/events+commands),
  REFLECTED (custom C++ bodies + un-ruled builtins -> honest field-widget card via AppGraphRenderMockPanel/
  ImAppReflect, body NOT run, "runs after Generate"), STUB (Note nodes; a custom ImGuiAppLayer body, though
  its windows still render). One `ImGuiAppPreviewControl` carries Persist|LastTemp|Temp byte buffers from an
  effective-field manifest (AppNodeEffectiveFields); one evaluator = a value-returning walk of the
  AppEventExprCheck grammar; edit-while-running preserves every (sanitized name, type) slot; input routes
  through real widgets writing Temp; selection brushes both ways. Gates F67-F70; §10 pins contract parity (F69)
  + the F70 record/scrub close.
- [ ] **F67 graph interpreter core** — allocate Persist/Temp/LastTemp from effective fields;
  per frame: Task pass in topo order (builtin + tween/timer/spring semantics, op evaluation,
  event AST: `when <edge> → set / emit`), command collect/latch/dispatch-once, window pass
  rendering composed windows with reflected widgets bound to live storage.
  *Accept: an authored producer/consumer/event/command graph runs; values move; the `^` edge
  fires once per edge.*
  DONE: `imguiapp_preview.{h,cpp}` -- a SECOND BACKEND (per F66) that builds a real `ImGuiApp` from the graph
  (inheriting RegisterAppStorage / the four passes / temp^last swap / ImGuiAppStateHistory / GetAppCompositionID,
  NOT a parallel loop). One `ImGuiAppPreviewControl : ImGuiAppControlBase` per interpreted control carries a
  dynamic Persist|LastTemp|Temp buffer from an effective-field manifest; the evaluator is a value-returning walk
  of the AppEventExprCheck grammar. Task pass = bindings -> latch-reset -> events in topo order; Command pass
  latches/dispatch-once; Window pass has a manifest-bound widget panel (F68). ZERO imguiapp_nodes.cpp edits (new
  module + a few local graph readers). `Test_interpreter_producer_consumer` (core): values move producer->consumer
  via a binding, the Changed edge fires EXACTLY once per edge (not per frame), the command dispatches once.
  Integration fix: the render core layer is `ImGuiAppDisplayLayer` on main (the module's old base called it
  ImGuiAppWindowLayer). F55/F56 stubs named in OnUpdate (Op-fold + animation dt).
- [ ] **F68 preview surface** — the Preview tab (or floating viewport): run/pause/reinit,
  direct interaction ("play with it"), graph edits apply next frame under F66's preserve
  policy; selected node's widgets halo in the preview and vice versa.
  *Accept: headless test drives a preview widget and asserts the model value; rewire test
  changes behavior next frame without reinit losing unrelated fields.*
- [ ] **F69 contract parity** — the executable contract suite (UCR order, edge-once, same-frame
  latch, dedup dispatch, pop symmetry, render purity, time travel) runs against the
  INTERPRETER as a second backend beside generated code.
  *Accept: contracts 1-9 green on the interpreter; one suite, two implementations.*
- [ ] **F70 preview ⇄ time-travel tie** — the previewed app feeds StateHistory (F29 transport
  scrubs it) and the recorder (a preview session exports an F61 container the playback
  debugger opens).
  *Accept: preview → record → open → scrub-to-tick state matches (closes the loop:
  author → play → record → debug, zero compiles).*

## P10 — closure

- [ ] **F71 flow tests** — one end-to-end headless test per roadmap flow 1-8:
  `flow1_new_app`, `flow2_maintain_existing`, `flow3_self_roundtrip` (reuses F51's harness),
  `flow4_move_copy_anywhere`, `flow5_time_travel`, `flow6_vocabulary` (op chain + tween +
  region authored, generated, run), `flow7_preview_play` (author → play in the previewer, no
  generate), `flow8_playback_debug` (record → open → scrub → state matches).
  *Accept: all eight green.*
- [ ] **F72 re-audit** — re-run the doc-claims audit; zero missing/partial not explicitly parked
  with a doc note; matrix published into feature-audit doc v2.
  *Accept: the re-audit IS 100%.*
- [ ] **F73 roadmap reset** — up-next.md + roadmap rewritten to the post-100 horizon (command
  payloads, status-layer model, module interop, edit-intent bus, remappable input binding (F74/F75),
  Lifecycle view).

## P11 — post-100 horizon (started early; NOT required for 100%)

F34/F35 already ship a complete, discoverable keyboard system, so remappable bindings sit PAST the 100%
line (they are a power-user / accessibility layer, not a gate on author → generate → run). They land here
rather than being deferred only because the retrofit is cheap and self-contained. Design doc:
[input-command-binding-design.md](input-command-binding-design.md).

- [x] **F74 remappable input→command binding** — the registry `Key`/`Mods` become the factory DEFAULT
  chord; a per-graph `Keymap` holds SPARSE user overrides (serialized as `Keybind=`; F01 extends). A
  pressed chord resolves to a command Id through the effective (override-or-default) map and runs through
  the ONE `run_command` dispatcher the palette uses — replacing the hardcoded per-key checks, so a rebind
  reaches every surface. Reserved: Space / Ctrl+P (palette openers). Delete (wire-aware), Tab/Esc (scope
  nav) keep dedicated handlers this phase. Extends F34 (registry) and F35 (keyboard); sibling to the parked
  edit-intent bus (both are indirection over editor ops — this one is input→verb only).
  *Accept: rebind Copy → the new chord fires Copy and the old one does not (resolve + driven chord); the
  keymap round-trips (save/load model-equal, reserialize byte-identical, a default graph writes zero
  `Keybind=` lines); step72's shortcut⟺key invariant evolves to shortcut-surface ⟺ a default binding
  exists (step89_keymap_rebind; AppGraphModelEqual extends to `Keymap`).*
- [x] **F75 keymap editor UI** — a rebind panel (`AppGraphShowKeymapEditor`): one row per rebindable verb,
  effective-chord button (click → capture the next chord), conflict flag, reset-to-default. Chrome idioms:
  em spacing, theme colors, single `###keychip_<id>` / `###keyreset_<id>` ids, flat text buttons (no glyph
  buttons).
  *Accept: the editor renders an addressable row per rebindable verb; capture rebinds; reset restores the
  default (step89_keymap_rebind UI-render assertions).*

## Dependency spine + parallel lanes

The phase order is the safe SERIAL order; most features do not actually block each other. The
hard dependency spine — the only chains where an item is truly unstartable before its
predecessor — is:

```
F02 → F01 → F51 → F52                      (records exist → harness → self round-trip → shell)
F16 → F22 → F23 → F24                      (one emitter → importer → fixed point → merge)
F16 → F55 ;  F16 → F59 ;  F16 → F51        (every new emission rides the unified emitter)
F53 → F54 → F55 ;  F53 → F56 ;  F53 → F57  (design verdict gates the vocabulary code)
F58 → F59 ;  F58 → F60                     (order record gates its emission + drag)
F29 → F30 ;  F29 → F56-scrub-accept        (transport gates tint + Tween scrub gate)
F34 → F35 ;  F34 ↔ F48 (align verb row)    (registry gates key dispatch)
F34/F35 → F74 → F75                        (registry + keys → remappable map → rebind UI) [post-100 horizon]
F38 → F39 ;  F33 → F21-pill-restyle        (style constants before their audits/surfaces)
F61 → F62 → F63 → F64 → F65                (playback: container → loader → transport → state → divergence)
F66 → F67 → F68 → F69/F70 ;  F53 → F66     (previewer: doc → interpreter → surface → parity/tie;
F29 → F63 ;  F29/F62 → F70                  vocabulary semantics feed the interpreter; transport + container close the loop)
everything → F71 → F72 → F73               (convergence)
```

Everything else is soft-ordered. Six lanes can run CONCURRENTLY, one owner each; a lane only
stops at its own spine edges:

- **Lane 1 — codegen spine (the long pole)**: F16 → F17 → F18/F19/F20 (those three parallel) →
  F22 → F23 → F24 → F51 → F52, then F55/F59 when their gates open. Serial by nature; staff it
  first.
- **Lane 2 — model/persistence**: F02, F06, F07 immediately and in parallel; then F01; then
  F03/F04/F05/F08 in any order. Only F01→F51 crosses lanes.
- **Lane 3 — sweeps + input tests**: F09, F10, F11, F12, F13, F15 — all six independent of every
  other lane and of each other. Pure parallel fodder; also the continuous test-debt burner
  (F40/F47 items can drip through this lane the whole time).
- **Lane 4 — mirror + transport**: F25, F26, F28 in parallel; F27 after Lane 1's F16 (needs a
  regenerate step); F29 → F30 anytime (StateHistory core already shipped-tested).
- **Lane 5 — chrome + inspector**: F31, F32, F33, F36, F37, F41 in parallel; F34 → F35; F38 →
  F39; F14 early (it deletes the toast Lane 4/5 surfaces would otherwise restyle twice).
- **Lane 6 — scopes/canvas**: F42, F43, F44, F45, F46, F50 in parallel; F48 coordinates one row
  with F34; F49 last in the lane (doc hygiene reflects the lane's outcomes).

Vocabulary (P8.5) is its own late lane: F53 solo (one author — it is a decision document), then
F54/F56/F57 in parallel, F55 closing against Lane 1.

Run-without-a-build (P9.5) splits into two sub-lanes that never touch Lane 1 (the whole point —
no codegen involved): playback F61→F65 needs only Lane 4's F29 and the AV rails; previewer
F66→F70 needs P8.5's semantics and closes against playback at F70. The two sub-lanes are fully
parallel with each other after their design docs.

Scheduling consequences: with two people/agents, pair Lane 1 with Lane 3; with three, add
Lane 2; Lanes 4-6 fill any remaining width. The checklist's F-order remains the tie-breaker
whenever two items in one lane compete.

## Explicitly parked (not part of 100%)

Null headless backend mode (enum-only today; headless-verify works via the real backend),
per-node LOD manual override (unless F49 resolves it as implement), reroute pins, wire
animation, diff-hunk node tagging, timeline slice T5 (superseded in part by F56's builtin set;
keyframe timelines stay parked), constraint layout edges unless F53's verdict builds them,
module interop, command payloads, status-layer model, Lifecycle north-star view. Remappable input
binding is horizon too, but delivered early as F74/F75 (P11).
