# Playback Debugger — scrub a recorded run offline (F61)

## 1. Summary & goals

The **playback debugger** opens a run the harness already recorded and scrubs it *offline*: the
timeline is a finished artifact, not a live app. It is the FILE half of P9.5's "composition is
executable data" pair — the previewer (F66+) interprets the graph live; the playback debugger
replays a captured trajectory. Both ride the same shipped rails, so this doc invents **no new
format**: the recorded take is already a self-describing, self-verifying container, and F61's job is
to *name* it, *freeze* its shape, and route the F29 transport at it.

Everything the debugger needs is what `imguix-headless-verify` already emits and the harness already
proves from its own pixels: frame images, the raw-input stream, per-frame state hashes, opt-in
TempData + snapshots, an identity record, and an end-of-stream digest — one embedded meta stream plus
a tick-correlated WAL (`imguiapp_testharness.cpp:302-309` is the run's ground-truth summary line;
`AppAVMetaVerify` at `imguiapp_av.cpp:1398-1491` is the ladder that produces it). This doc **freezes**
the container so F62 (loader + index), F63 (FILE-mode transport), F64 (state-at-tick), and F65
(divergence) have no open questions.

**Thesis (four moves).**
1. **One container, already shipped** — the RUN is the recording (spine: embedded meta stream) plus
   its basename siblings; a single openable path. §2.
2. **One walk builds the index** — the same traversal `AppAVMetaVerify` performs yields the per-tick
   index and the snapshot list. §3.
3. **One transport, two sources** — F29's `ComposerTransport` gains a SOURCE switch (LIVE ring vs
   FILE run) behind a two-method abstraction. §4–5.
4. **Two divergence layers, one marker** — recording-integrity (chain/digest) and replay-fidelity
   (recorded vs replayed state hash) both flag a tick; jump-to-first is a verb. §6.

**House constraints honored throughout:** no upstream edits (backend seam only,
`feedback_no_upstream_edits.md`); pointers not references; `char[]`+`ImVector`, no `std::string`;
tick (`ImGuiAppFrameID.FrameIndex`) is the single correlation key across every stream; the debugger
is read-only over the artifact. Everything below cites the rail it unifies.

---

## 2. The run container (FROZEN)

### 2.1 What already ships

The harness writes, per take, into one `ArtifactDir` keyed by `Name`
(`imguiapp_testharness.h:15` — "receives `<Name>.mp4/.wal/.frametimes.csv`"):

| Artifact | Producer | Carries |
|---|---|---|
| `<name>.mp4` **or** `<name>/` (QOI dir) | recorder + encoder backend | the frame images **and** the embedded meta stream (below) |
| `<name>.wal` | `ImGuiAppWAL` | tick-prefixed lifecycle log incl. command dispatch |
| `<name>.frametimes.csv` | harness | per-frame cost `frame_index, tsc_delta, ms` (`imguiapp_testharness.cpp:269-272`) |

The **meta stream** is *embedded in the recording's pixels*, not a side file (av-design.md:184-215):
the recorder chunks a continuous typed-record byte stream across the bottom `EmbedRows` rows of each
frame as 4×4 luma blocks (format frozen, av-design.md:314-330; config comment `imguiapp_av.h:50-59`).
This holds for **both** backends — the mp4 pixel strip and each `NNNNNN.qoi` frame's strip — so
extraction is uniform. The QOI dir additionally writes an auxiliary `index.tsv`
(`file\tframe_index\ttime_sec\ttsc`, `imguiapp_impl_qoi.cpp:87,119-123`); it is a convenience index,
**not** the authority — the embedded stream is.

### 2.2 The container definition

> **A RUN is addressed by ONE path — the recording** (`<name>.mp4` or the `<name>/` QOI directory).
> The loader resolves the rest by **basename in the same parent directory**: `<name>.wal` (optional),
> `<name>.frametimes.csv` (optional). There is no archive, no manifest, no new byte layout — the run
> "container" is exactly the harness's existing co-located outputs, joined by the shared basename and,
> internally, by **tick** = `ImGuiAppFrameID.FrameIndex`.

