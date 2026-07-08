# Vocabulary Nodes Design — logic Ops, animation builtins, layout nodes

The Composer's node vocabulary today is structural (App / Layer / Window / Sidebar / Control / Struct /
Field / Note; `imguiapp_nodes.h:240-248`). Three families extend it with *behaviour*: logic **Op** nodes
(F54/F55), **animation** builtin controls (F56), and **layout** nodes (F57). This document decides their
semantics so the downstream code lands with no open questions. It is a decision document — every verdict
below is binding on F54-F57.

Companion documents: [big-idea.md](big-idea.md) (the four-phase pipeline the codegen compiles against),
[bug-classes.md](bug-classes.md) (the temp^last one-frame skew the animation builtins obey),
[scope-interior-design.md](scope-interior-design.md) (scope domains + altitude; the Layout layer gets its
first interior here), [archive/up-next.md](archive/up-next.md) (module-interop layer model).

Each new kind states **four rows**: palette legality (which scope offers it), scope domain (where it
updates / what `AppScopeParentOf` returns), validation, and codegen shape. Constraints honored throughout:
never edit imgui/imnodes/implot; per-instance model state on the graph object (no TU globals); pointers not
references; ASCII-only in generated code.

## 0. Shared ground (the rails all three families ride)

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

## 1. Op nodes (`ImGuiAppNodeKind_Op`) — F54 / F55

> **VERDICT: BUILD.** One appended kind `ImGuiAppNodeKind_Op`. Ops are a graph-authoring form of an
> expression AST that **folds into the consumer's emitted expression** — no runtime object, no `app->Data`
> entry, no push line. The operator set is AND / OR / XOR / NOT, compare (`== != < <= > >=`), select/mux,
> and min/max. The single type authority is `AppEventExprCheck` — F54 adds no second type lattice.

### 1.1 Model

An Op node stores its operator token in `TypeName` (already serialized as `Type=`, `:11620`); its label
rides `Draft.Name`. `IsBuiltin = false` (there is no compiled backing type). Ports (new
`AppGraphStampPorts` case): N `DataIn` operand pins fixed by operator arity — NOT(1), compare/min/max/AND/
OR/XOR(2), select(3) — plus one `DataOut` "result" pin **stamped with `DataTypeId = 0`**. The zero id is
load-bearing: `AppGraphResolveLink`'s one-producer-per-type check only fires for non-zero ids (`:3028`), so
an Op result fans out to any number of consumers and never collides — correct, because an Op produces no
PersistData type.

### 1.2 Palette legality

Ops are app-level combinational logic; they belong wherever expression authoring happens.

| Scope | Offers Op? | `AppScopeKindComposable` row |
|---|---|---|
| root (no scope) | yes | `scope < 0` branch already passes (`:6412`) |
| Task-type Layer | yes | extend `:4950-4951` to also return true for `ImGuiAppNodeKind_Op` |
| Window / Sidebar / Control / Struct / other layers | no | default `false` |

Domain: `AppScopeParentOf` gains `case ImGuiAppNodeKind_Op: return <Task layer id>` (same fallback the
standalone Struct uses, `:4760-4762`) — an Op is Task-domain app data. `AppScopeCanEnter` is **not**
extended: an Op has no interior.

### 1.3 Type rules (ride AppEventExprCheck)

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

### 1.4 Validation

- **Cycles refused** by the shared guard: Op result→operand wires are `Data` edges, so
  `AppGraphResolveLink`'s `AppGraphDataReaches` check (`:3032-3034`) already rejects a chain that loops.
- **Arity**: every operand pin must be wired or carry an inline token; an empty operand is an error (mirrors
  the empty-expr handling at `:10478-10479`, which is legal only for a whole-event default).
- **Type**: the folded string is run through `AppEventExprCheck` and, when the chain feeds a SetField event,
  fit to its `DstField` (`:10497-10515`). This is one call to the existing checker — the structural graph and
  the string agree by construction.

### 1.5 Op-fold codegen (F55)

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

### 1.6 Import note (binding)

The generated C++ contains only the folded string — the graph structure of the Op nodes is **not**
recoverable from it. On re-import (the inverse of `AppEmitControlWithDeps`) the string restores as an
`ImGuiAppEventDesc::Expr` (`imguiapp_nodes.h:385`), **not** as Op nodes. The Op structure's only home is the
`.graph` file (`AppEmitNodeRecord`, `:11613-11644`). F55 records this: *folded output re-imports as an
expression, not as op nodes.* This is consistent with the project's rule that the graph file, not the C++,
carries authoring structure the runtime does not need.

