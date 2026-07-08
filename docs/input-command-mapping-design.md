# ImGuiAppInputMapping + ImGuiAppCommandMapping — the command pattern as an out-of-the-box app helper

What is being built: a generic, app-level command registry (`ImGuiAppCommandMapping`) and a layered,
remappable input→command binding table (`ImGuiAppInputMapping`), generalizing the editor-only F34/F74
machinery so ANY ImGuiApp application declares verbs once, binds inputs to them, and gets palette /
shortcut / rebinding / record-replay integration for free. Approach: string-name identity first
(additive), register-by-use ergonomic core second, determinism spine third. Riskiest assumption:
recordings can carry the RESOLVED command (never the chord) without the verb/gesture boundary leaking
mapper-time UI state into operands.

Primary source: Robert Nystrom, *Game Programming Patterns*, "Command" — the *"Configuring Input"*
section and the returned-command variant (*"we can delay when the call is executed"*). Direct
predecessor: [archive/input-command-binding-design.md](archive/input-command-binding-design.md) (F74,
editor-scoped); this design promotes that pattern from the Composer to the app layer. Companions:
[bug-classes.md](bug-classes.md) (derive once, in phase), [imgui-house-style.md](imgui-house-style.md)
(pull seams, data tables over code).

## 0. What exists (the three half-instances)

The codebase already contains three partial implementations of this pattern; the design unifies them
rather than adding a fourth:

- **F34 editor registry** — `ImGuiAppEditorCommand` (`imguiapp_internal.h:720`): one static table
  `s_editor_commands[]` (`imguiapp.cpp:17827`; retired — rows move onto the app-object registry at
  editor init), Id-keyed dispatch switch, `Surfaces` bitmask, an availability predicate, and the
  four-roads completeness test iterating the registry.
- **F74 keymap overrides** — `ImGuiAppKeyBinding` (`imguiapp_internal.h:585`): sparse serialized diff
  keyed by stable Id, `Key == ImGuiKey_None` = explicit unbind, carried per-document
  (`ImGuiAppGraph::Keymap`, `imguiapp_internal.h:978`).
- **Host-command pull seam** — `AppGraphSetHostCommands` / `AppGraphConsumeHostCommand`
  (`imguiapp_internal.h:1605`): per-frame registration, one-frame-latency pull, the library never
  calls host code. The WAL already logs `"execute command %d"` and the playback debugger correlates
  those lines to ticks (`AppRunAttachWal`, `imguiapp_internal.h:1247`).

Missing: a registry any app can populate (not just the editor), input mapping as a first-class
layered table (not a per-graph diff), and command dispatch as a recorded, replayable unit.

## 1. Decisions most likely to be tweaked (review these first)

### 1.1 Identity: dotted string names are the serialized identity; ints never persist

Every command carries a stable dotted name (`"edit.paste"`, `"host.export.png"`). Dense int ids are
minted per run at registration and never leave the process. Keymap files and WAL lines store the
NAME; recordings store `name_hash` per record plus a hash→name definition record emitted once per
command (§1.5), so the stream stays self-describing without repeating strings at dispatch rate.
Registration asserts duplicate names; `NameHash` (`ImHashStr`) is a lookup
accelerator only — every hash hit confirms with `strcmp`, so a hash collision degrades to a slow
path, never a wrong verb. Declared rows carry only the `Name` string; hashes are minted at
registration into the app-owned registry row and its `ImGuiStorage` map. Built-in editor rows
register at editor init from function-local init data; the file-scope table is retired. No global
state. The existing editor int `Id` + dispatch switch stay untouched;
`AppCommandFindByName()` is the one bridge.

- **Alternative rejected:** int ids with reserved ranges (library 0..N, app N+1..). A treaty, not a
  guarantee — the id-7-collision class survives, and saved keymaps/recordings break across versions.
- **Cost to change later:** highest of any decision here — three serialized surfaces (F74 doc keymap,
  WAL lines, meta stream) would need a second migration. Decide once.

### 1.2 API shape: register-by-use is the on-ramp; declared tables remain the palette-rich path

```cpp
if (ImGui::AppCommand("edit.paste"))            // Registers on first call, then pull-queries
    Paste();
ImGui::AppCommandSetup("edit.paste", &desc);    // Optional: chord/icon/label/surfaces (POD desc)
```

Bool return = G1 fired-this-frame; `Setup` follows the `TableSetupColumn` declarative grammar
(callable every frame, idempotent); the desc is POD.

