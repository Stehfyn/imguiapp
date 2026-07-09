# Composer Studio Design — gen 3: Author · Observe · Replay, one instrument

Third-generation UI/UX program. Gen 1 ([composer-ui-design.md](archive/composer-ui-design.md))
made six panels one instrument; gen 2 ([composer-workbench-design.md](archive/composer-workbench-design.md))
made the instrument a workbench. Gen 3 extends the workbench's grammar over the **runtime tools** —
Previewer, DLL preview, Playback debugger, Recorder, App-time transport — and makes the running
preview itself an editing surface (WYSIWYG). Thesis: **the Composer, Previewer and Debugger are not
three tools; they are three postures of one studio** — Author (edit the model), Observe (watch the
model run, edit it in place), Replay (interrogate a recorded run). Every posture shares one design
language, one command vocabulary, one transport, one health grammar.

Scope framing: gens 1–2 delivered a **proof of concept** — the surfaces exist and the ideas are
proven, but small positioning/sizing defects remain and several features stop at the skeleton.
Gen 3 is the production build-out: every surface below is specified to *finished*, not to
minimal-diff; the coherence audit (§8.0) is fixed first so no known bug class survives into — or
re-enters through — the new work.

Acceptance pillars (every proposal names the ones it serves, or is cut):
**consistency** (grammar conformance is 100%: zero bespoke constants), **communicability** (every
state change echoes in a named zone; every verb teaches its chord), **accessibility** (WCAG 2.2
mapped to imgui reality), **economy of motion** (flows budgeted in KLM operators and fixation
regions, not vibes).

## 1. Method

### 1.1 Anchors — kept and added

Kept from gens 1–2: Cognitive Dimensions[^cdn], coordinated multiple views / brushing[^cmv], calm
technology[^calm], Shneiderman's mantra, Norman's gulfs, Fitts, Hick, Gestalt. Reference editors
and their disciplines unchanged (VS tool-window grammar, UE viewport primacy + compile-as-button,
Unity component inspector + play tint, Blender regions + status keymap) — now with local media
mirrors under [media/](media/README.md) (`grabs/unreal/umg-*`, `grabs/unity/uibuilder-*` are the
WYSIWYG references; `grabs/unity/lifecycle-flowchart.png` remains the north star).

Added for gen 3:

- **KLM** (Card, Moran & Newell)[^klm] — flows are budgeted in operators: K (keystroke, ~0.2 s),
  P (point, ~1.1 s), H (hand switch, ~0.4 s), M (mental prepare, ~1.35 s). A redesign that does
  not reduce the operator string of a top loop is chrome, not UX (§6).
- **Proximity compatibility** (Wickens & Carswell)[^pcp] — information used together lives
  together; each loop step should complete inside one fixation region. Eye travel is a budget,
  same as keystrokes.
- **WCAG 2.2**[^wcag] with **APCA** contrast targets[^apca] — our palette is alpha-ladder-heavy
  (α0.4 pills, α0x66 tags), so ratio math on *effective* composited colors, minimum lightness
  contrast Lc 60 for body text rungs, Lc 45 for large/secondary, 3:1 for non-text essentials.
- **Okabe–Ito color-universal palette**[^cud] — semantic hue anchors chosen to survive the three
  common CVD types; color never carries a meaning alone (Bertin[^bertin]: every encoding names its
  channel; every semantic gets ≥ 2 channels).

### 1.2 Acceptance tests (operational, not aspirational)

| Pillar | Test |
|---|---|
| Consistency | zero `ImVec4(`/`IM_COL32(` chrome literals outside the style tables (ratchet, §3.6); one widget idiom per role (§3.3) |
| Communicability | every editor state change lands in a fixed zone (§4.4); status hint line renders again (§5.1); every chord visible in ≥ 3 surfaces |
| Accessibility | §7 checklist green: APCA rungs, double-encoding, 24 px targets, keyboard completeness |
| Economy | §6 KLM budgets met; each loop's eye path visits ≤ 3 fixation regions, monotonic left→right or in-place |

## 2. Ground truth — what the studio is today

Inventories taken 2026-07-09 (all `file:line` verified). Landed and assumed: flow-ordered toolbar
with phase captions + health-carrying Generate (demo.cpp:1185–1644) · outliner with kind filters,
brushing, drag-reparent, DESIGN/LIVE bands (nodes.cpp:15846–16059) · hand-rolled canvas with native
cursor-anchored zoom 0.3–2.5 (canvas.cpp:526–535) · scope drill + per-branch camera memory ·
component-section inspector with live echo + quick inspector (nodes.cpp:8302–8532, 7790–7832) ·
command registry, 40+ verbs, palette, rebindable keymap dispatch (nodes.cpp:10478–10682) · named
undo (nodes.cpp:14461) · layout sidecar + presets (demo.cpp:401–542) · theme-derived color engine
(nodes.cpp:249–367) · motion table 0.55/1.0/0.20, 150 ms (imguiapp_internal.h:800–813) ·
interpreter previewer with brushed surface (preview.cpp) · DLL preview with compile/hot-swap loop
(preview_dll.cpp) · FILE playback with marked timeline + state-at-tick (demo.cpp:702–1156) ·
meta recorder with UI Record/Stop (demo.cpp:2817–2833) · one `ImGuiAppComposerTransport` object
with LIVE/FILE source switch (demo.cpp:344–362).