The recording is the **authoritative spine**: the meta stream in its pixels fully determines frames,
inputs, snapshots, identity, and completeness, and self-verifies without any sibling. The WAL is a
**correlated command/lifecycle log**, joined by the `[tick:N tsc:T]` prefix
(`imguiapp.cpp:1018-1020`). Losing the WAL loses only the command annotations; losing the recording
loses the run.

### 2.3 Frozen stream map — how each existing stream lands in the container

Extraction is one call per backend, both returning the **same** reconstructed byte buffer
(`ImGuiApp_ImplLibav_ExtractEmbeddedMeta` / `ImGuiApp_ImplQoi_ExtractEmbeddedMeta`, declared
`imguiapp_av.h:179-181`, used `imguiapp_testharness.cpp:293-298`). That buffer is:

```
[ 40-byte header ] [ record ] [ record ] ...            (imguiapp_av.cpp:73-81, written 1015-1028)
  header  = char Magic[8]="IMAVMETA" | u32 Version=1 | f32 Fps |
            u64 StartTsc | u64 QpcHz | u64 StartQpc
  record  = u32 type | u32 size | payload               (TLV, append-only; imguiapp_av.cpp:86-97)
```

Record catalog (types `imguiapp_av.h:86-118`; builders cited). Every payload that names a frame uses
the same `frame_index` = tick:

| type | payload (frozen) | builder | role for the debugger |
|---|---|---|---|
| `Identity` (6) | `u32 applayer_ver \| u32 imgui_ver \| u32 composition_id \| u32 schema_hash \| u32 embed_rows \| u16 block \| u16 rsvd` — emitted ONCE after header | `imguiapp_av.cpp:423-452` | trust gate + chain seed; `composition_id`/`schema_hash` decide whether state reconstruction (F64) is legal |
| `Frame` (1) | `u64 frame_index \| u64 tsc \| f64 time_sec \| u32 user_size \| user…` | `312-321` | tick spine + wall-clock PTS; `frame_index==~0` is a ring-dump reason marker (`1419-1420`) |
| `IoFrame` (5) | `u64 tick \| f32 mx \| f32 my \| u8 buttons \| f32 wheel \| f32 wheel_h \| u32 state_hash \| u32 chain \| u16 key_n \| {u16 key,u8 down}* \| u16 char_n \| {u16 unit}*` (33-byte fixed prefix; `state_hash` at +25, `chain` at +29) | `471+` | every-frame raw input + per-tick recorded `state_hash` + splice-evident `chain` |
| `InputHdr` (2) | `u32 composition_id \| u32 frame_size \| u32 slots \| {u32 id,s32 off,s32 size}*` — once/take, OPT-IN | `325-344` | slot table for render-free replay |
| `InputFrame` (3) | `u64 frame_index \| u32 state_hash \| frame_size bytes (dt + TempData slots)` — OPT-IN | `348-354` | the per-tick input to replay forward (F64) |
| `StateSnapshot` (4) | `u32 composition_id \| u64 frame_index \| u32 slots \| {u32 id,s32 size}* \| state bytes (StorageEntries order)` | `359-392` (via `AppRecordSnapshotState`, `1090-1099`) | restore points; `frame_index` binds snapshot ↔ tick |
| `Digest` (7) | `u64 stream_bytes \| u32 digest = ImHashData(all preceding bytes,0)` — FINAL record | `579-585` | completeness/corruption proof for the whole take |
| `AudioPcm` (8) | reserved; no producer | — | ignore |

**Frozen guarantees a loader may assume** (all recomputed today by `AppAVMetaVerify`,
`imguiapp_av.cpp:1398-1487`):
- ticks are contiguous `FirstTick..LastTick` with `Frames == LastTick-FirstTick+1` (gaps counted);
- `IoFrames == Frames` (the every-frame contract extends to input);
- exactly one `Identity`, first non-header record; the io `chain` seeds from its `schema_hash`;
- a present, matching `Digest` ⇒ complete take; absent ⇒ truncated (crash); mismatch ⇒ corruption.

### 2.4 Version gate

`header.Version == 1` and `Identity` are the format contract; a loader refusing an unknown version is
the only forward-compat rule F62 needs. Byte layouts above are **frozen at v1** — F62–F65 read them
verbatim; any change bumps `Version` and is out of scope here.

---

## 3. Index shape (F62)

