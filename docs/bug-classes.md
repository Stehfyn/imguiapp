# Bug Classes — Rules of Thumb & Smells

Reference page, collated from the phase-coherence and zero-value reference pages and the
scope-composition, wire-ops, outliner, live-gating, and phase-coherence audits plus the usability
field pass (instance-level findings preserved in [archive/](archive/)). This page keeps only what
generalizes: the bug class, the rule that kills it, and the smell that finds it. Read the relevant
section before touching its surface; the smell table at the bottom is greppable.

---

## Phase Coherence — the out-of-phase class

Read this before writing ANY code that sizes, places, or styles UI from measured geometry. The
canonical regression: canvas zoom shipped with three of these at once (§3).

### 1. The bug class, generally

Immediate-mode UI measures as it draws: the size of a thing is only known AFTER submitting it. Naive
code therefore sizes frame T's UI from frame T-1's measurements. That is fine — **inevitable, even** —
as long as the cached measurement and the state it is combined with belong to the same coordinate
story. The bug class appears the moment a cached value from frame T-1 is combined with a TRANSFORM or
INPUT from frame T that changes the meaning of that value:

```
stale = f( measured_at(T-1), transform_at(T) )     // WRONG: mixed phases
ok    = f( measured_at(T-1), transform_at(T-1) )   // coherent pair, re-derived fresh at T
```

Symptoms: a one-frame flash of wrong sizes/positions on every change of the mixed-in input (zoom,
DPI, font, panning mode, layout direction). It is worse than a steady-state bug because it is
intermittent, ugly, and erodes trust — the UI visibly "settles."

**ImGuiAppLayer's whole architecture exists to kill this class.** The frame pipeline (ingest ->
command -> publish -> render, with OnUpdate as the sole state mutator and OnRender pure) forces
derived data to be computed once, in phase, before anything draws. A feature that bypasses the
pipeline and mixes phases has not just added a bug — it has contradicted the framework's thesis.

### 1b. The measurement feedback loop (second species)

The stale-frame mix (§1) is the FIRST species: a one-frame flash. The SECOND species is the
**measurement feedback loop**: a value that is *applied* one frame and *measured back* the next,
where the apply→measure round-trip is not exactly idempotent. Instead of a single-frame settle, the
value drifts a little every frame — the UI visibly **animates** toward (or past) its fixed point.

```
v(T+1) = measure( apply( v(T) ) )        // stable ONLY if measure∘apply is the identity
```

`measure∘apply` stops being the identity whenever a lossy channel sits in the loop: pixel snapping
under a zoom transform, glyph rasterization widths, rounding to ints, clamping. Each pass through
the loop adds a hair of error; combined with a max/ratchet the value stair-climbs for many frames.

**Rule: every measure→apply loop must have an explicit fixed point.** Either make the round-trip
exact (measure in the same invariant units you apply), or add a **deadband/hysteresis** so loop
noise cannot move the value — only real input changes can. If you cannot name the fixed point, the
loop is a bug waiting for a transform to excite it.

Canonical example: a shared uniform column width, cached as `pixels / zoom` and ratcheted by max.
At a fixed zoom the loop is exactly idempotent; a zoom change perturbs text/pixel rounding, and the
width visibly animated across frames on every wheel tick. Fix: the width only moves when the
measurement exceeds it by a small model-unit deadband — the loop has a fixed point again, and
genuine content growth still propagates in one settle.

A corollary: **a "settle"/settledness state machine is itself the bug class.** Do not add acceptance
rules or settled-flags on top of measurements; give the loop a fixed point (exact units or deadband)
and keep sizes plain T+1 engine measurements.

### 1c. The render-phase mutation (third species)

The first two species are about *reading* a value from the wrong phase. The third is about *writing*
one. **A model mutation performed during the render pass reads its own inputs mid-publication.** The
render pass publishes derived facts INCREMENTALLY — group frames, wall rects, column geometry are
each pushed as their owner draws. Code that mutates the model *while* that publication is in flight
can only see the facts published SO FAR this frame; for everything not yet drawn it must fall back to
LAST frame's complete set. So the write is gated on a mix of this-frame and last-frame facts — a
phase split hidden inside a single function.

