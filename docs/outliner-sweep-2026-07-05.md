# Outliner (scene-hierarchy tree) family sweep — 2026-07-05 (F09)

Adversarial pass over the outliner row verbs: eye/hide, clone, drag-reparent. Same form as the
wire-ops sweep.

## Verbs + tests

| Verb | Path | Test | Status |
|---|---|---|---|
| eye / hide | eye glyph → `n->Hidden` toggle (or live window Open); hidden node evicted, re-shown at saved pos | step22, step23, step24 (isolate/show-all) | FIXED |
| clone | clone glyph (hover-revealed draw-list hit-test) → `c->Act = 2` → `AppGraphDuplicateNode` | step59 | FIXED |
| drag-reparent | `BeginDragDropSource` on a Control/Field row, `BeginDragDropTarget` on a Window/Sidebar/Struct row → `c->Act = 5` → `AppGraphReparent` | guard read (see below) | VERIFIED BY READING |

## Adversarial invariants checked (no bug found)

- **Reparent pair gate is defense-in-depth.** The drop GATE is permissive (source = any Control/Field,
  target = any Window/Sidebar/Struct), which allows illegal drops like Control→Struct or Field→Window.
  `AppGraphReparent` (12433) re-checks and accepts ONLY `Control→{Window,Sidebar}` and `Field→Struct`;
  everything else returns false with no mutation. So an illegal outliner drop is a no-op, not a
  malformed graph. Live/self/missing-port cases also rejected.
- **Reparent rewires cleanly.** It erases the child's existing containment edge before adding the new
  one (12448) — one containment parent, always (matches the AppGraphValidate forest invariant).
- **Eye-hide keeps position.** step23/24 pin that a hidden→shown node returns to its stored GridPos
  (the imnodes-eviction round-trip that once reset it to the origin).

## Drag-reparent automated test — resolved (step103)

The hierarchy rows are `TreeNodeEx("##row", …)` under a per-node `PushID(id)` chain — hidden-label
items whose id a test reconstructs by folding `ImHashData(id, …, window->ID)` down the tree path
(step22 eye, step59 clone both drive real rows this way). The earlier gap was framed as needing a
window nested under the Display layer; that framing was wrong. AppGraphReparent is scope-blind — a
top-level Control dragged onto a top-level Window funnels through the identical
`BeginDragDropSource`/`AcceptDragDropPayload` → Act=5 → AppGraphReparent path. So step103 builds a
top-level Control + Window, reconstructs both single-element row ids via `AppTreeRowId`, drives
`ItemDragAndDrop`, and asserts the containment link appears — then asserts an illegal Field→Window
drop is a no-op (the pair-gate rejects it). The gesture is now UI-automated like the others; no
production change and no `##row` addressability change was needed.

## OPEN (bugs)

_(empty)_ — no behavior defect found; the drag-reparent gesture is now driven headlessly by step103.

Gate: imguix-tests 68/68 (test-only sweep + one new clone test; no production change).
