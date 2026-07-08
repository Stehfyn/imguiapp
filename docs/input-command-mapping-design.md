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
  `s_editor_commands[]` (`imguiapp.cpp:17827`), Id-keyed dispatch switch, `Surfaces` bitmask, an
  availability predicate, and the four-roads completeness test iterating the registry.
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
minted per run at registration and never leave the process. Keymap files, WAL lines, and recordings
store the NAME. Registration asserts duplicate names; `NameHash` (`ImHashStr`) is a lookup
accelerator only — every hash hit confirms with `strcmp`, so a hash collision degrades to a slow
path, never a wrong verb. The existing editor int `Id` + dispatch switch stay untouched;
`AppCommandFindByName()` is the one bridge.

- **Alternative rejected:** int ids with reserved ranges (library 0..N, app N+1..). A treaty, not a
  guarantee — the id-7-collision class survives, and saved keymaps/recordings break across versions.
- **Cost to change later:** highest of any decision here — three serialized surfaces (F74 doc keymap,
  WAL lines, meta stream) would need a second migration. Decide once.

### 1.2 API shape: register-by-use is the on-ramp; declared tables remain the palette-rich path

```cpp
if (ImGui::AppCommand("edit.paste"))            // registers on first call, then pull-queries
    Paste();
ImGui::AppCommandSetup("edit.paste", &desc);    // optional: chord/icon/label/surfaces (POD desc)
```

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
merge-friendly by construction. Same-layer chord conflict inside one surface context = lint assert;
cross-layer = legal shadowing (shadowed binding kept inert for the keymap UI to grey out). A binding
whose name no longer resolves is a tombstone: kept verbatim, WAL-logged once at load, revives if the
verb returns. Never silently deleted, never a crash.

- **Alternative rejected:** extend F74's single per-document override list. Conflates app-level and
  document-level concerns; F74 stays as-is and later reads through this resolver.
- **Cost to change later:** low-moderate — the merged-view resolver is one function; the file format
  is the sticky part.

### 1.5 Determinism spine: the resolved command is the recorded unit

New meta record type `ImGuiAppAVMetaRecordType_Command`: `(tick, seq, name_hash, surface, operand)`.
The mapper (chords, arbitration, veto) lives OUTSIDE the deterministic core; the core consumes only
command events. During playback the live mapper is hard-disabled and dispatch is fed from the stream
— a command can never fire "extra" in replay because the mapper is not consulted. Recordings become
keymap-independent: rebind keys, old recordings still replay the right verbs. Boundary rule: **verbs
are commands, gestures are raw** — mouse drags, text typing, scroll stay `InputFrame` records.
Operands are a tagged POD union `{none, i64, f64, name_hash, selection-ref}` — no raw pointers, no
screen coordinates — enforced at record time, because one "convenient" `ImVec2` captured outside the
recorded stream silently breaks replay-vs-live hash equality (the load-bearing risk of this design).
WAL line becomes `"execute command %08X:%s via %s"`.

- **Alternative rejected:** record raw chords and re-arbitrate during replay. Any keymap change
  between record and replay then replays the wrong verbs — the exact class the WAL exists to catch.
- **Cost to change later:** high — meta-stream format version bump; do not ship v1 recordings
  without settling this.

### 1.6 Placement: public seam in imguiapp.h, machinery in imguiapp_internal.h

`ImGuiAppCommandDesc`, `AppCommand()`, `AppCommandSetup()`, `ImGuiAppInputMapping` load/save entry
points go in `imguiapp.h` (this is the out-of-the-box surface). The registry row, arbiter state,
layer resolver, and lint live in `imguiapp_internal.h` beside the F34/F74 types they generalize. The
editor registry migrates onto the new core additively: Name/NameHash fields first, dispatch switch
untouched.

## 2. Known unknowns, defaults, pivot signals

- **Dear ImGui shortcut routing.** ImGui has `Shortcut()`/routing ownership that already solves the
  text-input capture fight. Default: own arbiter with the `WantTextInput` veto (matches the existing
  editor handlers). Pivot signal: if reading `imgui.h`/`imgui_internal.h` routing (verify against the
  vendored version — never assume upstream API from memory) shows owner-scoped routing covers the
  veto + focus-scope cases, route chord matching through it instead of hand-rolling.
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

1. **Identity (additive, zero behavior change):** `Name`/`NameHash` on `ImGuiAppEditorCommand`;
   dotted names across `s_editor_commands`; four-roads legs — name non-empty, matches
   `^[a-z0-9]+(\.[a-z0-9]+)+$`, Source-prefix correct (`host.*` etc.), duplicate-free by
   NameHash+strcmp.
2. **Registry + register-by-use:** per-app `ImVector<row>` + `ImGuiStorage` hash map; `AppCommand()`
   register/stamp/query; liveness debug window; palette renders union of declared + auto rows.
3. **Arbiter:** merged-binding resolution, veto, longest-mods, once-per-id, TempData pending slot,
   WAL `via %s` attribution.
4. **InputMapping:** layer resolver, sorted-line file IO, tombstones, chord-conflict lint, preset
   pack loader.
5. **Determinism:** command meta record type + framing round-trip in `tests/imguiapp_flow_tests.cpp`
   (framing proven before the mapper consumes it); replay feed path; meta-stream version bump;
   replay conformance test (dispatch fires with the verb's call-site window toggled off — pins
   record-the-id-not-the-chord).
6. **Diagnostics:** why-not ring buffer (fixed POD ring: chord, candidate, rejection enum) + Input
   Doctor panel; never serialized.
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

## 5. Review request

1. **Identity (§1.1):** dotted string names as the only serialized identity — yes/no? (Most
   expensive to reverse.)
2. **API (§1.2):** register-by-use `AppCommand()` in v1, or declared-table only with register-by-use
   later?
3. **Spine (§1.5):** determinism integration in v1 (meta record type from day one), or ship
   registry+mapping first and bump the stream format in a second release?
4. **Naming:** `ImGuiAppCommandMapping` = registry+dispatch, `ImGuiAppInputMapping` = binding layers
   — keep both names, or fold to one type with two tables?
