// ImGuiAppAV: frame encode-to-video and run artifacts for ImGuiApp (docs/av-design.md).
// This is the SEAM only. Encoder implementations live in backends/imguiapp_impl_*.h and
// are wired by the app, mirroring how imgui apps wire imgui_impl_* platform/renderer
// backends; this header never references a provider.

#pragma once

#include "imguiapp.h"

//-----------------------------------------------------------------------------
// [SECTION] Timing
//-----------------------------------------------------------------------------

// What time the video claims. A video is honest about realtime only under Realtime.
typedef int ImGuiAppAVTimingMode;
enum ImGuiAppAVTimingMode_
{
  ImGuiAppAVTimingMode_Auto = 0,   // follow the pacer: Fixed pacer -> Constant, else Realtime
  ImGuiAppAVTimingMode_Constant,   // CFR: frame N plays at N/Fps (synthetic timeline; matches Fixed dt)
  ImGuiAppAVTimingMode_Realtime,   // VFR: PTS = FrameID.TimeSec (wall clock; a 50ms hitch plays as 50ms)
};

//-----------------------------------------------------------------------------
// [SECTION] Frames + encoder seam
//-----------------------------------------------------------------------------

// One captured frame. Produced by the platform backend's CaptureFrame; consumed by an
// encoder's WriteFrame. Pixels are valid only during the WriteFrame call.
struct ImGuiAppAVFrame
{
  int             Width;
  int             Height;
  int             PitchBytes;     // row stride; encoders must honor it
  const void*     Pixels;         // RGBA8
  ImGuiAppFrameID FrameID;
  const void*     UserData;       // optional per-frame blob (sidecar, never the video)
  int             UserDataSize;

  ImGuiAppAVFrame() { Width = 0; Height = 0; PitchBytes = 0; Pixels = nullptr; UserData = nullptr; UserDataSize = 0; }
};

struct ImGuiAppAVEncodeConfig
{
  const char*          OutputPath;    // container path, or directory for sequence providers
  float                Fps;           // Constant mode: the frame rate. Realtime mode: nominal rate hint only
  ImGuiAppAVTimingMode Timing;
  int                  Width;         // 0 = first frame's size (fixed thereafter; resize aborts recording)
  int                  Height;
  int                  BitrateKbps;   // hint; lossless providers ignore
  bool                 WriteSidecar;  // "<OutputPath>.avmeta" written by the recorder

  ImGuiAppAVEncodeConfig() { OutputPath = nullptr; Fps = 60.0f; Timing = ImGuiAppAVTimingMode_Auto; Width = 0; Height = 0; BitrateKbps = 0; WriteSidecar = true; }
};

// Encoder provider vtable. Implementations allocate themselves (Create* in their own
// backends/imguiapp_impl_*.h header) and free themselves via Destroy.
struct ImGuiAppAVEncoder
{
  const char* Name;
  bool        SupportsRealtimePts;   // provider can carry per-frame wall-clock PTS (true VFR)
  bool (*Open)(ImGuiAppAVEncoder* self, const ImGuiAppAVEncodeConfig* config);
  bool (*WriteFrame)(ImGuiAppAVEncoder* self, const ImGuiAppAVFrame* frame);   // PTS from frame->FrameID.TimeSec under Realtime
  void (*Close)(ImGuiAppAVEncoder* self);
  void (*Destroy)(ImGuiAppAVEncoder* self);
  void* UserData;                    // provider state

  ImGuiAppAVEncoder() { Name = nullptr; SupportsRealtimePts = false; Open = nullptr; WriteFrame = nullptr; Close = nullptr; Destroy = nullptr; UserData = nullptr; }
};

// Close (if open) then Destroy. Null-safe.
IMGUI_API void ImGuiAppAV_DestroyEncoder(ImGuiAppAVEncoder* encoder);

//-----------------------------------------------------------------------------
// [SECTION] Sidecar track (.avmeta)
//-----------------------------------------------------------------------------

// Typed-record stream written by the recorder (uniform across providers): fixed header
// (magic "IMAVMETA", version, fps, start TSC + QPC Hz), then {u32 type, u32 size, payload}.
typedef int ImGuiAppAVMetaRecordType;
enum ImGuiAppAVMetaRecordType_
{
  ImGuiAppAVMetaRecordType_Frame = 1,      // frame_index, tsc, time_sec, user blob
  ImGuiAppAVMetaRecordType_InputHdr,       // ImGuiAppInputLog layout (composition id, slot table); once per take
  ImGuiAppAVMetaRecordType_InputFrame,     // frame_index + one input-log frame (TempData + dt) + state hash
  ImGuiAppAVMetaRecordType_StateSnapshot,  // composition id + snapshottable-state bytes (ImGuiAppStateHistory layout)
  ImGuiAppAVMetaRecordType_AudioPcm,       // RESERVED in v1: frame_index + sample format header + PCM chunk (no producer yet)
};

