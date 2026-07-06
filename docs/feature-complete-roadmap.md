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

## Post-100 arc (A → E, in order)

Each phase gates the next. Per-phase detail (files, structs, functions, decisions, acceptance) +
design-first docs live in [feature-complete-checklist.md](feature-complete-checklist.md) (v2).

- **Done:** DLL preview renders live in-panel (closes the F78/F78.5 "see it live" residual).
- **A — lean & mean split.** `imguiapp_internal.h` for tool interfaces + one `IMGUIX_DISABLE_TOOLS` switch
  that compiles the Composer/Previewer/Debugger out → true lean release, no source text in the `.exe`.
- **B — embed control source.** Bake the controls' source into the tools build → show REAL function bodies
  (not the fake generated skeleton) + rich contextual presence, and the "final fold": test a body change in
  the previewer, then write it back to the real source on disk (full bidirectional source↔graph↔source).
- **C — refactor to the imgui.h canonical schema.** Re-impose imgui.h's structure / ordering / comment
  discipline across the headers + `.cpp`s; consolidate; strip AI comments. Pure structural, zero behavior.
- **D — UI canon · HTML help · bug-button.** Formalize the design language (icon↔meaning); generate rich
  HTML help; a bug-button that records a tagged, playable session over the existing AV/playback rails.
- **E — phase-coherence re-audit + hardening → full UI redesign.** Only once everything above is stable.

Principle held: a feature never dictates how the library links (why F77's shared-core split was reverted).
