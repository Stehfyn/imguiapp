# Composer Workbench Design — Hardening, Refinement, Expansion

Second-generation UI design. The first program ([composer-ui-design.md](composer-ui-design.md))
made six panels into one instrument: lateral ties, viewport chrome, ambient problems — S1 and the
§7 chrome landed. This program makes the instrument into a **workbench**: a place with a settled
spatial grammar, one command vocabulary, inspectors at every altitude of the model, and a viewport
that reads as the subject rather than one panel among peers.

**Design pillars** (the acceptance test for every proposal): consistency, coherency,
communicability, ease of use, intuitiveness. A proposal that cannot name which pillar it serves —
and which Cognitive Dimension it moves — is cut.

## 1. Method

### 1.1 The reference editors and what each is FOR

Each reference contributes its *signature discipline*, not its feature list:

| Editor | Signature discipline | What we take |
|---|---|---|
| **Visual Studio 2022** | Tool-window grammar: every panel summonable, dockable, remembered[^vs-layout]; one Error List; feature search reaches everything[^vs-search] | Panel lifecycle contract (§3.1), command registry + palette (§5.2), status-bar zones (§4.3) |
| **Unreal Editor** | Viewport primacy: the level IS the screen, verbs live on the viewport toolbar[^ue-viewport]; Details panel categories follow the selection[^ue-details]; Compile button carries state[^ue-bp] | Viewport altitude rules (§4.1), primary-action-carries-health (kept), Details-style inspector sections (§5.1) |
| **Unity Editor** | The quartet (Project / Hierarchy / Inspector / Console) with the *component* as the inspector's unit: header, enable checkbox, help, ⋮ menu[^u-inspector][^u-components] | Component-section grammar (§5.1), project-level inspector (§5.3), play-state tinting (§4.4)[^u-prefs] |
| **Blender** | Modes and altitude: N-panel sidebar at the editor it serves[^bl-sidebar], region system[^bl-regions], F9 floating redo[^bl-undo], status keymap[^bl-status], nothing steals the viewport | Quick inspector (§5.4), operation-named undo (§3.4), overlay fade discipline (§4.2) |

### 1.2 Cognitive dimensions, weighted for this generation

Framework: Green & Petre's Cognitive Dimensions of Notations[^cdn]. Gen 1 spent on **visibility**
and **hidden dependencies** (the ties). Gen 2's spend:

- **Consistency** — similar semantics ↔ similar appearance ↔ similar *location*. The workbench
  test: can the user predict where a control lives before looking? If placement needs memory
  rather than rule, the grammar failed.
- **Viscosity** — actions-per-intent. Editing a field on a selected node must never cost a mouse
  journey across the screen (→ quick inspector).
- **Premature commitment** — the palette and context menus must let the user act from wherever
  their attention already is; no "first go arm the right mode".
- **Progressive evaluation** — every state change echoes somewhere glanceable (status zones,
  health chips) without demanding focus.
- **Hard mental operations** — the inspector does unit/type/default bookkeeping so the user never
  computes it (mixed-value states, reset-to-default, live expression checking already dogfoods this).

### 1.3 What "sublime" means operationally

Not decoration. Three testable properties:

1. **Nothing arbitrary.** Every size, color, and position derives from a named rule in the visual
   grammar (§6). An element that needs a bespoke constant is a design bug.
2. **Quiet until relevant.** Chrome rests near-invisible and *earns* salience from state: the
   Generate button is calm when fresh, amber when stale, red when broken — that idiom, everywhere.
   Nothing animates, blinks, or saturates without carrying a state change the user caused or must
   see. (Weiser & Brown's calm-technology principle: information moves between periphery and
   center of attention by relevance, not by shouting[^calm].)
3. **The model is the interface.** The best chrome is the graph itself getting clearer. When a
   feature can live as a property of the canvas (a mark, a halo, a band) instead of a widget, it does.

## 2. Current state (inventory the design builds on)

Landed and assumed by everything below: document toolbar (health-carrying Generate, file/edit
verbs, right-aligned Code/Live toggles) · viewport gizmo column (add/frame/fit/tidy/snap/overlays
popover) · transport overlay (App-time freeze + scrub) · viewport health strip (click → Output) ·
status bar (keymap hints + facts) · bottom panel tabs Code (source-mapped) / Project (doc files) /
Preview / Output (issues + log, brushing) · outliner with filter chips · per-node inspector column
(fields, events, commands, style/color mod descs) · brushing hover sync · ambient problem marks ·
drill-down scopes with breadcrumb · undo/redo rail · prefab registry · time travel.

