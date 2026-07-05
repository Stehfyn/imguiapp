# Feature Audit — verified matrix and TODOs (2026-07-04)

Code-verified audit of every feature claim in the design docs (23 agents; 470 claims checked
against `imguix/imguiapp/` + `tests/`). Milestone bar: [feature-complete-roadmap.md](feature-complete-roadmap.md).
Statuses: **shipped-tested** (code + at least one test), **shipped-untested** (code, no test),
**partial**, **missing**, **parked** (deferred by intent). Line anchors are as of audit day.

Sections below are TODO lists: `[ ]` build items (missing/partial), `[t]` test-debt items
(shipped-untested). Parked items are listed last per area, not as todos.

## Executive synthesis

# imguix Feature-Completeness Assessment
(Deduped from verified matrix + in-flight markers. "Tested" = asserted by imguix-core-tests / imguix-tests / imguix-headless-verify.)

---

## RUNTIME-CORE — the strongest area
**Shipped-tested core:** 5-layer fixed-order frame pipeline (UCR contracts 1-4); Task layer as sole mutator with dependency-ordered DAG updates (incl. producer-repush rebind, optional deps, instanced controls); command collect/dedup/dispatch-once same-frame; render purity; three-part memory (Persist/Temp/LastTemp) with one-frame deferred input; type-keyed const-pointer deps; topo push order; snapshot/restore + input replay + divergence detection byte-for-byte (contracts 7-9); GetAppCompositionID; FrameID + WAL frame prefix; advisory pacer incl. Fixed-dt determinism; ODR fix; edit-undo core; executable contract suite itself.
**Shipped-untested risk:** assert forensics (failure path never fired in a test); StatusLayer OnRender content; per-viewport present skip; WAL write-ahead ordering/content never asserted.
**Gaps ranked by loop impact:** (1) App-time scrubber UI missing — core StateHistory shipped-tested but zero UI callers (claimed shipped, is not); (2) status-layer queryable model / command payloads / edit-intent bus / module interop — all parked by intent, low loop impact.

## CODEGEN — generate step works and is byte-proven; the back edge doesn't exist
**Shipped-tested core:** codegen proof (byte-diff vs checked-in header + COMPILE + RUN: ^ event mutates PersistData, rising dispatches a real command); GenerateAppGraphCode/Ex + source map (step27); deps→topo push order + binding assignment lines (step14); fixed layer-push order contract; window AND sidebar containment codegen real (PushWindowControl implemented — old "stub" claims obsolete); struct import round-trip (step18); Kahn topo with cycle error surfaced verbatim to Output/Generate button.
**Shipped-untested risk:** Custom layer authoring→emission (zero tests touch "Custom"); AppGraphSignature stability; fresh/STALE gating (0 test hits on "Signature"); Generate health states; dropped-binding WARNING line; generate split-button/Diff; code-view scroll/hover ties.
**Gaps ranked:** (1) full control-skeleton C++ import missing — "round-trips to C++ and back" is structs-only, breaks iterate-after-generate; (2) two parallel emitters (AppEmitControlWithDeps vs legacy GenerateAppControlCode) with duplicated strings, only one byte-locked — drift hazard in the generate step; (3) comment-only codegen warnings counted nowhere; (4) diff-hunk node tagging, editable-lifecycle codegen — parked.

## CANVAS — engine replacement is complete and disciplined; polish slices unstarted
**Shipped-tested core:** hand-rolled canvas engine end-to-end (slices C1-C5, imnodes fully removed from build): model-space storage, single camera {Pan,Zoom}, cursor-centered zoom (rejection overruled and tested), same-frame exact measurement, single-enum FSM, pins/wires/create/drop events, minimap click-recenter, fit/frame; graph model: 7 node kinds + stamped ports + typed links + cycle guard + capture-validate-insert + stable id allocator + delete sweep (steps 10-15); layer column stacking, vertical containment (step29), group frames zoom-coherent (step35), group title drag (33/34), density flip (37), hide/isolate (23/24), layer-drag clamp never-pushes (31).
**Shipped-untested risk (largest cluster in the repo):** gizmo column clicks (shipped as dead chrome once already — obvious regression guard missing); wire-drop palette; detach-drag; LMB/RMB pan, RMB menu, Ctrl+click, wheel path, drag at zoom≠1; GridSnap; group collapse; halo renders; link-reject toast; minimal-pan reveal; hidden-endpoint link filter; AppConstantHash == runtime ImGuiType ID never asserted (stale stored DataTypeId on rename feeds the dup-type gate).
**Gaps ranked:** (1) sequence-order editing is read-only (see scopes); (2) S2 slice missing entirely (annotation frames, align/distribute); (3) pin pre-coloring / pre-drop can-link hint missing — legality telegraphed only after release; (4) filter co-application, reroute pins, wire animation, per-node LOD, lifecycle chart family — parked/horizon as scheduled.

## SCOPES — newest area, surprisingly well-tested, one big write-path hole
**Shipped-tested core:** Tab/Esc drill + breadcrumb (18, headless); scoped adds adopt (19); per-scope camera memory (19b); scope walls with Begin/End bands + config readout + published wall rect (38, on-camera capture); order strip chips w/ published rects + click-select (38); boundary portals: collect, inbound dock, click-jump, pure per-frame derivation (40); scope-local placement never disturbs root (39); density flip as pure model predicate (37); identity/detail card contents; root collapse; window/sidebar interior slice fully landed (ahead of its doc); chip overlay hit-test rule.
**Shipped-untested risk:** End band, void dimming, rails, shrink deadband, title ordinal badges, struct/field root-eviction gates, control- and struct-scope interiors, card geometry invariance across altitudes.
**Gaps ranked:** (1) **sequence-reorder drag write path** — rects published as the landing surface, but no drag, no order-as-model-state, no codegen emission; the single biggest compose-step gap; (2) per-scope sequence tidy verb (arrange left-to-right in execution order) missing; (3) layer-scope interiors (Display/Task/Command) partial — no walls/rail, wrong member density; (4) outbound portal label spec; (5) scope invariant tests (validate-on-mutate, breadcrumb==parent-chain) missing; (6) ScopeCams leak (memory only).

## CHROME — broadest shipped surface, thinnest verification, several false "shipped" claims
**Shipped-tested core:** flow-ordered document toolbar w/ Save/Code/Live/sidebar toggles (step26); chrome altitude rule; latched Code toggle (headless); hide/isolate command roads; Tab/Esc real-key nav; scope breadcrumb overlay.
**Shipped-untested risk:** viewport gizmo column; command palette (Space); status bar hints + fact readouts; health strip click→Output; Output severity toggles/filter; overlays popover; RevealPanel intents; F1 card; toolbar Add/captions; undo/redo/history clicks; near-opaque plates; Diff-in-panel mode; quick inspector; theme desc tables.
**Gaps ranked:** (1) **transport overlay removed** — "App time" claims in two docs are false, no UI reaches time travel; (2) problems-count toolbar badge missing (claimed landed); (3) Ctrl+P unbound; (4) frozen status-bar zone map w/ clickable facts, HEALTH/PERF pills, StatusPill primitive — missing; (5) panel lifecycle contract, command registry, layout presets, overlay quietness/motion/interaction ladder/em audit — W4-class polish, unstarted.

## OUTLINER-INSPECTOR
**Shipped-tested core:** brushing bus (hover report/read/expiry, all sources, step25); signature-keyed issue cache + severity (step25); two-way selection sync + eye-click consume (step22, 18); shared selection id; tree de-ImNodes; S1 coherence program.
**Shipped-untested risk:** outliner text filter + kind toggles; severity underline; composed inspector column; tree context-menu parity; drag-reparent; DESIGN/LIVE band; row origin tints; dangle guard; selection breadcrumb; inspector row reset; live-row dedup.
**Gaps ranked:** (1) **inspector dead writes on exploded Draft field lists** — authored edits silently discarded (acknowledged correctness bug, in-flight); (2) wire hover→inspector row tint half missing; multi-select and kebab are Style-only; Identity/Placement sections, row grammar, per-kind section persistence, scope header row — missing.

## PERSISTENCE — the loop's weakest correctness link
**Shipped-tested core:** graph format v2 round-trip incl. ports/links/bindings/style/events-format (step15); legacy [Draft] migration; NextId; named undo (15b); Place= scope placements (step39 model side); zero-per-app snapshot/restore + replay contracts.
**Shipped-untested risk:** layout sidecar (no save/load test); legacy dangling-link drop sweep never exercised; Place= file round-trip.
**Gaps ranked:** (1) **Init=/Dock= records never serialized — authored window placement/dock config is silently LOST across Save/Load** while codegen consumes it; a straight data-loss hole in persist; (2) event round-trip tests missing (save/load/undo/duplicate of a node carrying Events — the write path exists, unproven); (3) prefabs are session-only (registry in non-serialized editor state, no disk library); (4) last-active-tab claim false; redo/depth/coalescing untested.

## EVENTS
**Shipped-tested core:** authored events end-to-end: editor rows → guarded-block emission → runtime proof (^ mutation + rising command); expr type-checker + validate wiring (step21); edge-once contract; canvas wire lifecycle events.
**Shipped-untested risk:** confluence validator (check d) never triggered; inline diagnostic UI.
**Gaps:** named edge helpers defined but unreferenced (tabled, honored); command payloads parked.

## LIVE-MIRROR
**Shipped-tested core:** BuildAppLiveGraph (model-only, upsert by LiveKey, position survival across eye off/on); non-destructive hide + submission/read-back guards; reconcile-before-report ordering; live codegen with source spans (headless); reflect-free dep virtuals; eye toggle.
**Shipped-untested risk:** IsPromoted badge (zero test hits); live style write-back; hidden-endpoint link filter.
**Gaps:** transport/scrub regressed out; composition-ID-gated reconciliation parked by the project spine (deliberate).

## AV
**Shipped-tested core:** encoder vtable seam; libav backend; embedded meta stream w/ 5-layer integrity ladder verified from pixels through real x264; identity record; four-phase recorder pipeline; encoder thread (Block); encode-every-frame contiguity; frozen strip format; vulkan CaptureFrame; test harness (Reproduce posture); headless Offscreen mode — the whole verification spine the repo stands on.
**Shipped-untested risk:** QOI backend + extractor; MF backend; snapshot-state record (snapshots=0 in runs); layer-3 input log; resize-abort; Realtime timing; meta readback→reproduction; GL capture; DropNewest; **ring recorder exists but assert auto-dump wiring is MISSING (ImGuiAppAssertFail never calls AppRecordDumpRing; no callers, no tests)**.

## OTHER
Composer rename done (tested); undo coverage-by-construction + derived labels (tested); vocabulary alignment shipped; visual grammar/depth-order/four-roads all partial-by-convention (no enforcement point); prefab registry untested; slice discipline (S1-S5, W1-W5) diverged — W4 untouched, transport regressed; ImStructTable modified in tree with zero test references.

---

## 1) Ten highest-leverage gaps to a coherent author-an-app-end-to-end milestone
Ranked by where they cut the loop (scaffold → compose → events → generate → compiled app runs):

1. **Serialize Init=/Dock= (authored placement/dock config)** — persist step silently loses authored window config today; smallest fix, worst failure class.
2. **Fix inspector dead writes on exploded Draft field lists** — compose-step edits silently discarded (known, in-flight).
3. **Sequence-reorder write path** (chip drag + click-nudge fallback → order as model state → codegen emits authored order) — the loop can read execution order but cannot author it; infra (published chip rects) already landed.
4. **Event persistence round-trip tests** — events are the loop's payload; save/load/undo/duplicate of Events is entirely unproven.
5. **Full control-skeleton C++ import** (inverse of AppEmitControlWithDeps) — closes the regenerate/iterate asymmetry; structs-only today.
6. **Exercise AppGraphValidate structural checks** (forest violation, duplicate producer, event confluence, wholesale kind-pairing) — invalid graphs can currently reach Generate with unexercised guards.
7. **Custom-layer authoring test + emitted-skeleton byte-check** — the only user-extensible layer path has zero coverage from palette to PushAppLayer<Subclass>.
8. **Unify the two emitters** (or byte-lock the legacy one) — duplicated emission strings are a drift bomb under the proof's radar.
9. **Prefabs on disk + starter idiom library** — scaffold-step productivity; registry currently evaporates per session.
10. **Per-scope sequence tidy + fresh/STALE signature tests** — comprehension of compose order plus trust in the Generate gate (tied; both cheap).

Deliberately excluded: App-time transport (live debugging is parked by the project spine), S2 annotation/align (polish, not loop-blocking).

## 2) Test-coverage debt (shipped code with no assertions), ranked
1. Gizmo column click paths — already shipped once as dead chrome; headless click test is the missing regression guard.
2. AppGraphValidate checks b/c/d + wholesale (a).
3. Custom layer end-to-end.
4. Event + Place= + sidecar save/load round-trips; legacy dangling-link drop path.
5. Canvas input bindings: LMB-empty pan, RMB pan/menu, Ctrl+click, wheel zoom path, drag at zoom≠1, detach-drag, GridSnap.
6. Fresh/STALE transitions, Generate health states, dropped-binding WARNING.
7. Live style write-back; IsPromoted badge; hidden-endpoint link filter.
8. Status strip content (hints, counts, rejection override); problems row click/hover; RevealPanel paths; palette open/pick.
9. Assert forensics failure path + ring dump (incl. wiring the assert→dump call, which is a code gap too).
10. AV: QOI encoder/extractor, MF, snapshot-state record, layer-3 input log, resize abort, Realtime/Witness posture.
11. Redo, history depth/cap, drag coalescing.
12. Outliner filter/kind toggles, tree context menu, drag-reparent, DESIGN/LIVE band.
13. AppConstantHash vs ImGuiType<>::ID equivalence; AppGraphSignature stability.
14. Breadcrumb click-jump, Esc via key, group collapse, wire hover/click-select.

## 3) Percentage-feel per area (verifiable features shipped / total non-parked in matrix, rough)
- **runtime-core: 15/17 (~88%)** — only the scrubber UI and a few assertion holes.
- **av: 14/18 (~78%)** — spine proven, alternates and crash paths dark.
- **canvas: 40/48 (~83%)** — engine complete; polish slices S2/S4/S5 are the remainder.
- **scopes: 24/30 (~80%)** — window/sidebar interior done; write-path + layer interiors remain.
- **live-mirror: 8/10 (~80%)** — mirror solid; transport/reconciliation parked.
- **events: 6/8 (~75%)** — core loop proven; confluence + payloads open.
- **codegen: 12/16 (~75%)** — forward path bulletproof; back edge and unification missing.
- **persistence: 10/14 (~70%)** — graph round-trips; Dock/Init hole + events/prefabs untested/missing.
- **outliner-inspector: 14/22 (~64%)** — sync/brushing proven; section system half-built.
- **chrome: 25/40 (~62%)** — widest surface, most partials, weakest tests, two false "shipped" claims (transport, problems badge).

**Overall shape:** the vertical slice scaffold→compose→events→generate→compiled-app-runs *already executes and is byte-proven at both ends* (contract suite + codegen proof + headless recording). What's missing for a coherent milestone is not new machinery but (a) two silent-loss bugs in the middle of the loop (Dock/Init serialization, inspector dead writes), (b) the order-authoring write path, (c) the C++→graph back edge, and (d) test coverage for the ~60 shipped-untested surfaces, concentrated in chrome and canvas input.

---

## Per-area verified matrix

### runtime-core  (17/31 shipped-tested; 3 untested, 3 partial, 2 missing, 6 parked)

**Build TODOs — missing**

- [ ] **App-time control in right cluster** — Contradicts up-next.md ('App-time toggles right-aligned' and 'toolbar App time scrubber' claimed landed); only the runtime ImGuiAppStateHistory exists.
  - evidence: No App-time / freeze / frame-scrubber control exists in the toolbar or anywhere in imguiapp_demo.cpp (grep 'App time|Freeze|Scrub|StateHistory' = 0 hits in the file); right cluster enumerated at demo:790-856 has no such control.
- [ ] **Data-driven Layers / live topology rewiring** — Two clauses evolved past the rejection text: Custom layer subclass authoring/codegen was added (still compile-time), and PushWindowControl is now implemented (imguiapp.h:1389) so window-hosted controls are no longer a codegen TODO
  - evidence: Correctly absent: layers are fixed C++ types (ImGuiAppLayerType_ h:254-263, core layers permanent/undeletable nodes.cpp:2772, codegen emits PushAppLayer<FixedType> 10550-10553); DataDependencies remains a compile-time pack (imguiapp.h:763-770,1019); no live edge rewiring — the editor authors, codegen realizes

**Build TODOs — partial**

- [ ] **Time travel (flagship)** — The claimed Composer toolbar 'App time' scrubber does not exist: no scrub/rewind/freeze UI in imguiapp_demo.cpp (only the edit-history undo dropdown at 723-737), no AppStateSnapshot caller outside core tests/AV, and git log -S 'App time' finds nothing. imguiapp.h:445 references time scrub only as disabled-for-host commentary.
  - evidence: Core is shipped-tested: ImGuiAppStateHistory (imguiapp.h:839), AppStateSnapshot/Restore/Clear (imguiapp.h:372-374, imguiapp.cpp:1061-1290) keyed to GetAppCompositionID (imguiapp.cpp:1255); Contract 7 restore-and-replay (tests/imguiapp_core_tests.cpp:292-330) plus Contracts 8/9 (input replay, divergence detection) exceed the claim. Push helpers auto-register snapshottable sizes (imguiapp.h:1348+).
- [ ] **App-time state-history scrubber** — The Composer toolbar 'App time' scrubber up-next.md claims landed does not exist in any host; only the runtime API + tests exist.
  - evidence: Runtime ring shipped + tested: ImGuiAppStateHistory (C:/dev/imguix/imguix/imguiapp/imguiapp.h:839), AppStateSnapshot/AppStateRestore (imguiapp.cpp:1082,1250); core contract tests restore+replay (tests/imguiapp_core_tests.cpp:305-425); AV records snapshots (imguiapp_av.cpp:359,1090-1093). No UI: grep across imguiapp_demo.cpp / imguix_demo.cpp finds zero AppStateSnapshot/Restore callers or 'App time' control.
- [ ] **Status strip LIFECYCLE segment** — No Initialized pill, no 'storage N' (StorageEntries unused in demo), no 'shutdown: yes/no' (ShutdownPending only consumed by the runtime loop at demo.cpp:2121).
  - evidence: Strip shows 'composed'/'uncomposed' derived from Mirror->Layers.Size (demo.cpp:949-954, 979).

**Test-debt TODOs — shipped, untested**

- [t] **Assert forensics** — The failure path never fires in a passing run — no test deliberately trips an assert and inspects the WAL/stack output.
  - evidence: imguix/imguix_imconfig.h:11-13 redefines IM_ASSERT -> ImGuiAppAssertFail, injected globally via root CMakeLists.txt:120 IMGUI_USER_CONFIG; ImGuiAppAssertFail (imguiapp.cpp:99-108) writes expr/file/line + symbolized ImStackTrace (imguiapp.cpp:53) to the SetAppAssertWAL sink (imguiapp.h:410) and stderr, then breaks/exits; commit 3916bd9 'build: route IM_ASSERT through assert-forensics sink'. Sink registered by the test runner (core_tests.cpp:718).
