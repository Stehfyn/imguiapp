# Design References & Inspiration Library

Living link library. When a feature borrows a discipline, its source lands here — one line on what
it is FOR, so the link stays useful after the tab is closed. Add freely; prune never (a dead link
still names the idea). Design docs cite these; this file is the union.

## Visual Studio

- [Customize and save window layouts](https://learn.microsoft.com/en-us/visualstudio/ide/customizing-window-layouts-in-visual-studio?view=vs-2022) — tool windows vs document windows; saved named layouts. → panel lifecycle contract, layout presets.
- [Layout UX guidelines (extender-facing)](https://learn.microsoft.com/en-us/visualstudio/extensibility/ux-guidelines/layout-for-visual-studio?view=vs-2022) — the IDE's own rules for what docks where and why.
- [Visual Studio search (Ctrl+Q / Ctrl+T)](https://learn.microsoft.com/en-us/visualstudio/ide/visual-studio-search?view=vs-2022) — one box reaching features, settings, files, symbols. → command palette.
- [Default keyboard shortcuts](https://learn.microsoft.com/en-us/visualstudio/ide/default-keyboard-shortcuts-in-visual-studio?view=vs-2022) — the reference vocabulary for editor keybinding conventions.

## Unreal Editor

- [Unreal Editor Interface](https://dev.epicgames.com/documentation/unreal-engine/unreal-editor-interface) — panel taxonomy: viewport, outliner, details, content browser.
- [Viewport Toolbar](https://dev.epicgames.com/documentation/unreal-engine/viewport-toolbar) — view verbs live ON the viewport. → gizmo column, overlay altitude.
- [Level Editor Details Panel](https://dev.epicgames.com/documentation/unreal-engine/level-editor-details-panel-in-unreal-engine) — selection-driven categories, multi-edit, reset-to-default. → component-section inspector.
- [Blueprint Editor Toolbar](https://dev.epicgames.com/documentation/en-us/unreal-engine/toolbar-in-the-blueprints-visual-scripting-editor-for-unreal-engine) — compile-state-as-button. → Generate button health.

## Unity Editor

- [The Inspector window](https://docs.unity3d.com/Manual/UsingTheInspector.html) / [Manage the Inspector window](https://docs.unity3d.com/Manual/InspectorOptions.html) — component panels, ⋮ menus, locked/focused inspectors.
- [Use components](https://docs.unity3d.com/Manual/UsingComponents.html) — the component as the editing unit: header, enable checkbox, context commands. → section grammar, desc Active checkbox idiom.
- [Preferences (Colors ▸ Playmode tint)](https://docs.unity3d.com/Manual/Preferences.html) — ambient mode signaling. → run-state tint.
- [Script lifecycle flowchart](https://docs.unity3d.com/Manual/execution-order.html) — the north-star Lifecycle view's model (see archive/up-next.md).
- [Shader Graph Blackboard](https://docs.unity3d.com/Packages/com.unity.shadergraph@12.0/manual/Blackboard.html) — Save Asset primary action, right-aligned panel toggles.

## Blender

- [Regions](https://docs.blender.org/manual/en/latest/interface/window_system/regions.html) — the airspace model: header/toolbar/sidebar/adjust-last-op as owned regions of one editor. → overlay altitude table.
- [Sidebar (N panel)](https://docs.blender.org/manual/en/latest/editors/3dview/sidebar.html) — per-editor settings at the editor, toggled by `N`. → quick inspector.
- [Status Bar](https://docs.blender.org/manual/en/latest/interface/window_system/status_bar.html) — active-tool keymap left, facts right. → status bar zones.
- [Undo & Redo / Adjust Last Operation (F9)](https://docs.blender.org/manual/en/latest/interface/undo_redo.html) — named undo history; floating parameter-tweak panel. → named undo notches.
- [Node Editors](https://docs.blender.org/manual/en/latest/interface/controls/nodes/node_editors.html) — header, snapping toggle, overlays popover.
- [Tool System](https://docs.blender.org/manual/en/latest/interface/tool_system.html) — active-tool model vs one-shot operators.

## Theory

- [Cognitive Dimensions of Notations — Green & Petre 1996](https://www.cl.cam.ac.uk/~afb21/CognitiveDimensions/) (*JVLC* 7(2)) — the usability vocabulary every design doc here uses: visibility, viscosity, hidden dependencies, premature commitment, consistency, role-expressiveness, secondary notation.
- [Designing Calm Technology — Weiser & Brown 1995](https://calmtech.com/papers) — periphery/center attention; "quiet until relevant".
- Coordinated multiple views: Becker & Cleveland 1987 (brushing scatterplots); Roberts 2007, ["State of the Art: Coordinated & Multiple Views"](https://ieeexplore.ieee.org/document/4269947) — the formal basis for the lateral ties.
- Shneiderman's mantra — *overview first, zoom and filter, details on demand* ("The Eyes Have It", 1996).
- Norman, *The Design of Everyday Things* — gulfs of execution/evaluation; every action needs a visible next step and result.
- Fitts's law (targets at the cursor beat targets at the edge) · Hick's law (filtered palettes beat full palettes) · Gestalt common region/similarity (group boxes, phase bands, kind accents).

## Design docs using these

- [composer-ui-design](designs.md#composer-ui-design) — gen 1: lateral ties, viewport chrome.
- [composer-workbench-design](designs.md#composer-workbench-design) — gen 2: workbench hardening/expansion.
- [metrics-debugger-coherence-design](designs.md#metrics-debugger-coherence-design) — status/health coherence.
- [node-editor-upgrade-design](designs.md#node-editor-upgrade-design) — canvas/editor upgrade.
