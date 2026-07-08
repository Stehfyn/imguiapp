# Composer UI Design — Refinement and Lateral Interaction Ties

Deep-dive design for the Composer's next UI generation. The thesis: **usability does not scale
linearly with feature count — it scales with coherence.** Every feature below either strengthens a
tie between panels or consolidates an existing surface; nothing is additive chrome. References:
Blender (editor-area model, status-bar keymap, outliner sync), Unreal Blueprint editor (wire-drop
palette, comments, alignment, minimap), Unity GraphView (blackboard, groups, minimap).

## 1. Method: the two academic anchors

Two frameworks carry this document. Every proposal names which one it serves; a proposal that
serves neither is cut.

**Cognitive Dimensions of Notations** (Green & Petre, 1996 — built *for* visual-programming
editors). The dimensions we act on:

- **Visibility** — can the user see the state they must reason about, without navigation?
- **Hidden dependencies** — links that exist in the model but not on screen (the killer for graphs).
- **Viscosity** — resistance to small changes (how many actions to rename, rewire, reorder?).
- **Role-expressiveness** — can you tell what a component does by looking at it?
- **Secondary notation** — informal markup (comments, spatial grouping, color) *outside* the
  model's semantics. Graph editors live or die on this; the Composer currently has almost none.
- **Consistency** — similar semantics ↔ similar appearance and similar gesture.
- **Premature commitment** — being forced to decide early (e.g. placing before knowing what to place).

**Brushing and linking / coordinated multiple views** (Becker & Cleveland 1987; Roberts 2007,
"State of the Art: Coordinated & Multiple Views"). The Composer is a coordinated-views system —
outliner, canvas, inspector, code, problems, timeline are six projections of ONE model. The
academic result: coordination (selecting/hovering in one view highlights the same datum in all
views) is what turns N panels from N tools into one instrument. This is the formal name for the
requested "lateral interaction ties."

Supporting cast, used for micro-decisions: Shneiderman's visual information-seeking mantra
(*overview first, zoom and filter, then details-on-demand*), Norman's gulfs of execution/evaluation
(every action needs a visible next step and a visible result), Fitts's law (targets under the
cursor beat targets at the edge; hence context menus and on-wire affordances), Hick's law
(kind-filtered palettes beat full palettes — already dogfooded by the wire-drop palette), and the
Gestalt principles of common region and similarity (group boxes, phase bands, kind accents).

## 2. Shared-state inventory: what the ties synchronize

Coordination is only definable over named state. The Composer's cross-panel state:

| State | Owner today | Notes |
|---|---|---|
| Selection (multi + primary) | `g->Selection` + `*selected_node_id` | already two-way canvas↔tree |
| Hover | per-panel, **not shared** | the biggest missing tie |
| Scope (drill-down) | `g->ViewScope` | tree click navigates it; breadcrumb shows it |
| Camera | imnodes editor context | per-scope memory is on the roadmap |
| App time | `ImGuiAppStateHistory` scrubber | isolated in the toolbar |
| Edit time | undo history (`AppGraphHistory*`) | isolated in the toolbar |
| Problems | `AppGraphValidate` output | listed in a tab; click reveals node |
| Codegen freshness | `AppGraphSignature` | fresh/STALE indicator on the code panel |
| Filter/search | outliner `ImGuiTextFilter` | applies to the tree only |

The lateral-ties program is: **make each row visible in every panel where it is relevant, and
writable from every panel where the user's attention already is.**

## 3. The lateral ties (ranked by coherence-per-cost)

### T1. Hover reciprocity (brushing) — *the* missing tie
Hovering a datum in any view highlights it in all views, within the same frame:

- Outliner row hover → canvas node halo (accent outline, no selection change).
- Canvas node hover → outliner row tint; if scrolled out of view, a 1px accent tick on the tree's
  scrollbar gutter at the row's position (no auto-scroll — scroll theft breaks the tree's own use).
- Wire hover → both endpoint nodes halo + the wire's binding rows tint in the inspector.
- Inspector binding row / dep row hover → the corresponding wire thickens + endpoints halo.
- Problems row hover → node halo (click already reveals; hover should preview *without* commitment
  — CDoN: avoids premature commitment; Norman: closes the evaluation gulf before the action).

Mechanics: one `HoverSync { ImGuiID node_a, node_b; int link_id; }` written by whichever panel
hovers, read by all panels next frame (same one-frame-latency idiom the title-field hover uses).

