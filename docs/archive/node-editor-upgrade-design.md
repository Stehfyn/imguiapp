# Node Editor Upgrade — Authoring Layers, Windows, Sidebars, Controls & Data Flow

## 1. Summary & goals

Today the editor authors one pillar (draft *controls*) with semantically inert links
(`ImGuiAppNodeLink{Id,StartAttr,EndAttr}`, raw imnodes attr ids), index-derived ids that corrupt on
delete (`demo.cpp:285-286,309`), and per-draft codegen that emits no dependencies (`nodes.cpp:509-511`).
This upgrade makes the graph a **1:1 mirror of the `ImGuiApp` runtime object model**: five node kinds
(App/Layer/Window/Sidebar/Control), two edge kinds (containment + data-dependency), typed struct-level
ports, and a single monotonic id allocator so identity survives reorder/delete. Codegen becomes
whole-graph: it emits the data structs, derives each control's `DataDependencies` from incoming data
edges, and emits `Push*` bring-up in Kahn topological order so every dependency's `InstanceData` exists
in `app->Data` before its consumer's `OnInitialize` resolves it (`imguiapp.h:397`). Authoring all
four pillars buys a *complete, compilable app skeleton* from the canvas instead of isolated control stubs.

**Spine:** the *object-model-fidelity* frame (mirror the runtime hierarchy, single `NextId`, embed
`ImGuiAppNodeDraft` for helper reuse, Kahn topo codegen). **Grafted in:** the typed-port frame's
`ImGuiType<>::ID` bridge and exact-type compatibility gate, plus an optional per-edge **field-binding**
list that recovers the `data->dst = dep->src;` codegen without field-level pins. **Deferred to a
clearly-scoped later phase:** the live-introspection frame's two reflect-free virtuals (live mirror +
design→live promotion). Every idea a judge flagged is dropped or explicitly fixed in §9.

## 2. Node & edge model

All structs are POD/`ImVector`-shaped and imnodes-free (header stays imnodes-free, constraint 5). New enums:

```cpp
typedef int ImGuiAppNodeKind;
enum ImGuiAppNodeKind_ {
  ImGuiAppNodeKind_App = 0,   // singleton root; owns Layers/Windows/Sidebars/Controls (imguiapp.h:357-366)
  ImGuiAppNodeKind_Layer,     // FIXED C++ type (Task/Command/Status/Window) -> PushAppLayer<T>
  ImGuiAppNodeKind_Window,    // ImGuiAppWindow<T>  -> PushAppWindow<T>
  ImGuiAppNodeKind_Sidebar,   // ImGuiAppSidebar<T> -> PushAppSidebar<T>(app,vp,dir,size,flags)
  ImGuiAppNodeKind_Control,   // ImGuiAppControl<...> -> PushAppControl<T>   (draftable)
  ImGuiAppNodeKind_COUNT,
};
typedef int ImGuiAppLayerType;   // the 4 fixed layer classes (imguiapp.h:312-342)
enum ImGuiAppLayerType_ { ImGuiAppLayerType_Task=0, ImGuiAppLayerType_Command,
                          ImGuiAppLayerType_Status, ImGuiAppLayerType_Window, ImGuiAppLayerType_COUNT };
typedef int ImGuiAppPortKind;
enum ImGuiAppPortKind_ {
  ImGuiAppPortKind_DataOut = 0, // Control: provides its PersistData (one)
  ImGuiAppPortKind_DataIn,      // Control: consumes deps (ONE pin, multi-link)
  ImGuiAppPortKind_ChildOut,    // Layer/Window/Sidebar/Control: "I am owned by ..."
  ImGuiAppPortKind_ChildIn,     // App/Window/Sidebar: "... owns these"
  ImGuiAppPortKind_COUNT,
};
typedef int ImGuiAppEdgeKind;
enum ImGuiAppEdgeKind_ { ImGuiAppEdgeKind_Data = 0, ImGuiAppEdgeKind_Containment, ImGuiAppEdgeKind_COUNT };
```

Ports are **stored records** (not index-derived), each carrying the PersistData type hash it represents for data flow:

```cpp
struct ImGuiAppNodePort {
  int              Id;          // from ImGuiAppGraph::NextId; == imnodes attribute id
  ImGuiAppPortKind Kind;
  char             Name[IM_LABEL_SIZE];
  ImGuiID          DataTypeId;  // == ImGuiType<PersistData>::ID for DataOut/DataIn (§3); 0 otherwise
};
```

