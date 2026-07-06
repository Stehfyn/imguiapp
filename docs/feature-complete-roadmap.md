# Feature-Complete Roadmap

Living document (like [up-next.md](up-next.md); history in git). Written 2026-07-04 against the
milestone stated that day; the verified feature matrix section is filled by the code-verified
audit as it lands. This document says WHAT remains and in what order; design belongs to the
per-feature documents.

## 1. The bar (stated 2026-07-04)

**The entirety of ImGuiAppComposer + ImGuiApp**: the Composer fully capable of live
self-introspection, re-composing itself, bootstrapping itself — at the **round-trip-its-own-model**
tier: the Composer saves/loads/regenerates its OWN composition, and the emitted code compiles into
the same app. Lean user flows to maintain, update, generate, and wire new additions into an
existing ImGuiApp or a new one.

Decisions fixing the shape of the work:

- **Live mirror is THE ingest** for existing apps. Mirror surfaces are *read-only but never lie*:
  every mutation is blocked with an explanatory notice; Promote is the authoring bridge.
- **Generated files are owned** by codegen — whole-file overwrite is always safe; user code lives
  in separate files referencing generated types.
- **Round-trip bar**: save/load full fidelity, undo covering every mutation road, copy/paste
  subtree fidelity (including into a drilled scope), C++ import of full control skeletons.
- **Sequence-order editing (chip drag)**: deferred.
- **Bug-class priorities, equal rank**: scope-unaware surfaces; phase-coherence violations
  (docs/phase-coherence.md); data-dependency-declaration adherence (code uses data/temp/last_temp
  exactly as declared). Standing rule: adversarial sweeps per interaction family, fix what falls
  out; delete unhelpful narrative comments on contact.

## 2. Definition of done, as user flows

Each flow below must run end-to-end with no lying surface (nothing silently vanishes, no altitude
leaks, refusals state why). These are the acceptance tests of "feature complete":

1. **New app**: scaffold foundation → compose windows/sidebars/controls/structs → wire deps →
   author events/commands → Generate → emitted files compile and run standalone.
2. **Maintain an existing app**: run it with the Composer attached → mirror shows the truth →
   drill any scope read-only → Promote what needs authoring → edit → Generate → rebuild → mirror
   confirms (promoted marks reconcile).
3. **Self round-trip**: the Composer's own composition file saves → loads byte-stable → Generate
   emits code that compiles into the same app (compositionID matches).
4. **Move/copy anything anywhere**: duplicate, cut/copy/paste, prefab stamp, drag-reparent — at
   root or drilled — item lands where created, both altitudes coherent, undo restores exactly.
5. **Time travel**: App-time scrubber freezes/rewinds the mirrored app; restore-and-replay is
   exact (contract 7 already proves the core).
6. **Vocabulary** (added 2026-07-05): author an op chain gating a command, a Tween driving a
   field, and a window docked via a Layout region — generate → the compiled app runs all three.

## 3. Now (in flight)

- **Scope-composition correctness push** — landed with this document: every creation road
  (RMB/toolbar/gizmo palette, command palette, empty-scope CTA, duplicate, paste, prefabs,
  drop-create, promote, explode) composes into the drilled scope where legal, palettes filter to
  legal kinds, live interiors are read-only with notice, refusals toast; one-producer-per-altitude
  enforced on every writer (add, tidy, nudge, group drag, explode, fit). Regression tests
  step41-46. Remaining from the same sweep, NOT yet fixed:
  - Inspector edits to inline Draft field lists are dead writes while that list is exploded
    (shadowed by field nodes; collapse destroys them). Needs routing to the field nodes.
  - `ScopeCams` entries of deleted scopes linger (memory only, ids never reused).
- **Next sweeps, one interaction family at a time** (equal-priority bug classes):
  1. undo/redo road coverage (does every mutation checkpoint + restore placements/events/order?),
  2. outliner drag-reparent + eye/hide family,
  3. wire ops family (detach, retarget, bindings),
  4. save/load fidelity diff (serialize → load → reserialize byte-compare),
  5. phase-coherence audit refresh (docs/phase-coherence-audit checklist against new writers),
  6. data-dependency adherence: generated + runtime code touches data/temp/last_temp only per
     declared deps (validator + codegen tests).

## 4. Next (unblocks the milestone flows)

- **Round-trip + undo hardening** (flow 3, 4): serialize→load→reserialize byte-stable test over a
  maximal graph (events, bindings, placements, prefabs, order, custom layers); undo snapshot
  coverage for every road the sweeps enumerate.
- **Mirror "never lies" completion** (flow 2): read-only gating audited across inspector,
  outliner, canvas (drag? delete? rename?) for live rows — same notice discipline as the interior
  palettes; promoted-twin reconciliation marks verified against a rebuilt app.
- **C++ import of full control skeletons** (flow 2 escape hatch; inverse of
  `AppEmitControlWithDeps`) — structs already round-trip; controls (fields, deps, events,
  commands) do not.
