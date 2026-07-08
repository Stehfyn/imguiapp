# Zero-Value Behavior — the "0 triggers an action" bug class

Reference page. Read this before adding any field (or enum) that selects *which* action a control
performs — especially a TempData field. Sibling of [phase-coherence.md](phase-coherence.md): that page
is about reading a value from the wrong *phase*; this one is about the DEFAULT (zero) value silently
carrying *meaning*. The Composer regressed on this in 2026-07 (post-mortem in §3).

## 1. The bug class, generally

The smell: **a field whose zero value triggers non-default behavior** — an enum whose 0th entry is a
real action, or an index where 0 selects a real item while a negative sentinel (`-1`) means "none."

Zero is the universal resting state of memory. Value-initialization (`T{}`), aggregate `= {}`,
`memset`, and a bare `int` member with no initializer all land on 0. So any code that reads a field
as "perform action N" and treats `N == 0` as a valid action performs that action *every time the field
was never explicitly written*. The default state of the program is not inert — it does something.

```
if (action >= 0) do(action);   // WRONG: action==0 is armed by every zero-init
if (action >  0) do(action-1); // OK: 0 is inert; real actions are positive (stored 1-based)
```

**Rule: 0 means nothing — no action, the default, the no-op. Real actions start at a positive value.**
Store an action index 1-based (0 = none, action *k* = index *k-1*). Reserve an action enum's 0th entry
for `None`/`Default` and give it no side effect. Never pair "0 is a valid action" with a "-1 = none"
sentinel: the sentinel only protects the code paths that remember to set it, and it leaves the resting
state (0) armed.

## 2. Why immediate-mode TempData makes this bite

TempData is a control's per-frame INPUT: OnRender records it, OnUpdate consumes it. The framework
value-initializes it — the instance is `IM_NEW()()`-constructed, and the OnRender wrapper does
`_InstanceData->TempData = {}` at the top of every frame (`imguiapp.h`). OnUpdate consumes *last*
frame's OnRender output ("OnUpdate consumes the TempData recorded by last frame's OnRender",
`imguiapp.cpp`). Two consequences put a zero-valued TempData in front of OnUpdate:

- **First frame.** OnUpdate runs before any OnRender has written TempData (the per-frame order is
  OnUpdate then OnRender), so it reads an all-zero struct.
- **Any frame the writer skipped.** If the OnRender path that sets the field did not run (panel
  closed, early return), the `= {}` reset leaves the field at 0 for the next OnUpdate.

A TempData field whose 0 value is a real action therefore fires on the first frame and on every
skipped-writer frame — exactly the frames the author was not thinking about. Setting the sentinel in
OnRender cannot fix the first frame; setting it in OnInitialize would patch only that one frame and
leave skipped-writer frames armed. The robust fix is structural: make 0 mean none.

## 3. Post-mortem: the Composer stamped Producer/Consumer into every clean graph (2026-07)

The clean default graph is foundation-only — `AppGraphEnsureFoundation` seeds the five core layers and
nothing authored; `SeedAppGraph` adds only that plus the starter prefab *library*
(`AppGraphSeedStarterPrefabs` builds Producer/Consumer in a throwaway graph and registers it as a
prefab, never as a node). Yet launching the Composer showed a Producer and a Consumer node by default,
and every "producer/consumer" test looked like it depended on those nodes pre-existing.

Cause: `EditorBodyTempData::StampPrefab` was a **0-based** prefab index with `-1 = none`. OnUpdate
stamped whenever `StampPrefab >= 0`; the seeded library's index 0 is the "Producer/Consumer" prefab.
The `= -1` guard lived in OnRender, which cannot run before the first frame's OnUpdate. So frame 1:
TempData zero-init → `StampPrefab == 0` → `0 >= 0` → `AppGraphInstantiatePrefab(graph, 0, …)` stamped
the Producer/Consumer pair into the otherwise-clean graph. The flow tests never caught it because they
exercise the seed primitives (`AppGraphEnsureFoundation` + `AppGraphSeedStarterPrefabs`) directly, not
the control's first-frame OnUpdate — so the checked-in `composer_self.txt` was foundation-only while
the running Composer was not.

Fix: make `StampPrefab` **1-based** — `0 = none`, stamp index `k-1` when `> 0`. The Stamp button writes
`i + 1`. A zero-init frame now stamps nothing, so the `-1` sentinel and its OnRender guard are deleted.
The default graph is inert by construction, not by a guard that has to remember to run.

## 4. Checklist

- A TempData or enum field that selects an action: **0 = none/default; actions are positive.** Store
  indices 1-based.
- Reserve the 0th enum entry for `None`/`Default`; never give it a side effect.
- Do not rely on an OnRender-set sentinel (or an OnInitialize poke) to keep a zero-init field inert —
  OnRender does not run before the first OnUpdate, and `= {}` re-zeros the field on any skipped-writer
  frame.
- Audit tell: a `>= 0` or `!= 0` guard on a TempData action selector. Each is a candidate for this bug;
  prefer `> 0` with a 1-based value.