---

## 2. Animation builtins (Tween / Timer / Spring / Pulse) — F56

> **VERDICT: BUILD** as four **builtin Controls** via `AppGraphAddBuiltin` (`:1951-1970`), the RandomTime
> precedent (`imguiapp_demo.cpp:275`). They are ordinary controls — `ImGuiAppNodeKind_Control`,
> `IsBuiltin = true`, a compiled C++ type, a typed `DataOut` — so palette legality, scope domain, wiring,
> codegen, mirror, and time-travel all reuse the control machinery unchanged. The only new surface is an
> **"Animation" palette section** grouping the four and their compiled types.

### 2.1 The set and their DataOut

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

### 2.2 Phase discipline (each obeys phase-coherence)

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

### 2.3 Codegen (push + wiring)

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

### 2.4 App-time scrub determinism (F29 StateHistory, Fixed-dt)

Every animator's whole state is its PersistData accumulator, held in registered snapshottable storage. Under
Fixed-dt, `ImGuiAppStateHistory` byte-snapshots that storage each frame and restores it on scrub
(`big-idea.md`, "time travel is a theorem"). Because OnUpdate is the sole mutator and dt is fixed,
restore-and-replay reproduces the trajectory byte-for-byte (contract 7) — the RandomTime rng precedent
generalizes to Tween `t`, Timer `elapsed`, Spring `{value,velocity}`, and Pulse `phase`. The accumulators
must therefore be **fully inside PersistData** (no static/local carry, no TU global) — the standing
no-TU-globals rule, here load-bearing for determinism. *Accept (F56): Tween advances deterministically under
Fixed-dt and App-time scrub restores it.*

---

## 3. Layout nodes — F57 (all three candidate models evaluated)

### 3a. Window placement facts — baseline

> **VERDICT: KEEP, do not replace.** These already exist post-F02 and are correct for their job.

`HasInitialPlacement` / `InitialPos` / `InitialSize` / `DockDir` / `DockSize` are fields on the Window/
Sidebar node (`imguiapp_nodes.h:400-404`), inspector-edited (`:4176-4214`), shown on the scope wall
(`:5588-5601`), serialized as `Init=` / `Dock=` lines (`:11630-11633`, parsed `:11934-11935`), and emitted
into bring-up (`:11216-11224`, `:11553-11588`). They express *absolute first-use placement of one host* —
not composition. They are **fields, not a node kind**, and the Layout family below is additive on top of
them, never a rewrite. (`InitialPos/Size` are today gated to live-mirror emission at `:11457-11458`,
`:11561-11566`; making them emit for authored windows is an orthogonal, already-serialized tweak, out of
scope here.)

### 3b. Region / Split / Tabs composing into the Layout layer — PRIMARY

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
scope grammar of [scope-interior-design.md](scope-interior-design.md) applies unchanged (walls = the
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

### 3c. Constraint / anchor edges

> **VERDICT: REJECT** (formal, per F53's charge). Recorded in the rejected-alternatives section below.

---

## 4. Serialization & round-trip

| Family | New serialized surface | Round-trip home |
|---|---|---|
| Op | `Kind=` (appended int), operator in `Type=`, operand `Port=` rows (`:11617,11620,11637`) | `.graph` file only; C++ carries the folded **string** (re-imports as `Expr`, §1.6) |
| Animation | none new — builtin control `Kind=Control`, `Builtin=1`, `Type=`/`DataType=` (`:11619-11621`) | full round-trip via the control importer; compiled type supplies the shape |
| Layout | `Kind=` (appended int), variant in `Type=`; Window `Region=` line (parallel to `Dock=`, `:11632`) | `.graph` file; C++ carries the DockBuilder sequence + `DockWindow` labels |

All three obey the append-only kind rule (`imguiapp_nodes.h:247`): `ImGuiAppNodeKind_Op` and
`ImGuiAppNodeKind_Layout` are appended after `_Note`, before `_COUNT`. F01/F05 (record survives
save/load/undo) cover them by extension, not by parallel rails.

## 5. Phase-coherence compliance (checklist applied at design time)

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

## 6. Rejected alternatives

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