The node **embeds** the existing `ImGuiAppNodeDraft` rather than duplicating its fields, so
`BeginAppNodeRenamable`, `EditAppNodeDraftFields`, `AppEmitFieldDecl`, and `GenerateAppControlCode` keep
working verbatim and one legacy `[Draft]` section maps to one Control node (§6 migration):

```cpp
struct ImGuiAppNode {
  int                        Id;            // from NextId; == imnodes node id
  ImGuiAppNodeKind           Kind;
  ImGuiAppNodeDraft          Draft;         // Draft.Name = label; PersistFields/TempFields used only for Control
  bool                       IsBuiltin;     // palette node backed by a compiled C++ type
  char                       TypeName[IM_LABEL_SIZE];      // C++ type to Push<> (e.g. "StatusBar","ImGuiAppWindowLayer")
  char                       DataTypeName[IM_LABEL_SIZE];  // builtin control's PersistData type; empty => "<Name>Data"
  ImGuiAppLayerType          LayerType;     // Layer nodes
  bool                       HasInitialPlacement; ImVec2 InitialPos, InitialSize; // Window/Sidebar (h:279-281)
  ImGuiDir                   DockDir; float DockSize; ImGuiWindowFlags Flags;      // Sidebar (h:286-287)
  ImVec2                     GridPos; bool HasGridPos; bool _NeedsPlace;           // persisted canvas layout
  int                        BodyAttrId;    // dedicated non-port attribute id for EditAppNodeDraftFields body
  ImVector<ImGuiAppNodePort> Ports;
};
```

**Containment vs data edges.** The existing link struct stays a pure-scalar aggregate (so step8's
`ImGuiAppNodeLink l = { 1, 10, 11 };` at tests:358 keeps compiling) — add only a trailing `int Kind`
*with a default member initializer*, which both makes `{1,10,11}` value-init `Kind` to `_Data` **and**
fixes `CaptureAppNodeLinks`'s uninitialized `ImGuiAppNodeLink created;` (nodes.cpp:250) in one stroke
(default member initializers are legal in aggregates since C++14):

```cpp
struct ImGuiAppNodeLink {
  int Id; int StartAttr; int EndAttr;       // unchanged: stable PORT ids (producer -> consumer)
  ImGuiAppEdgeKind Kind = ImGuiAppEdgeKind_Data;   // NEW, trailing, defaulted -> aggregate preserved
};
```

The graph aggregate hoists the demo's loose statics (`drafts/links/next_link_id`, demo.cpp:397-399) into
one owned object. **Field bindings live on the graph, NOT on the link** — putting an `ImVector` on
`ImGuiAppNodeLink` would make it a non-aggregate and break step8:

```cpp
struct ImGuiAppFieldBinding {                // optional: one assignment line in codegen
  int  LinkId;                               // owning data edge
  char DstField[IM_LABEL_SIZE];              // consumer PersistData field
  char SrcField[IM_LABEL_SIZE];              // provider PersistData field
};
struct ImGuiAppGraph {
  ImVector<ImGuiAppNode>          Nodes;
  ImVector<ImGuiAppNodeLink>      Links;
  ImVector<ImGuiAppFieldBinding>  Bindings;
  int NextId;        // monotonic allocator for ALL node/port/body-attr/link ids
  int EditingNodeId; // was demo.cpp:281 static
  ImGuiAppGraph() { NextId = 1; EditingNodeId = -1; }
};
```

Per-kind mandatory ports (stamped by `AppGraphAddNode` at creation):

| Kind | Ports | Accepts (target) | Emits (source) |
|---|---|---|---|
| App | ChildIn | Containment from Layer/Window/Sidebar | — |
| Layer | ChildOut | — | Containment → App |
| Window | ChildOut, ChildIn | Containment from Control | Containment → App |
| Sidebar | ChildOut, ChildIn | Containment from Control | Containment → App |
| Control | DataOut, DataIn(multi), ChildOut | Data into DataIn | Data → Control; Containment → Window/Sidebar/App |

