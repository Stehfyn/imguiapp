# The Big Idea

ImGuiAppLayer is a claim about structure: an application is one **module** among independent
modules — worker threads, asynchronous I/O, external processes — and a frame is one iteration of a
**control loop**: sample what the world reports, act on what it asks, publish your own state, then
present — mutating nothing. Made explicit, user code decomposes into orthogonal pieces a machine
can analyze, mirror, and generate.

## The pipeline

Four layers run in fixed order every frame, one per phase of the loop:

| Layer | Phase | Contract |
|---|---|---|
| **Task** | *Sample & update* | Ingest the status of independent modules (a worker thread's file I/O, an async job) and update application state: every control's `OnUpdate` runs here, in dependency order — the sole mutator of persistent state. |
| **Command** | *Dispatch* | Collect commands the app received — from its own controls (`OnGetCommand`) or, eventually, other modules — deduplicate, dispatch each once. |
| **Status** | *Publish* | Set the app's own status for other modules to query. (Today that manifests as the status bar; the queryable model is the endpoint.) |
| **Window** | *Present* | Presentation only. All state is already decided — render the current model, mutating **nothing**. The name is Dear ImGui heritage; semantically it is the present phase. |

Task precedes Command, so state decided this frame commands this frame. Present runs last and is
pure. The order is not convention — it is the semantics generated code compiles against.

## Edge detection, not event storage

Immediate-mode UI samples input *during* presentation, so a naive program mixes state from
different loop iterations. The fix is pipelining: defer input processing one frame behind a double
buffer. Each control keeps a three-part memory:

- **PersistData** — durable state. One writer: `OnUpdate`.
- **TempData** — this frame's raw input sample, recorded by `OnDraw`, zeroed every frame.
- **LastTempData** — the previous frame's sample, maintained by the framework.

`OnUpdate` receives both samples and compares them. That one-frame skew restores the strict
sequence — sample, update, present — and makes **events derived, never stored**: an event is the
discrete derivative of state, an edge detected between two consecutive samples.

```c++
if (temp_data->hovered ^ last_temp_data->hovered)   // it changed
if (temp_data->pressed && !last_temp_data->pressed) // rising edge: it started
```

Render records what *is*; update computes what *happened*. No event queues, no callbacks, no stale
flags — a pure function of two consecutive states. (`ImAppRising / ImAppFalling / ImAppChanged`
name the vocabulary; the raw operators remain idiomatic.)

The discipline compounds:

- **Determinism.** `OnUpdate` is the sole mutator and every control's state lives in registered
  storage, so a byte snapshot of that storage *is* the application at that frame. Checkpoint,
  restore, and deterministic replay need zero per-app code (`ImGuiAppStateHistory`); the contract
  suite proves restore-and-replay reproduces trajectories byte-for-byte. **Time travel is a
  theorem here, not a feature** — the Composer toolbar exposes it as a scrubber.
- **Phase coherence.** Mixed-phase reads — a stale measurement combined with this frame's
  transform — are the endemic immediate-mode bug class. The pipeline exists to make them
  unrepresentable: derive in update, present from settled state ([bug-classes.md](bug-classes.md)).
- **Inert defaults.** TempData is value-initialized every frame, so the zero value of every field
  must mean *no action*. The default state of a program is inert by construction, not by guards
  ([bug-classes.md](bug-classes.md)).

## One producer per type

Dependencies are `const` pointers keyed by **type**: `app->Data` holds exactly one instance per
PersistData type — a functional dependency. The data flow is therefore a directed acyclic graph;
push order is a topological order over it. Cycles are detectable, ordering is computable, and
"wire A into B" has a single meaning.

## Why a node editor

Every concept above is a graph-theoretic object, so the Composer is not a picture of the app — it
edits the app in its own representation, the way a projectional editor edits a syntax tree:

- the composition tree → drill-down scopes (Tab in, Esc out, breadcrumb path);
- the frame pipeline → execution-order rails and sequence badges;
- edge detection → authorable events (`when <temp field> <edge> → <reaction>`) that generate the
  exact guarded blocks a demo author writes by hand;
- the invariants → machine-checked well-formedness, not gesture enforcement: containment is a
  forest, edges are kind-correct, each data type keeps one producer, event writers are confluent.

The graph round-trips to C++ and back — emit and import reach a fixed point, byte-locked by tests.
Generation is flexible precisely because the model's soundness is a checked relation, not a UI habit.

## The self-hosting shell

If the composition is executable data, the Composer can emit the host that runs *itself* — the
compiler-bootstrap move. The graph emitter already produces the composition body (data structs,
command dispatch, the `SetupApp` that pushes layers, windows, and controls); a **shell** emission
wraps it in the scaffold a standalone program needs: a concrete `ImGuiApp` composed via the emitted
`SetupApp`, and the `main()` that runs it. Nothing new is generated for the app's guts — the
scaffold is pure wiring.

The payoff is a Composer hosting a Composer. The editor ships as a library control
(`ImGuiAppComposerControl`) any app can `PushAppControl<>`: it owns the graph it edits and drives
`ShowAppGraphEditor`, so the editor implementation stays library code. A composition whose one
control is that Composer emits a complete host shell that, compiled and run, *is* a Composer — the
tool's output is the tool. The emitted shell only includes and pushes the library; it never
re-emits the editor. That the shell compiles and runs headless proves the round trip closes on
itself: model → C++ → a program that edits the model.

---

The library extends `namespace ImGui` from its own files and holds itself to the upstream coding
and API conventions, specified in [imgui-house-style.md](imgui-house-style.md).