First call inserts a registry row (label derived from the dotted name, defaults
`Surfaces = Palette|Shortcut`, no chord) and stamps `LastSeenFrame` for ImGui-style liveness; stale
rows grey out in the palette. `ImGuiAppCommandDesc` is field-identical to `ImGuiAppEditorCommand`, so
graduating from register-by-use to a declared table is mechanical data motion. The sanctioned path
must be the cheapest line in the file — cheaper than a raw `IsKeyChordPressed`, or users bypass the
system and ship non-rebindable keys.

- **Alternative rejected:** declared-table only (mirror `s_editor_commands`). Three declarations per
  verb is exactly the chore that makes users bypass the registry.
- **Cost to change later:** low — call sites lift into table rows mechanically.

### 1.3 Dispatch: one per-frame arbiter, resolved id into TempData, pull-side consumption

A single arbiter runs once per frame at input-record time: gathers chord matches over the merged
binding table, applies the text-input veto (`WantTextInput`), longest-mods wins, at most one dispatch
per id per tick, availability re-checked at execute time (a lying palette becomes a logged
diagnostic, not a heisenbug). The winning id is written into framework TempData like any other
recorded input; `AppCommand()` is a clear-on-read pull in the consumer's own update — input never
calls user code (house pull-seam rule; same one-frame contract as `AppGraphConsumeHostCommand`). A
chord pressed before a verb's first registration is dropped (one-frame warmup).

- **Alternative rejected:** immediate same-frame dispatch per surface. Reintroduces
  commands-firing-twice (menu click + shortcut same frame) and unattributable dispatches.
- **Cost to change later:** moderate — latency semantics are observable by apps and by recordings.

### 1.4 ImGuiAppInputMapping: three binding layers, last-writer-wins by name

