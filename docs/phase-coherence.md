# Phase Coherence — the out-of-phase bug class

Reference page. Read this before writing ANY code that sizes, places, or styles UI from measured
geometry. The Composer regressed on this in 2026-07 while gaining canvas zoom; the post-mortem below
is the canonical example. Related rules live in [usability-findings-2026-07.md](usability-findings-2026-07.md)
and the workbench design docs.

## 1. The bug class, generally

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

## 2. The rules

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

## 3. The canonical example (2026-07 zoom regression)

Canvas zoom was added without forking imnodes: imnodes' grid space = model units x zoom, node content
under a zoomed font, style scalars scaled per frame. Three phase violations shipped with it:

| Violation | Symptom | Fix |
|---|---|---|
| Uniform layer width = `GetNodeDimensions()` (last frame's PIXELS, rendered at the OLD zoom) `/ CURRENT zoom` | every wheel tick flashed wrong layer widths for one frame | cache model width at capture (`px / zoom_used_that_frame`) in the read-back; the consumer multiplies by the current zoom — zoom can no longer de-phase it |
| Pipeline box / phase bands / 1-2-3-4 execution rail drawn from PREVIOUS frame's `GetNodeScreenSpacePos/Dimensions` | decorations lagged the nodes by one frame on zoom (and always had, invisibly, on drag) | decorations compute rects as `canvas_origin + panning + GridPos(model) * zoom_now`, sizes from the model cache |
| Gizmo column / health chip drawn into the parent window's draw list, hit-tested with parent-window items | any node scrolled beneath them OCCLUDED the controls and KILLED their clicks (imnodes' inner child renders above the parent list and wins hover) | viewport chrome draws into the foreground draw list (clipped to the canvas) or is a real overlay window — both z-order and hit-testing stay above canvas content |

Where it lives in code (see the [SECTION] index in `imgui_applayer_nodes.cpp`):
`g_node_model_w/h` + `AppNodeModelSize` (the invariant cache), the capture in the post-submission
read-back, `AppDrawLayerGroupBox` / `AppDrawScopeSequence` (transform-fresh decorations), the gizmo
cluster (foreground list), and the demo's `##canvas_health` / `##canvas_transport` overlay windows.

## 4. Review checklist (apply to every canvas/layout change)

- [ ] Does any value cross a frame boundary? In what units? Are those units transform-invariant?
- [ ] Is anything computed in a render path that OnUpdate should own?
- [ ] Do two places derive the same fact from raw state?
- [ ] Do draw-list decorations read geometry from the frame they draw in?
- [ ] If zoom/DPI/font changed THIS frame, is every pixel this feature emits still correct THIS frame?
- [ ] Is any deliberate T+1 settling in invariant units (no visual jump), and is it commented as such?