- [t] **Machine-checked invariant: one producer per data type** — No test triggers the duplicate-producer graph issue; the check itself is unexercised.
  - evidence: AppGraphValidate check (c): two non-live Control/Struct nodes resolving to the same data type name flag "both emit data type '%s' (one producer per type)" (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:8659-8685); link gate also rejects a duplicate dependency type per consumer (2889-2904). Runtime side is tested separately (contract 10/11), but that is a different feature.
- [t] **Per-viewport pacing / present skip** — No test exercises it (harness disables viewports, testharness.cpp:116). Deviation from doc: refresh comes from the HMONITOR in PlatformHandle, not ImGuiPlatformMonitor, because ImGuiPlatformMonitor carries no refresh field (imguiapp.cpp:234-236).
  - evidence: Implemented ahead of the claimed P3 horizon: AppPacerViewportShouldPresent at imguiapp.cpp:923-968 (main viewport never skips :929-930, per-viewport NextPresentDeadline keyed by viewport ID in ImGuiAppViewportPace :226-232, deadline chain + re-anchor :962-966); consulted by both per-viewport present hooks (imguiapp_impl_win32_vulkan.cpp:1087, imguiapp_impl_win32_opengl3.cpp:122).

**Parked (deferred by intent)**

- Module interop — Whole item unbuilt; only naming groundwork exists.
- Status layer model — No queryable published-status structure; status is still 'the status bar'.
- Command payloads — Payload/argument queue unbuilt, by intent.
- Edit-intent bus — Bus unbuilt; blocked on command payloads per the doc.
- Commands from other modules — Entire feature (module command exchange, Source/Module node kind) — matches the doc's own 'eventually'.
- Queryable status model for other modules — Entire feature, consistent with the doc calling it 'the endpoint'.

<details><summary>Shipped-tested (verified, no action)</summary>

- Executable contract test suite
- ODR collision fix for test engine sample
- ImGuiAppWAL write-ahead logger
- Core hardening batch
- Four-layer fixed-order frame pipeline
- Task layer as sole mutator with dependency-ordered updates
- Command collection with dedup and dispatch-once
- Render-phase purity rule
- Three-part control memory (PersistData/TempData/LastTempData)
- One-frame deferred input processing
- Type-keyed const-pointer dependencies, one instance per type
- Push order as topological order over the data-flow DAG
- Edit-time undo history
- Frame identity ImGuiAppFrameID
- WAL frame-id prefix
- Advisory pacer AppPacerWait
- Fixed-pacer determinism (forced dt)

</details>

### codegen  (11/35 shipped-tested; 10 untested, 4 partial, 7 missing, 3 parked)

**Build TODOs — missing**

- [ ] **Import round-trip growth (full control skeletons)** — Full-control-skeleton parsing entirely absent, as planned.
  - evidence: Only struct import exists: AppGraphImportStructsFromCode (imguiapp_nodes.h:920, imguiapp_nodes.cpp:11219, member parser AppImportParseMember) tested by step18_roundtrip_import_structs (tests/imguiapp_nodes_tests.cpp:1306-1330). AppEmitControlWithDeps (imguiapp_nodes.cpp:9698) has no inverse — no parser for control class skeletons/events/deps.
- [ ] **Topo cycle name computed but collapsed**
  - evidence: Defect fixed: AppGraphTopoOrder writes 'dependency cycle at <Name>' to err; AppGraphValidate pushes it verbatim as a severity-2 issue 'dependency cycle: %s' (AppValidatePushIssue, nodes.cpp ~8600 region); rendered in the Output tab rows and '%d error(s)' counts (demo.cpp:1700-1713) and reddens the Generate button (demo.cpp:765-772). No ok/CYCLE token remains anywhere.
- [ ] **Codegen domain guards (!IsLive)** — Plan superseded by the opposite, deliberate decision (codegen domain != signature domain by design; staleness handled via NumUnbuilt).
  - evidence: Design reversed: live nodes are now FIRST-CLASS in whole-program codegen — GenerateAppGraphCode topo-orders with include_live=true and emits live PushAppWindow/PushWindowControl/PushAppLayer lines (nodes.cpp ~10162, 10446); headless composer_live_codegen asserts the live push lines and a source-map span for every live node (imguiapp_headless_verify.cpp:157-240). Signature stays authored-only, codegen does not.
- [ ] **Hosted-control codegen WARNING line** — None — the capability the WARNING apologized for now exists.
  - evidence: Obsolete: PushWindowControl is implemented (imguiapp.h:439, 1389) and codegen emits the real call 'ImGui::PushWindowControl<%s>(app, win_%d); // hosted by %s' (nodes.cpp:10446); headless composer_live_codegen asserts the generated line (imguiapp_headless_verify.cpp:175). No 'cannot be hosted' text exists.
- [ ] **Whole-graph codegen signature**
  - evidence: Rejection honored: AppGraphSignature skips IsLive nodes (nodes.cpp:10004-10013); the only live-derived input is the stable CommandLayer command-name list (deliberate, churn-free). Live edges/ids never enter the hash.
- [ ] **ImHashData over fixed char[] buffers**
  - evidence: Rejection honored: every char[] in the signature is hashed as NUL-terminated ImHashStr (nodes.cpp:10015-10017 etc.), with the trailing-garbage rationale documented at nodes.h:665-666 and nodes.cpp:10000-10002; ImHashData used only for POD scalars.
- [ ] **AppGraphCountKinds / Revision dirty bit**
  - evidence: Rejection honored: no AppGraphCountKinds symbol and no ImGuiAppGraph::Revision (only the unrelated ImGuiApp::CompositionRevision, imguiapp.h:876); counts are inline loops (StatusStrip demo.cpp:924-939; Project inspector 1171-1173) and freshness is per-frame AppGraphSignature (demo.cpp:494).

**Build TODOs — partial**

- [ ] **Graph-to-C++ round-trip** — C++->graph covers struct blocks only; parsing full control skeletons back into nodes ('the inverse of AppEmitControlWithDeps') is explicitly future work — up-next.md:45-46. 'Round-trips to C++ and back' overstates the back edge.
  - evidence: Forward direction is proven: GenerateAppGraphCode/GenerateAppNodeCode (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.h:898-904) tested by step6_codegen, step7_write_file, step14_codegen_deps_topo, step27_codegen_source_map, and the codegen proof which byte-diffs, COMPILES, and RUNS the output (C:/dev/imguix/tests/imguiapp_codegen_proof.cpp). Reverse direction is structs-only: AppGraphImportStructsFromCode (nodes.h:920; parser skips methods/nested blocks, nodes.cpp:10956) tested by step18_roundtrip_import_structs (nodes_tests:1306-1342). Live-app mirroring (BuildAppLiveGraph, step20) is runtime->graph, not C++ source.
- [ ] **AppEmitControl refactor preserving legacy emitter strings** — The 'one emitter, legacy calls depCount==0' refactor never happened — two parallel emitters whose shared strings can drift (only the graph one is byte-locked by codegen_proof)
  - evidence: No AppEmitControl(draft, depNames, depCount, out) exists. Instead AppEmitControlWithDeps(g, n, out) nodes.cpp:9424+ is a separate graph-aware emitter; legacy GenerateAppControlCode (1213-1241) keeps its own duplicate strings. step6/step7 output stability holds; codegen_proof locks the graph emitter
- [ ] **Generate tooltip authored-only count** — The specified tooltip tally was superseded, not implemented; intent (no conflation) is met.
  - evidence: The conflated '%d node(s)' tooltip no longer exists — Generate tooltips now state error/fresh/changed (demo.cpp:771-776). Design vs live counts are split explicitly elsewhere: Project inspector '%d design %d live' (demo.cpp:1171-1179) and the strip NODES split.
- [ ] **Fresh/STALE surfaces wiring** — No strip pill 'code: fresh/STALE', no amber panel header, Copy/Write buttons not warning-tinted, WriteMsg never cleared on the first stale frame.
  - evidence: One WrittenSig/FrameSig comparison coherently drives: Generate button green 'Generated'/amber 'Generate'/red-on-errors + tooltips (demo.cpp:764-776), toolbar 'Stale N'/'Built' readout with amber ink + bootstrap tooltip (792-822), Project-inspector check/warn icon (1160-1170), Project-tab per-header 'stale'/'matches graph' row (1662-1688).

**Test-debt TODOs — shipped, untested**

- [t] **Custom layers authorable** — No test creates a Custom layer or byte-checks its emitted skeleton/push (tests grep for 'Custom' = zero hits); the codegen-proof canonical graph uses no custom layer.
  - evidence: ImGuiAppLayerType_Custom (imguiapp_nodes.h:261; contract at 251-252: node's name IS the subclass); palettes offer only 'Custom Layer' (imguiapp_nodes.cpp palette entries + RMB menu + command palette 'Add: Custom Layer'); codegen emits 'struct %s : ImGuiAppLayer' skeleton and ImGui::PushAppLayer<%s> at stack position (emit sites formerly 10144/10381/10484); rename/delete allowed only for Custom.
- [t] **Codegen fresh/STALE indicator** — No test covers signature-based freshness (grep 'Signature' in tests/ = 0 hits).
  - evidence: doc->WrittenSig vs FrameSig (C:/dev/imguix/imguix/imguiapp/imguiapp_demo.cpp:611,764); code panel shows 'matches graph' / 'stale' (1688); Generate button + Built/Stale badge derive from it (764-798). AppGraphSignature declared in imguiapp_nodes.h.
- [t] **Select node -> code panel scroll + gutter tint** — Gutter uses the shared gold selection accent, not the node's kind accent; no UI test.
  - evidence: Landed: on selection change the code view scrolls the node's first span into the top quarter (C:/dev/imguix/imguix/imguiapp/imguiapp_demo.cpp:1031-1046); span rows get a background tint + left gutter accent bar (1061-1066).
- [t] **Hover code line -> node halo** — No test.
  - evidence: Landed: hovering a span line publishes External-source hover for the emitting node (C:/dev/imguix/imguix/imguiapp/imguiapp_demo.cpp:1079-1081); click also selects the node (1082-1083); halo pass consumes (imguiapp_nodes.cpp:8094-8118).
- [t] **Generate button carries document health** — No test asserts the health states.
  - evidence: Landed: Generate button colored red (errors, writing still allowed), green check (WrittenSig==FrameSig), amber (changed) with matching tooltips (C:/dev/imguix/imguix/imguiapp/imguiapp_demo.cpp:761-776).
- [t] **Generate split-button, Diff demoted** — No test opens the family or runs Diff/Copy.
  - evidence: C:\dev\imguix\imguix\imguiapp\imguiapp_demo.cpp:777-788: Generate + chevron ##generate_family with 'Copy generated C++ to clipboard' and 'Diff vs saved graph -> clipboard'; no peer Diff toolbar button remains (toolbar = Add/undo-redo-history/Save/Generate + right cluster); Code tab keeps a small in-panel Diff view toggle (1620).
- [t] **AppGraphSignature pure helper** — No direct unit test of signature stability/insensitivity.
  - evidence: imguiapp_nodes.cpp:9996+ (decl nodes.h:667): seed-chained fold skipping IsLive nodes (except stable live CommandLayer command names), char[] hashed as NUL-terminated ImHashStr, comment explicitly excludes GridPos/ids/BodyAttrId (10000-10002); consumed by demo FrameSig (demo.cpp:494) and the validation cache (nodes.h:871, nodes.cpp:3182-3193 region).
- [t] **Codegen staleness gating state** — No test exercises the fresh->stale transition.
  - evidence: Implemented as GraphDocData::WrittenSig (set on header write, demo.cpp:611) vs per-frame FrameSig (demo.cpp:494); fresh/unwritten/stale computed in toolbar (demo.cpp:764, 792-793); EditorBody adds a CodeSig regen gate for the code buffers (demo.cpp:1109).
- [t] **Dropped-binding codegen WARNING comment** — Neither tests nor imguiapp_codegen_proof.cpp assert the WARNING line.
  - evidence: imguiapp_nodes.cpp:9733 in AppEmitControlWithDeps types_ok==false path: out->appendf("    // WARNING: dropped binding %s = %s (type mismatch)\n", ...); sibling warnings at 9666 (unknown event command) and 9777 (field without parent struct).
- [t] **Diff as a Code-panel mode** — No test toggles Diff mode or asserts the in-panel diff.
  - evidence: imguiapp_demo.cpp:1604-1636: DiffMode toggle button in the Code tab header (1618-1623), diff rendered in-panel via ShowGeneratedCodeView('##codediff') with empty/no-save states; Code mode keeps the source map (CodeSpans, 1643). The clipboard diff survives only as an optional Generate-menu item (785-786).

**Parked (deferred by intent)**

- Lifecycle chart is editable (drag slot reorders push, codegen emits) — No reorder input surface, no order-as-model-state write path, no codegen wiring.
- Diff hunks tagged with origin node — No hunk->node tagging or click-to-reveal; still on the T4/S3 roadmap.
- Cycle click-to-select via topo out-param — Deferred as documented.

<details><summary>Shipped-tested (verified, no action)</summary>

- Codegen proof (byte-diff + compile + run)
- Layer order as codegen contract
- Code<->canvas source map recording
- Baseline defect: semantically inert links + dep-less codegen
- AppGraphTopoOrder Kahn sort with cycle error
- GenerateAppGraphCode whole-graph codegen
- DataDependencies derived from incoming data edges
- Field bindings emit OnUpdate assignment lines
- Bring-up emission in Kahn order: Layers, Windows/Sidebars, Controls
- Sidebar containment codegen realized, Window containment stubbed
- step14_codegen_deps_topo test

</details>

### canvas  (75/149 shipped-tested; 23 untested, 14 partial, 15 missing, 22 parked)

**Build TODOs — missing**

- [ ] **S2 slice: annotation frames + align/distribute** — Whole slice unstarted — consistent with 'next slice' claim.
  - evidence: No align/distribute verbs in the gizmo column (imguiapp_nodes.cpp:8244-8276: add/frame/fit/tidy/snap/overlays only) or palette (~7866: Tidy/Fit/Frame/Snap). No annotation-frame node kind (ImGuiAppNodeKind_ enum imguiapp_nodes.h:238-247). CanvasAnnotationDrawList is an overlay draw-list, not annotation frames; group frames (step32/35) are containment visuals.
- [ ] **Overlay rest opacity and hover salience** — Entire quietness system unimplemented.
  - evidence: Overlays render near-opaque always: health strip SetNextWindowBgAlpha(0.99f) (imguiapp_demo.cpp:1552), gizmo plate alpha 0.99 (imguiapp_nodes.cpp:8143). No 70%-rest/full-on-hover logic, no count>0 salience rule anywhere.
- [ ] **Overlay dim during gestures** — Not started.
  - evidence: No gesture-driven overlay dimming exists. The 'canvas dimming' commit (imguiapp dec967a) is unrelated: it added CanvasNextNodeAlpha to dim hidden/closed NODES (imguiapp_canvas.h, imguiapp_nodes.cpp:6680), not overlays during drags.
- [ ] **Run-state viewport tinting** — Blocked on transport reintroduction.
  - evidence: No viewport tint or toolbar accent-line code anywhere in imguiapp_demo.cpp/imguiapp_nodes.cpp; the App-time scrub state it would signal was itself removed with the transport (commit 58854ef).
- [ ] **Baseline defect: index-derived imnodes ids** — None — the defect this entry describes has been eliminated exactly as the design intended
  - evidence: NODE_DRAFT_BASE/ATTR_DRAFT_BASE exist only in docs/node-editor-upgrade-design.md:134-135,333; zero hits in any .cpp/.h. Replaced by AppGraphAllocId (nodes.cpp:1263, g->NextId++) + AppGraphFindPort search (nodes.cpp:2383); regression pinned by step12_identity_survives_delete (tests:615-644)
- [ ] **Pin pre-coloring by DataTypeId** — No type-compatibility pre-telegraphing before a drop; rejection is communicated after release via the LastLinkErr toast instead
  - evidence: Pins are colored by port KIND only: AppPinColor(port->Kind) at nodes.cpp:6691,6696,6714 returning ImGuiAppComposerStyle PinData/PinChild (5696). DataTypeId appears only in legality checks, persistence, and live mirror (grep: 1275,2901-2902,10567,10654,11769) — never in pin color logic
- [ ] **Field-level typed pins with packed FieldUid** — None — absence is the intended state; the Field-node evolution does not resurrect the rejected packed-uid design
  - evidence: FieldUid appears only in docs/node-editor-upgrade-design.md:390-391; no packed-uid scheme in code. Correctly absent per the rejection: bindings are name-based ImGuiAppFieldBinding (h:426-431); later per-field wiring arrived differently — ImGuiAppNodeKind_Field nodes (h:246) with normal allocator ids, no 12-bit cap
- [ ] **Multiple ordered DataIn pins, one per dependency** — None — absence is the intended state (persist/temp tie pins are struct-explosion ties, not per-dependency intakes)
  - evidence: Correctly absent: Control stamps exactly one 'deps' DataIn intake (nodes.cpp:1308); duplicate dep TYPE rejected at link time (2902-2903, 'already depends on this data type'); dep arg order derived at codegen time (AppGraphConsumerDeps)
- [ ] **Index-derived imnodes ids (status quo)** — None — absence is the intended state
  - evidence: Correctly absent: NODE_DRAFT_BASE/ATTR_DRAFT_BASE only in the design doc; all ids from AppGraphAllocId (nodes.cpp:1263) resolved by AppGraphFindNode/FindPort search (2383); step12 pins the delete-survival regression the old scheme failed
- [ ] **ImVector field-bindings stored on the link** — None — absence is the intended state
  - evidence: Correctly absent: ImGuiAppNodeLink h:295-302 holds only scalars (Id/Start/End/Kind/Soft) and stays brace-initializable ({1,10,11} at tests:404); bindings live on ImGuiAppGraph::Bindings keyed by LinkId (h:612, h:424-425 comment states the aggregate rationale)
- [ ] **Link-rejection reasons computed but discarded**
  - evidence: Defect no longer exists: CaptureAppGraphLinks now persists the reason (imguiapp_nodes.cpp:3010 clears on create, 3016-3017 ImStrncpy to g->LastLinkErr + Seq++ in reject branch); surfaced via canvas toast (nodes.cpp:8368-8384), status-hint strip (nodes.cpp:8107-8113, imguiapp_demo.cpp:969-971 red), and doc log 'link refused: %s' (demo.cpp:504-508). Call site still passes a local err (nodes.cpp:8288-8289) but the reason is no longer discarded.
- [ ] **Hoist scope rule preserving #26**
  - evidence: Obsolete: the reconcile lives wholly in OnUpdate (demo.cpp:488-491), ImNodes is gone, and tree submission no longer carries a selection ordering dependency (AppTreeClick nodes.cpp:12271-12280). The invariant the rule protected was retired, so the rule has no referent.
- [ ] **Toast clear on CaptureAppGraphLinks true return**
  - evidence: Rejection honored: LastLinkErr is cleared only in the successful-create branch (nodes.cpp:3010) and written only in the rejection branch with an explicit comment refusing the changed-return drive (3014-3018); link-destroy paths never touch it.
- [ ] **Origin suffix in node title string**
  - evidence: Rejection honored: titles are Draft.Name via CanvasNextNodeTitle/CanvasNextNodeTitleEditable with no suffix (nodes.cpp:6667-6676); origin lives in the title-bar dot and breadcrumb tag, and live titles are simply non-editable.
- [ ] **TitleBarSelected identity-tint override**
  - evidence: Rejection honored and moot: ImNodes was removed entirely; the canvas engine owns selection visuals (Wire/node selected colors, imguiapp_canvas.h:26-28) and origin tint is confined to the dot, never overriding selection cues.

**Build TODOs — partial**

- [ ] **Sequence-order editing (drag execution badges)** — Drag-to-reorder, click-nudge fallback, order-as-model-state, and codegen emission of authored order all absent — matches the doc's 'writing it doesn't [exist]'.
  - evidence: Read side + precursor exist: AppDrawScopeOrderStrip (imguiapp_nodes.cpp:5504) publishes one chip per member in execution order with screen rects (ScopeStripNodes/Rects), and chip-click-selects is tested (step38, tests/imguiapp_nodes_tests.cpp:2857-2865). Comment at nodes.cpp:5503: 'the coming sequence-reorder drag rides them'.
- [ ] **Wire hover -> endpoint halos + inspector row tint** — Inspector binding rows never read AppGraphHoveredLink, so wire hover does not tint inspector rows; only the canvas half shipped. No test.
  - evidence: Canvas wire hover publishes link (imguiapp_nodes.cpp ~7099); halo pass halos both endpoint owners of the hovered link (8119-8126).
- [ ] **Typography em scale for node text** — The exact ladder (1.0/0.9/0.9/0.8) is not what code uses (0.7-0.8 observed); no enforcement point.
  - evidence: All canvas text sizes are em multiples: PushFont at em*0.7/0.75/0.8 and zoom-scaled base font (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:5408-5554,6056); demo captions em*0.78 (imguiapp_demo.cpp:876).
- [ ] **Fixed canvas depth order** — No named layer enum/single source in code; the rule lives in the doc and implicit pass ordering.
  - evidence: Order exists structurally: bands + execution rail drawn on the background before nodes (imguiapp_nodes.cpp:3651-3708), wires under nodes via the canvas engine, overlays on CanvasAnnotationDrawList above CanvasEnd (imguiapp_canvas.h:90-93), toast last (8514).
- [ ] **Gesture vocabulary (current)** — Two claimed gestures don't match code: no MMB pan exists (no MouseButton_Middle in imguiapp_canvas.cpp; pan is LMB-drag on empty + wheel zoom per hint), and detach is grab-near-wire-end, not Alt+LMB (no KeyAlt in the canvas engine).
  - evidence: Verified: LMB select (step18 tests:1401-1405), drag move (headless canvas_solid_drag_no_overlap:276), dbl-click rename (canvas_c3:1081-1097), Tab enter scope (step18:1407-1411), RMB menus (imguiapp_nodes.cpp:7189-7242), pin-drag wire + filtered add on canvas release (8415-8519), drag-end rewire/detach (imguiapp_canvas.cpp:781-790), Space palette (7378).
- [ ] **Overlay altitude table with owned corners** — BC transport slot is empty (removed); no named-slot law/registry in code — the table is convention, not enforced; also a transient link toast squats BL (8450-8460) beside the health strip's corner.
  - evidence: De facto slot occupancy matches the table: TL breadcrumb (imguiapp_nodes.cpp:5757-5761), TR gizmo column (8140), BL health strip (imguiapp_demo.cpp:1547), BR minimap (imguiapp_nodes.cpp:6904-6906 + canvas engine), bottom hint via status bar (8096-8126), cursor transients (menus/palette/quick inspector); TR corner stacks second tenants into the overlays popover (8180-8191).
- [ ] **Depth order extended to chrome** — No codified full chain (badges/toasts/status override); panels-vs-overlay and cursor-UI precedence are emergent, not enforced.
  - evidence: Canvas-side ordering is real: overlays draw on CanvasAnnotationDrawList above nodes, clipped inside the editor's z-order (imguiapp_nodes.cpp:8135-8137; imguiapp commit d59852d 'canvas overlays stay inside the editor's z-order'); grid<bands<frames<wires<nodes ordering falls out of submission order.
