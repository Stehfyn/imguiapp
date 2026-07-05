# Scope Interior Design — nodes below the composition root

What nodes look like once the user drills into a scope (Tab / double-click / breadcrumb), designed
first for the window-group scope and stated as one rule set that every scope kind applies. Mockup:
the "Scope interior" artifact (window-blue Mixer scope, two hosted controls) — all hues in this
document are the editor's own (`kAppHue*` through `AppThemeAccent`, `imguiapp_nodes.cpp`).

Companion documents: [composer-ui-design.md](composer-ui-design.md) (visual grammar v1, altitude
law), [composer-workbench-design.md](composer-workbench-design.md) (grammar v2, overlay altitude
table), [phase-coherence.md](phase-coherence.md) (every geometry rule below complies; §7),
[up-next.md](up-next.md) (the Lifecycle north star this design advances).

## 1. Diagnosis — the drilled view today

Entering a scope filters which nodes submit and adds sequence badges. Four defects:

1. **The owner evaporates.** Enter "Mixer" and Mixer itself is gone; only breadcrumb text says
   where you are. The room has no walls. *(CDoN: visibility of context.)*
2. **No altitude change in the cards.** A control renders the identical full card at root and in
   scope — only binding rows gate on `altitude_root`. Drilling changes *which* nodes, not *what*
   nodes look like: semantic zoom without the zoom. Root group frames are correspondingly noisy.
3. **Cross-scope wires vanish.** A link whose other endpoint is outside the scope is simply not
   submitted, so the dependency disappears from view — the exact "hidden dependencies" failure the
   design docs name as the killer for graph notations.
4. **The sequence is annotation, not structure.** Badges and dashed arrows float over free-placed
   cards; the scope caption *says* "push order between the host's Begin/End" but nothing shows
   Begin or End.

## 2. The rules

### Rule A — the owner's silhouette becomes the room (walls)

The scope frame reuses the owner's root-level card silhouette at wall size. For a window scope:
squared corners (rounding 2 model units), a 1 px header rule in the window hue under the title
bar, neutral outline, translucent fill one step brighter than a root group frame
(window hue at ~4.5% alpha vs the neutral `GroupFill`). Title bar anatomy, left to right:

- **Name** in the owner's kind hue, weight as a node title (1.0 em scale slot).
- **Kind word** dim, 0.9 slot (`window`, `sidebar`, `Task`, …) — same muted kind word the card
  title bar carries.
- **Config readout**, right-aligned, dim, monospace: the owner's placement/dock facts
  (`320×240 @ (64,48) · AlwaysAutoResize`; a sidebar shows `dock Down · 0 px · AutoResize`).
  Read-only — editing stays in the inspector; the wall states identity, it does not editorialize.

The wall rect derives from the members' bounds (the `group_box` accumulation) plus padding, in
model units, transformed by this frame's camera. Entering a node reads literally: the card you
Tab'd into became the walls. *(Pillar: consistency — same silhouette at both altitudes. CDoN:
visibility. Serves defect 1.)*

### Rule B — lifecycle brackets *(REMOVED — field verdict 2026-07-04)*

Begin("name")/End() plates shipped and were cut the same day: in practice they read as unexplained
chrome floating over the walls ("whatever this is"), duplicated the wall title bar's identity, and
restated what the breadcrumb caption already says in words. The lifecycle-chart direction stays
(up-next.md north star) — but as its own view, not as furniture inside every scope. The rail
remains member → member with no endpoint segments; the sequence badges alone carry execution
order. *(Meta-rule from usability-findings: chrome that needs a legend is worse than no chrome.)*

### Rule C — the execution rail

Today's dashed cubic arrows + slate-marker number badges (`AppDrawScopeSequence`), unchanged in
style — drawn on the canvas annotation channel so badges are never occluded by the nodes they
number. The rail is the affordance the planned sequence-order editing (up-next.md "Now") will
snap to: dragging a badge along the rail reorders the push. This design does not implement
reordering; it builds the geometry reordering needs.

*(CDoN: role-expressiveness — the scope reads as a frame slice, not a bag of cards.)*

### Rule D — detail lives one scope below its owner (density flip)

One sentence of law, generalizing the existing altitude gates (structs/fields below root,
bindings below root): **a node shows its full authoring body only when the current scope is its
scope-parent** (`AppScopeParentOf`). Everywhere else it shows an identity card.