## 3. HARDENING — finish what exists, then trust it

### 3.1 Panel lifecycle contract (VS tool-window grammar)
Today the bottom tabs, the inspector column, and the outliner are three ad-hoc mechanisms with
three memories (splitter floats in doc data, one-shot `WantOutputTab` flag, `CodeH > 0` meaning
"open"). Replace with one contract every panel signs:

- Identity: stable id, icon, display name.
- State: open/closed, size, last-active tab — **persisted in imgui .ini** via the settings handler
  that is currently a stub (`AppWindowLayerSettingsHandler_*` bodies are empty — this is the
  concrete gap; layout amnesia across sessions is the single most un-workbench behavior we ship).
- Intent API: `RevealPanel(id, payload)` — generalizes the `WantOutputTab` one-shot into the one
  way any subsystem summons any panel (Output click, palette "Show X", future deep links).
  *Pillars: consistency, ease of use. CDoN: viscosity down (no re-opening ritual), premature
  commitment down (any panel reachable from anywhere).*

### 3.2 Four-roads audit, enforced by the registry
Gen 1 stated the rule (every verb: direct manipulation + context menu + palette + shortcut,
menus display the shortcut). Harden it by *construction*: one *command registry* (§5.2) is the
single table context menus, the palette, shortcuts, and status-bar hints all render from. A verb
that isn't registered doesn't exist; a registered verb is automatically everywhere. Known gaps to
close on landing: tidy/frame/fit/hide/isolate/align in the palette; palette itself (§5.2).

### 3.3 Keyboard reach
The canvas has a gesture vocabulary; the panels do not. Minimum: Ctrl+Z/Y (exists), F2 rename
selected, Del delete, arrow-key nudge (grid quantum), Tab/Esc scope drill (exists), Ctrl+P palette,
F focus-selection (exists), Ctrl+S save. Rule: a shortcut acts on the *selection*, never on the
hovered item (hover is for brushing; acting on hover is a mode error the user can't see).
*Pillar: ease of use. CDoN: viscosity.*

### 3.4 Undo, named and complete
- Coverage audit: every model mutation goes through the history (style/color mod edits, event
  edits, sequence nudges — anything found bypassing it is a defect).
- **Operations get names** (Blender's F9 header, VS's "Undo Typing"): the history rail's notches
  tooltip "Add Window 'Mixer'", "Rewire timer_secs", "Style: FrameRounding 3→6". Cost: a label
  argument on the snapshot call. Payoff: the timeline becomes legible history instead of a ruler.
  *Pillar: communicability. CDoN: progressive evaluation.*

### 3.5 Live-mirror write-back seam
The inspector shows live nodes read-only, but style/color descs were built runtime-toggleable.
Harden the seam: live nodes' Style section shows the Active checkboxes ENABLED, writing through to
the running item's `StyleMods/ColorMods` (the one sanctioned live mutation — it round-trips through
the mirror next frame and cannot desync the model, because the mirror IS the model for live nodes).
Everything else stays read-only. This turns the "runtime member" decision into a visible feature:
flip a window's rounding live, watch the app change, no regeneration.
*Pillars: intuitiveness (the checkbox does what checkboxes do), coherency (design nodes author,
live nodes actuate — same rows, honest affordances).*

## 4. REFINEMENT — the viewport as the subject

### 4.1 Overlay altitude table (one law for the canvas's airspace)
Every overlay names its slot; new overlays must claim a vacant one:

| Slot | Occupant | Notes |
|---|---|---|
| Top-left | scope breadcrumb | view state; clickable path |
| Top-right | gizmo column | view verbs only |
| Bottom-left | health strip (+ last log line) | click → Output |
| Bottom-center | transport (App time) | run state; appears only when a mirror exists |
| Bottom-right | minimap | overlay toggle; click/drag jumps |
| Bottom edge (inside) | status hint line | what the mouse does now |
| Cursor | context menu, wire-drop palette, quick inspector (§5.4) | transient, Fitts-optimal |

Rule: corners are *owned*, never shared; a second tenant in a corner stacks into its owner (the
overlays popover pattern), it does not squat beside it.

### 4.2 Overlay quietness (Blender discipline)
- Overlays render at rest ~70% opacity, full on hover of their bounds; the health strip goes full
  only when count > 0. No overlay ever exceeds the nodes' own contrast at rest.