### T2. Status-bar keymap hints — Blender's signature, and the cheapest teacher
A one-line strip under the canvas showing what the mouse does *right now*, given hover target and
modifier state: over node → `LMB select · drag move · dbl-click rename · Tab enter · RMB menu`;
over pin → `drag wire (release on canvas: filtered add)`; over wire → `drag end rewire · click select · Del
delete`; over canvas → `drag pan · wheel zoom · RMB add · Space palette`. Recognition over recall (Nielsen);
teaches the whole gesture vocabulary passively. The F1 card stays as the full reference; the
status bar replaces it as the *first* teacher. Also the natural home for the last validation error
(replacing the floating link-error fade) and the fresh/STALE indicator.

### T3. Filter co-application — one filter, all views
The outliner search currently filters the tree only. Apply the same active filter to the canvas by
dimming non-matching nodes to ~25% alpha (never hiding — spatial memory must survive; Shneiderman's
*filter* without losing *overview*). Matching nodes keep full opacity + a subtle underline. The
filter button row (kind toggles) already exists in the tree; the same buttons co-apply. One filter
state, two projections — consistency dimension, and it makes search a spatial operation.

### T4. Code ↔ canvas source map — role-expressiveness, uniquely ours
Codegen emits per-node; record `(node id → line range)` while appending (a `ImVector<{int node,
int line0, line1}>` filled in `GenerateAppGraphCode`). Then:

- Selecting a node scrolls the code panel to its emission and tints the range's gutter with the
  node's accent.
- Hovering a code line halos the node that emitted it (T1 extension).
- The diff view (`AppGraphDiffCode`) tags each hunk with its origin node → clicking a hunk reveals
  the node that caused the change.

No other node editor has this tie, because no other node editor's code *is* the ground truth. This
is the Composer's identity feature: the graph and the code are visibly the same object.

### T5. One timeline, two rails
App time (state-history scrubber) and edit time (undo history) are the same concept — *when* —
rendered as two disconnected widgets. Consolidate: a single collapsible timeline strip (toolbar
row) with two thin rails: **Edit** (one notch per undo snapshot, cursor = `AppGraphHistoryCursor`)
and **Run** (state-history ring, cursor = scrub index). Scrubbing either rail shows a corner badge
naming the time you are AT (`edit -3 · run -47f`). Rewinding the app while inspecting a stale
graph is a mode the user must *see* (visibility dimension: mode state must never be ambient).

### T6. Problems as ambient marks, not only a list
Validation issues render where the problem lives, not only in the tab: a small severity dot on the
node's title bar (canvas), a tinted row in the outliner, and an auto-expanded offending section in
the inspector when that node is selected. The list remains the index; the marks remove the
navigation step (visibility; UE does exactly this with compile-error badges on Blueprint nodes).

## 4. Canvas refinement (measured against Blender / UE / Unity)

**R1. Annotation frames (secondary notation — the biggest CDoN gap).** Blender frames / UE
comments / Unity sticky notes all exist because *the model's own containment is never enough*.
Add a `Note` node kind: a resizable, colored, titled rectangle, z-ordered behind nodes, dragging
it moves contained nodes (UE behavior), excluded from codegen and validation. Explicitly
non-semantic — the outliner shows it dimmed, at the bottom, filterable off. Cost: one node kind +
one palette entry; the group title-bar drag machinery already exists.

**R2. Reroute pins (wire secondary notation).** Double-click a wire → elbow node (tiny circle,
one in one out, same type). Purely visual; codegen treats it as pass-through. Long wires across
the layer column are already unreadable in medium graphs; UE ships this for the same reason.

**R3. Alignment and distribution.** Selection ops: align left/right/top/bottom edges, distribute
horizontal/vertical gaps (UE: accelerators on the selection context menu). Complements L (tidy):
L is global policy, alignment is local intent. Shortcut family: `Shift+A` alignment submenu on the
selection context menu — no new top-level keys (Hick).

**R4. Minimap.** Corner inset (Unity GraphView / UE): node rects at 1:~40, viewport rectangle,
click/drag to jump. All rects are already known per frame (`ImNodes::GetNode*`). Combined with F /
Home / scope drill this substitutes for most of what zoom provides (see R6). Draw-list only;
respect the overlay hit-test rule (AllowWhenBlockedByActiveItem, consume-before-canvas).

