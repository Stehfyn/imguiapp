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
is prefixed `[f:%llu tsc:%llu]`. Null = today's behavior, so this is non-breaking.

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
  The harness (section 5) uses it; encoders assume its rate for PTS.
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
struct ImGuiAppAVEncodeConfig
{
  const char* OutputPath;      // container path, or directory for sequence providers
  float       Fps;             // PTS rate; match the pacer's Fixed rate
  int         Width;           // 0 = first frame's size (fixed thereafter; resize aborts with error)
  int         Height;
  int         BitrateKbps;     // hint; lossless providers ignore
  bool        WriteSidecar;    // default true: "<OutputPath>.avmeta"
};

struct ImGuiAppAVEncoder
{
  const char* Name;
  bool (*Open)(ImGuiAppAVEncoder* self, const ImGuiAppAVEncodeConfig* config);
  bool (*WriteFrame)(ImGuiAppAVEncoder* self, const ImGuiAppAVFrame* frame);
  void (*Close)(ImGuiAppAVEncoder* self);
  void* UserData;              // provider state
};
```

### Built-in providers (all ship by default)

```cpp
// Zero-dependency lossless sequence: <dir>/NNNNNN.qoi + index.tsv. Deterministic,
// byte-stable across machines -- the CI/golden-image provider.
IMGUI_API ImGuiAppAVEncoder* ImGuiAppAV_CreateQoiSequenceEncoder();

// ffmpeg process pipe (DEFAULT video provider): spawns ffmpeg, feeds rawvideo RGBA on
// stdin ("-f rawvideo -pix_fmt rgba -s WxH -r <fps> -i - <extra_args> <OutputPath>").
// No link-time dependency; Open fails cleanly when the exe is absent. exe = nullptr
// searches PATH. extra_args = nullptr -> "-c:v libx264 -preset veryfast -crf 18".
IMGUI_API ImGuiAppAVEncoder* ImGuiAppAV_CreateFfmpegEncoder(const char* ffmpeg_exe, const char* extra_args);

// Windows Media Foundation mp4 (H.264/HEVC), no external exe needed. Windows-only.
IMGUI_API ImGuiAppAVEncoder* ImGuiAppAV_CreateMfEncoder();

IMGUI_API void ImGuiAppAV_DestroyEncoder(ImGuiAppAVEncoder* encoder);

// Default resolution order used when a config passes encoder = null:
// ffmpeg-on-PATH -> QOI sequence. (MF must be chosen explicitly: lossy + driver-variant
// output is the wrong silent default for test artifacts.)
IMGUI_API ImGuiAppAVEncoder* ImGuiAppAV_CreateDefaultEncoder();
```

### Sidecar track: `<output>.avmeta`

Written by the recorder, not the provider, so every provider gets it uniformly.
Binary, WAL-flavored: fixed header (magic `IMAVMETA`, version, fps, start TSC + QPC Hz
for TSC->seconds conversion), then one record per frame:

```
u64 frame_index | u64 tsc | f64 time_sec | u32 user_size | user_size bytes
```

TSC is the *default* per-frame payload (always present); `user_size` covers the optional
arbitrary blob. A tiny `AppAVMetaDump(path)` debug helper prints it as TSV.

### Recorder (glue between app, backend capture, and provider)

```cpp
struct ImGuiAppRecorder;   // opaque

IMGUI_API ImGuiAppRecorder* AppRecordBegin(ImGuiApp* app, ImGuiAppAVEncoder* encoder /* null = default */, const ImGuiAppAVEncodeConfig* config);
IMGUI_API void              AppRecordSetFrameData(ImGuiAppRecorder* rec, const void* data, int size);   // this frame's blob; copied immediately
IMGUI_API bool              AppRecordIsActive(const ImGuiAppRecorder* rec);
IMGUI_API void              AppRecordEnd(ImGuiAppRecorder* rec);   // flush queue, Close provider, finalize sidecar
```

Threading: `WriteFrame` runs on a single encoder thread behind a bounded queue
(default depth 3). Queue-full policy is configurable: `Block` (benchmarks/tests: never
drop) or `DropNewest` (live capture: never stall the app). Drops are counted and
WAL-logged.

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
  float       Fps;                                    // pacer Fixed rate + encode PTS rate; default 60
  ImGuiAppAVEncoder* Encoder;                         // null = ImGuiAppAV_CreateDefaultEncoder()
  ImGuiAppWALLevel   WALLevel;                        // default Frame
  const char* TestFilter;                             // test-engine filter; null = all
  void (*RegisterTests)(ImGuiTestEngine* engine);     // required
};

// Runs to queue-empty (or abort), returns a ctest-ready exit code.
IMGUI_API int AppTestHarnessRun(ImGuiApp* app, const ImGuiAppTestHarnessConfig* config);
```

Per frame the harness: `AppPacerWait` (Fixed) -> frame id -> `ImGui::NewFrame` -> app frame ->
render -> `CaptureFrame` -> recorder (`WriteFrame` + sidecar) -> `PreSwap`/present/`PostSwap`.
The WAL's frame-id source is set, so every event-source line carries the same frame index
that names the video frame: *scrub the video to frame N, grep the WAL for `f:N`*.

Benchmarking falls out of the same artifacts: the sidecar's TSC deltas are exact
per-frame costs under a deterministic dt; the harness additionally emits
`<Name>.frametimes.csv` (frame_index, tsc_delta, ms) and prints p50/p95/p99/max.

## 6. Phasing

- **P1** — FrameID on ImGuiApp; WAL frame-id prefix; ImGuiAppPacer + `AppPacerWait`
  in both win32 run loops (message-pump loop calls it unconditionally).
- **P2** — Encoder seam; QOI-sequence + ffmpeg-pipe providers; recorder + sidecar;
  `CaptureFrame` for win32-vulkan; `Headless_Offscreen` for vulkan.
- **P3** — ImGuiAppTestHarness + tests_main migration; Media Foundation provider;
  per-viewport pacing (`AppPacerViewportShouldPresent` wired into the viewport
  present hooks); OpenGL3 capture.

## 7. Non-goals / constraints

- No upstream edits: everything sits at the applayer seam (backend vtable is ours).
- Audio: out of scope ("AV" here is frame video + data tracks; the sidecar format
  reserves record space via its version field if that ever changes).
- Live streaming, GPU-side encode (NVENC et al): possible later behind the same
  provider vtable; not in P1-P3.
- Resize during recording: recorder aborts with a WAL line rather than silently
  rescaling (fixed-size contract keeps every provider simple and diffs honest).