- During canvas drag/wire gestures, non-cursor overlays dim to ~35% (UE's cinematic fade): the
  subject is the graph, chrome yields while the user is *doing*.
- One motion idiom everywhere: 150 ms linear alpha fade. Nothing slides, bounces, or scales.
*Pillar: sublime rule 2. CDoN: visibility of the DATA (chrome is noise in the data's channel).*

### 4.3 Status bar zones (VS discipline)
Freeze the zone map; each zone has one topic and one click action:

`[keymap hint | transient confirmation] ——— [selection breadcrumb] [counts] [mirror] [freshness]`

- Every right-zone fact becomes *clickable where it can act*: breadcrumb segments select, counts
  toggle the corresponding outliner filter chip, mirror fact toggles Live, freshness runs Generate.
  A status bar the user can click is a second command surface at zero pixel cost (VS's branch/
  encoding/line-col widgets). *CDoN: viscosity; pillar: ease of use.*

### 4.4 Run-state tinting (Unity discipline)
When App-time scrub is active (the mirrored app is frozen/rewound), tint the viewport background
2–3% toward the transport's accent and put a thin accent line under the toolbar — Unity's
play-mode tint. Mode-you-must-see, stated ambiently, no modal furniture. Complements the corner
badge from gen 1's T5. *CDoN: visibility of mode.*

### 4.5 Toolbar split-buttons (VS grammar for verb families)
`Save` grows a dropdown half (Save / Save As / Save Copy prefab-set); `Generate` similarly
(Generate / Copy to clipboard / Diff vs saved). Primary click keeps today's behavior; the family
lives behind the split. Removes `Diff` as a peer button (it is a Generate-family member, not a
document verb) — one less top-level item, zero capability lost. *CDoN: role-expressiveness
(family = one control); pillar: coherency.*

## 5. EXPANSION — inspectors at every altitude, one command vocabulary

### 5.1 Component-section inspector (the Unity/UE unit, node level)
Reshape the node inspector from a scroll of headings into **component sections**, each with the
same header anatomy: `▾ icon Name ······ [enable] ⋮`

- Sections by kind: Identity (name/type) · Placement (window/sidebar) · Dock (sidebar) ·
  Fields (Persist/Temp) · Events · Commands · **Style** (the desc rows — whose per-row Active
  checkbox already speaks this grammar; the section-level enable masters it).
- Collapsed state persists per section per kind (not per node — kinds are the schema).
- ⋮ menu: Reset section, Copy section, Paste section (prefab-grade reuse at section granularity —
  copy just the Style of one window onto another; the desc vectors make this trivially value-typed).
- Row grammar unified: label left at fixed fraction, value control right, right-click any row →
  Reset to default / Copy / Paste value (Blender/UE). Mixed-value display for multi-select: dash
  in the control, typing sets all (UE multi-edit). Multi-select inspector shows the *intersection*
  of sections.
*Pillars: consistency (one section anatomy), ease of use. CDoN: viscosity (section copy/paste),
hard mental operations (defaults bookkeeping).*

### 5.2 Command palette + command registry (the VS Quick Launch spine)
One registry entry per verb: id, display name, icon, shortcut, availability predicate, run().
Context menus, the gizmo tooltips, the status keymap hints, and shortcuts all *render from the
registry* (§3.2). The palette (Ctrl+P / Space on canvas) fuzzy-matches over:

- Verbs ("tidy", "generate", "toggle live") — with their shortcuts displayed, teaching them.
- Nodes by name ("mixer" → select + frame) — VS's Ctrl+, symbol search.
- Palette adds ("add window", "add slider control") — merging the wire-drop palette's vocabulary.
- Panels ("output", "project") → RevealPanel.

The palette is the completeness proof: its registry IS the audit list. *Pillars: ease of use,
communicability (it teaches shortcuts); CDoN: premature commitment (act from anywhere).*

### 5.3 Project-level inspector (the missing altitude)
Selection empty → the inspector shows the DOCUMENT, not a void (Unity: nothing; UE: World
Settings; VS: project properties — we take the strong version). Sections in the same §5.1 grammar:

- **Document** — graph path, header path, composition signature, freshness chip (click = Generate),
  node/link/binding counts.
- **Validation** — issue summary by severity; click reveals Output.
- **Composer theme** — the chrome's own desc tables (§6.1), editable: the composer styles itself
  with the machinery it teaches. Dogfooding as UI: a user learns "style mods" by seeing the editor
  wear them.
- **Logging** — WAL level, log path (Project tab keeps the file listing; this is the *controls*).
- **Prefab library** — the registry's names, apply/delete (graduates prefabs from context-menu-only).

Selection altitude now matches the model's: nothing = App, node = component, multi = intersection.
Scope drill sets a *scope* header row atop the inspector (the entered window's identity), so the
inspector always states its subject. *Pillars: coherency, intuitiveness; CDoN: visibility (the
document's own state finally has a home), consistency (same section grammar at every altitude).*

### 5.4 Quick inspector at the cursor (Blender's N-panel, sized to imnodes reality)
`N` (or the node context menu's "Inspect here") opens a floating, pinnable mini-inspector beside
the selected node with the two or three sections that node kind edits most (Control: Fields +
Events; Window: Style; Sidebar: Dock + Style). Same section components as §5.1 — it is a *view*
of the inspector, not a second editor. Dismiss on Esc/click-away unless pinned. Solves the
core viscosity of the layout: canvas on the left of your attention, inspector a full screen-width
away. *CDoN: viscosity; pillar: ease of use. Fitts: the edit happens where the eye already is.*

### 5.5 Layout presets, not workspaces
Full Blender workspaces are more machinery than a single-document editor earns. Instead: three
named layout presets on the View menu / palette — **Compose** (canvas + outliner + inspector),
**Review** (code panel tall, canvas short — the source-map's home), **Observe** (Live on, transport
prominent, Output docked open). A preset is just panel states (§3.1), so this is ~free once the
contract lands, and it names the three actual postures users occupy. *Pillar: ease of use without
new grammar.*

## 6. Visual grammar v2 (extends gen 1 §5 — one table, enforced)

### 6.1 The chrome defines itself in desc terms
`kBlComboColors` / `kBlEditColors` (landed) grow into the complete chrome table: every push-stack
style the composer's own UI uses is a named `ImGuiAppStyleModDesc`/`ImGuiAppColorModDesc` table,
surfaced read-write in the project inspector's Theme section (§5.3). The visual grammar stops
being documentation and becomes data — with Active flags as the rule's own on/off switches.

### 6.2 Interaction-state ladder (every control names its rung)
`rest → hover → active → selected → disabled → mixed`. One palette column per rung; a control
that invents a seventh state or a bespoke hover color is a defect. Draw-list widgets (the Bl
family) and stack-styled widgets read from the same constants.

### 6.3 Spacing and type
Spacing quantum: 0.25 em; sizes only in em multiples (audit the remaining raw-pixel constants).
Type scale unchanged from gen 1 (1.0 / 0.9 / 0.8) — it held.

### 6.4 Depth order, extended to chrome
grid < phase bands < group frames/notes < containment wires < data wires < nodes < node badges <
canvas overlays (§4.1) < transient cursor UI (menus, palette, quick inspector) < toasts/status.
Panels never float above canvas overlays; cursor UI beats everything except the status bar's
error override.

## 7. Delivery slices

| Slice | Contents | Theme |
|---|---|---|
| **W1 — Trust** | §3.1 panel contract + .ini persistence, §3.4 undo coverage + names, §3.3 keyboard reach | hardening: the workbench remembers and never loses work |
| **W2 — Verbs** | §5.2 registry + palette, §3.2 four-roads closure, §4.5 split-buttons | one command vocabulary |
| **W3 — Altitudes** | §5.1 component sections, §5.3 project inspector, §3.5 live write-back | inspectors match the model |
| **W4 — Subject** | §4.1–4.4 overlay law, quietness, status zones, run tint | viewport primacy |
| **W5 — Craft** | §5.4 quick inspector, §5.5 presets, §6 grammar enforcement pass | sublime finish |

W1 first and alone: persistence + undo naming are the credibility features — a workbench that
forgets its layout or its history's meaning cannot feel inevitable, and *inevitable* is what
sublime feels like from the inside.

## 8. Deliberately excluded (decided, not forgotten)

- **Docking framework adoption** for the panels — imgui docking exists, but the Composer's fixed
  quartet + presets (§5.5) covers the real postures; free-docking spends consistency to buy
  flexibility nobody asked for. Revisit only if a fifth panel kind appears.
- **Canvas zoom** — unchanged verdict from gen 1 §4 R6; minimap + scopes + framing still substitute.
- **Toasts/notification center** — the status bar's transient zone + WAL are the two honest
  channels; a third would split attention (VS's own notification hub is a cautionary tale).
- **Per-user theme marketplace ambitions** — the Theme section edits the one built-in table;
  serializing chrome themes waits until someone actually wants to ship one.

## References

Primary sources for the borrowed disciplines. (Gen 1's footnotes — UE Blueprint toolbar, Blender
node editors, Unity Shader Graph blackboard — remain in [composer-ui-design.md](composer-ui-design.md).)

[^vs-layout]: [Customize and save layouts of windows and tabs — Visual Studio 2022 (Microsoft Learn)](https://learn.microsoft.com/en-us/visualstudio/ide/customizing-window-layouts-in-visual-studio?view=vs-2022) — tool windows vs document windows, saved named layouts (§3.1, §5.5). See also the extender-facing [Layout for Visual Studio UX guidelines](https://learn.microsoft.com/en-us/visualstudio/extensibility/ux-guidelines/layout-for-visual-studio?view=vs-2022).
[^vs-search]: [Use Visual Studio search (Ctrl+Q feature search / Ctrl+T code search)](https://learn.microsoft.com/en-us/visualstudio/ide/visual-studio-search?view=vs-2022) — one box reaching features, settings, files, symbols (§5.2).
[^ue-viewport]: [Viewport Toolbar — Unreal Engine documentation](https://dev.epicgames.com/documentation/unreal-engine/viewport-toolbar) — view verbs on the viewport itself; and [Unreal Editor Interface](https://dev.epicgames.com/documentation/unreal-engine/unreal-editor-interface) for the panel taxonomy (§4.1).
[^ue-details]: [Level Editor Details Panel — Unreal Engine documentation](https://dev.epicgames.com/documentation/unreal-engine/level-editor-details-panel-in-unreal-engine) — selection-driven categories, multi-edit, reset-to-default arrows (§5.1).
[^ue-bp]: [Toolbar in the Blueprints Visual Scripting Editor (Epic docs)](https://dev.epicgames.com/documentation/en-us/unreal-engine/toolbar-in-the-blueprints-visual-scripting-editor-for-unreal-engine) — compile-state-as-button (kept from gen 1).
[^u-inspector]: [The Inspector window — Unity Manual](https://docs.unity3d.com/Manual/UsingTheInspector.html) and [Manage the Inspector window](https://docs.unity3d.com/Manual/InspectorOptions.html) — component panels, kebab (⋮) menus, locked/focused inspectors (§5.1, §5.4).
[^u-components]: [Use components — Unity Manual](https://docs.unity3d.com/Manual/UsingComponents.html) — the component as the unit of editing: header, enable checkbox, context commands (§5.1, §3.5).
[^u-prefs]: [Preferences — Unity Manual](https://docs.unity3d.com/Manual/Preferences.html) — Colors ▸ Playmode tint: ambient mode signaling (§4.4).
[^bl-sidebar]: [Sidebar (N panel) — Blender Manual](https://docs.blender.org/manual/en/latest/editors/3dview/sidebar.html) — per-editor settings at the editor, toggled by `N` (§5.4).
[^bl-regions]: [Regions — Blender Manual](https://docs.blender.org/manual/en/latest/interface/window_system/regions.html) — the airspace model: header, toolbar, sidebar, adjust-last-operation as owned regions of one editor (§4.1).
[^bl-undo]: [Undo & Redo — Blender Manual](https://docs.blender.org/manual/en/latest/interface/undo_redo.html) — named Undo History and the Adjust Last Operation (F9) panel (§3.4).
[^bl-status]: [Status Bar — Blender Manual](https://docs.blender.org/manual/en/latest/interface/window_system/status_bar.html) — keymap of the active tool on the left, facts on the right (§4.3; gen 1 T2's source, kept).
[^cdn]: T. R. G. Green & M. Petre, ["Usability analysis of visual programming environments: a 'cognitive dimensions' framework"](https://www.cl.cam.ac.uk/~afb21/CognitiveDimensions/), *Journal of Visual Languages and Computing* 7(2), 1996 — the dimensions vocabulary used throughout (§1.2).
[^calm]: M. Weiser & J. S. Brown, ["Designing Calm Technology"](https://calmtech.com/papers) (Xerox PARC, 1995) — periphery/center attention model behind "quiet until relevant" (§1.3, §4.2).
