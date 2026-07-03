# ImGuiAppAV — frame pacing, frame encoding, test/bench harness

Design for three use cases:
1. App frame pacing, optionally per-viewport (viewports may span monitors with different refresh rates).
2. App frame encode-to-video, with optional arbitrary per-frame data; `__rdtsc` (or platform equivalent) encoded per frame by default.
3. ImGui Test Engine + headless rendering + frame encoding + WAL/event-source logging in one harness, for ergonomic debugging, testing, and benchmarking.

Decisions fixed with the owner (2026-07-02): encoder is a provider seam with built-ins
(QOI sequence, linked libav, Media Foundation); ALL metadata lives IN the video --
the meta record stream is chunked across each frame's pixel strip, no side files;
pacing is an advisory pacer called by the backend run loop; the use-case-3 entry point
is named **ImGuiAppTestHarness**; an ffmpeg-SDK (libav) backend ships by default.

---

## 1. Frame identity: ImGuiAppFrameID

One id per frame, taken at the top of `OnDrawFrame`, is the correlation key across
video, embedded meta stream, WAL, and test-engine logs.

```cpp
struct ImGuiAppFrameID
{
  ImU64  FrameIndex;   // monotonic from run start (not ImGui's frame count: survives context recreation)
  ImU64  Tsc;          // __rdtsc / cntvct_el0 at frame begin
  double TimeSec;      // QPC seconds since run start
};
// lives on the app:
//   ImGuiAppFrameID FrameID;   (member of ImGuiApp, updated by OnDrawFrame)
```

WAL correlation: `ImGuiAppWAL` gains an optional frame-id source; when set, every record
is prefixed `[tick:%llu tsc:%llu]`. Null = today's behavior, so this is non-breaking.

```cpp
struct ImGuiAppWAL
{
  // ... existing fields ...
  const ImGuiAppFrameID* FrameID;   // optional; prefixes records with frame identity
};
```

## 2. Pacing: ImGuiAppPacer (advisory, backend loop stays owner)

```cpp
typedef int ImGuiAppPacerMode;
enum ImGuiAppPacerMode_
{
  ImGuiAppPacerMode_Off = 0,     // free-run; vsync/present mode governs
  ImGuiAppPacerMode_Target,      // pace wall clock to TargetHz (sleep + spin hybrid)
  ImGuiAppPacerMode_Fixed,       // Target pacing AND io.DeltaTime forced to exactly 1/TargetHz
};

struct ImGuiAppPacer
{
  ImGuiAppPacerMode Mode;
  float  TargetHz;         // <= 0 with Mode_Target = pace to primary monitor refresh
  float  SleepSlackMs;     // spin the last N ms (OS sleep granularity guard); default 2.0
  // read-only telemetry
  double LastFrameMs;
  double LastWaitMs;
  ImU64  MissedDeadlines;  // frames that arrived after their deadline
};

// Called once per loop iteration by every backend RunLoop, before OnDrawFrame.
// Off-mode returns immediately, so the call is unconditional in the loops.
IMGUI_API void AppPacerWait(ImGuiApp* app);
```

- `Fixed` is the determinism mode: constant dt feeds the replay theorem (OnUpdate as a
  pure function of state + input + dt), so an encoded run and its WAL are reproducible.
- Pacer dt and video timing are DECOUPLED (section 3, "Timing"): the pacer decides what
  time the app simulates; the encoder decides what time the video claims. A video is
  honest about realtime only when its PTS come from the real clock (`FrameID.TimeSec`),
  never from counting pacer ticks.
- Windows implementation raises timer resolution (`timeBeginPeriod(1)`) while a
  non-Off pacer exists, sleeps until `deadline - SleepSlackMs`, spins the rest on QPC.

### Per-viewport pacing (phase 2)

Multi-viewport apps present one swapchain per platform window; monitors differ in
refresh. The loop runs at the fastest cadence; slower viewports skip presents.

```cpp
// Consulted by the backend's per-viewport present hook (Renderer_SwapBuffers /
// Platform_RenderWindow). True = present this frame; false = skip (contents unchanged
// on that monitor until its next deadline). Main viewport never skips.
IMGUI_API bool AppPacerViewportShouldPresent(ImGuiApp* app, ImGuiViewport* viewport);
```