### Divergences (the consistency debt gen 3 retires)

- **D1 two scrub idioms** — FILE playback owns the marked draw-list rail (`ComposerPlaybackTimeline`,
  demo.cpp:931–984); App-time LIVE is a bare `SliderInt` + step buttons (demo.cpp:1477–1492). Same
  concept, two widget stacks.
- **D2 altitude breach** — playback is a floating window (demo.cpp:1055); every other surface is a
  docked panel under the panel contract.
- **D3 two pill grammars** — `ComposerStatusPill` (SmallButton, demo.cpp:1650–1683) vs the `AppBl*`
  draw-list family (nodes.cpp:454, 812).
- **D4 two icon-button idioms** — real buttons (toolbar) vs `AppTreeRowIcon` manual hit-test
  (outliner rows *and* gizmo column, nodes.cpp:15503, 7725).
- **D5 literal chrome colors** — ~20 `ImVec4` constants in demo.cpp outside the theme engine
  (Generate states demo.cpp:1363–1365, inspector/console greens/ambers/reds repeated at
  demo.cpp:2116–2972, canvas theme block behind a `GridSpacing != 26.0f` sentinel demo.cpp:2409–2422).
- **D6 dead teacher** — the per-hover-target status hint is composed every frame
  (nodes.cpp:7647–7677) and exported (`AppGraphStatusHint`, nodes.cpp:4509) but **never rendered**.
  Gen 1 T2's keymap teacher silently fell off the strip.
- **D7 unreachable rebind UI** — `AppGraphShowKeymapEditor` (nodes.cpp:10741) has no call site;
  F74 dispatch is live, F75's editor is dark.
- **D8 bodies without an editor** — `MethodBody[4][2048]` storage, codegen, serialize, import all
  exist (imguiapp_internal.h:389–398; nodes.cpp:1613–1849, 13379, 13827); no UI writes it.
- **D9 value preservation twice** — interpreter reconciles by (name,type) (preview.cpp:1135–1167);
  DLL reload preserves by label (preview_dll.cpp:370–401).
- **D10 duplicated plumbing** — RGBA→texture upload twice (demo.cpp:851–890 vs 895–926); run
  identity gate re-implemented inline in UI (demo.cpp:1014–1016) beside the core check
  (av.cpp:1880–1886).
- **D11 two defaults for one width** — outliner 220 px (demo.cpp:1815) vs em·16 (demo.cpp:2450).
- **D12 zoom without a readout** — zoom is first-class state; no indicator exists (gen 1's
  exclusion was for imnodes; the reason died with imnodes).

### Accessibility gaps (measured, §7 fixes)

- **A1** canvas objects are draw-list + manual hit-test: keyboard Nav cannot reach nodes, pins,
  wires, or `AppBl*` widgets — the graph is mouse-only despite `NavEnableKeyboard`.
- **A2** severity is color-only on canvas (title dot nodes.cpp:7642, row underline :15706); amber
  vs red is a classic CVD confusion pair.
- **A3** origin legend uses one glyph, three colors (demo.cpp:2465–2494).
- **A4** targets: frame-height buttons ≈ 23 px at default font; pin hit radius 8 model units
  shrinks below 8 px screen under zoom-out.
- **A5** thin contrast rungs: empty filter pills at text α×0.4 (nodes.cpp:830), kind tag at α0x66
  (canvas.cpp:1078), `TextMuted(0.60)` over `FieldBg(0.30)`.

## 3. Design language v3 — the grammar is data

The theme-derivation engine (`AppThemeNeutral/Accent/Dark`, nodes.cpp:249–277) is the single color
source; gen 3 closes over it. Nothing below invents machinery — it finishes and enforces what
exists.

### 3.1 Color: CUD-anchored semantic hues, literals retired

- Every D5 literal moves into `ImGuiAppComposerStyle` / `ImGuiAppChromeTheme` rows. New rows:
  `HealthOk / HealthStale / HealthBlocked` (the Generate triple), `RunTint` (frozen/replay wash),
  `RecordArmed`. The canvas theme block loses its sentinel and becomes desc rows.
- The `APP_HUE_*` table (nodes.cpp:280–302) re-anchors on Okabe–Ito coordinates[^cud], preserving
  the current role→feel mapping and the 15%-toward-Text pull: blue #56B4E9 (Task/data), orange
  #E69F00 (Command/containment/Control), bluish-green #009E73 (Status/Field/tie), reddish-purple
  #CC79A7 (Op), purple-blend (Struct/Display keeps its violet — nearest CUD-safe blend of blue+
  vermillion tested at the three CVD transforms), vermillion #D55E00 (error), amber #F0E442-shifted
  (warn — must separate from error in deutan simulation; verified pair, not eyeballed), sky/steel
  (live). Exit test: pairwise ΔE of the kind set under protan/deutan/tritan simulation ≥ threshold;
  pinned as a table test, not a promise.
