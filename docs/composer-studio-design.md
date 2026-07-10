# Composer Studio Design — gen 4: the tool explains itself

Fourth-generation UI/UX program, and a **reset**, not an increment. Gen 1 made six panels one
instrument; gen 2 made the instrument a workbench; gen 3
([archive/composer-studio-design-gen3.md](archive/composer-studio-design-gen3.md)) specified the
workbench to *finished* — and was rejected on contact with the only test that matters:

> **A first-time user, looking at the running product, cannot say what it is, what they are looking
> at, or what to do first.**

Gen 3 failed because it took the gen-1/2 shell as final and specified completion and polish inside
it. The shell itself communicates the wrong thing. Gen 4 redesigns the shell top-to-bottom against
the reference editors ([media/README.md](media/README.md)) whose entire job is teaching strangers:
Unity (Scene/Game split, Play mode, UI Builder), Unreal (viewport primacy, UMG Designer/Graph
tabs, compile-as-button), Blender (named regions, status-bar keymap). Everything built in gens 1–3
survives as **plumbing** — the grammar tables, the transport rail, the WYSIWYG gesture engine, the
phase-coherence discipline. What changes is *what the user is looking at and what the words say*.

## 1. Diagnosis — four structural failures (why polish cannot fix this)

**X1 — The subject is inverted.** The center of the screen — the biggest, first-read region — is
the *machinery*: a node graph of the frame lifecycle. The thing the user is making — their app —
is a small panel in a corner. Every reference editor does the opposite: Unreal's viewport IS the
level; Unity UI Builder's viewport IS the menu being built, huge, centered, looking exactly like
the shipped thing; the machinery (hierarchy, palette, inspector) stands around it in labeled side
panels. A stranger reads our screen as "some graph tool", not "my app, being built".

**X2 — The language is internal.** Task · Command · Status · Window, Persist, Temp, `TempData = {}`,
push order, `pop 7..1`, "one-frame skew", LIVE-MIRROR, scope, drill. These are the *implementation's*
words, presented as primary labels. Zero words on screen say "you are building an app; drag a
button in to start." The ontology is the product's genius and its worst first impression — it must
be *discovered*, never *fronted*.

**X3 — There is no entry point.** The product opens cold onto a loaded graph. No statement of
purpose, no template chooser, no guided first task, no empty-state teaching. Our own reference
library had to *label the panels with numbers* to explain the reference editors
(`uibuilder-full-labeled.png`, `level-editor-full-labeled.png`) — the product itself offers less
than our screenshots of other people's products.

**X4 — Everything renders at once.** Lifecycle lanes, live mirror, transport, codegen health,
replay, problems — all visible simultaneously at first run, all at equal weight. No layering from
novice surface to expert depth. (Unity hides Play-mode machinery until ▶; Blueprint hides the graph
behind the Designer tab until you ask for logic.)

## 2. Design laws (gen 4 constitution — every proposal cites one or is cut)

1. **Show the app, explain the machine.** The user's artifact is the subject of the center region
   at all times. Framework concepts appear as *explanations attached to things the user did*,
   never as the opening scene.
2. **Every region names itself.** A panel whose purpose is not written on it is a defect — the
   product must out-label our reference screenshots.
3. **The next action is always written on screen.** Empty states carry a verb; the status line
   teaches the hovered thing; the primary button says what it will do.
4. **Editing and testing are different postures, chosen knowingly.** Unity's Play discipline: one
   obvious ▶, unmistakable mode chrome while testing, one key back.
5. **Progressive disclosure, four rungs.** L0 *place* (arrange controls) → L1 *behave* (event
   sentences) → L2 *wire* (dataflow graph) → L3 *code & time* (bodies, generated source, replay).
   Each rung reachable only through a labeled affordance on the rung below; no rung leaks upward
   uninvited.

Acceptance pillars from gen 3 (consistency, accessibility, economy) are retained as instruments,
subordinated to the new first pillar: **self-evidence** (§9 cold-open test).

## 3. The frame — one shell, four views, one mode

