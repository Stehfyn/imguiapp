# ImGuiAppAV — frame pacing, frame encoding, test/bench harness

Design for three use cases:
1. App frame pacing, optionally per-viewport (viewports may span monitors with different refresh rates).
2. App frame encode-to-video, with optional arbitrary per-frame data; `__rdtsc` (or platform equivalent) encoded per frame by default.
3. ImGui Test Engine + headless rendering + frame encoding + WAL/event-source logging in one harness, for ergonomic debugging, testing, and benchmarking.

Decisions fixed with the owner (2026-07-02): encoder is a provider seam with built-ins
(QOI sequence, ffmpeg pipe, Media Foundation); per-frame data lives in a sidecar track
file; pacing is an advisory pacer called by the backend run loop; the use-case-3 entry
point is named **ImGuiAppTestHarness**; an ffmpeg backend ships by default.

---

## 1. Frame identity: ImGuiAppFrameID

One id per frame, taken at the top of `OnDrawFrame`, is the correlation key across
video, sidecar, WAL, and test-engine logs.

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
  const void* UserData;            // optional per-frame blob (goes to sidecar, not the video)
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
  bool        WriteSidecar;    // default true: "<OutputPath>.avmeta"
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
| ffmpeg pipe | wallclock VFR | true VFR: one pipe write per frame, `-use_wallclock_as_timestamps 1` stamps each at pipe-READ time (`-framerate 1000` gives the demuxer a 1ms timebase -- the default 25 quantizes PTS to 40ms and collides neighbors; `-fps_mode passthrough -video_track_timescale 1000000` preserve them). PTS = capture time + bounded encoder-queue latency; when that latency matters, the sidecar holds exact capture times and Close writes `<OutputPath>.remux.txt` with the concat-demuxer rebuild recipe |
| QOI sequence | exact | index.tsv carries `FrameID.TimeSec` per frame; inherently timestamped |
| Media Foundation | resampled CFR | true VFR through IMFSinkWriter is NOT achievable (measured): the H.264 path requires a declared MF_MT_FRAME_RATE (BeginWriting fails without one) and resamples per-sample timestamps to CFR at that rate even with the input rate omitted (90 VFR samples -> 208 CFR frames). Duration honest, frames duplicated to fill -- violates one-frame-once; prefer the ffmpeg pipe for Realtime |

The sidecar always records real TSC/QPC times regardless of mode -- ground truth
survives even a Constant-mode encode.

### Built-in encoder backends (all ship by default; each declared in its own header)

```cpp
// backends/imguiapp_impl_qoi.h -- zero-dependency lossless sequence:
// <dir>/NNNNNN.qoi + index.tsv. Deterministic, byte-stable across machines -- the
// CI/golden-image provider.
IMGUI_API ImGuiAppAVEncoder* ImGuiAppAV_CreateQoiSequenceEncoder();

// backends/imguiapp_impl_ffmpeg.h -- ffmpeg process pipe (DEFAULT video provider):
// spawns ffmpeg, feeds rawvideo RGBA on stdin ("-f rawvideo -pix_fmt rgba -s WxH
// -r <fps> -i - <extra_args> <OutputPath>"). No link-time dependency; Open fails
// cleanly when the exe is absent. exe = nullptr searches PATH. extra_args = nullptr ->
// "-c:v libx264 -preset veryfast -crf 18".
IMGUI_API ImGuiAppAVEncoder* ImGuiAppAV_CreateFfmpegEncoder(const char* ffmpeg_exe, const char* extra_args);

// backends/imguiapp_impl_mediafoundation.h -- Windows Media Foundation mp4
// (H.264/HEVC), no external exe needed. Explicit choice, never a silent
// default (lossy + driver-variant output is wrong for test artifacts).
IMGUI_API ImGuiAppAVEncoder* ImGuiAppAV_CreateMediaFoundationEncoder();

// core seam (imguiapp_av.h): frees any provider's encoder via its vtable Destroy.
IMGUI_API void ImGuiAppAV_DestroyEncoder(ImGuiAppAVEncoder* encoder);
```

### Sidecar track: `<output>.avmeta`

Written by the recorder, not the provider, so every provider gets it uniformly.
Binary, WAL-flavored: fixed header (magic `IMAVMETA`, version, fps, start TSC + QPC Hz
for TSC->seconds conversion), then a TYPED-RECORD stream -- `{u32 type, u32 size,
payload}` -- so tracks extend without format breaks:

```
type Frame:     u64 frame_index | u64 tsc | f64 time_sec | u32 user_size | user bytes
type InputHdr:  ImGuiAppInputLog layout (composition id, slot table) -- once per take
type InputFrm:  u64 frame_index | one ImGuiAppInputLog frame (TempData + dt) | state hash
type StateSnap: composition id | snapshottable-state bytes (ImGuiAppStateHistory layout)
type AudioPcm:  RESERVED in v1 (defined, no producer yet): u64 frame_index | sample
                format header | PCM chunk
```

TSC is the *default* per-frame payload (always present); `user_size` covers the optional
arbitrary blob. A tiny `AppAVMetaDump(path)` debug helper prints it as TSV.

The user blob is OPAQUE BYTES by contract -- the sidecar format knows nothing of app
types. One typed helper rides on top, built on the EXISTING snapshot machinery
(`AppStateSnapshot` / `ImGuiAppStateHistory` byte layout):

```cpp
// Serialize the app's snapshottable state (same byte layout as ImGuiAppStateHistory
// frames) as this frame's blob. Restoring those bytes IS time travel, so a recording
// made with this helper scrubs: video frame N <-> app state N.
IMGUI_API void AppRecordSnapshotState(ImGuiAppRecorder* rec, ImGuiApp* app);
```