```
render:  for each owner:  publish(owner.fact);  if (dragging) mutate(model, using facts_published_so_far ∪ facts_prev)   // WRONG
update:  for each owner:  publish(owner.fact);
         after all published:  if (drag_pending) mutate(model, using facts_this_frame)                                   // coherent
```

This is why **rule 2 says mutate in the update pass, never in render.** OnRender's only write is the
edit-INTENT (record "the user is dragging owner X"); OnUpdate applies it once every this-frame fact
is published. Deferring costs one frame of settle — in model units, imperceptible (rule 4).

Canonical example: a group drag whose slide-to-contact clamp ran inline in the canvas render pass,
mutating member positions while the obstacle frames were still being drawn. Its obstacle set could
therefore only be last frame's complete publication while this frame's was half-built — the drag
clamped against a one-frame-stale neighbour set, clipping past a box it should have slid against.
Fix: record the drag as a pending intent during the pass; run the clamp + writes after `CanvasEnd`
against this frame's complete facts. The tell was structural, not visual: *a model write between
`CanvasBegin` and `CanvasEnd`.* Audit rule: grep the render pass for model mutations (`GridPos =`,
`AppCanvasSetNodePos`, `…ScopePosStore`, node add/remove, link/reparent). Each must be a recorded
intent applied post-submission, NOT an inline write — the sole exceptions are pre-submission SEATING
(persistent model position → engine, before the node draws, like the input FSM) and transient view
toggles that feed no measurement.

### 2. The rules

1. **Cache in invariant units.** When a measurement must cross a frame boundary, divide out every
   frame-varying transform AT CAPTURE TIME, with that frame's own transform values. The cached value
   is then timeless; consumers re-apply the CURRENT transform. (The Composer caches node geometry in
   *model units*: `pixels / zoom_at_render`, captured in the same-frame read-back.)
2. **Derive in update, draw in render.** Counts, labels, sizes, layout solutions: computed in
   OnUpdate (or a same-frame read-back phase), stored in PersistData, read by OnRender. OnRender
   records raw input into TempData and mutates nothing (the edit-intent bus).
3. **One producer per fact.** If two panels each recompute a value from raw state in their render
   paths, they can disagree for a frame. Compute once, publish, consume (this is what control data
   dependencies are FOR — wire them instead of recomputing).
4. **Deliberate T+1 is fine; accidental T+1 is the bug.** Content-driven settling (a node grows
   because its title got longer, and dependent layout follows next frame) is the framework's
   documented measure-then-apply idiom — in invariant units it produces no visual artifact. The
   defect is exclusively the *mixed-phase* combination.
5. **Draw-list decorations obey the same law.** Anything drawn from node rects (group boxes, rails,
   badges, bands) must either read THIS frame's rects (post-submission) or transform cached
   model-space data with THIS frame's zoom/pan. Never previous-frame pixels.

The precise audit question is temporal, not topological: not "does the fact depend on the camera"
(subsequent dependence is fine) but **does any value MEASURED in a prior frame get combined with a
TRANSFORM or INPUT from THIS frame** — `f(measured(T-1), transform(T))` — or flow through a
measure→apply→measure loop (§1b), or get written mid-publication (§1c). A T-1 value is safe across
the boundary iff it is in transform-invariant (model) units.

### 3. The canonical example (the zoom regression)

Canvas zoom was added over the node engine: grid space = model units × zoom, node content under a
zoomed font, style scalars scaled per frame. Three phase violations shipped with it:

| Violation | Symptom | Fix |
|---|---|---|
| Uniform layer width = engine dimensions (last frame's PIXELS, rendered at the OLD zoom) `/ CURRENT zoom` | every wheel tick flashed wrong layer widths for one frame | cache model width at capture (`px / zoom_used_that_frame`) in the read-back; the consumer multiplies by the current zoom — zoom can no longer de-phase it |
| Decorations (pipeline box, phase bands, execution rail) drawn from PREVIOUS frame's screen pos/size | decorations lagged the nodes by one frame on zoom (and always had, invisibly, on drag) | decorations compute rects as `canvas_origin + panning + GridPos(model) * zoom_now`, sizes from the model cache |
| Viewport chrome drawn into the parent window's draw list, hit-tested with parent-window items | any node scrolled beneath it OCCLUDED the controls and KILLED their clicks (the engine's inner child renders above the parent list and wins hover) | viewport chrome draws into the foreground draw list (clipped to the canvas) or is a real overlay window — both z-order and hit-testing stay above canvas content |
| Group bounding boxes read the pool's screen rects while drawing BEFORE this frame's submission | group frames + title bars flashed stale on every zoom change — found in review AFTER an "audit" claimed decorations were fixed: the sweep asserted this one coherent without reading it | same model transform as the other decorations. Audit lesson: classify EVERY screen-pos/dimensions call site by its draw phase (pre-submission = last frame's pixels; post-submission = coherent) — do not sample |

### 4. Review checklist (apply to every canvas/layout change)

- [ ] Does any value cross a frame boundary? In what units? Are those units transform-invariant?
- [ ] Is anything computed in a render path that OnUpdate should own?
- [ ] Do two places derive the same fact from raw state?
- [ ] Do draw-list decorations read geometry from the frame they draw in?
- [ ] If zoom/DPI/font changed THIS frame, is every pixel this feature emits still correct THIS frame?
- [ ] Is any deliberate T+1 settling in invariant units (no visual jump), and is it commented as such?
- [ ] Does any value flow measure -> apply -> measure (a feedback loop)? Name its fixed point: either
      the round-trip is exact in invariant units, or a deadband/hysteresis absorbs loop noise (§1b).
- [ ] Does anything mutate the model between `CanvasBegin` and `CanvasEnd` (§1c)? Only recorded
      intents applied post-submission, pre-submission seating, and measurement-free view toggles pass.

---

## Zero-Value Defaults — "0 triggers an action"

Read this before adding any field (or enum) that selects *which* action a control performs —
especially a TempData field. Sibling of the phase class above: that one is about reading a value
from the wrong *phase*; this one is about the DEFAULT (zero) value silently carrying *meaning*.

The smell: **a field whose zero value triggers non-default behavior** — an enum whose 0th entry is a
real action, or an index where 0 selects a real item while a negative sentinel (`-1`) means "none."

Zero is the universal resting state of memory. Value-initialization (`T{}`), aggregate `= {}`,
`memset`, and a bare `int` member with no initializer all land on 0. So any code that reads a field
as "perform action N" and treats `N == 0` as a valid action performs that action *every time the
field was never explicitly written*. The default state of the program is not inert — it does something.

```
if (action >= 0) do(action);   // WRONG: action==0 is armed by every zero-init
if (action >  0) do(action-1); // OK: 0 is inert; real actions are positive (stored 1-based)
```

**Rule: 0 means nothing — no action, the default, the no-op. Real actions start at a positive value.**
Store an action index 1-based (0 = none, action *k* = index *k-1*). Reserve an action enum's 0th entry
for `None`/`Default` and give it no side effect. Never pair "0 is a valid action" with a "-1 = none"
sentinel: the sentinel only protects the code paths that remember to set it, and it leaves the resting
state (0) armed.

**Why immediate-mode TempData makes this bite.** TempData is a control's per-frame INPUT: OnRender
records it, OnUpdate consumes it. The framework value-initializes it — the instance is
`IM_NEW()()`-constructed, and the OnRender wrapper does `_InstanceData->TempData = {}` at the top of
every frame. OnUpdate consumes *last* frame's OnRender output. Two consequences put a zero-valued
TempData in front of OnUpdate:

- **First frame.** OnUpdate runs before any OnRender has written TempData (the per-frame order is
  OnUpdate then OnRender), so it reads an all-zero struct.
- **Any frame the writer skipped.** If the OnRender path that sets the field did not run (panel
  closed, early return), the `= {}` reset leaves the field at 0 for the next OnUpdate.

A TempData field whose 0 value is a real action therefore fires on the first frame and on every
skipped-writer frame — exactly the frames the author was not thinking about. Setting the sentinel in
OnRender cannot fix the first frame; setting it in OnInitialize would patch only that one frame and
leave skipped-writer frames armed. The robust fix is structural: make 0 mean none.

Canonical post-mortem: a prefab-stamp selector stored as a **0-based** index with `-1 = none` and an
OnRender-set guard. Frame 1: TempData zero-init → index 0 → `0 >= 0` → the library's first prefab
stamped into every clean graph. Tests missed it because they exercised the seed primitives directly,
not the control's first-frame OnUpdate. Fix: 1-based index (`0 = none`, stamp `k-1` when `> 0`); the
sentinel and its guard are deleted — the default graph is inert *by construction*, not by a guard
that has to remember to run.

Checklist:

- A TempData or enum field that selects an action: **0 = none/default; actions are positive.** Store
  indices 1-based.
- Reserve the 0th enum entry for `None`/`Default`; never give it a side effect.
- Do not rely on an OnRender-set sentinel (or an OnInitialize poke) to keep a zero-init field inert.
- Audit tell: a `>= 0` or `!= 0` guard on a TempData action selector. Each is a candidate for this
  bug; prefer `> 0` with a 1-based value.

---

## Scope Composition & Altitude

The editor renders the same model at multiple altitudes (root layout vs drilled interiors), with an
invariant: **one producer per altitude** — root positions (`GridPos`) are owned by root-altitude
writers, interior placements by interior writers. A whole family of bugs comes from breaking it.

**Creation without adoption.** Every road that creates a node (palette add, duplicate, paste,
drop-create, prefab, promote, template, importer) MUST go through the single composition road that
adopts the node into the active scope and seats it at the right altitude. A creation path that
bypasses it produces an *invisible node*: nothing appears where the user acted, and the node
materializes at root — often at coordinates from the wrong space. The class was found on a dozen
independent creation roads at once; when a new creation road is added, adoption is the first thing
to check, not the last. Smell: any call to the raw node-create/import primitive that is not followed
by the compose/adopt call.

**Silent adoption refusal.** If composition CAN refuse (illegal kind pair, live owner), the refusal
must have a feedback channel — see the next section. A legality table (which kinds compose into
which scopes) must also drive the UI: palettes offer only composable kinds, and the sole legal kind
is never missing from the menu. Smell: a palette whose offerings are static while scope context
varies.

**Altitude leak.** A writer that takes coordinates from one altitude and stores them at another —
interior click/camera coordinates written into root `GridPos`, or a root-frame drag origin baked into
interior placements. Symptoms: nodes "teleport" at the other altitude; layouts corrupt invisibly and
show up later. Every geometry verb (tidy, nudge, group drag, explode, fit, auto-place) must be
altitude-routed: read and write the altitude the user is looking at, and only that one. Smell: a
handler that reads an interior/camera-space position and writes `GridPos`, or vice versa, without an
explicit altitude switch.

**Scope-unaware geometry.** Geometry consumers (camera fit, occupancy scans, offspring anchoring,
drag origins) that read root positions while an interior is active frame or collide against *stale,
invisible* rects. Rule: geometry consumed while drilled reads the drilled altitude's effective
positions (placement-aware accessors), never raw root state.

---

## Silent Refusal & Read-Only Gating

**A refused operation must say so.** When the model layer refuses a mutation (illegal reparent,
read-only target), the gesture still completed from the user's view — popup closed, drag dropped.
If nothing appears and nothing explains why, the product reads as broken. Every refusal reachable by
a user gesture rides a feedback channel (the same toast/status channel refused links use), with one
canonical phrasing per refusal kind so the notice reads the same everywhere. Smell: a mutation
function whose `bool` result is discarded at a call site a gesture can reach.

**Gating that is complete but silent is half done.** A read-only surface (e.g. a live mirror of the
running app) must refuse every mutating verb — but audit the *notice* separately from the *block*.
The sweep shape: verbs × surfaces matrix, and for each cell record blocked? notified? Two legitimate
outcomes: (a) the verb is structurally unofferable (no glyph drawn, no menu item, no drag pickup) —
no notice needed, there is no gesture to intercept; (b) the verb is attemptable (hotkey, command
palette, programmatic road) — block AND notify.

**Guards live at user-facing call sites, not inside engine functions.** An engine primitive that
internal machinery must call on protected objects (e.g. remove-node, which the live-mirror rebuild
uses every frame) must NOT gain an internal is-protected guard — that would break the machinery.
Protection belongs at the gesture-level call sites. Conversely, *editor* entry points that mutate a
specific object SHOULD carry a defense-in-depth self-guard (early return on read-only) so they stay
safe under future callers — caller discipline alone is a latent gap.

---

## Graph & Record Hygiene

**No orphan records.** Every road that removes an entity must sweep the records keyed by it — edge
removal sweeps that edge's bindings; node removal sweeps its edges, bindings, placements, AND the
side tables (per-scope cameras, caches). The tell for the miss is usually memory-only growth (ids
never reused), which is why it escapes: audit removal roads by enumerating what is keyed by the dying
id, not by watching behavior. Smell: a new map keyed by node/link id with no corresponding erase in
the removal road(s).

**Permissive gate, strict engine.** UI-level drop/accept gates may be loose (accept broad kind
categories); the model-layer mutation function re-checks the exact legality table and returns false
with NO mutation on failure. An illegal gesture is then a no-op, never a malformed graph. The engine
check is the invariant; the UI gate is UX. (And an engine no-op still deserves a notice — see above.)

**Retarget = detach + create.** Re-pointing an edge destroys the old edge's dependent records
(bindings named the OLD endpoint's fields) and creates a fresh edge with none. Dependent records must
never silently follow a rewire to a new endpoint.

**One containment parent, always.** Reparent erases the existing containment edge before adding the
new one; the forest invariant is maintained by the mutation function itself, validated by the graph
validator.

**Eviction round-trips preserve position.** Hide/show (or any engine evict/re-add cycle) must return
the entity to its stored model position — an engine round-trip that resets to origin is the classic
regression; pin it with a test.

---

## State Residency

**Per-document state never lives in function-local statics or TU globals.** Every editor-session
fact (hover bus, caches, latches, filters, undo, clipboards, drag FSMs, per-canvas latches) belongs
on a per-document/per-graph state object, or in ImGui per-window storage for widget-scoped latches.
Two graphs (or two canvases) alive in one process expose every static: a ratcheted width shared
through a TU global let one wide graph permanently widen every other graph's layout, and tests
cross-contaminated. The migration tells:

- A function-local `static` holding anything mutable per-frame in editor code is wrong single-canvas
  discipline that "works" only until a second canvas interleaves mid-frame.
- A pointer through a TU-static "current graph" dangles the moment another document replaces it —
  pass the live graph; never read editor pool state through a static.
- If the document type is REFLECTED, session state cannot be embedded by value (it enters the
  reflection decomposition and leaks into schema/serialization): hang it off a lazily created
  POINTER member behind an accessor.

Sanctioned exceptions: derived-cache accessors behind function-local statics (the framework's own
idiom for pure derived data), and crash-path slots (assert/WAL hooks) that must be reachable from a
signal-handler context where no document pointer exists.

---

## Chrome & Interaction

The meta-lesson: **a visible control that does nothing in the default state is worse than no
control** — dead chrome reads as a broken product.

- **No dead chrome.** A control that cannot act in the current state hides or explains itself
  (disabled + tooltip stating what enables it) — never renders inert. Corollary: a control whose
  backing feature is hidden (e.g. a transport scrubbing an invisible target) renders only while its
  target is visible.
- **The hover-flags tell.** Chrome drawn over a canvas engine that uses an inner child window needs
  `ImGuiHoveredFlags_ChildWindows` on its hit-tests — without it every icon is dead chrome *from the
  day it lands*, because the mouse is always over the child. Any draw-list-hit-tested icon over an
  embedded child: check the flag first.
- **The camera belongs to the user.** No implicit centering, ever. Reveals use minimal pan: nudge the
  camera only enough to bring the target inside a small margin; in-view targets never move the camera.
- **Flow order beats category order** in chrome placement: order toolbars by the authoring loop
  (compose → iterate → persist → produce → observe) so the eye walks left→right once per iteration,
  not by verb category.
- **Chrome is opaque.** The canvas never bleeds through UI plates; quietness comes from *value
  contrast*, not translucency. See-through chrome reads as unfinished.
- **Wires read in the visual hierarchy's direction.** Pin sides follow the reading direction of the
  relationship (parent emits right, child receives left), even when the model's port direction is the
  reverse — the model is not the visual grammar.

---

## Audit Method

The lessons that recur across every sweep:

- **Read, don't sample.** A "conforming" verdict on a call site must come from reading it. The one
  decoration asserted coherent without being read was the one still broken. Classify EVERY call site
  of the suspect API by phase/altitude; a checklist pass "by inspection" that skipped a site is a
  claim, not a verification.
- **One verb at a time, verbs × surfaces.** Sweep an interaction family as a matrix (every verb on
  every surface it is reachable from), drive each through the real FSM where possible, and end the
  doc with an explicit OPEN list — empty or not. Negative results ("verified clean") are findings:
  record them so future fixes target only the guilty paths.
- **Refute adversarially.** For each suspected defect, drive it end-to-end before believing it; keep
  a "refuted" section. Half the plausible-sounding findings die on contact with the actual guard
  order.
- **Ask the temporal question, not the topological one.** "Does it depend on the camera" passes
  genuinely mixed-phase writers; "does a T-1 measurement meet a T transform" catches them. And audit
  the WRITE phase separately from the reads — a writer with clean reads can still mutate
  mid-publication (§1c).
- **Estimates must equal measurement or be invisible.** A first-frame per-kind size ESTIMATE that
  differs from the T+1 measured size is a visible settle flicker. Either the estimate is exact, or
  the first submission is measured hidden.
- **Test through the same channel the app uses.** A harness write that a per-frame owner overwrites
  (e.g. setting zoom under a view-state mirror) silently never applies; either drive the app's own
  channel or make the owner accept external writes.
- **Seed-primitive tests don't cover first-frame control behavior.** A control's first OnUpdate (all-
  zero TempData) is its own test surface; exercising the primitives it calls proves nothing about it.

---

## Smell Table (greppable tells)

| Smell | Class |
|---|---|
| `>= 0` or `!= 0` guard on a TempData action selector | zero-value default |
| Real action at enum value 0 / `-1 = none` sentinel | zero-value default |
| Model write between `CanvasBegin` and `CanvasEnd` (`GridPos =`, `AppCanvasSetNodePos`, add/remove/reparent) | phase §1c |
| Last-frame engine pixels combined with this frame's zoom/DPI/font | phase §1 |
| Screen-pos/dimensions read pre-submission for a decoration | phase §1/§5 |
| max/ratchet over a measured value with no deadband | phase §1b |
| "settled"/acceptance state machine over measurements | phase §1b |
| Node-create/import call not followed by the compose/adopt road | creation without adoption |
| Interior/camera coords written to `GridPos` (or root coords to placements) | altitude leak |
| Geometry verb reading root positions while drilled | scope-unaware geometry |
| Discarded `bool` from a mutation function on a gesture path | silent refusal |
| Read-only guard added inside an engine primitive internal machinery calls | gating at wrong layer |
| New id-keyed map with no erase in the removal road | orphan records |
| Function-local `static` mutable state in editor/canvas code | state residency |
| Reads through a TU-static "current graph/canvas" pointer | state residency |
| Draw-list icon over an embedded child without `ImGuiHoveredFlags_ChildWindows` | dead chrome |
| Control rendered while its enabling state is absent, with no tooltip | dead chrome |
| `EditorContextMoveToNode`-style centering on reveal | camera theft |
| Translucent plate over canvas content | chrome opacity |