- **Double-encoding rule (Bertin)**: hue never alone. Kind = hue + silhouette (exists,
  nodes.cpp:5876–5937) + word tag (exists). Severity = hue + **glyph**: warn ▲, error ⬢ — same two
  glyphs in canvas dot, outliner underline (becomes a leading glyph), problems list, status pill
  (fixes A2). Origin = hue + glyph: design ✎, live 👁, promoted ✓ (fixes A3). Pins already
  shape-coded (circle data / square containment) — pinned as grammar.

### 3.2 Type & space (held, now enforced)

Type ladder 1.00/0.90/0.80 and `SpaceQuantum` 0.25 em (imguiapp_internal.h:807–812) held from
gen 2. Canvas metrics (GridSpacing 24, PinRadius 4…) are **model units** scaled by zoom — correct,
not a violation. New: the ladder and quantum get a ratchet test; an off-ladder size is a defect.

### 3.3 One widget idiom per role (kills D3, D4)

The `AppBl*` draw-list family is the chrome idiom (it already owns fields, toggles, filters,
kebabs, disclosure). Status pills re-render through it; `ComposerStatusPill` dies. `AppTreeRowIcon`
is promoted from "outliner + gizmo hack" to the named icon-button of the family
(`AppBlIconButton`), one hit-test helper, one hover ladder, one place carrying the
`AllowWhenBlockedByActiveItem` rule. Interaction ladder (rest/hover/active/selected/disabled/mixed)
already consistent per family (§4 of the a11y inventory) — becomes a table test over style lookups.

### 3.4 Motion (held)

One table, one 150 ms linear fade, nothing else moves (imguiapp_internal.h:800–813,
nodes.cpp:7695–7711). Gen 3 additions reuse it: run-tint fade-in, record-armed pulse is **not** a
pulse — it is a static red dot (calm tech: recording is a state, not an alarm).

### 3.5 Targets

Interactive min hit = **1.5 em square** (24 px at default font — WCAG 2.5.8[^wcag]); frame-height
buttons pad their hit rect to it (visual size unchanged). Pin hit radius gets a **screen-space
floor of 8 px** regardless of zoom (hit only; visuals still scale). Fixes A4.

### 3.6 Enforcement

- **Literal ratchet**: chrome sections of demo.cpp/nodes.cpp carry zero color literals; grep-based
  test in the style ratchet suite (same mechanism as the existing style/indent ratchets).
- **Grammar dump test**: style table serialized and diffed — a new color/size names its row or the
  test fails. The project inspector's Theme section (demo.cpp:2101–2214) already edits the tables
  live; the grammar stays *data*, dogfooded by the machinery it teaches.

### 3.7 Temporal grammar — designing in phase

The visual grammar (§3.1–3.5) is the *spatial* half of the design language. The *temporal* half is
phase coherence ([bug-classes.md](bug-classes.md) §Phase Coherence) — and gen 3 promotes it from a
review checklist applied after the fact to a **design input stated before the first line lands**.
The framework's thesis (derive in update, draw in render, one-frame skew explicit) is a design
constraint of the same rank as the type ladder; a feature that cannot state its phase story is not
designed yet, whatever its mockup looks like.

**The law: update derives and updates the model; render represents it.** One sentence carries the
whole temporal grammar — *derivation-first* means exactly this pairing: update first **derives**
the facts (geometry, layout, events) and then **updates** the model against them; it is the sole
writer, and "the model" includes every geometry fact anything else depends on. Render is a pure
*representation* of that model under this frame's camera: it may compute freely in service of
representing (that is what makes it a projection, not a copy), but nothing it computes may
outlive its draw call.

```
              │◀───────────────────────────  frame T  ───────────────────────────▶│
              │                                                                   │
              │  UPDATE — derive + update MODEL    RENDER — represents the MODEL  │
              │  ┌──────────────────────────┐     ┌───────────────────────────┐   │
  intents ────┼─▶│ apply intents against    │     │ draw model(T)             │   │
  (T-1)       │  │ complete facts           │     │   × transform(T)          │   │
  Temp ───────┼─▶│ derive geometry, layout, │────▶│ repr-local calc OK — pure,│   │
  (T-1)       │  │ events (Temp ⊕ LastTemp) │model│ dies with its draw call   │   │
              │  │ from model + style +     │ (T) │ record intent + Temp      │───┼─▶ intents(T)
              │  │ CalcTextSize (pure query)│     │ [exception: read-back,    │   │   Temp(T)
              │  │ — the SOLE writer        │     │  laundered to model units]│───┼─▶ facts(T)
              │  └──────────────────────────┘     └───────────────────────────┘   │   (exception only)
              │                                                                   │
              └── crossings: intents, Temp, and (exception only) laundered facts ─┘

  the illegal arrows are all one arrow — render output leaking into the model story:
  ✗ §1   pixels measured(T-1) ──combined with──▶ transform(T)              one-frame flash
  ✗ §1b  apply(v) ──▶ measure ──▶ v′ ≠ v, no named fixed point             drift / "animation"
  ✗ §1c  RENDER ──▶ model write mid-publication (between Canvas Begin/End)  stale obstacle set
```

**Derivation-first, and where the line actually sits.** `CalcTextSize` is a pure function — it is
legal in *any* phase. The phase question is never "who may measure text"; it is **who consumes the
result**:

- **If the value participates in the model's story** — a plate size other nodes lay out against,
  a group/containment rect, an obstacle set, a stored hit target, a row height a band reserves,
  anything another frame or another consumer reads — it is model state, so **update derives it**:
  from the model's own content (rows, labels, pins), the style tables (type ladder, paddings,
  quanta), and pure queries. Derived in update(T), represented in render(T): same frame, nothing
  crosses, no settle, and species §1/§1b become unrepresentable because no measured pixel exists.
- **If the value serves only this representation** — centering a label in an already-sized plate,
  ellipsizing, clip tests — render computes it freely. Legality test: *delete the draw call and
  the computation must have no remaining effect.* The moment a render-computed value steers logic
  downstream, it has silently become model state living in the wrong phase — which is precisely
  what the audit confirmed in the field (F2: a render-derived em fed row layout, composed wrong;
  F1: a render-pass rect fed the drag obstacle set from the wrong altitude).
- **Read-back is the marked exception, not the norm** — reserved for content whose layout only
  imgui knows (arbitrary host-submitted widget stacks: preview surfaces, embedded editors). The
  exception's contract: laundered to model units at capture with that frame's own transform,
  every loop through it deadbanded (§1b), and the call site commented with *why derivation is
  impossible here*. During the derivation-first migration the read-back doubles as a verification
  harness: derived size ≍ measured size, asserted under the zoom acid test.
- The Temp/LastTemp input skew is the same law applied to input: render *records* raw samples
  (representation-side fact capture), update *derives* events from them. Nothing new to learn —
  edge detection was always derivation-first.

**Six patterns that make violations unrepresentable** (prefer a pattern over a guard — a guard
must remember to run; a structure cannot forget):

1. **Derive, then update** — update derives the sizes and layout the model story needs (from
   model + style + pure queries) and mutates the model against them; render receives, never
   decides.
2. **Representation-locality** — render-computed values die with their draw call; anything that
   would survive moves to update.
3. **Edit-intent bus** — OnDraw records "the user is dragging X"; OnUpdate (or post-`CanvasEnd`)
   applies it against this frame's complete facts.
4. **One producer per fact** — two consumers recomputing a value from raw state can disagree for a
   frame; compute once in update, publish, consume (this is what data dependencies are *for*).
5. **Model units at capture** — for the read-back exception: divide out every frame-varying
   transform with *that frame's own* values at capture; consumers re-apply the current transform.
6. **Inert zero** — every TempData/action field's zero value means *none*; real actions are
   positive. First-frame and skipped-writer frames are then inert by construction.

**The phase contract.** Every proposed feature that touches measured geometry, cached values, or
model writes ships a five-line contract in its design note (slice reviews reject its absence):

| Line | Question it answers |
|---|---|
| **Facts** | what the model story needs, **derived in update** from model + style + pure queries — and for anything measured instead, why derivation is impossible there |
| **Derived** | what is computed from the facts, and in which phase (update / marked read-back exception — never mid-render, never render-local values escaping) |
| **Writes** | which phase mutates the model? render-phase gestures record *intent only*, applied post-submission (§1c) |
| **Loops** | every measure→apply→measure cycle, with its **named fixed point** (exact invariant units, or a deadband) (§1b) |
| **Crossings** | every value that crosses a frame boundary, with its invariant-units proof — deliberate T+1 settling is fine *and commented as such* |

**Five patterns that make violations unrepresentable** (prefer a pattern over a guard — a guard
must remember to run; a structure cannot forget):