```
┌ toolbar ──────────────────────────────────────────────────────────────────────┐
│ [+ Add ▾] [Save]      Design · Logic · Code · Replay      [▶ Play]  [Build ✓▾] [?] │
├────────────┬──────────────────────────────────────────────┬───────────────────┤
│ LIBRARY    │                                              │ INSPECTOR         │
│ what you   │              THE SUBJECT                     │ the selected      │
│ can add    │   Design → your app, WYSIWYG, full bleed     │ thing: what it    │
├────────────┤   Logic  → the node canvas (per scope)       │ is · looks ·      │
│ APP        │   Code   → generated source + map            │ state · behavior  │
│ what you   │   Replay → a recorded run on the timeline    │ · wiring          │
│ have       │                                              │                   │
├────────────┴──────────────────────────────────────────────┴───────────────────┤
│ CONSOLE — build + run messages, filterable                                     │
├────────────────────────────────────────────────────────────────────────────────┤
│ status: what your mouse/keys do here (left) · facts (right)                    │
└────────────────────────────────────────────────────────────────────────────────┘
```

- **Views** change what the center shows — the subject. Tabs top-center, always visible, one
  always lit (UMG's Designer/Graph tab pair, generalized). `F6` cycles. Side columns persist
  across views; selection is one object shared by all of them.
- **Play is a mode, not a view** — it overlays the whole shell (§5). Views answer "what am I
  looking at"; Play answers "is this thing live".
- Gen 3's "postures" (Author/Observe/Replay layout presets) die. Views + Play replace them: the
  question was never "which panel layout" but "which subject, and is it live".

Reference mapping: Design ↔ UMG Designer / UI Builder viewport / Unity Scene view · Logic ↔
Blueprint Graph / ShaderGraph · Play ↔ Unity Game view + Play mode / PIE · Code ↔ UI Builder's
UXML/USS panes · Replay ↔ ours alone (the determinism payoff — the one place we out-feature the
references, so it must look like the reward, not the homework).

## 4. The views

### 4.1 Design — the WYSIWYG editor (default view, L0)

The subject is **the app's windows, drawn as they will ship**, on a neutral pasteboard: real title
bars, real widgets at real sizes (the interpreter already renders this — ST3 landed the veiled
edit surface, gesture grammar, and named-undo roads; gen 4 makes that surface the *home screen*
instead of a bottom tab).

- Arrange: drag from Library into a window (scoped add — landed road) · drag to reorder within a
  host (landed) · "Move to ▸" between hosts (landed) · double-click rename (landed) · Del with
  confirm. Selection outline + drag handles; insertion caret between siblings.
- The app is **not receiving input** in Design view — it is veiled (ST3.3's veil is the *normal
  state* of Design view, not a special mode). A control under the cursor shows edit affordances,
  not app behavior. Wanting behavior = pressing ▶ (law 4).
- Pasteboard chrome: window plates carry their model names; a subtle "not running — press ▶ to
  interact" watermark sits in the pasteboard corner (calm, not a banner).
- Zoom/pan grammar identical to the Logic canvas (one navigation muscle across views); zoom pill
  (landed) present in both.

### 4.2 Logic — the node canvas (L2)

The existing canvas — scopes, wires, brushing — plus the **lifecycle lanes** (gen-3's north star,
unbuilt today: no band rendering exists on the canvas yet), **entered deliberately, never first**.
Entry roads: the Logic tab (reopens last scope) · "Edit logic…" button in the Inspector's
Wiring/Behavior sections (opens *that node's* scope) · double-click a Data chip anywhere.

Lane captions (the lanes land wearing these words from day one — no internal-word phase ever
ships):

| Internal (tooltip, L2 detail) | On-canvas caption |
|---|---|
| Initialization · push order | **Startup** — "runs once, top to bottom" |
| Task · sample & update — sole mutator | **1 · Update** — "your controls change their state here" |
| Command · dispatch once | **2 · Commands** — "actions collected this frame run once" |
| Status · publish | **3 · Report** — "the app posts its status" |
| Window · present — mutate nothing | **4 · Draw** — "the screen renders; nothing changes" |
| Decommissioning · reverse pop | **Shutdown** — "runs once, bottom to top" |
| loop-back edge · one-frame skew | edge labeled **"next frame"**; skew arrow labeled "input arrives next Update" |

A one-line header sentence tops the canvas: *"What `MixerApp` does every frame, top to bottom.
Wires carry data; drag nodes between bands to change when they run."* — the whole mental model in
one sentence, always visible (X2's cure at the source).

### 4.3 Code — the generated source (L3)

The existing code panel promoted to a view, with one added sentence where it counts: header reads
*"C++ generated from your design — regenerate with Build. Click code to find its node; click a
node to find its code."* (the source map exists; nobody was told). Diff stays a header mode.
Method bodies are edited in the **Inspector's Behavior section** (§6), not here — Code view is for
reading what the machine wrote.

### 4.4 Replay — the recorded run (L3)

The Replay tab (landed, ST2.3) becomes the fourth view: transport rail full-width, frame view
beside state-at-tick (inspector-grammar tables), divergence marks, WAL chips. Gen 4's addition is
the **road in**: every Play session meta-records ambiently (cheap, determinism is free — that is
the thesis), and stopping Play offers one labeled affordance: **"Review this run"** → Replay view,
rail loaded, cursor at the stop tick. Replay stops being a feature you configure and becomes the
answer to "wait, what just happened?" — the average user's actual question.

## 5. Play — the test mode (Unity discipline, our superpower added)

One **▶ Play** button, toolbar center-right, the most prominent control on screen (Unity's exact
placement logic: the one verb every session uses).

Entering Play: the subject becomes the **running app** (interpreter or DLL backend — existing
paths), input flows to it, and the mode is unmistakable (law 4): viewport wash + accent border
(`RunTint` rows, landed), **PLAYING** pill top-center with "Esc to stop", side columns dim (they
remain live — see below). ⏹/Esc returns to Design view exactly as left.

- **Time is Play chrome**: the compact transport rail (landed) docks beside ▶ only while playing
  or when a recording exists — freeze, step, scrub, ⏺ Record ▾ (meta/video split, ST5.3). At rest
  the toolbar carries no time machinery (X4: no dead chrome for novices).
- **Live edit during Play** — the honest difference from Unity: Unity discards Play-mode edits;
  our model is the truth and the running app is a view of it, so Inspector edits during Play
  write through (landed live seam). The Inspector wears a small "live — edits keep" chip during
  Play, converting an expert quirk into a stated feature.
- DLL backend honesty: structural edit during Play → signature dirty → "compiling…" wash on the
  subject (ST4.3), gestures queue against the model. The subject never lies about staleness.

## 6. The Inspector — from field dump to sentences (L0–L1)

Sections reorganized around the user's questions, not storage classes. Storage names (Persist /
Temp / OnUpdate) demote to L2 tooltips.

| Section | Contents | The words it uses |
|---|---|---|
| **This is…** | icon + "**Mixer** — a Control inside MainWindow"; rename; kind chip | plain sentence identity |
| **Looks** | style fields, live write-through (landed seam) | "Looks" |
| **State** | Persist fields → "**Saved** (kept between runs)"; Temp fields → "**Input** (read fresh every frame)" | the two words teach the storage model without naming it |
| **Behavior** | the event rows + method bodies (§6.1) | "When … → …" |
| **Wiring** | data this node reads/writes, as chips; **"Edit logic…"** → Logic view at this scope | the L2 door |

### 6.1 The Behavior sentence-builder (the killer communicability feature)

The model already authors edge events (`when <temp field> <edge> → <reaction>`) and stores method
bodies (`MethodBody`, storage + codegen + serialize all landed, D8). Gen 4 fronts them as
**sentences a person can read and build**:

```
When  [pressed ▾]  [starts ▾]   →  [send command ▾]  [Play ▾]        (✎)
When  [hovered ▾]  [changes ▾]  →  [set field ▾]     [peak = 0 ▾]    (✎)
+ When…                                        [Edit update code…]
```

Dropdowns enumerate the node's Temp fields, the edge vocabulary (starts / stops / changes —
`ImAppRising/Falling/Changed` beneath), and legal reactions. Each row round-trips to the exact
guarded block a demo author writes by hand (the generator exists). "Edit update code…" opens the
body editor (monospace, compile chip ok/compiling/error-tail, 2048 cap — ST4.5 rehomed here): the
sentence rows are L1; the body is L3, one labeled click deeper. **This is the ontology teaching
itself**: the user writes "when pressed starts → Play" and *discovers* they authored edge
detection over a double-buffered input sample — vocabulary learned by using it, not by reading it.

## 7. First contact — the cold open program

**7.1 Welcome overlay** (first run, and File → New): three cards over the dimmed empty shell —
**Blank app** · **Mixer example** (the demo graph) · **Build a counter — 10 guided steps**. Each
card: one picture, one sentence. Below them, one line of product statement: *"Compose an app from
controls, wire its data, test it live — then Build generates the C++."* Dismissable forever
(footer checkbox); reopens from `?`.

**7.2 The guided first task** is a **replay-driven tour**: the 10-step counter tutorial drives
real verbs through the real command registry (palette-visible, keys shown), each step gated on
the user's own action, teaching Library → Design drag, rename, one Behavior sentence, ▶ Play,
Build. Dogfoods the recorder; costs no parallel tutorial machinery.

**7.3 The `?` overlay**: toolbar `?` toggles the numbered-region map — exactly what our reference
screenshots do to other editors, rendered live over ours: each region outlined, numbered, one
sentence each ("Library — everything you can add. Drag into your app."). Esc closes. Zero-state
cost, permanent orientation affordance.

**7.4 Empty states carry the next verb** (law 3) — table is normative:

| Surface, empty | It says |
|---|---|
| Design pasteboard, no windows | ghost window outline + "**Drag a Window from the Library** to start your app" |
| App panel, no nodes | "Nothing here yet — add from the Library on the left" |
| Inspector, no selection | "Select anything — in your app, the App list, or the Logic canvas — to edit it here" |
| Console, no messages | "Build and run messages appear here" |
| Replay, no runs | "Press ▶ Play — every run is recorded and can be replayed here" |
| Behavior, no events | "+ When… — make this control do something" |

**7.5 The status line teaches** (Blender): left slot always narrates the hovered thing's verbs
("window: drag title to move · drag edge to resize · double-click name to rename"); the landed
canvas hint line (ST1.5) is this same channel scoped to Logic view. Right slot: facts (selection
path, zoom, build state).

## 8. The language pass — words are chrome

- **One string table** for every L0/L1 chrome string (panel names, captions, empty states, status
  verbs, welcome copy) — today's scattered literals move there (same consolidation move as the
  color tables; the table is the reviewable artifact).
