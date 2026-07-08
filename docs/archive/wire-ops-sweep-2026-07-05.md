# Wire-ops family sweep — 2026-07-05 (F10)

Adversarial pass over the data/containment edge interactions, same form as the outliner sweep:
one verb at a time, driven through the real FSM where possible, `OPEN` empty at the end.

## Verbs + tests

| Verb | Path | Test | Status |
|---|---|---|---|
| create | `CaptureAppGraphLinks` on `CanvasWireCreated` (pin→pin drop) | canvas_c2 | FIXED (tested) |
| detach → delete | `CanvasWireDetached` grabs a wire near an endpoint, drop on empty → link + bindings swept | canvas_c5 | FIXED |
| detach re-drag / **retarget** | detach the IN end, drop on a DIFFERENT in-pin → old edge dies, new edge to the new consumer; producer end survives | canvas_c6 | FIXED |
| binding **create** | `EditAppDataEdgeBindings` "Add binding" pill → row keyed by the link id | step57 | FIXED |
| binding **update** | click-to-edit `##dst` / `##src` fields (`AppBlInputText`) | step57 | FIXED |
| binding **delete** | per-row `##del` → erases only that binding | step57 | FIXED |

## Adversarial invariants checked (no bug found)

- **No orphan bindings.** Every road that removes a data edge sweeps its bindings: `AppGraphEraseLink`
  (link delete, ~2907), `CaptureAppGraphLinks` detach branch (~3139), and `AppGraphRemoveNode`
  (node delete, F06) all erase `Bindings` keyed by the dying `LinkId`. Retarget goes through detach
  (bindings gone) + create (fresh edge, no bindings) — correct: a binding named the OLD producer's
  field and must not silently follow the wire to a new one.
- **Retarget keeps the surviving end.** The engine continues the drag from the pin NOT grabbed
  (`DragWireFromPin` = the far end), so a producer→consumer retarget changes only the consumer.
- **Bindings are data-edge only.** `EditAppDataEdgeBindings` early-returns for a containment edge —
  containment carries no field mapping.

## Drilled variants

Wire creation / detach / retarget are canvas-ENGINE gestures (pins + wires in model space); the
binding editor is keyed by link id. Neither reads the drill scope, so a wire op inside a drilled
interior resolves identically to one at root (the pins it connects are the same model ports). No
scope-specific path, so no scope-specific test needed.

## OPEN

_(empty)_ — the wire-op family is model-solid; the sweep added tests per verb, fixed no defects.

Gate: imguix-tests 67/67 (test-only sweep, no production change → core/headless unaffected).