### Input track

The framework already has record/replay at the right level: `ImGuiAppInputLog` records
every control's TempData + dt per frame with a post-update state hash, and
`AppInputReplay` re-runs it with divergence detection (imguiapp.h). AV does not
reinvent this -- the sidecar PERSISTS it:

```cpp
// Attach a live input log: the recorder serializes its frames into the sidecar as they
// are recorded (call AppInputRecord once per frame as usual). AppRecordEnd finalizes.
IMGUI_API void AppRecordAttachInputLog(ImGuiAppRecorder* rec, const ImGuiAppInputLog* log);

// Load a persisted run back: the input log and (if recorded) the starting state
// snapshot. Reproduction = restore snapshot, then AppInputReplay(app, &log, &div) --
// the existing divergence check pinpoints any non-determinism.
IMGUI_API bool AppAVMetaReadInputLog(const char* avmeta_path, ImGuiAppInputLog* out_log);
IMGUI_API bool AppAVMetaReadStateSnapshot(const char* avmeta_path, ImVector<char>* out_bytes, ImGuiID* out_composition_id);
```

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
IMGUI_API void              AppRecordEnd(ImGuiAppRecorder* rec);   // flush queue, Close provider, finalize sidecar
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

### Flight recorder (ring mode)

The WAL's crash-forensics philosophy applied to pixels: an always-on in-memory ring of
the last N seconds (frames QOI-compressed on capture, plus their sidecar records and
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
IMGUI_API bool              AppRecordDumpRing(ImGuiAppRecorder* rec, const char* reason);   // reason lands in the WAL + sidecar header
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
  const char* ArtifactDir;                            // receives <Name>.mp4/.avmeta/.wal/.frametimes.csv
  ImGuiAppHeadlessMode Headless;                      // default Offscreen
  bool        RecordVideo;                            // requires Offscreen (or windowed)
  bool        KeepArtifactsOnPass;                    // default false: artifacts survive only failures
  ImGuiAppPacerMode PacerMode;                        // default Fixed (reproducible tests); Target/Off for honest-clock benchmark captures
  float       Fps;                                    // Fixed pacer rate / Constant-timing rate; default 60
  ImGuiAppAVTimingMode Timing;                        // default Auto: Fixed pacer -> Constant video, else Realtime (honest) video
  ImGuiAppAVEncoder* Encoder;                         // null = harness default: ffmpeg on PATH, else QOI sequence
  ImGuiAppWALLevel   WALLevel;                        // default Frame
  const char* TestFilter;                             // test-engine filter; null = all
  void (*RegisterTests)(ImGuiTestEngine* engine);     // required
};

// Runs to queue-empty (or abort), returns a ctest-ready exit code.
IMGUI_API int AppTestHarnessRun(ImGuiApp* app, const ImGuiAppTestHarnessConfig* config);
```

Per frame the harness: `AppPacerWait` -> frame id -> `ImGui::NewFrame` -> app frame ->
render -> `CaptureFrame` -> recorder (`WriteFrame` + sidecar) -> `PreSwap`/present/`PostSwap`.
The WAL's frame-id source is set, so every event-source line carries the same frame index
that names the video frame: *scrub the video to frame N, grep the WAL for `tick:N`*.

Two harness postures, one knob pair:
- **Reproduce** (default): `PacerMode = Fixed`, `Timing = Auto -> Constant`. The app
  simulates a synthetic timeline and the video plays that timeline -- reruns are
  bit-comparable.
- **Witness**: `PacerMode = Target` (or Off), `Timing = Auto -> Realtime`. The video is
  honest realtime -- hitches, stalls, and pacing misses play back at their true
  duration. This is the benchmark-capture posture.

Benchmarking falls out of the same artifacts either way: the sidecar's TSC deltas are
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
                                           recorder, ring, sidecar writer/reader
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
(ffmpeg on PATH -> QOI sequence) is implemented by the harness, which includes the
provider headers it ships with.

Interface-first: the headers above are written and committed BEFORE implementation so
phase work can proceed in parallel against frozen signatures.

## 7. Phasing

- **P1** — FrameID on ImGuiApp; WAL frame-id prefix; ImGuiAppPacer + `AppPacerWait`
  in both win32 run loops (message-pump loop calls it unconditionally).
- **P2** — Encoder seam; QOI-sequence + ffmpeg-pipe providers; recorder + sidecar
  (input-log and state-snapshot record types from day one -- the format is v1);
  `CaptureFrame` for win32-vulkan; `Headless_Offscreen` for vulkan;
  `AppRecordSnapshotState` + `AppRecordAttachInputLog`.
- **P3** — ImGuiAppTestHarness + tests_main migration; flight recorder ring + assert
  hook; sidecar readback (`AppAVMetaRead*`) for reproduction runs; Media Foundation
  provider; per-viewport pacing (`AppPacerViewportShouldPresent` wired into the
  viewport present hooks); OpenGL3 capture.

## 8. Non-goals / constraints

- No upstream edits: everything sits at the applayer seam (backend vtable is ours).
- Audio: capture is out of scope, but the sidecar RESERVES its track now: .avmeta is a
  typed-record stream ({u32 type, u32 size, payload}); type AudioPcm is defined in v1
  (PCM chunk + FrameID + sample format header) with no producer yet, so a later
  capture lands without a format break.
- Live streaming, GPU-side encode (NVENC et al): possible later behind the same
  provider vtable; not in P1-P3.
- Resize during recording: recorder aborts with a WAL line rather than silently
  rescaling (fixed-size contract keeps every provider simple and diffs honest).
