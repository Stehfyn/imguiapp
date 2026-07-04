# Metrics/Debugger Coherence Upgrade — One Truth for State, Surfaced Diagnostics, Synced Views

## 1. Summary & goals

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

## 2. Current incoherences (grounded)

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

## 3. The design

### 3.0 Shared primitives (the spine)

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

### Theme A — One Status Truth: the status strip

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

### Theme B — Hidden-Diagnostics Surfacing

**Merges:** all link-reject-toast variants, topo-cycle-banner, all codegen-staleness variants,
codegen-warnings-count. **Targets:** #1, #2, #3, #11, #20, #21, #22.

#### B1 — Link-rejection reason (the #1 missing diagnostic)

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

#### B2 — Cycle node name in the strip

The HEALTH segment (Theme A) already renders `topo_err` verbatim from the once-computed, hoisted topo
result. That delivers the #2 diagnostic with no extra surface. **Decision:** ship the named warning;
do **not** add a "Select offending node" button derived from `topo_order` — on a cycle `AppGraphTopoOrder`
clears `out_control_ids`, so a "first control not in topo order" scan returns the wrong node and
contradicts the banner. Click-to-select is deferred until `AppGraphTopoOrder` exposes the cycle-member id
it already knows internally (the `done[i]==false` set, nodes.cpp:1167-1198), at which point it routes
through the Theme C selection channel.

#### B3 — Codegen staleness (fresh | STALE)

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

#### B4 — Dropped-binding & hosted-control warnings (codegen honesty + warnings count)

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

### Theme C — Cross-View Selection Sync

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

### Theme D — Design/Live Legibility

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

### Theme E — Non-Destructive Mirror

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

## 4. Phased rollout

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

## 5. Explicitly rejected ideas

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