**R5. Selected-wire flow direction.** On selection only, animate a subtle dash offset along the
wire (UE's bubbles, restrained). Data direction is a hidden dependency for newcomers; always-on
animation is noise (the de-noise pass was right) — showing it *on demand* on the selected edge is
details-on-demand.

**R6. Zoom: name the strategic gap.** Blender/UE/Unity all have canvas zoom; imnodes does not.
Do not fork imnodes for it now. Mitigations already in hand: scope drill-down (semantic zoom —
arguably better than optical zoom for a *compositional* model), F/Home framing, minimap (R4), and
group collapse (LOD by folding). Revisit only if medium-graph navigation still fails after R4+T3
land. Decision recorded here so the gap is chosen, not accidental.

**R7. Node-body LOD** *(parked — not implemented; see the checklist's "Explicitly parked:
per-node LOD").* A collapsed node state (Blender `H` on a node): title bar + pins only,
body folded. Per-node toggle (dbl-click title-bar icon; chevron on hover). The inspector already
holds the long tail; node bodies should identify and wire, not editorialize. Density becomes a
user choice per node — details-on-demand at the node granularity.

## 5. Visual grammar: one table, enforced everywhere

Consistency dimension — similar things must look similar *by rule, not by habit*. Single source of
truth (a `docs/` table now, a style struct in code when it next churns):

| Element | Encodes | Channel |
|---|---|---|
| Node accent (title strip + outliner row + code gutter) | kind (layer type / control / struct / field) | hue |
| Pin | port kind | shape: circle = data, square = containment; fill = wired, hollow = open |
| Wire | edge kind | color inherits pin; containment thinner, behind data wires |
| Badge (sequence number) | execution order | monospace numeral badge, phase-band accent |
| Severity | validation | dot: amber = warn, red = error — same glyph in canvas, tree, problems |
| Dim (25% alpha) | filtered-out / not-in-filter | never for "disabled" — dim means *filtered* |
| Fade (Hidden) | hidden-on-canvas | outliner-only tint; canvas absence |

Typography scale (multiples of em, no free sizes): node title 1.0, port label 0.9, body field 0.9,
meta/badge 0.8. Depth order (fixed): grid < phase bands < group boxes / notes < containment wires
< data wires < nodes < overlay icons < toasts/status. Any new element must name its row and layer.

## 6. Command surface: four roads to every verb

Rule (Blender/UE convention): every operation is reachable by (1) direct manipulation, (2) context
menu at the cursor (Fitts), (3) Space command palette, (4) shortcut — and the palette and menus
*display* the shortcut (recognition trains recall). Audit result — gaps to close: alignment (R3,
new), hide/isolate (context menu ✔, palette ✗, shortcut H ✔), tidy/L (palette ✗), frame/F
(palette ✗), codegen copy (button ✔, palette ✗). The Space palette becomes the completeness
check: **if it's not in the palette, it doesn't exist.**

Transient feedback consolidates onto the status bar (T2): link-rejection reasons, "copied N
nodes", "layout applied" — one place, one fade behavior, no floating toasts scattered per feature.

## 7. Viewport chrome: toolbar, status bar, overlay gizmos

The organizing rule, shared by all three reference editors: **chrome is sorted by what it acts on.**
Document verbs go in the top toolbar (UE Blueprint toolbar: Compile/Save/Find/panels[^ue]); view verbs
go on the viewport itself (Blender's per-editor header + gizmo column, overlays popover[^bl]); panel
toggles right-align (Unity Shader Graph: Blackboard/Inspector/Preview toggles[^un]); status lives in
one bar under everything (Blender: keymap hints left, facts right). A control placed at the wrong
altitude reads as clutter no matter how useful it is — that, not feature count, is what made the old
toolbar feel wrong: Add (view), Tidy (view), Write .h (document) and App time (run) shared one
undifferentiated row, above a second row of read-only pills nothing could interact with.

### Stencil

```
┌────────────────────────────────────────────────────────────────────────────────────┐
│ [⚠/✓/● Generate]  |  Save  Load  Diff  |  ↶ ↷ ══history══     …      [⚠2] [Code] [👁Live] [🕐App time ══] │  DOC toolbar
├──────────┬─────────────────────────────────────────────────────────────────────────┤
│ OUTLINER │  root ‣ MainWindow ‣ Mixer                                    ╭───╮     │
│          │   (scope breadcrumb overlay)                                  │ + │     │  view gizmo
│  filter  │                                                               │ ◎ │     │  column
│  buttons │                    C A N V A S                                │ ⤢ │     │  (overlay)
│  search  │                                                               │ ✨ │     │
│  tree    │                                                               │ ⌁ │     │
│          │                                                               │ ⚙ │     │
│          │                                              ┌─minimap──┐    ╰───╯     │
│          │                                              └──────────┘               │
├──────────┴─────────────────────────────────────────────────────────────────────────┤
│ CODE / PROBLEMS / DIFF panel (toggled)                                              │
├─────────────────────────────────────────────────────────────────────────────────────┤
│ drag move · click select · Tab enter · RMB menu        wrote a.h   sel: Main>Mixer   design 12 live 4   L4 W1 C3 composed │  STATUS bar
└─────────────────────────────────────────────────────────────────────────────────────┘
```

### Document toolbar (one row, left → right)

| Control | State it carries | Why here |
|---|---|---|
| **Generate** (primary) | green ✓ = header matches graph (signature == last written); amber ● = model changed since write; red ⚠ = validation errors (writing stays allowed) | UE's Compile button: the document's health lives ON the primary action, not in a separate lamp. Merges the old strip's "graph ok / cycle" pill into a control that can act on it. |
| Save / Load / Diff | — | file verbs, grouped, after the primary action |
| Undo / Redo / history rail | disabled when unavailable; rail = edit-time scrubber | edit verbs; the rail is T5's Edit rail until the unified timeline lands |
| *(right)* Problems count | count, colored by worst severity; click reveals the panel | UE compile-results pattern; the ambient marks (T6) point where, this counts how many |
| *(right)* Code toggle | lit while open | Unity panel-toggle placement: right-aligned, latched |
| *(right)* Live toggle | eye lit while the mirror shows | view-population toggle used rarely → right cluster, not prime left space |
| *(right)* App time | lit while frozen + frame scrubber | run control (UE's Simulate cluster sits right of the doc verbs) |

Dropped from the toolbar: **Add node** (view verb — gizmo column, RMB, Space), **Tidy** (view verb —
gizmo column, L), the **Show-live checkbox** (became the latched eye toggle: state reads at a glance,
Fitts-cheaper than a checkbox label).

### Viewport gizmo column (top-right overlay, draw-list buttons)

Blender's gizmo column / UE's in-viewport toolbar: view verbs live where the view is, reachable
without leaving the canvas. Top → bottom: **+ add** (opens the kind palette at the viewport center),
**◎ frame selection** (F), **⤢ fit all** (Home), **✨ tidy** (L), **⌁ snap** (G, latched-lit), **⚙
overlays popover** (grid / phase bands / group frames / minimap — Blender's overlays dropdown; lit
when any overlay is off, so a de-cluttered canvas is visibly a *mode*). Every gizmo tooltips its
shortcut (recognition trains recall). Hit-tests follow the overlay rule (AllowWhenBlockedByActiveItem).

### Status bar (window bottom, one row)

Left: the editor's **live keymap hint** — what LMB/drag/RMB do given the current hover target
(node/pin/wire/canvas, live- and layer-aware), computed by the editor, rendered by the host via
`AppGraphStatusHint`; refused-link errors override in red for 3 s. Right, in fixed order: transient
confirmation (`wrote imguix_generated_control.h`) · selection breadcrumb · node counts (design/live/
promoted) · mirrored-app composition (`L4 W1 S0 C3 composed`). Facts only — anything actionable
graduated to the toolbar (that inertness is what made the old strip feel broken: it sat *above* the
viewport in prime toolbar position while affording nothing).

### Deliberately excluded (decided, not forgotten)

- **Zoom % indicator** — imnodes has no zoom (§4 R6); showing one would promise a control that
  doesn't exist.
- **A second header row on the canvas** (Blender's editor header): at the demo's panel sizes one doc
  toolbar + gizmo column covers the verb set; a second strip is chrome tax. Revisit if the Composer
  becomes a multi-editor workspace.
- **Toolbar search field** — arrives with T3 (filter co-application); placing it before it filters
  the canvas would ship a lie.
- **Breadcrumb in the toolbar** — the scope breadcrumb stays a canvas overlay: it is view state and
  clicking it navigates the view; duplicating it in chrome splits one concept across two homes.

[^ue]: [Toolbar in the Blueprints Visual Scripting Editor (Epic docs)](https://dev.epicgames.com/documentation/en-us/unreal-engine/toolbar-in-the-blueprints-visual-scripting-editor-for-unreal-engine) — compile-state-as-button, doc verbs, debug cluster.
[^bl]: [Blender manual: Node Editors](https://docs.blender.org/manual/en/latest/interface/controls/nodes/node_editors.html) — header, snapping toggle, overlays popover, sidebar, status-bar keymap.
[^un]: [Unity Shader Graph: Blackboard / toolbar](https://docs.unity3d.com/Packages/com.unity.shadergraph@12.0/manual/Blackboard.html) — Save Asset primary, right-aligned panel toggles.

## 8. Delivery slices (each shippable, each independently visible)

| Slice | Contents | Serves |
|---|---|---|
| **S1 — Coherence core** | T1 hover reciprocity, T2 status-bar hints (+ error/state readouts), T6 problem marks | brushing/linking, visibility |
| **S2 — Canvas craft** | R1 notes, R3 alignment, R4 minimap | secondary notation, overview |
| **S3 — The identity tie** | T4 code↔canvas source map (select→scroll, hover→halo, diff→node) | role-expressiveness |
| **S4 — Time & filter** | T5 unified timeline, T3 filter co-application | visibility, filter |
| **S5 — Wire & density polish** | R2 reroute pins, R5 flow-on-select, R7 node LOD | secondary notation, DoD |

S1 first: it is the cheapest slice and the one that makes the whole application feel like one
instrument — the property the big three editors share and feature lists don't capture.
