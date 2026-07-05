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

## Drag-reparent automated test — harness limitation (not a behavior gap)

The hierarchy rows are `TreeNodeEx("##row", …)` under a per-node `PushID(id)` chain — hidden-label
items with no string path. A test must reconstruct the id by folding `ImHashData(id, …, window->ID)`
down the tree path. That works for a ROOT node (step22 eye, step59 clone both drive real rows this
way) but could not be made to resolve reliably for a window nested under the Display layer within the
harness, so the `ItemDragAndDrop` between a hosted-control row and a window row could not be driven
headlessly. The reparent BEHAVIOR is covered by the guard + rewire reading above; what is missing is
the UI-automation of the drag itself. Follow-up: a tree-row test seam (e.g. the outliner publishing
row rects, or a headless "reparent(child, parent)" command the drop already funnels into) would let
this verb be driven like the others.

## OPEN (bugs)

_(empty)_ — no behavior defect found. The one gap is test AUTOMATION of the drag-reparent gesture
(above), tracked as a follow-up, not a bug.

Gate: imguix-tests 68/68 (test-only sweep + one new clone test; no production change).