- **Self round-trip harness** (flow 3): a test that loads the Composer demo's own composition,
  regenerates, compiles the output, and compares `GetAppCompositionID`.

## 5. Later (named, not scheduled)

- Prefabs on disk + starter library.
- Command payloads; edit-intent bus folds into the command pipeline.
- Remappable input→command binding (delivered early as checklist F74/F75; extends F34/F35) — the
  editor's own keymap over the reified command registry; sibling to the edit-intent bus.
- Module interop (Source/Module node kind, status/command exchange).
- Status layer as queryable published structure.
- Sequence-order editing (chip drag + click-nudge) — deferred by decision above.
- Lifecycle view north star (up-next.md) — the root as the editable execution chart.

## 6. Verified feature matrix (audit, landed 2026-07-04)

Full matrix + detailed per-area TODOs: [feature-audit-2026-07-04.md](feature-audit-2026-07-04.md)
(470 doc claims verified against code + tests). Scope-sweep defect ledger:
[scope-sweep-2026-07-04.md](scope-sweep-2026-07-04.md).

| Area | shipped-tested | untested | partial | missing | parked |
|---|---|---|---|---|---|
| runtime-core | 17/31 | 3 | 3 | 2 | 6 |
| codegen | 11/35 | 10 | 4 | 7 | 3 |
| canvas | 75/149 | 23 | 14 | 15 | 22 |
| scopes | 25/56 | 11 | 10 | 9 | 1 |
| chrome | 8/83 | 33 | 20 | 12 | 10 |
| outliner-inspector | 10/39 | 14 | 10 | 4 | 1 |
| persistence | 9/17 | 2 | 2 | 3 | 1 |
| live-mirror | 8/16 | 3 | 0 | 4 | 1 |
| events | 5/7 | 1 | 0 | 0 | 1 |
| av | 16/27 | 5 | 2 | 0 | 4 |

