# Scope-Composition Sweep — findings and status (2026-07-04)

Adversarial sweep (40 agents, 6 lenses, every finding re-verified end-to-end) over every editor
path that ignored the drilled scope. Root mechanism: `AppScopeAdoptNewNode` discarded
`AppGraphReparent`'s refusal (live parents, non-legal kind pairs), and altitude writers mixed
interior coordinates into `GridPos` (or vice versa) against the one-producer-per-altitude
invariant (scope-interior-design.md par.7).

Fix (same day): `AppScopeKindComposable` legality table + `AppScopeComposeNewNode` /
`AppScopeComposeImported` (containment where the pair carries one, interior point into
`ScopePlacements`, root GridPos re-derived at root altitude), palettes filtered to composable
kinds (struct interiors gained Field), live non-layer interiors read-only with notice,
`AppGraphNotify` refusal toasts on the LastLinkErr channel, duplicate copies containment +
captures-by-value (fixed a latent dangling-src reallocation bug), and altitude-routed writers
for tidy / nudge / group drag / explode / fit. Regression tests step41-step46
(tests/imguiapp_nodes_tests.cpp); suite 52/52, imguix-headless-verify green.

Status legend: FIXED (this change) / OPEN (todo) / NO CHANGE (verified, correct or deliberate).

## [FIXED] Add palette writes GridPos from interior click coords (altitude leak) even when adoption succeeds

- severity: medium | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:7602` | category: altitude-leak
- symptom: Drill into a design window, RMB-add a Control at the click point: it appears there in the interior, but back at root the control sits at the interior camera's coordinates, dumped into/over unrelated root layout.

## [FIXED] Add palette adoption silently refused for Struct/Window/Sidebar/CustomLayer and for live scope owners — node vanishes

- severity: high | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:7603` | category: adopt-refused
- symptom: Inside a window scope, RMB-add a Struct (or Window/Sidebar/Custom Layer): nothing appears in the interior; the node materializes at root at the interior click coordinates. Same for adding a Control inside a LIVE window's scope.

## [FIXED] AppGraphDuplicateNode copies no containment and is never adopted — duplicating a scope member yields an invisible clone

- severity: high | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:2056` | category: creation-no-adopt
- symptom: Drill into a window, right-click a hosted control, Duplicate: no new node appears in the interior; the clone sits at root near the source's root position.

## [FIXED] Drop-create data-wire candidates are never composed into the scope and leak interior coords into GridPos

- severity: high | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:8159` | category: creation-no-adopt
- symptom: Inside a window scope, drag a wire from a member control's data pin to empty space and pick 'Struct (producer)' or 'Control (consumer)': the new node never appears in the interior, and at root it sits at the interior camera's drop coordinates.

## [FIXED] Drop-create containment candidates (Field/child Control) get membership but GridPos is written from interior drop coords

- severity: medium | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:8159` | category: altitude-leak
- symptom: Inside a scope, drag from a member struct's ChildIn pin and pick 'Field': the field appears at the drop point in the interior, but at root it renders at the interior camera's coordinates instead of near its struct.

## [FIXED] Paste clipboard (Ctrl+V / palette) imports with no adoption — pasted subtree invisible inside a scope

- severity: high | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:11376` | category: creation-no-adopt
- symptom: Copy a hosted control, drill into its window, Ctrl+V: nothing appears in the interior; the paste lands at root offset (40,40) from the copied root position.

## [FIXED] Prefab instantiate passes interior AddPopupGrid as origin and never adopts

- severity: high | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:7596` | category: creation-no-adopt
- symptom: Inside a scope, RMB > Prefabs > pick one: nothing appears in the interior; at root the prefab lands at its saved positions offset by the interior click coordinates (a double displacement).

## [FIXED] Paste C++ struct(s) creates root-level structs at interior coordinates

- severity: medium | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:10946` | category: creation-no-adopt
- symptom: Inside a window scope, RMB > 'Paste C++ struct(s)': no struct appears in the interior; at root the structs grid out from the interior click point. The Space-palette road (case 22) additionally reuses a STALE AddPopupGrid from whenever the add palette last opened.