| Altitude | Control card contents |
|---|---|
| identity (root, inside its group frame; any foreign scope) | title bar (name, kind word, origin dot, severity dot) · deps pin row with wired producer names · DataOut row · **one dim summary line**: `2 fields · 1 event · emits SetLevel` |
| detail (inside its host's scope) | identity content **plus** PersistData/TempData rows with inline field editors and current values, tie-pin explode disclosures, event rows (`when dragging ^ → set level`), command chips, per-edge binding editors (`from RandomTime: timer_secs ← max_timer_secs`) |

Same card width (`UniformCardW`), same rounding, same title anatomy at both altitudes — only the
depth of content changes, so spatial memory survives the flip. Wires land identically. The R7
per-node LOD toggle (composer-ui-design.md) remains as a manual override on top. *(CDoN:
details-on-demand at the scope granularity; de-noises root for free. Serves defect 2.)*

### Rule E — boundary portals (the walls take the cross-scope wires)

A data link with exactly one endpoint inside the scope docks a **portal chip** on the wall:

- **Inbound** (outside producer → inside consumer): chip on the LEFT wall at the consumer's
  deps-row height: `▸ RandomTime`. Wire runs chip → the consumer's real pin, normal data-wire
  styling.
- **Outbound** (inside producer → outside consumer): chip on the RIGHT wall at the producer's
  DataOut-row height: `db ▸ Peaks` (source field if a binding names one, else the producer's
  data name, then the consumer).
- Chip form: pill (fully rounded), 1 px border in the *remote* node's kind hue mixed ~45% toward
  the neutral line color, text in the same hue at ~75%, dark plate fill, 0.9 em text slot. Dim
  relative to real nodes — a chip is a reference, not a resident.
- **Hover**: existing brushing applies — halo the inside pin and tint the remote node's outliner
  row. **Click**: jump — `ViewScope` becomes the remote node's scope-parent chain, the remote
  node becomes the selection, minimal-pan reveal (the camera-belongs-to-the-user rule).
- Chips are rebuilt every frame from `Links` + the current scope. No ids, no persistence, no
  model records, no codegen — pure derived presentation, exactly like the trunk connectors.

*(CDoN: hidden dependencies — eliminated at the wall. Blender group-input/output sockets are the
model, minus their fake-node materialization. Serves defect 3.)*

## 3. Visual spec (grammar rows; extends composer-ui-design.md §5)

| Element | Encodes | Form | Color |
|---|---|---|---|
| Walls | current scope's owner | owner's silhouette at room size; title bar + header rule | kind hue: rule + name; fill = kind hue ~4.5% alpha; outline neutral |
| Rail segment | execution order | dashed cubic, arrowhead | scope accent ~55% alpha (unchanged) |
| Sequence badge | member's position in the sequence | slate-marker circle on card corner | scope accent fill, dark numeral (unchanged) |
| Portal chip | off-scope endpoint of a data edge | wall-docked pill, `▸` jump glyph | remote kind hue at ~45% border / ~75% text |
| Summary line | folded authoring detail | one dim text row | `TextMuted` |

Depth order (slots from workbench §6.4): walls sit in the group-frame slot (behind containment
wires, in the background draw list); rail + badges + chips render on the canvas annotation
channel (the child's post-merge list, clipped to the editor): above every node, never above
other windows. Typography: existing scale only (1.0 / 0.9 / 0.8 em);
spacing in 0.25 em quanta; no new constants outside the style table.

## 4. Per-scope-kind application

| Scope | Members (density) | Rail order |
|---|---|---|
| Window / Sidebar | controls (detail) | push order |
| Display layer | windows + sidebars (identity + hosted count) | render order: windows pass, then sidebars |
| Task layer | controls (detail for app-level controls whose scope-parent is the Task layer) | dependency (topo) order |
| Command layer | emitter controls (identity + command chips) | push order |
| Control | Persist/Temp struct plates + fields (already below-root only) | none (data domain) |
| Struct | field pills | none |

Window scope ships first: smallest surface, and it is where hosted-control authoring actually
happens.

## 5. Root-side consequence

The density flip is the only rule that changes the root view: controls inside group frames
collapse to identity cards, so a root composition of N windows scans as N labeled clusters of
title bars — the overview altitude the altitude law always claimed. Group frames, trunk
connectors, section packing, phase bands: unchanged.

## 6. Rejected alternatives

- **Owner as a giant node in-scope.** A node takes selection, drag, pins, deletion — none of
  which the scope owner can honor from inside itself. Walls are furniture with one interactive
  element (nothing), plus the existing breadcrumb for navigation.
- **Portals as real graph nodes** (Blender's group input/output nodes, UE tunnels). Our graph is
  a 1:1 mirror of the runtime object model; synthetic nodes would leak into validation, codegen,
  persistence, and the outliner, or need special-casing in all four. Chips are draw-list
  presentation of existing `Links` rows.
- **Fixed lane layout in-scope** (members force-packed along the rail). Free placement + rail
  preserves spatial memory and keeps the scope-local tidy verb (up-next.md) as the on-demand
  arranger. The rail suggests order; it does not own positions. (Window-section semantics stay
  root-only.)
- **Config editing in the wall title bar.** The inspector owns editing; a second editor surface
  in chrome splits one concept across two homes (same argument that kept the breadcrumb out of
  the toolbar).
- **Hiding cross-scope wires with a count badge** ("+2 external") instead of chips. Cheaper, but
  it states that dependencies exist without saying which — the hidden-dependency defect at lower
  resolution.

## 7. Phase-coherence compliance (checklist applied at design time)

- Walls: bounds from engine positions (submitted) / this scope's model placements + THIS frame's
  camera — the same transform-fresh path group frames use today. Published (sole producer;
  consumers read the published rect same-frame).
- Portal chips: derived every frame from `Links` + scope — no caches, no feedback loop. Chip
  anchor heights read pin rows from this frame's read-back geometry, drawn post-`CanvasEnd`
  (rule 5: post-submission reads are coherent).
- Density flip: a pure predicate on model state (`AppScopeCurrent == AppScopeParentOf`); the
  card's measured size changes exactly when content changes — the framework's documented
  content-driven T+1, in invariant units, no visual artifact.
- Scope-local placement (`ScopePlacements`): the interior read-back writes the drilled scope's
  records, the root read-back writes `GridPos` — one producer per altitude, no leak in either
  direction. Serialized as `Place=` lines.
- Hit-tests: chips follow the overlay rule (`AllowWhenBlockedByActiveItem` over the canvas child).
- Draw altitude: all in-scope annotations use `CanvasAnnotationDrawList` — above the merged canvas
  channels, inside the editor's z-order (never over other windows).