Highest-leverage findings against the section-2 flows (audit's ranking):

1. **App-time scrubber UI is gone** — StateHistory core is shipped-tested but no UI reaches it;
   two docs claim it shipped (flow 5 blocked at the surface).
2. **Full control-skeleton C++ import missing** — "round-trips to C++ and back" is structs-only
   (flow 2 escape hatch).
3. **Two parallel emitters** (`AppEmitControlWithDeps` vs legacy `GenerateAppControlCode`), only
   one byte-locked — drift hazard inside the Generate step (flow 1/3).
4. **Chrome is the thinnest-verified area** (8/83 tested) with false shipped claims
   (problems-count badge, transport overlay) — the "no lying UI" bar fails here first.
5. **Inspector dead writes** on exploded Draft field lists (authored edits silently discarded).
6. **Sequence-order write path** absent (read-only order) — deferred by decision, tracked here.
7. **Layer-scope interiors partial** (Display/Task/Command: no walls/rail, wrong member density).
8. **Persistence untested seams**: layout sidecar, prefab registry (memory-only), undo coverage
   of newer records.
9. **Canvas input family untested** (RMB menu, wheel, drag at zoom≠1, snap, wire-drop palette) —
   regression risk over the engine's most-touched surface.
10. **Scope invariant tests missing** (validate-on-mutate, breadcrumb == scope-parent chain).

## 7. Suggested sequence to 100%

> **100% REACHED — 2026-07-05.** The feature series below is complete: every item in
> [feature-complete-checklist.md](feature-complete-checklist.md) (F01–F78.5) shipped + tested or parked by
> intent, confirmed by [feature-audit-2026-07-05.md](feature-audit-2026-07-05.md) (v2). The series grew past
> the original F01–F63 with the post-audit decisions — vocabulary nodes (F53–F57), run-without-a-build
> (playback debugger F61–F65 + previewer F66–F70), and the DLL live preview (F76–F78.5, copy-marshalling so
> imguix stays static). The forward horizon (Lifecycle view, module interop, status-layer model, command
> payloads, edit-intent bus, the DLL in-panel render + method-body editor) now lives in
> [up-next.md](up-next.md), not here. The historical plan is preserved below.

Explicit feature series (one deliverable + acceptance gate each):
[feature-complete-checklist.md](feature-complete-checklist.md) — F01..F63 in build order, plus
the dependency spine and six concurrent lanes (the phase order is only the safe serial order;
the checklist's lane section says what actually parallelizes).

Ordering law: correctness rails first (they are the tools that verify everything after), then the
generate/ingest spine (each phase is the previous one's inverse or proof), then surface truthing,
then the deferred feature, then the re-audit that defines 100%. Two lanes run CONTINUOUSLY under
every phase: (a) the canvas/chrome test-debt burn (audit `[t]` items land with whatever phase
touches their area, never as one big test phase), (b) narrative-comment deletion on contact.

Each phase names its exit gate; "green" always means imguix-tests + imguix-headless-verify.

- **P0 — Scope composition correctness.** DONE 2026-07-04 (scope-sweep doc; step41-46).

- **P1 — Persistence + undo rails.** Serialize→load→reserialize byte-diff harness over a maximal
  graph (events, bindings, placements, prefabs, custom layers); undo-road sweep + fixes (every
  mutation checkpoints and restores placements/events/order); prefab registry to disk (Soon item,
  same records); fix the two OPEN sweep items (inspector dead-writes on exploded Draft lists,
  ScopeCams sweep on node removal).
  *Why first: every later phase adds records; without the byte-diff rail their bugs are invisible.*
  Gate: persistence 17/17; round-trip test red on any unserialized record.

- **P2 — Behavior-class sweeps.** The three equal-priority bug classes over the remaining
  interaction families: outliner drag-reparent/eye, wire ops (detach/retarget/bindings),
  phase-coherence audit refresh over the new writers, data-dependency-declaration adherence
  (validator + codegen tests: code touches data/temp/last_temp only per declared deps).
  *Why here: P1's rails catch what these sweeps shake loose.*
  Gate: each family has its sweep doc with FIXED/OPEN ledger; OPEN list empty or scheduled.

- **P3 — One emitter, byte-locked.** Unify `AppEmitControlWithDeps` vs legacy
  `GenerateAppControlCode` (kill or subsume the unlocked one); signature/staleness gating and
  Generate health states tested; custom-layer emission tested.
  *Why before import: the importer is the emitter's inverse — it needs ONE fixed target.*
  Gate: codegen 25+/35; single emission path; byte-lock covers every emitted construct.

- **P4 — Ingest closes.** Full control-skeleton C++ import (fields, deps, events, commands) as
  the inverse of P3's emitter; emit→import→emit fixed-point test over a corpus.
  Gate: flow-2 escape hatch works; fixed-point test green.

- **P5 — Mirror never lies.** Read-only gating audited across inspector/outliner/canvas for live
  rows (same notice discipline as the interiors); promote/reconcile marks verified against a
  rebuilt app; live-drill surfaces (walls caption, promote jump) tested.
  Gate: flow 2 runs end-to-end headless: mirror → drill → promote → edit → generate → marks
  reconcile.

- **P6 — Chrome tells the truth.** Restore the App-time transport surface (StateHistory has zero
  UI callers — flow 5's missing mouth); problems-count badge; Ctrl+P; status-bar fact zones; then
  burn the 33-item chrome test-debt list.
  *Why after P5: transport + health chrome read mirror state; gate on the surfaces they describe.*
  Gate: chrome has no false shipped claim; 8/83 → majority shipped-tested; flow 5 demo-able.

- **P7 — Scopes complete.** Layer-scope interiors (Display/Task/Command walls, rail order, member
  density per the par.4 table); scope invariant tests (validate-on-mutate, breadcrumb ==
  scope-parent chain); remaining scope test-debt (end band, rails, shrink deadband, ordinals).
  Gate: scopes 45+/56; every enterable scope kind renders per the table and takes only legal adds.

- **P8 — Self round-trip (the milestone's namesake).** Harness: load the Composer demo's own
  composition → regenerate → compile output → `GetAppCompositionID` equal; save→load→resave
  byte-stable on the same file.
  *Everything before this is its prerequisite: P1 fidelity, P3 emitter, P4 inverse.*
  Gate: flow 3 green in the suite.

- **P8.5 — Vocabulary: logic, animation, layout nodes** (decided 2026-07-05). Op node kind that
  FOLDS into the consumer's emitted expression at codegen (type rules ride AppEventExprCheck; no
  runtime object); animation as a builtin Control library (Tween/Timer/Spring/Pulse — RandomTime
  precedent, dt-driven Task phase, temp^last discipline, time-travel free); layout evaluated
  across all three models — window facts as the baseline, Region/Split/Tabs family composing
  into the Layout layer as primary (the layer's first real domain), constraint edges given an
  explicit build-or-reject verdict in the design doc.
  *Why here: P1 proves the new records round-trip, P3/P4 prove the new emissions stay
  byte-locked and importable — vocabulary extends the rails instead of preceding them.*
  Gate: op chain gates a command in a compiled run; Tween deterministic under Fixed-dt and
  App-time-scrubbable; window composed into a region docks in the generated app.

- **P9 — Sequence-order write path.** Order as model state + codegen emission + chip drag with
  click-nudge fallback (ScopeStripRects already publish the landing surface). Deferred until now
  by decision; lands on tested persistence (P1) and emitter (P3).
  Gate: reorder → save/load/undo → generate emits the new order; core 4 layers never reorder.

- **P10 — Re-audit = the definition of 100%.** Re-run the doc-claims audit; every claim is
  shipped-tested or explicitly parked in its doc; flows 1-6 each have one end-to-end headless
  test; roadmap sections 3-5 empty or moved to Horizon.

Post-100% (Horizon, unchanged): command payloads, status-layer queryable model, module interop,
edit-intent bus fold-in, remappable input binding (delivered early, checklist F74/F75), Lifecycle
north-star view.