**One multi-link `DataIn`, not N ordered pins** — faithful to the runtime:
`app->Data.GetVoidPtr(ImGuiType<Dep>::ID)` (h:397) is keyed by PersistData *type*, so a control cannot
depend on the same type twice and two same-typed deps are indistinguishable. One type-keyed intake
collecting N distinct-typed producers matches reality and keeps port count fixed.

## 3. Stable id & port scheme

**Root-cause bug being fixed:** ids derived from array index (`node_id = NODE_DRAFT_BASE+i`,
`attr_id = ATTR_DRAFT_BASE+i`, demo.cpp:285-286) — deleting draft *i* renumbers every later node/port and
silently re-targets links; the demo even re-places nodes "so ids shift cleanly" (demo.cpp:309). That is
the failure constraint 4 forbids.

**Fix — single monotonic allocator, resolve by search never by index:**
```cpp
int AppGraphAllocId(ImGuiAppGraph* g) { return g->NextId++; }
```
- imnodes node id = `node->Id`; attribute ids = each `port->Id` **and** the node's `BodyAttrId` (every
  node gets a dedicated body attribute id from `NextId` so `BeginStaticAttribute` for
  `EditAppNodeDraftFields` always has a valid, non-port id); link id = `link->Id`. One allocator ⇒
  globally unique, no fixed ranges, no collision math.
- Links store `StartAttr/EndAttr = stable port ids`. Resolution is `AppGraphFindPort(g, id, &owner)` by
  linear search — **never** by index. Ids retire on delete and are **never reused**. `AppGraphRemoveNode`
  erases the node and sweeps every incident link (and any `Binding` whose `LinkId` is gone). Surviving
  nodes/ports/links are bit-for-bit unchanged.
- `NextId` is serialized (§6) so ids stay stable across save/load.

**Data-flow id mapping.** A `DataOut`/`DataIn` port's `DataTypeId` is the PersistData type hash the
runtime keys on. The design recomputes the *exact* runtime value via a local mirror of `_ConstantHash`
over the scope-stripped display name (verified at imguiapp.h:143-148,186-189; for a global
`struct FooData`, `ImGuiType<FooData>::Name == "FooData"`):
```cpp
static ImGuiID AppConstantHash(const char* s){ return *s ? (ImGuiID)*s + 33u*AppConstantHash(s+1) : 5381u; }
ImGuiID AppNodeStructTypeId(const char* node_name){
  char id[IM_LABEL_SIZE]; AppSanitizeIdentifier(id, sizeof(id), node_name);  // reuses nodes.cpp:455
  ImStrncatf(id, sizeof(id), "Data");                                        // codegen appends "Data"
  return AppConstantHash(id);
}
```
(Identifiers are ASCII so `char` signedness is moot.) A builtin control node sets
`DataTypeId = AppConstantHash(DataTypeName)`; a drafted node sets it from `AppNodeStructTypeId(Draft.Name)`
and *recomputes on rename*. This is the bridge that makes a design DataOut pin carry the same id the
emitted control will register under in `app->Data` — and what enables design→live promotion in the
optional live phase.

## 4. Public API additions

All in `namespace ImGui`, pointer params, no references (per `feedback_no_references.md`),
`char[]`+`ImVector`, imnodes confined to `.cpp`. Existing signatures (`GenerateAppControlCode`,
`CaptureAppNodeLinks`, the four `Save/Load*`) are **kept verbatim** — tests depend on them.