Refresh per viewport comes from `ImGuiPlatformMonitor` (already maintained by the
platform backends). Pacer keeps per-viewport `NextPresentDeadline` keyed by viewport ID.

## 3. Encoding: provider seam + recorder

### Frame payload

```cpp
struct ImGuiAppAVFrame
{
  int    Width;
  int    Height;
  int    PitchBytes;               // row stride; providers must honor it
  const void* Pixels;              // RGBA8; valid only during WriteFrame
  ImGuiAppFrameID FrameID;
  const void* UserData;            // optional per-frame blob (meta stream record, not visible pixels)
  int    UserDataSize;
};
```

### Provider vtable

```cpp
// What time the video claims. A video is honest about realtime only under Realtime.
typedef int ImGuiAppAVTimingMode;
enum ImGuiAppAVTimingMode_
{
  ImGuiAppAVTimingMode_Auto = 0,   // follow the pacer: Fixed pacer -> Constant, else Realtime
  ImGuiAppAVTimingMode_Constant,   // CFR: frame N plays at N/Fps (synthetic timeline; matches Fixed dt)
  ImGuiAppAVTimingMode_Realtime,   // VFR: PTS = FrameID.TimeSec (wall clock; a 50ms hitch plays as 50ms)
};

struct ImGuiAppAVEncodeConfig
{
  const char* OutputPath;      // container path, or directory for sequence providers
  float       Fps;             // Constant mode: the frame rate. Realtime mode: nominal rate hint only
  ImGuiAppAVTimingMode Timing; // default Auto
  int         Width;           // 0 = first frame's size (fixed thereafter; resize aborts with error)
  int         Height;
  int         BitrateKbps;     // hint; lossless providers ignore
};

struct ImGuiAppAVEncoder
{
  const char* Name;
  bool        SupportsRealtimePts;   // provider can carry per-frame wall-clock PTS (true VFR)
  bool (*Open)(ImGuiAppAVEncoder* self, const ImGuiAppAVEncodeConfig* config);
  bool (*WriteFrame)(ImGuiAppAVEncoder* self, const ImGuiAppAVFrame* frame);   // PTS from frame->FrameID.TimeSec in Realtime mode
  void (*Close)(ImGuiAppAVEncoder* self);
  void (*Destroy)(ImGuiAppAVEncoder* self);   // frees the encoder itself (providers allocate their own)
  void* UserData;              // provider state
};
```

### Timing honesty per provider

Realtime contract, all providers: EVERY frame is present exactly once; PTS are honest
(exactness is method-specific, below). Stepping frame-by-frame in a player walks
consecutive capture frames with no gaps and no duplicates.

| Provider | Realtime PTS | How |
|---|---|---|
| libav (linked) | exact | true VFR: AVFrame->pts carries FrameID.TimeSec directly, microsecond timebase end-to-end through the mp4 muxer -- measured 0us error. The final sample gets a nominal duration at mux time (a zero-duration tail sample falls outside the edit list and decodes as DISCARD) |
| QOI sequence | exact | index.tsv carries `FrameID.TimeSec` per frame; inherently timestamped |
| Media Foundation | resampled CFR | true VFR through IMFSinkWriter is NOT achievable (measured): the H.264 path requires a declared MF_MT_FRAME_RATE (BeginWriting fails without one) and resamples per-sample timestamps to CFR at that rate even with the input rate omitted (90 VFR samples -> 208 CFR frames). Duration honest, frames duplicated to fill -- violates one-frame-once; prefer the libav backend for Realtime |

The meta stream always records real TSC/QPC times regardless of mode -- ground truth
survives even a Constant-mode encode.

### Built-in encoder backends (all ship by default; each declared in its own header)