- [ ] **Typography/spacing constraint for scope chrome** — Normalize to the stated scale/quanta or amend §3.
  - evidence: All metrics are em multiples (zoom-proportional), but font scales include 0.7 (kind word) and 0.75 (readout) outside the stated 1.0/0.9/0.8 scale; portal text is 1.0 em vs the table's 0.9; spacing uses 0.45/0.8/0.9/1.1/1.3 em, not 0.25-em quanta; constants live inline in the wall/strip/portal functions, not the style table.
- [ ] **Legacy public API kept verbatim** — 'Verbatim' broken for the two canvas-touching helpers (extra ImGuiCanvasState* param); emitter/persistence functions and their output strings are intact
  - evidence: GenerateAppControlCode h:332, SaveAppNodeGraph/LoadAppNodeGraph h:314-315, Save/LoadAppNodeGraphMulti h:319-320 unchanged; CaptureAppNodeLinks h:311 and BeginAppNodeRenamable h:137 now take ImGuiCanvasState* (canvas-engine migration). Steps 5-8 pass against the current signatures
- [ ] **Hand-wired live demo nodes become builtin Control palette nodes** — Only 2 of the 3 named nodes ('Default' absent from the palette); no test adds a builtin via the palette or asserts its wireable ports (builtin path is exercised indirectly through BuildAppLiveGraph at nodes.cpp:11814)
  - evidence: AppGraphAddBuiltin(g, Control, 'RandomTime', 'RandomTimeData') and ('Breathing','BreathingData') in the palette (demo:272-275); IsBuiltin node flag h:395, DataTypeName h:397; builtin DataOut stamped with the real data type id (nodes.cpp:1284,1949-1950); builtin body renderer branch nodes.cpp:4379 (reflects live data when app non-null)
- [ ] **Per-slice contract: phase-coherence checklist + zoom acid test** — No automated 'zoom acid test' (rapid wheel over every decoration, zero single-frame artifacts) exists — no 'acid'/wheel-sweep test anywhere in tests/; checklist passing 'by inspection' is a process claim, not mechanically verifiable.
  - evidence: Zoom-coherence is partially automated: canvas_c1 asserts model sizes and anchor stability across zoom (tests/imguiapp_nodes_tests.cpp:919-928); step35_group_frames_zoom_coherent_never_interpenetrate (:2591) covers decorations under zoom. Phase-coherence rules are encoded in engine comments citing docs/phase-coherence.md (imguiapp_canvas.cpp:1-4, 980-982, 1158-1162, 1220-1224).
- [ ] **Live node title-bar tint** — No whole-title-bar tint for live nodes; origin reads from the dot only.
  - evidence: Superseded mechanism: origin is carried by a title-bar origin dot (CanvasNextNodeOriginDot, imguiapp_canvas.cpp:1042; submitted nodes.cpp:6595) plus a static non-renamable title for live nodes (nodes.cpp:6667-6669); title fill color stays kind-based (AppNodeTitleColor nodes.cpp:3385-3389). ImNodesCol_* push/pop pairs are gone with ImNodes.
- [ ] **Body origin badges incl. promoted badge** — No in-body TextDisabled badge lines; promoted has no 'matches live <DataType>' text pairing tint with words.
  - evidence: Superseded per later chrome rules (no floating/body annotations): live = origin dot + read-only status hint 'live mirror (read-only)' (nodes.cpp:8147) + inspector '(live -- read-only mirror)' (4301) + tree 'live (read-only)' (12217); promoted = dot ring (6595) + breadcrumb '[promoted]' (8639) + strip count. AppNodeDataTypeName exists and is used in codegen/inspector, not in a body badge.
- [ ] **Gen-1 zoom rejection (R6)** — The rejection no longer stands: it was overturned exactly as usability-findings-2026-07.md:22-28 says, and zoom shipped (and is tested) in the custom canvas engine rather than an imnodes fork. The R6 text in the gen-1 doc is now a historical record contradicted by the code; neither gen-1 doc carries a supersession note.
  - evidence: The rejection record exists verbatim: composer-ui-design.md:146-150 'R6. Zoom: name the strategic gap... Do not fork imnodes for it now... Decision recorded here so the gap is chosen, not accidental'; reaffirmed at composer-workbench-design.md:273 and by the zoom-indicator ban at composer-ui-design.md:258.

**Test-debt TODOs — shipped, untested**

- [t] **Canvas de-noise** — Aesthetic claims with no pixel assertions; headless recordings capture the area but nothing checks grid/band extents.
  - evidence: Grid at whisper level: CanvasThemeMix(bg, ink, 0.16f/0.21f) for GridLine/GridLinePrimary (imguiapp_canvas.cpp:430-431); opaque group title plate GroupTitleBg 'grid must not bleed through text' (imguiapp_nodes.h:825, applied ~3674); phase bands hug the layer nodes' edges (band/hug geometry, nodes.cpp ~3651 and hug-line routing ~6300-6440).
- [t] **Structural graph invariants in validation** — Tests call AppGraphValidate only for expr errors and severity (1695-1700, 1908-1916); no test constructs a forest violation, duplicate producer, or confluence conflict and asserts the issue.
  - evidence: AppGraphValidate (imguiapp_nodes.cpp:8673) implements all four: (a) port-kind pairing ('link %d violates port-kind pairing'), (b) containment forest (<=1 parent, acyclic parent walk), (c) one producer per type (''%s' and '%s' both emit data type'), (d) event confluence (same-persist-field writers flagged). Link-time twin AppGraphCanLink tested by step11_typed_link (tests/imguiapp_nodes_tests.cpp:535-538) and step13_cycle_rejected (650).
- [t] **Wire-drop kind-filtered palette** — No test drags a pin to empty canvas and asserts the palette.
  - evidence: CanvasWireDropped -> ##AppGraphDropCreate popup (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:8415-8519): kind candidates from source port, auto-connect via AppGraphTryConnect (8494), detach-drop guard DragWasDetach (8419); engine event WireDroppedReq (imguiapp_canvas.cpp:196-198); status hint states it (8190).
- [t] **Outliner row hover -> canvas node halo** — Halo render itself has no test (only the channel is tested).
  - evidence: Tree row hover publishes Tree-source hover (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp, AppGraphHoverNode(g, n->Id, ImGuiAppHoverSource_Tree) in the outliner row); halo pass draws accent outline for non-canvas-source hovered node (8094-8118). Channel tested with Tree source in step25 (tests:1888-1891).
- [t] **Severity dot on canvas node title bar** — Dot rendering itself untested (backend is).
  - evidence: Landed: 'Ambient problem marks: a severity dot on the node's title bar' — per-node severity dot drawn top-right of title (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:8160-8174) from the signature-keyed cache AppGraphIssuesCached/AppGraphNodeSeverity (3185-3248). Severity backend tested in step25 (tests:1908-1916).
- [t] **Group collapse (LOD by folding)** — No test folds/unfolds a group (grep 'Collapsed' in tests/ = 0 hits).
  - evidence: GroupCollapsed toggle on fold click (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:6244-6245), AppNodeHiddenByCollapse hides descendants (4670-4680), palette 'Groups: Collapse all / Expand all' (7786).
- [t] **Viewport gizmo column** — No test clicks any gizmo; given this exact column already shipped once as dead chrome, a headless click-path test is the obvious missing regression guard.
  - evidence: C:\dev\imguix\imguix\imguiapp\imguiapp_nodes.cpp:8128-8216: top-right draw-list column with Add/Frame/Fit/Tidy/Snap + Overlays popover (##CanvasOverlays 8182-8191: grid/bands/frames/minimap) + view-scope gizmo. Hit-test fix recorded in docs/usability-findings-2026-07.md:13 (icons were dead chrome until ImGuiHoveredFlags_ChildWindows added).
- [t] **Viewport health strip, click reveals Output** — No test exercises the strip or its click-to-Output path.
  - evidence: C:\dev\imguix\imguix\imguiapp\imguiapp_demo.cpp:1532-1581: bottom-left overlay window ##canvas_health with error/warn counts + last log line, severity color 1562, click sets OpenOutput 1571; wired to RevealPanel=ComposerPanel_Output at 1305-1311 which force-opens the bottom panel.
- [t] **Root furniture unchanged invariant** — No regression test ties root furniture to the density flip specifically (earlier root tests cover furniture independently).
  - evidence: The flip gates only body rows (detail branch inside CanvasBeginNode); group-frame passes, trunk connectors, section packer (section_active requires root scope, :~4885), and phase bands take no AppScopeDetailAltitude input — unchanged by construction.
- [t] **DataTypeId via local _ConstantHash mirror, recomputed on rename** — No test asserts AppConstantHash == runtime ImGuiType<>::ID; a stored DataOut port's DataTypeId is stamped at creation and no rename-restamp was found (promotion handles rename by recompute-on-use, the dup-type gate reads the possibly stale stored id)
  - evidence: AppConstantHash nodes.cpp:1249-1252 (mirror of ImGuiStatic::_ConstantHash per comment 1247-1248); AppNodeStructTypeId 1254-1261 hashes sanitized '<Name>Data'; builtins hash DataTypeName (1284). Used by port stamping, dup-dep check (2902), live-mirror promotion (11888-11894 recomputes at mirror time)
- [t] **EditAppDataEdgeBindings inline UI inside DataIn attribute** — No test drives the binding-row widgets ('Add binding' appears in no test); the imnodes 'inside DataIn attribute' rule is moot under the canvas engine
  - evidence: Impl nodes.cpp:3046-3088 (per-link binding rows, add/remove, hover brushing); invoked inside the Control node body on the canvas at nodes.cpp:6834 and reachable from the inspector. Bindings themselves tested at model level (step14 codegen, step15 persistence)
- [t] **Pan bindings and Ctrl multi-select carried over** — No test drives LMB-empty pan, RMB pan, or Ctrl multi-select through the FSM (tests set pan programmatically via CanvasSetPan; no KeyCtrl usage in tests/).
  - evidence: LMB-on-empty pan (C:/dev/imguix/imguix/imguiapp/imguiapp_canvas.cpp:824-825, 839-843), RMB-drag pan via MenuPending travel (:920-929), Ctrl+click additive toggle (:798-811); defaults on (IO ctor :216-218).
- [t] **File and naming conventions for the engine** — None — structural convention, fully satisfied by inspection; no runtime test applies.
  - evidence: C:/dev/imguix/imguix/imguiapp/imguiapp_canvas.h/.cpp exist; namespace ImGui (canvas.cpp:405, 957); every symbol Canvas*-prefixed; types ImGuiCanvasStyle/ImGuiCanvasIO (canvas.h:9, 56); includes are only imgui.h/imgui_internal.h/string.h — zero imnodes includes (canvas.cpp:13-17).
- [t] **LastLinkErr/LastLinkErrSeq graph fields** — No test touches the fields.
  - evidence: imguiapp_nodes.h:636-637: char LastLinkErr[IM_LABEL_SIZE] + int LastLinkErrSeq with 'transient, NOT in Save/Load' comment; no serialization references anywhere (grep).
- [t] **Rejection-branch-only toast driving rule** — No test drives a rejected drag.
  - evidence: imguiapp_nodes.cpp:3010 clears LastLinkErr on successful create; 3014-3018 writes err + Seq++ only in the else of AppGraphResolveLink inside the wire-created block, with the exact comment 'NOT the changed return (also true on link destroy)'.
- [t] **Canvas link-reject toast** — No headless test asserts the toast renders.
  - evidence: imguiapp_nodes.cpp:8368-8384 in ShowAppGraphEditor after CaptureAppGraphLinks (8288-8289): GetItemRectMin/Max anchor at canvas bottom-left, em padding, kFade=2.5s alpha fade, ToastSeq/ToastT0 re-armed on LastLinkErrSeq change (editor-state, not function-local static).
- [t] **Tree-to-canvas apply plus reveal pan** — No test asserts the reveal pan or the no-pan-on-canvas-click rule.
  - evidence: imguiapp_nodes.cpp:8411-8456: external (non-canvas-originated) change vs AppliedSel latch -> CanvasSelectNode + minimal-pan reveal (only nudges when outside a 2-em margin; never pans on canvas clicks), plus scope-chain navigation for out-of-scope picks (8423-8434) — the canvas-engine analog of EditorContextMoveToNode.
- [t] **Shared origin tint constants** — The promised third consumer (demo legend) does not exist.
  - evidence: kAppHueLive (steel-blue) / kAppHuePromoted (green) at imguiapp_nodes.cpp:238-239, theme-derived into ImGuiAppComposerStyle::OriginLive/OriginPromoted (nodes.cpp:289-290, nodes.h:806-807); single accessor AppGraphOriginColor (nodes.cpp:3364-3368, comment: 'One origin vocabulary shared by the canvas..., the tree text tint, and the demo legend') consumed by the canvas origin dot (6595) and tree rows (12734). Names differ from the spec'd kAppLiveTint but the one-vocabulary goal holds.
- [t] **LMB-drag pan on empty canvas** — No test drives an LMB drag on empty canvas through the input FSM; step19b only uses the CanvasSetPan/GetPan API. Doc's described mechanism (imnodes three-button emulation) is stale.
  - evidence: Behavior shipped natively in the custom canvas engine (imnodes is gone from code entirely): IO.LmbPansEmptyCanvas=true default (imguiapp_canvas.cpp:216), empty-canvas LMB press enters ImGuiCanvasInteraction_Pan (~824-825), pan applied while held (~839-844). canvas-engine-design.md:33 records 'three-button emulation machinery' as Dropped — the doc's claimed mechanism was superseded, not kept.
- [t] **RMB-drag pan** — No test drives an RMB drag-pan gesture.
  - evidence: IO.RmbPans=true default (imguiapp_canvas.cpp:217); RMB press enters MenuPending (~830-834); travel beyond slop pans and keeps panning (~924-928).
- [t] **Box-select LMB binding removed** — No test asserts box-select absence or exercises Ctrl+click multi-select / 'A' select-all through input.
  - evidence: Canvas engine has no box-select at all: imguiapp_canvas.h:59 comment 'LMB-drag on empty canvas pans (no box select)'; canvas-engine-design.md:33 lists box selector as Dropped. Replacements present: Ctrl+click additive toggle (imguiapp_canvas.cpp:798-811), 'A' select all/none (imguiapp_nodes.cpp:~7414; F1 help ~8300).
- [t] **Minimal-pan outliner reveal** — No test asserts the minimal-pan reveal or the in-view-never-moves invariant.
  - evidence: imguiapp_nodes.cpp:~8245-8258: tree-originated selection nudges CanvasSetPan by the minimal delta to bring the node rect inside a 2-em margin; zero delta when already in view; '(don't yank the viewport on a click)' guard for canvas-originated changes (~8216-8217). EditorContextMoveToNode no longer exists in code (imnodes removed); CanvasCenterOn is used only for explicit user verbs (frame gizmo ~7976-7981, minimap imguiapp_canvas.cpp:727-731).
- [t] **Rule: the camera belongs to the user** — No test asserts the no-implicit-centering invariant directly (step19b covers camera memory, not reveal restraint).
  - evidence: Enforced in code: reveal uses minimal pan with in-view no-op (imguiapp_nodes.cpp:~8245-8258), 'Never pan on a canvas-originated change' (~8216-8217), CanvasCenterOn called only from explicit user verbs (frame gizmo ~7976-7981, Home/fit, minimap click imguiapp_canvas.cpp:727-731); per-scope camera memory restores the user's exact pan/zoom (tested: step19b_scope_camera_memory, tests:1490-1540).