```cpp
// allocation / factory
IMGUI_API int             AppGraphAllocId(ImGuiAppGraph* g);
IMGUI_API ImGuiAppNode*   AppGraphAddNode(ImGuiAppGraph* g, ImGuiAppNodeKind kind, const char* name);
IMGUI_API ImGuiAppNode*   AppGraphAddBuiltin(ImGuiAppGraph* g, ImGuiAppNodeKind kind,
                                             const char* type_name, const char* data_type_name);
IMGUI_API void            AppGraphRemoveNode(ImGuiAppGraph* g, int node_id);   // sweeps links + bindings
IMGUI_API ImGuiAppNode*   AppGraphFindNode(ImGuiAppGraph* g, int node_id);
IMGUI_API ImGuiAppNodePort* AppGraphFindPort(ImGuiAppGraph* g, int port_id, ImGuiAppNode** out_owner);
IMGUI_API ImGuiID         AppNodeStructTypeId(const char* node_name);

// typed edges (supersedes inert CaptureAppNodeLinks for the graph)
IMGUI_API bool AppGraphCanLink(const ImGuiAppGraph* g, int start_port, int end_port, char* err, int err_size);
IMGUI_API bool CaptureAppGraphLinks(ImGuiAppGraph* g, char* err, int err_size);  // folds + validates

// optional per-edge field assignment
IMGUI_API void EditAppDataEdgeBindings(ImGuiAppGraph* g, int link_id);   // inline UI, inside DataIn attr

// render whole typed graph
IMGUI_API void ShowAppGraphEditor(ImGuiApp* app, ImGuiAppGraph* g);

// codegen + topo
IMGUI_API bool AppGraphTopoOrder(const ImGuiAppGraph* g, ImVector<int>* out_control_ids, char* err, int err_size);
IMGUI_API void GenerateAppGraphCode(const ImGuiAppGraph* g, ImGuiTextBuffer* out);

// persistence (NEW; the four legacy fns untouched)
IMGUI_API bool SaveAppGraph(const char* path, const ImGuiAppGraph* g);
IMGUI_API bool LoadAppGraph(const char* path, ImGuiAppGraph* g);   // reads [Node] AND legacy [Draft]
```

`ShowAppGraphEditor` body (in `.cpp`, honoring every imnodes rule):
```cpp
ImNodes::BeginNodeEditor();
for (n : g->Nodes){
  if (n->_NeedsPlace){ ImNodes::SetNodeGridSpacePos(n->Id, n->GridPos); n->_NeedsPlace=false; } // BEFORE BeginNode (demo.cpp:236)
  ImGui::BeginAppNodeRenamable(n->Id, n->Draft.Name, IM_ARRAYSIZE(n->Draft.Name), &g->EditingNodeId);
  for (p : n->Ports) emit Begin{Input|Output}Attribute(p->Id) + label;       // pins
  if (n->Kind==Control){ ImNodes::BeginStaticAttribute(n->BodyAttrId);        // dedicated body id
                         ImGui::EditAppNodeDraftFields(&n->Draft);
                         EditAppDataEdgeBindings(g, /*incoming link*/);        // widgets INSIDE an attribute (rule 2c)
                         ImNodes::EndStaticAttribute(); }
  else { ImNodes::BeginStaticAttribute(n->BodyAttrId); /* kind-specific config rows */ ImNodes::EndStaticAttribute(); }
  ImGui::EndAppNode();   // body always submits >=1 item -> no "extend boundaries" assert (imnodes empty body)
}
ImGui::DrawAppNodeLinks(&g->Links);
ImNodes::EndNodeEditor();
for (n : g->Nodes){ n->GridPos = ImNodes::GetNodeGridSpacePos(n->Id); n->HasGridPos = true; }   // layout persists
ImGui::CaptureAppGraphLinks(g, err, sizeof err);
```

`AppGraphCanLink` resolves both endpoints and enforces: DataOut→DataIn ⇒ Data edge; ChildOut→ChildIn ⇒
Containment (with legal kind pairing); reject self/duplicate; for Data edges reject if the consumer
already consumes the producer's `DataTypeId`, and reject if accepting would make `start` reachable from
`end` (cycle guard, §5). imnodes has no pre-drop veto, so an illegal link is captured post-`EndNodeEditor`
then immediately refused (one-frame flicker); type-incompatible pins are pre-colored by `DataTypeId` to
telegraph rejection.

## 5. Codegen

`GenerateAppGraphCode` over the whole graph; `GenerateAppControlCode(draft,out)` is **retained and
reused** as the per-struct emitter (so step6/step7 strings are unchanged: the `deps==0` path still emits
`ImGuiAppControl<<Name>Data, <Name>TempData>` with no trailing comma). The control struct emission is
refactored into `AppEmitControl(draft, depNames, depCount, out)`; the legacy entry point calls it with
`depCount==0`.

**Algorithm:**
1. Collect Control nodes; build adjacency producer→consumer from every `_Data` link (`StartAttr` owner =
   producer, `EndAttr` owner = consumer).
2. **Kahn topo sort.** If a residual node retains nonzero in-degree, return `false` and write
   `"dependency cycle at <Name>"` to `err` (constraint 3; also pre-rejected in `AppGraphCanLink`). No
   partial emission.
