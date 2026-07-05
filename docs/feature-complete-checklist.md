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

- [ ] **F09 outliner family sweep** — drag-reparent (control↔windows, field↔structs, drilled
  variants), eye/hide, clone icon; fix + test what falls out.
  *Accept: family sweep doc with empty OPEN list; tests per verb.*
- [ ] **F10 wire-ops family sweep** — detach re-drag, retarget, binding create/update/delete,
  drilled variants.
  *Accept: same form as F09.*
- [x] **F11 phase-coherence audit refresh** — checklist pass over the new writers
  (Compose/ComposeImported, scoped tidy, nudge, group drag, explode anchors, fit).
  *Accept: phase-coherence-audit doc updated; violations fixed or entered as findings.
  (phase-coherence-audit-2026-07-03.md "Refresh 2026-07-05" classifies all seven writers as
  conforming — camera is an input to none; group drag carries the §1b kEps deadband; verdict
  test-backed by step45/46/43 + canvas_c1/c4. No new violations.)*
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

- [ ] **F16 single control emitter** — legacy GenerateAppControlCode subsumed by
  AppEmitControlWithDeps (depCount==0 path); both historic outputs byte-locked in tests/data.
  *Accept: one emission function; byte-diff test on both corpora.*
- [ ] **F17 whole-graph signature + dirty bit** — AppGraphSignature (ImHashData over fixed
  buffers) + Revision; edit flips STALE, generate clears.
  *Accept: signature-stability test (reload same file = same signature) + dirty-bit test.*
- [ ] **F18 fresh/STALE surfaced** — Generate button health state, code-panel stale tint,
  Copy/Write warning tint, WriteMsg cleared on first stale frame.
  *Accept: UI test drives edit→stale→generate→fresh.*
- [ ] **F19 codegen warnings surfaced** — count of `// WARNING` / `// codegen aborted` in the
  generated text shown beside Generate with listing popup.
  *Accept: graph engineered to warn shows the count; clean graph shows none.*
- [ ] **F20 custom-layer emission locked** — authored Custom layer → subclass skeleton +
  `ImGui::PushAppLayer<Name>` at its stack position, byte-locked + compiled.
  *Accept: extends the codegen-proof corpus.*
- [ ] **F21 cycle surfacing** — topo cycle name surfaced in the status strip + Select verb jumps
  selection to the cycle's nodes. Renders as plain status text until F33's HEALTH pill exists,
  then rides it (the P3 deliverable is the computation + verb, not the pill styling).
  *Accept: cyclic-graph test asserts status text + selection.*

## P4 — ingest closes

- [ ] **F22 control-skeleton importer** — parse the F16 emitter's full control output (fields,
  dep params, event blocks, command emits) back into Control + struct nodes + links; structs
  parity kept.
  *Accept: import test per construct.*
- [ ] **F23 emit→import→emit fixed point** — corpus enumerated: control with persist+temp
  fields; deps hard and optional; events (set-field and emit-command); commands; standalone
  struct; hosted and unhosted control; custom layer. Second emission byte-equal to the first.
  *Accept: fixed-point test green over the enumerated corpus.*
- [ ] **F24 import-merge policy** — importing into a non-empty graph updates the matching node
  by type name (no dup), creates otherwise.
  *Accept: merge test: import twice = import once.*

## P5 — mirror never lies

- [ ] **F25 live gating audit** — every mutating verb on IsLive rows across inspector, outliner,
  canvas (rename, delete, drag-reparent, field edit, event edit) blocked with the notice idiom.
  *Accept: per-surface test; notice text asserted.*
- [ ] **F26 IsPromoted rendered** — promoted mark on canvas card + outliner row (computed flag
  exists, never drawn).
  *Accept: promoted fixture shows the mark; unpromoted twin does not.*
- [ ] **F27 promote/reconcile round-trip** — promote live control → edit → generate → (fixture
  relaunch) → twin marked promoted, no duplicate mirror row.
  *Accept: headless test with simulated LiveApp fixture.*
