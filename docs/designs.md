# imguiapp Design Docs — Collected

Every per-subsystem design doc, collated into one document. Chapters appear in system order
(canvas → editor → scopes/vocabulary → input → status → AV → debugger/previewer/DLL preview →
source embed → lean split); each chapter is anchored by its former filename, so citations of the
form `designs.md (av-design)` resolve here. Originals preserved in [archive/](archive/).

## Contents

- [Canvas Engine — a lean, phase-coherent node canvas to replace imnodes](#canvas-engine-design) — `canvas-engine-design.md`
- [Node Editor Upgrade — Authoring Layers, Windows, Sidebars, Controls & Data Flow](#node-editor-upgrade-design) — `node-editor-upgrade-design.md`
- [Composer UI Design — Refinement and Lateral Interaction Ties](#composer-ui-design) — `composer-ui-design.md`
- [Composer Workbench Design — Hardening, Refinement, Expansion](#composer-workbench-design) — `composer-workbench-design.md`
- [Scope Interior Design — nodes below the composition root](#scope-interior-design) — `scope-interior-design.md`
- [Vocabulary Nodes Design — logic Ops, animation builtins, layout nodes](#vocabulary-nodes-design) — `vocabulary-nodes-design.md`
- [Input→Command Binding — remappable chords over the reified command registry](#input-command-binding-design) — `input-command-binding-design.md`
- [Metrics/Debugger Coherence Upgrade — One Truth for State, Surfaced Diagnostics, Synced Views](#metrics-debugger-coherence-design) — `metrics-debugger-coherence-design.md`
- [ImGuiAppAV — frame pacing, frame encoding, test/bench harness](#av-design) — `av-design.md`
- [Playback Debugger — scrub a recorded run offline (F61)](#playback-debugger-design) — `playback-debugger-design.md`
- [Previewer — interpret the composed graph live (F66)](#previewer-design) — `previewer-design.md`
- [DLL preview design (F76)](#dll-preview-design) — `dll-preview-design.md`
- [Source Embed — real code, not fake skeletons + the write-back fold  (Phase B design)](#source-embed-design) — `source-embed-design.md`
- [Lean/Mean Split — `imguiapp_internal.h` + `IMGUIX_DISABLE_TOOLS`  (Phase A design)](#lean-tools-split-design) — `lean-tools-split-design.md`


---

<a id="canvas-engine-design"></a>

## Canvas Engine — a lean, phase-coherent node canvas to replace imnodes

Decision (2026-07-02): hand-roll the node canvas inside ImGuiAppLayer. Rationale, in order:

1. **Phase coherence by construction.** imnodes stores node origins in screen-flavored "grid"
   pixels and measures into pixel rects, forcing our model↔pixel seam (zoom emulation, the model
   cache, per-frame style scaling, decoration transforms). Every bug in the 2026-07 series
   (docs/bug-classes.md) was seam friction. A canvas whose CORE is model-space — one transform,
   applied at draw, measurements returned in model units — cannot express those bugs.
2. **Zoom native**, not emulated through a style/font/position trampoline.
3. **No untouchable upstream in the hot path** (the no-upstream-edits rule stays; this moves the
   canvas into code we own).
4. **Lean**: the Composer uses a fraction of imnodes; the rest is carried weight.

### 1. What the Composer actually uses (migration contract)

Everything the editor calls today — this is the API the engine must cover, no more:

| Used | Notes |
|---|---|
| BeginNodeEditor / EndNodeEditor | one canvas child, grid, interactions |
| BeginNode(+title bar) / EndNode | via BeginAppNode/BeginAppNodeRenamable wrappers |
| BeginInput/Output/StaticAttribute | pins left/right, body rows |
| Link(id, a, b) | cubic wires, hover, selection |
| IsLinkCreated / IsLinkDropped / IsLinkDestroyed + detach-drag, snap-create | CaptureAppGraphLinks |
| Node/link hover + selection (Num/Get/Set/Clear), SetNodeDraggable | selection sync, layer column lock |
| Get/SetNodeGridSpacePos, GetNodeScreenSpacePos, GetNodeDimensions | become model-space getters |
| Panning get/reset, MoveToNode | camera |
| Style colors (subset) + GridSpacing/rounding/padding/pin radii + GridSnapping/GridLines flags | |
| Pan bindings (LMB-empty / RMB drag), Ctrl multi-select | already our policy |
| MiniMap | reimplement small (rects + view box + click-jump) |

**Dropped** (carried weight today): box selector, three-button emulation machinery (bindings become
first-class), pin shape variety (circle + square only: data vs containment), attribute flags stack,
multiple editor contexts, minimap hover callbacks, auto-panning-at-edge (re-add if missed),
LinkLineSegmentsPerLength tuning, the imnodes style stack (style is a plain struct we set).

### 2. Architecture

#### 2.1 Spaces — the core invariant
- **Model space**: node origins, measured node sizes, pin anchors. THE storage. Zoom-independent.
- **Camera**: `{ ImVec2 Pan; float Zoom; }` — the only transform. `screen = origin + Pan + model * Zoom`.
- No API ever returns pixels for model things: `CanvasNodePos/Size/PinPos` are model units;
  hosts transform with the camera themselves (one helper: `CanvasToScreen/ScreenToCanvas`).

#### 2.2 Frame flow (single pass, same-frame geometry)
1. `BeginCanvas`: child window, grid draw, camera input (pan/zoom wheel — cursor-centered, built in).
2. Per node: `BeginCanvasNode(id)` sets the imgui cursor at `transform(origin)`, pushes the zoomed
   font + scaled layout vars (the engine owns this; hosts submit plain widgets). `EndCanvasNode`
   measures the group → size in model units stored NOW. Same frame, no read-back phase.
3. Pins: attribute begin/end capture the row's model-space anchor at the node edge.
4. `CanvasLink`: records wires; drawn in `EndCanvas` under the nodes' channel.
5. `EndCanvas`: draw wires + pins from THIS frame's model data via the camera; resolve hover
   (top-most node by submission order, then pins by radius, then wires by bezier distance); run the
   interaction FSM; report events.
- Decorations (host-drawn boxes/rails/badges) query model geometry of the CURRENT frame after the
  node's End — the stale-decoration class is unrepresentable.

#### 2.3 Interaction FSM (one enum, explicit)
`None → PanCanvas | DragNodes | DragWire(detach?) | PendingMenu` — transitions from the bindings
policy (LMB-drag empty = pan, LMB node = select+drag, LMB pin = wire, RMB drag = pan, RMB click =
menu-pending, wheel = zoom, Ctrl+click = toggle-select). Drag deltas divide by Zoom once, at the FSM.

#### 2.4 Measurement feedback loops
Node size = measured content each frame (imgui group rect / Zoom captured with the SAME Zoom used to
place it — exact by construction). Uniform-width constraints (layer column) stay host-side with
their deadband (§1b of bug-classes.md); the engine gives them exact model measurements so the
loop noise shrinks to glyph rounding only.

#### 2.5 Files + naming
`imguiapp_canvas.h/.cpp`, namespace ImGui, prefix `Canvas*` (`ImGuiCanvasStyle`,
`ImGuiCanvasIO`). No imnodes includes. nodes.cpp migrates behind a thin call-site adapter so the
diff stays reviewable; imnodes include + vendored dir dependency drop at the end.

### 3. Slices

| Slice | Contents | Exit test |
|---|---|---|
| C1 | canvas child, camera (pan/zoom native), grid, BeginCanvasNode/End with measurement, selection + drag | nodes render/drag/zoom with zero seam code in nodes.cpp |
| C2 | pins + wires + hover + create/detach/drop events | CaptureAppGraphLinks ports over |
| C3 | title bars/rename hooks, draggable locks, MoveTo/fit helpers, minimap | editor feature parity |
| C4 | Composer migration (delete the model cache, style trampoline, zoom reseat — the seam dies) | all 29 GUI tests green; zoom acid test clean |
| C5 | remove imnodes from the build | build has no imnodes |

Contract for every slice: the phase-coherence checklist passes by inspection, and the zoom acid
test (rapid wheel over every decoration) shows zero single-frame artifacts.


---

<a id="node-editor-upgrade-design"></a>

## Node Editor Upgrade — Authoring Layers, Windows, Sidebars, Controls & Data Flow

### 1. Summary & goals

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

### 2. Node & edge model

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

### 3. Stable id & port scheme

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

### 4. Public API additions

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

### 5. Codegen

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

### 6. Persistence format + back-compat

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

### 7. Demo UX + test plan

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

### 8. Phased rollout

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

### 9. Explicitly rejected ideas

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


---

<a id="composer-ui-design"></a>

## Composer UI Design — Refinement and Lateral Interaction Ties

Deep-dive design for the Composer's next UI generation. The thesis: **usability does not scale
linearly with feature count — it scales with coherence.** Every feature below either strengthens a
tie between panels or consolidates an existing surface; nothing is additive chrome. References:
Blender (editor-area model, status-bar keymap, outliner sync), Unreal Blueprint editor (wire-drop
palette, comments, alignment, minimap), Unity GraphView (blackboard, groups, minimap).

### 1. Method: the two academic anchors

Two frameworks carry this document. Every proposal names which one it serves; a proposal that
serves neither is cut.

**Cognitive Dimensions of Notations** (Green & Petre, 1996 — built *for* visual-programming
editors). The dimensions we act on:

- **Visibility** — can the user see the state they must reason about, without navigation?
- **Hidden dependencies** — links that exist in the model but not on screen (the killer for graphs).
- **Viscosity** — resistance to small changes (how many actions to rename, rewire, reorder?).
- **Role-expressiveness** — can you tell what a component does by looking at it?
- **Secondary notation** — informal markup (comments, spatial grouping, color) *outside* the
  model's semantics. Graph editors live or die on this; the Composer currently has almost none.
- **Consistency** — similar semantics ↔ similar appearance and similar gesture.
- **Premature commitment** — being forced to decide early (e.g. placing before knowing what to place).

**Brushing and linking / coordinated multiple views** (Becker & Cleveland 1987; Roberts 2007,
"State of the Art: Coordinated & Multiple Views"). The Composer is a coordinated-views system —
outliner, canvas, inspector, code, problems, timeline are six projections of ONE model. The
academic result: coordination (selecting/hovering in one view highlights the same datum in all
views) is what turns N panels from N tools into one instrument. This is the formal name for the
requested "lateral interaction ties."

Supporting cast, used for micro-decisions: Shneiderman's visual information-seeking mantra
(*overview first, zoom and filter, then details-on-demand*), Norman's gulfs of execution/evaluation
(every action needs a visible next step and a visible result), Fitts's law (targets under the
cursor beat targets at the edge; hence context menus and on-wire affordances), Hick's law
(kind-filtered palettes beat full palettes — already dogfooded by the wire-drop palette), and the
Gestalt principles of common region and similarity (group boxes, phase bands, kind accents).

### 2. Shared-state inventory: what the ties synchronize

Coordination is only definable over named state. The Composer's cross-panel state:

| State | Owner today | Notes |
|---|---|---|
| Selection (multi + primary) | `g->Selection` + `*selected_node_id` | already two-way canvas↔tree |
| Hover | per-panel, **not shared** | the biggest missing tie |
| Scope (drill-down) | `g->ViewScope` | tree click navigates it; breadcrumb shows it |
| Camera | imnodes editor context | per-scope memory is on the roadmap |
| App time | `ImGuiAppStateHistory` scrubber | isolated in the toolbar |
| Edit time | undo history (`AppGraphHistory*`) | isolated in the toolbar |
| Problems | `AppGraphValidate` output | listed in a tab; click reveals node |
| Codegen freshness | `AppGraphSignature` | fresh/STALE indicator on the code panel |
| Filter/search | outliner `ImGuiTextFilter` | applies to the tree only |

The lateral-ties program is: **make each row visible in every panel where it is relevant, and
writable from every panel where the user's attention already is.**

### 3. The lateral ties (ranked by coherence-per-cost)

#### T1. Hover reciprocity (brushing) — *the* missing tie
Hovering a datum in any view highlights it in all views, within the same frame:

- Outliner row hover → canvas node halo (accent outline, no selection change).
- Canvas node hover → outliner row tint; if scrolled out of view, a 1px accent tick on the tree's
  scrollbar gutter at the row's position (no auto-scroll — scroll theft breaks the tree's own use).
- Wire hover → both endpoint nodes halo + the wire's binding rows tint in the inspector.
- Inspector binding row / dep row hover → the corresponding wire thickens + endpoints halo.
- Problems row hover → node halo (click already reveals; hover should preview *without* commitment
  — CDoN: avoids premature commitment; Norman: closes the evaluation gulf before the action).

Mechanics: one `HoverSync { ImGuiID node_a, node_b; int link_id; }` written by whichever panel
hovers, read by all panels next frame (same one-frame-latency idiom the title-field hover uses).

#### T2. Status-bar keymap hints — Blender's signature, and the cheapest teacher
A one-line strip under the canvas showing what the mouse does *right now*, given hover target and
modifier state: over node → `LMB select · drag move · dbl-click rename · Tab enter · RMB menu`;
over pin → `drag wire (release on canvas: filtered add)`; over wire → `drag end rewire · click select · Del
delete`; over canvas → `drag pan · wheel zoom · RMB add · Space palette`. Recognition over recall (Nielsen);
teaches the whole gesture vocabulary passively. The F1 card stays as the full reference; the
status bar replaces it as the *first* teacher. Also the natural home for the last validation error
(replacing the floating link-error fade) and the fresh/STALE indicator.

#### T3. Filter co-application — one filter, all views
The outliner search currently filters the tree only. Apply the same active filter to the canvas by
dimming non-matching nodes to ~25% alpha (never hiding — spatial memory must survive; Shneiderman's
*filter* without losing *overview*). Matching nodes keep full opacity + a subtle underline. The
filter button row (kind toggles) already exists in the tree; the same buttons co-apply. One filter
state, two projections — consistency dimension, and it makes search a spatial operation.

#### T4. Code ↔ canvas source map — role-expressiveness, uniquely ours
Codegen emits per-node; record `(node id → line range)` while appending (a `ImVector<{int node,
int line0, line1}>` filled in `GenerateAppGraphCode`). Then:

- Selecting a node scrolls the code panel to its emission and tints the range's gutter with the
  node's accent.
- Hovering a code line halos the node that emitted it (T1 extension).
- The diff view (`AppGraphDiffCode`) tags each hunk with its origin node → clicking a hunk reveals
  the node that caused the change.

No other node editor has this tie, because no other node editor's code *is* the ground truth. This
is the Composer's identity feature: the graph and the code are visibly the same object.

#### T5. One timeline, two rails
App time (state-history scrubber) and edit time (undo history) are the same concept — *when* —
rendered as two disconnected widgets. Consolidate: a single collapsible timeline strip (toolbar
row) with two thin rails: **Edit** (one notch per undo snapshot, cursor = `AppGraphHistoryCursor`)
and **Run** (state-history ring, cursor = scrub index). Scrubbing either rail shows a corner badge
naming the time you are AT (`edit -3 · run -47f`). Rewinding the app while inspecting a stale
graph is a mode the user must *see* (visibility dimension: mode state must never be ambient).

#### T6. Problems as ambient marks, not only a list
Validation issues render where the problem lives, not only in the tab: a small severity dot on the
node's title bar (canvas), a tinted row in the outliner, and an auto-expanded offending section in
the inspector when that node is selected. The list remains the index; the marks remove the
navigation step (visibility; UE does exactly this with compile-error badges on Blueprint nodes).

### 4. Canvas refinement (measured against Blender / UE / Unity)

**R1. Annotation frames (secondary notation — the biggest CDoN gap).** Blender frames / UE
comments / Unity sticky notes all exist because *the model's own containment is never enough*.
Add a `Note` node kind: a resizable, colored, titled rectangle, z-ordered behind nodes, dragging
it moves contained nodes (UE behavior), excluded from codegen and validation. Explicitly
non-semantic — the outliner shows it dimmed, at the bottom, filterable off. Cost: one node kind +
one palette entry; the group title-bar drag machinery already exists.

**R2. Reroute pins (wire secondary notation).** Double-click a wire → elbow node (tiny circle,
one in one out, same type). Purely visual; codegen treats it as pass-through. Long wires across
the layer column are already unreadable in medium graphs; UE ships this for the same reason.

**R3. Alignment and distribution.** Selection ops: align left/right/top/bottom edges, distribute
horizontal/vertical gaps (UE: accelerators on the selection context menu). Complements L (tidy):
L is global policy, alignment is local intent. Shortcut family: `Shift+A` alignment submenu on the
selection context menu — no new top-level keys (Hick).

**R4. Minimap.** Corner inset (Unity GraphView / UE): node rects at 1:~40, viewport rectangle,
click/drag to jump. All rects are already known per frame (`ImNodes::GetNode*`). Combined with F /
Home / scope drill this substitutes for most of what zoom provides (see R6). Draw-list only;
respect the overlay hit-test rule (AllowWhenBlockedByActiveItem, consume-before-canvas).

**R5. Selected-wire flow direction.** On selection only, animate a subtle dash offset along the
wire (UE's bubbles, restrained). Data direction is a hidden dependency for newcomers; always-on
animation is noise (the de-noise pass was right) — showing it *on demand* on the selected edge is
details-on-demand.

**R6. Zoom: name the strategic gap.** Blender/UE/Unity all have canvas zoom; imnodes does not.
Do not fork imnodes for it now. Mitigations already in hand: scope drill-down (semantic zoom —
arguably better than optical zoom for a *compositional* model), F/Home framing, minimap (R4), and
group collapse (LOD by folding). Revisit only if medium-graph navigation still fails after R4+T3
land. Decision recorded here so the gap is chosen, not accidental.

**R7. Node-body LOD** *(parked — not implemented; see the checklist's "Explicitly parked:
per-node LOD").* A collapsed node state (Blender `H` on a node): title bar + pins only,
body folded. Per-node toggle (dbl-click title-bar icon; chevron on hover). The inspector already
holds the long tail; node bodies should identify and wire, not editorialize. Density becomes a
user choice per node — details-on-demand at the node granularity.

### 5. Visual grammar: one table, enforced everywhere

Consistency dimension — similar things must look similar *by rule, not by habit*. Single source of
truth (a `docs/` table now, a style struct in code when it next churns):

| Element | Encodes | Channel |
|---|---|---|
| Node accent (title strip + outliner row + code gutter) | kind (layer type / control / struct / field) | hue |
| Pin | port kind | shape: circle = data, square = containment; fill = wired, hollow = open |
| Wire | edge kind | color inherits pin; containment thinner, behind data wires |
| Badge (sequence number) | execution order | monospace numeral badge, phase-band accent |
| Severity | validation | dot: amber = warn, red = error — same glyph in canvas, tree, problems |
| Dim (25% alpha) | filtered-out / not-in-filter | never for "disabled" — dim means *filtered* |
| Fade (Hidden) | hidden-on-canvas | outliner-only tint; canvas absence |

Typography scale (multiples of em, no free sizes): node title 1.0, port label 0.9, body field 0.9,
meta/badge 0.8. Depth order (fixed): grid < phase bands < group boxes / notes < containment wires
< data wires < nodes < overlay icons < toasts/status. Any new element must name its row and layer.

### 6. Command surface: four roads to every verb

Rule (Blender/UE convention): every operation is reachable by (1) direct manipulation, (2) context
menu at the cursor (Fitts), (3) Space command palette, (4) shortcut — and the palette and menus
*display* the shortcut (recognition trains recall). Audit result — gaps to close: alignment (R3,
new), hide/isolate (context menu ✔, palette ✗, shortcut H ✔), tidy/L (palette ✗), frame/F
(palette ✗), codegen copy (button ✔, palette ✗). The Space palette becomes the completeness
check: **if it's not in the palette, it doesn't exist.**

Transient feedback consolidates onto the status bar (T2): link-rejection reasons, "copied N
nodes", "layout applied" — one place, one fade behavior, no floating toasts scattered per feature.

### 7. Viewport chrome: toolbar, status bar, overlay gizmos

The organizing rule, shared by all three reference editors: **chrome is sorted by what it acts on.**
Document verbs go in the top toolbar (UE Blueprint toolbar: Compile/Save/Find/panels[^ue]); view verbs
go on the viewport itself (Blender's per-editor header + gizmo column, overlays popover[^bl]); panel
toggles right-align (Unity Shader Graph: Blackboard/Inspector/Preview toggles[^un]); status lives in
one bar under everything (Blender: keymap hints left, facts right). A control placed at the wrong
altitude reads as clutter no matter how useful it is — that, not feature count, is what made the old
toolbar feel wrong: Add (view), Tidy (view), Write .h (document) and App time (run) shared one
undifferentiated row, above a second row of read-only pills nothing could interact with.

#### Stencil

```
┌────────────────────────────────────────────────────────────────────────────────────┐
│ [⚠/✓/● Generate]  |  Save  Load  Diff  |  ↶ ↷ ══history══     …      [⚠2] [Code] [👁Live] [🕐App time ══] │  DOC toolbar
├──────────┬─────────────────────────────────────────────────────────────────────────┤
│ OUTLINER │  root ‣ MainWindow ‣ Mixer                                    ╭───╮     │
│          │   (scope breadcrumb overlay)                                  │ + │     │  view gizmo
│  filter  │                                                               │ ◎ │     │  column
│  buttons │                    C A N V A S                                │ ⤢ │     │  (overlay)
│  search  │                                                               │ ✨ │     │
│  tree    │                                                               │ ⌁ │     │
│          │                                                               │ ⚙ │     │
│          │                                              ┌─minimap──┐    ╰───╯     │
│          │                                              └──────────┘               │
├──────────┴─────────────────────────────────────────────────────────────────────────┤
│ CODE / PROBLEMS / DIFF panel (toggled)                                              │
├─────────────────────────────────────────────────────────────────────────────────────┤
│ drag move · click select · Tab enter · RMB menu        wrote a.h   sel: Main>Mixer   design 12 live 4   L4 W1 C3 composed │  STATUS bar
└─────────────────────────────────────────────────────────────────────────────────────┘
```

#### Document toolbar (one row, left → right)

| Control | State it carries | Why here |
|---|---|---|
| **Generate** (primary) | green ✓ = header matches graph (signature == last written); amber ● = model changed since write; red ⚠ = validation errors (writing stays allowed) | UE's Compile button: the document's health lives ON the primary action, not in a separate lamp. Merges the old strip's "graph ok / cycle" pill into a control that can act on it. |
| Save / Load / Diff | — | file verbs, grouped, after the primary action |
| Undo / Redo / history rail | disabled when unavailable; rail = edit-time scrubber | edit verbs; the rail is T5's Edit rail until the unified timeline lands |
| *(right)* Problems count | count, colored by worst severity; click reveals the panel | UE compile-results pattern; the ambient marks (T6) point where, this counts how many |
| *(right)* Code toggle | lit while open | Unity panel-toggle placement: right-aligned, latched |
| *(right)* Live toggle | eye lit while the mirror shows | view-population toggle used rarely → right cluster, not prime left space |
| *(right)* App time | lit while frozen + frame scrubber | run control (UE's Simulate cluster sits right of the doc verbs) |

Dropped from the toolbar: **Add node** (view verb — gizmo column, RMB, Space), **Tidy** (view verb —
gizmo column, L), the **Show-live checkbox** (became the latched eye toggle: state reads at a glance,
Fitts-cheaper than a checkbox label).

#### Viewport gizmo column (top-right overlay, draw-list buttons)

Blender's gizmo column / UE's in-viewport toolbar: view verbs live where the view is, reachable
without leaving the canvas. Top → bottom: **+ add** (opens the kind palette at the viewport center),
**◎ frame selection** (F), **⤢ fit all** (Home), **✨ tidy** (L), **⌁ snap** (G, latched-lit), **⚙
overlays popover** (grid / phase bands / group frames / minimap — Blender's overlays dropdown; lit
when any overlay is off, so a de-cluttered canvas is visibly a *mode*). Every gizmo tooltips its
shortcut (recognition trains recall). Hit-tests follow the overlay rule (AllowWhenBlockedByActiveItem).

#### Status bar (window bottom, one row)

Left: the editor's **live keymap hint** — what LMB/drag/RMB do given the current hover target
(node/pin/wire/canvas, live- and layer-aware), computed by the editor, rendered by the host via
`AppGraphStatusHint`; refused-link errors override in red for 3 s. Right, in fixed order: transient
confirmation (`wrote imguix_generated_control.h`) · selection breadcrumb · node counts (design/live/
promoted) · mirrored-app composition (`L4 W1 S0 C3 composed`). Facts only — anything actionable
graduated to the toolbar (that inertness is what made the old strip feel broken: it sat *above* the
viewport in prime toolbar position while affording nothing).

#### Deliberately excluded (decided, not forgotten)

- **Zoom % indicator** — imnodes has no zoom (§4 R6); showing one would promise a control that
  doesn't exist.
- **A second header row on the canvas** (Blender's editor header): at the demo's panel sizes one doc
  toolbar + gizmo column covers the verb set; a second strip is chrome tax. Revisit if the Composer
  becomes a multi-editor workspace.
- **Toolbar search field** — arrives with T3 (filter co-application); placing it before it filters
  the canvas would ship a lie.
- **Breadcrumb in the toolbar** — the scope breadcrumb stays a canvas overlay: it is view state and
  clicking it navigates the view; duplicating it in chrome splits one concept across two homes.

[^ue]: [Toolbar in the Blueprints Visual Scripting Editor (Epic docs)](https://dev.epicgames.com/documentation/en-us/unreal-engine/toolbar-in-the-blueprints-visual-scripting-editor-for-unreal-engine) — compile-state-as-button, doc verbs, debug cluster.
[^bl]: [Blender manual: Node Editors](https://docs.blender.org/manual/en/latest/interface/controls/nodes/node_editors.html) — header, snapping toggle, overlays popover, sidebar, status-bar keymap.
[^un]: [Unity Shader Graph: Blackboard / toolbar](https://docs.unity3d.com/Packages/com.unity.shadergraph@12.0/manual/Blackboard.html) — Save Asset primary, right-aligned panel toggles.

### 8. Delivery slices (each shippable, each independently visible)

| Slice | Contents | Serves |
|---|---|---|
| **S1 — Coherence core** | T1 hover reciprocity, T2 status-bar hints (+ error/state readouts), T6 problem marks | brushing/linking, visibility |
| **S2 — Canvas craft** | R1 notes, R3 alignment, R4 minimap | secondary notation, overview |
| **S3 — The identity tie** | T4 code↔canvas source map (select→scroll, hover→halo, diff→node) | role-expressiveness |
| **S4 — Time & filter** | T5 unified timeline, T3 filter co-application | visibility, filter |
| **S5 — Wire & density polish** | R2 reroute pins, R5 flow-on-select, R7 node LOD | secondary notation, DoD |

S1 first: it is the cheapest slice and the one that makes the whole application feel like one
instrument — the property the big three editors share and feature lists don't capture.


---

<a id="composer-workbench-design"></a>

## Composer Workbench Design — Hardening, Refinement, Expansion

Second-generation UI design. The first program ([composer-ui-design.md](#composer-ui-design))
made six panels into one instrument: lateral ties, viewport chrome, ambient problems — S1 and the
§7 chrome landed. This program makes the instrument into a **workbench**: a place with a settled
spatial grammar, one command vocabulary, inspectors at every altitude of the model, and a viewport
that reads as the subject rather than one panel among peers.

**Design pillars** (the acceptance test for every proposal): consistency, coherency,
communicability, ease of use, intuitiveness. A proposal that cannot name which pillar it serves —
and which Cognitive Dimension it moves — is cut.

### 1. Method

#### 1.1 The reference editors and what each is FOR

Each reference contributes its *signature discipline*, not its feature list:

| Editor | Signature discipline | What we take |
|---|---|---|
| **Visual Studio 2022** | Tool-window grammar: every panel summonable, dockable, remembered[^vs-layout]; one Error List; feature search reaches everything[^vs-search] | Panel lifecycle contract (§3.1), command registry + palette (§5.2), status-bar zones (§4.3) |
| **Unreal Editor** | Viewport primacy: the level IS the screen, verbs live on the viewport toolbar[^ue-viewport]; Details panel categories follow the selection[^ue-details]; Compile button carries state[^ue-bp] | Viewport altitude rules (§4.1), primary-action-carries-health (kept), Details-style inspector sections (§5.1) |
| **Unity Editor** | The quartet (Project / Hierarchy / Inspector / Console) with the *component* as the inspector's unit: header, enable checkbox, help, ⋮ menu[^u-inspector][^u-components] | Component-section grammar (§5.1), project-level inspector (§5.3), play-state tinting (§4.4)[^u-prefs] |
| **Blender** | Modes and altitude: N-panel sidebar at the editor it serves[^bl-sidebar], region system[^bl-regions], F9 floating redo[^bl-undo], status keymap[^bl-status], nothing steals the viewport | Quick inspector (§5.4), operation-named undo (§3.4), overlay fade discipline (§4.2) |

#### 1.2 Cognitive dimensions, weighted for this generation

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
  health readouts) without demanding focus.
- **Hard mental operations** — the inspector does unit/type/default bookkeeping so the user never
  computes it (mixed-value states, reset-to-default, live expression checking already dogfoods this).

#### 1.3 What "sublime" means operationally

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

### 2. Current state (inventory the design builds on)

Landed and assumed by everything below: document toolbar (health-carrying Generate, file/edit
verbs, right-aligned Code/Live toggles) · viewport gizmo column (add/frame/fit/tidy/snap/overlays
popover) · toolbar App-time transport (freeze + frame scrub, ShowLive-gated) · viewport health strip (click → Output) ·
status bar (keymap hints + facts) · bottom panel tabs Code (source-mapped) / Project (doc files) /
Preview / Output (issues + log, brushing) · outliner with filter buttons · per-node inspector column
(fields, events, commands, style/color mod descs) · brushing hover sync · ambient problem marks ·
drill-down scopes with breadcrumb · undo/redo rail · prefab registry · time travel.

### 3. HARDENING — finish what exists, then trust it

#### 3.1 Panel lifecycle contract (VS tool-window grammar)
Today the bottom tabs, the inspector column, and the outliner are three ad-hoc mechanisms with
three memories (splitter floats in doc data, one-shot `WantOutputTab` flag, `CodeH > 0` meaning
"open"). Replace with one contract every panel signs:

- Identity: stable id, icon, display name.
- State: open/closed, size, last-active tab — **persisted across sessions**. *(Landed as a sidecar
  `imguix_composer_layout.ini` rather than an imgui settings handler: the Composer initializes
  mid-frame-loop, after imgui has already loaded its ini, and late-registered handlers never see
  their sections. The `AppWindowLayerSettingsHandler_*` stubs remain a framework-level question.)*
- Intent API: `RevealPanel(id, payload)` — generalizes the `WantOutputTab` one-shot into the one
  way any subsystem summons any panel (Output click, palette "Show X", future deep links).
  *Pillars: consistency, ease of use. CDoN: viscosity down (no re-opening ritual), premature
  commitment down (any panel reachable from anywhere).*

#### 3.2 Four-roads audit, enforced by the registry
Gen 1 stated the rule (every verb: direct manipulation + context menu + palette + shortcut,
menus display the shortcut). Harden it by *construction*: one *command registry* (§5.2) is the
single table context menus, the palette, shortcuts, and status-bar hints all render from. A verb
that isn't registered doesn't exist; a registered verb is automatically everywhere. Known gaps to
close on landing: tidy/frame/fit/hide/isolate/align in the palette; palette itself (§5.2).

#### 3.3 Keyboard reach
The canvas has a gesture vocabulary; the panels do not. Minimum: Ctrl+Z/Y (exists), F2 rename
selected, Del delete, arrow-key nudge (grid quantum), Tab/Esc scope drill (exists), Ctrl+P palette,
F focus-selection (exists), Ctrl+S save. Rule: a shortcut acts on the *selection*, never on the
hovered item (hover is for brushing; acting on hover is a mode error the user can't see).
*Pillar: ease of use. CDoN: viscosity.*

#### 3.4 Undo, named and complete
- Coverage audit: every model mutation goes through the history (style/color mod edits, event
  edits, sequence nudges — anything found bypassing it is a defect).
- **Operations get names** (Blender's F9 header, VS's "Undo Typing"): the history rail's notches
  tooltip "Add Window 'Mixer'", "Rewire timer_secs", "Style: FrameRounding 3→6". Cost: a label
  argument on the snapshot call. Payoff: the timeline becomes legible history instead of a ruler.
  *Pillar: communicability. CDoN: progressive evaluation.*

#### 3.5 Live-mirror write-back seam
The inspector shows live nodes read-only, but style/color descs were built runtime-toggleable.
Harden the seam: live nodes' Style section shows the Active checkboxes ENABLED, writing through to
the running item's `StyleMods/ColorMods` (the one sanctioned live mutation — it round-trips through
the mirror next frame and cannot desync the model, because the mirror IS the model for live nodes).
Everything else stays read-only. This turns the "runtime member" decision into a visible feature:
flip a window's rounding live, watch the app change, no regeneration.
*Pillars: intuitiveness (the checkbox does what checkboxes do), coherency (design nodes author,
live nodes actuate — same rows, honest affordances).*

### 4. REFINEMENT — the viewport as the subject

#### 4.1 Overlay altitude table (one law for the canvas's airspace)
Every overlay names its slot; new overlays must claim a vacant one:

| Slot | Occupant | Notes |
|---|---|---|
| Top-left | scope breadcrumb | view state; clickable path |
| Top-right | gizmo column | view verbs only |
| Bottom-left | health strip (+ last log line) | click → Output |
| Bottom-center | (free) | App-time transport lives in the toolbar cluster instead (ShowLive-gated) |
| Bottom-right | minimap | overlay toggle; click/drag jumps |
| Bottom edge (inside) | status hint line | what the mouse does now |
| Cursor | context menu, wire-drop palette, quick inspector (§5.4) | transient, Fitts-optimal |

Rule: corners are *owned*, never shared; a second tenant in a corner stacks into its owner (the
overlays popover pattern), it does not squat beside it.

#### 4.2 Overlay quietness (Blender discipline)
- Overlays render at rest ~70% opacity, full on hover of their bounds; the health strip goes full
  only when count > 0. No overlay ever exceeds the nodes' own contrast at rest.
- During canvas drag/wire gestures, non-cursor overlays dim to ~35% (UE's cinematic fade): the
  subject is the graph, chrome yields while the user is *doing*.
- One motion idiom everywhere: 150 ms linear alpha fade. Nothing slides, bounces, or scales.
*Pillar: sublime rule 2. CDoN: visibility of the DATA (chrome is noise in the data's channel).*

#### 4.3 Status bar zones (VS discipline)
Freeze the zone map; each zone has one topic and one click action:

`[keymap hint | transient confirmation] ——— [selection breadcrumb] [counts] [mirror] [freshness]`

- Every right-zone fact becomes *clickable where it can act*: breadcrumb segments select, counts
  toggle the corresponding outliner filter button, mirror fact toggles Live, freshness runs Generate.
  A status bar the user can click is a second command surface at zero pixel cost (VS's branch/
  encoding/line-col widgets). *CDoN: viscosity; pillar: ease of use.*

#### 4.4 Run-state tinting (Unity discipline)
When App-time scrub is active (the mirrored app is frozen/rewound), tint the viewport background
2–3% toward the transport's accent and put a thin accent line under the toolbar — Unity's
play-mode tint. Mode-you-must-see, stated ambiently, no modal furniture. Complements the corner
badge from gen 1's T5. *CDoN: visibility of mode.*

#### 4.5 Toolbar split-buttons (VS grammar for verb families)
`Save` grows a dropdown half (Save / Save As / Save Copy prefab-set); `Generate` similarly
(Generate / Copy to clipboard / Diff vs saved). Primary click keeps today's behavior; the family
lives behind the split. Removes `Diff` as a peer button (it is a Generate-family member, not a
document verb) — one less top-level item, zero capability lost. *CDoN: role-expressiveness
(family = one control); pillar: coherency.*

### 5. EXPANSION — inspectors at every altitude, one command vocabulary

#### 5.1 Component-section inspector (the Unity/UE unit, node level)
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

#### 5.2 Command palette + command registry (the VS Quick Launch spine)
One registry entry per verb: id, display name, icon, shortcut, availability predicate, run().
Context menus, the gizmo tooltips, the status keymap hints, and shortcuts all *render from the
registry* (§3.2). The palette (Ctrl+P / Space on canvas) fuzzy-matches over:

- Verbs ("tidy", "generate", "toggle live") — with their shortcuts displayed, teaching them.
- Nodes by name ("mixer" → select + frame) — VS's Ctrl+, symbol search.
- Palette adds ("add window", "add slider control") — merging the wire-drop palette's vocabulary.
- Panels ("output", "project") → RevealPanel.

The palette is the completeness proof: its registry IS the audit list. *Pillars: ease of use,
communicability (it teaches shortcuts); CDoN: premature commitment (act from anywhere).*

#### 5.3 Project-level inspector (the missing altitude)
Selection empty → the inspector shows the DOCUMENT, not a void (Unity: nothing; UE: World
Settings; VS: project properties — we take the strong version). Sections in the same §5.1 grammar:

- **Document** — graph path, header path, composition signature, freshness indicator (click = Generate),
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

#### 5.4 Quick inspector at the cursor (Blender's N-panel, sized to imnodes reality)
`N` (or the node context menu's "Inspect here") opens a floating, pinnable mini-inspector beside
the selected node with the two or three sections that node kind edits most (Control: Fields +
Events; Window: Style; Sidebar: Dock + Style). Same section components as §5.1 — it is a *view*
of the inspector, not a second editor. Dismiss on Esc/click-away unless pinned. Solves the
core viscosity of the layout: canvas on the left of your attention, inspector a full screen-width
away. *CDoN: viscosity; pillar: ease of use. Fitts: the edit happens where the eye already is.*

#### 5.5 Layout presets, not workspaces
Full Blender workspaces are more machinery than a single-document editor earns. Instead: three
named layout presets on the View menu / palette — **Compose** (canvas + outliner + inspector),
**Review** (code panel tall, canvas short — the source-map's home), **Observe** (Live on, transport
prominent, Output docked open). A preset is just panel states (§3.1), so this is ~free once the
contract lands, and it names the three actual postures users occupy. *Pillar: ease of use without
new grammar.*

### 6. Visual grammar v2 (extends gen 1 §5 — one table, enforced)

#### 6.1 The chrome defines itself in desc terms
`kBlComboColors` / `kBlEditColors` (landed) grow into the complete chrome table: every push-stack
style the composer's own UI uses is a named `ImGuiAppStyleModDesc`/`ImGuiAppColorModDesc` table,
surfaced read-write in the project inspector's Theme section (§5.3). The visual grammar stops
being documentation and becomes data — with Active flags as the rule's own on/off switches.

#### 6.2 Interaction-state ladder (every control names its rung)
`rest → hover → active → selected → disabled → mixed`. One palette column per rung; a control
that invents a seventh state or a bespoke hover color is a defect. Draw-list widgets (the Bl
family) and stack-styled widgets read from the same constants.

#### 6.3 Spacing and type
Spacing quantum: 0.25 em; sizes only in em multiples (audit the remaining raw-pixel constants).
Type scale unchanged from gen 1 (1.0 / 0.9 / 0.8) — it held.

#### 6.4 Depth order, extended to chrome
grid < phase bands < group frames/notes < containment wires < data wires < nodes < node badges <
canvas overlays (§4.1) < transient cursor UI (menus, palette, quick inspector) < toasts/status.
Panels never float above canvas overlays; cursor UI beats everything except the status bar's
error override.

### 7. Delivery slices

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

### 8. Deliberately excluded (decided, not forgotten)

- **Docking framework adoption** for the panels — imgui docking exists, but the Composer's fixed
  quartet + presets (§5.5) covers the real postures; free-docking spends consistency to buy
  flexibility nobody asked for. Revisit only if a fifth panel kind appears.
- **Canvas zoom** — unchanged verdict from gen 1 §4 R6; minimap + scopes + framing still substitute.
- **Toasts/notification center** — the status bar's transient zone + WAL are the two honest
  channels; a third would split attention (VS's own notification hub is a cautionary tale).
- **Per-user theme marketplace ambitions** — the Theme section edits the one built-in table;
  serializing chrome themes waits until someone actually wants to ship one.

### References

Primary sources for the borrowed disciplines. (Gen 1's footnotes — UE Blueprint toolbar, Blender
node editors, Unity Shader Graph blackboard — remain in [composer-ui-design.md](#composer-ui-design).)

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


---

<a id="scope-interior-design"></a>

## Scope Interior Design — nodes below the composition root

What nodes look like once the user drills into a scope (Tab / double-click / breadcrumb), designed
first for the window-group scope and stated as one rule set that every scope kind applies. Mockup:
the "Scope interior" artifact (window-blue Mixer scope, two hosted controls) — all hues in this
document are the editor's own (`kAppHue*` through `AppThemeAccent`, `imguiapp_nodes.cpp`).

Companion documents: [composer-ui-design.md](#composer-ui-design) (visual grammar v1, altitude
law), [composer-workbench-design.md](#composer-workbench-design) (grammar v2, overlay altitude
table), [bug-classes.md](bug-classes.md) (every geometry rule below complies; §7),
[archive/up-next.md](archive/up-next.md) (the Lifecycle north star this design advances).

### 1. Diagnosis — the drilled view before rules A–E (all resolved)

Entering a scope filters which nodes submit and adds sequence badges. Four defects motivated the
rules below; each is now resolved (resolving rule + function cited):

1. **The owner evaporates.** Enter "Mixer" and Mixer itself is gone; only breadcrumb text says
   where you are. The room has no walls. *(CDoN: visibility of context.)* **Resolved** (rules A/B):
   `AppDrawScopeWalls` draws the owner's silhouette as walls, publishing `ScopeWallRect`.
2. **No altitude change in the cards.** A control renders the identical full card at root and in
   scope — only binding rows gate on `altitude_root`. Drilling changes *which* nodes, not *what*
   nodes look like: semantic zoom without the zoom. Root group frames are correspondingly noisy.
   **Resolved** (rule D): `AppScopeDetailAltitude` gates the full authoring body on the
   scope-parent, an identity card elsewhere; proven by `step37_density_flip`.
3. **Cross-scope wires vanish.** A link whose other endpoint is outside the scope is simply not
   submitted, so the dependency disappears from view — the exact "hidden dependencies" failure the
   design docs name as the killer for graph notations. **Resolved** (rule E): `AppDrawScopePortals`
   docks a portal chip on the wall for every crossing edge.
4. **The sequence is annotation, not structure.** Badges and dashed arrows float over free-placed
   cards; the scope caption *says* "push order between the host's Begin/End" but nothing shows
   Begin or End. **Resolved** (rules B/C): the walls ARE the Begin/End pair and
   `AppDrawScopeOrderStrip` renders the order strip; members carry title ordinals.

### 2. The rules

#### Rule A — the owner's silhouette becomes the room (walls)

The scope frame reuses the owner's root-level card silhouette at wall size. For a window scope:
squared corners (rounding 2 model units), a kind-hue rule under the face band, neutral outline.
No interior fill — rule B's void carries figure-ground. Face band anatomy is rule B's (the Begin
line); the config readout is right-aligned, dim, code font: the owner's placement/dock facts
(`320x240 @ (64,48) · AlwaysAutoResize`; a sidebar shows `dock Down · auto`). Read-only —
editing stays in the inspector; the wall states identity, it does not editorialize.

The wall rect derives from the members' bounds (the `group_box` accumulation) plus padding, in
model units, transformed by this frame's camera. Entering a node reads literally: the card you
Tab'd into became the walls. *(Pillar: consistency — same silhouette at both altitudes. CDoN:
visibility. Serves defect 1.)*

#### Rule B — the walls ARE the Begin/End pair (rev 4, shipped)

Field history: Begin/End plates shipped as floating furniture and were cut same-day (the TEXT was
right, the floating rendering was the defect). Rev 4 fuses the calls into the walls — the room is
drawn as the code block it generates:

- **Face band (top wall)**: the `Begin("Mixer")` line in the code font — `Begin(` muted
  framework-grey, the name in the owner's kind hue, `)` muted — kind word after, config readout
  right-aligned. The runs order strip (rule C) is the band's second row; one plate, one kind-hue
  rule at its base.
- **End band (bottom wall)**: a thin closing band, `End()` muted, same font, left-aligned under
  the Begin column. Members run between the two lines, top to bottom.
- **The void**: interior fill is gone; instead everything OUTSIDE the walls dims (~45% dark) —
  figure-ground: inside is stated by light. No translucent box competing with group frames.
- **Rails**: the left/right edges thicken to ~1 em; portal chips (rule E) dock straddling them.
  An empty rail reads as "no external dependencies" at a glance.
- **Stability**: the wall rect grows instantly and shrinks only past a 1.5 em deadband
  (phase-coherence §1b fixed point) — the room does not breathe during drags.

*(Pillar: the model is the interface — the scope caption's sentence became the geometry.)*

#### Rule C — sequence: order strip + title ordinals (rev 2/4, shipped)

The slate number circles and dashed arrows are gone (floating annotation — needed a legend,
occluded card corners). Execution order lives in two existing surfaces:

- **Order strip** (`AppDrawScopeOrderStrip`): the face band's second row — `runs 1 Gain → 2 Meter
  → 3 Scope`, one chip per member in execution order. Hover halos the member (brushing bus,
  External source); click selects it; overflow folds to a stated `+N`. Chip rects publish per
  frame (`ScopeStripRects/Nodes`) — the coming sequence-reorder drag rides them: dragging a chip
  in a 1D strip beats repositioning badges in 2D space.
- **Title ordinal**: members carry `1/3` in their title bar via `CanvasNextNodeTitleBadge` — the
  exact idiom the layer nodes wear at root (`3/5`). Order is part of the card; nothing floats,
  nothing occludes.

*(CDoN: role-expressiveness + consistency — one badge grammar at every altitude.)*

#### Rule D — detail lives one scope below its owner (density flip)

One sentence of law, generalizing the existing altitude gates (structs/fields below root,
bindings below root): **a node shows its full authoring body only when the current scope is its
scope-parent** (`AppScopeParentOf`). Everywhere else it shows an identity card.

| Altitude | Control card contents |
|---|---|
| identity (root, inside its group frame; any foreign scope) | title bar (name, kind word, origin dot, severity dot) · deps pin row with wired producer names · DataOut row · **one dim summary line**: `2 fields · 1 event · emits SetLevel` |
| detail (inside its host's scope) | identity content **plus** PersistData/TempData rows with inline field editors and current values, tie-pin explode disclosures, event rows (`when dragging ^ → set level`), command chips, per-edge binding editors (`from RandomTime: timer_secs ← max_timer_secs`) |

Same card width (`UniformCardW`), same rounding, same title anatomy at both altitudes — only the
depth of content changes, so spatial memory survives the flip. Wires land identically. Per-node
LOD (R7, composer-ui-design.md) is parked — not implemented; the density flip is the only
altitude mechanism. *(CDoN:
details-on-demand at the scope granularity; de-noises root for free. Serves defect 2.)*

#### Rule E — boundary portals (the walls take the cross-scope wires)

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

### 3. Visual spec (grammar rows; extends composer-ui-design.md §5)

| Element | Encodes | Form | Color |
|---|---|---|---|
| Walls | the owner's Begin/End call pair | face band (Begin line + runs strip) / end band (End()) / 1 em rails; void dims outside | kind hue: rule + name; bands opaque title-bg; void dark ~45% |
| Order-strip chip | member's place in the sequence | small squared chip in the face band, `n Name`, `→` separators | ordinal in scope accent; chip neutral; hover border accent |
| Title ordinal | member's place in the sequence | `n/N` badge in the member title bar | scope accent (layer-badge idiom) |
| Portal chip | off-scope endpoint of a data edge | wall-docked pill, `▸` jump glyph | remote kind hue at ~45% border / ~75% text |
| Summary line | folded authoring detail | one dim text row | `TextMuted` |

Depth order (slots from workbench §6.4): walls sit in the group-frame slot (behind containment
wires, in the background draw list); rail + badges + chips render on the canvas annotation
channel (the child's post-merge list, clipped to the editor): above every node, never above
other windows. Typography: existing scale only (1.0 / 0.9 / 0.8 em);
spacing in 0.25 em quanta; no new constants outside the style table.

### 4. Per-scope-kind application

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

### 5. Root-side consequence

The density flip is the only rule that changes the root view: controls inside group frames
collapse to identity cards, so a root composition of N windows scans as N labeled clusters of
title bars — the overview altitude the altitude law always claimed. Group frames, trunk
connectors, section packing, phase bands: unchanged.

### 6. Rejected alternatives

- **Owner as a giant node in-scope.** A node takes selection, drag, pins, deletion — none of
  which the scope owner can honor from inside itself. Walls are furniture with one interactive
  element (nothing), plus the existing breadcrumb for navigation.
- **Portals as real graph nodes** (Blender's group input/output nodes, UE tunnels). Our graph is
  a 1:1 mirror of the runtime object model; synthetic nodes would leak into validation, codegen,
  persistence, and the outliner, or need special-casing in all four. Chips are draw-list
  presentation of existing `Links` rows.
- **Fixed lane layout in-scope** (members force-packed along the rail). Free placement + rail
  preserves spatial memory and keeps the scope-local tidy verb (archive/up-next.md) as the on-demand
  arranger. The rail suggests order; it does not own positions. (Window-section semantics stay
  root-only.)
- **Config editing in the wall title bar.** The inspector owns editing; a second editor surface
  in chrome splits one concept across two homes (same argument that kept the breadcrumb out of
  the toolbar).
- **Hiding cross-scope wires with a count badge** ("+2 external") instead of chips. Cheaper, but
  it states that dependencies exist without saying which — the hidden-dependency defect at lower
  resolution.

### 7. Phase-coherence compliance (checklist applied at design time)

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


---

<a id="vocabulary-nodes-design"></a>

## Vocabulary Nodes Design — logic Ops, animation builtins, layout nodes

The Composer's node vocabulary today is structural (App / Layer / Window / Sidebar / Control / Struct /
Field / Note; `imguiapp_nodes.h:240-248`). Three families extend it with *behaviour*: logic **Op** nodes
(F54/F55), **animation** builtin controls (F56), and **layout** nodes (F57). This document decides their
semantics so the downstream code lands with no open questions. It is a decision document — every verdict
below is binding on F54-F57.

Companion documents: [big-idea.md](big-idea.md) (the four-phase pipeline the codegen compiles against),
[bug-classes.md](bug-classes.md) (the temp^last one-frame skew the animation builtins obey),
[scope-interior-design.md](#scope-interior-design) (scope domains + altitude; the Layout layer gets its
first interior here), [archive/up-next.md](archive/up-next.md) (module-interop layer model).

Each new kind states **four rows**: palette legality (which scope offers it), scope domain (where it
updates / what `AppScopeParentOf` returns), validation, and codegen shape. Constraints honored throughout:
never edit imgui/imnodes/implot; per-instance model state on the graph object (no TU globals); pointers not
references; ASCII-only in generated code.

### 0. Shared ground (the rails all three families ride)

Verified mechanisms these verdicts lean on — cited once here, referenced by name below:

- **Palette legality** is one table: `AppScopeKindComposable(g, scope_id, kind)` (`imguiapp_nodes.cpp:4932-4958`).
  Add-verbs gate on it via `AppGraphEditorCommandAvailable` (`:6404-6419`, the `AddKind != COUNT` branch at
  `:6407-6412`). Today: Window/Sidebar take Control (`:4941-4943`); a Task-type Layer takes Control/Struct
  (`:4950-4951`); a Display-type Layer takes Window/Sidebar (`:4952-4953`); **Layout/Status/Custom compose
  nothing** (`:4954`). To offer a new kind in a scope you add a row here — no other switch changes.
- **Scope domain** is `AppScopeParentOf(g, id)` (`:4746-4782`): a Control under no host falls back to the
  **Task** layer (`:4770-4771`); a standalone Struct is Task-domain app data (`:4760-4762`); Window/Sidebar
  are Display-domain (`:4776-4777`). Membership is `AppNodeInScope` (`:4831-4880`); enterability is
  `AppScopeCanEnter` (`:4821-4825`). The Layout layer scope currently returns nothing (`:4851-4852`).
- **Link legality** is `AppGraphResolveLink` (`:2992-3052`): output→input only, no self-link, no duplicate;
  one **producer per PersistData type** keyed by the DataOut `DataTypeId` (`:3016-3031`, skipped when the id
  is 0 at `:3028`); **cycle refused** via `AppGraphDataReaches` (`:3032-3034`); containment is single-parent
  (`:3040-3048`). Ports are stamped per kind in `AppGraphStampPorts` (`:1285-1320`).
- **Builtin controls** ride `AppGraphAddBuiltin(g, kind, type_name, data_type_name)` (`:1951-1970`): sets
  `IsBuiltin`, `TypeName`, `DataTypeName` *before* stamping ports so the DataOut carries the builtin's real
  data-type id. Precedent: `AppGraphAddBuiltin(graph, ImGuiAppNodeKind_Control, "RandomTime", "RandomTimeData")`
  (`imguiapp_demo.cpp:275`).
- **Type rules** are `AppEventExprCheck` (`imguiapp_nodes.cpp:10473-10521`) over the tiny grammar
  (`:10231-10471`): scalar predicates `AppExprIsBool/IsNumeric/IsInt` (`:10182-10184`), promotion
  `AppExprPromote` (`:10186-10192`), `^` pairs bools (the change idiom) or ints (`:10425-10441`), `!` needs a
  bool (`:10327-10333`), comparison needs numbers (`:10394-10407`), and the result is fit to `DstField`
  (`:10497-10515`). The section comment blesses growth: *"growing it is fine as long as every construct stays
  type-checkable"* (`:10117-10118`).
- **The emitter** is `AppEmitControlWithDeps` (`:10524-10861`): dependency params from `AppGraphConsumerDeps`
  (`:10618-10641`), binding assignment lines (`:10707-10761`), and authored-event guards / command latches
  (`:10534-10554`, `:10667-10678`, `:10794-10838`). Bring-up (`PushApp*`) is emitted in
  `GenerateAppGraphCode` (`:11447-11519`).
- **Serialization**: `AppEmitNodeRecord` writes `Kind=%d` as an int (`:11617`), `Type=` (`:11620`),
  `DataType=` (`:11621`), `Dock=` (`:11632`), per-kind `Note=` (`:11634`), and every `Port=` with its
  `DataTypeId` (`:11637`). Node kinds are **append-only** (the Note comment, `imguiapp_nodes.h:247`): Op and
  Layout append before `_COUNT`.
- **Layout is not greenfield**: `ImGuiApp::OnLayout()` is a real (default-empty) virtual
  (`imguiapp.h:906`); the Layout layer caption already reads *"the app's OnLayout() submits dockspaces & dock
  bindings before any window Begins"* (`imguiapp_nodes.cpp:5361`, `:7555`), and a live dockspace inspector
  already walks `DockContext.Nodes` (`:7558-7587`). The layer stack orders Layout before Display
  (`AppGraphEnsureFoundation`, `:1981-1991`) — dockspaces run before windows render.

---

### 1. Op nodes (`ImGuiAppNodeKind_Op`) — F54 / F55

> **VERDICT: BUILD.** One appended kind `ImGuiAppNodeKind_Op`. Ops are a graph-authoring form of an
> expression AST that **folds into the consumer's emitted expression** — no runtime object, no `app->Data`
> entry, no push line. The operator set is AND / OR / XOR / NOT, compare (`== != < <= > >=`), select/mux,
> and min/max. The single type authority is `AppEventExprCheck` — F54 adds no second type lattice.

#### 1.1 Model

An Op node stores its operator token in `TypeName` (already serialized as `Type=`, `:11620`); its label
rides `Draft.Name`. `IsBuiltin = false` (there is no compiled backing type). Ports (new
`AppGraphStampPorts` case): N `DataIn` operand pins fixed by operator arity — NOT(1), compare/min/max/AND/
OR/XOR(2), select(3) — plus one `DataOut` "result" pin **stamped with `DataTypeId = 0`**. The zero id is
load-bearing: `AppGraphResolveLink`'s one-producer-per-type check only fires for non-zero ids (`:3028`), so
an Op result fans out to any number of consumers and never collides — correct, because an Op produces no
PersistData type.

#### 1.2 Palette legality

Ops are app-level combinational logic; they belong wherever expression authoring happens.

| Scope | Offers Op? | `AppScopeKindComposable` row |
|---|---|---|
| root (no scope) | yes | `scope < 0` branch already passes (`:6412`) |
| Task-type Layer | yes | extend `:4950-4951` to also return true for `ImGuiAppNodeKind_Op` |
| Window / Sidebar / Control / Struct / other layers | no | default `false` |

Domain: `AppScopeParentOf` gains `case ImGuiAppNodeKind_Op: return <Task layer id>` (same fallback the
standalone Struct uses, `:4760-4762`) — an Op is Task-domain app data. `AppScopeCanEnter` is **not**
extended: an Op has no interior.

#### 1.3 Type rules (ride AppEventExprCheck)

An Op subtree renders to an expression **string**, and that string is checked by the existing parser
(`AppExprOr`, `:10458`) exactly as a hand-typed event Expr is. There is no separate pin-type resolver:

- A **leaf** operand is an expression *primary* — either wired from a Field node's `value` DataOut
  (rendered as the producer's `param->field`, the same primary `AppExprPrimary` resolves for a dep at
  `:10267-10281`) or an inline token typed into the pin (`temp_data->hovered`, `data->armed`, `3.0`,
  `true`). The inline token is validated by a single-primary parse — the same engine.
- A **composed** operand is another Op wired into the pin; the fold recurses, substituting that Op's
  parenthesized substring.
- Each operator's validity is exactly its grammar rule: **NOT** → `!x`, requires bool (`:10327-10333`);
  **compare** → `x > y`, requires numbers, yields bool (`:10394-10407`); **AND/OR** → `&&`/`||`, two bools
  (`:10443-10471`); **XOR** → `^`, two bools or two ints (`:10425-10441`); **min/max** → `ImMin/ImMax(a,b)`,
  numeric-promoted; **select/mux** → `c ? t : f`, a bool condition and two same-class arms.

`?:`, `ImMin`, and `ImMax` are **not yet in the grammar** (`AppExprPrimary` parses only literals, parens,
and field-ref roots, `:10231-10323`). F54 **extends `AppEventExprCheck`** with exactly a ternary primary
(condition bool; arms paired like `==`, numeric∪numeric or bool∪bool, `:10409-10423`) and recognized
`ImMin/ImMax(a,b)` calls (numeric, promoted). This is the sanctioned grammar growth (`:10117-10118`) and it
is *required*: without it, min/max/select folds would emit C++ the checker cannot re-parse, breaking the
round-trip guarantee below. AND/OR/XOR/NOT/compare need no grammar change.

#### 1.4 Validation

- **Cycles refused** by the shared guard: Op result→operand wires are `Data` edges, so
  `AppGraphResolveLink`'s `AppGraphDataReaches` check (`:3032-3034`) already rejects a chain that loops.
- **Arity**: every operand pin must be wired or carry an inline token; an empty operand is an error (mirrors
  the empty-expr handling at `:10478-10479`, which is legal only for a whole-event default).
- **Type**: the folded string is run through `AppEventExprCheck` and, when the chain feeds a SetField event,
  fit to its `DstField` (`:10497-10515`). This is one call to the existing checker — the structural graph and
  the string agree by construction.

#### 1.5 Op-fold codegen (F55)

Concrete chain: `AND( compare-gt( random_time->max_timer_secs , 0 ) , data->armed )` feeding a Control that
gates a `Fire` command. The chain folds to the string `(random_time->max_timer_secs > 0) && data->armed`
(the inverse of the recursive-descent parser). No node is emitted for the Op; the string lands in the
consumer's body:

As a command gate (level form; `OnGetCommand` sees `data`/`temp_data`/deps but not `last_temp_data`, so the
guard reads them directly — the existing Active-edge path at `:10667-10671`, with the folded expression in
place of a bare temp field):

```cpp
virtual void OnGetCommand(const ImGuiApp* app, ImGuiAppCommand* cmd,
                          const FireData* data, const FireTempData* temp_data,
                          const RandomTimeData* random_time) const override final
{
  IM_UNUSED(app); IM_UNUSED(temp_data);
  if ((random_time->max_timer_secs > 0) && data->armed)
    *cmd = (ImGuiAppCommand)AppCommand_Fire;
}
```

As a SetField value (the assignment path at `:10826`, `data->%s = %s;` with the folded Expr):

```cpp
  data->lit = (random_time->max_timer_secs > 0) && data->armed;   // OnUpdate
```

An edge-gated command keeps the existing latch shape (`:10534-10554`, `:10797-10836`): OnUpdate sets the
`FirePending` latch on the folded condition's rising edge, OnGetCommand emits it the same frame (Task updates
before Command collects, `big-idea.md`).

#### 1.6 Import note (binding)

The generated C++ contains only the folded string — the graph structure of the Op nodes is **not**
recoverable from it. On re-import (the inverse of `AppEmitControlWithDeps`) the string restores as an
`ImGuiAppEventDesc::Expr` (`imguiapp_nodes.h:385`), **not** as Op nodes. The Op structure's only home is the
`.graph` file (`AppEmitNodeRecord`, `:11613-11644`). F55 records this: *folded output re-imports as an
expression, not as op nodes.* This is consistent with the project's rule that the graph file, not the C++,
carries authoring structure the runtime does not need.

---

### 2. Animation builtins (Tween / Timer / Spring / Pulse) — F56

> **VERDICT: BUILD** as four **builtin Controls** via `AppGraphAddBuiltin` (`:1951-1970`), the RandomTime
> precedent (`imguiapp_demo.cpp:275`). They are ordinary controls — `ImGuiAppNodeKind_Control`,
> `IsBuiltin = true`, a compiled C++ type, a typed `DataOut` — so palette legality, scope domain, wiring,
> codegen, mirror, and time-travel all reuse the control machinery unchanged. The only new surface is an
> **"Animation" palette section** grouping the four and their compiled types.

#### 2.1 The set and their DataOut

Each is dt-driven in the **Task** phase (`OnUpdate(dt, data, temp, last_temp, ...)`) and publishes a typed
PersistData `DataOut` consumed downstream in dependency order. The accumulator lives in PersistData — the
sole mutator is OnUpdate — exactly like `RandomTimeData::rng` (seeded in OnInitialize, stepped only by
OnUpdate; `imguiapp_demo.cpp:68-69,95-106`).

| Builtin | PersistData (DataOut) | Task-phase update | Trigger / input |
|---|---|---|---|
| **Timer** | `{ float elapsed; bool done; }` | `elapsed += dt; done = elapsed >= duration;` | (re)start on a rising trigger |
| **Tween** | `{ float value; float t; bool done; }` | `t = ImClamp(t + dt/duration, 0,1); value = ease(a,b,t);` | restart on rising trigger; `a`,`b`,`duration` are params or deps |
| **Spring** | `{ float value; float velocity; }` | `velocity += (k*(target-value) - c*velocity)*dt; value += velocity*dt;` | `target` from a dep/field |
| **Pulse** | `{ bool pulse; float phase; }` | `phase += dt/period; if (phase>=1){ phase-=1; pulse=true; } else pulse=false;` | free-running; `period` a param |

`duration`, `period`, `target`, `stiffness`, `damping`, and the ease selector are PersistData params
(authored constants) or wired dependency fields. Taking inputs from **wired deps** (not required OnRender
input) keeps them headless-deterministic — no injected input, per the headless-only verification rule.

#### 2.2 Phase discipline (each obeys phase-coherence)

All four update in Task and are pure-published: OnUpdate is the sole writer, the DataOut is set before any
consumer reads it because Task runs consumers in dependency order (`big-idea.md`; the type-keyed DAG). None
sizes or styles UI from measured geometry, so none can trip the stale-frame class (`bug-classes.md §1`).

The **temp^last** edge idiom (`big-idea.md`; `imguiapp_demo.cpp:167`) appears in two places:

- **Trigger restart** (Timer/Tween): the trigger is a TempData bool recorded from a dep or OnRender; OnUpdate
  restarts on `temp_data->trigger ^ last_temp_data->trigger` (rising) — the exact `^` shape codegen already
  emits for authored events (`:10824`, `AppEmitEventGuard`).
- **Downstream edge consumption** (Pulse): `pulse` is a one-frame flag; a consumer that wants the *edge*
  mirrors it into its own temp and compares temp^last — no new mechanism.

Spring is the one integrator whose backward step is not the inverse of its forward step (a damped
oscillator is not time-symmetric). It stays deterministic anyway because **App-time scrub rewinds by
snapshot restore, not by backward integration** — see §2.4.

#### 2.3 Codegen (push + wiring)

A builtin control emits no struct body (its type is compiled); `GenerateAppGraphCode` emits the bring-up
line and its wiring, identical to any builtin. Example — a Tween driving a consumer's `col`:

```cpp
  ImGui::PushAppControl<ImAppTween>(app);   // Animation builtin (compiled type)
  // ... consumer control pushed after, in dependency order:
```

and inside the consumer's `OnUpdate`, the dependency binding line the emitter already produces
(`:10707-10761`): `data->col = tween->value;` (or via an explicit binding row). The dep param
(`const ImAppTweenData* tween`) is threaded through OnInitialize / OnUpdate / OnGetCommand / OnRender by
`emit_dep_params` (`:10632-10641`) — unchanged. Wiring, one-producer-per-type, and cycle refusal are the
shared `AppGraphResolveLink` rails: a builtin's DataOut carries its real `DataTypeId` (`:1955-1965`,
`:1287-1288`), so two Tweens into one consumer collide (`:3016-3031`) and a feedback loop is refused
(`:3032-3034`) — the same guarantees every control gets.

#### 2.4 App-time scrub determinism (F29 StateHistory, Fixed-dt)

Every animator's whole state is its PersistData accumulator, held in registered snapshottable storage. Under
Fixed-dt, `ImGuiAppStateHistory` byte-snapshots that storage each frame and restores it on scrub
(`big-idea.md`, "time travel is a theorem"). Because OnUpdate is the sole mutator and dt is fixed,
restore-and-replay reproduces the trajectory byte-for-byte (contract 7) — the RandomTime rng precedent
generalizes to Tween `t`, Timer `elapsed`, Spring `{value,velocity}`, and Pulse `phase`. The accumulators
must therefore be **fully inside PersistData** (no static/local carry, no TU global) — the standing
no-TU-globals rule, here load-bearing for determinism. *Accept (F56): Tween advances deterministically under
Fixed-dt and App-time scrub restores it.*

---

### 3. Layout nodes — F57 (all three candidate models evaluated)

#### 3a. Window placement facts — baseline

> **VERDICT: KEEP, do not replace.** These already exist post-F02 and are correct for their job.

`HasInitialPlacement` / `InitialPos` / `InitialSize` / `DockDir` / `DockSize` are fields on the Window/
Sidebar node (`imguiapp_nodes.h:400-404`), inspector-edited (`:4176-4214`), shown on the scope wall
(`:5588-5601`), serialized as `Init=` / `Dock=` lines (`:11630-11633`, parsed `:11934-11935`), and emitted
into bring-up (`:11216-11224`, `:11553-11588`). They express *absolute first-use placement of one host* —
not composition. They are **fields, not a node kind**, and the Layout family below is additive on top of
them, never a rewrite. (`InitialPos/Size` are today gated to live-mirror emission at `:11457-11458`,
`:11561-11566`; making them emit for authored windows is an orthogonal, already-serialized tweak, out of
scope here.)

#### 3b. Region / Split / Tabs composing into the Layout layer — PRIMARY

> **VERDICT: BUILD.** One appended kind `ImGuiAppNodeKind_Layout` with a variant token (`Region` / `Split` /
> `Tabs`) in `TypeName` (serialized `Type=`, `:11620`), mirroring the Op-operator decision. These nodes give
> the **Layout layer its first real domain** — the dockspace tree that `OnLayout()` already anticipates
> (`imguiapp.h:906`; caption `:5361`). Windows reference the region they dock into via a **node field**, not
> an edge (justified below). Codegen emits the DockBuilder sequence into an `OnLayout()` override.

**Palette legality.** Extend `AppScopeKindComposable` (`:4932-4958`): the `Layer` /
`ImGuiAppLayerType_Layout` branch (today `return false`, `:4954`) returns true for `ImGuiAppNodeKind_Layout`;
a Split/Tabs node's own scope accepts nested `ImGuiAppNodeKind_Layout` children. Windows do **not** compose
into the Layout scope — they stay Display-domain and *reference* a region (see below), so the two layers do
not tangle their containment trees.

**Scope domain + interior.** `AppScopeParentOf` (`:4746-4782`) gains `case ImGuiAppNodeKind_Layout`:
returns the parent Layout node when nested, else the Layout layer id. `AppNodeInScope`'s Layout-layer branch
(today `return false`, `:4851-4852`) returns true for Layout nodes whose scope-parent chain reaches this
layer — the same chain walk the other layer scopes use (`:4853-4866`). `AppScopeCanEnter` (`:4821-4825`)
gains `ImGuiAppNodeKind_Layout`: a Split/Tabs is **enterable**, its interior showing its child regions — the
scope grammar of [scope-interior-design.md](#scope-interior-design) applies unchanged (walls = the
Split's Begin/End, members = child regions, rail order = split order).

**Windows reference their region: node FIELD, not reference edge.**

> **VERDICT: node field.** The Window/Sidebar node gains a `RegionRef` (target Layout node name), inspector-
> edited like `DockDir`, serialized as one `Region=` line (append-only, exactly parallel to `Dock=` at
> `:11632`).

Justification: *where a window docks* is already a window field — `DockDir`, `InitialPos`, `HasInitial-
Placement` all live on the node and are edited in one place, the inspector (`:4176-4214`). A region
reference is the same category of fact and belongs in the same home. A reference **edge** would (1) fork
window placement across two homes (some in fields, some in a wire) — the "two editors for one concept"
defect the scope-interior doc explicitly rejected (its rejected-alternative "config editing in the wall
title bar"); (2) create a second cross-tree relation on Windows, which already have a Display-layer
containment parent — a Layout→Display wire crosses layers and re-introduces the hidden-dependency risk; and
(3) need a new edge kind or an overload of the Data edge whose one-producer-per-type / cycle semantics
(`:3012-3034`) are meaningless for a placement reference. The field carries none of that baggage: the
DockBuilder emitter reads it directly, and the canvas can still *show* the reference as a derived
portal-style chip (draw-list only, the scope-interior rule-E pattern — no model edge, rebuilt each frame).

**Validation.** A `RegionRef` naming a missing/non-Layout node is a validation error (like a dangling dep).
Nested Layout composition is a forest (containment single-parent, `:3040-3048`); a Tabs/Region cannot
contain a Split cycle (same containment guard). A Split must have its children; an empty Split is a warning.

**Codegen (dock-builder sequence).** Emits an `ImGuiApp::OnLayout()` override (`imguiapp.h:906`) — a
first-use-guarded DockBuilder pass, the standard imgui idiom, run before Display windows Begin (layer order
`:1981-1991`; caption `:5361`). Windows dock by their pushed label (`PushAppWindow<Base>`, `:11454`):

```cpp
virtual void OnLayout() override
{
  ImGuiID root = ImGui::GetID("AppDockSpace");
  if (ImGui::DockBuilderGetNode(root) != nullptr) return;   // build once
  ImGui::DockBuilderRemoveNode(root);
  ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(root, ImGui::GetMainViewport()->WorkSize);
  ImGuiID left, center;
  ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, 0.25f, &left, &center);   // Split node
  ImGui::DockBuilderDockWindow("Outliner", left);      // window RegionRef = left
  ImGui::DockBuilderDockWindow("Viewport", center);    // window RegionRef = center
  ImGui::DockBuilderFinish(root);
}
```

Region/Split/Tabs map to `AddNode` / `SplitNode` / a tab-bar node respectively; each window's `Region=`
field selects its `DockWindow` target. This emits into a real user override point — no upstream edit. The
build is available (the running context already has dockspaces, `:7558-7587`; `imgui.ini` `DockSpace`).
*Accept (F57): a window composed into a region docks accordingly in a headless-verify frame.*

#### 3c. Constraint / anchor edges

> **VERDICT: REJECT** (formal, per F53's charge). Recorded in the rejected-alternatives section below.

---

### 4. Serialization & round-trip

| Family | New serialized surface | Round-trip home |
|---|---|---|
| Op | `Kind=` (appended int), operator in `Type=`, operand `Port=` rows (`:11617,11620,11637`) | `.graph` file only; C++ carries the folded **string** (re-imports as `Expr`, §1.6) |
| Animation | none new — builtin control `Kind=Control`, `Builtin=1`, `Type=`/`DataType=` (`:11619-11621`) | full round-trip via the control importer; compiled type supplies the shape |
| Layout | `Kind=` (appended int), variant in `Type=`; Window `Region=` line (parallel to `Dock=`, `:11632`) | `.graph` file; C++ carries the DockBuilder sequence + `DockWindow` labels |

All three obey the append-only kind rule (`imguiapp_nodes.h:247`): `ImGuiAppNodeKind_Op` and
`ImGuiAppNodeKind_Layout` are appended after `_Note`, before `_COUNT`. F01/F05 (record survives
save/load/undo) cover them by extension, not by parallel rails.

### 5. Phase-coherence compliance (checklist applied at design time)

- **Ops** compute nothing at runtime — they fold to a string at codegen; no measured geometry, no
  cross-frame value, no phase to violate.
- **Animation builtins** update in Task (OnUpdate, sole mutator), publish DataOut before consumers read it
  (dependency order), and never read measured UI geometry — clean of all three species
  (`bug-classes.md §1-1c`). Determinism rests on state living in PersistData (§2.4).
- **Layout** codegen runs in `OnLayout()` before any window Begins (`:5361`), so no window reads a
  half-built dock tree; DockBuilder is imgui's own once-guarded build. The canvas region-reference chip is
  draw-list-only, rebuilt each frame from the model (rule-E pattern), reading no previous-frame pixels.
- Every canvas decoration these kinds add (Op pins, Layout walls/portals) uses the established
  transform-fresh / post-submission read-back paths; none introduces a measure→apply loop.

### 6. Rejected alternatives

- **Ops as runtime objects** (an emitted `ImAppOp` control per node). They have no PersistData, produce no
  `app->Data` type, and would need a push line, a topo slot, and a mirror record for a value that is pure
  combinational logic. Folding to the consumer's expression is zero runtime cost and keeps the emitted code
  identical to what a demo author writes by hand — the codegen thesis.
- **A parallel Op type-checker.** Duplicating the scalar lattice invites drift between what the graph accepts
  and what the string compiles to. Folding-then-checking through the single `AppEventExprCheck` (extended
  minimally for `?:` / min-max) keeps one authority and preserves round-trip import.
- **Op result as a typed (non-zero `DataTypeId`) DataOut.** It would trip one-producer-per-type
  (`:3028`) and forbid fanning a result into two consumers — wrong for stateless logic. Id 0 opts out
  cleanly while the cycle guard still applies.
- **Keyframe / timeline animation nodes** (a track editor). Deferred (roadmap): the builtin set (Tween/
  Timer/Spring/Pulse) covers the dt-driven idioms with four compiled controls and zero new UI surface;
  timelines are a large parked feature, not gated by F53.
- **Windows→region reference EDGE** (a Data or new edge kind). Rejected in §3b: forks placement across two
  homes, crosses the Display/Layout layers, and overloads edge semantics that don't apply to a placement
  reference. The field + derived chip gives the same legibility without the model cost.
- **Layout nodes as a replacement for placement facts.** Absolute first-use placement (§3a) and
  compositional docking (§3b) are different jobs (one host's pixels vs the workspace tree); the layout family
  is additive.
- **Constraint / anchor-edge layout** (Auto Layout / cassowary-style relations between windows). REJECTED.
  (1) There is no phase for it — a constraint solver is a runtime relaxation step the four-phase pipeline
  does not have, and inventing one contradicts the framework's thesis. (2) There is no imgui primitive to
  emit *to*; codegen would have to emit a bespoke non-foldable solver, the opposite of the "generate the code
  a human writes" goal. (3) It mirrors no runtime object — like the synthetic portal nodes the scope-interior
  doc rejected, constraint edges would leak into validation, codegen, and the outliner with special-casing in
  each. (4) Dock nodes (§3b) already give the compositional layout windows actually use. Anchors stay parked
  (roadmap: *constraint layout edges unless F53's verdict builds them* — it does not).


---

<a id="input-command-binding-design"></a>

## Input→Command Binding — remappable chords over the reified command registry

How the Composer separates the *trigger* (a key chord) from the *verb* (an editor command), so a user
can rebind which chord runs Copy/Paste/Delete/… without touching the verb. Retrofit of the F34
command registry, not a parallel system.

Primary source: Robert Nystrom, *Game Programming Patterns*, "Command" (Design Patterns Revisited,
first pattern). Read verbatim; quotes below are cited to that chapter. Companion docs:
[composer-ui-design.md](#composer-ui-design) (§T2 status-bar keymap, §"four roads"),
[metrics-debugger-coherence-design.md](#metrics-debugger-coherence-design) (surface hidden
diagnostics), [bug-classes.md](bug-classes.md) (derive once, in phase),
[archive/feature-complete-checklist.md](archive/feature-complete-checklist.md) (F01 harness, F58 precedent).

### 1. The problem — input welded to verb

The editor's verbs live in one table, `s_editor_commands[]` (`imguiapp_nodes.cpp:6340-6384`). Each row
is `{ Id, Icon, Label, Shortcut, Key, Mods, Surfaces, AddKind }` (`imguiapp_nodes.h:739-749`). `Key`
and `Mods` are **hardcoded inline in the table**: `Edit: Copy` *is* `ImGuiKey_C | ImGuiMod_Ctrl`, welded
at compile time. The chord and the verb are the same datum, so a user cannot choose which key does Copy.

Worse, the chord is welded twice. Verbs reach two *different* dispatch paths:

- **The palette** reifies them: an `Id`-keyed `switch (run)` (`imguiapp_nodes.cpp:8494-8649`) fed by rows
  rendered straight from the registry (`:8420-8438`). This is already the Command pattern — a table of
  verbs dispatched by identity.
- **The keyboard** does *not* go through that switch. A parallel wall of inline `IsKeyPressed()` /
  `IsKeyChordPressed()` handlers performs the action directly at the key site: `F`/`Home`/`L`/`G`/`N`
  (`:7907-7932`), `F2` (`:7945`), `Tab`/`Esc` (`:7957-7976`), `[`/`]` (`:8052-8069`), and
  `Ctrl+C`/`Ctrl+V`/`Ctrl+D`/`Ctrl+Z`/`Ctrl+Y` (`:9148-9199`). So "Copy" exists as **both** `switch`
  case 16 (`:8522`) and the inline `Ctrl+C` block (`:9150`) — two implementations, one verb, and the
  chord is a literal in the second.

Goal: one indirection. A pressed chord resolves to a command `Id` through a **remappable map**, then a
**single dispatcher** runs it — the palette switch and the keyboard share that one path. Rebinding
becomes editing the map.

### 2. Command-pattern framing (what exists, what's missing)

Nystrom's tagline: *"A command is a reified method call."* — a method call "wrapped in an object … that
you can stick in a variable, pass to a function." He notes the GoF framing, *"Encapsulate a request as
an object, thereby letting users parameterize clients with different requests, queue or log requests,
and support undoable operations,"* and the better slugline: *"Commands are an object-oriented
replacement for callbacks."*

The chapter's *"Configuring Input"* section is this task exactly:

> "many games let the user configure how their buttons are mapped. To support that, we need to turn
> those direct calls to `jump()` and `fireGun()` into something that we can swap out."

His `InputHandler` holds a `Command*` per button and `handleInput()` delegates to it, so that *"where
each input used to directly call a function, now there's a layer of indirection."* Rebinding is
reassigning the pointer. He later returns a command from `handleInput()` rather than executing it in
place — *"we can delay when the call is executed"* — putting one dispatcher between input and verb.

**What this project already has: the reified verbs.** The registry is the command list; the `Id`-keyed
`switch` is `execute()`. C++'s "limited support for first-class functions" (chapter, *"Classy and
Dysfunctional?"*) is why the reification here is an `Id` + a `switch` rather than one class per verb —
and that is sufficient; a class explosion buys nothing, since undo is already a separate snapshot
system (`ImGuiAppEditorUndo`, `imguiapp_nodes.h:526-533`), not per-command `undo()`.

**What's missing: the swappable input→command MAP and the single InputHandler indirection.** Today the
"map" is a compile-time column (`Key`/`Mods`) and the keyboard bypasses the dispatcher entirely. This
design adds the map and routes both surfaces through one `execute()`.

### 3. Data model

#### 3.1 Registry stays the DEFAULT source; the keymap is a sparse override

Decision: **`Key`/`Mods` remain in the registry as the factory-default source of truth.** The runtime
keymap is *derived* from it, and per-graph user changes layer on top as a sparse diff. Rejected
alternative (move chords entirely into the keymap) in §9.

Justification: (a) "unchanged out of the box" is then free — an empty override set means the effective
keymap *equals* the registry-derived default; (b) the registry stays the self-documenting completeness
anchor the four-roads test iterates (`step72`, §7); (c) persistence stays minimal — a default graph
serializes **zero** keymap lines, so F01 byte-stability is trivially preserved (§5); (d) reset-to-default
is a *delete*, not a rewrite.

#### 3.2 Two representations

- **Persisted model state — the override list.** A new member on the graph:
  `ImVector<ImGuiAppKeyBinding> Keymap;` on `ImGuiAppGraph` (`imguiapp_nodes.h:619-661`, beside
  `Bindings`/`ScopePlacements`). Per the no-TU-globals rule it rides the document object — **not** a
  file static, and **not** on `ImGuiAppEditorState` (that struct is transient/unserialized,
  `:537-615`), because a keymap survives save/load. Each record:

  ```
  struct ImGuiAppKeyBinding { int CmdId; ImGuiKey Key; int Mods; };   // Key==None => explicit unbind
  ```

  Only *changes from default* are stored: a rebind writes `{ CmdId, newKey, newMods }`; an explicit
  unbind writes `{ CmdId, ImGuiKey_None, 0 }`. Keyed by the **stable** `Id` (0,1,2,10,… — sparse and
  fixed, matching the `switch` cases), never by array index, so reordering the registry never corrupts
  a saved keymap.

- **Derived effective table — transient.** `AppKeymapResolve(g)` folds the registry defaults with the
  overrides into an ordered `{ chord → CmdId }` list, in registry order. Transient like `_TrunkRoutes`
  (recompute on override change; the register is small). This is the InputHandler's live map.

#### 3.3 Resolution, precedence, conflicts

Effective chord of a verb = its override if one exists, else its registry default. Dispatch walks the
effective table and fires the **first** binding whose chord matches — order is precedence, mirroring the
chapter's `handleInput()` scanning buttons in sequence. A *conflict* is two active bindings sharing one
chord; the first-in-registry-order wins, so a conflict is never undefined — only a **shadowed** verb,
which the rebind UI surfaces as a warning (§6), in the spirit of "surface hidden diagnostics"
(metrics-debugger design). Exact-mods matching (via `IsKeyChordPressed`): `H` (id 25) and `Alt+H`
(id 26) do not collide, and `Ctrl+Z` never fires plain `Z`.

#### 3.4 Reserved / unbindable chords

- **`Space` and `Ctrl+P`** open the palette (`imguiapp_nodes.cpp:7931`). They are the escape hatch to
  *every* verb (the palette is the completeness surface, `step72:5534`), so they are **reserved**:
  never remappable, never a legal rebind target. Unbinding any other verb still leaves it reachable
  here.
- **Text-input focus.** Every handler already guards on `!GetIO().WantTextInput`
  (`:7907`, `:9148`); the resolver inherits that guard, so single-letter defaults (`L`,`G`,`F`,`N`,`H`)
  are inert while renaming a node or typing in a filter. This is *why* bare letters are safe defaults —
  the reserved-context guard, not a reserved-chord list, protects them.
- **Dedicated-handler verbs (phase-1 boundary).** `Delete` (id 19), `Tab` (id 23), `Esc` (id 24) keep
  their own bespoke handlers rather than routing through the keymap: `Delete` is *wire-aware* (it also
  removes a selected link, which the single-verb `case 19` does not), and `Tab`/`Esc` carry dual-mode
  scope navigation (Tab enters or, with nothing enterable, goes up). They therefore are **not
  rebindable** this phase — `AppKeymapCommandRebindable` excludes them, and the resolver skips them, so
  their keys never double-fire. They still have registry defaults (so §7's invariant holds) and can join
  the keymap later once their extra semantics are expressed as verbs.

### 4. Dispatch flow

> **Phase-1 realization.** The dispatcher is a `run_command` **lambda** inside `ShowAppGraphEditor`
> (capturing the view helpers `fit_all`/`fit_ids`/`snap_grid` by reference), not a free function — same
> single-execute indirection, no signature plumbing for the view-op captures. The clipboard/edit/view/
> toggle/order/hide/rename verbs route through the keymap; `Delete`/`Tab`/`Esc` keep dedicated handlers
> (§3.4). The steps below describe the general design; the shipped scope is that boundary.

1. **Extract one dispatcher.** Lift the palette `switch (run)` (`:8494-8649`) into a free function
   `AppGraphRunCommand(ImGuiApp*, ImGuiAppGraph* g, int cmd_id, const ImVec2* at, int* sel)` — the
   Composer's `command->execute()`. The palette calls it with the picked `Id`; the keyboard calls it
   with the resolved `Id`. The parallel inline handlers (`:7907-8069`, `:9148-9199`) are **deleted** and
   replaced by the resolver pass. Where an inline handler and its `switch` twin diverge (e.g. `Ctrl+C`
   at `:9150` copies the canvas selection with a `selected_node_id` fallback, while case 16 at `:8522`
   copies `g->Selection`), unification adopts the single more-complete behavior as a deliberate, tested
   reconciliation — not a silent drift.
2. **Resolver pass** (canvas focused, not text-input): walk `AppKeymapResolve(g)`; for the first binding
   whose `IsKeyChordPressed(Key | Mods)` fires **and** whose verb passes
   `AppGraphEditorCommandAvailable(g, c)` (`:6398-6413`, the same predicate that greys a palette row),
   call `AppGraphRunCommand`. A disabled verb's chord is inert, exactly as its palette row is.
3. **Palette openers stay hard-wired** at `:7931` — outside the remappable keymap — so the escape hatch
   cannot be rebound shut.
4. **Host commands** (`ImGuiAppGraphHostCmd`, `imguiapp_nodes.h:836-845`) keep their host-owned meaning
   and per-frame registration (`AppGraphSetHostCommands`/`ConsumeHostCommand`,
   `imguiapp_nodes.cpp:3263-3273`). Their chord match (`:7936-7941`) routes through the *same* resolver
   helper so editor-vs-host precedence is defined (editor keymap scanned first, host chords second) and
   a user rebind that collides with a host chord is **detected** by the shared conflict scan rather than
   silently shadowing. Host chords are not user-rebindable in this phase (host-owned pointers, not part
   of the persisted verb set); a three-tier future is noted in §9.
5. **Downstream displays read the resolver, not `c->Shortcut`.** Palette rows (`:8436`), the F1 help
   card (id 34), and the status-bar keymap hint (`AppGraphStatusHint`, `imguiapp_nodes.h:826`;
   composer-ui-design §T2) show the *effective* chord, so a rebind is reflected on every surface from
   one source. The static `Shortcut` column remains the default's display seed only.

### 5. Persistence

`AppGraphSerialize` (`imguiapp_nodes.cpp:11477-11490`) emits `[Graph]`, `NextId=`, node records,
`Link=`, `Bind=`, `Place=`. Append one **sparse** record kind after `Place=`:

```
Keybind=<cmdId>,<keyInt>,<modsMask>        # Key==0 (ImGuiKey_None) encodes an explicit unbind
```

Only overrides serialize, so a graph the user never rebound writes **no** `Keybind=` lines — its file is
byte-identical to the pre-feature serialization. Deserialize reads them into `g->Keymap`; an unknown/
retired `cmdId` is dropped on load (graceful, like dangling selection ids are cleared).

**F01 harness extension — the F58 precedent.** New persisted model state joins the field-by-field
compare, which catches a record that *load* drops even when the serializer omits it (byte-stability
alone cannot see that; `archive/feature-complete-checklist.md:12-18` — "authored order once F58 exists"
extends the same harness). Add to `AppGraphModelEqual`
(`tests/imguiapp_nodes_tests.cpp:140-229`, after the `Bindings`/`ScopePlacements` blocks `:213-226`):

```
APP_NEQ(a.Keymap.Size != b.Keymap.Size, "Keymap.Size");
for (i) { APP_NEQ(CmdId ‖ Key ‖ Mods differ, "keybind …"); }
```

and extend `step49_maximal_roundtrip` (`:3776-3895`) to author ≥1 rebind and ≥1 unbind, so the maximal
rail exercises the record: build → serialize → load → `AppGraphModelEqual` → reserialize → byte-identical.

### 6. Rebind UI — the keymap editor

A panel (inspector section, or a modal reached from the palette verb *"Edit: Keyboard shortcuts…"* and
the F1 card), listing every verb that can carry a chord. Row grammar:

```
[icon]  Label ..................................  [ chord chip ]  [reset]
```

- **Chord chip** shows the effective chord (or "—" when unbound). Click → chip enters *listening*; the
  next `IsKeyChordPressed` over the held keys becomes the override; `Escape` cancels capture. The chip is
  a **flat custom button** with a stable `###keychip_<cmdId>` id — never `SmallButton`/`ArrowButton`
  (no-default-glyph-buttons rule), and a single addressable id per chip (test-addressability rule:
  draw-list/multi-widget rows have no `ItemExists` id).
- **Conflict** — if the captured chord is already active on another verb, show inline
  `⚠ conflicts with <Label>` plus `Reassign` (steal: unbind the other) / `Cancel`. A reserved chord
  (§3.4) as target is refused outright.
- **Reset** per row drops the override (`###reset_<cmdId>`); a `Reset all` clears `g->Keymap`.
- Chrome: theme-derived colors (`kAppHue*` → `AppThemeAccent`), em spacing, DPI-invariant — per the
  theme/DPI invariants rule.

### 7. step72 invariant evolution

Today: `has_shortcut (Surface_Shortcut) ⟺ c->Key != ImGuiKey_None`
(`tests/imguiapp_nodes_tests.cpp:5536`). The registry `Key` becoming the *default-binding source*
re-expresses this through the resolver:

- **Evolved invariant:** `Surface_Shortcut ⟺ the verb has a DEFAULT binding` —
  `AppKeymapDefaultChord(cmdId) != none`. Same truth, sourced from the resolver instead of the inline
  column.
- **New clause — the factory keymap is conflict-free:** no two *default* bindings share a chord (a
  compile/test-time guarantee that the shipped map has no self-collisions), and no default binding
  targets a reserved chord (§3.4).
- Palette reachability (`step72:5548-5564`) is unchanged: the palette still renders from the registry,
  and unbinding a chord never removes the palette row (completeness holds).

### 8. Validation + edge cases

- **Text input:** resolver inert while `WantTextInput` — single-letter safety (§3.4).
- **Exact-mods matching:** `Ctrl+Z` ≠ `Z`; `H` ≠ `Alt+H`. The resolver compares the full mod mask.
- **Availability gating:** a disabled verb's chord does nothing (matches its greyed palette row).
- **Retired/unknown `cmdId` in a loaded keymap:** dropped on load; no dangling reference.
- **Reserved target:** rebinding onto `Space`/`Ctrl+P` is refused by the UI and never emitted.
- **Unbind then reach:** an unbound verb has "—" as its chip and still runs from the palette.
- **Host collision:** a user chord equal to a host chord is flagged by the shared conflict scan;
  precedence is deterministic (editor first).
- **Phase coherence:** the effective table is derived once per override change, before dispatch — no
  measured geometry, no camera input; nothing here mixes phases (`bug-classes.md`).

### 9. Rejected alternatives

- **Move `Key`/`Mods` out of the registry into the keymap entirely.** Forces every graph to serialize
  the full table, strips the registry of its self-documenting default and of the four-roads completeness
  anchor, and makes "unchanged out of the box" a thing you must *write* rather than get free. Rejected;
  registry stays the default source (§3.1).
- **Keep the inline `IsKeyPressed` handlers AND add a keymap.** Two dispatch paths — the welding bug
  persists and rebinds wouldn't reach the inline path. The whole point is one indirection (§4).
- **Store the full effective keymap in the graph (not a sparse diff).** Breaks F01 byte-stability for
  default graphs (every save writes ~20 lines equal to the default) and makes reset-to-default a rewrite
  instead of a delete.
- **Host-preference file instead of per-graph state.** Natural for "my Copy key on every graph," but the
  brief scopes bindings to the round-tripped document. A three-tier resolver — *graph override > host
  preference > registry default* — is a clean later extension (it would also make host chords
  rebindable); noted, not adopted now.
- **One polymorphic `Command` subclass per verb (GoF-literal).** The chapter's own *"Classy and
  Dysfunctional?"* caveat: in C++ the `Id`+`switch` reification already *is* the command list, and undo
  is a separate snapshot system, so subclasses add ceremony with no payoff.

### 10. Acceptance criteria (headless gates)

Driven through `imguix-headless-verify` (chord synthesis via `IsKeyChordPressed`, model inspection);
no OS input injection.

1. **Default parity.** With `g->Keymap` empty, every `Surface_Shortcut` verb fires on its registry
   chord, and a default graph saves byte-identical to the pre-feature serialization.
2. **Rebind.** `AppKeymapRebind(g, /*Copy*/16, Ctrl+Shift+C)` → `Ctrl+Shift+C` runs Copy; the old
   `Ctrl+C` does **not** (asserts the model delta of a copy appears on the new chord, absent on the old).
3. **Conflict.** Rebinding onto an already-active chord is detected (a `###conflict…` marker exists);
   the resolver's first-in-order winner is asserted deterministic.
4. **Unbind.** Unbinding Delete (19) makes `Del` inert; the verb still runs from the palette row
   (completeness holds).
5. **Reserved.** A rebind targeting `Space`/`Ctrl+P` is refused; both always open the palette.
6. **Round-trip.** Author a rebind + an unbind → save → load → `AppGraphModelEqual` true → reserialize
   byte-identical (extends `step49`); a no-override graph emits zero `Keybind=` lines.
7. **step72-evolved.** `has_shortcut ⟺ default binding exists`; the factory keymap has no duplicate
   active chord and no reserved-chord default.
8. **One dispatch.** For one verb, a palette click and its chord produce an identical model delta (both
   go through `AppGraphRunCommand`).


---

<a id="metrics-debugger-coherence-design"></a>

## Metrics/Debugger Coherence Upgrade — One Truth for State, Surfaced Diagnostics, Synced Views

### 1. Summary & goals

The "ImGuiAppLayer Metrics/Debugger" window (`demo.cpp:319`) authors the object-model node graph, but
the channels through which it *reports* state are mutually incoherent: the same fact is told two or three
ways with different precision/labels/color, half a dozen diagnostics the code already computes are thrown
away every frame, the three views (tree / canvas / code) silently disagree about what is selected, and a
read-only "live mirror" is visually indistinguishable from authored work — and is destroyed, not hidden,
when toggled off. This upgrade is **coherence-only**: no new authoring features, no model rewrite. It
makes every readout speak one grammar from one source.

**Thesis (four moves).**
1. **One truth for state** — FPS, composition counts, and graph health are computed once, after the live
   mirror reconciles, and rendered in a single status strip; the duplicate/divergent copies are deleted.
2. **Surface hidden diagnostics** — the link-rejection reason, the cycle node name, codegen staleness, the
   dropped-binding and hosted-control warnings: all already computed, all currently discarded, all given a
   home.
3. **Sync the three views** — selection becomes one window-level id with canvas→tree read-back,
   reveal/pan, and stale-id reconciliation.
4. **Make design-vs-live legible** — origin tint + a legend + the long-promised "promoted" badge, and a
   "Mirror live" that hides instead of deletes.

**Spine:** a shared `StatusPill(text, level)` grammar and a *reconcile-before-report* frame ordering, on
top of which every theme hangs. **House constraints honored throughout:** pointers never references
(`feedback_no_references.md`), `char[]`+`ImVector` (no `std::string`), imnodes confined to `nodes.cpp`,
em-based sizing, node bodies always submit ≥1 item (`feedback_imnodes_empty_node_body.md`). New surface is
minimal and additive: one trailing `char[]`+`int` pair on `ImGuiAppGraph`, one pure `AppGraphSignature`
helper, and two pointer/bool params on the already-single-caller `ShowAppGraphEditor`. Every judge-flagged
hazard is fixed or rejected in §9.

### 2. Current incoherences (grounded)

All refs `demo.cpp` unless prefixed. `[H]`=hidden state, `[I]`=incoherence, `[G]`=gap.

| # | Tag | Incoherence | Where |
|---|-----|-------------|-------|
| 1 | H | `AppGraphResolveLink` writes 8 precise rejection reasons; the sole call site declares `char err[128]` and never reads it — a rejected drag gives **zero** feedback | nodes.cpp:752-806, 826; nodes.cpp:1066-1067 |
| 2 | H | `topo_err` ("dependency cycle at <Node>") collapsed to a bare `ok`/`CYCLE` token; the always-on health readout is the useless one | nodes.cpp:1176-1181; demo.cpp:437-438 |
| 3 | H/G | No codegen-staleness signal: after any edit the panel still reads "Generated C++", Copy stays live, Write .h silently writes stale output, `write_msg` keeps confirming a stale file | demo.cpp:388-394, 549-560, 399-410 |
| 4 | H | `IsPromoted` computed every frame, rendered nowhere — the doc-promised promoted badge does not exist | nodes.cpp:1722-1733; node-editor-upgrade-design.md:383 |
| 5 | I | `nodes N / links N` conflate authored + live-mirror + promoted into one jumping number | demo.cpp:437-438; nodes.cpp:1657-1719 |
| 6 | I/G | Selection one-way (tree→canvas); canvas click never lights the tree; never reconciled on delete | demo.cpp:482,485; nodes.cpp:1788-1793 |
| 7 | I | "Mirror live" OFF runs `AppGraphRemoveNode` on every live node + sweeps incident authored links — destructive, not a view filter; tooltip says "overlay" | demo.cpp:510-518; nodes.cpp:669-692,424 |
| 8 | I | FPS shown twice: main `%.1f`+`%.3f ms` uncolored vs cluster `%.0f` color-coded, no ms | demo.cpp:308 vs 436,445-448 |
| 9 | I | Composition counts duplicated/reordered: header `L/W/S/C` labeled vs cluster `C# W# S#` unlabeled, Layers dropped | demo.cpp:301-302 vs 437-438 |
| 10 | H | Toolbar topo+counts run **before** the body reconciles the mirror — strip and canvas disagree for one frame on every toggle | demo.cpp:434-438 before 508-518 |
| 11 | G | `AppGraphCanLink` is a fully-built, public, dead validator — no pre-drop feedback | nodes.h:440; nodes.cpp:809-814 |
| 12 | G | Zero `PushColorStyle` in the module — live vs authored invisible on the canvas | nodes.cpp:957-961, 976-1017 |
| 13 | G | Selection id dangles after delete/strip/Load — highlight points at a ghost | demo.cpp:482; nodes.cpp:1454-1457 |
| 14 | G | Tree→canvas selects but never reveals; `EditorContextMoveToNode` exists, never called | imnodes.h:256; imnodes.cpp:2064-2071 |
| 16 | I | Live objects listed twice in the tree; only the lower copy clickable — top click reads as broken | nodes.cpp:1748-1774 vs 1780-1794 |
| 17 | G | Lifecycle/storage (`Initialized`, `StorageEntries`, `ShutdownPending`) live only in the main header, absent from the debugger | demo.cpp:300-304 |
| 18 | I | Window titled "Metrics/Debugger"; the only metric is a duplicated FPS | demo.cpp:319 |
| 19 | H | Right-edge cluster silently overlaps the buttons when narrow — no wrap/ellipsis/signal | demo.cpp:442-443 |
| 20 | H | Type-mismatched field binding dropped from codegen with no line/comment/warning | nodes.cpp:1289-1292 |
| 21 | H | Window-hosted-control limitation surfaces only as a buried trailing `// TODO` in generated source | nodes.cpp:1373-1374 |
| 22 | I | Generate tooltip "%d node(s)" uses conflated `graph.Nodes.Size` (incl. live mirror) | demo.cpp:395 |
| 26 | H | Tree→canvas `SelectNode` rests on an undocumented submit-order invariant; reordering panels → hard assert | nodes.cpp:1791-1792; imnodes.cpp:1958-1963 |

### 3. The design

#### 3.0 Shared primitives (the spine)

**(a) `StatusPill` grammar.** One demo-local lambda next to `HelpMarker` (demo.cpp:343), reused by every
status surface so health, counts, freshness, and rejection all read in the same visual language:

```cpp
// level: 0 neutral, 1 ok, 2 warn, 3 err. Reuses the EXISTING fps palette (demo.cpp:445-447) + TextDisabled.
auto StatusPill = [&](const char* text, int level)
{
  static const ImVec4 kCol[4] = {
    style.Colors[ImGuiCol_TextDisabled],          // neutral
    ImVec4(0.45f,0.85f,0.45f,1.0f),               // ok    (== fps green)
    ImVec4(0.90f,0.80f,0.35f,1.0f),               // warn  (== fps yellow)
    ImVec4(0.90f,0.45f,0.45f,1.0f) };             // err   (== fps red)
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(kCol[level], "%s", text);    // ASCII text only — no glyphs outside the default atlas
};
```

No new palette is invented; ASCII labels only (no `↑ → ⇒ ■ ✓ ◐`, which are not in the default font atlas).

**(b) Reconcile-before-report.** Hoist the mirror-reconcile block (currently demo.cpp:508-518, *after* the
toolbar) to the top of the window, right after `SeedAppGraph` (demo.cpp:332). `BuildAppLiveGraph` is
model-only — zero `ImNodes::` calls, confirmed nodes.cpp:1582-1734 — and its `_NeedsPlace` flags are still
consumed later inside `ShowAppGraphEditor` before `BeginNode` (nodes.cpp:951-955), so hoisting is safe.
Run `AppGraphTopoOrder` **once** on the reconciled graph and feed every readout from that single result.
This kills the one-frame strip-vs-canvas disagreement (#10) at the source.

> **Invariant to preserve (#26):** move only the pure-data reconcile block. Do **not** reorder the tree
> submission (demo.cpp:485) relative to the editor (demo.cpp:519) — the soon-to-be-removed
> `ImNodes::SelectNode` draw-order dependency lives in that gap and is retired by Theme C, not here.

---

#### Theme A — One Status Truth: the status strip

**Merges:** health-strip, canonical-status-strip, reconcile-then-report, fps-once, app-runtime-metrics.
**Targets:** #2, #5, #8, #9, #10, #17, #18, #19, #22.

**Change.** Delete the right-edge `rest_buf`/FPS cluster (demo.cpp:435-450) and the main-window "ImGuiApp
Status" composition/FPS/backend lines (demo.cpp:300-308); reduce that header to a one-line pointer
("See Metrics/Debugger → status strip"). Add **one** full-width framed strip between the toolbar
`EndChild` (demo.cpp:452) and the body (demo.cpp:457), built from one pass over `graph.Nodes`:

- **HEALTH** — `StatusPill` from the hoisted topo result. `"graph ok"` (ok) or `"cycle: <topo_err>"` (err),
  printing the computed string **verbatim** ("dependency cycle at Breathing"), with
  `SetItemTooltip("Code generation is blocked until this cycle is broken.")`.
- **NODES** — `design D · live L · promoted P`, where `D = !IsLive`, `L = IsLive`, `P = IsPromoted` over
  `graph.Nodes` (per-node flags already exist, nodes.h:382-383). The `promoted P` segment is **gated on
  `mirror_live`**, because `IsPromoted` is only recomputed inside `BuildAppLiveGraph` (#4 refinement) —
  show `design D` alone when the mirror is off.
- **COMPOSITION** — fully labeled `L# W# S# C#` read from the **object model** (`app.Layers/Windows/
  Sidebars/Controls.Size`), *not* the mirror, so the number is identical whether Mirror is on or off and
  Layers is no longer dropped.
- **PERF** — one color-coded FPS, the single FPS in the build: `"60 FPS  16.6 ms"` (`%.1f ms`; the old
  `%.3f` was false precision). Its tooltip earns the window's name — backend + real per-frame metrics:
  `"Win32 / DX11\n1234 vtx  1850 idx\nImGui windows: 5"` from `io.BackendPlatformName/RendererName`,
  `io.MetricsRenderVertices/Indices`, `io.MetricsActiveWindows`. Label it **"ImGui windows"**, never bare
  "Windows", so it can't be conflated with `app.Windows.Size`.
- **LIFECYCLE** — `StatusPill` for `Initialized`, plus `storage N` and `shutdown: yes/no` from
  `app.StorageEntries.Size` / `app.ShutdownPending` (#17 — the debugger can finally debug the lifecycle).

**UI sketch.**
```
+- ImGuiAppLayer Metrics/Debugger -------------------------------------------------+
| [+Add] | [Save][Load] | [Generate][Write .h][Code >] | [x] Show live mirror  (?) |
+----------------------------------------------------------------------------------+
|  graph ok  │  design 5 · live 4 · promoted 2  │  L2 W3 S1 C8  │  init  storage 7 │
|            │                                  │               │  60 FPS  16.6 ms │  <- hover: backend + vtx/idx
+----------------------------------------------------------------------------------+
   on a cycle:   cycle: dependency cycle at Breathing   (red, tooltip: "codegen blocked")
+----------------------------------------------------------------------------------+
|  tree            |               node canvas                  |   code (toggle)   |
```

**API / code touch.** Pure `demo.cpp`. The strip is its own row, so the narrow-window overlap guard
(demo.cpp:442-443, #19) is **deleted**, not relocated — render each segment as separate `StatusPill`
calls with `ToolSep()` rules; allow the strip child to wrap/clip within itself at the 46-em min width.
Counts come from one inline classification loop (`IsLive`/`IsPromoted`), no new public API. Conventions:
em spacing via the existing `ToolSep()`, `char[]`+`ImFormatString`, no references, imnodes untouched.

---

#### Theme B — Hidden-Diagnostics Surfacing

**Merges:** all link-reject-toast variants, topo-cycle-banner, all codegen-staleness variants,
codegen-warnings-count. **Targets:** #1, #2, #3, #11, #20, #21, #22.

##### B1 — Link-rejection reason (the #1 missing diagnostic)

Stop discarding the reason `CaptureAppGraphLinks` already holds. Add a transient, **non-serialized** pair
to `ImGuiAppGraph` (nodes.h, beside `EditingNodeId`), char[] per house style:

```cpp
struct ImGuiAppGraph {
  ...
  char LastLinkErr[IM_LABEL_SIZE];  // last refused-link reason; transient UI state, NOT in Save/Load
  int  LastLinkErrSeq;              // bumped on every rejection -> demo edge-triggers the fade
  ImGuiAppGraph() { ...; LastLinkErr[0] = 0; LastLinkErrSeq = 0; }
};
```

Write it **only** in the rejection branch — the `else` of `AppGraphResolveLink` inside the `IsLinkCreated`
block (nodes.cpp:826), where `err` is already populated — never in the unconditional `err[0]=0` reset at
nodes.cpp:820, and clear it on a successful create. Drive off the rejection branch, **not** the function's
`changed` return, which is also `true` on link *destroy* (nodes.cpp:848):

```cpp
if (AppGraphResolveLink(g, sa, ea, &s, &d, &k, err, err_size)) {
  ... push_back; changed = true;
  g->LastLinkErr[0] = 0;                                   // success silences any standing toast
} else {
  ImStrncpy(g->LastLinkErr, err, IM_ARRAYSIZE(g->LastLinkErr));
  g->LastLinkErrSeq++;                                     // re-fire even on identical back-to-back rejects
}
```

Render the toast where the eyes are and where imnodes is legal — inside `ShowAppGraphEditor`, immediately
after the `CaptureAppGraphLinks` call (nodes.cpp:1067). Anchor via `GetItemRectMin/Max` (the editor child
is the last item post-`EndNodeEditor`) at the canvas bottom-left, em-padded, fading alpha over ~2.5s; the
fade timer is a function-local static re-armed when `LastLinkErrSeq` changes (single owner, single home):

```
canvas ----------------------------------------------------
  (RandomTime) o------>o (Breathing)        <- drag loops back
  /!\ link refused: would create a dependency cycle
  .......................... fades after 2.5s ...............
```

This also revives the dead `AppGraphCanLink` (#11) as a scoped follow-up: while
`ImNodes::IsLinkStarted()` reports a drag and `ImNodes::IsPinHovered()` a target, pre-call
`AppGraphCanLink` to telegraph a will/won't-connect hint before the drop.

##### B2 — Cycle node name in the strip

The HEALTH segment (Theme A) already renders `topo_err` verbatim from the once-computed, hoisted topo
result. That delivers the #2 diagnostic with no extra surface. **Decision:** ship the named warning;
do **not** add a "Select offending node" button derived from `topo_order` — on a cycle `AppGraphTopoOrder`
clears `out_control_ids`, so a "first control not in topo order" scan returns the wrong node and
contradicts the banner. Click-to-select is deferred until `AppGraphTopoOrder` exposes the cycle-member id
it already knows internally (the `done[i]==false` set, nodes.cpp:1167-1198), at which point it routes
through the Theme C selection channel.

##### B3 — Codegen staleness (fresh | STALE)

`code` is filled only by Generate; nothing marks it stale (#3). Add one pure helper (the one justified new
public function — a const-pointer / scalar-return shape mirroring `AppNodeStructTypeId`):

```cpp
IMGUI_API ImGuiID AppGraphSignature(const ImGuiAppGraph* g);   // fold of codegen-DETERMINING authored state
```

It folds **only the authored (`!IsLive`) population — exactly what becomes code** — via seed-chained
`ImHashStr`/`ImHashData`: per `!IsLive` node `Kind`, `Draft.Name`, each `PersistField`/`TempField`
`Name`/`Type`/`ArraySize`, `DataTypeName`, `TypeName`, `IsBuiltin`, `LayerType`; per authored link (both
endpoints non-live) `StartAttr`/`EndAttr`/`Kind`; per binding on an authored link `DstField`/`SrcField`.
**Hash char[] as NUL-terminated `ImHashStr`, never `ImHashData` over the fixed buffer** (ctors zero only
byte 0 → trailing garbage). Explicitly exclude `GridPos`/`HasGridPos`/`_NeedsPlace`/`BodyAttrId`/raw ids so
node drags and live churn never false-trigger.

**Coherence prerequisite:** make the codegen domain equal the signature domain. Add `&& !n->IsLive` guards
to the bring-up loops (nodes.cpp:1340-1377) and the topo control collection (nodes.cpp:1139-1141) — live
mirror nodes must not be re-`Push`ed in generated bring-up regardless — so an authored-only signature can't
read "fresh" after a Mirror toggle that actually changed emitted code.

Demo gating, computed once near the top of the metrics block (so it drives collapsed surfaces too):

```cpp
static ImGuiID code_sig = 0; static bool code_emitted = false;     // beside `code` (demo.cpp:327)
... on Generate: code_sig = ImGui::AppGraphSignature(&graph); code_emitted = true;
const bool stale = code_emitted && ImGui::AppGraphSignature(&graph) != code_sig;
```

Drives, coherently, every freshness surface: a strip `StatusPill` `"code: fresh"` (ok) / `"code: STALE"`
(warn) / hidden when `!code_emitted`; the panel header (demo.cpp:549) → amber `"Generated C++ — STALE"`;
**Copy and Write .h** warning-tinted while stale; the Write .h tooltip (demo.cpp:410) →
`"writing STALE output — Generate first"`; `write_msg` cleared on the first stale frame (so the green
"wrote" line can never coexist with staleness). Fixes the Generate tooltip count (#22) in the same pass:
tally `!IsLive` nodes, keep "node(s)".

##### B4 — Dropped-binding & hosted-control warnings (codegen honesty + warnings count)

Two silent codegen drops get a voice. **(a)** In the `types_ok==false` path of `AppEmitControlWithDeps`
(nodes.cpp:1289-1293; in-scope vars are `dst_id`/`src_id`) add an explicit `else`:
`out->appendf("  // WARNING: dropped binding %s = %s (type mismatch)\n", dst_id, src_id);`. **(b)** Rewrite
the trailing hosted-control TODO (nodes.cpp:1373-1374) onto its own line as
`// WARNING: control '%s' cannot be hosted in window '%s' yet (PushWindowControl unimplemented)`.

**Demo surfacing:** after Generate, scan `code` for **line-leading** `// WARNING` plus `// codegen aborted`
(never the per-control boilerplate `// TODO: render widgets` at nodes.cpp:1301), count matches, and render
a clickable amber `(!) N` count after the Generate button (demo.cpp:395); click opens a popup listing the
matched lines via `TextUnformatted`. Hidden when `N==0`, persistent until next Generate. Scope: #20/#21
only (load-time drops #23 never touch `code`).

```
toolbar:  [Generate]  (!) 2  [Write .h]  [Code >]
click (!) -> +- codegen warnings (2) ------------------------------+
             | - dropped binding Phase = Seed (type mismatch)      |
             | - control 'Mixer' cannot be hosted in window 'Main' |
             +----------------------------------------------------+
```

---

#### Theme C — Cross-View Selection Sync

**Merges:** unify-selection-channel, selection-sync, selection-breadcrumb. **Targets:** #6, #13, #14, #16,
#26.

**Change.** Promote the demo-local `tree_sel` (demo.cpp:482) to one window-level `sel` passed by pointer
to **both** views. `ShowAppGraphTree` already takes `int*`; add the same to the single-caller editor:

```cpp
IMGUI_API void ShowAppGraphEditor(ImGuiApp* app, ImGuiAppGraph* g, int* selected_node_id, bool show_live);
// demo: static int sel = -1;  ShowAppGraphTree(..., &sel);  ShowAppGraphEditor(&app, &graph, &sel, show_live);
```

The editor owns reconciliation after `EndNodeEditor` (where `NumSelectedNodes`'s `CurrentScope==None`
assert is satisfied), with two function-local latches, in this order:

1. **Dangle guard (first):** `if (*sel >= 0 && !AppGraphFindNode(g,*sel)) *sel = -1;` — kills the ghost
   uniformly after delete / Mirror-strip / Load (#13).
2. **Canvas→tree read-back:** if `NumSelectedNodes()==1` and the id differs, write it to `*sel`
   (closes the one-way gap, #6). If `>1`, leave `*sel` unchanged (single-select model).
3. **Tree→canvas apply + reveal:** if `*sel` changed externally (vs the `applied_sel` latch),
   `ClearNodeSelection(); SelectNode(*sel); EditorContextMoveToNode(*sel)` — outline **and** pan an
   off-screen node into view (#14). Pan **only** when the change originated from the tree, never on a
   canvas-originated change (don't yank the viewport on a click).

The tree drops its `ImNodes::ClearNodeSelection/SelectNode` (nodes.cpp:1791-1792) entirely — it just sets
`*sel` — which retires the fragile submit-order invariant (#26). For #16, **de-duplicate** rather than
re-deriving `LiveKey`: drop the inert "Live app" `BulletText` section (nodes.cpp:1748-1774), or make its
rows jump to the existing selectable "Graph > Nodes" rows; do not wire a second copy.

**Optional breadcrumb.** A strip `SELECTION` segment via a nodes.cpp helper (keeps the file-static
`AppGraphParentOf` walk encapsulated, char[] out): `AppGraphSelectionBreadcrumb(const ImGuiAppGraph*, int
id, char* buf, int buf_size)` → `"sel: MainWindow > Mixer [design]"` / `"sel: StatusBar [live]"` /
`"sel: —"`. The `[design|live|promoted]` tag is the first non-canvas surfacing of `IsPromoted`. Left-align
it — do not pile it onto any right edge.

```
click canvas node 'RandomTime'  ->  tree row 'RandomTime' highlights      (was: nothing)
click tree row 'Breathing'      ->  canvas selects + PANS to Breathing     (was: outline only)
delete the selected node        ->  highlight clears                       (was: ghost)
```

> One-frame note: the tree is submitted (demo.cpp:485) before the editor (519), so canvas→tree read-back
> lands one frame later — acceptable for immediate mode; comment it so a future reorder isn't mistaken for
> a bug. The two latches assume a single live editor instance (true for the demo).

---

#### Theme D — Design/Live Legibility

**Merges:** identity-legend, node-status-tint, shared-vocab-legend, origin-vocabulary. **Targets:** #4,
#12, #16, #27.

**Change.** Establish one origin vocabulary and apply it in canvas, tree, and legend from a single set of
named constants so they cannot drift:

```cpp
// nodes.cpp, shared by canvas PushColorStyle, tree PushStyleColor(ImGuiCol_Text), and the legend swatches
static const ImU32 kAppLiveTint     = IM_COL32(90,120,165,255);   // steel-blue: read-only mirror
static const ImU32 kAppPromotedTint = IM_COL32(80,150,90,255);    // green: design matches a live type
// design = default (no push)
```

**Canvas** (`ShowAppGraphEditor` node loop): for live nodes push `ImNodesCol_TitleBar` +
`ImNodesCol_TitleBarHovered` before `BeginAppNode` and pop after `EndAppNode`, strictly balanced via a
helper that returns the push count. **Do not push `ImNodesCol_TitleBarSelected`** — leave the selection
cue unambiguous (it must stay legible under Theme C). Carry origin in the **body**, not the title string —
`Draft.Name` is the renamable `InputText` buffer (nodes.cpp:106-115,961) and appending corrupts it. Live
nodes get a body `TextDisabled("live — read-only (mirrors running app)")`; promoted design nodes get
`TextDisabled("promoted -> matches live <DataType>")` via `AppNodeDataTypeName(n,...)` (already called at
nodes.cpp:1729). Bodies already submit an item, so the empty-body assert cannot fire. Pair every tint with
text (colorblind-safe). **Tree:** `PushStyleColor(ImGuiCol_Text, ...)` on the **clickable** "Graph > Nodes"
rows only (nodes.cpp:1780-1794) — never the inert section, which would make a broken click look *more*
interactive.

**Legend** micro-row under the toolbar, ASCII swatches reading the same constants:
`[ ] design   [#] live (read-only)   [#] promoted`. Extend the `HelpMarker` (demo.cpp:427) — the only
in-UI explanation — to finally describe design→live→promotion and what "Show live mirror" does (#27).

```
legend:  [ ] design   [#] live (read-only)   [#] promoted

  . Breathing .      . RandomTime .(blue)     . Default .(green)
  | timer_secs |     | live —      |          | promoted -> |
  |____________|     | read-only   |          |  DefaultData|
```

`IsPromoted` only computes while the mirror is on (no live nodes ⇒ no promotion) — promoted tint/badge
appears only then. That is self-consistent; note it so it isn't read as a bug.

---

#### Theme E — Non-Destructive Mirror

**Stands alone.** **Targets:** #7, #27.

**Change.** Reframe "Mirror live" OFF from *delete the live nodes* to *hide them*. Delete the destructive
strip loop (demo.cpp:510-518); always run `BuildAppLiveGraph` (its `LiveKey` mechanism, nodes.h:384,
preserves dragged positions across toggles). Add `bool show_live` to `ShowAppGraphEditor` (Theme C already
opens the signature). The model is never mutated, so re-showing restores everything in place.

> **The assert that the naïve version causes (must fix):** when `!show_live` you cannot merely skip a live
> node's body. `EndNodeEditor` evicts un-submitted nodes from the imnodes pool, and the unconditional
> grid-pos read-back (nodes.cpp:1036-1040) then calls `GetNodeGridSpacePos` → `IM_ASSERT(idx!=-1)`
> (imnodes.cpp:2794-2795) on the evicted node. **`continue` over the ENTIRE per-node submission** for
> `!show_live && n->IsLive` (including the `SetNodeGridSpacePos` placement at nodes.cpp:951-955) **and**
> add the same guard to the read-back loop. Skipping read-back also correctly retains each hidden node's
> last-shown `GridPos`.

**Link filter:** a link to an un-submitted attribute must not be drawn. Inline the link loop in
`ShowAppGraphEditor` where `g` is in scope (the `DrawAppNodeLinks(&g->Links)` signature can't resolve
owners): for each link, `AppGraphFindPort` both endpoints and skip if either owner `IsLive` when
`!show_live`. Rename the checkbox **"Show live mirror"** with an honest tooltip:
`"Hide/show read-only nodes mirrored from the running app. Hiding never deletes your design."`
Composition counts (Theme A) already read `app->*.Size` and the codegen signature (Theme B) already
excludes `IsLive`, so an always-on `BuildAppLiveGraph` does not perturb either.

### 4. Phased rollout

Low-risk and additive first; behavior-changing and imnodes-touching last. Each phase is independently
shippable.

- **Phase 0 — frame ordering + status strip (S, low).** Pure `demo.cpp`. Reconcile-before-report hoist
  (§3.0b); `StatusPill` primitive; the status strip (Theme A) with one FPS, verbatim `topo_err`, labeled
  composition from `app->*.Size`, NODES split, lifecycle, and the perf tooltip; delete the duplicate
  main-header lines and the overlap guard; fix the Generate tooltip count. No model/API change. *This is
  the spine every later phase reuses.*
- **Phase 1 — discarded diagnostics (S→M, low).** B1 link-reject (`LastLinkErr`/`Seq` field + the
  `CaptureAppGraphLinks` write + the canvas toast); B4 codegen-honesty emits + the warnings count. Additive
  field + demo render only.
- **Phase 2 — codegen freshness (M, low).** B3: `AppGraphSignature`, the `&& !n->IsLive` codegen guards
  (so domain==domain), and the fresh|STALE wiring across header/Copy/Write/write_msg/strip. One pure
  helper; behavior-preserving once guards land.
- **Phase 3 — selection sync (M, med).** Theme C: `int*` param on `ShowAppGraphEditor`, the post-
  `EndNodeEditor` reconcile (dangle→read-back→apply/reveal), tree de-`ImNodes` + live-row de-dup. Ship the
  read-back + dangle core first; breadcrumb and cycle-Select are follow-ups behind the topo out-param.
- **Phase 4 — design/live legibility (M, med).** Theme D: origin tint (canvas, .cpp), body badges, tree
  text tint, legend, HelpMarker copy. First color styling in the module; push/pop balanced via the helper.
- **Phase 5 — non-destructive mirror (M, med).** Theme E: `bool show_live`, delete the strip loop,
  the read-back-loop guard, the inline live-link filter, the renamed checkbox. Last because it changes
  observable Mirror behavior and shares the per-node submission path with Phase 4.

### 5. Explicitly rejected ideas

- **Clearing the link toast on `CaptureAppGraphLinks`'s `true` return.** Rejected: it returns `true` on
  link *destroy* too (nodes.cpp:848), so an unrelated delete would wipe the toast. Drive strictly off the
  rejection branch (nodes.cpp:826 `else`); clear only on a real create.
- **A standalone "App" metrics table with its own FPS/ms row** (app-runtime-metrics as pitched). Rejected:
  it renders FPS a *third* time in the same window and adds a "Windows" row adjacent to the tree's
  "Windows (n)". Folded instead into the single PERF segment's tooltip with the disambiguated "ImGui
  windows" label.
- **Cycle "Select" button deriving the node from `topo_order`.** Rejected: `topo_order` is emptied on a
  cycle, so the scan jumps to the wrong node and contradicts the banner it sits beside. Deferred to a
  proper `AppGraphTopoOrder` cycle-member out-param routed through the selection channel.
- **Making the inert "Live app" tree rows clickable by re-deriving `LiveKey`.** Rejected: duplicates the
  join the lower selectable rows already own (#16 made worse). De-duplicate the section instead.
- **Origin suffix appended to the node title string** (`"· live"` / `"· promoted"`). Rejected: the title
  is the renamable `InputText` buffer (nodes.cpp:961) — appending corrupts the rename target. Use a body
  `TextDisabled` line + title-bar tint.
- **A whole-graph codegen signature** (including `IsLive` nodes and live-derived edges). Rejected:
  `BuildAppLiveGraph` re-derives live data edges with fresh ids every frame (nodes.cpp:1697-1720), so it
  churns and pins the panel to permanent STALE. Hash the authored (`!IsLive`) population only.
- **Hashing char[] via `ImHashData` over the fixed buffer.** Rejected: ctors zero only byte 0, so trailing
  indeterminate bytes make the signature unstable. Use NUL-terminated `ImHashStr`.
- **Overriding `ImNodesCol_TitleBarSelected` with the identity tint.** Rejected: it fights imnodes' own
  selection cue, which Theme C depends on. Tint `TitleBar` + `TitleBarHovered` only.
- **Non-ASCII status glyphs** (`↑ → ⇒ ■ ✓ ◐`). Rejected: absent from the default font atlas → rendered as
  boxes. ASCII labels and `ImDrawList` rects only.
- **A new public `AppGraphCountKinds` / per-mutator `ImGuiAppGraph::Revision` dirty bit.** Rejected: the
  count split is a one-line inline loop, and a revision counter would instrument many `nodes.cpp` mutation
  sites. Keep the per-frame `AppGraphSignature` and inline counting; minimal new surface.
- **"Mirror live" OFF as a destructive strip** (status quo, demo.cpp:510-518). Rejected: it is the bug —
  it sweeps incident authored links and bindings that never return. Replaced by a pure view filter that
  leaves the model intact.

---

*Relevant files:* `imguiapp_demo.cpp`, `imguiapp_nodes.cpp`, `imguiapp_nodes.h`,
`imguiapp.h`, `imnodes.cpp`/`imnodes.h`, `docs/node-editor-upgrade-design.md`.


---

<a id="av-design"></a>

## ImGuiAppAV — frame pacing, frame encoding, test/bench harness

Design for three use cases:
1. App frame pacing, optionally per-viewport (viewports may span monitors with different refresh rates).
2. App frame encode-to-video, with optional arbitrary per-frame data; `__rdtsc` (or platform equivalent) encoded per frame by default.
3. ImGui Test Engine + headless rendering + frame encoding + WAL/event-source logging in one harness, for ergonomic debugging, testing, and benchmarking.

Decisions fixed with the owner (2026-07-02): encoder is a provider seam with built-ins
(QOI sequence, linked libav, Media Foundation); ALL metadata lives IN the video --
the meta record stream is chunked across each frame's pixel strip, no side files;
pacing is an advisory pacer called by the backend run loop; the use-case-3 entry point
is named **ImGuiAppTestHarness**; an ffmpeg-SDK (libav) backend ships by default.

---

### 1. Frame identity: ImGuiAppFrameID

One id per frame, taken at the top of `OnDrawFrame`, is the correlation key across
video, embedded meta stream, WAL, and test-engine logs.

```cpp
struct ImGuiAppFrameID
{
  ImU64  FrameIndex;   // monotonic from run start (not ImGui's frame count: survives context recreation)
  ImU64  Tsc;          // __rdtsc / cntvct_el0 at frame begin
  double TimeSec;      // QPC seconds since run start
};
// lives on the app:
//   ImGuiAppFrameID FrameID;   (member of ImGuiApp, updated by OnDrawFrame)
```

WAL correlation: `ImGuiAppWAL` gains an optional frame-id source; when set, every record
is prefixed `[tick:%llu tsc:%llu]`. Null = today's behavior, so this is non-breaking.

```cpp
struct ImGuiAppWAL
{
  // ... existing fields ...
  const ImGuiAppFrameID* FrameID;   // optional; prefixes records with frame identity
};
```

### 2. Pacing: ImGuiAppPacer (advisory, backend loop stays owner)

```cpp
typedef int ImGuiAppPacerMode;
enum ImGuiAppPacerMode_
{
  ImGuiAppPacerMode_Off = 0,     // free-run; vsync/present mode governs
  ImGuiAppPacerMode_Target,      // pace wall clock to TargetHz (sleep + spin hybrid)
  ImGuiAppPacerMode_Fixed,       // Target pacing AND io.DeltaTime forced to exactly 1/TargetHz
};

struct ImGuiAppPacer
{
  ImGuiAppPacerMode Mode;
  float  TargetHz;         // <= 0 with Mode_Target = pace to primary monitor refresh
  float  SleepSlackMs;     // spin the last N ms (OS sleep granularity guard); default 2.0
  // read-only telemetry
  double LastFrameMs;
  double LastWaitMs;
  ImU64  MissedDeadlines;  // frames that arrived after their deadline
};

// Called once per loop iteration by every backend RunLoop, before OnDrawFrame.
// Off-mode returns immediately, so the call is unconditional in the loops.
IMGUI_API void AppPacerWait(ImGuiApp* app);
```

- `Fixed` is the determinism mode: constant dt feeds the replay theorem (OnUpdate as a
  pure function of state + input + dt), so an encoded run and its WAL are reproducible.
- Pacer dt and video timing are DECOUPLED (section 3, "Timing"): the pacer decides what
  time the app simulates; the encoder decides what time the video claims. A video is
  honest about realtime only when its PTS come from the real clock (`FrameID.TimeSec`),
  never from counting pacer ticks.
- Windows implementation raises timer resolution (`timeBeginPeriod(1)`) while a
  non-Off pacer exists, sleeps until `deadline - SleepSlackMs`, spins the rest on QPC.

#### Per-viewport pacing (phase 2)

Multi-viewport apps present one swapchain per platform window; monitors differ in
refresh. The loop runs at the fastest cadence; slower viewports skip presents.

```cpp
// Consulted by the backend's per-viewport present hook (Renderer_SwapBuffers /
// Platform_RenderWindow). True = present this frame; false = skip (contents unchanged
// on that monitor until its next deadline). Main viewport never skips.
IMGUI_API bool AppPacerViewportShouldPresent(ImGuiApp* app, ImGuiViewport* viewport);
```

Refresh per viewport comes from `ImGuiPlatformMonitor` (already maintained by the
platform backends). Pacer keeps per-viewport `NextPresentDeadline` keyed by viewport ID.

### 3. Encoding: provider seam + recorder

#### Frame payload

```cpp
struct ImGuiAppAVFrame
{
  int    Width;
  int    Height;
  int    PitchBytes;               // row stride; providers must honor it
  const void* Pixels;              // RGBA8; valid only during WriteFrame
  ImGuiAppFrameID FrameID;
  const void* UserData;            // optional per-frame blob (meta stream record, not visible pixels)
  int    UserDataSize;
};
```

#### Provider vtable

```cpp
// What time the video claims. A video is honest about realtime only under Realtime.
typedef int ImGuiAppAVTimingMode;
enum ImGuiAppAVTimingMode_
{
  ImGuiAppAVTimingMode_Auto = 0,   // follow the pacer: Fixed pacer -> Constant, else Realtime
  ImGuiAppAVTimingMode_Constant,   // CFR: frame N plays at N/Fps (synthetic timeline; matches Fixed dt)
  ImGuiAppAVTimingMode_Realtime,   // VFR: PTS = FrameID.TimeSec (wall clock; a 50ms hitch plays as 50ms)
};

struct ImGuiAppAVEncodeConfig
{
  const char* OutputPath;      // container path, or directory for sequence providers
  float       Fps;             // Constant mode: the frame rate. Realtime mode: nominal rate hint only
  ImGuiAppAVTimingMode Timing; // default Auto
  int         Width;           // 0 = first frame's size (fixed thereafter; resize aborts with error)
  int         Height;
  int         BitrateKbps;     // hint; lossless providers ignore
};

struct ImGuiAppAVEncoder
{
  const char* Name;
  bool        SupportsRealtimePts;   // provider can carry per-frame wall-clock PTS (true VFR)
  bool (*Open)(ImGuiAppAVEncoder* self, const ImGuiAppAVEncodeConfig* config);
  bool (*WriteFrame)(ImGuiAppAVEncoder* self, const ImGuiAppAVFrame* frame);   // PTS from frame->FrameID.TimeSec in Realtime mode
  void (*Close)(ImGuiAppAVEncoder* self);
  void (*Destroy)(ImGuiAppAVEncoder* self);   // frees the encoder itself (providers allocate their own)
  void* UserData;              // provider state
};
```

#### Timing honesty per provider

Realtime contract, all providers: EVERY frame is present exactly once; PTS are honest
(exactness is method-specific, below). Stepping frame-by-frame in a player walks
consecutive capture frames with no gaps and no duplicates.

| Provider | Realtime PTS | How |
|---|---|---|
| libav (linked) | exact | true VFR: AVFrame->pts carries FrameID.TimeSec directly, microsecond timebase end-to-end through the mp4 muxer -- measured 0us error. The final sample gets a nominal duration at mux time (a zero-duration tail sample falls outside the edit list and decodes as DISCARD) |
| QOI sequence | exact | index.tsv carries `FrameID.TimeSec` per frame; inherently timestamped |
| Media Foundation | resampled CFR | true VFR through IMFSinkWriter is NOT achievable (measured): the H.264 path requires a declared MF_MT_FRAME_RATE (BeginWriting fails without one) and resamples per-sample timestamps to CFR at that rate even with the input rate omitted (90 VFR samples -> 208 CFR frames). Duration honest, frames duplicated to fill -- violates one-frame-once; prefer the libav backend for Realtime |

The meta stream always records real TSC/QPC times regardless of mode -- ground truth
survives even a Constant-mode encode.

#### Built-in encoder backends (all ship by default; each declared in its own header)

```cpp
// backends/imguiapp_impl_qoi.h -- zero-dependency lossless sequence:
// <dir>/NNNNNN.qoi + index.tsv. Deterministic, byte-stable across machines -- the
// CI/golden-image provider.
IMGUI_API ImGuiAppAVEncoder* ImGuiApp_ImplQoi_CreateEncoder();

// backends/imguiapp_impl_libav.h -- linked ffmpeg SDK (DEFAULT video provider when the
// SDK is present; scripts/get-ffmpeg.ps1 stages it, CMake gates the TU on it). mp4
// H.264 via libx264, exact per-frame PTS, and the decode side for reading embedded
// input logs back out of a recording. GPL SDK variant: distributing linked binaries
// is GPL.
IMGUI_API ImGuiAppAVEncoder* ImGuiApp_ImplLibav_CreateEncoder();
IMGUI_API bool ImGuiApp_ImplLibav_ReadEmbeddedInputLog(const char* video_path, int embed_rows, ImGuiAppInputLog* out_log, int* out_corrupt_frames);

// backends/imguiapp_impl_mediafoundation.h -- Windows Media Foundation mp4
// (H.264/HEVC), no external exe needed. Explicit choice, never a silent
// default (lossy + driver-variant output is wrong for test artifacts).
IMGUI_API ImGuiAppAVEncoder* ImGuiApp_ImplMediaFoundation_CreateEncoder();

// core seam (imguiapp_av.h): frees any provider's encoder via its vtable Destroy.
IMGUI_API void ImGui::AppAVDestroyEncoder(ImGuiAppAVEncoder* encoder);
```

#### The meta stream (embedded in the video)

There is NO side file: the video is the only metadata store. The recorder maintains one
meta record stream per take -- fixed header (magic `IMAVMETA`, version, fps, start TSC
+ QPC Hz for TSC->seconds conversion), then a TYPED-RECORD stream `{u32 type, u32 size,
payload}` -- and chunks it across the frames' pixel strips (next section). Records
self-describe, so tracks extend without format breaks:

```
type Frame:     u64 frame_index | u64 tsc | f64 time_sec | u32 user_size | user bytes
type InputHdr:  ImGuiAppInputLog layout (composition id, slot table) -- once per take
type InputFrm:  u64 frame_index | one ImGuiAppInputLog frame (TempData + dt) | state hash
type StateSnap: composition id | snapshottable-state bytes (ImGuiAppStateHistory layout)
type AudioPcm:  RESERVED in v1 (defined, no producer yet): u64 frame_index | sample
                format header | PCM chunk
```

TSC is the *default* per-frame payload (always present); `user_size` covers the optional
arbitrary blob. `AppAVMetaDump(meta, size)` prints a reconstructed stream as TSV;
`imguix-avtool meta|verify <take> <rows>` extracts and checks one from a recording.

The user blob is OPAQUE BYTES by contract -- the stream format knows nothing of app
types. One typed helper rides on top, built on the EXISTING snapshot machinery
(`AppStateSnapshot` / `ImGuiAppStateHistory` byte layout):

```cpp
// Serialize the app's snapshottable state (same byte layout as ImGuiAppStateHistory
// frames) as this frame's blob. Restoring those bytes IS time travel, so a recording
// made with this helper scrubs: video frame N <-> app state N.
IMGUI_API void AppRecordSnapshotState(ImGuiAppRecorder* rec, ImGuiApp* app);
```

#### Input: source events by default, derived checkpoints opt-in

Input records in three layers, cheapest-first:

1. **Raw io (`IoFrame`) -- default, every frame, O(1).** The source events: mouse
   position (main-viewport-relative), buttons, wheel (from the frame's
   `InputEventsTrail`; `EndFrame` zeroes the io fields before the pump runs), key
   transitions against the recorder's shadow, text as UTF-16 units -- plus the frame's
   `AppStateHash` fingerprint. Tens of bytes per frame regardless of app size.
   Replay-side (feeding `AddMousePosEvent` et al. and re-rendering) is a future phase.
2. **State hash (in every IoFrame) -- default.** `AppStateHash(app)` (imguiapp.h): the
   Persist + LastTemp fingerprint AppInputRecord stores. Enables divergence detection
   against any future raw replay without carrying TempData.
3. **TempData log (`InputHdr`/`InputFrame`) -- OPT-IN via AppRecordAttachInputLog.**
   The derived checkpoint: every control's TempData + dt per frame. Enables render-free
   replay (`AppInputReplay`) and per-control divergence attribution. Cost is O(sum of
   TempData) per frame -- attach deliberately.

```cpp
// OPT-IN derived checkpoint (call AppInputRecord once per frame as usual).
IMGUI_API void AppRecordAttachInputLog(ImGuiAppRecorder* rec, const ImGuiAppInputLog* log);

// Parse a reconstructed stream (extract per-backend: ImGuiApp_ImplLibav_ /
// ImGuiApp_ImplQoi_ExtractEmbeddedMeta). Reproduction = restore the snapshot, then
// AppInputReplay(app, &log, &div) -- the divergence check pinpoints non-determinism.
IMGUI_API bool AppAVMetaReadInputLog(const void* meta, int meta_size, ImGuiAppInputLog* out_log);
IMGUI_API bool AppAVMetaReadStateSnapshot(const void* meta, int meta_size, ImVector<char>* out_bytes, ImGuiID* out_composition_id);
```

**Replay mismatch taxonomy** (the `Identity` record, emitted once right after the
stream header, declares the take: applayer + imgui version numbers, composition id,
schema hash over the snapshottable slot table, embed geometry):

1. Identity differs from the replaying build -> **declared version/schema mismatch**:
   refuse or warn BEFORE replaying a single frame.
2. Identity matches but the per-frame hash chain diverges at frame k ->
   **nondeterminism or corruption at k** (first-divergence semantics, as in
   `AppInputReplay`).

#### Integrity layers

Five hashes, each isolating a different failure:

| Layer | Scope | A failure isolates |
|---|---|---|
| Chunk checksum (CRC32c, per frame's strip) | that frame's chunk bytes | pixel-level damage in ONE frame; the stream truncates there on read |
| State hash (`AppStateHash`, in every IoFrame) | that frame's Persist+LastTemp state | which FRAME a replay diverged at |
| Hash chain (IoFrame `chain`: `chain_k = ImHashData(&state_hash_k, 4, chain_{k-1})`, seeded by the Identity schema hash) | the hash SEQUENCE | reorder / splice / single-frame substitution -- an attacker or bit flip cannot swap frames without breaking every later link; the seed binds the chain to the declared identity. Ring dumps recompute the chain over surviving entries (eviction legitimately changes the sequence) |
| Schema hash (Identity) | the snapshottable slot layout | replaying against a build whose registered types moved -- refused before frame 1 |
| Stream digest (`Digest`, final record: byte count + `ImHashData` over every preceding stream byte) | the whole take | presence = complete take; absence = truncation (crash); mismatch = corruption anywhere the per-frame layers missed |

`imguix-avtool verify` recomputes all of them from the video alone.

#### Recorder (glue between app, backend capture, and provider)

```cpp
struct ImGuiAppRecorder;   // opaque

// The frame is four ordered phases -- ImGuiApp::Frame() = OnDrawFrame (frame id,
// NewFrame, app layers) -> OnRenderFrame (draw data -> GPU, platform windows) ->
// OnEncodeFrame (recorder pump reads the frame just rendered) -> OnPresentFrame.
// AppRecordBegin registers itself on app->Recorder, so OnEncodeFrame pumps it
// automatically; overriding a phase extends the pipeline at that point.

IMGUI_API ImGuiAppRecorder* AppRecordBegin(ImGuiApp* app, ImGuiAppAVEncoder* encoder /* required; providers in backends/ */, const ImGuiAppAVEncodeConfig* config);
IMGUI_API void              AppRecordSetFrameData(ImGuiAppRecorder* rec, const void* data, int size);   // this frame's blob; copied immediately
IMGUI_API bool              AppRecordIsActive(const ImGuiAppRecorder* rec);
IMGUI_API void              AppRecordEnd(ImGuiAppRecorder* rec);   // flush queue, Close provider
```

Threading: `WriteFrame` runs on a single encoder thread behind a bounded queue
(default depth 3). Queue-full policy defaults to `Block` in every mode -- ENCODE EVERY
FRAME is the contract; `DropNewest` (never stall the app, drops counted + WAL-logged)
is an explicit opt-in via `AppRecordSetQueuePolicy`.

Encode-every-frame guarantees: the take's frame ids are contiguous. The first pump
captures the current frame synchronously (no pipeline-priming loss); `AppRecordEnd`
drains the pipelined tail so the final frame lands; any frame that produced no pixels
mid-take (minimized window, capture hiccup) is synthesized as a pause-glyph placeholder
frame carrying its real frame id. The win32 run loop keeps frames running while
minimized whenever a recorder is active, so the pause span is encoded rather than
skipped.

#### The pixel strip (stream chunking)

Embedding is UNCONDITIONAL while recording. The stream (header first, then records in
emission order) is a continuous byte sequence; each frame's strip carries its NEXT
chunk, up to capacity. Records need no per-frame alignment -- a large state snapshot
legitimately spans several frames' strips -- so over-capacity is impossible by
construction. A frame with no pending bytes stamps an empty chunk. The only loss mode
is a corrupt frame (checksum), which truncates the reconstructable stream at that
point -- the same honesty as a torn WAL tail. Crash-honesty lives in the CONTAINER:
the libav encoder muxes fragmented mp4 (movflags +frag_keyframe+empty_moov, gop 30), so
a killed process still yields every completed fragment's frames and their chunks; the
QOI sequence is per-frame files, crash-safe by construction. Providers must preserve
the strip through their encode (lossy is fine within the block coding's margin).

Format (frozen; extractors: `ImGuiApp_ImplLibav_ExtractEmbeddedMeta`,
`ImGuiApp_ImplQoi_ExtractEmbeddedMeta`):

- Strip: the bottom `EmbedRows` rows (clamped at `AppRecordBegin` to a multiple of 4,
  minimum 4; the adjusted value is the take's contract). Blocks are 4x4 pixels, luma
  black 16 / white 235 (R = G = B, A = 255), read threshold 128 -- survives lossy encode.
- Block addressing: block (bx, by), by = 0 the TOPMOST reserved row group; bit index
  = by * blocks_per_row + bx, blocks_per_row = floor(W / 4). Pixels right of the block
  grid (W % 4) are black filler.
- Bitstream is a byte stream read MSB-first per byte (bit k of byte i is stream bit
  i * 8 + k, k = 0 the most significant bit).
- Per-frame layout: `u32 magic 'I','M','I','L'` | `u32 chunk_size (LE)` | chunk (the
  stream's next chunk_size bytes) | `u32 checksum (LE) = ImHashData(chunk, chunk_size,
  0)`. Note for external tools: ImHashData is CRC32c (reflected polynomial 0x82F63B78,
  imgui >= 1.91.6), not zlib CRC32. chunk_size 0 (idle frame) is valid.
- Capacity: floor(W/4) * (EmbedRows/4) / 8 - 12 bytes of chunk per frame.
- Unused trailing blocks are black (bit 0).

Reassembly = concatenate chunks in frame order, then parse the record stream. Giant
4x4 luma blocks survive crf-level quantization; the per-frame checksum gates residual
corruption. State snapshots ride the same stream, spanning frames as needed.

#### Flight recorder (ring mode)

The WAL's crash-forensics philosophy applied to pixels: an always-on in-memory ring of
the last N seconds (frames QOI-compressed on capture, plus their meta records and
input events), dumped to disk through the provider only when something goes wrong.

```cpp
struct ImGuiAppRingConfig
{
  float  Seconds;        // ring span; default 10
  int    MaxMemoryMB;    // hard cap; oldest frames evicted when either bound binds. default 256
  float  Fps;            // <= 0 (default) = every frame; > 0 = explicit subsample opt-out of encode-every-frame
};

IMGUI_API ImGuiAppRecorder* AppRecordBeginRing(ImGuiApp* app, ImGuiAppAVEncoder* encoder, const ImGuiAppAVEncodeConfig* config, const ImGuiAppRingConfig* ring);
// Encode the ring's contents to disk NOW (assert hook, test failure, hotkey, user code).
// The ring keeps recording; repeated dumps get "-2", "-3" suffixes.
IMGUI_API bool              AppRecordDumpRing(ImGuiAppRecorder* rec, const char* reason);   // reason lands in the WAL + a stream marker record
```

The assert path wires itself: when a ring recorder exists, `ImGuiAppAssertFail` (the
IM_ASSERT sink that already writes the WAL) also calls `AppRecordDumpRing(rec, expr)` --
a failed assert leaves a video of the last N seconds next to the WAL that names it.

### 4. Capture: backend readback hook

New optional entry on the platform backend vtable (null = backend cannot capture;
`AppRecordBegin` fails with a clear error):

```cpp
struct ImGuiAppPlatformBackend
{
  bool (*InitPlatform)(ImGuiApp* app, ImGuiAppConfig& config);
  void (*ShutdownPlatform)(ImGuiApp* app);
  int  (*RunLoop)(ImGuiApp* app);
  // Readback of the frame just rendered (called after render, before present).
  // Double-buffered staging: returns frame N-1's pixels while frame N copies -- the
  // frame id travels inside ImGuiAppAVFrame, so latency never misaligns identity.
  bool (*CaptureFrame)(ImGuiApp* app, ImGuiAppAVFrame* out_frame);
};
```

Vulkan first: copy swapchain image to a host-visible staging buffer at
`ImGuiAppFrameFlags` time, map the previous one. OpenGL3: PBO ring. Headless capture
needs an offscreen target -- see below.

Multi-viewport: the video captures the MAIN viewport only. Secondary platform windows
are not readback targets (their swapchains belong to the viewport hooks); their events
still land in the WAL under the same FrameID, so they stay debuggable, just not
pictured. The harness never spawns secondary viewports, so use case 3 is unaffected.

#### Headless rendering

`ImGuiAppConfig` gains one field:

```cpp
typedef int ImGuiAppHeadlessMode;
enum ImGuiAppHeadlessMode_
{
  ImGuiAppHeadlessMode_None = 0,   // normal windowed app
  ImGuiAppHeadlessMode_Null,       // no GPU, no pixels (test engine only; CaptureFrame = null)
  ImGuiAppHeadlessMode_Offscreen,  // GPU renders to an offscreen target; no OS window, CaptureFrame works
};
// ImGuiAppConfig: ImGuiAppHeadlessMode Headless;
```

`Offscreen` is what makes use case 3 record video without a display (CI boxes, ssh).
Vulkan backend implements it as a render-target image instead of a swapchain; the run
loop skips present (existing `ImGuiAppFrameFlags_NoPresent`).

### 5. ImGuiAppTestHarness (use case 3)

One entry point wires app + Test Engine + headless + recorder + WAL, all sharing the
frame id. Replaces the hand-rolled loop in tests/imguix_tests_main.cpp (which today
uses the test engine's own null app, not ImGuiApp -- migrating it is part of this work).

```cpp
struct ImGuiAppTestHarnessConfig
{
  const char* Name;                                   // artifact base name
  const char* ArtifactDir;                            // receives <Name>.mp4/.wal/.frametimes.csv
  ImGuiAppHeadlessMode Headless;                      // default Offscreen
  bool        RecordVideo;                            // requires Offscreen (or windowed)
  bool        KeepArtifactsOnPass;                    // default false: artifacts survive only failures
  ImGuiAppPacerMode PacerMode;                        // default Fixed (reproducible tests); Target/Off for honest-clock benchmark captures
  float       Fps;                                    // Fixed pacer rate / Constant-timing rate; default 60
  ImGuiAppAVTimingMode Timing;                        // default Auto: Fixed pacer -> Constant video, else Realtime (honest) video
  ImGuiAppAVEncoder* Encoder;                         // null = harness default: libav when the SDK is linked, else QOI sequence
  ImGuiAppWALLevel   WALLevel;                        // default Frame
  const char* TestFilter;                             // test-engine filter; null = all
  void (*RegisterTests)(ImGuiTestEngine* engine);     // required
};

// Runs to queue-empty (or abort), returns a ctest-ready exit code.
IMGUI_API int AppTestHarnessRun(ImGuiApp* app, const ImGuiAppTestHarnessConfig* config);
```

Per frame the harness: `AppPacerWait` -> frame id -> `ImGui::NewFrame` -> app frame ->
render -> `CaptureFrame` -> recorder (`WriteFrame` + strip chunk) -> `PreSwap`/present/`PostSwap`.
The WAL's frame-id source is set, so every event-source line carries the same frame index
that names the video frame: *scrub the video to frame N, grep the WAL for `tick:N`*.

Two harness postures, one knob pair:
- **Reproduce** (default): `PacerMode = Fixed`, `Timing = Auto -> Constant`. The app
  simulates a synthetic timeline and the video plays that timeline -- reruns are
  bit-comparable.
- **Witness**: `PacerMode = Target` (or Off), `Timing = Auto -> Realtime`. The video is
  honest realtime -- hitches, stalls, and pacing misses play back at their true
  duration. This is the benchmark-capture posture.

Benchmarking falls out of the same artifacts either way: the meta stream's TSC deltas are
real per-frame costs regardless of timing mode; the harness additionally emits
`<Name>.frametimes.csv` (frame_index, tsc_delta, ms) and prints p50/p95/p99/max.

### 6. File layout

P1 concepts (frame identity, pacing) are app-loop machinery with no AV dependency --
they live in core. Encoder providers follow the idiom Dear ImGui set with its
platform/renderer impl backends: one self-contained TU per provider in /backends with
its own small header, the core seam never includes them, and the app (or harness)
wires a provider exactly like imgui apps wire imgui_impl_win32 + imgui_impl_vulkan.
The harness is its own pair because it alone drags the test engine.

```
imguix/imguiapp/
  imguiapp.h / .cpp                  P1: ImGuiAppFrameID, ImGuiAppPacer + AppPacerWait,
                                           WAL frame-id source, CaptureFrame backend vtable slot
  imguiapp_config.h                           P1: ImGuiAppHeadlessMode field on ImGuiAppConfig
  imguiapp_av.h / imguiapp_av.cpp          AV seam: timing, ImGuiAppAVFrame, encoder vtable,
                                           recorder, ring, meta stream writer/reader
  imguiapp_testharness.h / .cpp            ImGuiAppTestHarness (gated on the test-engine
                                           option; owns the ffmpeg -> QOI default fallback)
  backends/imguiapp_impl_qoi.h / .cpp      encoder backend: QOI sequence (zero-dep, lossless)
  backends/imguiapp_impl_ffmpeg.h / .cpp   encoder backend: ffmpeg pipe (default video provider)
  backends/imguiapp_impl_mediafoundation.h / .cpp
                                           encoder backend: Media Foundation (compiled WIN32
                                           only; the only TU linking mfplat/mfreadwrite)
  backends/imguiapp_impl_*                    platform backends: CaptureFrame impls,
                                           Headless_Offscreen, the unconditional AppPacerWait
                                           call in RunLoop
```

Because core cannot reference providers, there is no CreateDefaultEncoder in the seam:
`AppRecordBegin(encoder = null)` is an error, and the null-Encoder convenience default
(libav when linked -> QOI sequence) is implemented by the harness, which includes the
provider headers it ships with.

Interface-first: the headers above are written and committed BEFORE implementation so
phase work can proceed in parallel against frozen signatures.

### 7. Phasing

- **P1** — FrameID on ImGuiApp; WAL frame-id prefix; ImGuiAppPacer + `AppPacerWait`
  in both win32 run loops (message-pump loop calls it unconditionally).
- **P2** — Encoder seam; QOI-sequence + ffmpeg-pipe providers; recorder + meta stream
  (input-log and state-snapshot record types from day one -- the format is v1);
  `CaptureFrame` for win32-vulkan; `Headless_Offscreen` for vulkan;
  `AppRecordSnapshotState` + `AppRecordAttachInputLog`.
- **P3** — ImGuiAppTestHarness + tests_main migration; flight recorder ring + assert
  hook; stream readback (`AppAVMetaRead*`) for reproduction runs; Media Foundation
  provider; per-viewport pacing (`AppPacerViewportShouldPresent` wired into the
  viewport present hooks); OpenGL3 capture.

### 8. Non-goals / constraints

- No upstream edits: everything sits at the applayer seam (backend vtable is ours).
- Audio: capture is out of scope, but the stream RESERVES its track now: it is a
  typed-record stream ({u32 type, u32 size, payload}); type AudioPcm is defined in v1
  (PCM chunk + FrameID + sample format header) with no producer yet, so a later
  capture lands without a format break.
- Live streaming, GPU-side encode (NVENC et al): possible later behind the same
  provider vtable; not in P1-P3.
- Resize during recording: recorder aborts with a WAL line rather than silently
  rescaling (fixed-size contract keeps every provider simple and diffs honest).


---

<a id="playback-debugger-design"></a>

## Playback Debugger — scrub a recorded run offline (F61)

### 1. Summary & goals

The **playback debugger** opens a run the harness already recorded and scrubs it *offline*: the
timeline is a finished artifact, not a live app. It is the FILE half of P9.5's "composition is
executable data" pair — the previewer (F66+) interprets the graph live; the playback debugger
replays a captured trajectory. Both ride the same shipped rails, so this doc invents **no new
format**: the recorded take is already a self-describing, self-verifying container, and F61's job is
to *name* it, *freeze* its shape, and route the F29 transport at it.

Everything the debugger needs is what `imguix-headless-verify` already emits and the harness already
proves from its own pixels: frame images, the raw-input stream, per-frame state hashes, opt-in
TempData + snapshots, an identity record, and an end-of-stream digest — one embedded meta stream plus
a tick-correlated WAL (`imguiapp_testharness.cpp:302-309` is the run's ground-truth summary line;
`AppAVMetaVerify` at `imguiapp_av.cpp:1398-1491` is the ladder that produces it). This doc **freezes**
the container so F62 (loader + index), F63 (FILE-mode transport), F64 (state-at-tick), and F65
(divergence) have no open questions.

**Thesis (four moves).**
1. **One container, already shipped** — the RUN is the recording (spine: embedded meta stream) plus
   its basename siblings; a single openable path. §2.
2. **One walk builds the index** — the same traversal `AppAVMetaVerify` performs yields the per-tick
   index and the snapshot list. §3.
3. **One transport, two sources** — F29's `ComposerTransport` gains a SOURCE switch (LIVE ring vs
   FILE run) behind a two-method abstraction. §4–5.
4. **Two divergence layers, one marker** — recording-integrity (chain/digest) and replay-fidelity
   (recorded vs replayed state hash) both flag a tick; jump-to-first is a verb. §6.

**House constraints honored throughout:** no upstream edits (backend seam only,
`feedback_no_upstream_edits.md`); pointers not references; `char[]`+`ImVector`, no `std::string`;
tick (`ImGuiAppFrameID.FrameIndex`) is the single correlation key across every stream; the debugger
is read-only over the artifact. Everything below cites the rail it unifies.

---

### 2. The run container (FROZEN)

#### 2.1 What already ships

The harness writes, per take, into one `ArtifactDir` keyed by `Name`
(`imguiapp_testharness.h:15` — "receives `<Name>.mp4/.wal/.frametimes.csv`"):

| Artifact | Producer | Carries |
|---|---|---|
| `<name>.mp4` **or** `<name>/` (QOI dir) | recorder + encoder backend | the frame images **and** the embedded meta stream (below) |
| `<name>.wal` | `ImGuiAppWAL` | tick-prefixed lifecycle log incl. command dispatch |
| `<name>.frametimes.csv` | harness | per-frame cost `frame_index, tsc_delta, ms` (`imguiapp_testharness.cpp:269-272`) |

The **meta stream** is *embedded in the recording's pixels*, not a side file (av-design.md:184-215):
the recorder chunks a continuous typed-record byte stream across the bottom `EmbedRows` rows of each
frame as 4×4 luma blocks (format frozen, av-design.md:314-330; config comment `imguiapp_av.h:50-59`).
This holds for **both** backends — the mp4 pixel strip and each `NNNNNN.qoi` frame's strip — so
extraction is uniform. The QOI dir additionally writes an auxiliary `index.tsv`
(`file\tframe_index\ttime_sec\ttsc`, `imguiapp_impl_qoi.cpp:87,119-123`); it is a convenience index,
**not** the authority — the embedded stream is.

#### 2.2 The container definition

> **A RUN is addressed by ONE path — the recording** (`<name>.mp4` or the `<name>/` QOI directory).
> The loader resolves the rest by **basename in the same parent directory**: `<name>.wal` (optional),
> `<name>.frametimes.csv` (optional). There is no archive, no manifest, no new byte layout — the run
> "container" is exactly the harness's existing co-located outputs, joined by the shared basename and,
> internally, by **tick** = `ImGuiAppFrameID.FrameIndex`.

The recording is the **authoritative spine**: the meta stream in its pixels fully determines frames,
inputs, snapshots, identity, and completeness, and self-verifies without any sibling. The WAL is a
**correlated command/lifecycle log**, joined by the `[tick:N tsc:T]` prefix
(`imguiapp.cpp:1018-1020`). Losing the WAL loses only the command annotations; losing the recording
loses the run.

#### 2.3 Frozen stream map — how each existing stream lands in the container

Extraction is one call per backend, both returning the **same** reconstructed byte buffer
(`ImGuiApp_ImplLibav_ExtractEmbeddedMeta` / `ImGuiApp_ImplQoi_ExtractEmbeddedMeta`, declared
`imguiapp_av.h:179-181`, used `imguiapp_testharness.cpp:293-298`). That buffer is:

```
[ 40-byte header ] [ record ] [ record ] ...            (imguiapp_av.cpp:73-81, written 1015-1028)
  header  = char Magic[8]="IMAVMETA" | u32 Version=1 | f32 Fps |
            u64 StartTsc | u64 QpcHz | u64 StartQpc
  record  = u32 type | u32 size | payload               (TLV, append-only; imguiapp_av.cpp:86-97)
```

Record catalog (types `imguiapp_av.h:86-118`; builders cited). Every payload that names a frame uses
the same `frame_index` = tick:

| type | payload (frozen) | builder | role for the debugger |
|---|---|---|---|
| `Identity` (6) | `u32 applayer_ver \| u32 imgui_ver \| u32 composition_id \| u32 schema_hash \| u32 embed_rows \| u16 block \| u16 rsvd` — emitted ONCE after header | `imguiapp_av.cpp:423-452` | trust gate + chain seed; `composition_id`/`schema_hash` decide whether state reconstruction (F64) is legal |
| `Frame` (1) | `u64 frame_index \| u64 tsc \| f64 time_sec \| u32 user_size \| user…` | `312-321` | tick spine + wall-clock PTS; `frame_index==~0` is a ring-dump reason marker (`1419-1420`) |
| `IoFrame` (5) | `u64 tick \| f32 mx \| f32 my \| u8 buttons \| f32 wheel \| f32 wheel_h \| u32 state_hash \| u32 chain \| u16 key_n \| {u16 key,u8 down}* \| u16 char_n \| {u16 unit}*` (33-byte fixed prefix; `state_hash` at +25, `chain` at +29) | `471+` | every-frame raw input + per-tick recorded `state_hash` + splice-evident `chain` |
| `InputHdr` (2) | `u32 composition_id \| u32 frame_size \| u32 slots \| {u32 id,s32 off,s32 size}*` — once/take, OPT-IN | `325-344` | slot table for render-free replay |
| `InputFrame` (3) | `u64 frame_index \| u32 state_hash \| frame_size bytes (dt + TempData slots)` — OPT-IN | `348-354` | the per-tick input to replay forward (F64) |
| `StateSnapshot` (4) | `u32 composition_id \| u64 frame_index \| u32 slots \| {u32 id,s32 size}* \| state bytes (StorageEntries order)` | `359-392` (via `AppRecordSnapshotState`, `1090-1099`) | restore points; `frame_index` binds snapshot ↔ tick |
| `Digest` (7) | `u64 stream_bytes \| u32 digest = ImHashData(all preceding bytes,0)` — FINAL record | `579-585` | completeness/corruption proof for the whole take |
| `AudioPcm` (8) | reserved; no producer | — | ignore |

**Frozen guarantees a loader may assume** (all recomputed today by `AppAVMetaVerify`,
`imguiapp_av.cpp:1398-1487`):
- ticks are contiguous `FirstTick..LastTick` with `Frames == LastTick-FirstTick+1` (gaps counted);
- `IoFrames == Frames` (the every-frame contract extends to input);
- exactly one `Identity`, first non-header record; the io `chain` seeds from its `schema_hash`;
- a present, matching `Digest` ⇒ complete take; absent ⇒ truncated (crash); mismatch ⇒ corruption.

#### 2.4 Version gate

`header.Version == 1` and `Identity` are the format contract; a loader refusing an unknown version is
the only forward-compat rule F62 needs. Byte layouts above are **frozen at v1** — F62–F65 read them
verbatim; any change bumps `Version` and is out of scope here.

---

### 3. Index shape (F62)

F62 does **one linear walk** of the reconstructed meta buffer — structurally the traversal
`AppAVMetaVerify` already performs (`imguiapp_av.cpp:1411-1481`) — and, instead of only counting,
records each record's **tick and payload offset**. No format change: same records, a richer landing.

```cpp
// F62 build product (heap; opaque to the snapshot contract, like ComposerTransport).
struct ImGuiAppRunTick        // one per tick, FirstTick..LastTick
{
  ImU64 Tick;                 // == Frame/IoFrame frame_index
  ImU64 Tsc;                  // Frame.tsc
  double TimeSec;             // Frame.time_sec (PTS; also QOI index.tsv)
  int   FrameImage;           // frame ordinal for decode (mp4 sample / QOI NNNNNN); -1 = placeholder
  int   IoOffset;             // byte offset of this tick's IoFrame record (-1 if none)
  ImU32 StateHash;            // IoFrame.state_hash (recorded fingerprint)
  ImU32 Chain;                // IoFrame.chain (as recomputed; divergence flag lives here)
  int   InputOffset;          // InputFrame record offset (-1 if not opt-in this tick)
  int   SnapshotOffset;       // StateSnapshot record offset at this tick (-1 if none)
  int   WalFirst, WalCount;   // slice into the parsed WAL lines carrying [tick:N]
};
struct ImGuiAppRunIndex
{
  ImGuiAppAVMetaHeader Header;
  ImGuiAppRunMeta      Identity;      // decoded Identity (composition_id, schema_hash, versions)
  ImVector<ImGuiAppRunTick> Ticks;
  ImVector<int>        SnapshotTicks; // ascending tick indices where SnapshotOffset>=0 (nearest lookup)
  ImGuiAppAVStreamStats Stats;        // = AppAVMetaVerify output: gaps, chain, digest, first-divergence
  // ImGuiAppInputLog reconstructed via AppAVMetaReadInputLog (imguiapp_av.cpp:1493) when opt-in present.
};
```

- **tick → nearest snapshot** is a binary search in `SnapshotTicks` for the greatest `<= N`. The
  shipped `AppAVMetaReadStateSnapshot` returns only the *first* snapshot ("first snapshot wins",
  `imguiapp_av.cpp:1560`); F62 generalizes that single-shot read into the tick-keyed `SnapshotOffset`
  during the same walk (still zero format change).
- **per-tick presence** (has-input / has-command / has-snapshot / has-image / digest) is exactly the
  boolean set F63 paints as timeline markers and F64/F65 query.
- **WAL correlation**: parse `<name>.wal` once, bucket lines by their `[tick:N]` prefix
  (`imguiapp.cpp:1018-1020`); `"execute command %d"` lines (`imguiapp.cpp:518`, Lifecycle level,
  `imguiapp.h:133`) are the command dispatches. WAL absent ⇒ `WalCount==0` everywhere; the run still
  opens (the recording is authoritative).
- **Acceptance (F62):** the index counts reproduce the summary line —
  `Ticks.Size == Frames`, snapshot/io/input tallies equal `Stats`, and `chain`/`digest` match
  `imguiapp_testharness.cpp:302-309`.

The mp4 frame-image decode is per-sample (libav decode side already exists as the extraction
counterpart); the QOI image is `ImQoiDecode` of `NNNNNN.qoi` (`imguiapp_qoi.h:13`). Decode is
**on-demand at the scrub position**, never a full-run preload.

---

### 4. Transport grammar shared with F29 (F63)

#### 4.1 The surface already exists

F29's `ComposerTransport` (`imguiapp_demo.cpp:326-331`) is an `ImGuiAppStateHistory` ring plus
`Frozen` + `Frame` (scrub position). Its toolbar — pause/resume, step-back, frame slider `f %d`,
step-forward, all gated on `show_live` — is `imguiapp_demo.cpp:951-983`. LIVE behavior: unfrozen it
snapshots the Mirror every frame and follows the newest (`578-585`); frozen it clamps `Frame` and
`AppStateRestore`s the scrubbed bytes back into the running app (`586-591`). F63 **reuses this exact
chrome**; it does not fork a second transport.

#### 4.2 The SOURCE switch

The transport gains one enum and one pointer — the surface stays identical, the *source of frames*
differs:

```cpp
enum ImGuiAppTransportSource_ { Source_LiveRing = 0, Source_FileRun };

// Two-method abstraction the toolbar drives; neither variant knows about the other.
struct ImGuiAppTransportView          // what the slider addresses + what the canvas shows
{
  int   Count() const;                // LIVE: History.Count      FILE: Index.Ticks.Size
  // Land on scrub index i (0..Count-1). LIVE: AppStateRestore into the Mirror, canvas re-renders the
  // live app at that state. FILE: decode the frame image at Ticks[i] and blit it; no app is driven.
  void  Show(int i);
};
```

- **LiveRingSource** wraps `ComposerTransport.History` + the Mirror app; `Show(i)` = the existing
  `AppStateRestore(Mirror, &History, i)` (`imguiapp_demo.cpp:589`). The canvas is a *re-render* of a
  restored live app.
- **FileRunSource** wraps `ImGuiAppRunIndex` + the decoded-frame blitter; `Show(i)` decodes
  `Ticks[i].FrameImage` and displays those **pixels**. There is no live app to restore into — the
  frame is authoritative imagery, and Persist/Temp values come from §5, not from re-rendering.

Because both expose `Count()`/`Show(int)`, the pause/step/slider block is source-agnostic. The one
honest asymmetry: LIVE "resume" re-arms recording; FILE "resume" is inert (a finished run has no
newest frame) — the FILE source reports `Frozen`-always, so the toolbar hides resume and shows only
scrub verbs. Step maps to `Frame±1` over integer tick indices, so **step always lands on an exact
tick** (no interpolation) — F63's acceptance ("shown frame's tick == slider tick") is structural:
`Show(i)` addresses `Ticks[i].Tick` directly.

#### 4.3 Timeline strip

The FILE source paints per-tick markers from the index booleans: input ticks, command dispatches
(WAL), snapshot points, and — from §6 — divergence ticks. This is the F29 slider widened into a
marked strip; the slider value remains the scrub tick.

---

### 5. State-at-tick (F64)

Scrubbing shows the recorded *image* at tick N. F64 additionally reconstructs the *values* at N by
the contract-7 machinery (restore-and-replay proven exact,
`tests/imguiapp_core_tests.cpp:292-330`; `big-idea.md:47-51`):

1. **Identity gate.** State reconstruction is legal only when a reconstruction app exists whose
   `GetAppCompositionID` (`imguiapp.h:414`) equals `Index.Identity.composition_id` and whose
   `schema_hash` matches (av-design.md:246-255 taxonomy; `Identity` at `imguiapp_av.h:101-110`). In
   the Composer that app is the Mirror (the running binary that IS this composition). On mismatch,
   §4's image scrub, io stream, WAL, and divergence still work — only value reconstruction is refused,
   loudly, before frame 1.
2. **Restore nearest snapshot.** Binary-search `SnapshotTicks` for the greatest `S <= N`; splat that
   `StateSnapshot`'s bytes (read at `Ticks[S].SnapshotOffset`; single-shot reader shape shipped as
   `AppAVMetaReadStateSnapshot`, `imguiapp_av.cpp:1551`) into the reconstruction app's storage,
   matched by the slot `{id,size}` table — the mirror image of `AppStateRestore`
   (`imguiapp.cpp:1256+`).
3. **Replay S→N.** Feed ticks `S+1..N` of the reconstructed `ImGuiAppInputLog`
   (`AppAVMetaReadInputLog`, `imguiapp_av.cpp:1493`) through the replay loop — inject each
   `InputFrame`'s TempData, call `UpdateApp(app, dt)`, no render — exactly `AppInputReplay`
   (`imguiapp.h:381`; impl `imguiapp.cpp:1217-1254`). `out_first_divergence` falls out here for §6.
   Replay is render-free, so reconstructing any N is cheap.
4. **Inspect.** The reconstruction app's registered storage now holds the app AT tick N. The inspector
   renders each instance's `Persist` and `Temp` through the reflection field-widgets (ImStructTable /
   the reflection walk `imguiapp_reflect.h`, the same table the live mirror uses,
   `archive/feature-complete-checklist.md:563`). Persist/LastTemp is the `[0, TempOffset)` prefix; Temp is the
   `[TempOffset, TempOffset+TempSize)` input range (`imguiapp.h:842-844`).
5. **Command log for N.** The WAL slice `Ticks[N].WalFirst..+WalCount` — the `[tick:N]` lines,
   `"execute command %d"` among them — is that tick's dispatch list, shown verbatim.

**Acceptance (F64):** at any N, the reconstructed `AppStateHash` (`imguiapp.h:386`) equals the
recorded `Ticks[N].StateHash`; replay from a snapshot is exact.

> Requires opt-in `InputHdr`/`InputFrame` in the take (`AppRecordAttachInputLog`,
> `imguiapp_av.cpp:1101`). A raw-io-only take (no TempData) still scrubs images + io + WAL + snapshots,
> but value reconstruction *between* snapshot ticks is unavailable — F64 then reports values only at
> snapshot ticks and greys the inspector elsewhere. The debugger states which capability the loaded
> take supports rather than faking values.

---

### 6. Divergence semantics (F65)

Two independent comparisons; the timeline paints both as a divergence marker, jump-to-first visits the
earliest.

**(a) Recording integrity — is the artifact itself intact?** From the load-time verify ladder
(`AppAVMetaVerify`, `imguiapp_av.cpp:1441-1487`; ladder table av-design.md:257-268):
- the io **chain** `chain_k = ImHashData(&state_hash_k, 4, chain_{k-1})`, seeded by `Identity.schema_hash`, is recomputed over the surviving IoFrames; the first tick whose stored `chain`
  differs is `Stats.ChainDivergesAt` (io-frame ordinal) — a reorder, splice, or bit-flip is
  reorder-evident there.
- the final **Digest** over the whole logical stream gives `Stats.DigestState` (0 ok / 1 missing /
  2 mismatch).

A **corrupted fixture** (tampered bytes) breaks the chain at exactly the touched tick and/or fails the
digest; that tick is marked. This is F65's `corrupted-fixture` acceptance ("flags the right tick;
clean run shows none") — a clean run has `ChainDivergesAt == -1` and `DigestState == 0`, hence no
markers.

**(b) Replay fidelity — does the build still reproduce the run?** When §5 runs, `AppInputReplay`
compares the *replayed* `AppStateHash` at each tick against the *recorded* `state_hash`
(`imguiapp.cpp:1241-1242`) and returns `out_first_divergence`. A mismatch means the reconstruction app
computed a different state than was recorded — **nondeterminism in this build vs the captured run**
(a changed `OnUpdate`, an unrecorded input, an unregistered field), NOT artifact damage. This is the
diagnostic verb: "at which tick does today's code stop matching the recording?"

**Definition (frozen).** A tick's `state_hash` is the recorded ground truth. It is compared against
(a) its own recomputed chain link — corruption — and (b) a deterministic replay's live
`AppStateHash` — infidelity. A marker means "recorded ≠ authoritative recomputation"; the tooltip
names which layer fired. Jump-to-first-divergence targets `min(ChainDivergesAt, out_first_divergence)`
over whichever layers are active — always an **exact tick**, so the transport lands on it via §4.2.

---

### 7. Non-goals & constraints

- **No new byte format, no upstream edits.** The container is the harness's existing outputs; the meta
  stream, pixel strip, WAL, and integrity ladder are shipped and frozen. F62–F65 are a loader, an
  index, a source adapter, and two comparisons — all at the applayer seam
  (`feedback_no_upstream_edits.md`).
- **Read-only over the artifact.** The debugger never rewrites a recording; the reconstruction app is
  a scratch replay target, never the source.
- **Identity mismatch degrades, never crashes.** Image/io/WAL/integrity always work; value
  reconstruction is gated on composition + schema equality and refused up front.
- **Live and file are one transport.** The LIVE ring (`ComposerTransport`) and a FILE run are the same
  scrub surface differing only in `Source`; the F70 tie (preview → record → open) closes the loop back
  through this same container.
- **Ring dumps are runs too.** A flight-recorder dump (`AppRecordDumpRing`,
  `imguiapp_av.cpp:1184-1291`) is a valid recording with the same header/Identity/Digest (its chain is
  recomputed over survivors at dump time); the loader opens an assert-dump exactly like a full take —
  no special case.


---

<a id="previewer-design"></a>

## Previewer — interpret the composed graph live (F66)

### 1. Summary & goals

The **previewer** runs the composition *without generating or compiling it*: rewire a dependency,
retype a field, author an event, and the previewed app behaves differently **the next frame** — the
shadertoy loop. It is the LIVE half of P9.5's "composition is executable data" pair — the playback
debugger (F61) replays a captured trajectory offline; the previewer *executes* the model each frame.

The previewer is **a second backend for the object model, beside codegen**. Codegen *emits* the exact
C++ a demo author writes by hand (`AppEmitControlWithDeps`, `imguiapp_nodes.cpp:10522`); the previewer
*evaluates the same model* every frame and emits nothing. The two backends share one authority for
every decision — the type checker, the effective field lists, the storage machinery, the four-phase
pipeline — so "what the preview does" and "what the generated code does" are the same statement read
two ways. Nothing the previewer needs is new physics: it re-runs shipped rails.

This doc **freezes the interpreter's semantics** so F67 (interpreter core), F68 (preview surface),
F69 (contract parity), and F70 (time-travel/record tie) land with no open questions. The centrepiece
is the **per-node-kind semantics table** (§9): every node kind classified *interpreted / reflected /
stub* with its exact rule, so F67 is implementable straight from the table.

Companion documents (binding cross-references, cited by name below):
[vocabulary-nodes-design.md](#vocabulary-nodes-design) (F53 — the interpreter's vocabulary *is*
F53's semantics: op-fold expressions, animation builtins' Task-phase dt update, layout nodes),
[big-idea.md](big-idea.md) (the four-phase pipeline the interpreter executes),
[bug-classes.md](bug-classes.md) (the temp^last skew every animator and brush obeys),
[metrics-debugger-coherence-design.md](#metrics-debugger-coherence-design) (the selection channel
and StatusPill grammar the preview reuses), [playback-debugger-design.md](#playback-debugger-design)
(F61 — the shared F29 transport and run container F70 closes against).

**Thesis (five moves).**
1. **The model is the program.** The interpreter is a pure function of the authored graph + one input
   frame; no side-model, no code artifact between author and run. §2.
2. **One evaluation authority.** The Task/Command/Window passes the interpreter runs are the *same*
   passes the framework runs (`imguiapp.cpp:469-521`); the expression evaluator is a value-returning
   walk of the *same* grammar the type checker walks (`AppEventExprCheck`, `imguiapp_nodes.cpp:10471`);
   Ops fold into the consumer's expression exactly as codegen folds them (vocabulary §1.5). §3–4.
3. **Never fake user C++.** A custom C++ control body renders as a reflected field-widget CARD with a
   "body runs after Generate" note — the interpreter renders the honest field surface and executes
   nothing the user wrote by hand. §5.
4. **Storage is the shipped storage.** Persist/Temp/LastTemp allocated from effective field lists
   through `RegisterAppStorage` (`imguiapp.cpp:1036`), so `ImGuiAppStateHistory`, `AppStateHash`, and
   contract-7 restore/replay work over the previewed app with zero new machinery. §6, §10.
5. **Edit-while-running preserves by (name, type).** A graph edit rebuilds the store field-by-field,
   copying every surviving `(sanitized name, type)` slot and default-initialising the rest — a rewire
   changes behaviour next frame without losing unrelated field values. §7.

**House constraints honored throughout:** no upstream edits — the interpreter lives at the applayer
seam, never touching imgui/imnodes/implot (`feedback_no_upstream_edits.md`); pointers not references;
per-instance interpreter state rides a heap session object opaque to the graph (like `_Ed`,
`imguiapp_nodes.h:660`, and `ComposerTransport`, `imguiapp_demo.cpp:326-331`) — **no TU globals**
(`feedback_no_tu_globals.md`); `char[]` + `ImVector`, no `std::string`; ASCII status text only; every
mechanism below is cited to the rail it reuses.

---

### 2. The interpreter session (scope + placement)

**Scope of interpretation = everything the MODEL defines.** The interpreter runs, per frame: builtin
controls with model-defined semantics, design-drafted controls, ops (folded into consumer
expressions), the animation builtins (Tween/Timer/Spring/Pulse) with F53's dt rules, events (the
checked AST `when <edge> -> set/emit`), commands (collect/latch/dedup/dispatch-once), window/sidebar
composition (rendered with reflected widgets bound to live storage), and layout nodes (a DockBuilder
pass). Everything a compiled ImGuiApp would do *except* running hand-written C++ bodies (§5).

**Session object (per-instance, no globals).** One heap `ImGuiAppPreview` per previewed document,
created on first preview frame, freed with the process — the same lifetime discipline as the editor's
`_Ed`. It owns: the interpreter's `ImGuiApp` (a real framework app instance whose Layers/Windows/
Controls the interpreter pushes — so `RegisterAppStorage`, the update-order rebuild, and StateHistory
apply verbatim), the value stores (§6), the per-instance store manifests (§6.1), the run/pause/reinit
state, the dispatched-command log (§4.3), and the last interpreter signature (§7). It is **not**
serialized and **not** part of any snapshot — exactly `ComposerTransport`'s status
(`playback-debugger-design.md §4`). The graph document is never mutated by running it (render purity,
contract; `bug-classes.md §1c`).

**The interpreter's app is a real `ImGuiApp`.** Rather than reimplement the pipeline, the interpreter
*builds* a live `ImGuiApp`: it pushes the framework core layers (`PushAppLayer<ImGuiAppTaskLayer>` …,
`imguiapp.cpp:1447-1448`) and, for each interpreted Control node, pushes an **interpreter control**
(§3) that carries the node's effective fields, events, commands, and dependency wiring. Then
`UpdateApp` (`imguiapp.cpp:1479`) and `RenderApp` (`imguiapp.cpp:1491`) drive the four phases with no
special-casing. This is what makes F69 "one suite, two implementations" true: the contract suite
already targets an `ImGuiApp`; the interpreter *is* one.

---

### 3. The interpreter control (one class, dynamic fields)

A design-drafted control's C++ type does not exist, so the interpreter cannot instantiate a
`ImGuiAppInterfaceAdapter<…>` (`imguiapp.h:920`). Instead **one** compiled `ImGuiAppPreviewControl :
ImGuiAppControlBase` stands in for every interpreted control; its "type" is data, carried per instance:

- Its `PersistData`/`TempData`/`LastTempData` are **flat byte buffers** sized and laid out from the
  effective field lists (§6.1), not C++ structs — the equivalent of `InstanceData`'s three members
  (`imguiapp.h:924-929`) but dynamically sized.
- `GetControlDataID()` returns the node's data-type id (the same id `AppGraphStampPorts` stamps on the
  DataOut, so dependency keying by type is unchanged and `GetAppCompositionID` sees it,
  `imguiapp.cpp:1313-1318`).
- `OnInitialize` default-initialises the Persist buffer from field defaults; `OnUpdate` runs the Task
  work (§4); `OnGetCommand` drains the command latches (§4.3); `OnRender` renders the field-widget
  panel and records interaction into the Temp buffer (§8).
- Dependencies resolve by the same type-keyed lookup the framework uses (`app->Data.GetVoidPtr`,
  `imguiapp.h:949`) — one producer per PersistData type, topo push order — because the interpreter
  control registers real storage under the real type id.

Animation builtins (Tween/Timer/Spring/Pulse) and RandomTime are interpreter controls too, but their
`OnUpdate` runs the **defined closed-form rule** (§4.4) rather than field-generic Task work; their
field layout is fixed by vocabulary §2.1 rather than a draft. A builtin control the interpreter has
**no rule for** falls through to the reflected-card path (§5, §9).

---

### 4. The frame loop (four phases, interpreted)

The interpreter runs the framework's own pass order (`big-idea.md`, table lines 13-18); each phase
maps to a real framework call, cited:

#### 4.1 Task pass — ingest & update (sole mutator)
`ImGuiAppTaskLayer::OnUpdate` iterates `AppRebuildUpdateOrder` in dependency-topo order
(`imguiapp.cpp:469-478`, `1322-1383`). For each interpreter control, `OnUpdate(dt, persist, temp,
last_temp, deps…)`:
1. **Dependency binding lines.** Copy each wired dep field into a Persist/local slot — the interpreter
   form of the emitter's binding assignment (`data->Dst = dep->Src;`, `imguiapp_nodes.cpp:10707-10761`;
   `ImGuiAppFieldBinding`, `imguiapp_nodes.h:429`). Read via the dep's manifest (§6.1).
2. **Events** (§4.2), in authored order.
3. Persist is the sole thing written; Temp is read-only here (it was recorded last render); LastTemp is
   the previous Temp. The interpreter never writes state outside this pass — contract "OnUpdate is the
   sole mutator" holds by construction.

#### 4.2 Events — `when <TempField> <edge> -> set/emit`
`ImGuiAppEventDesc` (`imguiapp_nodes.h:379-387`): `TempField`, `Edge`, `Action`, `DstField`, `Expr`,
`Command`. Per event, in `OnUpdate`:
- **Edge test** on the watched TempField, reading temp vs last_temp from the store (edges
  `imguiapp_nodes.h:360-364`): Rising `temp && !last`, Falling `!temp && last`, Changed `temp ^ last`,
  Active `temp`. This is the `^` idiom (`big-idea.md:38-44`) evaluated, not emitted.
- **SetField** → `persist[DstField] = eval(Expr)` (§4.5); empty `Expr` copies `temp[TempField]`
  (`imguiapp_nodes.cpp:10476-10477`). Type fit to the destination is already guaranteed by
  `AppEventExprCheck` (`imguiapp_nodes.cpp:10495-10514`) — the evaluator writes through the manifest's
  slot type.
- **EmitCommand** → set the command's **latch** (a Persist bool the interpreter allocates per
  `(instance, command)`, the interpreter form of codegen's `<Cmd>Pending`, `imguiapp_nodes.cpp:10532`).
  Task runs before Command, so a latch set this frame dispatches this frame.

#### 4.3 Command pass — collect / dedup / dispatch-once
`ImGuiAppCommandLayer::OnUpdate` calls each control's `OnGetCommand`, dedups linearly, and dispatches
each distinct command once in first-emission order (`imguiapp.cpp:495-521`). The interpreter control's
`OnGetCommand` emits a latched command (edge form) or a level-gated command (Active edge / folded op
gate, the `OnGetCommand` guard shape at `imguiapp_nodes.cpp:10667-10671`). Dispatch target: the
interpreter has **no user `OnExecuteCommand`** to run, so dispatch appends `(tick, command name)` to
the session's dispatched-command log — surfaced in the preview and fed to the F70 recorder. No user
C++ runs; the framework's dedup/once guarantee is inherited unchanged (F69 contract).

#### 4.4 Animation builtins — the defined dt rules (interpreted)
Each animator's `OnUpdate` runs vocabulary §2.1's closed form over its Persist accumulator (the sole
mutator; the `RandomTime` rng precedent, `imguiapp_demo.cpp:68-69,95-106`):
| Builtin | Persist (DataOut fields) | Task update (dt) | Trigger |
|---|---|---|---|
| **Timer** | `elapsed`, `done` | `elapsed += dt; done = elapsed >= duration` | restart on rising trigger (temp^last) |
| **Tween** | `value`, `t`, `done` | `t = clamp(t + dt/duration, 0,1); value = ease(a,b,t)` | restart on rising trigger |
| **Spring** | `value`, `velocity` | `velocity += (k*(target-value) - c*velocity)*dt; value += velocity*dt` | `target` from dep/field |
| **Pulse** | `pulse`, `phase` | `phase += dt/period; if (phase>=1){phase-=1; pulse=true;} else pulse=false` | free-running |
`duration`/`period`/`target`/`k`/`c`/ease-selector are Persist params or wired deps. Inputs come from
**wired deps**, never injected OS input — headless-deterministic (`feedback_headless_only_verification.md`,
vocabulary §2.1). Determinism under Fixed-dt scrub: accumulators live wholly in the snapshottable
Persist store (§6), so restore-and-replay reproduces the trajectory byte-for-byte (contract 7,
vocabulary §2.4). Spring's damped step is not time-symmetric, but scrub rewinds by snapshot restore,
not backward integration — deterministic regardless (vocabulary §2.4).

#### 4.5 The expression evaluator (one grammar, value-returning)
Ops and event/binding expressions are evaluated by a walk that mirrors `AppEventExprCheck`'s
recursive-descent productions (`AppExprOr → AppExprAnd → AppExprXor → AppExprEq → AppExprRel →
AppExprAdd → AppExprMul → AppExprUnary → AppExprPrimary`, `imguiapp_nodes.cpp:10229-10469`) but returns
a tagged runtime value (`bool`/`int`/`float`/`double`) instead of a type. Because the checker already
proved every construct re-parseable and type-fits its destination, the evaluator is a parallel walk —
one grammar, two visitors (typecheck / evaluate), the single-authority rule (vocabulary §6, rejected
"parallel type-checker").
- **Primary roots** resolve to the store (`imguiapp_nodes.cpp:10259-10279`): `temp_data`/
  `last_temp_data` → this control's list-1 buffer (current/previous), `data` → list-0 Persist, a dep
  param name → the producer instance's list-0 buffer (resolved through `AppGraphConsumerDeps`). Struct
  member chains hop one `.` per level through the referenced Struct node's manifest
  (`imguiapp_nodes.cpp:10296-10317`). Builtin-dep fields are `Unknown` to the checker
  (`imguiapp_nodes.cpp:10275,10291`); the evaluator reads them by name from the builtin's interpreted
  store (the animator field names of §4.4).
- **Ops** carry no runtime object: an Op subtree **folds into the consumer's expression string**
  exactly as codegen folds it (`(a > 0) && b`, vocabulary §1.5), and that string runs through the same
  evaluator. Op result pins have `DataTypeId = 0` and fan out freely (vocabulary §1.1) — the evaluator
  treats them as sub-expressions, never as stored values. This is the "Ops as runtime objects"
  rejection (vocabulary §6) honored at run time.

#### 4.6 Layout + Window/Status passes — render (pure)
`RenderApp` iterates layers' `OnRender` (`imguiapp.cpp:1491-1501`). The **Layout** layer runs its
DockBuilder sequence before any window Begins (layer order Layout-before-Display,
`AppGraphEnsureFoundation`, vocabulary §3b, `imguiapp_nodes.cpp:1981-1991`): Region/Split/Tabs →
`DockBuilderAddNode`/`SplitNode`/tab-bar, each Window's `Region=` field selecting its `DockWindow`
target (vocabulary §3b codegen block) — run directly instead of emitted into `OnLayout()`. The
**Window/Sidebar** pass Begins each host and renders its hosted controls' field-widget panels bound to
live storage (§8). The **Status** framework layer renders the built-in status bar
(`ImGuiAppStatusLayer::OnRender`, `imguiapp.cpp:544`); a *custom* Status layer's body is not run (§5).
Render mutates no Persist — widget interaction writes only the Temp input buffer (§8), the framework's
"OnRender records TempData" contract (`big-idea.md:31`).

---

### 5. Custom C++ control bodies — the reflected card (never faked)

A **custom C++ control** is a control whose real behaviour is a hand-written `OnUpdate`/`OnRender` in a
compiled type — code the interpreter cannot and must not synthesize. The previewer renders it as a
**reflected field-widget card**, never an execution:

- **What renders:** the control's effective Persist and Temp fields as labelled widget rows — the exact
  `AppGraphRenderMockPanel` / `AppMockRenderFields` surface (`imguiapp_nodes.cpp:9749,9694`) — bound to
  live storage (§8) so values move and can be poked, plus a one-line note **"body runs after
  Generate"** and a listing of its authored events/commands (the same summary the mock panel already
  prints, `imguiapp_nodes.cpp:9795-9808`). If the custom type is *compiled and present* in the process
  (the live-mirror case), the card reflects the **real** members with `VisitAppFields` /
  `ImAppReflect` (`imguiapp_nodes.h:110-121`, `imguiapp_reflect.h`) — the same reflection the live node
  inspector uses (`imguiapp_nodes.cpp:9767-9772`).
- **What does NOT run:** the user's `OnRender` widget layout and any imperative `OnUpdate` logic beyond
  what the *model* declares (events/commands/bindings, which the interpreter runs generically). The
  card is the honest boundary: the design control's field panel *is* its interpreted UI; once the user
  authors a real body, that body is C++ that only exists after Generate — the note states exactly this.
- **Rule:** a control is "custom C++" for the previewer when it declares a compiled backing type with
  no interpreter-known semantics — i.e. `IsBuiltin` with no §4.4 rule, or a design control the user has
  marked as carrying a hand-written body. Every other control is interpreted (§9).

This never fabricates behaviour: the field values are real (live storage), the widgets are honest
reflections of the declared fields, and the "runs after Generate" note prevents the card from being
mistaken for the real body.

---

### 6. Storage — effective fields through `RegisterAppStorage`

#### 6.1 Store layout (the manifest)
For each interpreter control instance the session builds a **manifest**: the effective field list
(`AppNodeEffectiveFields`, `imguiapp_nodes.cpp:2144` — exploded Field nodes when present, else the
inline draft list) resolved to `(sanitized name, ImGuiAppFieldType, arraySize, byte offset)` rows,
packed at natural alignment. Field sizes: Float/Int 4, Bool 1, Double/Vec2 8, Vec4 16, String
`ArraySize` bytes, Struct = the referenced Struct node's manifest packed recursively
(`ImGuiAppFieldType_`, `imguiapp_nodes.h:180-191`). The instance buffer is the three sub-buffers in
`InstanceData` order — **Persist | LastTemp | Temp** (`imguiapp.h:924-929`) — so the `[0, TempOffset)`
prefix is state and `[TempOffset, TempOffset+TempSize)` is this-frame input, exactly the split
`AppStateHash` reads (`imguiapp.cpp:1144`). The manifest is the interpreter's dynamic reflection: the
widget renderer (§8) and the expression evaluator (§4.5) both address slots through it.

#### 6.2 Registration
Each instance registers through the snapshottable, input-ranged overload:
`RegisterAppStorage(app, instanceKey, buffer, size = persist+lasttemp+temp, temp_offset = persist+lasttemp,
temp_size, destroy)` (`imguiapp.h:365`, `imguiapp.cpp:1036`). Consequences, all inherited:
- `AppStateSnapshot`/`AppStateRestore` byte-copy the snapshottable slots keyed by
  `GetAppCompositionID` (`imguiapp.cpp:1088-1131,1256-1277,1302-1320`) — App-time scrub over the
  previewed app is free.
- `AppStateHash` fingerprints the Persist+LastTemp prefix (`imguiapp.cpp:1135-1150`) — the per-tick
  digest F70 records and the playback debugger verifies.
- `instanceKey = ImGuiAppInstanceKey(dataTypeId, instance)` (`imguiapp.h:345`); `CompositionRevision`
  bumps on every register/unregister (`imguiapp.cpp:1055`), driving the update-order rebuild
  (`imguiapp.cpp:1329`). No TU statics: every accumulator lives in the registered buffer — the
  no-globals rule is load-bearing for determinism (vocabulary §2.4).

---

### 7. Edit-while-running reconciliation (preserve by (name, type))

Graph edits apply on the **next frame** with no reinit of unrelated state. The policy:

**Detect.** Fold an **interpreter signature** over the authored population that determines the run —
per interpreted node: `Kind`, `Draft.Name`, each effective Persist/Temp field's `Name`/`Type`/
`ArraySize`, `DataTypeName`, `TypeName`, `IsBuiltin`, events, commands, bindings; per authored link its
endpoints + kind. This reuses `AppGraphSignature`'s hashing discipline (NUL-terminated `ImHashStr` on
`char[]`, exclude transient/layout fields; `metrics-debugger-coherence-design.md §B3`). A change in the
signature triggers reconcile before the next Task pass.

**Reconcile (field-level merge, not wholesale reinit).** For each interpreted Control node in the new
graph:
- **Surviving instance** (same node/data-type id): rebuild its manifest (§6.1); allocate the new
  buffer; for every field in the **new** effective list, if a field with the **same `(sanitized name,
  ImGuiAppFieldType)`** existed in the old manifest, **copy its bytes** (Persist and LastTemp;
  Temp is this-frame input, re-recorded); otherwise **default-initialise** it. Dropped fields are
  discarded; a retyped field (name matches, type differs) is treated as new → default. Re-register
  storage.
- **New Control node:** fresh instance, default-initialised, `OnInitialize` run.
- **Vanished Control node:** `UnregisterAppStorage` (destroy + remove, `imguiapp.cpp:1279`).

**Rebuild dependents.** The register/unregister bumps `CompositionRevision`, so `AppRebuildUpdateOrder`
re-topo-sorts and `RefreshControlDependencyData` re-resolves every consumer's dep pointers
(`imguiapp.cpp:1329-1380`) — a rewire re-routes dependencies for next frame. `GetAppCompositionID`
changes, so `ImGuiAppStateHistory` clears and re-lays its slots (`imguiapp.cpp:1094-1099`) — the
shipped composition-change rule; the App-time ring restarts, exactly as it does on any push/pop today.

This is F68's accept criterion made precise: *a rewire changes behaviour next frame without reinit
losing unrelated fields*, because only structurally-changed slots reinit; every `(name, type)`-stable
slot carries its value across the edit.

---

### 8. Input routing + selection brushing

#### 8.1 Input routing
The preview surface hosts **real ImGui widgets**: each interpreted/reflected control renders its field
panel (the §5/§6 widget rows) inside its host window during the Window pass. User interaction with a
widget writes the bound **Temp** slot through the manifest — the interpreter's `OnRender`-records-input
step (`big-idea.md:31`; the framework `TempData` contract). The next frame's Task pass compares
temp^last and derives events (§4.2). So input maps with zero routing table: the previewed app's inputs
*are* imgui inputs to the panel widgets, and the framework's one-frame skew turns them into edges. The
widget switch is `AppMockRenderFields`' switch (`imguiapp_nodes.cpp:9694-9747`) rewritten to read/write
manifest offsets in live storage instead of scratch `ImGuiStorage` — bool→Checkbox, int→DragInt,
float/double→DragFloat, Vec2→DragFloat2, Vec4→ColorEdit4/DragFloat4, String→InputText (the same
type→widget table as `EditAppField`, `imguiapp_nodes.h:79-108`).

**Headless discipline (absolute).** The preview is driven ONLY through imgui itself — the
`imguix-headless-verify` on-camera harness/test engine actuates the panel widgets; **never** OS input
injection (`feedback_headless_only_verification.md`). F68's accept test drives a preview widget and
asserts the model value moved — a pure imgui interaction, no synthetic OS events.

#### 8.2 Selection brushing (composer ⇄ preview)
Selection is **one id, two surfaces**, extending the metrics-debugger selection channel
(`metrics-debugger-coherence-design.md` Theme C) from canvas↔tree to canvas↔preview:
- **Composer → preview.** When a Control node is the primary selection (`*selected_node_id`,
  `imguiapp_nodes.h:741-745`) or hovered, its instance's widget group **haloes** in the preview: the
  panel measures its group rect during the Window pass and an overlay draws the outline. This is a
  coherent T-1 publish/consume pair (`ScopeWallRect` pattern, `imguiapp_nodes.h:622`; `bug-classes.md`
  rule 1) — measured last frame, drawn this frame, never mixing a fresh transform with a stale rect.
- **Preview → composer.** When the user hovers/clicks a widget in the preview, the owning instance's
  node id is published into the editor's hover **brushing bus** (`HoverNode`/`HoverPrevNode`, render
  reports into TempData, readers see it next frame — `imguiapp_nodes.h:555-563`, the same `^`-skew bus
  the canvas already uses), so the composer node haloes. Click promotes to `*selected_node_id`. One
  selection id reconciled both ways; the pan-only-on-tree-origin rule (Theme C) carries over — a
  preview click never yanks the canvas camera.

---

### 9. THE SEMANTICS TABLE

Classification: **interpreted** = the previewer executes the model's defined semantics each frame (no
hand-written C++); **reflected** = the previewer renders the node's declared fields as widgets/readout
bound to live storage but executes no user body ("runs after Generate"); **stub** = ignored for
execution (placeholder only). Every node kind (`imguiapp_nodes.h:238-249`, plus F53's appended Op/Layout
and the animation builtins) appears exactly once; F67 implements straight from this table.

| Node kind | Class | Phase | Interpretation rule (cite) |
|---|---|---|---|
| **App** (root singleton) | interpreted | — | The session anchor: owns the interpreter `ImGuiApp`, the value stores, and the four-phase loop. No widget. (`imguiapp.h:878`, §2) |
| **Layer / Task** | interpreted | Task | `OnUpdate` over `AppRebuildUpdateOrder` in topo order; sole mutator. (`imguiapp.cpp:469-478`, §4.1) |
| **Layer / Command** | interpreted | Command | Collect `OnGetCommand`, dedup linear, dispatch-once first-emission order → dispatched-command log. (`imguiapp.cpp:495-521`, §4.3) |
| **Layer / Status** | interpreted (framework) | Render | Framework status bar renders. A *custom* Status subclass body is not run → stub note. (`imguiapp.cpp:544`, §4.6) |
| **Layer / Layout** | interpreted | Render (pre-window) | Run the DockBuilder sequence from child Layout nodes before windows Begin. (vocabulary §3b; `imguiapp_nodes.cpp:1981-1991`, §4.6) |
| **Layer / Display** | interpreted | Render | Container/order for Window/Sidebar rendering. (§4.6) |
| **Layer / Custom** | reflected + stub | — | A user `ImGuiAppLayer` subclass: its C++ `OnUpdate`/`OnRender` is **not** run (named stub in the stack, "body runs after Generate"); its contained windows still render. (§5) |
| **Window** | interpreted | Render | Real `Begin`/`End`; hosts its controls' field panels bound to live storage; honors `Region=`/placement fields. (§4.6, §8.1) |
| **Sidebar** | interpreted | Render | Docked host, same as Window with `DockDir`/`DockSize`. (§4.6) |
| **Control — builtin, animation** (Tween/Timer/Spring/Pulse) | interpreted | Task | The vocabulary §2.1 closed-form dt rule over its Persist accumulator; deterministic under Fixed-dt scrub. (§4.4) |
| **Control — builtin, RandomTime** | interpreted | Task | splitmix64 rng in Persist, seeded in `OnInitialize`, stepped only in `OnUpdate` — the precedent all animators follow. (`imguiapp.h:287`, `imguiapp_demo.cpp:68-69,95-106`) |
| **Control — builtin, other (no rule)** | reflected | Render | Compiled type with no interpreter semantics → reflected card (real members if compiled/live, else field-widget card) + "body runs after Generate". (§5) |
| **Control — design draft** (plain fields) | interpreted | Task + Render | Store from effective fields (§6.1); Task runs bindings + events + command latches; Render draws the live-bound field panel = its interpreted UI. (§3, §4, §8.1) |
| **Control — custom C++ body** | reflected | Render | Field-widget card bound to live storage + events/commands listing + "body runs after Generate"; hand-written body **not** executed. (§5) |
| **Struct** | interpreted (data) | — (Task-domain data) | Contributes a packed field manifest to its consumer's store, or a standalone Task-domain data record; its members address slots. No widget of its own. (`imguiapp_nodes.h:245`, §6.1, §4.5 member chains) |
| **Field** | interpreted (data) | — | One member of a struct's effective list (exploded); drives exactly one store slot and one widget row. (`imguiapp_nodes.cpp:2144-2168`) |
| **Note** | stub | — | Non-semantic annotation frame; excluded from interpretation exactly as it is excluded from codegen/validation. (`imguiapp_nodes.h:247`) |
| **Op** (AND/OR/XOR/NOT/compare/select/min/max) | interpreted (folded) | Task | No runtime object: folds into the consumer's expression string and is evaluated by the shared evaluator; result pin `DataTypeId=0` fans out freely. (vocabulary §1.5, §4.5) |
| **Layout** (Region / Split / Tabs) | interpreted | Render (pre-window) | A DockBuilder node (`AddNode`/`SplitNode`/tab-bar) in the Layout pass; windows dock via their `Region=` field. (vocabulary §3b, §4.6) |
| **Event** (`when <edge> → set/emit`) | interpreted | Task | Edge test temp^last on the watched TempField; SetField writes `data[Dst]=eval(Expr)`, EmitCommand sets a latch. Not a node kind — a Control sub-record. (`imguiapp_nodes.h:379-387`, §4.2) |
| **Command** (definition/selection) | interpreted | Command | Latched in Task, emitted by `OnGetCommand`, deduped + dispatched once → log; no user handler runs. (§4.3) |

---

### 10. Phase-coherence, determinism, and the F69/F70 close

- **Render purity.** The interpreter mutates Persist only in Task; Render writes only Temp (input) and
  draws — no model mutation mid-publication (`bug-classes.md §1c`). The graph document is never
  written by running it.
- **No measured-geometry feedback.** The only render-phase measurement is the selection-halo group rect
  and the layout dock tree, both consumed as coherent T-1 pairs or built once by DockBuilder
  (`bug-classes.md §1,§1b`, vocabulary §5) — no measure→apply loop, no settle.
- **Contract parity (F69).** Because the interpreter *is* an `ImGuiApp` with real registered storage,
  the shipped contract suite runs against it unchanged: UCR order (topo `AppRebuildUpdateOrder`),
  edge-once (§4.2 temp^last), same-frame latch (Task-before-Command, §4.3), dedup dispatch
  (`imguiapp.cpp:510-513`), pop symmetry (`UnregisterAppStorage`), render purity (above), time travel
  (§6.2 snapshot/restore) — "contracts 1-9 green on the interpreter; one suite, two implementations".
- **Time-travel / record tie (F70).** The previewed app feeds `ImGuiAppStateHistory` (App-time scrub,
  `AppComposerAppTimeFrames`, `imguiapp.h:455`) and the recorder; a preview session exports the F61
  run container (`AppInputRecord` + snapshots + digest chain), which the playback debugger opens and
  scrubs. LIVE ring and FILE run are the *same* F29 transport, different source
  (`playback-debugger-design.md §4`) — the loop **author → play → record → debug closes with zero
  compiles**.

### 11. Rejected alternatives

- **A separate interpreter runtime (not an `ImGuiApp`).** Rejected: it would fork the pipeline, storage,
  and StateHistory, and F69's "one suite, two implementations" would become two suites. Building a real
  `ImGuiApp` with interpreter controls inherits every contract for free (§2).
- **A second expression evaluator / type lattice.** Rejected — the vocabulary doc's standing rule
  (vocabulary §6): the evaluator is a value-returning walk of `AppEventExprCheck`'s own grammar, so the
  graph accepts exactly what the previewer evaluates and what codegen emits.
- **Op nodes as runtime objects in the preview.** Rejected: Ops fold into the consumer's expression at
  evaluation time exactly as they fold at codegen time (`DataTypeId=0`, stateless) — no per-node value,
  no topo slot (vocabulary §1.5, §6).
- **Executing (or synthesizing) custom C++ bodies.** Rejected, absolutely: the previewer renders the
  honest reflected field card with a "body runs after Generate" note and runs only what the *model*
  declares. Faking a body would make the preview lie about behaviour it cannot know (§5).
- **Wholesale reinit on any graph edit.** Rejected: it would discard unrelated field values on every
  rewire, failing F68. The `(name, type)` field-level merge preserves surviving state and reinits only
  structurally-changed slots (§7).
- **Injecting OS input to drive the preview.** Rejected by the headless-only rule
  (`feedback_headless_only_verification.md`): the preview is driven through imgui widgets by the
  on-camera harness; nothing synthesizes OS events (§8.1).


---

<a id="dll-preview-design"></a>

## DLL preview design (F76)

The live preview should run the **real generated program**, not an interpretation of the graph.
F67/F68 build a second in-process backend that *interprets* the AppEventExprCheck grammar and
manifest-bound storage; faithful, but it is a re-implementation of the framework's semantics and can
drift from what `GenerateAppGraphCode` actually emits. A DLL preview closes that gap: emit the app's
C++, compile it, load it, and tick the **same code the user will ship**. The interpreter stays as the
instant / no-compiler-present fallback (F66 §9 already frames the previewer as pluggable backends).

This doc fixes the mechanism before any code, per the "spec it first" decision.

### 1. The crux: one runtime, not two

The generated app subclasses framework types (`ImGuiApp`, `ImGuiAppControl<P,T>`, `ImGuiAppWindow<T>`),
calls `ImGui::*` widgets, and its `PushAppLayer/Window/Control` helpers touch framework state. For the
host to *run* an object the DLL *creates*, host and DLL must agree on, and SHARE, exactly one of each:

- **ImGui context** — the DLL's widgets must draw into the host's context (same `GImGui`, same atlas,
  same draw lists). Two contexts = the preview draws nowhere the host can see.
- **Allocator** — an object `IM_NEW`'d in the DLL and `IM_DELETE`'d in the host (or vice-versa) must use
  one heap. Two allocators = cross-heap free = corruption.
- **Framework globals** — imguix has process-global state (e.g. the DisplayLayer's ini settings handler;
  `flow3` already documents the double-register hazard). Two copies of imguix = duplicate globals.
- **Type layout + vtable ABI** — the host reads the DLL-built object through framework base pointers, so
  struct layouts and vtable order must be identical. Guaranteed when both compile the *same* headers with
  the *same* MSVC toolset (the preview compiles against this repo's headers, and we pin the compiler — §4).

The first three are the design problem. Two candidate resolutions:

#### Verdict: **shared framework core** (build change), NOT static-relink-with-bridging

- **Rejected — static relink + runtime bridging.** Let the DLL statically link imgui+imguix, then have the
  host inject its context (`ImGui::SetCurrentContext`) and allocator (`ImGui::SetAllocatorFunctions`) into
  the DLL at load. This shares the *imgui* context and heap, but does NOT share imguix's own globals: the
  DLL gets its own copy of the settings handler, the AV registry, every file-static. The preview app would
  double-register process-global handlers the moment it composes a DisplayLayer — the exact bug `flow3`
  guards. Rejectable on that alone; also fragile (every future imguix global is a latent duplicate).

- **Chosen — a shared `imguix-core` DLL.** Split the framework runtime (imgui + the `imguiapp*` core, minus
  the demo/composer/editor) into a shared library that BOTH the host exe and every preview DLL link against.
  One context, one allocator, one copy of every global, one set of vtables — by construction, not by
  bridging. The preview DLL contains only the *generated* translation unit; it is thin and fast to compile.
  Cost: a build-system change (a shared target + `IMGUI_API`/`IMGUIX_API` decorated as
  `__declspec(dllexport/dllimport)`). That cost is the correct one — it is the only option that makes
  "the DLL's object and the host are the same program" TRUE rather than approximately true.

### 2. What the emitter adds: a preview entry, not a `main()`

`GenerateAppShellCode` today appends an `AppShell : ImGuiApp` + `int main()`. Add a sibling emitter
`GenerateAppPreviewModuleCode` that appends, instead of `main()`, a C-ABI surface (extern "C", so no C++
name-mangling contract across the boundary — the objects still cross as framework base pointers):

```cpp
extern "C" __declspec(dllexport) ImGuiApp* ImGuiAppPreview_Create(ImGuiViewport* vp)
{
  AppShell* app = IM_NEW(AppShell)();   // shared allocator via imguix-core
  IM_UNUSED(vp);
  return app;                            // composes lazily on its first initialized OnDrawFrame (as today)
}
extern "C" __declspec(dllexport) void ImGuiAppPreview_Destroy(ImGuiApp* app) { IM_DELETE(app); }
extern "C" __declspec(dllexport) unsigned int ImGuiAppPreview_ABI() { return IMGUIAPP_PREVIEW_ABI; }
```

`ImGuiAppPreview_ABI()` returns a compile-time constant baked into both host and generated module; a
mismatch (stale headers, wrong toolset) is refused at load with a clear message rather than crashing on a
layout skew. No context/allocator init export is needed — imguix-core owns the single copy.

### 3. Reload lifecycle (per-instance, no TU globals)

State lives on the composer's `ImGuiAppPreviewDll` session object (rides the editor state, mirroring F68).

1. **Edit → dirty.** The composer already tracks `AppGraphSignature`; a change (layout-excluded, as F68)
   marks the DLL preview dirty.
2. **Compile (async, off the frame thread).** Regenerate the module `.cpp`, write it to a scratch dir,
   invoke the compiler (§4) into `preview_<n>.dll` (monotonic name — never overwrite a loaded DLL; Windows
   locks it). Compilation runs on a worker; the UI keeps ticking the *last-good* DLL. No frame stalls.
3. **Swap when ready.** On success: snapshot the running app's state (reuse F68's capture — Persist+LastTemp
   by (sanitized name, type, size) manifest), `ImGuiAppPreview_Destroy` the old app, `FreeLibrary` the old
   DLL, `LoadLibrary` the new, `ImGuiAppPreview_Create`, then restore the snapshot slot-for-slot (new/retyped
   fields keep their zero default — identical policy to the interpreter's reconcile, so behavior matches).
4. **Failure keeps the last good.** A compile error never tears down the running preview; see §5.

`FreeLibrary` ordering matters: destroy the app (its vtables live in the DLL) BEFORE unloading, or the
`IM_DELETE` virtual dispatch reads freed code. The monotonic-name scheme means an in-flight compile can't
clobber the DLL currently mapped.

### 4. Compiler invocation

- **Locate the toolset once.** Resolve `cl.exe` + the environment via `vswhere` → `VsDevCmd`/`vcvars64`,
  cached on the session. If none is found, the DLL backend is unavailable and the preview silently uses the
  F67 interpreter — the fallback is not an error, it is the no-compiler path.
- **Command.** `cl /LD /std:c++20 /O2 /MD <module.cpp> /I<repo include dirs> /link imguix-core.lib` →
  `preview_<n>.dll`. `/MD` (dynamic CRT) so host and DLL share one CRT heap — required for cross-boundary
  new/delete even with the shared allocator, because framework code may allocate through the CRT.
- **Pin the toolset** to the one that built the host (record its version; the ABI constant in §2 encodes
  it). A preview built by a different compiler is refused, not run.
- Compilation is the latency cost of fidelity: a thin generated TU against a prebuilt `imguix-core.lib`
  import lib compiles in well under a second; that is the "runtime" in "runtime live preview".

### 5. Error surfacing

Capture the compiler's stderr. On failure, the preview panel shows the diagnostics (file:line from the
generated `.cpp`, mapped back to the offending node via the existing `ImGuiAppCodeSpan` source-map so a
compile error highlights the node, not an opaque line number) and a "using last good build" banner. The
previously-loaded DLL keeps running. This is strictly better than the interpreter, which can only refuse
edits it cannot model; the DLL preview refuses only edits that do not COMPILE, and says exactly why.

### 6. Testing (headless, on-camera)

- `dll_preview_roundtrip` (headless): author a one-control graph, drive the DLL backend through a compile +
  load + create, tick frames, drive a widget, assert the model value moved — the same assertion F68's
  `step102` makes against the interpreter, now against real compiled code. Skipped-with-note when no
  compiler is present on the box (CI without a toolset), never silently vacuous.
- `dll_preview_reload_preserves`: run, edit a field's sibling, recompile+reload, assert the unrelated
  field's bytes survived and the rewired behavior changed next frame — the F68 preserve contract, proven on
  the DLL path.
- Contract parity (F69) gains a third column: contracts 1-9 that already run on framework + interpreter also
  run on the DLL backend where a compiler exists.

### 7. Roadmap placement

- **F76 DLL preview design doc** — this file (verdict: shared `imguix-core`; C-ABI create/destroy; async
  monotonic-name reload; toolset-pinned compile; interpreter is the no-compiler fallback).
- **F77 imguix-core shared split** — carve the framework runtime into a shared target with exported
  `IMGUI_API`/`IMGUIX_API`; host + tests link it; no behavior change (a pure build refactor, all suites
  stay green). Gates F78.
- **F78 DLL preview backend** — `GenerateAppPreviewModuleCode`, the `ImGuiAppPreviewDll` session (compile /
  load / create / tick / async reload / state preserve / error surface), the Preview tab's backend toggle
  (DLL when a toolset exists, interpreter otherwise), and the §6 tests.

No code precedes this doc's acceptance; F77 precedes F78 (the shared core is the ABI foundation the whole
approach rests on).


---

<a id="source-embed-design"></a>

## Source Embed — real code, not fake skeletons + the write-back fold  (Phase B design)

Goal: with the CONTROLS' source baked into the tools build, the Composer / Previewer / Debugger show the
REAL function bodies (kill the perceived generated skeleton), surface source contextually, and — the "final
fold" — write a tested edit back to the real file on disk. Gated by Phase A's `IMGUIX_DISABLE_TOOLS`: the
lean build embeds nothing (and keeps source text out of the `.exe`); the tools build deliberately bakes it.
Design-first (this doc) before code; write-back touches real files, so the safety design is load-bearing.

### 1. Motivation

Today the Code tab shows GENERATED C++ — a perceived skeleton (`// TODO: render widgets…`), not the bodies
that actually run. F78.5 already lets a control carry real hand-written bodies; B generalizes: bake the
controls' real source so every tool view resolves to real code, and close the author→test→write loop.

### 2. Scope (decided)

Embed the **controls' source only** — the user's control implementations, where F78.5 bodies + write-back
live. NOT the framework, NOT the whole repo. (Finer subset — headers too? only the control TUs? — settled
during build.)

### 3. Pieces

#### 3.1 The embed (build-time)
- **Format (OPEN fork):** a generated byte-array/string-table `.cpp` compiled into the tools build (like the
  codegen corpus, but carrying source text) vs a loaded resource. Proposed: a generated TU (`file → source
  bytes`), produced at configure/build from the control source files, under `IMGUIX_DISABLE_TOOLS` so it
  never enters a lean build.

#### 3.2 Symbol → source index
- Map a control / command / node → its real file + declaration + **body span** (start/end offsets). This is
  the lookup behind hover, the Code tab, AND the write-back target.
- **Build (OPEN fork):** parse to get spans (clang tooling? a light C++ range scan keyed off the control
  type/method names the reflection already knows?) vs a build-emitted manifest. Must survive Phase C's reorder
  (spans are offsets, so regenerate the index at build, don't hard-code).

#### 3.3 Real-code presence (read)
- Code tab shows the REAL body (from the index) — with a real-vs-generated view (generated = "what ships";
  real = "what runs"). Hover a node/control → its actual source. Debugger shows real code at a tick.
  Design-time preview references real impls. F78.5 bodies are already real; this extends to embedded source.

#### 3.4 The final fold (write-back)
- Loop: edit a control's OnRender (F78.5) body → test live in the previewer/DLL (V0) → the tool writes the
  edit back to the REAL file on disk at the mapped span. Inverse of the F22–F24 import edge → full
  bidirectional source↔graph↔source.
- **Tiers:** (1) **method bodies** map to one file + one span → surgical replace, FIRST target (pairs with the
  previewer test-before-write); (2) **structural** edits (add field / rewire dep / new event) regenerate the
  skeleton → reconciling generated structure vs hand-touched source, LATER.
- **Safety (load-bearing):** a **diff/confirm gate** before any disk write (show the exact hunk); **span
  accuracy** (write only the mapped body span, never the whole file); **staleness** — hash/mtime the source
  at index time and refuse (or offer re-index/reconcile) if the file changed outside the tool since; never
  write on a stale index.

#### 3.5 Interactive preview (folds V0's residual)
- Forward host input into the DLL preview so in-panel widgets (V0 render) become interactive — needed for the
  "test it live" half of the fold to be real interaction, not just playback.

### 4. Acceptance

- Real control bodies render in the Code tab from the embedded index (not the skeleton), tools build only.
- A tested method-body edit writes back to the real source at the correct span, behind a diff/confirm gate;
  a headless test drives edit → write-back → re-reads the file and asserts the body changed at the right span.
- Staleness guard fires when the file is touched outside the tool.
- Lean build (`IMGUIX_DISABLE_TOOLS`) embeds nothing; `strings` shows no control source in the binary.

### 5. Open forks (settle before/within build)

- Embed format (generated `.cpp` vs resource) and exact subset (bodies only? whole control TU + headers?).
- Index build mechanism (clang tooling vs light scan vs build manifest).
- Write-back conflict UX when the source drifted from the index.


---

<a id="lean-tools-split-design"></a>

## Lean/Mean Split — `imguiapp_internal.h` + `IMGUIX_DISABLE_TOOLS`  (Phase A design)

Goal: a clean core/tool boundary so a release build compiles the authoring tools (Composer, Previewer,
Debugger — graph model, canvas, codegen, preview backends) out entirely. Result: a true "lean & mean"
runtime, and — because the tools are what bake control source + generated-code strings — no source-code
text in the shipped `.exe`. One master switch, mirroring how `imconfig.h` / `IMGUI_DISABLE` gate imgui.

Design-first (gates the code); nothing built before this doc is agreed.

### 1. Precedent (imgui)

imgui ships: public `imgui.h` + `imconfig.h`; internal `imgui_internal.h`; impl split by topic
(`imgui.cpp` / `imgui_draw.cpp` / `imgui_tables.cpp` / `imgui_widgets.cpp` / `imgui_demo.cpp`). We mirror
this: keep the per-topic `.cpp` split (NO `imguiapp_widgets.cpp` — `imguiapp_canvas.cpp` + `imguiapp_nodes.cpp`
stay separate), add an `imguiapp_internal.h`, and gate the tool TUs behind one macro.

### 2. Core vs tool classification

**THE RULE (decided): if it's UI, it's a tool; anything else is NOT a tool.** So the switch
(`IMGUIX_DISABLE_TOOLS`) gates UI only; all non-UI machinery stays core.

**CORE — always on (everything that is not UI):**
- `imguiapp.h` (already `[SECTION]`-structured like imgui.h) + `imguiapp.cpp`: the runtime — `ImGuiApp`,
  layers, controls, `RegisterAppStorage`, `UpdateApp`/`RenderApp`, `ImGuiAppStateHistory`, `ImGuiAppWAL`,
  commands, `GetAppCompositionID`, `ImGuiAppPlatform`/backends. `imguiapp_config.h` (gains the switch, §4).
- **Graph MODEL + CODEGEN** (not UI): `ImGuiAppGraph`/`AppGraph*` (add/serialize/validate/topo),
  `GenerateApp*Code` + the emitter/importer. (Lives in `imguiapp_nodes.cpp` today, intertwined with the
  editor UI — §MIXED.)
- **RECORDER + encoder** (records with tools OFF): `AppRecord*`, `AppMetaRecord*`, `ImGuiAppRecorder`,
  `ImGuiAppMetaRecorder`, `ImGuiAppAVEncoder` seam, `ImGuiAppAVMeta*` write, frame capture. Impl may move
  into `imguiapp.cpp`; the backends' encoder-hook is core.
- **DECODER / loader / playback read side** (not UI): `AppRunOpen`/`Close`/`TickCount`/`TickAt`/
  `StateAtTick`, `ImGuiAppRunIndex`, the frame decode. (F62–F65 read machinery — core; only the transport
  *UI* is tool.)
- **Interpreter CORE** (F67 headless build+run in `imguiapp_preview.cpp`): `AppPreviewCreate`/`Frame`/
  `Reconcile` + the evaluator — not UI.
- anim builtins (`ImApp*`); the AV codec (`qoi`/`libav`/`mediafoundation`).

**TOOL — UI only (gated by `IMGUIX_DISABLE_TOOLS`):**
- The Composer (`imguiapp_demo.cpp`); the graph EDITOR UI (`ShowAppGraphEditor`/`ShowAppGraphTree` + inspector
  / panel rendering in `imguiapp_nodes.cpp`); the canvas UI (`imguiapp_canvas.cpp`, entirely UI); the preview
  SURFACE (F68 widget surface + brushing in `imguiapp_preview.cpp` / `imguiapp_preview_dll.cpp`); the playback
  transport UI + bug-button UI (in the demo).

**⚠ MIXED files (the real work):** `imguiapp_nodes.cpp` (model+codegen = core / editor UI = tool) and
`imguiapp_preview.cpp` (interpreter core = core / F68 surface = tool) each hold both → the UI must be SPLIT
out (own gated TU), not gated wholesale. `imguiapp_canvas.cpp` + `imguiapp_demo.cpp` are UI-only → gate
wholesale. `imguiapp_av.cpp` is record+decode, no UI → core.

**`imguiapp_internal.h`** holds the TOOL-UI + internal-helper interfaces; its tool-UI declarations are
wrapped in `#ifndef IMGUIX_DISABLE_TOOLS` so the macro hides the UI in the header too. The core
model/codegen/recorder/decoder/interpreter/anim interfaces stay in CORE headers (`imguiapp.h` / a core model
header / a core `imguiapp_av.h`), unconditional — so a lean build still links model+codegen+recorder+decoder+
interpreter+anim (a generated app still `PushAppControl<ImAppTween>` (F56) and still records/opens runs); it
drops only the UI + Phase-B's embedded source. "No source in the `.exe`" comes from gating the embed
(Phase B) — codegen skeleton strings are small and stay.

### 3. `imguiapp_internal.h`

New header, the `imgui_internal.h` analog. It AGGREGATES the TOOL-UI interfaces + internal-only helpers:
the graph editor UI (split out of `imguiapp_nodes.cpp`), the canvas UI (`imguiapp_canvas.h`), the preview
SURFACE UI (split out of `imguiapp_preview.cpp` + `imguiapp_preview_dll.h`), and the demo/transport/bug-button
UI decls. It does NOT hold the core machinery: the graph MODEL + CODEGEN, the RECORDER, the DECODER/loader
(`AppRun*`), the interpreter CORE, and anim all keep CORE headers (`imguiapp.h` / a core model header / a
core `imguiapp_av.h`). `imguiapp_internal.h`'s tool-UI decls are wrapped in `#ifndef IMGUIX_DISABLE_TOOLS`
(the macro hides the UI in the header too); the core headers it does NOT own stay unconditional. The deep
reorder is Phase C; Phase A is header re-home + the mixed-file UI split, not a rewrite.

### 4. The gate

- `imguiapp_config.h`: `// #define IMGUIX_DISABLE_TOOLS` documented; when defined, the tools compile out.
- `imguiapp_internal.h`'s tool-UI declarations are themselves wrapped in `#ifndef IMGUIX_DISABLE_TOOLS` …
  `#endif` — the macro HIDES the UI in the HEADER too (a lean consumer that includes `internal.h` sees no UI
  API), not just in the impl. Any non-UI internal helper decls stay unconditional. This does NOT strand the
  lean runtime: the UI=tool rule already moved anim / decoder / model / codegen / interpreter interfaces OUT
  of `internal.h` into CORE headers, which stay unconditional — so a lean build still links them.
- The UI-only impl TUs likewise wrap their bodies (imgui's `IMGUI_DISABLE` pattern): `imguiapp_canvas.cpp`,
  `imguiapp_demo.cpp`, the graph-editor-UI TU split out of `imguiapp_nodes.cpp`, and the preview-surface UI
  split out of `imguiapp_preview.cpp` / `imguiapp_preview_dll.cpp`. The MIXED files' CORE remainder
  (`imguiapp_nodes.cpp` model+codegen; `imguiapp_preview.cpp` interpreter core; `imguiapp_av.cpp` record+decode)
  is NOT gated.
- CMake (`imguix/CMakeLists.txt`): the tool sources stay in `IMGUIX_SOURCES` (they self-empty); add an
  option `IMGUIX_ENABLE_TOOLS` (default ON) that, when OFF, adds `IMGUIX_DISABLE_TOOLS` to
  `IMGUIX_COMPILE_DEFINITIONS`. The demo/composer executable target is not built in the lean config.

### 5. Acceptance

- Normal build (tools ON): nodes / core / headless suites unchanged + green — this is a pure re-home + gate,
  zero behavior change.
- Lean build (`-DIMGUIX_ENABLE_TOOLS=OFF`): the core library + a minimal host compile and link with ZERO
  tool symbols; a `strings` scan of the lean binary shows none of the control/source/generated-code text the
  tools embed.
- The core/tool boundary (this doc's §2) is the contract Phase B (embed) and Phase C (schema) build on.

### 6. Non-goals (deferred)

Deep declaration/definition reordering + comment discipline = Phase C. Source embedding = Phase B. This
phase only moves interfaces into `imguiapp_internal.h` and adds the gate.