F62 does **one linear walk** of the reconstructed meta buffer — structurally the traversal
`AppAVMetaVerify` already performs (`imguiapp_av.cpp:1411-1481`) — and, instead of only counting,
records each record's **tick and payload offset**. No format change: same records, a richer landing.

```cpp
// F62 build product (heap; opaque to the snapshot contract, like ComposerTransport).
struct ImGuiAppRunTick        // one per tick, FirstTick..LastTick
{
  ImU64 Tick;                 // == Frame/IoFrame frame_index
  ImU64 Tsc;                  // Frame.tsc
  double TimeSec;             // Frame.time_sec (PTS; also QOI index.tsv)
  int   FrameImage;           // frame ordinal for decode (mp4 sample / QOI NNNNNN); -1 = placeholder
  int   IoOffset;             // byte offset of this tick's IoFrame record (-1 if none)
  ImU32 StateHash;            // IoFrame.state_hash (recorded fingerprint)
  ImU32 Chain;                // IoFrame.chain (as recomputed; divergence flag lives here)
  int   InputOffset;          // InputFrame record offset (-1 if not opt-in this tick)
  int   SnapshotOffset;       // StateSnapshot record offset at this tick (-1 if none)
  int   WalFirst, WalCount;   // slice into the parsed WAL lines carrying [tick:N]
};
struct ImGuiAppRunIndex
{
  ImGuiAppAVMetaHeader Header;
  ImGuiAppRunMeta      Identity;      // decoded Identity (composition_id, schema_hash, versions)
  ImVector<ImGuiAppRunTick> Ticks;
  ImVector<int>        SnapshotTicks; // ascending tick indices where SnapshotOffset>=0 (nearest lookup)
  ImGuiAppAVStreamStats Stats;        // = AppAVMetaVerify output: gaps, chain, digest, first-divergence
  // ImGuiAppInputLog reconstructed via AppAVMetaReadInputLog (imguiapp_av.cpp:1493) when opt-in present.
};
```

- **tick → nearest snapshot** is a binary search in `SnapshotTicks` for the greatest `<= N`. The
  shipped `AppAVMetaReadStateSnapshot` returns only the *first* snapshot ("first snapshot wins",
  `imguiapp_av.cpp:1560`); F62 generalizes that single-shot read into the tick-keyed `SnapshotOffset`
  during the same walk (still zero format change).
- **per-tick presence** (has-input / has-command / has-snapshot / has-image / digest) is exactly the
  boolean set F63 paints as timeline markers and F64/F65 query.
- **WAL correlation**: parse `<name>.wal` once, bucket lines by their `[tick:N]` prefix
  (`imguiapp.cpp:1018-1020`); `"execute command %d"` lines (`imguiapp.cpp:518`, Lifecycle level,
  `imguiapp.h:133`) are the command dispatches. WAL absent ⇒ `WalCount==0` everywhere; the run still
  opens (the recording is authoritative).
- **Acceptance (F62):** the index counts reproduce the summary line —
  `Ticks.Size == Frames`, snapshot/io/input tallies equal `Stats`, and `chain`/`digest` match
  `imguiapp_testharness.cpp:302-309`.

The mp4 frame-image decode is per-sample (libav decode side already exists as the extraction
counterpart); the QOI image is `ImQoiDecode` of `NNNNNN.qoi` (`imguiapp_qoi.h:13`). Decode is
**on-demand at the scrub position**, never a full-run preload.

---

## 4. Transport grammar shared with F29 (F63)

### 4.1 The surface already exists

F29's `ComposerTransport` (`imguiapp_demo.cpp:326-331`) is an `ImGuiAppStateHistory` ring plus
`Frozen` + `Frame` (scrub position). Its toolbar — pause/resume, step-back, frame slider `f %d`,
step-forward, all gated on `show_live` — is `imguiapp_demo.cpp:951-983`. LIVE behavior: unfrozen it
snapshots the Mirror every frame and follows the newest (`578-585`); frozen it clamps `Frame` and
`AppStateRestore`s the scrubbed bytes back into the running app (`586-591`). F63 **reuses this exact
chrome**; it does not fork a second transport.

### 4.2 The SOURCE switch

The transport gains one enum and one pointer — the surface stays identical, the *source of frames*
differs:

```cpp
enum ImGuiAppTransportSource_ { Source_LiveRing = 0, Source_FileRun };

// Two-method abstraction the toolbar drives; neither variant knows about the other.
struct ImGuiAppTransportView          // what the slider addresses + what the canvas shows
{
  int   Count() const;                // LIVE: History.Count      FILE: Index.Ticks.Size
  // Land on scrub index i (0..Count-1). LIVE: AppStateRestore into the Mirror, canvas re-renders the
  // live app at that state. FILE: decode the frame image at Ticks[i] and blit it; no app is driven.
  void  Show(int i);
};
```

- **LiveRingSource** wraps `ComposerTransport.History` + the Mirror app; `Show(i)` = the existing
  `AppStateRestore(Mirror, &History, i)` (`imguiapp_demo.cpp:589`). The canvas is a *re-render* of a
  restored live app.
- **FileRunSource** wraps `ImGuiAppRunIndex` + the decoded-frame blitter; `Show(i)` decodes
  `Ticks[i].FrameImage` and displays those **pixels**. There is no live app to restore into — the
  frame is authoritative imagery, and Persist/Temp values come from §5, not from re-rendering.

Because both expose `Count()`/`Show(int)`, the pause/step/slider block is source-agnostic. The one
honest asymmetry: LIVE "resume" re-arms recording; FILE "resume" is inert (a finished run has no
newest frame) — the FILE source reports `Frozen`-always, so the toolbar hides resume and shows only
scrub verbs. Step maps to `Frame±1` over integer tick indices, so **step always lands on an exact
tick** (no interpolation) — F63's acceptance ("shown frame's tick == slider tick") is structural:
`Show(i)` addresses `Ticks[i].Tick` directly.

### 4.3 Timeline strip

The FILE source paints per-tick markers from the index booleans: input ticks, command dispatches
(WAL), snapshot points, and — from §6 — divergence ticks. This is the F29 slider widened into a
marked strip; the slider value remains the scrub tick.

---

## 5. State-at-tick (F64)

Scrubbing shows the recorded *image* at tick N. F64 additionally reconstructs the *values* at N by
the contract-7 machinery (restore-and-replay proven exact,
`tests/imguiapp_core_tests.cpp:292-330`; `big-idea.md:47-51`):

1. **Identity gate.** State reconstruction is legal only when a reconstruction app exists whose
   `GetAppCompositionID` (`imguiapp.h:414`) equals `Index.Identity.composition_id` and whose
   `schema_hash` matches (av-design.md:246-255 taxonomy; `Identity` at `imguiapp_av.h:101-110`). In
   the Composer that app is the Mirror (the running binary that IS this composition). On mismatch,
   §4's image scrub, io stream, WAL, and divergence still work — only value reconstruction is refused,
   loudly, before frame 1.
2. **Restore nearest snapshot.** Binary-search `SnapshotTicks` for the greatest `S <= N`; splat that
   `StateSnapshot`'s bytes (read at `Ticks[S].SnapshotOffset`; single-shot reader shape shipped as
   `AppAVMetaReadStateSnapshot`, `imguiapp_av.cpp:1551`) into the reconstruction app's storage,
   matched by the slot `{id,size}` table — the mirror image of `AppStateRestore`
   (`imguiapp.cpp:1256+`).
3. **Replay S→N.** Feed ticks `S+1..N` of the reconstructed `ImGuiAppInputLog`
   (`AppAVMetaReadInputLog`, `imguiapp_av.cpp:1493`) through the replay loop — inject each
   `InputFrame`'s TempData, call `UpdateApp(app, dt)`, no render — exactly `AppInputReplay`
   (`imguiapp.h:381`; impl `imguiapp.cpp:1217-1254`). `out_first_divergence` falls out here for §6.
   Replay is render-free, so reconstructing any N is cheap.
4. **Inspect.** The reconstruction app's registered storage now holds the app AT tick N. The inspector
   renders each instance's `Persist` and `Temp` through the reflection field-widgets (ImStructTable /
   the reflection walk `imguiapp_reflect.h`, the same table the live mirror uses,
   `feature-complete-checklist.md:563`). Persist/LastTemp is the `[0, TempOffset)` prefix; Temp is the
   `[TempOffset, TempOffset+TempSize)` input range (`imguiapp.h:842-844`).
