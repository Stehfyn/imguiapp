# Node Ontology Migration — runtime conforms to the model's node vocabulary

The Composer's model already calls every composable thing a node (`ImGuiAppNodeKind_App / Layer /
Window / Sidebar / Control`, imguiapp_internal.h:409-422) and already models containment as ports
(`ImGuiAppPortKind_ChildOut/ChildIn`, "I am owned by ..." / "... owns these"). The runtime object
model predates that vocabulary: items, controls, flat vectors. This migration makes the runtime
adopt the model's ontology — one node tree, layers as domains, each layer hosting its own node
kinds — so the mirror becomes an isomorphism instead of a translation.

## Pinned decisions

- **D1 — everything composable is a node.** Windows, sidebars, child windows, task sources: all
  nodes. `AppNodeBegin` (imguiapp_nodes.cpp) draws a node because it IS one; no vocabulary
  collision, the canvas and the runtime share the concept.
- **D2 — Layer is a node; App is the root node.** The model says so already
  (`ImGuiAppNodeKind_Layer`, `_App`). The whole runtime becomes one tree:
  App → Layers → nodes → children.
- **D3 — the type-erased mirror surface lives on `ImGuiAppNodeBase`.** GetDataID / GetFields /
  GetDependency* / GetLiveData move up from `ImGuiAppControlBase`; data-less kinds return
  empty/zero. Tools walk exactly one type.
- **D4 — Task layer hosts EXTERNAL INFORMATIONAL SOURCE nodes only.** Clock, filesystem, network,
  device pollers — the app's nondeterminism boundary. Display nodes are not task nodes; a task
  node never draws. (The Task layer's current job of running the global dependency-sorted update
  walk is a separate concern and moves with phase N3.)
- **D5 — OnDraw leaves the shared base.** Each layer defines its nodes' phase contract; only
  display-node bases declare OnDraw. Kills the inert-OnDraw default permanently
  (imguiapp.cpp:802-807 called OnDraw on free compute controls that never draw).
- **D7 — lifetime and membership are distinct brackets.** OnInitialize/OnShutdown fire once per
  node object; OnAttach/OnDetach (inert defaults on the base) fire on entering/leaving the live
  tree. Push = OnInitialize -> OnAttach; pop = OnDetach -> OnShutdown. Recomposition may
  detach/re-attach without destroying.
- **D8 — StyleMods/ColorMods are display-only.** They live on `ImGuiAppDisplayNodeBase`
  (windows, sidebars, hosted/free controls), not the shared node base: style brackets widget
  submission, and non-display nodes never submit.
- **D6 — push grammar per layer.** `PushAppWindow` / `PushAppSidebar` stay (display).
  `PushWindowControl` / `PushSidebarControl` become the child-window push. Free `PushAppControl`
  dies. `PushAppTask` is born. Model enum appends `ImGuiAppNodeKind_Task` (append-only,
  serialized as int — safe).

## Done

- **N0a** — `OnRender` → `OnDraw` on all interfaces; frame phases `OnDrawFrame` /
  `OnRenderFrame` / `OnPresentFrame` untouched; codegen emitters + checked-in generated
  expectations moved together.
- **N0b** — `ImGuiAppItemBase` → `ImGuiAppNodeBase`; `ImGuiAppWindowBase::Controls` →
  `Children` (element type still `ImGuiAppControlBase*`, generalized in N1). No behavior change.
- **N1** — machinery up the tree. Mirror-surface virtuals moved `ImGuiAppControlBase` →
  `ImGuiAppNodeBase` with inert defaults (D3); `ImGuiAppControlBase` is now a pure type marker.
  `Children`, `app->Controls`, and `UpdateOrder` element type generalized to
  `ImGuiAppNodeBase*`. `ForEachAppControl` → `ForEachAppNode`, `ShutdownAppControls` →
  `ShutdownAppNodes`; no adapter change was needed (Base parameter unchanged).
- **N2** — Layer is a node; display base split out. `ImGuiAppLayerBase` is a marker over
  `ImGuiAppNodeBase`; concrete layers keep OnAttach/OnDetach as membership hooks (D7) and inherit
  inert lifetime hooks from `ImGuiAppLayer`. `ImGuiAppBase` derives the node base (root node,
  hooks inert; frame phases drive it). `ImGuiAppDisplayNodeBase` carries StyleMods/ColorMods (D8);
  WindowBase/ControlBase derive it; display lists (`Children`, `app->Controls`) are display-typed,
  `UpdateOrder` stays node-typed. The shared OnUpdate hook takes a mutable app (layers rebuild
  order/dispatch through it; the control adapter ignores it). All push/pop paths fire the D7
  double bracket. `app->Layers` → `app->Children` (`ImGuiAppNodeBase*`): the root node owns the
  layer list as its children and walks them through the node hooks, no layer-typed access left
  in the frame path. IMGUIAPP_PREVIEW_ABI bumped.

## Phases (each lands green: build + 7 ctest suites incl. style/section/indent ratchets)
- **N3 — collections migrate into their layers.** `app->Windows/Sidebars/Controls` flat vectors
  move into the Display layer node; cross-cutting services (storage registry, dependency-sorted
  update order, WAL, composition ID, state history, mirror walk) iterate the tree. The global
  update walk finds a home independent of the Task layer (D4): update order spans all data
  nodes regardless of hosting layer, exactly as `AppRebuildUpdateOrder` does today.
- **N4 — task nodes.** Task-node base: no OnDraw (D5); Persist/Temp/deps via the same adapter
  stack; an input-capture hook (working name OnPoll) runs where OnDraw's TempData-recording
  half runs today — before OnUpdate consumes, skipped when replay injects — preserving the
  record/replay nondeterminism boundary. `PushAppTask` added; free `PushAppControl` removed;
  pure producers (ImAppTween/Timer/Spring/Pulse, test probes) migrate to task nodes;
  self-windowed free controls become windows. Codegen: `ImGuiAppNodeKind_Task` appended,
  palette legality row, bring-up line, live-mirror kind.
- **N5 — child windows.** Window/sidebar-hosted controls become the display layer's child-window
  node kind. Resolve the sidebar quirk: sidebar-hosted controls currently render their own
  windows OUTSIDE the sidebar's Begin/End (imguiapp.cpp:747) — decide whether that stays a
  sidebar-child contract or becomes plain windows.
- **N6 — Composer parity.** Mirror recursion over Children, scene-hierarchy tree from the real
  node tree, round-trip + prefabs over the new kinds.

## Open questions (deliberately unpinned)

- Dynamic spawn: task nodes creating children at runtime bumps CompositionRevision per spawn and
  clears state history — record/replay degrades under churn. Decide when a driving use case
  exists; until then composition stays push/pop-time.
- OnPoll naming + exact seat (pre-update phase vs display-phase sibling).
- Whether `ImGuiAppSidebarBase : ImGuiAppWindowBase` survives N5 or sidebars become a display
  node kind beside windows.
