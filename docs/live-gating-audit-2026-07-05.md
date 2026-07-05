# Live-node edit-gating audit — 2026-07-05 (F25)

The live mirror is a read-only reflection of the running app: every mutating verb on an `IsLive` node
must be refused. This audit swept all five verbs across all three surfaces and found gating **complete
but silent** — the verb was blocked (control not offered, or a `!IsLive` guard) with no user notice.
F25 adds the notice idiom (`AppNotifyLiveReadOnly` → `AppGraphNotify`) at every path a user can actually
trigger on a live node, and hardens the two editor functions with a self-guard.

## Verbs × surfaces (post-fix)

| Verb | Surface | Blocks live | Notifies | Where |
|---|---|---|---|---|
| rename | canvas title dbl-click | YES | n/a (title drills instead) | 7318 |
| rename | canvas F2 | YES | **YES (added)** | 7502 |
| rename | command "Rename" | YES | **YES (added)** | 8175 |
| rename | outliner pen / dbl-click / inline | YES | n/a (glyph/field not drawn for live) | 13538/13579/13438 |
| delete | canvas Delete key | YES | **YES (added)** | 7366 |
| delete | canvas context menu | YES | n/a (menu item absent; live branch offers Promote) | 7679 |
| delete | command "Delete" | YES | **YES (added)** | 8103 |
| delete | outliner trash / deferred | YES | **YES (added, defensive)** | 13842 |
| drag-reparent | outliner drag source/target | YES | n/a (live can't be picked up or accept) | 13587/13593 |
| drag-reparent | `AppGraphReparent` | YES | **YES (added)** | 13173 |
| field edit | inspector | YES (whole-section early return + read-only banner) | banner | 4459 |
| field edit | canvas inline body | YES | n/a (rows not drawn for live) | 6756/6816 |
| field edit | `EditAppNodeFieldSection` | **YES (added self-guard)** | — | 2272 |
| event edit | inspector | YES (unreachable past early return) | banner | 4481 |
| event edit | canvas inline body | YES | n/a | 6816 |
| event edit | `EditAppControlEvents` | **YES (added self-guard)** | — | 2733 |

## What changed

- `AppNotifyLiveReadOnly(g, n)`: one canonical phrasing (`'<name>' is a live mirror -- read-only. Promote
  it to author it.`) so the notice reads the same wherever it fires. Rides the existing `LastLinkErr`
  channel (status hint + feedback slot).
- Notices added on every **user-attemptable** live mutation: canvas Delete key, F2, the Delete/Rename
  commands, `AppGraphReparent`, and the outliner deferred delete. Verbs that are structurally
  unofferable on live rows (no glyph, no menu item, no drag pickup) need no notice — there is no gesture
  to intercept; the audit confirms each is blocked.
- Defense-in-depth: `EditAppNodeFieldSection` and `EditAppControlEvents` now early-return on `IsLive`.
  They were safe only by caller discipline (inspector early-return + canvas `!IsLive` gates); the
  self-guard closes the latent gap without changing today's behavior.

## Not changed (deliberately)

- `AppGraphRemoveNode` has **no** internal `IsLive` guard and must not gain one: `BuildAppLiveGraph`
  removes stale live nodes through it every frame (imguiapp_nodes.cpp ~12946). Live protection for delete
  lives at the user-facing call sites, not the function.

## OPEN

_(empty)_ — every mutating verb blocks live; user-attemptable ones now notice. step68 drives F2 on a live
control and asserts the notice fires, rename does not start, and the name is untouched.

Gate: imguix-tests 77/77, imguix-core-tests 87/0, imguix-headless-verify 11/11.