- [ ] **F28 live-scope surface tests** — walls caption facts from live placement, read-only
  palette in live window/sidebar/control/struct interiors (extends step44).
  *Accept: one test per live scope kind.*

## P6 — chrome tells the truth

- [ ] **F29 App-time transport** — toolbar scrubber: freeze toggle, frame slider, step back/fwd,
  driving ImGuiAppStateHistory on the mirrored app; gated on ShowLive; visible in the
  right-aligned toolbar cluster (docs already claim it — make them true).
  *Accept: headless scrub test asserts restored PersistData at frame N; docs claim matches code.*
- [ ] **F30 run-state viewport tint** — frozen/rewound tint while transport is engaged.
  *Accept: on-camera frame capture differs frozen vs live.*
- [ ] **F31 problems-count toolbar badge** — worst-severity colored count; click opens Output
  filtered to problems.
  *Accept: badge test with engineered error graph.*
- [ ] **F32 frozen status-bar zone map** — fixed x anchors: keymap | breadcrumb (click selects
  scope owner) | counts (click filters outliner) | mirror facts (click toggles Live) | freshness
  (click generates). No width-dependent shifting.
  *Accept: per-zone click test + anchor invariance test across window widths.*
- [ ] **F33 StatusPill grammar + HEALTH/PERF segments** — shared pill primitive (ok/warn/err);
  HEALTH: "graph ok" / cycle name / "codegen blocked"; PERF: FPS + ms, tooltip backend/vtx/idx.
  *Accept: pill states driven by fixtures; no inline color triples left at call sites.*
- [ ] **F34 command registry** — id/icon/label/shortcut/availability-predicate/run() table; the
  Space palette, context menus, gizmo tooltips, status keymap hints, and shortcut dispatch all
  render from it.
  *Accept: four-roads completeness test iterates the registry and asserts each verb reachable
  from palette + menu + shortcut + toolbar/gizmo (where declared).*
- [ ] **F35 keyboard completions** — Ctrl+P palette; key-driven tests for F2 rename, Del, nudge,
  Ctrl+S.
  *Accept: key tests green.*
- [ ] **F36 layout presets** — Compose / Review / Observe presets over the panel sidecar state.
  *Accept: preset switch test asserts panel visibilities persist.*
- [ ] **F37 origin literacy row** — origin legend micro-row + design→live→promotion HelpMarker
  copy + "Show live mirror" checkbox wording ("Hiding never deletes your design.").
  *Accept: ItemExists tests on the copy.*
- [ ] **F38 motion + quietness idiom** — single 150 ms linear alpha fade for transient chrome;
  overlay rest-opacity/hover-salience ladder; overlays dim during wire-drag/marquee.
  *Accept: style-table constants only; on-camera captures at rest vs hover vs gesture.*
- [ ] **F39 typography/spacing audit** — enforce 1.0/0.9/0.8 em ladder + 0.25 em quanta from the
  style table (ad-hoc 0.7-0.8 factors normalized).
  *Accept: grep-audit test over the style constants; visual capture reviewed.*
- [ ] **F40 chrome test-debt burn** — the audit's 33 `[t]` chrome items (gizmo clicks, overlays
  popover, F1 card, Output severity toggles, quick inspector, panel toggles, host verbs...).
  *Accept: each item has one test; chrome majority shipped-tested in re-audit.*
- [ ] **F41 inspector completion** — Identity/Placement sections; per-kind section-collapse
  persistence; section kebab (reset-to-defaults/copy/paste) on every section; unified row
  grammar with row context actions; multi-select mixed-value editing beyond Style; project
  inspector logging section; quick-inspector pin/dismiss/"Inspect here".
  *Accept: one test per subsystem; audit rows flip to shipped-tested.*

## P7 — scopes + canvas completion

- [ ] **F42 layer-scope interiors** — per par.4 table: Display (identity cards + hosted count,
  walls + rail in render order), Task (walls + rail in topo order), Command (identity cards +
  command chips, push order).
  *Accept: one interior test per layer kind (walls valid, member density, rail order).*
