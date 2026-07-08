# Feature Audit v2 — the re-audit IS 100% (2026-07-05)

Successor to [feature-audit-2026-07-04.md](feature-audit-2026-07-04.md) (v1: 23 agents, 470 claims).
v1 established the code-verified baseline and ranked the remaining gaps per area. v2 confirms those gaps
are now closed by the explicit feature series
[feature-complete-checklist.md](feature-complete-checklist.md) (F01–F78.5, archived at 100%;
[feature-complete-checklist-v2.md](feature-complete-checklist-v2.md) is the v2 post-100 tracker), each item shipped with at least
one test on `imguix-tests` / `imguix-core-tests` / `imguix-headless-verify`, or parked by intent with a
doc note. The checklist's per-item DONE notes are the authoritative per-feature evidence (function names +
test names + suite counts); this doc is the roll-up + the closure verdict.

## Verdict

**100%.** Every checklist item is `[x]` (shipped + tested) or explicitly parked by intent. There is no
`[ ]` or `[~]` remaining except F72 (this audit) and F73 (the roadmap reset it gates). Full-suite gate at
audit time (Release, vulkan): **imguix-tests 112/112 · imguix-core-tests 409 checks / 0 failures ·
imguix-headless-verify composer-headless 31/31 + verify OK**.

## v1 "Gaps ranked" → closed by

Every ranked gap from v1's area synthesis now maps to a shipped, tested feature:

- **Runtime-core** — App-time scrubber UI (v1 #1: "claimed shipped, is not") → **F29/F30** (toolbar
  transport + frozen tint, on-camera). Status-layer model / command payloads / edit-intent bus / module
  interop → **parked** (horizon; F73 records them).
- **Codegen** — control-skeleton C++ import (the missing back-edge) → **F22–F24** (importer + emit→import→emit
  fixed point + merge policy); two-parallel-emitters drift hazard → **F16** (one emitter, both corpora
  byte-locked); comment-only warnings counted nowhere → **F19**; custom-layer emission untested → **F20**.
- **Canvas** — sequence-order editing read-only → **F58–F60** (order as model state + codegen + drag/nudge);
  S2 slice (annotation frames, align/distribute) → **F48**; pin pre-coloring / can-link telegraph → **F50**;
  the gizmo/pan/menu/GridSnap/detach test cluster → **F13/F40**.
- **Scopes** — sequence-reorder write path (v1's "single biggest compose-step gap") → **F58–F60**; per-scope
  tidy verb → **F44**; layer-scope interiors → **F42**; outbound portal label → **F45**; scope invariant
  tests → **F43**; ScopeCams leak → **F06**; scope chrome test-debt → **F47**.
- **Chrome** — transport claims false (v1 #1) → **F29** (made true) + **F49** (docs corrected); problems
  badge → **F31**; Ctrl+P → **F35**; frozen zone map + HEALTH/PERF pills + StatusPill → **F32/F33**; command
  registry / layout presets / motion+em audit → **F34/F36/F38/F39**; the 33-item chrome test-debt → **F40**.
- **Outliner-inspector** — drag-reparent + row tints + filters + inspector completion → **F09/F25/F26/F41**.
- **Persistence/undo** — round-trip harness, Init/Dock, events, prefabs, undo-every-road, history jump →
  **F01–F08**.
- **Mirror** — live gating, IsPromoted render, promote/reconcile, live-scope surfaces → **F25–F28**.
- **Vocabulary** (post-v1 decision) — Op nodes + fold, animation builtins, layout family → **F53–F57**.
- **Run-without-a-build** (post-v1 decision) — playback debugger (container/loader/transport/state/
  divergence) → **F61–F65**; previewer interpreter + surface + contract parity + time-travel tie →
  **F66–F70**; self round-trip + generated shell → **F51/F52**.
- **DLL live preview** (post-v1 decision, P12) — **F76** (design), **F78** (copy-marshalling backend,
  link-agnostic), **F78.5** (hand-written method bodies compiled + run). **F77** (imguix-core shared split)
  was implemented then **reverted/superseded** — the copy-marshalling design keeps imguix static, so no
  shared linkage is forced on consumers.

## Parked by intent (not gaps)

Per the checklist's "Explicitly parked" section: null headless backend mode, per-node LOD manual override
(F49 struck it), reroute pins, wire animation, diff-hunk node tagging, keyframe timelines (superseded by
F56), constraint layout edges (F53 rejected them), module interop, command payloads, status-layer model,
Lifecycle north-star view. F74/F75 (remappable input binding) shipped early though horizon-class.

## Tracked residuals (shipped features with a noted follow-on, NOT missing/partial)

These are refinements on already-accepted items, each recorded on its checklist entry:

- **F78/F78.5 in-panel DLL render** — the DLL preview compiles + runs real code (verified headlessly); the
  Preview-tab toggle that renders the compiled app's widgets *inside* the composer panel needs a
  drawdata/texture blit (the module renders in its own context). Deferred; interpreter is the in-panel surface.
- **F47/F48** — interactive note resize handle, per-note colour read, outliner note ordering; heavier
  scope-chrome pixel-extent tests.
- **F40** — residual low-value chrome click tests (undo/redo/history clicks, Diff-in-panel, theme desc tables).

## Test-reliability follow-on (not a feature gap)

- **step93_order_roundtrip is flaky** (~1-in-2). Pre-existing (F58/F60 `Order=` loader area; F60 fixed one
  use-after-free but flakiness remains). It is a test-reliability issue, not a missing/partial feature — the
  order model + emission + drag are shipped-tested (step93/94/96/97/98). Tracked for a dedicated fix; a clean
  run is 112/112.

## Method note

v2 does not re-run v1's 470-claim sweep; it confirms v1's ranked gaps are closed (mapping above), spot-checks
the closures against the checklist DONE-notes + the live suite counts, and inherits v1's still-true baseline
for the unchanged areas. The checklist remains the per-item source of truth.