## [FIXED] Explode fields / explode control-data position offspring from owner->GridPos, ignoring the owner's interior placement

- severity: medium | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:2172` | category: scope-unaware-geometry
- symptom: Drill into a window, drag a hosted control to an interior-local spot (writes a ScopePlacement), then right-click > 'Explode PersistData' (or expand a struct's fields): the new struct/field nodes appear clustered around the control's ROOT position, detached from where the control actually renders in the interior.

## [FIXED] Promote-to-design never adopts; inside a live window's interior the design twin is invisible

- severity: medium | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:7447` | category: creation-no-adopt
- symptom: Drill into a live window, right-click a live control member, 'Promote to design': no node appears in the interior; the twin sits at root next to the live control's root position (outliner road act 6 behaves identically).

## [FIXED] Empty-scope CTA '+ Control' in a live window/sidebar scope: adoption refused, node spawns invisibly at root, CTA stays

- severity: medium | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:5738` | category: adopt-refused
- symptom: Double-click a live window with no visible members (live drill is allowed at 7074), click the CTA's '+ Control': nothing appears, the 'nothing composed here yet' panel remains, and each click quietly stacks another NewControl at root.

## [FIXED] Cmd palette adds: adoption refused for Struct/Window/Sidebar/CustomLayer; adopted Controls get root open-placement with no ScopePlacement

- severity: high | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:7845` | category: adopt-refused
- symptom: Inside a window scope, Space > 'Add: Struct' (or Window/Sidebar/Custom Layer): nothing appears in the interior. 'Add: Control' does adopt, but the node lands wherever root open-placement put it — typically far from the interior camera and outside the walls.

## [FIXED] Templates reachable from the interior add palette: created nodes are root-level and unadopted (and the wipe can delete the scope owner)

- severity: medium | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:7579` | category: creation-no-adopt
- symptom: Inside a LIVE window's scope, RMB > New from template > 'Window + control': the interior shows nothing new (MainWindow/MainControl are root-level); in a design-window scope the wipe deletes the scope owner itself and the view silently snaps up the scope chain.

## [FIXED] fit_all / fit_ids frame root GridPos while an interior is active

- severity: medium | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:7162` | category: scope-unaware-geometry
- symptom: Inside a scope where members were moved locally (ScopePlacements), press F / 'View: Fit': the camera frames the members' ROOT positions, not where they render, panning to empty space.

## [FIXED] Live-mirror Window/Sidebar scope: even the one legal add (Control) is refused and vanishes

- severity: high | anchor: `C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:11822` | category: adopt-refused
- symptom: Double-click a running window to drill in (double-click at 7074 only enters live nodes, so this is THE mouse path into a window interior), RMB > Control: the new control never appears in the interior and instead shows up at root in the Task layer's domain.

## [FIXED] Struct scope: the palettes offer only illegal kinds and never offer Field, the sole legal adoptee

- severity: medium | anchor: `C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:7540` | category: scope-unaware-ui
- symptom: Inside a struct interior with one or more fields, there is no palette path to add another field (RMB, Space, and toolbar + all lack Field), and picking anything the palette does offer (Control/Struct/Window/Sidebar/Custom Layer) silently vanishes to root.

## [FIXED] Task/Display layer scopes: adoption always refused, masked by domain fallback for matching kinds only -- cross kinds vanish

- severity: high | anchor: `C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:4494` | category: adopt-refused
- symptom: Inside the Task layer scope, adding a Control or Struct appears to work while adding a Window or Sidebar silently vanishes (it lands in the Display domain); inside the Display layer scope the asymmetry flips (Window/Sidebar appear, Control/Struct vanish) -- identical gesture, kind-dependent outcome.

## [FIXED] Command, Status, Layout, and Custom layer scopes are enterable dead ends: every add is invisible by construction

- severity: high | anchor: `C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:4577` | category: adopt-refused
- symptom: Tab or double-click into the Command layer (or Status/Layout/Custom layer), add a Control from any palette: it never appears in the scope (a fresh control emits zero commands) and materializes at root instead; in Status/Layout/Custom scopes literally every addable kind vanishes.

## [FIXED] Root mechanism: AppScopeAdoptNewNode discards AppGraphReparent's result -- refusal has no feedback channel, unlike refused links

