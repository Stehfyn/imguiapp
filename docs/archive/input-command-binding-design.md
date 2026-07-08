# Input→Command Binding — remappable chords over the reified command registry

How the Composer separates the *trigger* (a key chord) from the *verb* (an editor command), so a user
can rebind which chord runs Copy/Paste/Delete/… without touching the verb. Retrofit of the F34
command registry, not a parallel system.

Primary source: Robert Nystrom, *Game Programming Patterns*, "Command" (Design Patterns Revisited,
first pattern). Read verbatim; quotes below are cited to that chapter. Companion docs:
[composer-ui-design.md](composer-ui-design.md) (§T2 status-bar keymap, §"four roads"),
[metrics-debugger-coherence-design.md](metrics-debugger-coherence-design.md) (surface hidden
diagnostics), [bug-classes.md](bug-classes.md) (derive once, in phase),
[archive/feature-complete-checklist.md](archive/feature-complete-checklist.md) (F01 harness, F58 precedent).

## 1. The problem — input welded to verb

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

## 2. Command-pattern framing (what exists, what's missing)

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

## 3. Data model

### 3.1 Registry stays the DEFAULT source; the keymap is a sparse override

Decision: **`Key`/`Mods` remain in the registry as the factory-default source of truth.** The runtime
keymap is *derived* from it, and per-graph user changes layer on top as a sparse diff. Rejected
alternative (move chords entirely into the keymap) in §9.

Justification: (a) "unchanged out of the box" is then free — an empty override set means the effective
keymap *equals* the registry-derived default; (b) the registry stays the self-documenting completeness
anchor the four-roads test iterates (`step72`, §7); (c) persistence stays minimal — a default graph
serializes **zero** keymap lines, so F01 byte-stability is trivially preserved (§5); (d) reset-to-default
is a *delete*, not a rewrite.

### 3.2 Two representations

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

### 3.3 Resolution, precedence, conflicts

Effective chord of a verb = its override if one exists, else its registry default. Dispatch walks the
effective table and fires the **first** binding whose chord matches — order is precedence, mirroring the
chapter's `handleInput()` scanning buttons in sequence. A *conflict* is two active bindings sharing one
chord; the first-in-registry-order wins, so a conflict is never undefined — only a **shadowed** verb,
which the rebind UI surfaces as a warning (§6), in the spirit of "surface hidden diagnostics"
(metrics-debugger design). Exact-mods matching (via `IsKeyChordPressed`): `H` (id 25) and `Alt+H`
(id 26) do not collide, and `Ctrl+Z` never fires plain `Z`.

### 3.4 Reserved / unbindable chords

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

## 4. Dispatch flow

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

## 5. Persistence

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

## 6. Rebind UI — the keymap editor

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

## 7. step72 invariant evolution

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

## 8. Validation + edge cases

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

## 9. Rejected alternatives

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

## 10. Acceptance criteria (headless gates)

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