- **Banned lexicon at L0/L1**: Persist, Temp, TempData, scope, drill, push, pop, mirror, WAL,
  tick, DAG, node (in Design view), posture, altitude, mutate. Each has an approved surface word
  (Saved, Input, inside, open, order, …) or appears only at L2+/tooltips.
- **Lexicon ratchet**: a test walks the string table and fails on banned words at L0/L1 rows —
  same mechanism as the chrome-literal ratchet (landed, `tests/style/chrome_literal_check.py`
  precedent). Communication regressions become CI failures, not opinions.
- Renames that stick: outliner → **App** · palette (panel) → **Library** · Generate → **Build**
  (UE compile-as-button states: ✓ built / ● needs build / ⚠ built with warnings / ✖ failed —
  `blueprint-compile-*.png` grammar, health rows landed) · Output → **Console** · "App time" →
  the Play-mode time rail.

## 9. Acceptance — self-evidence is tested, not asserted

**9.1 Cold-open ratchet (automated)**: fresh start, headless walk asserts — a product statement
is on screen · every visible panel renders its name · every empty surface renders its §7.4 verb ·
zero banned-lexicon words visible · ▶ Play and + Add present and enabled. Runs in the GUI-step
suite; regressions fail red.

**9.2 The 60-second protocol (human, per milestone)**: a first-time user (rotating victim), no
prompting, must within 60 seconds: (a) say what the product is for, (b) add a control and see it
in the app, (c) run the app and interact with it, (d) undo everything. Failures are defects with
X-numbers, triaged like crashes.