5. **Command log for N.** The WAL slice `Ticks[N].WalFirst..+WalCount` — the `[tick:N]` lines,
   `"execute command %d"` among them — is that tick's dispatch list, shown verbatim.

**Acceptance (F64):** at any N, the reconstructed `AppStateHash` (`imguiapp.h:386`) equals the
recorded `Ticks[N].StateHash`; replay from a snapshot is exact.

> Requires opt-in `InputHdr`/`InputFrame` in the take (`AppRecordAttachInputLog`,
> `imguiapp_av.cpp:1101`). A raw-io-only take (no TempData) still scrubs images + io + WAL + snapshots,
> but value reconstruction *between* snapshot ticks is unavailable — F64 then reports values only at
> snapshot ticks and greys the inspector elsewhere. The debugger states which capability the loaded
> take supports rather than faking values.

---

## 6. Divergence semantics (F65)

Two independent comparisons; the timeline paints both as a divergence marker, jump-to-first visits the
earliest.

**(a) Recording integrity — is the artifact itself intact?** From the load-time verify ladder
(`AppAVMetaVerify`, `imguiapp_av.cpp:1441-1487`; ladder table av-design.md:257-268):
- the io **chain** `chain_k = ImHashData(&state_hash_k, 4, chain_{k-1})`, seeded by `Identity.schema_hash`, is recomputed over the surviving IoFrames; the first tick whose stored `chain`
  differs is `Stats.ChainDivergesAt` (io-frame ordinal) — a reorder, splice, or bit-flip is
  reorder-evident there.
- the final **Digest** over the whole logical stream gives `Stats.DigestState` (0 ok / 1 missing /
  2 mismatch).

A **corrupted fixture** (tampered bytes) breaks the chain at exactly the touched tick and/or fails the
digest; that tick is marked. This is F65's `corrupted-fixture` acceptance ("flags the right tick;
clean run shows none") — a clean run has `ChainDivergesAt == -1` and `DigestState == 0`, hence no
markers.

**(b) Replay fidelity — does the build still reproduce the run?** When §5 runs, `AppInputReplay`
compares the *replayed* `AppStateHash` at each tick against the *recorded* `state_hash`
(`imguiapp.cpp:1241-1242`) and returns `out_first_divergence`. A mismatch means the reconstruction app
computed a different state than was recorded — **nondeterminism in this build vs the captured run**
(a changed `OnUpdate`, an unrecorded input, an unregistered field), NOT artifact damage. This is the
diagnostic verb: "at which tick does today's code stop matching the recording?"

**Definition (frozen).** A tick's `state_hash` is the recorded ground truth. It is compared against
(a) its own recomputed chain link — corruption — and (b) a deterministic replay's live
`AppStateHash` — infidelity. A marker means "recorded ≠ authoritative recomputation"; the tooltip
names which layer fired. Jump-to-first-divergence targets `min(ChainDivergesAt, out_first_divergence)`
over whichever layers are active — always an **exact tick**, so the transport lands on it via §4.2.

---

## 7. Non-goals & constraints

- **No new byte format, no upstream edits.** The container is the harness's existing outputs; the meta
  stream, pixel strip, WAL, and integrity ladder are shipped and frozen. F62–F65 are a loader, an
  index, a source adapter, and two comparisons — all at the applayer seam
  (`feedback_no_upstream_edits.md`).
- **Read-only over the artifact.** The debugger never rewrites a recording; the reconstruction app is
  a scratch replay target, never the source.
- **Identity mismatch degrades, never crashes.** Image/io/WAL/integrity always work; value
  reconstruction is gated on composition + schema equality and refused up front.
- **Live and file are one transport.** The LIVE ring (`ComposerTransport`) and a FILE run are the same
  scrub surface differing only in `Source`; the F70 tie (preview → record → open) closes the loop back
  through this same container.
- **Ring dumps are runs too.** A flight-recorder dump (`AppRecordDumpRing`,
  `imguiapp_av.cpp:1184-1291`) is a valid recording with the same header/Identity/Digest (its chain is
  recomputed over survivors at dump time); the loader opens an assert-dump exactly like a full take —
  no special case.