```cpp
// backends/imguiapp_impl_qoi.h -- zero-dependency lossless sequence:
// <dir>/NNNNNN.qoi + index.tsv. Deterministic, byte-stable across machines -- the
// CI/golden-image provider.
IMGUI_API ImGuiAppAVEncoder* ImGuiApp_ImplQoi_CreateEncoder();

// backends/imguiapp_impl_libav.h -- linked ffmpeg SDK (DEFAULT video provider when the
// SDK is present; scripts/get-ffmpeg.ps1 stages it, CMake gates the TU on it). mp4
// H.264 via libx264, exact per-frame PTS, and the decode side for reading embedded
// input logs back out of a recording. GPL SDK variant: distributing linked binaries
// is GPL.
IMGUI_API ImGuiAppAVEncoder* ImGuiApp_ImplLibav_CreateEncoder();
IMGUI_API bool ImGuiApp_ImplLibav_ReadEmbeddedInputLog(const char* video_path, int embed_rows, ImGuiAppInputLog* out_log, int* out_corrupt_frames);

// backends/imguiapp_impl_mediafoundation.h -- Windows Media Foundation mp4
// (H.264/HEVC), no external exe needed. Explicit choice, never a silent
// default (lossy + driver-variant output is wrong for test artifacts).
IMGUI_API ImGuiAppAVEncoder* ImGuiApp_ImplMediaFoundation_CreateEncoder();

// core seam (imguiapp_av.h): frees any provider's encoder via its vtable Destroy.
IMGUI_API void ImGui::AppAVDestroyEncoder(ImGuiAppAVEncoder* encoder);
```

### The meta stream (embedded in the video)

There is NO side file: the video is the only metadata store. The recorder maintains one
meta record stream per take -- fixed header (magic `IMAVMETA`, version, fps, start TSC
+ QPC Hz for TSC->seconds conversion), then a TYPED-RECORD stream `{u32 type, u32 size,
payload}` -- and chunks it across the frames' pixel strips (next section). Records
self-describe, so tracks extend without format breaks:

```
type Frame:     u64 frame_index | u64 tsc | f64 time_sec | u32 user_size | user bytes
type InputHdr:  ImGuiAppInputLog layout (composition id, slot table) -- once per take
type InputFrm:  u64 frame_index | one ImGuiAppInputLog frame (TempData + dt) | state hash
type StateSnap: composition id | snapshottable-state bytes (ImGuiAppStateHistory layout)
type AudioPcm:  RESERVED in v1 (defined, no producer yet): u64 frame_index | sample
                format header | PCM chunk
```

TSC is the *default* per-frame payload (always present); `user_size` covers the optional
arbitrary blob. `AppAVMetaDump(meta, size)` prints a reconstructed stream as TSV;
`imguix-avtool meta|verify <take> <rows>` extracts and checks one from a recording.

The user blob is OPAQUE BYTES by contract -- the stream format knows nothing of app
types. One typed helper rides on top, built on the EXISTING snapshot machinery
(`AppStateSnapshot` / `ImGuiAppStateHistory` byte layout):

```cpp
// Serialize the app's snapshottable state (same byte layout as ImGuiAppStateHistory
// frames) as this frame's blob. Restoring those bytes IS time travel, so a recording
// made with this helper scrubs: video frame N <-> app state N.
IMGUI_API void AppRecordSnapshotState(ImGuiAppRecorder* rec, ImGuiApp* app);
```

### Input: source events by default, derived checkpoints opt-in

Input records in three layers, cheapest-first:

1. **Raw io (`IoFrame`) -- default, every frame, O(1).** The source events: mouse
   position (main-viewport-relative), buttons, wheel (from the frame's
   `InputEventsTrail`; `EndFrame` zeroes the io fields before the pump runs), key
   transitions against the recorder's shadow, text as UTF-16 units -- plus the frame's
   `AppStateHash` fingerprint. Tens of bytes per frame regardless of app size.
   Replay-side (feeding `AddMousePosEvent` et al. and re-rendering) is a future phase.
2. **State hash (in every IoFrame) -- default.** `AppStateHash(app)` (imguiapp.h): the
   Persist + LastTemp fingerprint AppInputRecord stores. Enables divergence detection
   against any future raw replay without carrying TempData.
3. **TempData log (`InputHdr`/`InputFrame`) -- OPT-IN via AppRecordAttachInputLog.**
   The derived checkpoint: every control's TempData + dt per frame. Enables render-free
   replay (`AppInputReplay`) and per-control divergence attribution. Cost is O(sum of
   TempData) per frame -- attach deliberately.