1. **Model units at capture** — divide out every frame-varying transform with *that frame's own*
   values; consumers re-apply the current transform (the canvas engine's core invariant).
2. **Edit-intent bus** — OnDraw records "the user is dragging X"; OnUpdate (or post-`CanvasEnd`)
   applies it against this frame's complete facts. One frame of settle in model units is
   imperceptible; a mid-publication write is a flash.
3. **One producer per fact** — two panels recomputing a value from raw state can disagree for a
   frame; compute once, publish, consume (this is what data dependencies are *for*).
4. **Same-frame read-back** — measure at submission with the same transform used to place, store
   immediately in model units; the stale-decoration class becomes unrepresentable.
5. **Inert zero** — every TempData/action field's zero value means *none*; real actions are
   positive. First-frame and skipped-writer frames are then inert by construction.

**Gen-3 features, pre-contracted** (the contract applied at design time — what it looks like):

| Feature | Phase story |
|---|---|
| Composer node plates (§5.1) | sizes **derived in update** from the row model (fields/pins/labels × type ladder × paddings); zoom applied only at draw; the engine measurement path becomes a verification assert (derived ≍ measured under the zoom acid test) |
| Transport rail (§4.2) | notch positions derive from tick indices (model units) each frame; scrub is an update-phase state restore; nothing caches rail pixels |
| WYSIWYG overlay, interpreter (§5.2) | overlay rects read from THIS frame's surface manifest post-submission; every gesture is an intent applied through the normal mutation path in update — the gesture grammar *is* pattern 2 |
| WYSIWYG overlay, DLL (§5.2) | rect report and rasterized frame cross the ABI as a **coherent pair stamped with the same tick**; the overlay draws in the image's own space onto that image — panel-side pan/zoom of a newer frame can never meet an older rect |
| Lifecycle lanes (§5.1) | lane assignments/geometry derive from model order in update; bands and the spine draw from this frame's lane table; slot drag is an intent into the F58–60 order write |
| Quick inspector anchor (§5.4 gen 2 / §5.2) | anchored to this frame's widget/node rect, re-read each frame — never a cached screen position |
| Zoom pill (§5.1) | reads engine zoom the frame it draws; no state |
| Uniform widths / any ratchet | keeps the existing model-unit deadband; the fixed point is named in the code comment |

**Exit discipline per slice**: the zoom acid test (rapid wheel over every decoration, zero
single-frame artifacts) runs on every surface a slice touches; every `screen-pos/dimensions` call
site the slice adds is classified by draw phase in review (read, don't sample — the audit method
is the review method).

## 4. The studio frame — one shell, three postures

### 4.1 Postures = layout presets + transport source

Existing presets Compose/Review/Observe (demo.cpp:1587–1606) become **Author / Observe / Replay**
(Review folds into Author — the code panel is part of authoring; the source map is its tie):

| Posture | Subject | Panels | Transport source |
|---|---|---|---|
| **Author** | canvas (lifecycle lanes at root) | tree + inspector + code/output | LIVE (idle) |
| **Observe** | preview (interpreter or DLL) | preview subject-sized + inspector + output | LIVE ring |
| **Replay** | recorded run | transport rail + frame view + state-at-tick inspector | FILE run |

A posture is panel states + transport source — nothing else. Switching posture never loses work
(panel contract persists per posture). One keystroke cycle: `F6` (VS window-cycling family), also
palette verbs.

### 4.2 One transport component (kills D1)

`ComposerPlaybackTimeline`'s marked rail generalizes to **`AppBlTransportRail`**: notches = state
snapshots (gold), commands (green), input ticks (faint), divergence (error hue + ⬢), cursor;
step-back/forward, play/freeze, source badge. Three renderings, one widget:

- **Toolbar compact** (App-time LIVE): the rail replaces the bare SliderInt; same notch grammar at
  1-row height.
- **Replay full** (FILE): today's timeline, now the same code path.
- **Preview armed** (recording): rail grows live at the right edge; red dot + tick count.

Scrubbing anywhere shows the same corner badge (`edit −3 · run −47f`, gen 1 T5 — now honest across
tools). Run-state tint (gen 2 §4.4) applies in **every** posture when time ≠ now: viewport wash
2–3% toward `RunTint` + accent line under the toolbar.

### 4.3 Replay docks (kills D2)

The playback window becomes the **Replay** bottom-panel tab (panel contract, `RevealPanel`
reachable). "Open run…" moves to the Project tab's file list (runs listed beside graph/header
files — they are document artifacts) and to the palette. The identity/integrity line renders from
the core gate's result struct — the inline re-check dies (D10).

### 4.4 Fixed echo zones (communicability contract)

Every state change lands in exactly one of: **primary-action health** (Generate button),
**problems badge**, **status transient zone**, **status facts zones**, **panel tab badge**
(Output err/warn counts), **canvas ambient marks**, **transport rail marks**. New features name
their zone in review; a floating one-off is a defect. (VS status-bar zone discipline[^vs]; the
zones are already anchored at fixed em offsets, demo.cpp:1837–1840 — the *rule* is what's new.)

## 5. Tool designs

### 5.1 Composer (Author posture)

- **Status hint line revived** (D6): one line inside the canvas bottom edge rendering
  `AppGraphStatusHint` — hover-target keymap teaching (`node: LMB select · drag move · Tab enter ·
  N inspect`), link-refusal override in error hue for 2.5 s (machinery all exists; one call site
  was missing). This is the cheapest communicability win in the program.
- **Zoom pill** (D12): `78%` beside the minimap corner; click = fit-all, right-click = 100%.
  Reads from `CanvasGetZoom`; honest now that zoom exists.
- **Keymap editor reachable** (D7): project inspector gains a **Shortcuts** section hosting
  `AppGraphShowKeymapEditor`; palette verb "Rebind shortcuts…". Rebinds echo everywhere chords
  render (registry-driven — already true once visible).
- **Lifecycle lanes at root** (north star, unchanged from the gen-3 end-goal mock
  [media/mock/composer-gen3-endgoal.html](media/mock/composer-gen3-endgoal.html)): phase bands as
  lanes with contract captions, frame spine, loop-back edge, one-frame-skew arrow, grey internal
  rows, slot drag = order write (F58–60 path). Lane view / free view = overlays-popover toggle.
- **Four-roads closure**: Align/Distribute join the selection context menu's `Shift+A` submenu
  (registered chord), Collapse/Expand-all join the view menu; registry surface flags updated so
  step72 pins the closure.

### 5.2 Previewer — the WYSIWYG subject (Observe posture)

Preview graduates from bottom-tab to the Observe posture's **subject panel** (right column, sized
like the UMG/UI Builder canvas — see `media/grabs/unreal/umg-designer-full.png`,
`media/grabs/unity/uibuilder-full-labeled.png`; end state mocked in
[media/mock/previewer-wysiwyg-endgoal.html](media/mock/previewer-wysiwyg-endgoal.html)). Both
backends render in-panel (interpreter widgets; DLL rasterized frame — existing paths
demo.cpp:2849–2900).

