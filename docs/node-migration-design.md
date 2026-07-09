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
- **D9 — containment is generic, dispatch is kind-tagged.** The root owns a flat `app->Layers`
  (layer nodes, stack order); every layer hosts one `ImVector<ImGuiAppNodeBase*> Children` in push
  order. `ImGuiAppNodeKind` lives in imguiapp.h (public vocabulary, D1); the push paths stamp
  `ImGuiAppNodeBase::Kind`; consumers filter by kind — no typed per-kind vectors. Draw order stays
  a display contract (kind-phased: sidebars, windows, free controls), never push order. The
  `app->DisplayLayer` cache survives as the display domain's push-time relationship, seated by
  OnAttach (special-cased on push by design).

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

- **N3** — collections migrated into their layers. First `app->Windows/Sidebars/Controls` moved
  under the Display layer as typed vectors; then D9 replaced them with the single generic
  kind-discriminated `Children` on `ImGuiAppLayerBase`, `app->Children` renamed back to
  `app->Layers`, and `ImGuiAppNodeKind` moved public with `Kind` stamped on every pushed node.
  The global update walk left `ImGuiAppTaskLayer::OnUpdate` (now inert until N4) for the top of
  `UpdateApp` (D4): data nodes update first, spanning every hosting layer, then layers run.
  Pops/drains scan `Children` back-to-front by kind; composition ID hashes layers count + window
  labels + sidebar labels (kind-filtered passes, hash-order preserved); codegen emits
  cast-`Children.back()` accessors and the checked-in goldens match. IMGUIAPP_PREVIEW_ABI bumped.

- **N4** — task nodes. `ImGuiAppTaskNodeBase` (no OnDraw, D5) + `ImGuiAppTask<>` over the shared
  adapter stack via `ImGuiAppNodeMirrorAdapter`; OnPoll records TempData in the Task layer's render
  pass (where display OnDraw records), so replay — which drives UpdateApp only — never runs it.
  `PushAppTask`/`PopAppTask` added; free `PushAppControl`/`PopAppControl` and the display layer's
  free-control draw pass removed; `app->TaskLayer` cache seated by OnAttach. Producers migrated
  (ImAppTween/Timer/Spring/Pulse, test probes, interpreter stand-ins, FlowAccum); the Composer
  control became window-hosted (its own Begin/End dropped). Model: `Kind_Task` first-class —
  unhosted data nodes are tasks (`ImAppNodeKindIsData` generalizes the shared draft/wiring/codegen
  machinery), Task-layer legality row is Task/Struct/Op, controls are hosted-only (validator
  enforces); emitters pick `ImGuiAppTask`/OnPoll vs `ImGuiAppControl`/OnDraw by kind, bring-up
  emits `PushAppTask`, import parses both bases, live mirror seeds runtime task nodes as Kind_Task.
  OnPoll name and its render-phase seat are now pinned (was an open question). IMGUIAPP_PREVIEW_ABI
  bumped; goldens regenerated.

## Phases (each lands green: build + 7 ctest suites incl. style/section/indent ratchets)
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
- Whether `ImGuiAppSidebarBase : ImGuiAppWindowBase` survives N5 or sidebars become a display
  node kind beside windows.
