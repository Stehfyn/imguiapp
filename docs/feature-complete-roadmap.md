# Roadmap (v2 — post-100)

Lean forward map. The original feature series (F01–F78.5) reached 100% on 2026-07-05 — it and its audit
live in [archive/](archive/) ([checklist](archive/feature-complete-checklist.md),
[roadmap](archive/feature-complete-roadmap.md), [up-next](archive/up-next.md),
[audit v1](archive/feature-audit-2026-07-04.md)); the closure is
[feature-audit-2026-07-05.md](feature-audit-2026-07-05.md).

## Where we are

Author → generate → run → debug is a closed loop: persistence + undo, one byte-locked emitter with a full
import back-edge, a hand-rolled canvas, scopes + sequence-order authoring, truthful chrome, the op /
animation / layout vocabulary, a playback debugger, a graph previewer (interpreter backend), and a **DLL
live preview** that compiles + runs the real emitted program by copy-marshalling (imguix stays a static
lib) — including hand-written control method bodies.

## Post-100 horizon

Sequenced roughly by how directly each closes a live-authoring gap. Per-item detail (files, structs,
functions, acceptance) lives in [feature-complete-checklist.md](feature-complete-checklist.md) (v2).

1. **See the DLL preview live in-panel** — the compiled preview renders in its own context; get its frame
   into the Composer's Preview panel.
2. **Author method bodies in-app** — a control body editor over F78.5's compiled-body machinery.
3. **Lifecycle view** — the north-star authoring surface: the frame as an editable lifecycle chart (rides
   the sequence-order write path).
4. **Module interop** — independent modules the Task layer ingests / Command layer drives / Status layer
   publishes; a Source/Module node kind + a runtime exchange.
5. **Status-layer model** — a queryable published-status structure replacing "the status bar".
6. **Command payloads** — arguments in a queue, not bare enums.
7. **Edit-intent bus** — fold the doc-control mutation escape hatch into the command pipeline.
8. **Reliability residuals** — the `step93` order-loader flake + the tracked chrome/scope test-debt.

Principle held: a feature never dictates how the library links (why F77's shared-core split was reverted).
Live debugging and module interop are the north; single-app authoring came first and is done.
