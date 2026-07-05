# Previewer — interpret the composed graph live (F66)

## 1. Summary & goals

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
[vocabulary-nodes-design.md](vocabulary-nodes-design.md) (F53 — the interpreter's vocabulary *is*
F53's semantics: op-fold expressions, animation builtins' Task-phase dt update, layout nodes),
[big-idea.md](big-idea.md) (the four-phase pipeline the interpreter executes),
[phase-coherence.md](phase-coherence.md) (the temp^last skew every animator and brush obeys),
[metrics-debugger-coherence-design.md](metrics-debugger-coherence-design.md) (the selection channel
and StatusPill grammar the preview reuses), [playback-debugger-design.md](playback-debugger-design.md)
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

## 2. The interpreter session (scope + placement)

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
contract; `phase-coherence.md §1c`).

**The interpreter's app is a real `ImGuiApp`.** Rather than reimplement the pipeline, the interpreter
*builds* a live `ImGuiApp`: it pushes the framework core layers (`PushAppLayer<ImGuiAppTaskLayer>` …,
`imguiapp.cpp:1447-1448`) and, for each interpreted Control node, pushes an **interpreter control**
(§3) that carries the node's effective fields, events, commands, and dependency wiring. Then
`UpdateApp` (`imguiapp.cpp:1479`) and `RenderApp` (`imguiapp.cpp:1491`) drive the four phases with no
special-casing. This is what makes F69 "one suite, two implementations" true: the contract suite
already targets an `ImGuiApp`; the interpreter *is* one.

---

## 3. The interpreter control (one class, dynamic fields)

A design-drafted control's C++ type does not exist, so the interpreter cannot instantiate a
`ImGuiInterfaceAdapter<…>` (`imguiapp.h:920`). Instead **one** compiled `ImGuiAppPreviewControl :
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

## 4. The frame loop (four phases, interpreted)

The interpreter runs the framework's own pass order (`big-idea.md`, table lines 13-18); each phase
maps to a real framework call, cited:

### 4.1 Task pass — ingest & update (sole mutator)
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

### 4.2 Events — `when <TempField> <edge> -> set/emit`
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

### 4.3 Command pass — collect / dedup / dispatch-once
`ImGuiAppCommandLayer::OnUpdate` calls each control's `OnGetCommand`, dedups linearly, and dispatches
each distinct command once in first-emission order (`imguiapp.cpp:495-521`). The interpreter control's
`OnGetCommand` emits a latched command (edge form) or a level-gated command (Active edge / folded op
gate, the `OnGetCommand` guard shape at `imguiapp_nodes.cpp:10667-10671`). Dispatch target: the
interpreter has **no user `OnExecuteCommand`** to run, so dispatch appends `(tick, command name)` to
the session's dispatched-command log — surfaced in the preview and fed to the F70 recorder. No user
C++ runs; the framework's dedup/once guarantee is inherited unchanged (F69 contract).

### 4.4 Animation builtins — the defined dt rules (interpreted)
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

### 4.5 The expression evaluator (one grammar, value-returning)
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

### 4.6 Layout + Window/Status passes — render (pure)
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

## 5. Custom C++ control bodies — the reflected card (never faked)

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

## 6. Storage — effective fields through `RegisterAppStorage`

### 6.1 Store layout (the manifest)
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

### 6.2 Registration
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

## 7. Edit-while-running reconciliation (preserve by (name, type))

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

## 8. Input routing + selection brushing

### 8.1 Input routing
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

### 8.2 Selection brushing (composer ⇄ preview)
Selection is **one id, two surfaces**, extending the metrics-debugger selection channel
(`metrics-debugger-coherence-design.md` Theme C) from canvas↔tree to canvas↔preview:
- **Composer → preview.** When a Control node is the primary selection (`*selected_node_id`,
  `imguiapp_nodes.h:741-745`) or hovered, its instance's widget group **haloes** in the preview: the
  panel measures its group rect during the Window pass and an overlay draws the outline. This is a
  coherent T-1 publish/consume pair (`ScopeWallRect` pattern, `imguiapp_nodes.h:622`; `phase-coherence.md`
  rule 1) — measured last frame, drawn this frame, never mixing a fresh transform with a stale rect.
- **Preview → composer.** When the user hovers/clicks a widget in the preview, the owning instance's
  node id is published into the editor's hover **brushing bus** (`HoverNode`/`HoverPrevNode`, render
  reports into TempData, readers see it next frame — `imguiapp_nodes.h:555-563`, the same `^`-skew bus
  the canvas already uses), so the composer node haloes. Click promotes to `*selected_node_id`. One
  selection id reconciled both ways; the pan-only-on-tree-origin rule (Theme C) carries over — a
  preview click never yanks the canvas camera.

---

## 9. THE SEMANTICS TABLE

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

## 10. Phase-coherence, determinism, and the F69/F70 close

- **Render purity.** The interpreter mutates Persist only in Task; Render writes only Temp (input) and
  draws — no model mutation mid-publication (`phase-coherence.md §1c`). The graph document is never
  written by running it.
- **No measured-geometry feedback.** The only render-phase measurement is the selection-halo group rect
  and the layout dock tree, both consumed as coherent T-1 pairs or built once by DockBuilder
  (`phase-coherence.md §1,§1b`, vocabulary §5) — no measure→apply loop, no settle.
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

## 11. Rejected alternatives

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