- severity: high | anchor: `C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:4632` | category: adopt-refused
- symptom: Every wrong cell in the matrix is silent: the add gesture completes, the popup closes, and the node is simply not there -- no toast, no error, in contrast to refused wire connections which flash a 'link refused' toast.

## [FIXED] Tidy layout (L) while drilled writes the interior arrangement into root GridPos

- severity: high | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:4752` | category: altitude-leak
- symptom: Press L (or the wand gizmo, or palette 'Tidy') inside a drilled scope: nodes with placement records don't move in the interior, but every scope member's ROOT position is silently rewritten to interior-tidy coordinates (origin 80,60).

## [FIXED] Arrow-key nudge writes interior canvas position straight into GridPos

- severity: medium | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:7336` | category: altitude-leak
- symptom: Nudge a node with arrow keys inside a drilled interior: its root-layout position teleports to the interior arrangement's coordinates.

## [FIXED] Group-frame drag while drilled corrupts BOTH altitudes (root-coord drag origin + direct GridPos writes)

- severity: high | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:5988` | category: altitude-leak
- symptom: Drag a group frame's title bar inside a drilled interior: members teleport to their root-layout coordinates plus the drag delta (wrong origin), the root layout absorbs the drag, and the next frame's read-back bakes those root-frame coords into the scope's placements.

## [FIXED] Paste-C++-structs and prefab instantiation import at interior coordinates with no adoption

- severity: high | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:11376` | category: altitude-leak
- symptom: 'Paste C++ struct(s)' or a Prefab from the add palette while drilled: nothing appears in the interior (nodes stay app-level, filtered by AppNodeInScope) and the nodes materialize at root positioned by interior click coordinates.

## [NO CHANGE] Containment-wire auto-place rewrites the child's root GridPos from an interior interaction

- severity: medium | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:3006` | category: altitude-leak
- symptom: Wire a containment edge between two members inside a drilled interior: nothing visibly changes in the interior (the child's placement record wins), but the child's root position is silently relocated next to the parent's root position; at root the same auto-place also discards a child position the user had already chosen.
- status note: write reads root inputs and writes root altitude (coherent); the snap-to-cluster on rehost is the intended idiom

## [FIXED] Command-palette and empty-scope-CTA adds seat at a root free-space scan, unrelated to the interior view

- severity: medium | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:7844` | category: scope-unaware-geometry
- symptom: Add a node from the command palette (or the empty-scope CTA button) while drilled: the node is adopted and visible in the interior, but it appears wherever the ROOT layout's open-placement march found a gap — possibly far outside the interior camera.

## [FIXED] Group-frame drag seeds member origins from root GridPos: cluster teleports on first drag frame in an interior

- severity: high | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:5988` | category: scope-unaware-geometry
- symptom: In an interior with group frames on, rearrange members then drag a control-data or struct group's title bar: all members jump from their interior placements to their root-layout coordinates (+drag delta), and the root layout is dragged along too.

## [FIXED] fit_ids (F key / Frame-selection gizmo) frames root GridPos of selected members

- severity: medium | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:7186` | category: scope-unaware-geometry
- symptom: In an interior, move a member, select it, press F: the camera frames the member's stale root-layout rect; the visible card can end up outside the viewport.

## [FIXED] Explode fields / explode control data anchor new nodes at the owner's root GridPos

- severity: medium | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:2154` | category: scope-unaware-geometry
- symptom: In a window interior, move a control card then explode its Persist/Temp struct or a struct's fields: the new nodes spawn clustered around the owner's OLD root-layout position, potentially nowhere near the visible card.

## [FIXED] Open-placement occupancy test compares against every node's root GridPos regardless of scope

- severity: medium | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:1514` | category: scope-unaware-geometry
- symptom: Creating a node in an interior can drop it exactly on top of a visible member (whose interior placement is ignored) or shove it sideways to dodge an invisible off-scope node's root rect.
- status note: by construction: interior seats are exact placement points now; FindOpenPlacement only ever runs with root-altitude inputs

## [NO CHANGE] Outliner 'Promote to design' (parity path) places the twin from the live node's root GridPos

