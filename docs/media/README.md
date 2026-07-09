# Reference Media — local mirrors

Offline copies of the Design References & Inspiration Library (designs.md §references), organized
for quick referencing:

- `grabs/` — curated editor screenshots at native resolution, named by what they show. Start here.
- `pages/` — full page snapshots: `<name>.html` (stylesheets pulled into `pages/css/`, images
  resolve remotely) with a `<name>.render.png` rendering alongside each.
- `mock/` — our own mocks, same html + rendering pattern.

## grabs/unity

- `lifecycle-flowchart.svg` / `.png` — THE north-star artifact: phase bands as labeled lanes, one
  vertical spine, loop-back edge, grey internal rows, side annotations.
- `editor-full-labeled.jpg` — full editor with Toolbar / Hierarchy / Scene / Inspector / Project labeled.
- `toolbar.png` · `hierarchy-window.jpg` · `inspector-window.jpg` · `project-window.jpg` ·
  `scene-view.jpg` — the quartet, one highlight each.
- `scene-view-3d-nav.jpg` · `scene-view-ortho-vs-persp.jpg` — 3D navigation; orthographic (2D)
  vs perspective framing of the same scene.
- `2d-mode-setting.png` — the Default Behavior Mode = 2D dropdown; `2d-art-styles.jpg` ·
  `2d-game-examples.jpg` — what 2D projects look like.
- `uibuilder-full-labeled.png` — UI Builder (Unity's 2D UI authoring editor), numbered panels:
  the closest Unity analog to the Composer.
- `shadergraph-blackboard.png` — blackboard categories: graph-level property panel.

## grabs/unreal

- `level-editor-full-labeled.png` — full Level Editor (3D), panels numbered.
- `level-editor-selection-details.png` — selection driving the Details panel.
- `menu-bar.png` · `main-toolbar-labeled.png` · `bottom-toolbar-labeled.png` · `outliner-panel.png` — chrome anatomy.
- `viewport-perspective.png` · `viewport-ortho-top.png` · `viewport-ortho-front-wireframe.png` ·
  `viewport-4pane-multiview.png` · `viewport-viewmode-lit.png` · `viewport-actor-gizmo.png` ·
  `viewport-toolbar.png` — 3D + orthographic (2D) viewport vocabulary.
- `umg-designer-full.png` · `umg-designer-canvas.png` · `umg-palette-hierarchy-details.png` ·
  `umg-property-binding.png` — UMG UI Designer (2D UI authoring): palette/hierarchy/canvas/details
  and property binding.
- `blueprint-toolbar.png` · `blueprint-compile-{good,needs,warn,fail}.png` — compile-state-as-button.

## grabs/blender

- `node-editor-header.png` — node editor header row.
- `node-sidebar.png` — N-panel sidebar at the editor it serves.
- `regions-3dview.png` — region anatomy of one editor (the airspace model).
- `status-bar.png` — keymap hints left, facts right.

## pages/

Unity: `unity-execution-order` (north-star source) · `unity-editor-interface` (Unity 6 TOC) ·
`unity-editor-breakdown` (2018 labeled-interface page the quartet grabs came from) ·
`unity-scene-view` · `unity-scene-view-toolbar` · `unity-scene-view-nav` · `unity-2d-mode` ·
`unity-2d-vs-3d` · `unity-uibuilder` · `unity-shadergraph-blackboard`.

Unreal: `ue-editor-interface` · `ue-viewports` · `ue-umg-designer` (quick-start guide; the
UMG-designer overview doc 403s) · `ue-blueprint-toolbar`.

Blender: `blender-node-editors` · `blender-regions` · `blender-status-bar`.

Visual Studio: `vs-window-layouts`.

Note: the UE Details-panel doc page persistently 403'd; its discipline is covered by
`grabs/unreal/level-editor-selection-details.png` and the live link in designs.md §references.

## mock/

- `composer-gen3-endgoal.html` / `.render.png` — gen-3 end-goal visual: the Lifecycle Studio
  (lifecycle-lane root canvas, in-panel DLL preview, body sections in the inspector, console-grade
  Output, flow-ordered toolbar). Companion to the gen-3 design doc.