3. Per Control in topo order: emit `struct <Name>Data`/`<Name>TempData` via `AppEmitFieldDecl`
   (nodes.cpp:479; `String`→`char[N]`, deliberately outside the reflect subset, constraint 1), then the
   control struct. Its `DataDependencies` = the *distinct* producer PersistData types of its incoming data
   edges, ordered by topo index (deterministic); each becomes an extra `ImGuiAppControl<...>` template arg
   and a `const <Dep>Data*` param appended to `OnInitialize/OnUpdate/OnRender` in that order (matching
   `ImGuiAppInterfaceAdapterBase`, h:297-302). Each `ImGuiAppFieldBinding` on that edge emits one `OnUpdate`
   line `data-><DstField> = <depParam>-><SrcField>;` (only when the two fields' `ImGuiAppFieldType` match;
   mismatches dropped).
4. Emit one bring-up function: **Layers** first (`PushAppLayer<TypeName>`), then **Windows/Sidebars** with
   placement / `PushAppSidebar<T>(app,vp,dir,size,flags)` (h:561), then **Controls** in topo order. A
   Control with a Containment edge to a Sidebar emits `PushSidebarControl<T>(app, sidebar)` (h:652); to a
   Window it emits `PushAppControl<T>` + `// TODO: PushWindowControl (runtime stub, imguiapp.h:625-650)`.

**Worked example** — Layer(WindowLayer) + Sidebar(StatusBar) hosting `RandomTime`, and `Breathing`
depending on `RandomTime` with a field binding `timer_secs ← max_timer_secs`:
```cpp
struct RandomTimeData     { float max_timer_secs; };
struct RandomTimeTempData { bool  generate; };
struct RandomTime : ImGuiAppControl<RandomTimeData, RandomTimeTempData> { /* stubs */ };

struct BreathingData      { float timer_secs; };
struct BreathingTempData  { bool  hovered; };
struct Breathing : ImGuiAppControl<BreathingData, BreathingTempData, RandomTimeData> {   // dep derived
  void OnInitialize(ImGuiApp* app, BreathingData* data,
                    const RandomTimeData* random_time) const override final {}
  void OnUpdate(float dt, BreathingData* data, const BreathingTempData* temp_data,
                const BreathingTempData* last_temp_data,
                const RandomTimeData* random_time) const override final {
    data->timer_secs = random_time->max_timer_secs;     // from one field binding
  }
  void OnRender(const BreathingData* data, BreathingTempData* temp_data,
                const RandomTimeData* random_time) const override final {}
};

void SetupApp(ImGuiApp* app, ImGuiViewport* vp) {
  PushAppLayer<ImGuiAppWindowLayer>(app);
  PushAppSidebar<StatusBar>(app, vp, ImGuiDir_Down, 0.0f, ImGuiWindowFlags_AlwaysAutoResize);
  PushSidebarControl<RandomTime>(app, app->Sidebars.back());  // contained, no deps -> first
  PushAppControl<Breathing>(app);                             // depends on RandomTime -> second
}
```

## 6. Persistence format + back-compat

New text format, same `appendf`/`sscanf` idiom (nodes.cpp:287-451), reusing `Name=/Persist=/Temp=` so the
embedded `ImGuiAppNodeDraft` serialization is shared with legacy migration:
```
[Graph]
NextId=42
[Node]
Id=12
Kind=4
Name=Breathing
Builtin=0
Type=
DataType=
Pos=224,48
Persist=timer_secs,0,128
Temp=hovered,2,128
Port=20,1,duration         ; id,portkind,name
Port=21,0,source
Port=22,2,owner
Init=224,48,288,144        ; Window/Sidebar placement
Dock=3,0,32                ; Sidebar dir,size,flags
Link=100,21,20,0           ; id,start,end,kind
Bind=100,timer_secs,max_timer_secs   ; linkid,dstfield,srcfield
```
**Backward compatibility (constraint 4, asserted by step5/step8):**
- The four legacy functions (`Save/LoadAppNodeGraph`, `…Multi`) are **byte-identical** ⇒ steps 5 & 8 pass unchanged.
- `LoadAppGraph` recognizes legacy `[Draft]` sections (the existing matcher at nodes.cpp:377): each becomes
  a Control node with fresh `NextId` ids, synthesized DataOut/DataIn/ChildOut ports, a staggered `GridPos`,
  and `DataTypeId = AppNodeStructTypeId(Name)`.