Layer 0: registry defaults (each command's declared chord). Layer 1: app-shipped preset packs
(one-handed, sticky-mods, vim-chords) as data assets. Layer 2: the user file, serialized as sorted
one-binding-per-line text — `edit.paste=Ctrl+Shift+V`, empty right side = explicit unbind —
merge-friendly by construction. The user file is a sidecar, not an `ImGuiSettingsHandler` section —
register-by-use registers verbs after the host .ini loads; a late handler never sees its sections
(I26). Same-layer chord conflict inside one surface context = lint assert;
cross-layer = legal shadowing (shadowed binding kept inert for the keymap UI to grey out). A binding
whose name no longer resolves is a tombstone: kept verbatim, WAL-logged once at load, revives if the
verb returns. Never silently deleted, never a crash.

- **Alternative rejected:** extend F74's single per-document override list. Conflates app-level and
  document-level concerns; F74 stays as-is and later reads through this resolver.
- **Cost to change later:** low-moderate — the merged-view resolver is one function; the file format
  is the sticky part.

### 1.5 Determinism spine: the resolved command is the recorded unit

Two new meta record types: `ImGuiAppAVMetaRecordType_Command` `(tick, seq, name_hash, surface,
operand)` and `ImGuiAppAVMetaRecordType_CommandNameDef` `(name_hash, name)`, the latter emitted
lazily before a command's first Command record — register-by-use means names appear after stream
start, and lazy defs keep a crash-truncated stream self-describing for every command it actually
used. Hash collisions between defs are detectable at load, not silently resolved.
The mapper (chords, arbitration, veto) lives OUTSIDE the deterministic core; the core consumes only
command events. During playback the live mapper is hard-disabled and dispatch is fed from the stream
— a command can never fire "extra" in replay because the mapper is not consulted. Recordings become
keymap-independent: rebind keys, old recordings still replay the right verbs. Boundary rule: **verbs
are commands, gestures are raw** — mouse drags, text typing, scroll stay `InputFrame` records.
Operands are a tagged POD union `{none, i64, f64, name_hash, selection-ref}` — no raw pointers, no
screen coordinates — enforced at record time, because one "convenient" `ImVec2` captured outside the
recorded stream silently breaks replay-vs-live hash equality (the load-bearing risk of this design).
WAL line becomes `"execute command 0x%08X:%s via %s"` (house hex register, I21/F24); the avtool WAL
marker parser (`imguiapp.cpp:26803`, decimal `strtol` after `"execute command "`) updates in the
same change or every recording loses its command markers.

- **Alternative rejected:** record raw chords and re-arbitrate during replay. Any keymap change
  between record and replay then replays the wrong verbs — the exact class the WAL exists to catch.
- **Cost to change later:** high — meta-stream format version bump; do not ship v1 recordings
  without settling this.

### 1.6 Placement: public seam in imguiapp.h, machinery in imguiapp_internal.h

`ImGuiAppCommandDesc`, `AppCommand()`, `AppCommandSetup()`, `ImGuiAppInputMapping` load/save entry
points go in `imguiapp.h` (this is the out-of-the-box surface). The registry row, arbiter state,
layer resolver, and lint live in `imguiapp_internal.h` beside the F34/F74 types they generalize;
definitions land in the matching `[SECTION]` regions of the unity `imguiapp.cpp` (Δ7). Registry
rows and arbiter state are owned by the app object — no file-scope mutable statics (I17; globals
banned, Δ3/Δ6). Debug surfaces ship in the house introspection register: `DebugNodeAppCommands` /
`DebugNodeAppInputMapping` beside the existing `DebugNode*` block (`imguiapp_internal.h:1525`),
guarded by `IMGUI_DISABLE_DEBUG_TOOLS` (I22). The editor registry migrates onto the new core
additively: Name/NameHash fields first, dispatch switch untouched.

## 2. Known unknowns, defaults, pivot signals

- **Dear ImGui shortcut routing.** RESOLVED against vendored 1.92.9 WIP: `Shortcut()` + routing
  (RouteFocused/Global/Always, key ownership) exists but is call-site/focus-scoped, while this
  arbiter is central and must hard-disable during replay — a poor fit. Decision: own arbiter with
  the `WantTextInput` veto (matches editor handlers, `imguiapp.cpp:14401`/`15448`); exact-mods
  chord matching makes longest-mods-wins inherent. Residual gap: the arbiter ignores ImGui key
  ownership, so popup Escape claims or InputText-internal Ctrl+A/C/V/X can double-fire against a
  bound verb. Pivot signal: first observed double-fire of that class — add a `TestKeyOwner`
  (`imgui_internal.h`) check beside the veto rather than adopting routing wholesale.
- **Host-command seam fold-in.** Default: `AppGraphSetHostCommands` stays; a later adapter registers
  host commands into the app registry under `host.*`. Pivot signal: if the adapter is under ~50
  lines, fold immediately and deprecate the parallel path.
- **Editor registry migration depth.** Default: additive only (names on rows, four-roads test
  extended); the run() switch and int ids stay. Pivot signal: if the chord-conflict lint requires the
  effective-keymap resolver anyway, migrate the editor's keyboard wall onto the arbiter in the same
  change.
- **Operand payloads.** Default: v1 commands are verb-only (`operand = none`). The union exists in
  the record framing from day one; first parameterized command fills it in.
- **Input contexts.** Default: v1 context = `Surfaces` bitmask + text-input veto. A pushable context
  stack (per-context four-roads) is designed-for (context mask column reserved) but not built.
- **Beyond keyboards.** Bindings reserve a `SourceKind` discriminator (`{Name, SourceKind, Code,
  Mods}`) so gamepad/MIDI arrive as arbiter-only additions; no call-site edits. Not built in v1.
  Combo/leader-key DFA sequences: same posture — table shape leaves room, v1 ships without.

## 3. Mechanical work (compressed; trust assumed)

1. **Identity (additive, zero behavior change):** `Name` on `ImGuiAppEditorCommand` (string only,
   no stored hash); dotted names across the rows; four-roads legs — name non-empty, matches
   `^[a-z0-9]+(\.[a-z0-9]+)+$`, Source-prefix correct (`host.*` etc.), duplicate-free by
   on-the-fly hash + strcmp.
2. **Registry + register-by-use:** app-object-owned `ImVector<row>` + `ImGuiStorage` hash map (I17);
   `AppCommand()` register/stamp/query; built-in editor rows registered at init and the file-scope
   `s_editor_commands` retired (§5 ruling); `DebugNodeAppCommands` liveness introspection; palette
   renders union of declared + auto rows.
3. **Arbiter:** merged-binding resolution, veto, longest-mods, once-per-id, TempData pending slot,
   WAL `via %s` attribution (+ avtool marker-parser co-update, §1.5).
4. **InputMapping:** layer resolver, sorted-line sidecar IO via `ImAppFormatChord`/`ImAppParseChord`
   third-tier helpers (N11 imguiapp tier, `ImFormat*`/`ImParse*` family stems), tombstones,
   chord-conflict lint, preset pack loader.
5. **Determinism:** Command + CommandNameDef meta record types + framing round-trip in
   `tests/imguiapp_flow_tests.cpp` (framing proven before the mapper consumes it, including a
   truncated-stream case — defs precede first use); replay feed path; meta-stream version bump;
   replay conformance test (dispatch fires with the verb's call-site window toggled off — pins
   record-the-id-not-the-chord).
6. **Diagnostics:** why-not ring buffer (fixed POD ring: chord, candidate, rejection enum) +
   `DebugNodeAppInputMapping` Input Doctor panel behind `IMGUI_DISABLE_DEBUG_TOOLS` (I22); never
   serialized.
7. **Later (designed-for, unbuilt):** rebind-capture popup, preset packs as shipped assets, verb-level
   recording diff in `imguix_avtool`, `.commands` macro text format, shadow-mapper conformance probe.

## 4. Rejected wholesale (trap list, from the divergence pass)

- Commands as reflection-interpreted `{field-path, op, operand}` rows as THE model — cannot express
  imperative verbs (Save, Generate); may return later as an optional command class.
- Dual-latency "reflex" bindings (in-frame + one-frame paths) — two dispatch timings = a replay
  divergence class.
- Per-control responder-chain dispatch — implicit ordering; kills the single-table lint story.
- Grace-window ("coyote time") availability — availability time-travel in tool apps.
- Auto-GC of dead bindings on save — silent data loss; tombstones strictly better.

## 5. Review outcome

All review questions resolved, positions as written: dotted string names are the only serialized
identity (recordings: hash-per-record + CommandNameDef, §1.5); register-by-use `AppCommand()` ships
in v1; the determinism spine ships in v1 — deferring it would force replay to re-arbitrate chords,
the exact class §1.5 rejects; both type names stay (`ImGuiAppCommandMapping` has process lifetime,
`ImGuiAppInputMapping` has file lifetime, and F74 later reads through the latter alone).

## 6. Implementation checklist (v1, sequenced; one code change per box)

Each box is one small, buildable change; order within a phase matters, phases follow §3. Test boxes
land in the same change as the code they pin unless marked otherwise.

### 6.1 Identity (additive, zero behavior change)

- [ ] Add `Name` field to `ImGuiAppEditorCommand` (`imguiapp_internal.h:720`), default `""` — no stored hash; hashes are minted at registration on the app side, declared data is never written.
- [ ] Put dotted names on every editor command row (`imguiapp.cpp:17827`; table retires in 6.2).
- [ ] Assign `host.*` fallback names to host rows inside `AppGraphSetHostCommands` registration.
- [ ] Add `AppCommandFindByName()` (hash probe, `strcmp` confirm) beside the existing id lookup.
- [ ] Extend `step72_command_registry_four_roads` (`imguiapp_nodes_tests.cpp:5557`): name non-empty leg.
- [ ] Same test: name matches `^[a-z0-9]+(\.[a-z0-9]+)+$` leg.
- [ ] Same test: Source-prefix leg (`host.*` rows have `Source == Host`, editor rows don't).
- [ ] Same test: duplicate-free leg (hash on the fly + strcmp over the whole registry).

### 6.2 Registry + register-by-use

- [ ] Declare `ImGuiAppCommandDesc` POD in `imguiapp.h`, field-identical to `ImGuiAppEditorCommand`.
- [ ] Add registry storage on the app object: `ImVector<row>` + `ImGuiStorage` hash→index map (I17: no file-scope mutable statics; internal header).
- [ ] Implement registration insert with duplicate-name assert (G18: `IM_ASSERT(expr && "Readable hint?")`).
- [ ] Implement `AppCommand(name)`: register-on-first-call, stamp `LastSeenFrame`, clear-on-read pull.
- [ ] Implement `AppCommandSetup(name, &desc)` upsert of chord/icon/label/surfaces.
- [ ] Derive default label from the dotted name (`edit.paste` → "Edit: Paste").
- [ ] Palette renders the union of declared + auto rows.
- [ ] Palette greys rows whose `LastSeenFrame` is stale.
- [ ] Register built-in editor rows into the app registry at editor init, from function-local init data consumed once (no file-scope statics of any kind — §5 ruling).
- [ ] Retire `s_editor_commands` (`imguiapp.cpp:17827`): re-point the editor lookup helpers (`:17880`, `:17885`, `:17917`) and the palette/keyboard walls at app registry rows; delete the table + the `imguiapp_internal.h:715` comment reference.
- [ ] `DebugNodeAppCommands(...)` liveness introspection (name, hash, last-seen, source) beside the existing `DebugNode*` block (`imguiapp_internal.h:1525`), guarded `IMGUI_DISABLE_DEBUG_TOOLS`.
- [ ] Test: `AppCommand` twice same frame = one row, one dispatch slot.

### 6.3 Arbiter

- [ ] Add pending-command slot (resolved id + surface) to framework TempData.
- [ ] Implement per-frame chord scan over the merged binding view at input-record time.
- [ ] Apply `WantTextInput` veto (match existing handlers, `imguiapp.cpp:14401`/`15448`).
- [ ] Exact-mods chord matching (longest-mods-wins falls out; assert no same-tick double match).
- [ ] Enforce at most one dispatch per id per tick.
- [ ] Re-check availability at execute; log lying-palette diagnostic to WAL instead of dispatching.
- [ ] Change WAL line to `"execute command 0x%08X:%s via %s"` with surface attribution (I21 hex register).
- [ ] Update the avtool WAL marker parser in the same change (`imguiapp.cpp:26803`: decimal `strtol` → hex hash + name).
- [ ] Test: chord pressed before first registration drops silently (one-frame warmup).

### 6.4 ImGuiAppInputMapping

- [ ] Add `ImAppFormatChord`/`ImAppParseChord` third-tier helpers (`"Ctrl+Shift+V"` ↔ key+mods; N11 imguiapp tier, context-free), round-trip tested.
- [ ] Declare `ImGuiAppInputMapping` with layer 0/1/2 tables (`{Name, SourceKind, Code, Mods}` rows).
- [ ] Implement the merged-view resolver (one function, last-writer-wins by name).
- [ ] Save: sorted one-binding-per-line sidecar text (not a settings-handler section, §1.4/I26), empty right side = explicit unbind.
- [ ] Load: parse lines, unresolvable names become tombstones kept verbatim, WAL-logged once.
- [ ] Tombstone revival when the verb registers later.
- [ ] Same-layer same-surface chord conflict = lint assert.
- [ ] Cross-layer shadowing kept inert and queryable (for the keymap UI grey-out).
- [ ] Preset pack loader: layer-1 table from a data asset via the same line parser.
- [ ] Test: save→load→save byte-identical (sorted, tombstones preserved).

### 6.5 Determinism spine

- [ ] Append `ImGuiAppAVMetaRecordType_Command` + `_CommandNameDef` AFTER `_AudioPcm` (`imguiapp_internal.h:161`; serialized values, never renumber).
- [ ] Bump the Identity schema so old readers refuse new streams.
- [ ] Define operand tagged POD union `{none, i64, f64, name_hash, selection_ref}` with record-time tag assert.
- [ ] Writer: emit `CommandNameDef (name_hash, name)` lazily before a hash's first Command record.
- [ ] Writer: emit `Command (tick, seq, name_hash, surface, operand)` when the arbiter fills the pending slot.
- [ ] Count both record types in the stream-stats switch (`imguiapp.cpp:26260` region).
- [ ] Framing round-trip test in `tests/imguiapp_flow_tests.cpp` BEFORE the mapper consumes records.
- [ ] Truncated-stream test: every Command record in a cut stream has a preceding NameDef.
- [ ] Replay: hard-disable the live arbiter when a stream is driving.
- [ ] Replay: feed Command records into the pending slot by tick (hash→name via the def table).
- [ ] Replay: unknown `name_hash` (verb gone) = logged skip, never a crash.
- [ ] Conformance test: rebind between record and replay, same verbs fire (pins record-the-id-not-the-chord).
- [ ] Conformance test: dispatch fires with the verb's call-site window toggled off.

### 6.6 Diagnostics

- [ ] Why-not ring buffer: fixed POD ring `(chord, candidate id, rejection enum)`, never serialized.
- [ ] Arbiter writes a ring entry on every rejected candidate (veto, availability, shadowed, warmup).
- [ ] `DebugNodeAppInputMapping(...)` Input Doctor panel rendering the ring newest-first, guarded `IMGUI_DISABLE_DEBUG_TOOLS` (I22, empty stub in the `#else`).