**9.3 Instruments retained from gen 3**: KLM budgets for the six loops (unchanged targets),
fixation-region rule, WCAG 2.2/APCA program (§ST6 scope intact), zoom/drill acid tests, phase
contracts (bug-classes.md) as standing implementation gates. They audit the *build*; §9.1–9.2
audit the *design*. Both run; the new one outranks.

## 10. What survives, what dies

**Survives as plumbing (landed, keeps working)**: theme/grammar tables + literal ratchet ·
`AppBl*` family + transport rail · WYSIWYG gesture grammar + veil + named undo · selection
protocol (doc+graph+engine) · posture masks (become view presets) · replay/recorder machinery ·
codegen + source map + bodies storage · command registry/palette/keymap editor · derive-and-update
discipline and every ST0–ST3 checklist item.

**Dies**: the lifecycle graph as home screen (X1) · ontology words as primary labels (X2) · the
cold open onto a loaded graph (X3) · Author/Observe/Replay postures as the top-level concept ·
"Preview" as a bottom tab (the app is the subject, not a preview of it) · toolbar phase captions
(compose/iterate/persist/produce — they named our loop, not the user's).

**Deliberately excluded (unchanged verdicts + new)**: WYSIWYG free window geometry · structural
delete from WYSIWYG without confirm · screen-reader tree (upstream) · docking framework · toast
channel · video tutorials or web onboarding (the product teaches in-product or not at all).

## 11. Delivery slices

Gen-3's unlanded remainder (ST4 DLL parity/bodies, ST5 replay depth, ST6 a11y) is rescoped inside
these; landed slices are inherited. Tracker:
[composer-studio-checklist.md](composer-studio-checklist.md).

| Slice | Delivers | Exit |
|---|---|---|
| **G0 — The frame** | view tabs Design/Logic/Code/Replay; subject inversion (WYSIWYG center, full bleed, veiled); postures→views; panels self-labeled | cold-open ratchet: names + tabs assert; F6 cycles views |
| **G1 — Library** | palette panel → Library: icon grid, plain categories (Windows · Controls · Data · Logic), drag-to-subject add via landed roads | GUI step: drag Library→Design creates scoped node with named undo |
| **G2 — Play mode** | ▶/⏹ + Esc; mode chrome; input gating flip (veil off during Play only); time rail docks in Play; ambient meta-record + "Review this run" → Replay | step: play→interact→stop→review lands in Replay at stop tick |
| **G3 — Language** | string table; rename pass (App/Library/Build/Console); lexicon ratchet; lane captions + canvas header sentence; loop-edge "next frame" | lexicon ratchet green; Logic header present |
| **G4 — Inspector** | section reorg (This is/Looks/State/Behavior/Wiring); Saved/Input framing; Behavior sentence rows; body editor + compile chip (ST4.5 rehomed); "Edit logic…" door | step: sentence row → generated guard round-trips; body edit runs in Play |
| **G5 — First contact** | welcome cards + templates; `?` region map; §7.4 empty states; status-line teacher on all views | cold-open ratchet full green; empty-state table asserted |
| **G6 — Replay & DLL depth** | ST5.1–5.4 (state-at-tick grammar, divergence marks, record split-button, artifact handoff) + ST4.1–4.4 (rect ABI, DLL overlay, compiling UX, one preservation rule) inside Play/Replay | gen-3 exit tests, rehomed |
| **G7 — Guided tour + a11y** | replay-driven counter tutorial; ST6.1–6.6 (CUD hues, glyph double-encoding, APCA rungs, targets, focus walk, a11y test group); 60-second protocol run | tour completes headless; §ST6 group green; protocol pass logged |

Order: G0 first and alone (everything else lands inside its frame). G3 before G4/G5 (words are
the material those slices build with). G2 early — Play is the payoff that makes Design view make
sense. G7 last only because the tour wants final geography.

## 12. Mocks

- [media/mock/composer-gen4-first-run.html](media/mock/composer-gen4-first-run.html) — the cold
  open: empty shell, every panel self-labeled with its empty-state verb, welcome cards over it.
- [media/mock/composer-gen4-design-view.html](media/mock/composer-gen4-design-view.html) — Design
  view editing the Mixer: WYSIWYG subject center, Library grid, App tree, sentence Inspector,
  Console, teaching status line.
- [media/mock/composer-gen4-play-mode.html](media/mock/composer-gen4-play-mode.html) — Play mode:
  running subject, mode chrome, time rail, live-edit chip, stop affordance.
- Logic-view interior: gen-3's lane mock remains representative
  ([media/mock/composer-gen3-endgoal.html](media/mock/composer-gen3-endgoal.html)) *minus* its
  shell (toolbar captions, preview-as-panel) and *plus* §4.2 relabeling.

## References

Gen-3 method anchors (KLM, PCP, WCAG/APCA, CUD, Bertin, CDN, CMV, calm tech) carry forward for
§9.3 instruments — citations in the archived doc. Gen 4's own anchors are the reference editors
themselves, mirrored locally: Unity Play mode + Scene/Game split and UI Builder
(`grabs/unity/uibuilder-full-labeled.png`), UMG Designer/Graph (`grabs/unreal/umg-designer-full.png`),
compile-as-button (`grabs/unreal/blueprint-compile-*.png`), viewport primacy
(`grabs/unreal/level-editor-full-labeled.png`), region + status-bar teaching
(`grabs/blender/regions-3dview.png`, `grabs/blender/status-bar.png`), lifecycle lanes
(`grabs/unity/lifecycle-flowchart.png`), and first-run/onboarding discipline argued from all of
them jointly (every one ships labeled-interface documentation; gen 4's `?` overlay puts that
documentation *in* the product).