```cpp
// OPT-IN derived checkpoint (call AppInputRecord once per frame as usual).
IMGUI_API void AppRecordAttachInputLog(ImGuiAppRecorder* rec, const ImGuiAppInputLog* log);

// Parse a reconstructed stream (extract per-backend: ImGuiApp_ImplLibav_ /
// ImGuiApp_ImplQoi_ExtractEmbeddedMeta). Reproduction = restore the snapshot, then
// AppInputReplay(app, &log, &div) -- the divergence check pinpoints non-determinism.
IMGUI_API bool AppAVMetaReadInputLog(const void* meta, int meta_size, ImGuiAppInputLog* out_log);
IMGUI_API bool AppAVMetaReadStateSnapshot(const void* meta, int meta_size, ImVector<char>* out_bytes, ImGuiID* out_composition_id);
```

**Replay mismatch taxonomy** (the `Identity` record, emitted once right after the
stream header, declares the take: applayer + imgui version numbers, composition id,
schema hash over the snapshottable slot table, embed geometry):

1. Identity differs from the replaying build -> **declared version/schema mismatch**:
   refuse or warn BEFORE replaying a single frame.
2. Identity matches but the per-frame hash chain diverges at frame k ->
   **nondeterminism or corruption at k** (first-divergence semantics, as in
   `AppInputReplay`).

### Integrity layers

Five hashes, each isolating a different failure:

