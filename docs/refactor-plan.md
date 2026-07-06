# Refactor Plan — realign to the imgui.h canonical schema  (Phase C design)

Goal: `imguiapp.h` / `imguiapp_internal.h` / all `.cpp`s were originally written emulating `imgui.h`'s
structure, ordering, and comment discipline down to the detail — treating `imgui.h` as the canonical schema.
Across F01–F78.5 they drifted. Phase C realigns them. **Pure structural, ZERO behavior change** (F77-style):
all suites stay green and the codegen corpus stays byte-identical.

Runs AFTER Phase A (so tool decls already live in `imguiapp_internal.h` and the core/tool boundary is clean;
C reorders *within* that structure). Design-first (this doc) before the big diff.

## 1. What drifted (archaeology)

- The 4 top-level `[SECTION]`s are INTACT since the earliest `imguiapp.h` (`0e17e75`, 2026-07-02, pre-rename
  ancestor was `imapp.h`): `Header mess` / `Forward declarations and basic types` / `Compile-time helpers
  (ImGuiStatic<>, ImGuiType<>)` / `Dear ImGui end-user API functions`. The SCHEMA held.
- The drift is WITHIN sections: `imguiapp.h` grew 979 → 1503 lines (+524, ~53%) over the F-series. Sources of
  drift: (a) bloat / dead + duplicated code; (b) AI-written narrative comments accreted feature-by-feature;
  (c) declaration order within sections wandered; (d) tool declarations mixed into the public header (Phase A
  moves those to `internal.h`); (e) `.cpp` definition order no longer tracks header declaration order.
- Reference discipline = the `0e17e75`-era file (terse comments, tight ordering, no tool decls in the public
  header) — NOT a revert (all features stay); restore the DISCIPLINE while keeping the F01–F78.5 surface.

## 2. The imgui.h schema (the target rules)

- `[SECTION]` banners partition the file; the ordering mirrors `imgui.h`: forward decls / basic types →
  compile-time helpers → the end-user API → the big structs last. `imgui_internal.h` holds the internal API
  (our `imguiapp_internal.h`, Phase A).
- Comment discipline: terse, behavior/constraint-stating; `// (…)` parenthetical asides; NO narrative essays,
  NO rationale-in-comments (rationale → commits, provenance → docs). This matches the standing rules.
- `.cpp` definitions appear in the same order as their header declarations, under matching `[SECTION]`s.

## 3. Plan (reviewable passes — never one mega-diff)

1. **Drift map** (part of this doc's execution): per file, per `[SECTION]`, list the out-of-order decls, the
   bloat/dup candidates, and the AI-comment spans. Compare current vs `0e17e75` vs `imgui.h`'s ordering.
2. **De-AI-comment pass** across `.h` + `.cpp`: strip narrative/AI comments; keep behavior/constraint one-liners.
3. **Header reorder**: within each `[SECTION]`, order decls to the imgui.h convention; ensure the public /
   internal split is clean (leans on Phase A).
4. **`.cpp` reorder**: definitions follow header declaration order, under matching section banners.
5. **Consolidate/remove**: dead code, duplicated helpers, redundant forward decls.
6. Apply file-by-file: `imguiapp.h`, `imguiapp_internal.h`, `imguiapp.cpp`, `imguiapp_nodes.cpp`,
   `imguiapp_canvas.cpp`, `imguiapp_preview*.cpp`, `imguiapp_av.*`. One file (or one section) per commit.

## 4. Safety net (this is a no-behavior-change refactor)

- After every pass: `imguix-tests` + `imguix-core-tests` + `imguix-headless-verify` green, and the codegen
  corpus (`ProofDrift` / `ProofControlDrift` / the `*_generated.h` byte-locks) UNCHANGED — reordering
  declarations/defs and deleting comments must not move an emitted byte. A corpus diff = a behavior change
  slipped in; stop and revert that pass.
- Because it is byte-locked + suite-gated, each pass is independently verifiable and revertible.

## 5. Acceptance

- All suites green + codegen corpus byte-identical after the full pass.
- A read-through confirms imgui.h-schema conformance: section banners + ordering, `.cpp` matches header order,
  public/internal split clean, no AI-narrative comments remain, no dead/dup code.
- `imguiapp.h` line count trends back toward the pre-drift discipline (bloat removed), features intact.

## 6. Open fork

- Confirm the **reference commit/era** to diff against: `0e17e75` (earliest `imguiapp.h`) is the proposed
  discipline reference; if a specific pre-rename `imapp.h` commit is "the one," name it and the drift map
  targets that instead.