**Two modes, one toggle, mode-you-must-see** (header segmented control + cursor + frame):

- **▶ Interact** (today's behavior): input drives the running app; TempData records; the app is
  live. Neutral frame.
- **✎ Edit** (new): input drives a design overlay; the app still runs beneath but receives no
  input. Accent frame (`RunTint` idiom) + pencil cursor. `E` toggles; `Esc` exits.

**Edit-mode gesture grammar** — every gesture writes through the *same* model mutation + named
undo path as canvas edits (one writer; `AppUndoDeriveLabel` names them; nothing bypasses history):

| Gesture on preview widget | Model write | Undo label | Existing path |
|---|---|---|---|
| click | select node (brush → select promotion) | — | AppPreviewTakeClickedNode, preview.cpp:1340 |
| drag vertically within host | sequence order nudge | "Reorder X" | F58–60 order write |
| drag onto another window/sidebar | reparent | "Move X" | tree Act=5 path, nodes.cpp:16005 |
| double-click text | rename label | "Rename A→B" | inline-rename path |
| drag Region/Split divider | split fraction param | "Edit Split" | Layout node params (F53–57) |
| right-click | node context menu (canvas menu, four roads) | — | AppTreeContextMenu grammar |
| `N` | quick inspector pinned **beside the widget** | — | nodes.cpp:7790 |
| context verb "Capture as default" | live value → Persist default | "Edit X" | field default write |
| drag from Add palette onto a host | scoped add into hovered scope | "Add X" | scoped-add, step19 |

Deliberately *not* WYSIWYG: authored window rectangles (the model does not author free window
geometry today — runtime owns it; joins the horizon with the Region vocabulary), structural
deletes (Del stays a canvas/tree verb — destructive actions keep their confirmation surfaces).

**Backend honesty:**

- *Interpreter*: edits interpret next frame (status line already says so, demo.cpp:2836). Overlay
  hit-testing uses the surface manifest (F68) — instance→widget rects are known.
- *DLL*: overlay geometry crosses the C-ABI as a per-tick **rect report** (instance id, rect) —
  bytes-only, same marshalling discipline, `IMGUIAPP_PREVIEW_ABI` bump (precedented). Structural
  edits mark the signature dirty → existing recompile loop; while compiling, the pane wears the
  stale wash + "compiling…" pill (fixed echo zone), Edit gestures queue against the model (the
  model is the truth; the DLL is a view that catches up). Value preservation unifies on the
  interpreter's (name,type) rule (D9) — label-keyed CopyIn adopts it.

**Method-body editor lands here** (D8): selected Control/Task in Observe shows a **Body** section
(per-method monospace `InputTextMultiline`, 2048 cap with counter, imguiapp_internal.h:136) with a
compile-status chip (ok / compiling / error-with-log-line from `PreviewDllErr`) and "runs in
preview" affordance. Palette verbs: "Edit OnUpdate body" etc. The DLL loop is the body's REPL.

### 5.3 Playback debugger (Replay posture)

Layout inside the Replay tab: full-width `AppBlTransportRail`; below it, **frame view** (decoded
pixels — existing texture path) beside **state-at-tick** rendered with the *inspector's component
sections* in live-echo grammar (Data(tick)/Temp(tick) tables) instead of bespoke text — the
inspector is the one way state is read everywhere (consistency; hard mental operations down).
Divergence: rail mark (⬢ + error hue) + a Problems row ("state hash diverges at tick N — open
Replay") — click scrubs to the tick (fixed echo zones). WAL command chips on the rail (exists)
gain click → Output filtered to that tick. Identity mismatch renders the core gate verdict with
the severity glyph grammar.

### 5.4 Recorder

One **Record** affordance in the transport cluster (not a preview-panel special): split-button
`⏺ Record ▾` → "Meta only" (default) / "With video (mp4/QOI)". Armed = static red dot + tick
count in the transport rail's right edge; recorder lifecycle lines land in Output (they already
WAL). On stop: status transient zone confirms (`wrote preview-session.meta`), the artifact appears
in the Project tab file list with an **Open in Replay** action (it already reopens via
`AppRunOpen`, demo.cpp:778–792 — the handoff becomes visible instead of implicit).

## 6. Flow economy — KLM budgets and eye paths

Operator costs: K 0.2 s · P 1.1 s · H 0.4 s · M 1.35 s[^klm]. Budgets for the six loops that
dominate a session (current strings from the inventories; designed strings from §5):

| Loop | Today | Gen 3 | Mechanism |
|---|---|---|---|
| L1 add node in scope | Space, type, Enter (M K… K) — already tight | unchanged | palette is the floor (Hick: kind-filtered) |
| L2 edit field of selected | P(cross-screen to inspector) M P K… | `N` K, edit in place | quick inspector at cursor; in Observe, field IS the widget (WYSIWYG) |
| L3 reorder a control | drill scope, find chip strip, drag (M P M P) | drag the widget itself in preview (P) | §5.2 gesture grammar — the artifact is the handle |
| L4 why is Generate red? | click problems, read, click row, fix | eye stays: Generate carries state, hover previews the worst issue, click = reveal+select (1 P) | primary-action-carries-health + hover preview (Norman evaluation gulf) |
| L5 tweak style live + persist | inspector hunt, toggle Active, regenerate | select in preview (P), quick inspector Style (K), edit; live write-through, Generate when done | live style seam (gen 2 §3.5) + WYSIWYG selection |
| L6 record → find divergence | record, stop, open file dialog, open playback, scrub hunting | ⏺ (P K), stop, "Open in Replay" (K), rail already marks divergence — click it (P) | §5.4 handoff + §5.3 marks |

Eye-path rules (PCP[^pcp]): the toolbar's compose→iterate→persist→produce order is the *only*
horizontal sweep; everything else edits **at the point of attention** (quick inspector, WYSIWYG,
context menus — Fitts) or echoes at a **fixed zone** the eye already knows (§4.4). Keyboard
completeness: every verb reachable Space→type→Enter; chords displayed at every surface that renders
the verb (registry-driven); hover teaches via the revived status hint (recognition→recall).

## 7. Accessibility program (WCAG 2.2 → imgui reality)

| SC | Rule | Where |
|---|---|---|
| 1.4.1 Use of color | every semantic ≥ 2 channels: severity glyphs ▲/⬢, origin glyphs ✎/👁/✓, kind hue+silhouette+tag, pins shape-coded | §3.1 (fixes A2, A3) |
| 1.4.3 / APCA | text rungs: body ≥ Lc 60, secondary/muted ≥ Lc 45 on their *actual* composited bg; empty-pill α0.4 and kind-tag α0x66 re-derived to meet Lc 45 | §3.1 (fixes A5); pinned by a contrast table test computed from the style tables at default theme |
| 1.4.11 Non-text | pins/wires/notches ≥ 3:1 vs grid; pins keep `DarkOutline` | grammar rows |
| 2.1.1 Keyboard | palette = complete verb access (exists); **new: canvas focus walk** — arrows move a focus cursor node-to-node (nearest-neighbor in model space), Enter selects, context-menu key opens the node menu; focus ring = dashed accent, distinct from gold selection | fixes A1 for the graph; `AppBl*` widgets already keyboard-editable once focused (SetKeyboardFocusHere paths exist) |
| 2.4.7 Focus visible | dashed ring ≠ selection outline; never color-only | §3.1 |
| 2.5.8 Target size | 1.5 em hit clamp; pin 8 px screen floor | §3.5 (fixes A4) |
| 1.4.4 Resize text | already em-derived everywhere; canvas model units scale by zoom; pinned by the existing em-audit ratchet | held |
| 2.3 / motion | one 150 ms fade, nothing blinks (record dot static) | §3.4 |

Out of scope honestly: screen-reader output (dear imgui has no accessibility tree; the palette +
keyboard walk + status hint line are the accessible layer; revisit when upstream grows one).
Ratchet: the §7 table becomes a test group — contrast rows computed, target sizes asserted, focus
walk exercised by a GUI step (the 113-step suite has zero a11y steps today; that number stops
being zero in ST6).

## 8. Delivery slices

### 8.0 The coherence floor (fix first — the audited POC defects)

Two full bug-class audits (2026-07-09; phase-coherence species + all other classes, read-don't-
sample over the five tool files) confirmed four defects and cleared the rest of the surface
(intent-defer, deadbands, seating, adoption roads, altitude verbs, eviction round-trips, residency,
zero-value defaults, dead-chrome flags all verified clean). The floor is fixed before any slice
lands, so gen-3 work builds on coherent ground:

| # | Defect | Fix locus |
|---|---|---|
| F1 | `AppGroupAccumulate` unsubmitted-member fallback reads root `GridPos` while drilled — group frames balloon toward root coords on drill-in and publish a wrong-altitude obstacle rect (both audits, independently) | nodes.cpp:8961 mirrors the scope-aware fallback its three siblings use (nodes.cpp:6777, 6802, 5507) |
| F2 | Scope order strip + portal chips size text by `GetFontSize() × AppCanvasScale` = FontRatio applied twice — chips render FontRatio× oversized under any DPI/user font scale, overflowing the reserved band row | nodes.cpp:9963, 10103 — `× AppCanvasZoom`, not `× AppCanvasScale` |
| F3 | Outliner drag-reparent discards `AppGraphReparent`'s bool — illegal kind pairs (Control→Struct, Field→Window/Sidebar) complete the gesture silently | nodes.cpp:16046 rides the refusal channel (the link-refused idiom, nodes.cpp:4032–4038); drop gate kind-filters so illegal targets never highlight |
| F4 | `_TrunkRoutes` cache keyed by owner id has no erase in `AppGraphRemoveNode` — memory-only growth | sweep joins the removal road (nodes.cpp:2850–2902) |

Watch item (plausible, not confirmed): DLL preview paused + panel resize rasterizes last-ticked
draw data into the new size for a frame (demo.cpp:2855–2874) — decide frozen-view semantics when
ST4 touches that path.

**Guardrail carried by every slice below**: new features ship the §3.7 phase contract; reviews
classify every added screen-pos/dimensions call site by phase and every geometry verb by altitude;
the audit smell table is the review grep list. That is how "fully fleshed out" stays compatible
with "no known bug class re-enters."

Task-level tracker with source identifiers:
[composer-studio-checklist.md](composer-studio-checklist.md).

### 8.1 The slices

| Slice | Contents | Exit test |
|---|---|---|
| **ST0 — Coherence floor** | F1–F4 above | drill-in acid test (enter/leave scopes under zoom, zero single-frame group artifacts); FontRatio ≠ 1 chrome test; refused-reparent notice step; trunk-route sweep step |
| **ST1 — Grammar closure** | D5 literal sweep into tables; D3/D4 idiom unification; zoom pill; status hint revival; keymap editor reachable; D11 one default; **derive-and-update pass** on Composer-owned geometry (node plates, band rows, chip metrics — F2's whole class), engine read-back demoted to verification assert | literal ratchet green; step: hint text present; step: rebind via UI; derived ≍ measured assert green under zoom acid |
| **ST2 — One transport** | `AppBlTransportRail`; toolbar scrub replaced; Replay tab (D2); identity gate single-source (D10); Project-tab run listing | step: LIVE and FILE scrub through one widget; playback window gone |
| **ST3 — Observe + WYSIWYG (interpreter)** | preview as subject; Interact/Edit modes; gesture grammar (reorder/reparent/rename/split/capture-default/palette-drop); quick-inspector-at-widget | steps: each gesture row round-trips with named undo |
| **ST4 — DLL parity + bodies** | rect-report ABI; overlay on DLL; compiling wash; (name,type) preservation (D9); Body section editor + compile chip (D8) | F78.5 body edited in-app, runs in preview |
| **ST5 — Replay depth** | state-at-tick via inspector sections; divergence marks + Problems row; recorder split-button + artifact handoff | step: record→stop→Open in Replay→click divergence mark |
| **ST6 — Accessibility** | CUD hue re-anchor + CVD pair test; severity/origin glyphs; contrast re-derivation + table test; target clamps; canvas focus walk | §7 test group green |

ST1 first and alone — it is pure debt retirement, touches every surface lightly, and every later
slice inherits its one-idiom world. ST3 is the identity slice (the tool's output becomes the
tool's input surface); ST6 last only because its test harness wants the final grammar tables.

## 9. Deliberately excluded (decided, not forgotten)

- **WYSIWYG window geometry** — the model doesn't author free window rects; faking it via style
  mods would lie. Joins the Region/Layout vocabulary horizon.
- **WYSIWYG structural delete** — destructive verbs keep canvas/tree surfaces and their guards.
- **Screen-reader tree** — upstream limitation, named in §7.
- **Docking framework / free panel layout** — unchanged verdict; postures cover the real
  configurations.
- **A third preview backend, video-in-preview, marching-ants wire animation** — no pillar served.
- **Second status/notification channel** — the §4.4 zone set is closed; a toast system would
  reopen it.

## References

Gen 1/2 footnotes remain in their documents; new for gen 3:

[^klm]: Card, S. K., Moran, T. P., Newell, A., ["The keystroke-level model for user performance time with interactive systems"](https://dl.acm.org/doi/10.1145/358886.358895), *CACM* 23(7), 1980 — operator budgeting (§1.1, §6).
[^pcp]: Wickens, C. D., Carswell, C. M., ["The proximity compatibility principle: its psychological foundation and relevance to display design"](https://doi.org/10.1518/001872095779064560), *Human Factors* 37(3), 1995 — fixation-region budgeting (§1.1, §6).
[^wcag]: [WCAG 2.2, W3C Recommendation](https://www.w3.org/TR/WCAG22/) — SC 1.4.1, 1.4.3, 1.4.11, 2.1.1, 2.4.7, 2.5.8 (§7).
[^apca]: [APCA — Accessible Perceptual Contrast Algorithm](https://git.apcacontrast.com/) — Lc targets for alpha-composited rungs (§3.1, §7).
[^cud]: Okabe, M., Ito, K., ["Color Universal Design"](https://jfly.uni-koeln.de/color/), 2008 — the CVD-safe hue anchors (§3.1).
[^bertin]: Bertin, J., *Semiology of Graphics*, 1967/1983 — visual variables; the ≥ 2-channels rule (§3.1).
[^cdn]: Green & Petre 1996 — the dimensions vocabulary (all sections).
[^cmv]: Becker & Cleveland 1987; Roberts 2007 — brushing/linking (§5.2 selection ties).
[^calm]: Weiser & Brown 1995 — quiet-until-relevant (§3.4, §5.4).
[^vs]: [Visual Studio status-bar / tool-window grammar](https://learn.microsoft.com/en-us/visualstudio/ide/customizing-window-layouts-in-visual-studio?view=vs-2022) — zone discipline (§4.4).

Media mirrors: [media/README.md](media/README.md) — `grabs/unreal/umg-*` and
`grabs/unity/uibuilder-full-labeled.png` (WYSIWYG discipline), `grabs/unreal/blueprint-compile-*`
(health-as-button), `grabs/unity/lifecycle-flowchart.png` (north star),
`grabs/blender/status-bar.png` (keymap hint line), `mock/composer-gen3-endgoal.html` (Author
posture end state).