- severity: medium | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:12509` | category: scope-unaware-geometry
- symptom: While drilled into a scope showing a live control that has been moved in the interior, Promote from the outliner context menu spawns the design twin at the live node's root position +260, not beside the visible card.
- status note: root-altitude GridPos math is correct for an app-level twin; the selection write already ejects to the twin's scope and reveals it

## [FIXED] Outliner Duplicate never adopts into the open scope and never copies containment

- severity: high | anchor: `C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:12490` | category: creation-no-adopt
- symptom: Drilled into window W, click the clone icon (12214) or Duplicate (12007) on a hosted control's tree row: nothing appears in the interior; the copy silently lands at root, unhosted, at the source's ROOT position.

## [FIXED] Duplicate of a Field row creates a node visible at NO canvas altitude

- severity: medium | anchor: `C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:12007` | category: creation-no-adopt
- symptom: Right-click a field row (or its clone icon) -> Duplicate: no node ever appears on the canvas at root or in any interior; an orphan row materializes at the outliner root.

## [NO CHANGE] Tree 'Promote to design' while drilled: twin never adopted, placed with root-altitude GridPos math, and selection points at an unsubmitted node

- severity: medium | anchor: `C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:12509` | category: creation-no-adopt
- symptom: Drilled into a live window's interior, right-click a live control row -> Promote to design: nothing appears in the interior; the design twin lands at root next to the live card and *selected_node_id targets a node the current scope never submits.
- status note: root-altitude GridPos math is correct for an app-level twin; the selection write already ejects to the twin's scope and reveals it

## [NO CHANGE] VERIFIED CLEAN: read-back split, seat path, and layer/section packers honor the altitude invariant

- severity: low | anchor: `imguix/imguiapp/imguiapp_nodes.cpp:6999` | category: other
- symptom: No user-visible defect — negative results establishing which writers are NOT leak sources, so fixes target only the paths above.
- status note: negative result, kept for the record

## [OPEN] Inspector edits inline Draft field lists that are shadowed while exploded — edits invisible below the breadcrumb and destroyed on Collapse

- severity: low | anchor: `C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:4399` | category: other
- symptom: Drill into a struct's scope (its fields exploded into Field nodes — the state that scope exists to show), select the struct, add/edit a field in the inspector's Fields section: nothing appears in the interior or in effective fields, and the entry is silently overwritten when fields are collapsed.
- status note: needs routing of inspector edits to field nodes while exploded; separate mechanism from scope composition
- [ ] TODO: needs routing of inspector edits to field nodes while exploded; separate mechanism from scope composition

## [OPEN] Tree delete of the scope owner: ViewScope pops cleanly next canvas frame; only ScopeCams entries leak

- severity: low | anchor: `C:/dev/imguix/imguix/imguiapp/imguiapp_nodes.cpp:2792` | category: other
- symptom: No user-visible failure found: deleting the drilled scope owner (or an ancestor entry) from the outliner is repaired before the next canvas submission — the view falls back to the surviving prefix with the re-seat + camera-restore path; the only residue is dead g->ScopeCams entries (memory growth, never wrong behavior since ids are never reused).
- status note: memory-only leak: sweep ScopeCams in AppGraphRemoveNode like ScopePlacements
- [ ] TODO: memory-only leak: sweep ScopeCams in AppGraphRemoveNode like ScopePlacements

## [NO CHANGE] test-idiom

- severity: low | anchor: `C:/dev/imguix/tests/imguiapp_nodes_tests.cpp:2890` | category: other
- symptom: Not a bug: this is the test-idiom recipe for writing scope-interior adoption regression tests, distilled from step18/19/37/38/39/40.
- status note: recipe, not a defect

## Refuted during verification

- Drop-create 'Window (host)' from a member control's ChildOut reparents the member OUT of the current scope
  - Refuted end-to-end. A control visible in window A's interior necessarily already has a containment edge to A (AppScopeParentOf 4494-4499 requires AppGraphParentOf == host). The drop-create pick calls AppGraphTryConnect (8161) -> AppGraphResolveLink, whose one-parent guard (2916-2919, 'node already has a parent') rejects any second containment edge from the control's single ChildOut port, so the ed