- `Link=` parses 3-field (`%d,%d,%d` ⇒ `Kind` defaults `_Data`) or 4-field — bidirectional; an old loader
  reading a new file stops after 3 ints and ignores `,kind`.
- **Legacy raw-link migration:** old `Link=` endpoints (old `ATTR_DRAFT_BASE+i` values) won't resolve to
  any synthesized port and are dropped. This is the accepted, documented loss — those endpoints were
  "semantically inert bare attr ids"; resolvable-or-drop keeps load total and crash-free.

This explicitly **fixes the incremental frame's round-trip violation**: the new `LoadAppGraph` *parses*
`Kind/Id/Pos/Port/...` back (the judge proved that leaving `Load*` unchanged silently drops them);
identity rides in the parsed records, not in recomputed packed ints.

## 7. Demo UX + test plan

**Demo (Metrics/Debugger):**
- Replace `drafts/links/next_link_id` statics (demo.cpp:397-399) and the `NODE_DRAFT_BASE/ATTR_DRAFT_BASE`
  enum (demo.cpp:226-227) with one `static ImGuiAppGraph graph;` seeded with the App-root node.
  `ShowDataFlowGraph` delegates to `ShowAppGraphEditor(&app, &graph)`.
- `+ Add node` (demo.cpp:430) becomes a palette popup: **Layer** (submenu of the 4 fixed types →
  `AppGraphAddBuiltin`), **Window**, **Sidebar**, **Control** (draftable → `AppGraphAddNode`).
- The hand-wired live `Random Time`/`Default`/`Breathing` nodes (demo.cpp:243-271) become **builtin
  Control palette nodes** (`IsBuiltin`, `DataTypeName="RandomTimeData"`/`"BreathingData"`); their
  reflect-edit bodies move into the builtin body renderer and their `source`/`duration` labels become real
  wireable DataOut/DataIn ports.
- `Generate` (demo.cpp:457) calls `GenerateAppGraphCode`; the status cluster (demo.cpp:497-511) gains a
  topo indicator (`AppGraphTopoOrder` → red `cycle!`) and an edge count.
- `Save/Load` call `SaveAppGraph`/`LoadAppGraph`.

**imgui_test_engine plan (keep steps 1-9 intact; drive real widgets/drags as in tests:218-241):**
- **step10_kind_ports** — `AppGraphAddNode` each kind; assert mandatory ports (Control:
  DataIn+DataOut+ChildOut; App: ChildIn; Layer: ChildOut) and a nonzero `BodyAttrId`.
- **step11_typed_link_legality** — drag DataOut→DataIn (accepted, `Kind==_Data`); DataOut→ChildIn
  (rejected, `Links.Size` unchanged); ChildOut→ChildIn (accepted, `_Containment`).
- **step12_identity_survives_delete** — A,B,C controls; link B.out→C.in; `AppGraphRemoveNode(A)`; assert
  the surviving link's `StartAttr/EndAttr` unchanged and still resolve via `AppGraphFindPort` (the
  regression the old base+index scheme fails).
- **step13_cycle_rejected** — A→B accepted; B→A ⇒ `AppGraphCanLink==false`; `AppGraphTopoOrder` returns
  false + nonempty `err`.
- **step14_codegen_deps_topo** — A producer, B depends on A with a field binding; `GenerateAppGraphCode`;
  assert `strstr("ImGuiAppControl<BData, BTempData, AData>")`, `offset("PushAppControl<A") <
  offset("PushAppControl<B")`, and `strstr("->max")` for the assignment.
- **step15_graph_persist_roundtrip** — mixed-kind graph + typed links + a binding →
  `SaveAppGraph`/`LoadAppGraph`; assert kinds/ports/`NextId`/link `Kind`/binding survive. Then write a
  legacy `[Draft]`+`Link=` file and assert `LoadAppGraph` ingests it as a Control node (back-compat)
  **and** legacy `LoadAppNodeGraphMulti` still loads it unchanged.

## 8. Phased rollout

- **Phase 0 — model + ids (shippable, invisible).** Add enums, `ImGuiAppNodePort`, `ImGuiAppNode`,
  `ImGuiAppGraph`, the trailing defaulted `Link.Kind`, `AppGraphAllocId/AddNode/RemoveNode/FindNode/FindPort`.
  Land step10/step12. *Risk: low; pure additive code, no demo change.*