//-----------------------------------------------------------------------------
// [SECTION] Recorder
//-----------------------------------------------------------------------------

// Glue between the app, the platform backend's CaptureFrame, and one encoder.
// WriteFrame runs on a single encoder thread behind a bounded queue.
struct ImGuiAppRecorder;   // opaque

typedef int ImGuiAppRecordQueuePolicy;
enum ImGuiAppRecordQueuePolicy_
{
  ImGuiAppRecordQueuePolicy_Block = 0,   // never drop (benchmarks/tests); app stalls when the queue is full
  ImGuiAppRecordQueuePolicy_DropNewest,  // never stall (live capture); drops counted + WAL-logged
};

// Always-on in-memory ring of the last N seconds (frames QOI-compressed on capture,
// plus sidecar records and input frames); encoded to disk only on dump.
struct ImGuiAppRingConfig
{
  float Seconds;       // ring span
  int   MaxMemoryMB;   // hard cap; oldest frames evicted when either bound binds
  float Fps;           // <= 0 (default) = keep EVERY frame; > 0 = explicit subsample opt-out of the encode-every-frame contract

  ImGuiAppRingConfig() { Seconds = 10.0f; MaxMemoryMB = 256; Fps = 0.0f; }
};

namespace ImGui
{
  // Print a sidecar as TSV to stdout (debug helper).
  IMGUI_API bool AppAVMetaDump(const char* avmeta_path);

  // Load a persisted run for reproduction: restore the snapshot, then AppInputReplay
  // (imguiapp.h) -- its divergence check pinpoints any non-determinism.
  IMGUI_API bool AppAVMetaReadInputLog(const char* avmeta_path, ImGuiAppInputLog* out_log);
  IMGUI_API bool AppAVMetaReadStateSnapshot(const char* avmeta_path, ImVector<char>* out_bytes, ImGuiID* out_composition_id);

  // encoder is REQUIRED (providers live in backends/imguiapp_impl_*.h; the core seam
  // cannot pick one). Fails (null) when the platform backend has no CaptureFrame.
  IMGUI_API ImGuiAppRecorder* AppRecordBegin(ImGuiApp* app, ImGuiAppAVEncoder* encoder, const ImGuiAppAVEncodeConfig* config);

  // Per-frame pump: capture -> queue for encode -> sidecar records. Call once per frame
  // between render and present (the double-buffered CaptureFrame makes the exact
  // position forgiving). Null-safe; no-op when inactive.
  IMGUI_API void              AppRecordPump(ImGuiAppRecorder* rec);
  IMGUI_API bool              AppRecordIsActive(const ImGuiAppRecorder* rec);
  IMGUI_API ImU64             AppRecordFrameCount(const ImGuiAppRecorder* rec);   // frames accepted this take (the video/sidecar ordinal); 0 on null
  IMGUI_API void              AppRecordSetQueuePolicy(ImGuiAppRecorder* rec, ImGuiAppRecordQueuePolicy policy, int depth /*= 3*/);
  IMGUI_API void              AppRecordEnd(ImGuiAppRecorder* rec);   // flush queue, Close provider, finalize sidecar, free rec

  // This frame's user blob (copied immediately; lands in the sidecar Frame record).
  IMGUI_API void AppRecordSetFrameData(ImGuiAppRecorder* rec, const void* data, int size);

  // Serialize the app's snapshottable state (ImGuiAppStateHistory byte layout) as this
  // frame's blob: video frame N <-> app state N; restoring the bytes IS time travel.
  IMGUI_API void AppRecordSnapshotState(ImGuiAppRecorder* rec, ImGuiApp* app);

  // Attach a live input log: the recorder serializes its frames into the sidecar as
  // they are recorded (keep calling AppInputRecord once per frame as usual).
  IMGUI_API void AppRecordAttachInputLog(ImGuiAppRecorder* rec, const ImGuiAppInputLog* log);

  IMGUI_API ImGuiAppRecorder* AppRecordBeginRing(ImGuiApp* app, ImGuiAppAVEncoder* encoder, const ImGuiAppAVEncodeConfig* config, const ImGuiAppRingConfig* ring);

  // Encode the ring's contents to disk NOW (assert hook, test failure, hotkey, user
  // code); reason lands in the WAL and the sidecar header. The ring keeps recording;
  // repeated dumps get "-2", "-3" path suffixes. When a ring recorder exists, the
  // IM_ASSERT sink (ImGuiAppAssertFail) dumps it with the failed expression as reason.
  IMGUI_API bool AppRecordDumpRing(ImGuiAppRecorder* rec, const char* reason);

} // namespace ImGui
