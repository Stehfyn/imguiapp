# Up Next

Living document. Rewrite freely; history lives in git. Priorities follow the project's spine:
**framework first — the Composer is flagship dogfood proving the idioms.**
(See [big-idea.md](big-idea.md) for the concepts these items serve.)

## 100% reached (2026-07-05)

The explicit feature series [feature-complete-checklist.md](feature-complete-checklist.md) (F01–F78.5) is
**complete** — every item shipped + tested or parked by intent; the re-audit
([feature-audit-2026-07-05.md](feature-audit-2026-07-05.md)) confirms zero missing/partial without a doc
note. Author → generate → run → debug is a closed loop: persistence + undo rails, one byte-locked emitter
with full import back-edge, a hand-rolled canvas, scopes + sequence-order write path, chrome that tells the
truth, the vocabulary (op / animation / layout nodes), the playback debugger, the graph **previewer**
(interpreter backend + contract parity), and the **DLL live preview** — real compiled code compiled + run
at runtime by copy-marshalling (imguix stays static; F78/F78.5), including **hand-written control method
bodies** (`OnRender`/`OnUpdate`/… with arbitrary ImGui calls, compiled + run live).

The list below is the **post-100 horizon** — named, not gated. Nothing here blocks the closed loop.

## North star: the Lifecycle view (authoring-UI direction)

Unity's script-lifecycle flowchart (docs.unity3d.com/Manual/execution-order.html) is the model, upgraded
from documentation to authoring surface. The Composer's root becomes a generated, editable lifecycle chart
of the USER'S composed app: one vertical spine = the frame (loop-back edge included), the phase layers as
labeled bands bracketed by Initialization (push order) and Decommissioning (reverse-pop); event slots inside
each band in true call order; framework internals as grey context rows (`TempData = {}`, the `LastTemp =
Temp` swap); the one-frame skew arrow (this architecture's signature). Editable: dragging a slot reorders the
push, codegen emits the new order. The sequence-order write path (F58–F60) is the model substrate this rides.

## Now (post-100)

- **DLL preview in-panel render** — close the live loop visually: the copy-marshalling DLL renders in its
  own context, so its frame must cross as pixels (offscreen readback) or replayed draw data to appear in the
  Composer's Preview panel. The backend + hand-written bodies run + verify headlessly today (F78/F78.5); this
  is the remaining "see it live in-app" step.
- **Author method bodies in-app** — F78.5 stores + compiles hand-written `OnInitialize/OnGetCommand/OnUpdate/
  OnRender` bodies through the DLL preview with no UI yet. Add a per-control body editor (text field, reload
  on edit) and reflect custom bodies into the interpreter's card ("runs after Generate / in the DLL preview").
- **Lifecycle view** — the north-star authoring surface above, now unblocked by the order write path.

## Horizon (named, not scheduled)

- **Module interop** — the layer model's full shape: independent modules (worker threads, async IO, external
  processes) whose status the Task layer ingests, whose commands the Command layer receives, and who query
  the Status layer's published status. Wants a Source/Module node kind + a runtime status/command exchange.
- **Status layer model** — replace "the status bar" with a queryable published-status structure other modules
  (and the Composer) read.
- **Command payloads** — commands are bare enums, interim by intent; arguments belong in a queue, not the enum.
- **Edit-intent bus** — the doc-control's "const dependency that panels mutate" escape hatch is acknowledged
  tech debt; fold into the command pipeline once payloads exist. (Sibling to the shipped input→command binding
  F74/F75.)
- **Reliability + residuals** — fix the `step93_order_roundtrip` flake (F58/F60 `Order=` loader); the F47/F48
  scope-chrome + note refinements; the F40 low-value chrome test-debt.

## Recently landed (2026-07-05) — the run-without-a-build + DLL milestone

- **DLL live preview (F76–F78.5)** — the previewer's high-fidelity backend: emit the real program, compile it
  at runtime, run it, drive it by **copy-marshalling** (own runtime per DLL, bytes-only C-ABI) so imguix stays
  a static lib and the preview is link-mode-agnostic. F78.5 adds hand-written method bodies (real ImGui calls
  compiled + run). F77's shared-core split was implemented then **reverted** — a feature must not dictate how
  the library links.
- **Previewer (F66–F70)** — a second backend interpreting the model live; contract suite (1–9) runs on the
  interpreter beside generated code; preview ⇄ time-travel tie (record → open → scrub).
- **Playback debugger (F61–F65)** — a recorded run opens as one artifact; tick index; FILE-mode transport with
  decoded frames; state-at-tick via restore-nearest-snapshot + replay; divergence surfacing.
- **Vocabulary (F53–F57)** — Op nodes + expression fold, animation builtins (Tween/Timer/Spring/Pulse), the
  Layout node family (Region/Split/Tabs → the Layout layer's first domain).
- **Self round-trip + shell (F51/F52)** — the Composer's own composition round-trips + regenerates + compiles;
  the emitter emits a host shell that runs a Composer control.
- **Sequence-order write path (F58–F60)** — order as model state, serialized + undoable + validated
  (core layers never reorder); codegen emits authored push order; chip-drag reorder + click-nudge.
- **Remappable input→command binding (F74/F75)** — per-graph keymap overriding the registry default chords,
  through one dispatcher; a rebind editor UI. Horizon-class, delivered early.