**Parked (deferred by intent)**

- Lifecycle view root chart — Entire feature unbuilt; doc explicitly frames it as North-star direction, not scheduled work.
- Lifecycle spine + phase/init/decommission bands — Spine, loop-back edge, and init/decommission bands all unbuilt.
- Event slots in true call order — No lifecycle-slot visualization; only the order strip chips and title-bar ordinals exist as precursors.
- Framework internals as grey context rows — Unbuilt; depends on the lifecycle chart existing first.
- One-frame skew arrow — Visualization unbuilt; semantics proven only in the contract suite.
- Filter co-application to canvas — Not started, as scheduled.
- Note annotation node kind — Not started; explicitly queued for S2.
- Reroute/elbow pins on wires — Not started, as scheduled (S5).
- Align and distribute selection ops — Not started; queued for S2.
- Selected-wire flow-direction animation — Not started, as scheduled (S5).
- Always-on wire animation — No flow animation exists anywhere (no GetTime-driven wire drawing in imguiapp_canvas.cpp/imguiapp_nodes.cpp) — the rejection is honored in code.
- Per-node body LOD collapse — R7 as specified not started, as scheduled (S5).
- Hiding filtered-out nodes — The chosen encoding (dim-to-25%) is equally absent since T3 hasn't started.
- Box selector — Absent as designed: no box-select code in C:/dev/imguix/imguix/imguiapp/imguiapp_canvas.cpp; LMB on empty canvas pans instead (:824-826); canvas.h:59 comment 'LMB-drag on empty canvas pans (no box select)'.
- Three-button emulation machinery — Absent as designed: no emulation code; bindings are first-class ImGuiCanvasIO fields (C:/dev/imguix/imguix/imguiapp/imguiapp_canvas.h:56-63) consumed directly by the FSM (canvas.cpp:747, 824, 924).
- Pin shape variety (only circle + square kept) — Exactly two shapes: ImGuiCanvasPinShape_Circle/Square (C:/dev/imguix/imguix/imguiapp/imguiapp_canvas.h:152) with the semantic split data vs containment carried into style colors (PinData/PinContainment, canvas.h:29-30) a
- Attribute flags stack — Absent as designed: no push/pop attribute flags API anywhere in C:/dev/imguix/imguix/imguiapp/imguiapp_canvas.h; per-pin intent is one-shot CanvasNextPinColor/CanvasNextPinSide (:154-155) consumed by the next BeginPin.
- Multiple editor contexts — Context-stack machinery absent as designed: ImGuiCanvasState is opaque, passed explicitly, 'no context stack' (C:/dev/imguix/imguix/imguiapp/imguiapp_canvas.h:65). Multiple independent canvases fall out for free (tests a
- Auto-panning at canvas edge — Absent as designed ('re-add if missed'): no edge-proximity pan in the FSM (C:/dev/imguix/imguix/imguiapp/imguiapp_canvas.cpp:716-951); drags past the canvas edge do not scroll the camera.
- LinkLineSegmentsPerLength tuning — Absent as designed: no tessellation knob; wire hit-test uses fixed 24 segments (C:/dev/imguix/imguix/imguiapp/imguiapp_canvas.cpp:349), dashed bodies 26 (:1534), solid bodies use ImGui's AddBezierCubic default tessellati
- imnodes style stack — Absent as designed: no PushColorStyle/PushStyleVar canvas analog; ImGuiCanvasStyle is a plain struct hosts mutate directly via CanvasGetStyle (C:/dev/imguix/imguix/imguiapp/imguiapp_canvas.h:71, e.g. imguiapp_nodes.cpp:5
- Pre-drop can-link hint — Horizon item, still deferred as documented.

<details><summary>Shipped-tested (verified, no action)</summary>

- Minimap
- Rule: core four phase layers never reorder
- Core phases immutable
- Execution-order rails and sequence badges
- Machine-checked invariant: edges are kind-well-formed
- Per-scope camera memory
- Shared HoverSync channel
- Group title-bar drag machinery
- Minimap corner inset
- Canvas optical zoom
- F frame-selection / Home fit-all
- Brushing hover sync
- Ambient problem marks
- Minimap overlay
- Canvas zoom
- Depth ordering of scope-interior elements
- All in-scope annotations on the annotation draw list
- Baseline: legacy draft-node helpers
- Five node kinds enum
- Two edge kinds via trailing defaulted Link.Kind
- Stored port records with type hash
- Node embeds ImGuiAppNodeDraft
- ImGuiAppGraph owns demo's loose statics
- Field bindings stored on graph, keyed by LinkId
- Per-kind mandatory ports stamped at creation
- One multi-link DataIn pin per Control
- Dedicated BodyAttrId per node
- Single monotonic id allocator, resolve-by-search
- Delete sweeps links and orphaned bindings, survivors untouched
- AppGraphCanLink typed-link legality gate
- Cycle guard at link time
- Capture-then-refuse illegal links post-EndNodeEditor
- CaptureAppGraphLinks folds + validates
- ShowAppGraphEditor whole-graph render loop
- Demo rewired to one static graph seeded with App root
- Add-node palette popup by kind
- Test steps 1-9 kept intact
- step10_kind_ports test
- step11_typed_link_legality test
- step12_identity_survives_delete test
- step13_cycle_rejected test
- Hand-rolled canvas engine replacing imnodes
- Migration contract scoped to actual Composer usage
- BeginCanvas/EndCanvas editor frame
- Canvas node begin/end with title bar via app wrappers
- Pin attributes: input/output/static rows
- Links as cubic wires with hover and selection
- Node/link hover + selection API and per-node drag lock
- Model-space node position/size getters
- Camera pan get/reset and MoveToNode
- Plain style struct (colors subset, spacing, rounding, padding, pin radii, GridSnapping/GridLines flags)
- Model-space storage invariant
- Single camera transform {Pan, Zoom}
- No-pixels API rule with CanvasToScreen/ScreenToCanvas helpers
- Built-in cursor-centered wheel zoom and pan input
- Same-frame node measurement, no read-back phase
- Exact measurement by construction (group rect / same Zoom)
- Uniform-width layer-column constraint stays host-side
- EndCanvas hover resolution order
- Wires and pins drawn from this frame's model data
- Current-frame decoration geometry (stale-decoration class unrepresentable)
- Explicit single-enum interaction FSM
- Full input bindings policy
- Drag deltas divided by Zoom exactly once, at the FSM
- Migration behind a thin call-site adapter in nodes.cpp
- Slice C1: canvas child, native camera, grid, measured nodes, selection + drag
- Slice C2: pins, wires, hover, create/detach/drop events
- Slice C3: title bars/rename hooks, draggable locks, MoveTo/fit helpers, minimap
- Slice C4: Composer migration kills the seam (model cache, style trampoline, zoom reseat deleted)
- Slice C5: imnodes removed from the build
- AppGraphCanLink dead validator
- Canvas icon hover hit-test fix
- Short RMB click still opens context menus
- Containment pin sides swapped
- Canvas zoom (fork feature in imguix/imnodes)

</details>

### scopes  (25/56 shipped-tested; 11 untested, 10 partial, 9 missing, 1 parked)

**Build TODOs — missing**

- [ ] **Scope header row on inspector during drill** — Not started.
  - evidence: Inspector header is the static 'Inspector' caption regardless of scope (C:\dev\imguix\imguix\imguiapp\imguiapp_demo.cpp:1803-1805); no scope-identity row is rendered when g->ViewScope is non-empty.
- [ ] **Defect: identical card at both altitudes** — Doc §1.2 diagnosis is stale — superseded by shipped Rule D.
  - evidence: The described behavior no longer exists: AppScopeDetailAltitude:5241 gates the full authoring body (field editors, events, commands, bindings) on scope-parent, identity summary elsewhere; binding rows now gate on `detail && !altitude_root`. step37_density_flip proves cards differ by altitude.
- [ ] **Floating Begin/End plates** — None (absence is the intent) — two stale 'bracket' comments remain (file TOC line 15 'lifecycle brackets'; ~6022 'wall/bracket rects').
  - evidence: Absence confirmed: no plate/bracket drawing code remains; commits 67d9752→d9e48fb show brackets shipped then asserts dropped; Begin/End text now fused into the wall bands.
- [ ] **R7 per-node LOD toggle retained** — R7 was never implemented — scope-interior-design.md's 'remains as a manual override' asserts a feature that does not exist.
  - evidence: No LOD/body-collapse-per-node symbols anywhere in imguix/imguiapp or tests; composer-ui-design.md:152 defines R7 and :280 still lists it in the S5 backlog ('R2 reroute pins, R5 flow-on-select, R7 node LOD').
- [ ] **Owner as a giant in-scope node** — None — absence is by design.
  - evidence: Rejection holds: AppNodeInScope returns false for id==top so the owner never submits inside itself; AppDrawScopeWalls contains zero interactive elements (draw-only, no hit-tests); breadcrumb (AppDrawScopeBreadcrumb:5736) carries navigation.
- [ ] **Portals as real graph nodes** — None — absence is by design.
  - evidence: Rejection holds: no portal node kind; ImGuiAppScopePortal (h:855-862) is a transient view struct rebuilt per frame from Links; serializer writes no portal records (only Place=/graph lines); validation/codegen/outliner never see chips.
- [ ] **Fixed lane layout in-scope** — None — absence is by design; step39 additionally proves free interior placement.
  - evidence: Rejection holds: free placement via ScopePlacements (drag read-back stores per-scope at :~7163); scoped tidy is the on-demand arranger (AppGraphAutoLayout scoped branch); window-section semantics stay root-only (section_active requires AppScopeCurrent < 0).
- [ ] **Config editing in the wall title bar** — None — absence is by design.
  - evidence: Rejection holds: the readout is draw-list AddText only in AppDrawScopeWalls (dropped, never truncated, on collision); placement/dock editing lives in EditAppWindowNodeProps (node body/inspector).
- [ ] **External-dependency count badge instead of chips** — None — absence is by design.
  - evidence: Rejection holds: one chip per boundary-crossing link names its remote endpoint; no '+N external' aggregation exists (the strip's '+N' is order-strip overflow, a different surface).

**Build TODOs — partial**

- [ ] **Per-scope sequence layout (tidy on demand)** — No verb that arranges scope members left-to-right in execution order on demand — tidy is a containment-tree layout, not a sequence layout.
  - evidence: Scoped tidy exists (AppGraphAutoLayout filters by AppNodeInScope, imguiapp_nodes.cpp ~4900 'scoped tidy arranges only what the scope shows') and scope-local placement is shipped-tested (step39_scope_local_placement, tests/imguiapp_nodes_tests.cpp:2890-2953: interior move leaves root GridPos intact and is remembered). Execution-order member list exists (nodes.cpp:4986).
- [ ] **Scope navigation invariant tests** — The two named invariants — validate-on-mutate of ViewScope and breadcrumb chain == scope-parent chain — have no dedicated tests; tests mutate ViewScope directly without invariant assertions.
  - evidence: Navigation behavior is tested: step18_scope_tab_navigation (tests/imguiapp_nodes_tests.cpp:1348), step19_scoped_add_adopts (1427), step38-40 walls/placement/portals (2804-3033), headless composer_breadcrumb_visible (tests/imguiapp_headless_verify.cpp:242). Model-side revalidation-on-change is asserted via the signature-keyed issue cache (1914 'model change -> new signature -> revalidate').
- [ ] **Defect: owner evaporates in-scope** — Doc §1.1 diagnosis is stale — the defect was fixed by the shipped walls (Rule A/B).
  - evidence: Owner still never submits as a node (AppNodeInScope returns false when id==scope top, ~4596-4597), but AppDrawScopeWalls:5328 now renders the owner's silhouette as the room — 'only breadcrumb text gives context' no longer describes the code (breadcrumb still exists, AppDrawScopeBreadcrumb:5736).
- [ ] **Defect: cross-scope wires vanish** — Doc §1.3 stale as a pure defect — dependency is now surfaced via shipped portal chips (Rule E).
  - evidence: Normal wire still skipped when an endpoint is out of scope (link loop skips AppNodeHiddenByCollapse owners, ~7013 in earlier read), but AppScopeCollectPortals:5595 + AppDrawScopePortals:5645 now dock a wall chip and draw a chip→pin wire for every boundary-crossing data edge.
- [ ] **Outbound portal chip placement** — Label spec unimplemented; no outbound test (step40 covers inbound only).
  - evidence: Right-wall dock at the producer's DataOut pin row implemented (Inbound=false path uses wall.z + StartAttr pin), but the label is only '<consumer> ▸' — the spec'd 'db ▸ Peaks' form (binding-named source field, else producer's data name, then consumer) is not built.
- [ ] **Portal chip visual form** — Text slot is 1.0 em (PushFont(nullptr, em)) not the spec'd 0.9 em; border uses alpha over the plate rather than a mix toward the neutral line color; nothing asserted.
  - evidence: Pill fully rounded (rounding = height/2), thin border max(1px, em*0.07) in remote kind hue at 0.45 alpha, text same hue at 0.75 alpha, dark plate AppThemeNeutral(0.11, 0.98), hover brightens both.
- [ ] **Portal chip hover brushing** — Inside-pin halo from the spec not implemented; hover path untested; uses Canvas source (strip uses External) — verify the outliner reacts to Canvas-source hovers.
  - evidence: Hover: AppGraphHoverNode(remote, ImGuiAppHoverSource_Canvas) + tooltip + hand cursor — remote node rides the existing brushing bus (outliner tint).
- [ ] **Display-layer scope interior** — Identity-density cards with hosted count, and walls/rail treatment for layer scopes.
  - evidence: Drill + member filter + sequence (windows pass then sidebars, AppScopeSequenceIds Display branch) + title ordinals work; but no walls/rail (window/sidebar owners only) and members render window/sidebar prop bodies when drilled (props gate on AppScopeCurrent>=0), not identity + hosted count.
- [ ] **Task-layer scope interior** — No walls/rail for the Task scope; no tests of this scope kind.
  - evidence: Topo-order sequence (AppScopeSequenceIds Task branch via AppGraphTopoOrder); density flip already grants detail to app-level controls whose scope-parent is the Task layer (AppScopeParentOf control fallback :~4530).
- [ ] **Command-layer scope interior** — Command chips on identity cards not shown (EditAppControlCommandChoices is detail-only); no walls/rail; untested.
  - evidence: Members = emitter controls only (AppNodeInScope: Commands.Size > 0, :~4607); push-order sequence branch; emitters land at identity density by the generic predicate, and the summary line says 'emits X'.

**Test-debt TODOs — shipped, untested**

- [t] **Machine-checked invariant: containment is a forest** — No test constructs a multi-parent or containment-cycle graph and asserts the issue; only step21 calls AppGraphValidate and it checks the event-expr path. up-next.md:43-44 lists validate-on-mutate scope tests as Soon.
  - evidence: AppGraphValidate check (b): <=1 containment parent and acyclic parent walk (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:8633-8657); gesture gate also rejects a second parent at link time ('node already has a parent', 2916-2919). Validate runs wholesale after load/paste/import/undo per the comment at 8602-8608.
- [t] **End band closes the room** — Exercised under step38/capture but nothing asserts the End band's presence or alignment.
  - evidence: AppDrawScopeWalls: bottom band rect (end_m = em*1.1), 'End()' muted in code font at the same left x as 'Begin(' (smn.x + em*0.75); members run between the bands.
- [t] **Void dimming outside the walls** — No assertion; verified by pixel-review capture only.
  - evidence: Four AddRectFilled rects outside the wall rect with AppThemeDark(0.45f) on the canvas background list; no interior fill anywhere in the wall pass.
- [t] **1-em rails on left/right walls** — No test asserts rails; 'empty rail reads as no external dependencies' is a reading, not a mechanism.
  - evidence: rail_m = em_m * 1.0f folded into the wall target rect; two rail fill rects AppThemeNeutral(0.14, 0.85) on left/right between the bands; portal chips dock straddling the wall edge (dock at wall.x/wall.z in AppDrawScopePortals).
- [t] **Wall shrink deadband (grow instant, shrink 1.5 em)** — No test drives a shrink past/inside the deadband to assert stability during drags.
  - evidence: Grow-fast/shrink-slow per edge in AppDrawScopeWalls: dead = em_m * 1.5f; expansion applies immediately, contraction only past the deadband; rect reseeds on scope change (ScopeWallScope check).
- [t] **Removal of number circles + dashed sequence arrows** — No test asserts absence (reasonable); nothing remains to remove.
  - evidence: No sequence-circle or dashed-sequence-arrow code found; remaining circles are severity dots (:8159-8174) and diff dots, remaining dashes are Soft-link wires (CanvasNextWireDashed:932, link loop). Sequence lives only in the strip + title ordinal.
- [t] **Title ordinal badge on members** — Exercised by every drilled test but no assertion on badge presence/content.
  - evidence: Submission loop: when drilled, scope_seq lookup emits ImFormatString('%d/%d') → CanvasNextNodeTitleBadge per member — same idiom as the layer badge a few lines below.
- [t] **Existing altitude gates (structs/fields/bindings below root)** — No test directly asserts struct/field root eviction or binding-row root gating (step37 covers the generalized density gate only).
  - evidence: altitude_root (at_root && Display present): struct/field kinds never submit at root (submission-loop skip), struct/control-cluster group boxes gated !altitude_root, binding editors gated `detail && !altitude_root`.
- [t] **Card geometry invariant across altitudes** — No test compares node width/rounding/pin positions across the flip.
  - evidence: UniformCardW applied via CanvasNextNodeWidth to every non-layer node independent of altitude; rounding/title-chrome switch independent of the detail flag; deps/DataOut pins submitted at both altitudes so wires land identically.
- [t] **Control scope interior (struct plates + fields)** — No test drills into a control scope and asserts the plates/fields population.
  - evidence: AppScopeCanEnter includes Control; struct's scope-parent is its owning control (AppScopeParentOf:4506 branch), fields chain through their struct; struct/field kinds never submit at root (altitude_root gate); no rail/walls for control scopes by design (AppScopeWallsWanted false).
- [t] **Struct scope interior (field pills)** — No dedicated test or design pass for the struct interior.
  - evidence: Works via generic machinery: AppScopeCanEnter includes Struct, field's scope-parent is its struct, field nodes render as pills (CanvasNextNodeRounding 999), no walls/rail for struct scopes.

**Parked (deferred by intent)**

- Sequence-reorder drag on strip chips — Drag not implemented — matches doc's 'coming'.

<details><summary>Shipped-tested (verified, no action)</summary>

- Remember camera per scope
- Drill-down scopes
- Drill-down scopes (Tab in, Esc out, breadcrumb path)
- Scope drill-down navigation
- Scope breadcrumb canvas overlay
- Drill-down scopes with breadcrumb
- Scope drill filters submitted nodes
- Wall silhouette reuses owner's root card
- Wall rect derived from member bounds
- Face band renders the Begin() code line
- Read-only config readout on face band
- Runs order strip in face band
- Order-strip chip hover halo + click select
- Per-frame published strip chip rects
- Density-flip law (detail one scope below owner)
- Identity-card content spec
- Detail-card content spec
- Portal chips for single-inside-endpoint links
- Inbound portal chip placement
- Portal chip click-jump
- Chips as pure per-frame derivation
- Window/Sidebar scope interior
- Root collapses controls to identity cards
- Density flip as pure model predicate
- Chip hit-test overlay rule

</details>

### chrome  (8/83 shipped-tested; 33 untested, 20 partial, 12 missing, 10 parked)

**Build TODOs — missing**

- [ ] **Composer toolbar time scrubber ('App time')** — The scrubber UI, mirror freeze/rewind wiring, and the Breathing-runs-backwards demo path. The underlying ImGuiAppStateHistory mechanism is shipped and tested separately.
  - evidence: No 'App time' string in any .cpp/.h; AppStateSnapshot/AppStateRestore/ImGuiAppStateHistory are referenced only in imguiapp.h/.cpp, tests, and imguiapp_av.h — never by any UI. The Composer toolbar (C:/dev/imguix/imguix/imguiapp/imguiapp_demo.cpp:556-750) has only the graph EDIT history dropdown (ICON_FA_CLOCK_ROTATE_LEFT -> AppGraphHistoryGoto at 723-737), which scrubs undo snapshots of the graph, not app state. up-next.md:101-104 and big-idea.md:50-51 claim it landed, but no code exists.
- [ ] **Problems count badge in toolbar** — No toolbar issue-count badge colored by worst severity, despite up-next.md:73 claiming 'problems count' landed in the chrome pass.
  - evidence: The toolbar right cluster contains only Built/Stale badge + outliner/inspector/Code/Live toggles (C:/dev/imguix/imguix/imguiapp/imguiapp_demo.cpp:790-856); NumErrors only colors the Generate button (763-772). The issue count appears on the Output tab label (1700-1704), not the toolbar.
- [ ] **Single motion idiom: 150 ms linear alpha fade** — Not started.
  - evidence: No fade/animation system in imguiapp_nodes.cpp or imguiapp_demo.cpp; the only time-based transient is the 3-second link-error toast expiry (imguiapp_nodes.cpp:8105, 8455). Overlays appear/disappear instantly.
- [ ] **Frozen status-bar zone map with clickable facts** — All four click actions (breadcrumb select, counts→filters, mirror→Live, freshness→Generate) unimplemented; zone map not frozen as specified.
  - evidence: Right zone is one non-interactive TextDisabled string (C:\dev\imguix\imguix\imguiapp\imguiapp_demo.cpp:973-983): transient msg + breadcrumb + counts + mirror concatenated; freshness lives in the toolbar Built/Stale button (792-822), not the status bar. Nothing on the right is clickable.
- [ ] **Layout presets Compose/Review/Observe** — Not started (panel-state substrate now exists via the sidecar fields, so this is buildable).
  - evidence: No preset code: 'Compose/Review/Observe' and preset machinery absent from imguiapp_demo.cpp/imguiapp_nodes.cpp (grep across imguix/imguiapp *.cpp/h).
- [ ] **Interaction-state ladder** — Not started.
  - evidence: No rest/hover/active/selected/disabled/mixed rung constants exist; widgets use bespoke per-widget hover colors (AppBlFill/FillHover/FillEdit imguiapp_nodes.cpp:325-328, gizmo dim/lit 8146-8147, kebab 0.55 alpha 412).
- [ ] **Em spacing quantum audit** — Audit/normalization pass not started.
  - evidence: Audit not performed: em-derived sizing is widespread but raw-pixel constants remain, e.g. CanvasFitRect 60.0f margin (imguiapp_nodes.cpp:7277), 2.0f halo stroke (8056-8057), 9.0f grab offsets in tests; em multiples used are not 0.25-quantized (0.55, 0.72, 1.2, 0.35 ...).
- [ ] **StatusPill shared grammar** — No shared status-text primitive; ok/warn/err colors repeated inline at each surface.
  - evidence: No StatusPill symbol anywhere in code (only in the design doc). Status surfaces use ad-hoc TextColored/TextDisabled with inline duplicated palettes (demo.cpp:971, 1168, 1193, 1196-1197, 1747).
- [ ] **Status strip PERF segment (single FPS)** — PERF segment (FPS + ms + backend/vtx/idx tooltip) never built; pointer text overpromises.
  - evidence: No FPS/io.Framerate/io.Metrics* readout exists anywhere in imguiapp_demo.cpp or the composer (repo-wide grep hits only implot/implot3d demos). The pointer line demo.cpp:1942 even promises 'status strip for composition, lifecycle and FPS' — the FPS part is stale copy.
- [ ] **Origin legend micro-row** — Legend under the toolbar never built; origin vocabulary is undocumented in-UI.
  - evidence: No legend row anywhere in imguiapp_demo.cpp or nodes.cpp; the only mention is the aspirational comment 'exposed so the demo legend reads the same constants' (nodes.cpp:3362-3364).
- [ ] **Standalone App metrics table**
  - evidence: Rejection honored: no App metrics table and no FPS row exists anywhere in the composer/demo (grep across imguiapp_demo.cpp and composer code finds zero FPS/Framerate/io.Metrics readouts).
- [ ] **Cycle Select button from topo_order scan**
  - evidence: Rejection honored: no Select button; the cycle issue is pushed with NodeId=-1 and the Output row click is gated on NodeId>=0 (demo.cpp:1752-1755), so it is deliberately non-navigating.

**Build TODOs — partial**

- [ ] **Link-rejection errors move to status bar** — The floating canvas toast was NOT replaced: 'B1: transient link-rejection toast' still fades 2.5s at canvas bottom-left (imguiapp_nodes.cpp:8514-8531). Both render.
  - evidence: Rejections write g->LastLinkErr/Seq (imguiapp_nodes.cpp:3010-3017); status hint overrides red (severity 2) for 3s (8179-8188); demo renders it red (imguiapp_demo.cpp:971).
- [ ] **Four-roads-to-every-verb rule** — Rule is satisfied for the major verbs but not audited/enforced per verb; no completeness test.
  - evidence: Space palette lists editor verbs WITH shortcuts (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:7772-7792) plus host document verbs (7798-7813, demo host_cmds imguiapp_demo.cpp:1516-1528); RMB canvas/node menus (7189+); direct keys (7279-7512).
- [ ] **Space palette as completeness check** — 'If it's not in the palette it doesn't exist' is not enforced by any audit/test; completeness is by hand.
  - evidence: Palette shipped with editor verbs + shortcuts, host document verbs, overlays, scope verbs, and go-to-node model search (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:7744-7845); the previously-listed gaps (Tidy/Frame/Hide/copy) are in.
- [ ] **Transient feedback consolidation on status bar** — The floating canvas link-error toast still exists (8514-8531) — one slot/one fade not yet true; editor-side actions like copy-selection emit no confirmation.
  - evidence: Status strip right slot renders transient confirmations from doc->WriteMsg ('generated C++ -> clipboard', 'diff vs saved -> clipboard', write messages) (C:/dev/imguix/imguix/imguiapp/imguiapp_demo.cpp:905,958,975-976); editor notices share the LastLinkErr status-hint channel via AppGraphNotify (imguiapp_nodes.cpp:4626-4635).
- [ ] **Add-node and Tidy dropped from toolbar** — Add still lives in the document toolbar, contrary to §7's relocation of both.
  - evidence: Tidy is out of the toolbar (only L key, palette, gizmo 8176). But an 'Add' button remains as the toolbar's compose verb (C:/dev/imguix/imguix/imguiapp/imguiapp_demo.cpp:696, routed to AppGraphRequestAddPalette imguiapp_nodes.cpp:3152,7217-7221).
- [ ] **Bottom panel tab set Code/Project/Preview/Output** — Preview tab absent from the tab set (moved to the node inspector); Output brushing untested.
  - evidence: C:\dev\imguix\imguix\imguiapp\imguiapp_demo.cpp:1602-1778: tabs are Code (source-mapped view w/ brushing 1019-1065), Project (doc-file table 1647-1699), Output (issues+log, severity toggles/filter, row click selects node, hover brushes via AppGraphHoverNode 1752-1757). No Preview tab — Preview is an inspector section instead (1821-1822). Tests: step26 (Project tab appears on Code toggle), headless composer_code_toggle (tests\imguiapp_headless_verify.cpp:258).
- [ ] **Panel lifecycle contract** — The unifying contract (identity + state records per panel) is not built; three storage shapes remain.
  - evidence: Two of three ad-hoc mechanisms addressed: WantOutputTab replaced by the ComposerPanel_* RevealPanel intent (imguiapp_demo.cpp:341,466,1305-1311) and panel state persisted via the sidecar (357-436 incl. TreeOpen/InspOpen). But no panel contract object exists: CodeH>0 still means open (687,1308), splitter floats still live loose in doc data (366), and there is no stable id/icon/display-name registry.
- [ ] **Four-roads audit enforced by registry** — No single command registry; align verb exists nowhere; context menus/shortcuts don't render from the palette table.
  - evidence: The named palette gaps are closed: Layout: Tidy / View: Fit all / View: Frame selection / View: Hide selection all in the palette (C:\dev\imguix\imguix\imguiapp\imguiapp_nodes.cpp:7828-7830); host verbs flow through one table (ImGuiAppGraphHostCmd, demo.cpp:1516-1528). But context menus (7600-7794), shortcuts (7249-7497) and status hints (8096-8126) are hand-rolled separately — nothing is enforced by construction.
- [ ] **New keyboard shortcuts** — Ctrl+P missing; none of F2/Del/nudge/Ctrl+S are key-driven in tests.
  - evidence: F2 rename-selection C:\dev\imguix\imguix\imguiapp\imguiapp_nodes.cpp:7382-7387; Del delete 7249-7271; arrow-key nudge at grid quantum (Shift x10, model units) 7454-7475; Ctrl+S save imguiapp_demo.cpp:746-747. Ctrl+P palette NOT bound — palette opens on Space only (7377-7378; no ImGuiKey_P anywhere).
- [ ] **Save split-button** — Family members Save As / Save Copy prefab-set missing.
  - evidence: Split anatomy landed: Save primary + chevron dropdown ##save_family (C:\dev\imguix\imguix\imguiapp\imguiapp_demo.cpp:746-758). But the family contains only 'Load graph' — no Save As, no Save Copy prefab-set.
- [ ] **Command registry** — No id/icon/availability-predicate/run() registry; context menus, gizmo tooltips, status hints and shortcut handling do not render from any registry.
  - evidence: A minimal host-verb table exists: ImGuiAppGraphHostCmd {Label, Shortcut, Id} registered per frame via AppGraphSetHostCommands (C:\dev\imguix\imguix\imguiapp\imguiapp_nodes.h:758-770, imguiapp_demo.cpp:1516-1528) and rendered by the palette (imguiapp_nodes.cpp:7852-7864) with picks consumed by the toolbar (demo.cpp:859-871). Editor verbs are a palette-local static Cmd table (7823-7844).
- [ ] **Complete self-describing chrome theme table** — 'Every push-stack style' is far from covered; only Bl combo/edit chrome is desc-table-ized.
  - evidence: The read-write surfacing landed: project inspector's Composer theme section edits the Combo/Edit desc rows live incl. Active flags (C:\dev\imguix\imguix\imguiapp\imguiapp_demo.cpp:1204-1228; nodes.h:835 comment). But only those two tables exist — other push-stack styles (toolbar health colors, overlay plates, gizmo colors, kDemoGold etc.) remain hardcoded constants.
- [ ] **Type scale 1.0/0.9/0.8** — Scale drifted to ad-hoc 0.7-0.8 factors; 'it held' is not verifiable in code.
  - evidence: Small type exists but not on the documented scale: PushFont factors are 0.78 (toolbar captions, imguiapp_demo.cpp:876), 0.75/0.8/0.7 (scope walls/strip, imguiapp_nodes.cpp:5397,5404,5491,5540). No 0.9 rung and no codified scale constants.
- [ ] **Status cluster topo indicator and edge count** — No literal red 'cycle!' lamp in the status cluster — the status strip (demo:892-987) shows keymap hint + design/live/promoted + mirror counts; cycle/edge readouts live in the Project panel and problems badge instead; none of these readouts are test-asserted
  - evidence: Edge count exists: '%d links %d bindings' (demo:1182, Project panel). Cycle surfaced via AppGraphValidate pushing severity-2 'dependency cycle: ...' (nodes.cpp:8655-8659) into NumErrors badges (demo:497-500,763,1191,1535) and codegen abort comment (10359); Generate flows through GenerateAppGraphCode (demo:605,650,1383)
- [ ] **Status strip HEALTH segment** — No always-on strip HEALTH pill ('graph ok' / cycle string / 'codegen blocked' tooltip); health is panel- and button-borne, not strip-borne.
  - evidence: Cycle text surfaces verbatim via Output tab issues + counts (demo.cpp:1700-1755), Validation inspector section (demo.cpp:1189-1200), and a red Generate button with '%d error(s)' tooltip (demo.cpp:765-772).
- [ ] **Delete narrow-window overlap guard** — Not replaced by StatusPill+ToolSep segments that wrap/clip within the strip; a new ImMax-style guard idiom exists instead.
  - evidence: The old right-edge cluster and its guard were deleted with the whole toolbar rewrite; the replacement uses SameLine(ImMax(cursor+em, region-cluster_w)) overlap handling in toolbar and strip (demo.cpp:809, 981).
- [ ] **Cycle name shipped via strip, no Select button** — Surfaced through the Output panel/Generate button rather than the specified strip HEALTH segment.
  - evidence: Cycle string surfaces verbatim ('dependency cycle: <err with node name>') as a severity-2 issue with NodeId=-1 so the row is deliberately non-clickable (AppValidatePushIssue call in AppGraphValidate; row click gated on it.NodeId>=0 at demo.cpp:1752-1755); no Select-offending-node button exists anywhere.
- [ ] **Codegen warnings count + popup** — No scan of generated text for '// WARNING'/'// codegen aborted', no amber '(!) N' next to Generate, no popup — so warnings that exist only as generated-code comments (e.g. dropped binding) are not counted anywhere.
  - evidence: Superseded by structured issues instead of comment-scanning: AppGraphValidate/AppGraphIssuesCached feed NumErrors/NumWarnings (demo.cpp:496-500), Output tab labeled 'Output (N)' (1700-1704), severity-filtered clickable rows that select the offending node (1742-1757), 'Open Output' button (1198).
- [ ] **HelpMarker design/live/promotion copy** — No single in-UI explanation of the design->live->promotion model, and no note that promoted tint appearing only while the mirror runs is self-consistent.
  - evidence: HelpMarker itself no longer exists. Honest per-control copy shipped instead: Live-eye tooltip 'Show / hide read-only nodes mirrored from the running app' (demo.cpp:856), Stale/Built bootstrap tooltips (818-822), scope caption 'promote a member to author against it' (nodes.cpp:5833).
- [ ] **Checkbox rename 'Show live mirror' + honest tooltip** — Not the literal 'Show live mirror' checkbox nor the exact 'Hiding never deletes your design.' sentence (that phrasing lives only in code comments/tests).
  - evidence: Shipped as a toolbar eye toggle labeled 'Live' (ICON_FA_EYE/EYE_SLASH, demo.cpp:800, 850-856) with tooltip 'Show / hide read-only nodes mirrored from the running app' — honest hide/show semantics; toggle behavior tested (nodes_tests:1966-1972).

**Test-debt TODOs — shipped, untested**

- [t] **View gizmo column** — No test clicks any gizmo button (no 'gizmo' hits in tests/); the verbs' underlying actions (tidy, fit) are tested only through other paths.
  - evidence: Top-right overlay column with exactly the claimed verbs: imguiapp_nodes.cpp:8210 'Viewport gizmo cluster (top-right overlay column): VIEW verbs only' — Add 8244, Frame selection 8249, Fit all 8256, Tidy 8258, Snap 8260, Overlays popover 8262-8271 (Grid/Bands/Frames/Minimap), plus scope indicator 8276.
- [t] **Bottom status bar with keymap hints** — No test asserts the strip's presence or hint content.
  - evidence: StatusStripControl rendered last -> window bottom (imguiapp_demo.cpp:895-987): left = live keymap hint via AppGraphStatusHint (969) with severity color; right = breadcrumb + design/live/promoted counts + mirror facts 'L# W# S# C# composed' (973-983). Hint composition per hover target in the editor (imguiapp_nodes.cpp StatusHint block, ~7905 pre-shift).
- [t] **Dead strip removed / grid clipped from title bars** — No pixel test asserts either property; the headless 'strip' checks (imguiapp_headless_verify.cpp:735) are about the scope order strip, not this.
  - evidence: Grid-vs-title fix is a real mechanism: ImGuiAppComposerStyle.GroupTitleBg documented 'opaque: grid must not bleed through text' (imguiapp_nodes.h:825) and applied at the group title plate (imguiapp_nodes.cpp ~3674). 'Dead strip' has no remaining symbol anywhere — consistent with removal, though absence is only negatively verifiable.
- [t] **Canvas-bottom status line (hover keymap + link-rejection messages)** — No test asserts hint text or the rejection-message takeover; placement differs slightly from the doc's 'along the canvas bottom' wording (it is the host window's bottom strip).
  - evidence: Hover-target keymap hints composed per frame in the editor (StatusHint block: pin/link/node/layer/canvas variants) with link-rejection override for 3 seconds at severity 2 (LastLinkErr/ErrSeq logic); displayed red at the strip (imguiapp_demo.cpp:967-971). The 'canvas bottom' line and the window status bar merged into one strip at the window bottom.
- [t] **Status published as status bar** — No test targets StatusLayer::OnRender output; the Composer's own status strip (StatusStripData, headless_verify:383) is a separate control and is what gets verified.
  - evidence: ImGuiAppStatusLayer::OnRender submits the 'AppLayerStatus' overlay window at the viewport bottom with App/Platform/Renderer lines (C:/dev/imguix/imguix/imguiapp/imguiapp.cpp:538-570). Core contract suite deliberately excludes the Status layer ('unconditionally submits a real ImGui window', core_tests:137); it renders during imguix-headless-verify recorded frames but no test asserts its presence or content.
- [t] **Problems tab with click-to-reveal** — No test clicks a problems row and asserts the reveal.
  - evidence: Output tab rows are Selectables; click writes selection = it.NodeId (C:/dev/imguix/imguix/imguiapp/imguiapp_demo.cpp:1752-1755); the editor applies external selection and scope-navigates to reveal (imguiapp_nodes.cpp:8552-8571). AppGraphValidate itself tested (tests/imguiapp_nodes_tests.cpp:1700, step13 cycle).
- [t] **Problems row hover -> node halo preview** — No test hovers a problems row.
  - evidence: Output row hover publishes External-source hover without committing selection (C:/dev/imguix/imguix/imguiapp/imguiapp_demo.cpp:1756-1757); halo pass consumes it (imguiapp_nodes.cpp:8094-8118).
- [t] **Status-bar live keymap hints** — No test reads the hint text.
  - evidence: AppGraphStatusHint API (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.h:749; imguiapp_nodes.cpp:3451-3456); editor computes per-hover-target hints (over_pin/over_link/over_node/empty, 8186-8194 and the empty-canvas line 'drag pan  wheel zoom  RMB add  Space palette  F frame  F1 help'); host renders it in the bottom status strip (imguiapp_demo.cpp:967-971).
- [t] **F1 card retained as full reference** — No test opens the card.
  - evidence: HelpOverlay flag (imguiapp_nodes.h:566); F1 key + palette 'Help: Shortcut card' toggle (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:7403,7428,7791,8132); overlay rendered at 8349; gesture list content near 8190-8210.
- [t] **Tidy (L) palette entry** — No palette test.
  - evidence: Gap closed: 'Layout: Tidy' with shortcut L in the command palette (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:7776, handler case 10 -> AppGraphAutoLayout at 7860); L key at 7396; gizmo button 8176.
- [t] **Frame (F) palette entry** — No palette test.
  - evidence: Gap closed: 'View: Frame selection' (F) in the palette (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:7776, handler case 12 fit_ids at 7862).
- [t] **Codegen-copy palette entry** — No test picks it from the palette.
  - evidence: Gap closed: host command 'File: Copy generated C++' registered into the palette (C:/dev/imguix/imguix/imguiapp/imguiapp_demo.cpp:1521,1528 AppGraphSetHostCommands) and consumed as ComposerHostCmd_CopyCode (859-864, action 647-653).
- [t] **Toolbar undo/redo + edit-history rail** — It is a popup list, not a rail; no UI test clicks undo/redo/history.
  - evidence: Undo/redo buttons with BeginDisabled when unavailable and step-named tooltips (C:/dev/imguix/imguix/imguiapp/imguiapp_demo.cpp:704-718); edit-history scrubber as a jump-list popup via AppGraphHistoryGoto (720-741, 643-646). Underlying named history tested by step15b (tests:840).
- [t] **Viewport gizmo column** — No test clicks a gizmo.
  - evidence: Landed: top-right draw-list overlay column with add / frame (F) / fit all (Home) / tidy (L) / snap (G, latched) / overlays popover / scope indicator, tooltips carrying shortcuts (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:8128-8196); hover fix for overlay hit-testing documented at 12161-12162.
- [t] **Overlays popover** — No test.
  - evidence: Gizmo lit when ANY overlay is off (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:8180 '!(ov_grid && ov_bands && ov_frames && ov_minimap)'); toggles OvGrid/OvBands/OvFrames/OvMinimap (5891-5934, palette cases 27-30).
- [t] **Status-bar right fact readouts** — No test reads the strip.
  - evidence: Landed in fixed order: transient Msg, selection breadcrumb (AppGraphSelectionBreadcrumb), 'design N live N promoted N' counts, mirror 'L# W# S# C# composed/uncomposed' (C:/dev/imguix/imguix/imguiapp/imguiapp_demo.cpp:897-979); all derived in OnUpdate, OnRender lays out text only — facts, nothing actionable.
- [t] **Status bar with keymap hints + facts** — No behavioral test (hint content, transient override, counts).
  - evidence: C:\dev\imguix\imguix\imguiapp\imguiapp_demo.cpp:891-987 StatusStripControl: left = live keymap hint via AppGraphStatusHint (composed per hover target in imguiapp_nodes.cpp:8096-8126, refused-link override in red), right = transient WriteMsg + selection breadcrumb + node counts + mirror facts. Headless composer_window_group_members only asserts StatusStripData exists (tests\imguiapp_headless_verify.cpp:383).
- [t] **RevealPanel intent API** — No payload argument; no test drives any reveal path (step26 tests the Code toggle, not reveal).
  - evidence: Landed as an int intent, not a function: doc->RevealPanel (ComposerPanel_*) at C:\dev\imguix\imguix\imguiapp\imguiapp_demo.cpp:341; producers: health-strip click 1305-1307, palette Panel: verbs via host cmds 866-868 + 1523-1525, toolbar Built/Stale button 815, Validation 'Open Output' 1198-1199; consumer: tab bar SetSelected 1605/1647/1706, auto-open 1308-1309, AckReveal consumption 1705.
- [t] **Shortcuts act on selection, never hover** — Convention only — no test or structural guard prevents a future hover-acting shortcut.
  - evidence: Rule implemented and stated in code: F2 comment 'Acts on selection, never hover -- hover is for brushing' (C:\dev\imguix\imguix\imguiapp\imguiapp_nodes.cpp:7380-7387); Del/H/nudge/F all read g->Selection or canvas selection (7249-7271, 7441-7451, 7462-7474, 7355-7363); no shortcut reads hovered_node.
- [t] **Command palette** — Ctrl+P binding missing (Space only); no test opens the palette or runs a pick.
  - evidence: ##cmdpalette landed (C:\dev\imguix\imguix\imguiapp\imguiapp_nodes.cpp:7796-8038): opens on Space (7377), fuzzy filter, verbs with shortcuts drawn dim-right (teaching them), adds, overlay/panel/sidebar toggles, host document verbs, and go-to-node rows that select + frame (7867-7897). Panels reach the reveal intent via Panel: host cmds.
- [t] **Chrome color desc tables** — Doc symbol names stale; no test.
  - evidence: Evolved past the doc's names: kBlComboColors/kBlEditColors no longer exist; landed as ImGuiAppChromeTheme with Combo[]/Edit[] ImGuiAppColorModDesc tables seeded from the composer style (C:\dev\imguix\imguix\imguiapp\imguiapp_nodes.cpp:335-367), applied via AppBlPushStyle/PushAppColorMods (377-397).
- [t] **Status strip NODES split** — No test asserts the strip text.
  - evidence: StatusStripControl::OnUpdate inline loop counts IsLive/IsPromoted; CountMsg 'design %d  live %d  promoted %d' when ShowLive, 'design %d' otherwise (demo.cpp:920-947) — promoted segment gated on the mirror exactly as specified.
- [t] **Status strip COMPOSITION segment** — No test asserts the counts.
  - evidence: MirrorCounts 'L%d W%d S%d C%d' read from the object model app->Layers/Windows/Sidebars/Controls.Size (demo.cpp:949-955), rendered right-side of the strip (demo.cpp:978-979); Layers included, independent of the mirror toggle.
- [t] **Delete duplicate FPS/status surfaces** — Pointer text mentions lifecycle and FPS the strip does not actually show.
  - evidence: rest_buf/FPS cluster gone (no matches in code); main-window 'ImGuiApp Status' header reduced to the one-line pointer 'See Tools > Composer -> status strip...' (demo.cpp:1940-1942); zero FPS surfaces remain in the app layer.
- [t] **Non-ASCII status glyphs** — Design-doc rejection is stale; if headless pixel review matters, glyph coverage rests on the merged-font assumption rather than a test.
  - evidence: Rejection premise superseded: the host merges FontAwesome into the atlas (imguix-demo/demo.cpp:195-197, MergeMode=true) and ICON_FA_* glyphs are used throughout the composer chrome (imguiapp_demo.cpp:696+ passim; nodes.cpp:30 'font merged by the host app') — glyphs render correctly, so the ASCII-only rule was consciously abandoned rather than honored.
- [t] **Near-opaque canvas overlay plates** — No test asserts plate opacity. The F1 shortcuts card uses 0.92 (imguiapp_nodes.cpp:~8217) — a transient overlay, arguably outside the rule.
  - evidence: Gizmo column plate AppThemeNeutral(0.04f, 0.99f) = ~252 alpha (imguiapp_nodes.cpp:~8086-8088); demo canvas health overlay uses SetNextWindowBgAlpha(0.99f) + WindowBg mixed at 0.99 (imguiapp_demo.cpp:1552-1555). No 215-alpha (0.84) plate remains anywhere in imguiapp.
- [t] **Explicit Add entry point in toolbar** — No test clicks the toolbar Add button.
  - evidence: imguiapp_demo.cpp:696-697: ICON_FA_PLUS ' Add' button opens the palette, tooltip 'Add a node (Space / right-click canvas)'; gizmo '+' (imguiapp_nodes.cpp:~7971-7975) and Space palette remain.
- [t] **Toolbar group phase captions** — No test asserts the captions render.
  - evidence: imguiapp_demo.cpp:873-885: caps[4] = {compose, iterate, persist, produce} rendered as a dim TextDisabled row at 0.78x font, x-anchored under each cluster via cap_x[].
- [t] **Bottom panel shared header row** — No test covers the header row; per-tab height memory (also complained about in the doc) not verified.
  - evidence: Shared grammar implemented per tab: Code tab 'Shared tab header grammar: context label left, actions right' with label + Diff/Copy on the right (imguiapp_demo.cpp:1607-1627); Project tab context label (1650-1651); Output tab label left + err/warn/info toggles, filter, Clear right (1712-1734).
- [t] **Output tab severity toggles and search filter** — No test exercises the toggles or filter.
  - evidence: imguiapp_demo.cpp:1717-1733: err/warn/info toggles on AppGraphEditorState OutputShowErr/Warn/Info + OutputFilter.Draw; filtering applied to issues (1745) and log lines (1766-1767); Clear button (1732). Output rows also brush-hover the canvas node (1757).
- [t] **Rule: no dead chrome** — A principle, not a mechanism — no lint/test enforces it globally; the stale 'transport bottom-center' comment (demo:1532) is itself a doc-level dead-chrome remnant.
  - evidence: Rule text lives only in usability-findings-2026-07.md:77-78 (no separate design-principles doc carries it). Code conforms in the touched surfaces: undo/redo/history disabled via BeginDisabled with tooltips stating the enabling condition (imguiapp_demo.cpp:704-726), splitter grips exist only while panels are open (asserted by step26, tests:1975-1988), Generate tooltip explains its state (771-776).
- [t] **Rule: flow order beats category order** — The implementing toolbar is functionally tested (step26) but no test asserts ordering; the rule exists only in the findings doc, not a principles doc.
  - evidence: Rule recorded at usability-findings-2026-07.md:80-81; realized by the flow-ordered toolbar (imguiapp_demo.cpp:6, 552, 691-885) with phase captions naming the walk.
- [t] **Rule: chrome is opaque** — No automated check; the transient F1 help card sits at 0.92 (~235).
  - evidence: Rule at usability-findings-2026-07.md:82; enforced by 0.99-alpha (~252) plates: gizmo column (imguiapp_nodes.cpp:~8086-8088), demo overlay window bg (imguiapp_demo.cpp:1552-1555); no 215-alpha plate remains.

**Parked (deferred by intent)**

- Unified two-rail timeline strip — Not started, as scheduled (S4).
- Timeline scrub corner badge — Not started, as scheduled.
- Zoom % indicator — Exclusion honored, but its rationale ('imnodes has no zoom') is obsolete — the canvas engine now has wheel zoom (imguiapp_canvas.h:61-62,98-102).
- Second canvas header row — One document toolbar child (imguiapp_demo.cpp:689) + the gizmo overlay column (imguiapp_nodes.cpp:8128); no second header row exists.
- Toolbar search field — No search field in the toolbar (imguiapp_demo.cpp:689-885); search lives in the outliner (imguiapp_nodes.cpp:12674) and palettes (7754), consistent with the exclusion until T3.
- Breadcrumb in toolbar — The scope breadcrumb renders only as the canvas overlay (imguiapp_nodes.cpp:5756-5773); the toolbar has none. The status strip carries a SELECTION breadcrumb (demo:957,977), a different concept per §7's own status-bar sp
- Docking framework adoption — Rejection holds: Composer panels are fixed BeginChild columns with hand-rolled splitters (imguiapp_demo.cpp:1508-1600, 1786-1801); no DockBuilder/dockspace use for panels. (step36_dockspace_host_stays_behind_floaters and
- Toasts / notification center — Minor tension: the link toast is a third surface for the same message the status bar shows.
- Chrome theme serialization / marketplace — Confirmed: the Theme section edits only the one built-in ImGuiAppChromeTheme (imguiapp_demo.cpp:1204-1228); nothing serializes it — the layout sidecar omits theme fields (demo.cpp:428-431) and AppGraphChromeTheme reseeds
- Minimap hover callbacks — Absent as designed: CanvasMiniMap(c, size_fraction) takes no callback (C:/dev/imguix/imguix/imguiapp/imguiapp_canvas.h:113); hover inside the map only restyles node fill locally (canvas.cpp:1722-1726).

<details><summary>Shipped-tested (verified, no action)</summary>

- Document toolbar
- Hide/isolate command roads
- Chrome altitude rule
- Latched Code panel toggle
- Document toolbar with health-carrying Generate
- Existing keyboard shortcuts
- MiniMap reimplemented small: rects + view box + click-jump
- Toolbar reordered by authoring loop

</details>

### outliner-inspector  (10/39 shipped-tested; 14 untested, 10 partial, 4 missing, 1 parked)

**Build TODOs — missing**

- [ ] **Section collapse persists per section per kind** — Per-kind keying and persistence unimplemented.
  - evidence: Open state is session-lived window state storage keyed by section str_id, explicitly 'session-lived, shared per panel', with first-section-open-per-frame default (C:\dev\imguix\imguix\imguiapp\imguiapp_nodes.cpp:421-435). Not per node kind, not persisted across sessions.
- [ ] **Unified row grammar with right-click row actions** — Row grammar and row context actions not started.
  - evidence: No right-click Reset-to-default/Copy/Paste on any inspector row (no such menu code in imguiapp_nodes.cpp inspector sections or imguiapp_demo.cpp project inspector); label/value layout is ad-hoc per section (e.g. fixed label_w = 5.5em only in the project inspector, demo.cpp:1156).
- [ ] **Tree-to-canvas SelectNode on submit-order invariant**
  - evidence: Retired: zero ImNodes:: calls remain in product code (only docs mention it); the tree click writes only *sel + g->Selection (AppTreeClick, nodes.cpp:12271-12280); the editor applies selection itself via CanvasSelectNode in its own sync step (nodes.cpp:8437-8439). The fragile submit-order invariant is gone.
- [ ] **Clickable inert Live-app rows via re-derived LiveKey**
  - evidence: Rejection honored via the de-dup route: the inert section was removed entirely and live rows are ordinary selectable outliner rows in the single hierarchy (nodes.cpp:12750-12762); no second LiveKey join exists in the tree.

**Build TODOs — partial**

- [ ] **Canvas node hover -> outliner tint + gutter tick** — The 1px scrollbar-gutter tick for scrolled-out rows is not implemented; only the in-view tint exists. No test.
  - evidence: Canvas hover publishes (imguiapp_nodes.cpp: AppGraphHoverNode(g, hovered_node, Canvas) after canvas hover resolve, ~7097); brushed row tint + left accent bar in outliner (12463-12469); no SetScrollHereY/ScrollToItem in the outliner (auto-scroll correctly absent).
- [ ] **Inspector auto-expands offending section** — No section auto-expand (no SetNextItemOpen driven by issues); intent covered by the pinned issue rows. No test.
  - evidence: Implemented differently: the selected node's issue rows are pinned at the TOP of the inspector (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:4313-4327), 'stated where the fix happens'.
- [ ] **Component-section inspector** — No Identity/Placement sections (name is a bare InputText 4369-4373); Persist/Temp not split as labeled sections; the composed section UI is untested.
  - evidence: Section anatomy landed: AppInspectorSection with icon, name, optional enable checkbox, kebab (C:\dev\imguix\imguix\imguiapp\imguiapp_nodes.cpp:425-487); sections by kind: Fields/Events + Style for Control, Style for Window, Dock+Style for Sidebar, Fields for Struct (4376-4419); Style section-level enable masters per-row Active flags (4162-4169).
- [ ] **Section kebab menu: reset/copy/paste section** — Only Style has a kebab; other sections pass nullptr (4383-4399); Reset clears rather than resetting to defaults; untested.
  - evidence: Landed for the Style section only: kebab popup with Copy section / Paste section / Reset (clear all), session-lived value-typed clipboard StyleClipMods/StyleClipCols (C:\dev\imguix\imguix\imguiapp\imguiapp_nodes.cpp:4170-4192); multi-select variant 'Paste section to all' / 'Clear style on all' (4233-4252).
- [ ] **Multi-select mixed values and section intersection** — No mixed-value dash editing (typing-sets-all), no general section intersection — only the Style section is offered.
  - evidence: Multi-select inspector exists but is Style-only: EditAppNodesInspectorMulti (C:\dev\imguix\imguix\imguiapp\imguiapp_nodes.cpp:4199-4259) with master enable across all selected design nodes and per-node style counts readout.
- [ ] **Project-level inspector on empty selection** — Logging section (WAL level/log-path controls) missing; freshness indicator is not click=Generate; prefab delete missing; untested.
  - evidence: Landed: ShowComposerProjectInspector on empty selection (C:\dev\imguix\imguix\imguiapp\imguiapp_demo.cpp:1152-1246, dispatched 1806-1808) with Document (paths, freshness icon+tooltip, node/link/binding counts, signature), Validation (severity summary + Open Output button), Composer theme (read-write chrome desc rows with Active flags), Prefabs (Stamp).
- [ ] **Quick inspector at the cursor** — No context-menu 'Inspect here', no pinning, no Esc/click-away dismissal, no kind-specific top-section subsetting; untested.
  - evidence: Landed in basic form: N toggles a floating auto-sized inspector clamped beside the primary selection, closable via its X, follows selection (C:\dev\imguix\imguix\imguiapp\imguiapp_nodes.cpp:7375-7376, 8219-8239); palette entry 'View: Quick inspector' (7841). It renders the full EditAppNodeInspector — a true view of the inspector.
- [ ] **Kind filter buttons overflow popover** — The planned overflow POPOVER was not built; the underlying complaint (buttons clipping dead at narrow widths) is addressed by wrapping. No test.
  - evidence: Narrow-width handling exists but as flow-wrap, not a popover: imguiapp_nodes.cpp:~12516-12521 'Flow-wrap: at narrow panel widths the buttons wrap to a second row instead of clipping dead.'
- [ ] **Multi-select intersection editing** — Intersection editing covers the Style section only — fields, events, dock, etc. are not multi-editable. No test.
  - evidence: EditAppNodesInspectorMulti (imguiapp_nodes.cpp:~4197-4219): 'Style (all selected)' section with master enable across all selected design nodes' descs, paste-to-all, clear-all; header declares 'multi-selection: intersection editing (style across all selected)' (imguiapp_nodes.h:693); called from imguiapp_demo.cpp:1812.
- [ ] **Kind icons in inspector section headers** — Icons are drawn in plain ImGuiCol_Text (line ~462), NOT matched to canvas kind-accent colors as planned; headers are per-component (Style/Fields/Events), not per node kind. No test.
  - evidence: AppInspectorSection takes an icon and every section header renders one (imguiapp_nodes.cpp:425-462; call sites ~4164-4399 and imguiapp_demo.cpp:1158-1230, 1821-1825).

**Test-debt TODOs — shipped, untested**

- [t] **Outliner text filter (tree-only)** — No test exercises the outliner filter.
  - evidence: OutlinerFilter (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.h:585); search box + flat filtered results (imguiapp_nodes.cpp:12671-12700). No OutlinerFilter use in the canvas pass, confirming tree-only.
- [t] **Outliner kind-toggle filter buttons** — No test toggles a kind button.
  - evidence: OutlinerKindVis[ImGuiAppNodeKind_COUNT] (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.h:584); AppBlFilterButton row with per-kind counts + tooltips (imguiapp_nodes.cpp:12665-12668) feeding ctx.KindVisible (12679-12680).
- [t] **Inspector binding-row hover -> wire thicken** — Implemented as brighten, not thicken; render untested.
  - evidence: Binding row hover publishes Inspector-source link hover (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:3118-3120); brushed wire renders bright (7027-7032) and its endpoints halo (8119-8126). Channel tested with Inspector source in step25 (tests:1894-1896).
- [t] **Problem-tinted outliner rows** — Underline variant rather than full row tint; no test.
  - evidence: Landed as a severity underline along the row bottom in the same hue as the canvas dot (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:12470-12476, AppSeverityColor shared at 3250).
- [t] **Selection dangle guard** — No test deletes/reloads and asserts the reset.
  - evidence: First step of the cross-view sync after link capture: if (*selected_node_id >= 0 && AppGraphFindNode(g,*selected_node_id)==nullptr) *selected_node_id = -1 (imguiapp_nodes.cpp:8397-8399); tree delete path also resets (*selected_node_id=-1 near nodes.cpp:12681 region).
- [t] **Live-row de-duplication in tree** — No test asserts the single-population outliner.
  - evidence: The inert 'Live app' BulletText section no longer exists (no matches); the outliner is one hierarchy — design roots, a dim band, then live mirror roots as first-class selectable rows (imguiapp_nodes.cpp:12750-12762), hidden when !show_live (12639, 12727).
- [t] **Selection breadcrumb strip segment** — No test (headless composer_breadcrumb_visible covers the SCOPE breadcrumb overlay, a different thing).
  - evidence: Horizon item actually delivered: AppGraphSelectionBreadcrumb (imguiapp_nodes.cpp:8632-8646, decl nodes.h:702) produces 'sel: Parent > Name [design|live|promoted]' via AppGraphParentOf; StatusStrip renders it right-aligned (demo.cpp:957, 977). The tag is a non-canvas surfacing of IsPromoted.
- [t] **Tree row text tint (clickable rows only)** — No test asserts row tinting.
  - evidence: PushStyleColor(ImGuiCol_Text, AppGraphOriginColor(n) or kind color, alpha-faded when not rendering) around the clickable Selectable rows (imguiapp_nodes.cpp:12734-12743); the inert section it warned about no longer exists.
- [t] **Preview relocated into the inspector** — No test renders the Preview section; dead ComposerPanel_Preview/PanelPreview enum entries linger.
  - evidence: imguiapp_demo.cpp:1817-1822: AppInspectorSection('##sec_preview', ICON_FA_PLAY, 'Preview') inside the inspector column calls AppGraphRenderMockPanel(graph, selection, doc->Mirror) (impl at imguiapp_nodes.cpp:~8954). Bottom tab bar has only Code/Project/Output — no Preview tab. Vestigial ComposerPanel_Preview enum remains (demo:302, 316).
- [t] **Tree row context menu parity with canvas node menu** — No test opens the tree row context menu or diffs its items against the canvas menu.
  - evidence: AppTreeContextMenu (imguiapp_nodes.cpp:~12178-12250+): Rename, Duplicate, Delete, Save as prefab, Explode/Collapse fields and PersistData/TempData, live-row Promote to design, hide/isolate — with explicit '(canvas menu parity)' comments (~12197, ~12696) mirroring the canvas ##AppGraphNodeCtx popup (~7563).
- [t] **DESIGN / LIVE band separator in tree** — No test asserts the band renders between design and live sections.
  - evidence: imguiapp_nodes.cpp:~12581-12596: two-pass root render ('two populations must not read as one list'); before the first live root a dim band renders: Spacing + TextDisabled(ICON_FA_EYE ' live mirror') + Separator, gated on ctx.ShowLive.
- [t] **Drag-to-reparent affordance in tree** — No test drives a tree drag-reparent.
  - evidence: Implemented despite the doc leaving it an open gap: imguiapp_nodes.cpp:~12558-12574 — BeginDragDropSource on non-live Control/Field rows (payload 'APPNODE'), BeginDragDropTarget on Window/Sidebar/Struct rows, accepted payload records deferred reparent action (c->Act=5, ActNode/ActTarget).
- [t] **Inspector reset-to-default on rows** — Reset exists only on style/color mod rows (the rows that have a default); no test exercises it.
  - evidence: Per-row right-click '##rowreset' popup with 'Reset to current style value' on style-var rows (imguiapp_nodes.cpp:~3915-3919, reseeds via AppStyleModSeedValue) and color rows (~3950-3954); section kebab offers 'Reset (clear all)' (~4186).
- [t] **W5 quick-inspector (N at cursor)** — Placement is beside the SELECTION, not at the mouse cursor as the doc phrased it. No test.
  - evidence: Shipped: N toggles QuickInspector (imguiapp_nodes.cpp:~7375-7376, 'Blender N-panel'), floating self-sized 'Quick inspect###quick_insp' window rendered beside the primary selection (~8164-8177), palette entry 'View: Quick inspector' (~7789), listed in F1 help (~8207).

**Parked (deferred by intent)**

- Hover auto-scroll in outliner — The promised substitute (1px scrollbar-gutter tick for out-of-view rows) is also absent.

<details><summary>Shipped-tested (verified, no action)</summary>

- UI coherence program (S1)
- Brushing hover sync across panels
- Ambient problem marks from validation cache
- Two-way selection sync canvas<->tree
- Outliner with filter buttons
- Per-node inspector column
- Window-level shared selection id
- Canvas-to-tree selection read-back
- Tree de-ImNodes (retire submit-order invariant)
- Tree/canvas hover reciprocity

</details>

### persistence  (9/17 shipped-tested; 2 untested, 2 partial, 3 missing, 1 parked)

**Build TODOs — missing**

- [ ] **Round-trip tests for events** — The tests themselves — the entire planned item.
  - evidence: The save/load code path for events exists (imguiapp_nodes.cpp save writes 'Event=%d,%d,%s,...' per ImGuiAppEventDesc and load parses it back; duplicate deep-copies n->Events) but no test round-trips it: step15_graph_persist_roundtrip (tests/imguiapp_nodes_tests.cpp:734-834) covers links/bindings/style mods only, step15b undo (840) covers add/style/rename, and no test saves/loads/undoes/copies a node carrying Events.
- [ ] **Prefabs on disk** — Serialization of the registry and the starter idiom library — the whole planned item.
  - evidence: In-memory registry is shipped and used: AppGraphSavePrefab/PrefabCount/PrefabName/InstantiatePrefab (imguiapp_nodes.h:938-944, imguiapp_nodes.cpp:11735+), canvas menu + inspector stamping (imguiapp_demo.cpp:1230-1327). But Prefabs live in ImGuiAppEditorState (nodes.h:595 within _Ed, which is 'opaque to reflection/serialization' per nodes.h:627) — SaveAppGraph writes no prefab records, and no starter library exists on disk.
- [ ] **Save-writes-keys-but-Load-unchanged persistence** — Residual echo of the anti-pattern in a different spot: authored Dock/Init placement fields are written by NO key at all (see format-v2 finding) — not the rejected write-only failure, but a save-coverage hole
  - evidence: Correctly absent: AppGraphDeserialize parses every additive key back (nodes.cpp:10650-10696); step15 proves the round-trip; legacy Load* untouched (1049-1148)

**Build TODOs — partial**

- [ ] **Save/Load/Diff grouped after primary action** — Order is Save-then-Generate (persist before produce), not 'immediately after Generate' as §7 specifies; Diff sits under the Generate chevron instead of the file group.
  - evidence: File verbs are grouped: Save + save-family popup (Load) (C:/dev/imguix/imguix/imguiapp/imguiapp_demo.cpp:746-758); Diff lives in the generate-family popup (783-787).
- [ ] **SaveAppGraph/LoadAppGraph text format v2** — No Init=/Dock= records exist: authored HasInitialPlacement/InitialPos/InitialSize/DockDir/DockSize are inspector-editable (nodes.cpp:4040-4046) and consumed by codegen (10586) but never serialized — authored dock/placement config is lost across Save/Load
  - evidence: SaveAppGraph nodes.cpp:10526-10539; AppGraphSerialize/AppEmitNodeRecord 10447-10524 write [Graph]/[Node] with Id/Kind/Name/Pos/Port=/Link=/Bind=/Persist=/Temp=/Command=/Style=/Color=/Event=/Place=. step15 round-trips a mixed-kind graph

**Test-debt TODOs — shipped, untested**

- [t] **Layout persistence sidecar ini** — Last-active-tab is NOT persisted (claim says it is); tests only delete the sidecar for isolation (tests\imguiapp_nodes_tests.cpp:1933, tests\imguiapp_headless_verify.cpp:99) — no save/load roundtrip test.
  - evidence: C:\dev\imguix\imguix\imguiapp\imguiapp_demo.cpp:357-436: kComposerLayoutPath='imguix_composer_layout.ini', ComposerLayoutLoad (380-412) + hash-gated ComposerLayoutSaveIfChanged (415-436), loaded in OnInitialize (474). Persists TreeW/InspW/CodeH/ShowLive/TreeOpen/InspOpen/Snap/OvGrid/OvBands/OvFrames/OvMinimap/Zoom.
- [t] **Legacy raw-link endpoints dropped on migration** — step15's legacy file contains zero links, so the drop path is never actually exercised by a test with stale ATTR_DRAFT_BASE-era endpoints
  - evidence: Resolvable-or-drop sweep after load: nodes.cpp:10706-10720 drops any link whose endpoint fails AppGraphFindPort, cascading binding cleanup — load stays total and self-consistent

**Parked (deferred by intent)**

- Framework-level imgui settings handler — Doc's symbol name AppWindowLayerSettingsHandler_* is stale (now AppDisplayLayerSettingsHandler_*).

<details><summary>Shipped-tested (verified, no action)</summary>

- Zero-per-app-code state snapshot/restore
- Restore-and-replay byte-for-byte contract suite
- Scope-local placement records
- NextId serialized across save/load
- Load actually parses the new keys (round-trip fix)
- Legacy [Draft] section migration on load
- Bidirectional 3/4-field Link= parsing
- Four legacy persistence functions byte-identical
- step15_graph_persist_roundtrip test

</details>

### live-mirror  (8/16 shipped-tested; 3 untested, 0 partial, 4 missing, 1 parked)

**Build TODOs — missing**

- [ ] **Transport overlay (App-time freeze + scrub)** — Reintroduce a transport UI against the one-app mirror (or update composer-workbench-design.md §2/§4.1 — the landed-inventory claim is now false).
  - evidence: Removed: imguiapp submodule commit 58854ef (2026-07-03, 'one-app composer') deleted ##canvas_transport, ToggleScrub/ScrubIdx and MirrorHistory from imguiapp_demo.cpp (verified via git show). Current tree has no transport code; the stale comment at imguiapp_demo.cpp:1532 still says 'transport bottom-center' but only the health strip renders. Core scrub machinery (AppStateSnapshot/Restore) survives in imguiapp.cpp:1082-1261.
- [ ] **IsPromoted computed, never rendered**
  - evidence: Defect fixed: IsPromoted renders as the origin-dot ring (nodes.cpp:6595 CanvasNextNodeOriginDot(..., n->IsPromoted && !n->IsLive)), the strip 'promoted %d' count (demo.cpp:935-942), the breadcrumb [promoted] tag (nodes.cpp:8639), and drives NumUnbuilt staleness (demo.cpp:521-524).
- [ ] **Destructive Mirror-live OFF strip (status quo)**
  - evidence: The rejected destructive behavior is gone: the demo contains no AppGraphRemoveNode strip loop; Mirror-OFF just flips doc->ShowLive (demo.cpp:623-626) while BuildAppLiveGraph keeps running (488-491), and the Theme E view filter hides live nodes everywhere (~30 show_live guards in nodes.cpp). Replacement verified by headless position-survival test (imguiapp_headless_verify.cpp:460-483).
- [ ] **App-time transport gated on ShowLive** — The claimed fix is stale: the transport was removed (consistent with live-debugging being parked), so the ShowLive gate cannot be verified. Either resurrect a gated transport or delete the stale bottom-center comment at imguiapp_demo.cpp:1532.
  - evidence: No transport UI exists anywhere in app-layer code. imguiapp_demo.cpp:1532 comment still says 'transport bottom-center' but the block renders only the health readout (1534-1581). No play/pause/scrub widget, no ##transport, no frame-cursor UI in imguiapp_nodes.cpp/imguiapp_demo.cpp. The underlying snapshot API survives (AppStateSnapshot/AppStateRestore, imguiapp.cpp:1082/1250, exercised by imguiapp_core_tests.cpp:305-425) and imguiapp.h:444-446 notes time scrub is disabled for the host.

**Test-debt TODOs — shipped, untested**

- [t] **Live-mirror style write-back** — No test flips a live Active flag and asserts the running item restyles.
  - evidence: Landed: EditAppNodeInspectorEx signature notes '+ live style write-back (see workbench §3.5)' (C:\dev\imguix\imguix\imguiapp\imguiapp_nodes.h:692); live nodes' Style (live) section renders ENABLED Active checkboxes bound directly to the running item's StyleMods/ColorMods (imguiapp_nodes.cpp:4308-4358, tooltip 'live toggle: pushed (or not) starting next frame'); push happens in ImGuiAppItemBase::OnStylePush (imguiapp.cpp:639-645); everything else stays read-only (4299-4306).
- [t] **Design-to-live 'promoted' badge** — No test asserts IsPromoted or the badge; grep for IsPromoted/promoted in tests/ returns nothing
  - evidence: IsPromoted set in BuildAppLiveGraph by matching emitted type id to live nodes (nodes.cpp:11888-11894); rendered as origin dot (6595 CanvasNextNodeOriginDot), origin color OriginPromoted (3367), breadcrumb tag 'promoted' (8470), status-strip count 'promoted N' (demo:900,942)
- [t] **Hidden-endpoint link filter** — No test asserts hidden-endpoint links are not drawn.
  - evidence: Link loop is inlined in ShowAppGraphEditor with the documented rationale '(Inlined: DrawAppNodeLinks can't resolve owners)' (nodes.cpp:7071 region) and skips any link whose either endpoint owner IsLive when !show_live (skip at the oa/ob owner check, formerly 6876); DrawAppNodeLinks remains only as an unused-by-the-editor public helper (nodes.cpp:925).

**Parked (deferred by intent)**

- Live debugging / mirror reconciliation — Composition-ID-gated reconciliation of BuildAppLiveGraph itself not implemented — the demo just re-mirrors every update (imguiapp_demo.cpp:486-490); deliberately parked per the project spine.

<details><summary>Shipped-tested (verified, no action)</summary>

- Live mirror eye toggle
- Two reflect-free virtuals exposing runtime deps
- BuildAppLiveGraph read-only live mirror
- LiveKey position preservation
- BuildAppLiveGraph is model-only
- Reconcile-before-report frame ordering
- Non-destructive mirror hide (show_live)
- Hidden-node full submission skip + read-back guard

</details>

### events  (5/7 shipped-tested; 1 untested, 0 partial, 0 missing, 1 parked)

**Test-debt TODOs — shipped, untested**

- [t] **Machine-checked invariant: event writers are confluent** — No test constructs conflicting writers and asserts the confluence issue; only the expr-error validate path is tested (step21).
  - evidence: AppGraphValidate check (d): two SetField events on the same persist field, or an event plus a data-edge binding writing the same field, are flagged as order-dependent (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:8687-8722).

**Parked (deferred by intent)**

- Edge helpers in codegen (named vs raw) — None — the tabling is faithfully reflected in code; revisit items (style toggle / ImAppEdge<T>) correctly unbuilt.

<details><summary>Shipped-tested (verified, no action)</summary>

- Event expression checking
- Authored events
- Derived-event idiom with named edge vocabulary
- Authorable events generating guarded blocks
- Link lifecycle events plus detach-drag and snap-create

</details>

### av  (16/27 shipped-tested; 5 untested, 2 partial, 0 missing, 4 parked)

**Build TODOs — partial**

- [ ] **Flight recorder ring + assert auto-dump** — Assert wiring is MISSING: ImGuiAppAssertFail (imguiapp.cpp:99-123) writes the WAL and exits but never calls AppRecordDumpRing — no ring registry exists for it to find one. Also zero callers of AppRecordBeginRing/AppRecordDumpRing outside imguiapp_av.cpp and zero tests.
  - evidence: Ring implemented: ImGuiAppRingConfig defaults 10s/256MB/Fps=0 (imguiapp_av.h:137-144); AppRecordBeginRing (av.cpp:1155-1173, QOI-compressed entries + meta records, provider stays closed until dump); eviction on either bound (:727-740); Fps>0 subsample opt-out (:843,:907-917); AppRecordDumpRing with -2/-3 path suffixes, reason in WAL + sentinel Frame stream marker, survivor chain recompute, dump Digest (:1175-1282).
- [ ] **Headless rendering modes** — Null mode is enum-only: no backend implements it — vulkan asserts against it (win32_vulkan.cpp:1243) and win32-opengl3 rejects all headless modes (win32_opengl3.cpp:322-326). The nodes suite still uses the test engine's own null backend instead.
  - evidence: ImGuiAppHeadlessMode None/Null/Offscreen enum + ImGuiAppConfig.Headless at imguiapp_config.h:42-58; vulkan implements Offscreen as a render-target image (imguiapp_impl_win32_vulkan.cpp:54,:1124,:1242) with present skipped via ImGuiAppFrameFlags_NoPresent (:1071); Offscreen exercised and required by imguix-headless-verify (imguiapp_headless_verify.cpp:823,:833 — non-Offscreen exits 77 SKIP; test passed).

**Test-debt TODOs — shipped, untested**

- [t] **Fixed-size recording, resize aborts** — The abort branch has no test — no test resizes mid-recording.
  - evidence: Width/Height 0 = first frame's size locked (imguiapp_av.cpp:885-889); mismatch aborts with WAL line 'av: abort recording, frame %dx%d != locked %dx%d' and sets Active=false (imguiapp_av.cpp:890-896). Size-lock path runs in the passing headless-verify take.
- [t] **QOI sequence encoder backend** — No test exercises the QOI encoder or its extractor — headless-verify always takes the libav path because it only builds when the ffmpeg SDK is present (tests/CMakeLists.txt:55).
  - evidence: imguiapp_impl_qoi.cpp: <dir>/NNNNNN.qoi frames (:110) + index.tsv carrying FrameID.TimeSec per frame (:83,:119-122), ImGuiApp_ImplQoi_CreateEncoder (:150, SupportsRealtimePts=true :154), ImGuiApp_ImplQoi_ExtractEmbeddedMeta (:209). Ships by default (imguix/CMakeLists.txt:20-21, unconditional). Harness fallback when libav absent (testharness.cpp:175-182); demo fallback (imguix-demo/demo.cpp:121-124).
- [t] **Media Foundation encoder backend** — No test or in-repo caller exercises the MF encoder.
  - evidence: Implemented ahead of the claimed P3 horizon: imguiapp_impl_mediafoundation.cpp — IMFSinkWriter H.264 (:2,:40), the only TU linking mfplat/mfreadwrite (:5, CMake links mfplat/mfreadwrite/mfuuid WIN32-only at imguix/CMakeLists.txt:64-70), non-WIN32 stub returns null (:321-328), ImGuiApp_ImplMediaFoundation_CreateEncoder (:308), SupportsRealtimePts=false (:312). Never a silent default: harness default is libav-else-QOI only (testharness.cpp:167-182); demo never selects MF.
- [t] **Snapshot-state frame blob (scrub = time travel)** — No test: harness run reports snapshots=0. Deviation from doc: snapshot travels as a typed StateSnapshot record, not as the Frame record's user blob — the user blob stays free for AppRecordSetFrameData (av.cpp:1090-1092).
  - evidence: AppRecordSnapshotState at imguiapp_av.cpp:1085-1094; AvBuildStateSnapshotRecord serializes StorageEntries slots (id+size table then concatenated bytes, matching snapshot layout) with composition id + frame_index at :357-393; AppAVMetaReadStateSnapshot parses it back (:1531). Demo calls it per take (imguix-demo/demo.cpp:136).
- [t] **Meta stream readback for reproduction** — No test or in-repo caller performs the full reproduction (restore snapshot then AppInputReplay from a recording).
  - evidence: Implemented ahead of the claimed P3 horizon: AppAVMetaReadInputLog at imguiapp_av.cpp:1473 and AppAVMetaReadStateSnapshot at :1531; convenience wrapper ImGuiApp_ImplLibav_ReadEmbeddedInputLog extracts-then-parses (imguiapp_impl_libav.cpp:543-551).

**Parked (deferred by intent)**

- MF true VFR path — Rejection is encoded in the implementation: 'True VFR through IMFSinkWriter is NOT achievable (measured): the H.264 path requires MF_MT_FRAME_RATE ... resamples per-sample timestamps to CFR (90 VFR samples -> 208 CFR fra
- Raw-io replay side — Not started, consistent with the claim.
- Audio capture — No capture code anywhere; ImGuiAppAVMetaRecordType_AudioPcm reserved in the v1 enum with 'no producer yet' (imguiapp_av.h:117), exactly matching the non-goal + format-reservation claim.
- Live streaming + GPU-side encode — Not started, consistent with the 'not in P1-P3' claim.

<details><summary>Shipped-tested (verified, no action)</summary>

- Encoder provider seam (vtable)
- Video timing modes + honesty contract
- libav linked encoder backend
- Embedded meta stream, no side files
- v1 meta record types incl. reserved audio
- Three-layer input recording
- Identity record + replay mismatch taxonomy
- Five integrity layers + avtool
- Recorder + four-phase frame pipeline
- Encoder thread + queue policy
- Encode-every-frame guarantees
- Pixel-strip chunking + crash honesty
- Frozen strip block format + extractors
- CaptureFrame backend readback hook
- ImGuiAppTestHarness entry point
- Reproduce/Witness postures + benchmark artifacts

</details>

### other  (3/10 shipped-tested; 2 untested, 5 partial, 0 missing, 0 parked)

**Build TODOs — partial**

- [ ] **Visual grammar single source of truth** — Grammar is spread across style struct + helpers; not yet one enforced table in code.
  - evidence: Doc table exists (composer-ui-design.md §5 at line 157). Code already has a theme-derived chrome style struct: ImGuiAppComposerStyle / AppComposerStyleFromTheme / AppComposerGetStyle (C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.h:775-836; imguiapp_nodes.cpp:254-315) plus shared encodings AppKindColor and AppSeverityColor ('same hue in canvas, outliner, and inspector', 3250).
- [ ] **Delivery slice plan S1-S5** — S2 (notes R1, align/distribute R3) not started; S4 (T5 timeline, T3 filter) and S5 (R2/R5/R7) not started; T4's diff-hunk tagging still open.
  - evidence: S1 (T1+T2+T6) verified landed in code: hover channel + halos (imguiapp_nodes.cpp:3156-3183,8094-8126), status hints (8179-8194), ambient severity marks (8160-8174,12470-12476,4313-4327); §7 chrome also landed (demo toolbar/gizmo/status). S3's core (T4 source map + code-view ties) landed early (nodes.h:886-899, demo:1031-1084). up-next.md:31-32 confirms 'S1 landed; next slice S2'.
- [ ] **Undo/redo rail** — No rail/notch visualization; history navigation is a popup list. HistoryGoto jump path untested.
  - evidence: The gen-1 notched rail no longer exists; replaced by toolbar undo/redo buttons with named tooltips + a history dropdown listing every step with its label and cursor caret (C:\dev\imguix\imguix\imguiapp\imguiapp_demo.cpp:700-741, AppGraphHistoryGoto jump). Undo core tested (step15b, tests\imguiapp_nodes_tests.cpp:840-872).
- [ ] **Time travel** — No editor UI reaches the time-travel core anymore; landed-inventory listing is only true at the framework level.
  - evidence: Core mechanism shipped-tested: ImGuiAppStateHistory ring + AppStateSnapshot/AppStateRestore (C:\dev\imguix\imguix\imguiapp\imguiapp.cpp:1061-1261), tests Test_contract_time_travel and Test_contract_input_replay (C:\dev\imguix\tests\imguiapp_core_tests.cpp:296-368, 397-428). But the Composer-facing surface (transport scrubber) was removed in commit 58854ef.
- [ ] **Delivery slice ordering W1-W5** — W4 slice not started; several landed W2/W3/W5 items are partial and untested; slice discipline diverged from the doc.
  - evidence: W1 (Trust) is fully landed: sidecar persistence (demo.cpp:357-436), named undo + coverage (nodes.cpp:11262-11447, step15b), keyboard reach minus Ctrl+P (nodes.cpp:7249-7497). But 'W1 first and alone' was not observed: W2 items (palette 7796-8038, split-buttons demo 746-788), W3 items (component sections 425-487/4152-4421, project inspector 1152-1246, live write-back 4308-4358) and a W5 item (quick inspector 8219-8239) landed too, while W4 (overlay quietness/status zones/run tint) is untouched and the transport regressed out.

**Test-debt TODOs — shipped, untested**

- [t] **Layer vocabulary aligned to module-interop loop** — Purely lexical feature; no test asserts strings (reasonable), but nothing verifies docs/nodes/captions stay in sync.
  - evidence: Role strings across surfaces: imguiapp_nodes.cpp:3288 Task = 'ingests module status & updates app state', 3290 Status = 'publishes the app's own status'; scope captions ~5026-5066 ('state collection: module status ingest + control updates in dependency order', 'presentation only ... mutating nothing'); drilled-scope hints ~6879-6883 ('status ingest & control updates', 'publishes the app's status'); docs updated (big-idea.md, up-next.md).
- [t] **Prefab registry** — Zero test coverage (no 'prefab' hits in tests/); no delete verb in the inspector section.
  - evidence: C:\dev\imguix\imguix\imguiapp\imguiapp_nodes.cpp:11560-11595 (AppGraphSavePrefab/PrefabCount/PrefabName/InstantiatePrefab, name-replace semantics); context menu 'Save as prefab' 7630-7635 and 12146-12150; add-palette Prefabs submenu 7770-7781; project-inspector Prefabs section with Stamp (imguiapp_demo.cpp:1230-1244) — the §5.3 'graduate to inspector' plan already partially landed.

<details><summary>Shipped-tested (verified, no action)</summary>

- Rename Metrics/Debugger -> Composer
- Undo coverage audit
- Named undo operations

</details>


---

## In-flight markers (TODO/FIXME/interim comments + recent commit subjects)

- [ ] **Frame pipeline phase contract** — imguiapp_core_tests.cpp Contracts 1-4 assert every frame runs Update-then-Command-then-Render (UCR tape), edge events fire exactly once per input pulse, a command latched in OnUpdate dispatches the SAME frame (UCX), and two controls emitting the same command dispatch once (set semantics).
- [ ] **Control lifecycle and storage** — imguiapp_core_tests.cpp Contracts 5, 6, 10 assert pop frees instance data and storage entries so re-push gets fresh state, RenderApp mutates no persistent bytes or composition ID, and instanced controls (same type, distinct instance ids) hold independent state with the bare type id left free.
- [ ] **Determinism: time travel + input replay** — imguiapp_core_tests.cpp Contracts 7-9 assert snapshot/restore reproduces a 10-frame trajectory byte-for-byte, a recorded input log re-executes a run to an identical final state with zero divergence, and a control consulting a hidden global is caught with the exact diverging frame named.
- [ ] **Dependency routing and update DAG** — imguiapp_core_tests.cpp Contracts 11-14 assert unrouted deps resolve to own-instance then singleton fallback, explicit ImGuiAppDataBinding overrides, updates run producer-before-consumer even when window hosting order inverts it, producer pop+re-push rebinds the consumer's cached pointer, and Optional bindings resolve to null / rebind live / rewire through the erased base.
- [ ] **Codegen drift + compile/behavior proof** — imguiapp_codegen_proof.cpp builds a canonical graph, byte-diffs its emitted C++ against checked-in tests/data/imguix_contract_generated.h (drift gate, writes .h.new on mismatch), then #includes and RUNS that header: the changed(^) event mutates PersistData and the rising event dispatches a real AppCommand.
- [ ] **Reflection field helpers + node bodies** — imguiapp_nodes_tests.cpp steps 1-3 assert VisitAppFields enumerates member names, EditAppField dispatches type-correct widgets that mutate bound values, fields render inside a canvas node, and an inline edit in a node reaches the bound struct.
- [ ] **Draft authoring + graph persistence** — imguiapp_nodes_tests.cpp steps 4, 5, 8, 15, 28 assert draft fields add/remove through the inspector, a dragged link round-trips save/load, multi-draft files persist with links, a mixed-kind graph with typed link + binding survives Save/Load, legacy [Draft] files still ingest, and a fresh Composer document saves foundation layers only (no seeded window/control).
- [ ] **Graph topology, identity, typed links** — imguiapp_nodes_tests.cpp steps 10-13 assert every node kind renders with mandatory ports (no empty-body assert), AppGraphCanLink accepts/rejects by type and a real pin drag folds a Data edge, port ids survive deleting an unrelated node (stable identity vs base+index renumbering), and cycles are refused with no topological order.
- [ ] **Whole-graph codegen from the UI** — imguiapp_nodes_tests.cpp steps 6, 7, 14, 16, 27 assert Generate emits the Data/TempData/control scaffold, write-to-file lands verified contents, dependencies derive topological push order plus binding assignment lines, control commands generate the client enum/switch (orphan commands excluded, fixed layers singleton), and GenerateAppGraphCodeEx records per-node line-range source maps identical in text to the plain emission.
- [ ] **C++ struct import round-trip** — imguiapp_nodes_tests.cpp step18_roundtrip_import_structs asserts parsing a C++ struct block yields a Struct node with correctly mapped field types including Vec2, Bool, and char-array String with ArraySize preserved.
- [ ] **Canvas engine model-space invariants** — imguiapp_nodes_tests.cpp canvas_c1-c3 plus headless canvas_solid_drag_no_overlap assert same-frame measurement in model units with zoom anchor fixed (docs/phase-coherence.md as build contract), drag-through-FSM moves model pos by pixels/zoom, pins anchor to node edges and pin-to-pin drags latch WireCreated (out,in), fit-all frames content, double-click is reported not interpreted, title edit hands state back, and solid node drags clamp to contact without freezing.
- [ ] **Scope navigation + interior semantics** — imguiapp_nodes_tests.cpp steps 18 (tab nav), 19, 19b, 37-46 assert Tab/Esc drill in/out with reselection, scoped adds adopt into the scope owner at the click point with root GridPos derived at root altitude, per-branch camera memory restores pan/zoom, density flip folds bodies at root, scope walls publish only when drilled, interior placement/tidy/nudge/duplicate never disturb the root layout, struct interiors offer Field only, and live-mirror interiors are read-only.
- [ ] **Layer column + containment chrome layout** — imguiapp_nodes_tests.cpp steps 17, 29-36 assert layer nodes resolve into a non-overlapping fixed-X vertical stack, containment reads vertically (ChildIn bottom edge / ChildOut top edge) while data pins stay horizontal, windows/sidebars stack in execution order under the Display section, layer drags clamp against neighbors (or push-and-stick clusters that return home), containment fans trunk into group frames, group title drags never move the seated owner, frames stay zoom-coherent without interpenetration, and the dockspace host stays behind floaters on focus.
- [ ] **Outliner overlays + hover sync + issue cache** — imguiapp_nodes_tests.cpp steps 22-25 assert a real click on the row eye overlay toggles Hidden and is consumed (not also selecting), hide/show and isolate/show-all retain every node's canvas position, hover brushing propagates writer-to-reader with one-frame skew and expiry, and validation recomputes only on model change with severity marks ambient.
- [ ] **Event expression checking** — imguiapp_nodes_tests.cpp step21 asserts AppEventExprCheck parses the expression grammar, type-checks field refs (temp/last_temp/data/dep params, struct members) against effective field lists and the result type against the destination, and bad exprs surface as severity-2 issues on the node.
- [ ] **Live mirror foundation + live codegen** — imguiapp_nodes_tests.cpp step20/step44 plus headless composer_live_codegen and composer_window_group_members assert a real applayer app mirrors onto the four authored layer nodes (never phase twins) with live nodes and containment edges per window/control, live interiors reject edits, type-erased reflection sees real aggregate members (ImVector element types included, no hand manifest), and live codegen spans every live node.
- [ ] **Composer chrome end-to-end** — imguiapp_nodes_tests.cpp step26 plus headless composer_code_toggle/composer_breadcrumb_visible drive the REAL demo (ShowAppLayerDemo) and assert the Code toggle reveals the panel, the Live eye latches, inspector splitters appear, and the scope breadcrumb window is active when drilled; steps 9 and 15b cover inline title rename commit and named undo history walking add/style/rename steps back.
- [ ] **Headless on-camera verification harness** — imguiapp_headless_verify.cpp (the default verification path per CMakeLists) runs the Composer on an offscreen GPU with a recorded mp4 whose integrity ladder VerifyRecording proves from its own pixels, covering save roundtrip, trunk router endings (S-curve, corner+under-swing, far-side) asserted from graph-derived positions, display-layer Open=false suppressing Begin/OnRender, and scope-interior wall layout held on camera.
- [ ] **WAL crash forensics (infrastructure only)** — imguiapp_core_tests.cpp and imguix_tests_main.cpp write-ahead every test/frame stage so a crash's last marker names the in-flight operation; the WAL is used as infrastructure but its own behavior (rotation, level filtering, assert capture) has no direct assertions.
- [ ] **AV backend direct coverage** — imguiapp_av.cpp is exercised only implicitly as recording infrastructure in imguiapp_headless_verify.cpp (and skipped entirely when the ffmpeg SDK is absent, exit 77); no test directly asserts encode/decode/frame-extraction APIs, error paths, or format handling.
- [ ] **QOI codec** — imguiapp_qoi.cpp has zero test references anywhere under tests/ -- no round-trip, malformed-input, or size-limit assertions.
- [ ] **Status layer** — ImGuiAppStatusLayer is deliberately excluded from the core PushPipeline (imguiapp_core_tests.cpp header: 'unconditionally submits a real ImGui window') and no other test pushes it, so its readout content and fixed-metric-column behavior are unasserted.
- [ ] **Theme/DPI invariants** — All runners pin DpiAware=false at fixed 1600x1000 (imguix_tests_main.cpp) so the project invariant of theme-derived colors, em-sized chrome, and DPI-invariant canvas model units has no automated assertions across scale factors or themes.
- [ ] **Undo/redo beyond named history** — step15b_undo_named_history only asserts checkpoint naming and a backward walk; there is no redo test, no history-depth/cap test, and no coalescing-under-drag test.
- [ ] **ImStructTable** — The imguix/ImStructTable component is modified in the working tree per git status but nothing under tests/ references it -- no registration in imguix_tests_main.cpp or either standalone runner.
- [ ] **Scope-interior test/pixel-review sweep** — Active uncommitted session hardening scope interiors (Begin/End walls, order strip, drilled camera) via headless steps 37-40: commits d592b16/a221322/d9e48fb/67d9752/25b9015 plus modified tests/imguiapp_nodes_tests.cpp, tests/imguix_tests_main.cpp, imguix/imguix_demo.cpp and dirty imguix/imguiapp submodule; design at docs/scope-interior-design.md (Rules B-D).
- [ ] **Sequence-reorder drag (write path of sequence order)** — Reading execution order shipped (order strip + title ordinals) but writing it has not: chip rects publish per frame explicitly so 'the coming sequence-reorder drag rides them' (imguiapp_nodes.cpp:5525, docs/scope-interior-design.md:74, docs/up-next.md 'Now' lines 34-37), while docs/feature-complete-roadmap.md:87 ('Later') defers the chip-drag + click-nudge, so infra is landed and the interaction is pending.
- [ ] **Inspector dead writes on exploded Draft field lists** — Acknowledged residual from the scope-composition correctness push, listed as 'Remaining from the same sweep, NOT yet fixed' — inspector edits to inline Draft field lists are dead writes while the list is exploded into field nodes and need routing to those nodes (docs/feature-complete-roadmap.md:54-56, section 3 'Now (in flight)').
- [ ] **ScopeCams leak for deleted scopes** — Second acknowledged residual of the same sweep: ScopeCams entries of deleted scopes linger (memory only, ids never reused) per docs/feature-complete-roadmap.md:57, section 3 'Now (in flight)'.
- [ ] **Interaction-family bug sweeps (queued in the in-flight section)** — docs/feature-complete-roadmap.md:58-65 (section 3 'Now (in flight)') enumerates the next equal-priority sweeps: undo/redo road coverage, outliner drag-reparent + eye/hide, wire ops (detach/retarget/bindings), save/load byte-compare fidelity, phase-coherence audit refresh, and data-dependency adherence validation for generated + runtime code.
- [ ] **UI coherence program slice S2** — docs/up-next.md 'Now' (lines 29-32) states S1 (brushing/linking, status hints, problem marks) landed and the next slice S2 is annotation frames + align/distribute, per composer-ui-design.md.
- [ ] **Scope-local tidy polish** — docs/up-next.md 'Now' (lines 38-39): per-scope sequence layout (arrange members left-to-right in execution order on demand) and remembered per-scope camera — scope-local placement/camera groundwork already visible in commits 25b9015 and 8cd70cc but the tidy arrangement is outstanding.
- [ ] **Window-hosted control runtime (PushWindowControl stub)** — Window-hosted controls codegen PushAppControl plus a '// TODO: PushWindowControl (runtime stub)' because the runtime call does not exist yet (docs/node-editor-upgrade-design.md:259 and :410; surfacing of that buried TODO is itself flagged as issue 21 in docs/metrics-debugger-coherence-design.md:57), and the doc's imguiapp.h:625-650 anchor has drifted — that range now holds ImGuiAppVecSpelling, so the stub location needs re-verification against commit bb2394b 'hosted-only controls'.
- [ ] **AV input replay side** — Recording of raw io frames + state-hash divergence detection is designed/landed but the replay side (feeding AddMousePosEvent et al. and re-rendering) is explicitly 'a future phase' (docs/av-design.md:226-229, 'Input: source events by default' section), with render-free replay only via the opt-in TempData log.
- [ ] **C++ import of full control skeletons** — Acknowledged asymmetry: structs already round-trip from C++ back into nodes but controls (fields, deps, events, commands) do not — the inverse of AppEmitControlWithDeps is listed in docs/feature-complete-roadmap.md:75-77 (section 4 'Next') and docs/up-next.md:45-46 ('Soon').
- [ ] **Command payloads + edit-intent bus debt** — Commands are bare enums 'interim by intent' with arguments belonging in a queue, and the doc-control's edit-intent escape hatch is 'acknowledged tech debt' to fold into the command pipeline once payloads exist (docs/up-next.md:63-64 and :67-68, 'Horizon'); roadmap echoes it at docs/feature-complete-roadmap.md:84.
- [ ] **Verified feature matrix audit** — docs/feature-complete-roadmap.md:90-94 (section 6) says the doc-claims-vs-code audit is 'in progress at time of writing' and the per-area shipped/untested/missing matrix is 'Pending workflow completion'.