- [ ] **F43 scope invariant tests** — validate-on-mutate of ViewScope; breadcrumb chain ==
  scope-parent chain.
  *Accept: dedicated tests, including mutation-under-drill.*
- [ ] **F44 per-scope sequence tidy** — on-demand verb arranging members left→right in execution
  order (AppScopeSequenceIds; consumes F58's authored order automatically once it lands),
  writing THIS scope's placements only.
  *Accept: test: tidy in scope → order left→right; GridPos untouched (step45 idiom).*
- [ ] **F45 portal completion** — outbound label spec (`field ▸ Consumer`), outbound test, 0.9 em
  chip text slot, border mix toward neutral, inside-pin hover halo.
  *Accept: outbound + hover tests; on-camera chip capture.*
- [ ] **F46 scope header row in inspector during drill.**
  *Accept: ItemExists test while drilled.*
- [ ] **F47 scope chrome test-debt** — end band, rails, void dim, shrink deadband, title
  ordinals, struct/field root-eviction gates, card geometry invariance across altitudes.
  *Accept: audit `[t]` scope rows flip to shipped-tested.*
- [ ] **F48 canvas S2 slice** — annotation frames (R1) + align/distribute verbs (R3, registered
  in F34).
  *Accept: frame create/label/move test; align test on a selection.*
- [ ] **F49 doc + comment hygiene** — refresh scope-interior-design §1 stale diagnoses; delete
  stale bracket comments; resolve the R7-LOD claim (implement or strike); fix the MMB-pan and
  transport claims in composer-workbench-design; sweep remaining narrative comments.
  *Accept: audit re-run finds no doc claim contradicting code.*
- [ ] **F50 pin pre-coloring / can-link telegraph** — type-compatibility shown at drag time
  (pin tint or cursor), not only post-release toast.
  *Accept: drag-over test asserts the telegraph state.*

## P8 — self round-trip

- [ ] **F51 self round-trip harness** — the Composer demo's own composition file checked into
  tests/data: load → regenerate → compile emitted output → GetAppCompositionID equal; F01
  byte-stability on the same file.
  *Accept: flow-3 test green in suite.*
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
- [ ] **F54 Op node kind** — `ImGuiAppNodeKind_Op`: AND/OR/XOR/NOT, compare (==,!=,<,<=,>,>=),
  select/mux, min/max; typed pins checked by the expression type rules; app-level data domain
  (scope-parent Task layer); cycles refused; serialized; palettes offer it only where the scope
  takes it (extends AppScopeKindComposable).
  *Accept: add/wire/type-refusal/serialize/undo tests; F01 covers records.*
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

- [ ] **F58 order as model state** — per-scope member order record; serialized (F01 extends),
  undoable (F05 extends), validated: the four core layers never reorder.
  *Accept: model + validation tests.*
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

- [ ] **F61 playback-debugger design doc** — `playback-debugger-design.md`: the run container
  (unify what headless-verify already emits — WAL + QOI frames + input stream + snapshots +
  digest chain — into ONE openable artifact), index shape (tick → nearest snapshot), transport
  grammar shared with F29 (LIVE ring vs FILE run are the same surface, different source),
  divergence semantics.
  *Accept: doc lands with the container format frozen; no code before it.*
- [ ] **F62 run-file loader + index** — open a recorded run in the Composer: parse container,
  build tick index (inputs, commands, snapshots, digests, frame images).
  *Accept: headless-verify's own output opens; index counts match the recorder's summary line.*
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
- [ ] **F67 graph interpreter core** — allocate Persist/Temp/LastTemp from effective fields;
  per frame: Task pass in topo order (builtin + tween/timer/spring semantics, op evaluation,
  event AST: `when <edge> → set / emit`), command collect/latch/dispatch-once, window pass
  rendering composed windows with reflected widgets bound to live storage.
  *Accept: an authored producer/consumer/event/command graph runs; values move; the `^` edge
  fires once per edge.*
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
  payloads, status-layer model, module interop, edit-intent bus, Lifecycle view).

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
module interop, command payloads, status-layer model, Lifecycle north-star view.
