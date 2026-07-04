# Usability Findings — 2026-07 field pass

Raw driving-the-editor feedback, each item: complaint → diagnosis → disposition. Fixed items note
the change; open items are the next work, ordered by how hard they burned. The meta-lesson joins
the design principles: **a visible control that does nothing in the default state is worse than no
control** — dead chrome reads as a broken product, and translucency that lets the grid bleed
through chrome reads as unfinished.

## Fixed in this pass

| Complaint | Diagnosis | Fix |
|---|---|---|
| Gizmo column / overlay toolbars non-functional | `AppTreeRowIcon` hover test lacked `ImGuiHoveredFlags_ChildWindows`: the mouse is over imnodes' inner child, the outer-window test was always false — every canvas icon was dead chrome since landing | flag added (both the icon hit-test and the gizmo tooltip test) |
| MMB-only pan | imnodes defaults | LMB-drag on empty canvas pans (three-button emulation, always-on modifier); RMB-drag also pans; a short RMB click still opens context menus (release-with-no-drag disambiguation, `MouseDragMaxDistanceSqr < 9px²`). Box-select lost its LMB binding — Ctrl+click multi-select and `A` remain |
| Click centers the node "weirdly" | outliner-reveal used `EditorContextMoveToNode`, which CENTERS; off-screen test used the node's center point | minimal-pan reveal: nudge the camera only enough to bring the node rect inside a 2-em margin; in-view nodes never move the camera |
| Dead-looking history icon bottom-center | the App-time transport rendered whenever the mirror had recorded frames — with Live hidden it scrubs an invisible app, i.e. does nothing | transport renders only while `ShowLive` |
| Grid lines visible under canvas chrome | overlay plates at 215 alpha by "quietness" | plates near-opaque (252); quietness must come from *value contrast*, not see-through chrome |
| Containment wire reads backwards (child's right → parent's left) | containment pins followed model direction (`ChildOut` on child = output/right, `ChildIn` on parent = input/left), so the wire looped against the visual hierarchy | pin sides swapped for containment: the parent's `children` pin EMITS on its right, the child's `parent` pin RECEIVES on its left — composition now reads parent → child, left → right, DAG-style. Model direction unchanged |

## Open — ordered next work

### 1. Canvas zoom (verdict overturned)
Gen-1 §4 R6 decided against zoom; field use overrules the decision: minimap + scopes do not
substitute. imnodes upstream has no zoom, so this is a **fork feature** in `imguix/imnodes`:
a scale factor in the editor context applied in grid↔screen transforms, `ImFont` scaled text
(or `SetWindowFontScale`), wheel-zoom about the cursor, clamped ~0.3–2.0. Sizeable but contained;
the vendored copy is ours to change. Plan as its own slice before further chrome work — every
canvas feature compounds on navigation.

### 2. Toolbar re-ordered around the core flow (eye path)
Complaint: "I don't know what I'm supposed to do first / next; positioning isn't conducive to the
flow." The toolbar currently orders by *category* (primary action, file verbs, edit verbs, panel
toggles). Reorder by the **authoring loop** so the eye walks left → right exactly once per
iteration:

```
[ Add / Palette ] → [ edit: undo · redo · history ] → [ Save ] → [ Generate ✓/●/⚠ ▾ ] ‖ right: [Code] [Live]
   compose            iterate                           persist     produce                     observe
```

- Generate moves to the END of the left walk — it is the loop's *output*, the thing you do last;
  its health state then also reads as "how did my loop go".
- An explicit Add entry point joins the toolbar (the gizmo `+` and Space palette remain; the
  toolbar teaches the loop's *start*).
- Group captions (tiny dim labels under the clusters, VS-style) name the phases: compose ·
  iterate · persist · produce. Recognition of the flow without a tutorial.

### 3. Bottom panel: from tab pile to output console
Complaint: "incredibly ad-hoc, does not perform its job well." Diagnosis: four tabs with four
different interior grammars, no shared header row, no persistent height memory per tab, Preview
buried. Direction (VS Output/Error-List discipline):

- One shared header row across tabs: context label left (what am I looking at), actions right
  (Copy, Clear, filter) — same slots every tab.
- Output gains severity toggles (err/warn/info) and a search filter — it is the console, make
  it operate like one.
- Code keeps the source map; Diff becomes a Code-panel MODE (toggle in its header), not a clipboard
  dump.
- Preview moves INTO the inspector (it previews the *selection* — it is inspector content at panel
  altitude, which is why it feels lost among document-level tabs).

### 4. Tree sidebar completion
Complaint: "incomplete/awkward." Known gaps: no drag-to-reparent affordance visible, filter buttons
overflow at narrow widths, no per-row context parity with the canvas menu, live/design sections
blend together. Plan: row context menu = canvas node menu (four-roads rule applied to the tree);
kind filter buttons collapse into an overflow popover at narrow widths; a subtle DESIGN / LIVE band
separator; hover reciprocity is already in.

### 5. Inspector ergonomics
"Certain kinds of inspection could be more ergonomic." Concrete candidates (from §5.1's unbuilt
remainder): reset-to-default on rows, multi-select intersection editing, kind icons in section
headers matched to canvas accents, and the W5 quick-inspector (N at cursor) which shortcuts the
cross-screen trip entirely.

## Meta rules adopted (added to the design principles)

1. **No dead chrome**: a control that cannot act in the current state hides or explains itself
   (tooltip stating what enables it) — never renders inert.
2. **The camera belongs to the user**: no implicit centering, ever; reveals use minimal pan.
3. **Flow order beats category order** in chrome placement; the eye path for one loop iteration
   should be a single left-to-right walk.
4. **Chrome is opaque**: the canvas never bleeds through UI plates.