- **Phase 1 — render + typed capture.** `ShowAppGraphEditor`, `CaptureAppGraphLinks`, `AppGraphCanLink`,
  `AppNodeStructTypeId`. Rewire the demo canvas to the graph; convert the live demo nodes to builtin
  Control nodes. Land step11/step13. *Risk: medium — demo rewrite must not regress the steps 1-9 UI drag
  recipe; mitigate by keeping `BeginAppNodeRenamable`/`EditAppNodeDraftFields`/`DrawAppNodeLinks` calls
  byte-identical, only their id sources change.*
- **Phase 2 — whole-graph codegen.** `AppGraphTopoOrder`, `AppEmitControl` refactor, `GenerateAppGraphCode`,
  field bindings + `EditAppDataEdgeBindings`. Land step14. *Risk: low; legacy emitter reused as subroutine
  guarantees step6/step7 stability.*
- **Phase 3 — persistence v2.** `SaveAppGraph`/`LoadAppGraph` + legacy `[Draft]` migration. Land step15.
  *Risk: low; legacy funcs untouched.*
- **Phase 4 (optional) — live mirror.** Add two reflect-free virtuals `GetControlDataID()`/
  `GetControlDependencyIDs(out,cap)` to `ImGuiAppControlBase`/`ImGuiAppControl` (h:290-292,458-463) — they
  re-expose the compile-time-erased dependency pack with no reflect/imnodes in the core.
  `BuildAppLiveGraph` upserts read-only live nodes keyed by `DataTypeId` and discovers live data edges by
  matching consumer dep-ids to producer `DataTypeId`; a draft whose `AppNodeStructTypeId` equals a live id
  shows a "promoted" badge. *Risk: medium — touches the lean header; `GetControlDependencyIDs` must
  `return min(count,cap)` (tail-garbage guard); live edges must refuse imnodes deletion (they re-derive
  next frame).* This phase is gated behind Phases 0-3 and is droppable without affecting authoring.

## 9. Explicitly rejected ideas

- **Field-level typed pins** (one Sink+Source pin per persist field, packed `FieldUid`). Rejected: (a) the
  12-bit `FieldUid` is monotonic-never-reused and exhausts under heavy editing (hard cap); (b) per-field
  inline editors risk living outside an attribute → canvas pans during edits (constraint 2c); (c)
  synthesizing field-descs for reflection-backed live nodes was hand-waved. **Replaced** by struct-level
  ports (faithful to the type-keyed `app->Data`, h:397) plus name-based `ImGuiAppFieldBinding` rows
  rendered *inside* the DataIn static attribute — recovering the `data->dst = dep->src;` codegen without
  any of those hazards.
- **Multiple ordered `DataIn` pins, one per dependency.** Rejected: `app->Data` is keyed by PersistData
  type, so same-typed deps are impossible at runtime; one multi-link intake with topo-derived arg order is
  the only faithful model.
- **Recomputing imnodes ids from the vector index** (status quo, demo.cpp:285-286,309). Rejected: it is the
  exact reorder/delete corruption bug (constraint 4). Replaced by stored monotonic `NextId` resolved by search.
- **Save-writes-keys-but-Load-unchanged** (incremental frame). Rejected: the judge proved `Kind/Id/Pos` are
  written but never parsed back, so the new round-trip fails constraint 4. Fixed by making the new
  `LoadAppGraph` actually parse the additive records, while keeping the legacy `Load*` byte-identical for
  steps 5/8.
- **`ImVector` field-bindings on `ImGuiAppNodeLink`.** Rejected: it would make the link a non-aggregate and
  break step8's `{1,10,11}` (tests:358). Bindings live on `ImGuiAppGraph`, keyed by `LinkId`.
- **Data-driven Layers / live topology rewiring.** Rejected: layers are fixed C++ types (h:312-342) and
  `DataDependencies` is a compile-time pack — the editor can only *select/place/order* layers and *author*
  (not live-rewire) data edges. Window-hosted controls codegen `PushAppControl` + TODO because
  `PushWindowControl` is unimplemented (h:625-650); only Sidebar containment fully realizes.

---

*Relevant files:* `imguiapp_nodes.h`, `imguiapp_nodes.cpp`, `imguiapp_demo.cpp`,
`imguiapp.h`, `../../tests/imguiapp_nodes_tests.cpp`.