| Layer | Scope | A failure isolates |
|---|---|---|
| Chunk checksum (CRC32c, per frame's strip) | that frame's chunk bytes | pixel-level damage in ONE frame; the stream truncates there on read |
| State hash (`AppStateHash`, in every IoFrame) | that frame's Persist+LastTemp state | which FRAME a replay diverged at |
| Hash chain (IoFrame `chain`: `chain_k = ImHashData(&state_hash_k, 4, chain_{k-1})`, seeded by the Identity schema hash) | the hash SEQUENCE | reorder / splice / single-frame substitution -- an attacker or bit flip cannot swap frames without breaking every later link; the seed binds the chain to the declared identity. Ring dumps recompute the chain over surviving entries (eviction legitimately changes the sequence) |
| Schema hash (Identity) | the snapshottable slot layout | replaying against a build whose registered types moved -- refused before frame 1 |
| Stream digest (`Digest`, final record: byte count + `ImHashData` over every preceding stream byte) | the whole take | presence = complete take; absence = truncation (crash); mismatch = corruption anywhere the per-frame layers missed |

`imguix-avtool verify` recomputes all of them from the video alone.

### Recorder (glue between app, backend capture, and provider)

```cpp
struct ImGuiAppRecorder;   // opaque

// The frame is four ordered phases -- ImGuiApp::Frame() = OnDrawFrame (frame id,
// NewFrame, app layers) -> OnRenderFrame (draw data -> GPU, platform windows) ->
// OnEncodeFrame (recorder pump reads the frame just rendered) -> OnPresentFrame.
// AppRecordBegin registers itself on app->Recorder, so OnEncodeFrame pumps it
// automatically; overriding a phase extends the pipeline at that point.

IMGUI_API ImGuiAppRecorder* AppRecordBegin(ImGuiApp* app, ImGuiAppAVEncoder* encoder /* required; providers in backends/ */, const ImGuiAppAVEncodeConfig* config);
IMGUI_API void              AppRecordSetFrameData(ImGuiAppRecorder* rec, const void* data, int size);   // this frame's blob; copied immediately
IMGUI_API bool              AppRecordIsActive(const ImGuiAppRecorder* rec);
IMGUI_API void              AppRecordEnd(ImGuiAppRecorder* rec);   // flush queue, Close provider
```

Threading: `WriteFrame` runs on a single encoder thread behind a bounded queue
(default depth 3). Queue-full policy defaults to `Block` in every mode -- ENCODE EVERY
FRAME is the contract; `DropNewest` (never stall the app, drops counted + WAL-logged)
is an explicit opt-in via `AppRecordSetQueuePolicy`.

Encode-every-frame guarantees: the take's frame ids are contiguous. The first pump
captures the current frame synchronously (no pipeline-priming loss); `AppRecordEnd`
drains the pipelined tail so the final frame lands; any frame that produced no pixels
mid-take (minimized window, capture hiccup) is synthesized as a pause-glyph placeholder
frame carrying its real frame id. The win32 run loop keeps frames running while
minimized whenever a recorder is active, so the pause span is encoded rather than
skipped.

### The pixel strip (stream chunking)

Embedding is UNCONDITIONAL while recording. The stream (header first, then records in
emission order) is a continuous byte sequence; each frame's strip carries its NEXT
chunk, up to capacity. Records need no per-frame alignment -- a large state snapshot
legitimately spans several frames' strips -- so over-capacity is impossible by
construction. A frame with no pending bytes stamps an empty chunk. The only loss mode
is a corrupt frame (checksum), which truncates the reconstructable stream at that
point -- the same honesty as a torn WAL tail. Crash-honesty lives in the CONTAINER:
the libav encoder muxes fragmented mp4 (movflags +frag_keyframe+empty_moov, gop 30), so
a killed process still yields every completed fragment's frames and their chunks; the
QOI sequence is per-frame files, crash-safe by construction. Providers must preserve
the strip through their encode (lossy is fine within the block coding's margin).

Format (frozen; extractors: `ImGuiApp_ImplLibav_ExtractEmbeddedMeta`,
`ImGuiApp_ImplQoi_ExtractEmbeddedMeta`):

- Strip: the bottom `EmbedRows` rows (clamped at `AppRecordBegin` to a multiple of 4,
  minimum 4; the adjusted value is the take's contract). Blocks are 4x4 pixels, luma
  black 16 / white 235 (R = G = B, A = 255), read threshold 128 -- survives lossy encode.
- Block addressing: block (bx, by), by = 0 the TOPMOST reserved row group; bit index
  = by * blocks_per_row + bx, blocks_per_row = floor(W / 4). Pixels right of the block
  grid (W % 4) are black filler.
- Bitstream is a byte stream read MSB-first per byte (bit k of byte i is stream bit
  i * 8 + k, k = 0 the most significant bit).
- Per-frame layout: `u32 magic 'I','M','I','L'` | `u32 chunk_size (LE)` | chunk (the
  stream's next chunk_size bytes) | `u32 checksum (LE) = ImHashData(chunk, chunk_size,
  0)`. Note for external tools: ImHashData is CRC32c (reflected polynomial 0x82F63B78,
  imgui >= 1.91.6), not zlib CRC32. chunk_size 0 (idle frame) is valid.
- Capacity: floor(W/4) * (EmbedRows/4) / 8 - 12 bytes of chunk per frame.
- Unused trailing blocks are black (bit 0).

Reassembly = concatenate chunks in frame order, then parse the record stream. Giant
4x4 luma blocks survive crf-level quantization; the per-frame checksum gates residual
corruption. State snapshots ride the same stream, spanning frames as needed.

### Flight recorder (ring mode)

The WAL's crash-forensics philosophy applied to pixels: an always-on in-memory ring of
the last N seconds (frames QOI-compressed on capture, plus their meta records and
input events), dumped to disk through the provider only when something goes wrong.

```cpp
struct ImGuiAppRingConfig
{
  float  Seconds;        // ring span; default 10
  int    MaxMemoryMB;    // hard cap; oldest frames evicted when either bound binds. default 256
  float  Fps;            // <= 0 (default) = every frame; > 0 = explicit subsample opt-out of encode-every-frame
};

IMGUI_API ImGuiAppRecorder* AppRecordBeginRing(ImGuiApp* app, ImGuiAppAVEncoder* encoder, const ImGuiAppAVEncodeConfig* config, const ImGuiAppRingConfig* ring);
// Encode the ring's contents to disk NOW (assert hook, test failure, hotkey, user code).
// The ring keeps recording; repeated dumps get "-2", "-3" suffixes.
IMGUI_API bool              AppRecordDumpRing(ImGuiAppRecorder* rec, const char* reason);   // reason lands in the WAL + a stream marker record
```

The assert path wires itself: when a ring recorder exists, `ImGuiAppAssertFail` (the
IM_ASSERT sink that already writes the WAL) also calls `AppRecordDumpRing(rec, expr)` --
a failed assert leaves a video of the last N seconds next to the WAL that names it.

## 4. Capture: backend readback hook

New optional entry on the platform backend vtable (null = backend cannot capture;
`AppRecordBegin` fails with a clear error):

```cpp
struct ImGuiAppPlatformBackend
{
  bool (*InitPlatform)(ImGuiApp* app, ImGuiAppConfig& config);
  void (*ShutdownPlatform)(ImGuiApp* app);
  int  (*RunLoop)(ImGuiApp* app);
  // Readback of the frame just rendered (called after render, before present).
  // Double-buffered staging: returns frame N-1's pixels while frame N copies -- the
  // frame id travels inside ImGuiAppAVFrame, so latency never misaligns identity.
  bool (*CaptureFrame)(ImGuiApp* app, ImGuiAppAVFrame* out_frame);
};
```

Vulkan first: copy swapchain image to a host-visible staging buffer at
`ImGuiAppFrameFlags` time, map the previous one. OpenGL3: PBO ring. Headless capture
needs an offscreen target -- see below.

Multi-viewport: the video captures the MAIN viewport only. Secondary platform windows
are not readback targets (their swapchains belong to the viewport hooks); their events
still land in the WAL under the same FrameID, so they stay debuggable, just not
pictured. The harness never spawns secondary viewports, so use case 3 is unaffected.

### Headless rendering

`ImGuiAppConfig` gains one field:

```cpp
typedef int ImGuiAppHeadlessMode;
enum ImGuiAppHeadlessMode_
{
  ImGuiAppHeadlessMode_None = 0,   // normal windowed app
  ImGuiAppHeadlessMode_Null,       // no GPU, no pixels (test engine only; CaptureFrame = null)
  ImGuiAppHeadlessMode_Offscreen,  // GPU renders to an offscreen target; no OS window, CaptureFrame works
};
// ImGuiAppConfig: ImGuiAppHeadlessMode Headless;
```

`Offscreen` is what makes use case 3 record video without a display (CI boxes, ssh).
Vulkan backend implements it as a render-target image instead of a swapchain; the run
loop skips present (existing `ImGuiAppFrameFlags_NoPresent`).

## 5. ImGuiAppTestHarness (use case 3)

One entry point wires app + Test Engine + headless + recorder + WAL, all sharing the
frame id. Replaces the hand-rolled loop in tests/imguix_tests_main.cpp (which today
uses the test engine's own null app, not ImGuiApp -- migrating it is part of this work).

```cpp
struct ImGuiAppTestHarnessConfig
{
  const char* Name;                                   // artifact base name
  const char* ArtifactDir;                            // receives <Name>.mp4/.wal/.frametimes.csv
  ImGuiAppHeadlessMode Headless;                      // default Offscreen
  bool        RecordVideo;                            // requires Offscreen (or windowed)
  bool        KeepArtifactsOnPass;                    // default false: artifacts survive only failures
  ImGuiAppPacerMode PacerMode;                        // default Fixed (reproducible tests); Target/Off for honest-clock benchmark captures
  float       Fps;                                    // Fixed pacer rate / Constant-timing rate; default 60
  ImGuiAppAVTimingMode Timing;                        // default Auto: Fixed pacer -> Constant video, else Realtime (honest) video
  ImGuiAppAVEncoder* Encoder;                         // null = harness default: libav when the SDK is linked, else QOI sequence
  ImGuiAppWALLevel   WALLevel;                        // default Frame
  const char* TestFilter;                             // test-engine filter; null = all
  void (*RegisterTests)(ImGuiTestEngine* engine);     // required
};

// Runs to queue-empty (or abort), returns a ctest-ready exit code.
IMGUI_API int AppTestHarnessRun(ImGuiApp* app, const ImGuiAppTestHarnessConfig* config);
```

Per frame the harness: `AppPacerWait` -> frame id -> `ImGui::NewFrame` -> app frame ->
render -> `CaptureFrame` -> recorder (`WriteFrame` + strip chunk) -> `PreSwap`/present/`PostSwap`.
The WAL's frame-id source is set, so every event-source line carries the same frame index
that names the video frame: *scrub the video to frame N, grep the WAL for `tick:N`*.

Two harness postures, one knob pair:
- **Reproduce** (default): `PacerMode = Fixed`, `Timing = Auto -> Constant`. The app
  simulates a synthetic timeline and the video plays that timeline -- reruns are
  bit-comparable.
- **Witness**: `PacerMode = Target` (or Off), `Timing = Auto -> Realtime`. The video is
  honest realtime -- hitches, stalls, and pacing misses play back at their true
  duration. This is the benchmark-capture posture.

Benchmarking falls out of the same artifacts either way: the meta stream's TSC deltas are
real per-frame costs regardless of timing mode; the harness additionally emits
`<Name>.frametimes.csv` (frame_index, tsc_delta, ms) and prints p50/p95/p99/max.

## 6. File layout

P1 concepts (frame identity, pacing) are app-loop machinery with no AV dependency --
they live in core. Encoder providers follow the idiom Dear ImGui set with its
platform/renderer impl backends: one self-contained TU per provider in /backends with
its own small header, the core seam never includes them, and the app (or harness)
wires a provider exactly like imgui apps wire imgui_impl_win32 + imgui_impl_vulkan.
The harness is its own pair because it alone drags the test engine.

```
imguix/imguiapp/
  imguiapp.h / .cpp                  P1: ImGuiAppFrameID, ImGuiAppPacer + AppPacerWait,
                                           WAL frame-id source, CaptureFrame backend vtable slot
  imguiapp_config.h                           P1: ImGuiAppHeadlessMode field on ImGuiAppConfig
  imguiapp_av.h / imguiapp_av.cpp          AV seam: timing, ImGuiAppAVFrame, encoder vtable,
                                           recorder, ring, meta stream writer/reader
  imguiapp_testharness.h / .cpp            ImGuiAppTestHarness (gated on the test-engine
                                           option; owns the ffmpeg -> QOI default fallback)
  backends/imguiapp_impl_qoi.h / .cpp      encoder backend: QOI sequence (zero-dep, lossless)
  backends/imguiapp_impl_ffmpeg.h / .cpp   encoder backend: ffmpeg pipe (default video provider)
  backends/imguiapp_impl_mediafoundation.h / .cpp
                                           encoder backend: Media Foundation (compiled WIN32
                                           only; the only TU linking mfplat/mfreadwrite)
  backends/imguiapp_impl_*                    platform backends: CaptureFrame impls,
                                           Headless_Offscreen, the unconditional AppPacerWait
                                           call in RunLoop
```

Because core cannot reference providers, there is no CreateDefaultEncoder in the seam:
`AppRecordBegin(encoder = null)` is an error, and the null-Encoder convenience default
(libav when linked -> QOI sequence) is implemented by the harness, which includes the
provider headers it ships with.

Interface-first: the headers above are written and committed BEFORE implementation so
phase work can proceed in parallel against frozen signatures.

## 7. Phasing

- **P1** — FrameID on ImGuiApp; WAL frame-id prefix; ImGuiAppPacer + `AppPacerWait`
  in both win32 run loops (message-pump loop calls it unconditionally).
- **P2** — Encoder seam; QOI-sequence + ffmpeg-pipe providers; recorder + meta stream
  (input-log and state-snapshot record types from day one -- the format is v1);
  `CaptureFrame` for win32-vulkan; `Headless_Offscreen` for vulkan;
  `AppRecordSnapshotState` + `AppRecordAttachInputLog`.
- **P3** — ImGuiAppTestHarness + tests_main migration; flight recorder ring + assert
  hook; stream readback (`AppAVMetaRead*`) for reproduction runs; Media Foundation
  provider; per-viewport pacing (`AppPacerViewportShouldPresent` wired into the
  viewport present hooks); OpenGL3 capture.

## 8. Non-goals / constraints

- No upstream edits: everything sits at the applayer seam (backend vtable is ours).
- Audio: capture is out of scope, but the stream RESERVES its track now: it is a
  typed-record stream ({u32 type, u32 size, payload}); type AudioPcm is defined in v1
  (PCM chunk + FrameID + sample format header) with no producer yet, so a later
  capture lands without a format break.
- Live streaming, GPU-side encode (NVENC et al): possible later behind the same
  provider vtable; not in P1-P3.
- Resize during recording: recorder aborts with a WAL line rather than silently
  rescaling (fixed-size contract keeps every provider simple and diffs honest).
