#pragma once

// [SECTION] AV recording types + interface (encode config, encoder provider vtable, ring/queue policy,
// captured frame, recorder). Public so clients can drive recording: supply an encoder provider, tune the
// encode/ring config, inspect/extend the recorder. Encoder IMPLEMENTATIONS live in backends/imguiapp_impl_*.h
// (wired like imgui_impl_* backends). The rest of the AV seam (decoder, meta-stream verify, AppRecord* API)
// is in imguiapp_internal.h. See docs/av-design.md.
//
// SELF-CONTAINED for AV-only consumers: ImGuiAppFrameID is defined below (ImGuiAppAVFrame holds one by
// value), so this header needs no include-order contract. ImGuiApp / ImGuiAppInputLog are held by pointer
// (forward-declared below); include imguiapp.h if you need their definitions.

#include "imgui.h"                        // ImVector, ImU32/ImU64, ImGuiID, ImGuiKey_NamedKey_COUNT
#include <thread>                         // ImGuiAppRecorder encoder thread
#include <mutex>                          // ImGuiAppRecorder encoder-thread mutex
#include <condition_variable>             // ImGuiAppRecorder bounded-queue signalling
#include <cstring>                        // memset (ImGuiAppRecorder ctor)

struct ImGuiApp;          // defined in imguiapp.h (held by pointer)
struct ImGuiAppInputLog;  // defined in imguiapp.h (held by pointer)

// Frame identity: one id per frame, taken at the top of OnDrawFrame. The correlation key across video
// frames, sidecar records, WAL lines, and test logs (docs/av-design.md). Defined HERE (not imguiapp.h)
// so this header is self-contained -- ImGuiAppAVFrame holds one by value.
struct ImGuiAppFrameID
{
  ImU64  FrameIndex; // monotonic from run start (not ImGui's frame count: survives context recreation)
  ImU64  Tsc;        // __rdtsc / platform equivalent at frame begin
  double TimeSec;    // QPC seconds since run start

  ImGuiAppFrameID() { FrameIndex = 0; Tsc = 0; TimeSec = 0.0; }
};

// One captured frame. Produced by the platform backend's CaptureFrame; consumed by an
// encoder's WriteFrame. Pixels are valid only during the WriteFrame call.
struct ImGuiAppAVFrame
{
  int             Width;
  int             Height;
  int             PitchBytes; // row stride; encoders must honor it
  const void*     Pixels;     // RGBA8
  ImGuiAppFrameID FrameID;
  const void*     UserData;   // optional per-frame blob (meta stream record, never visible pixels)
  int             UserDataSize;

  ImGuiAppAVFrame() { Width = 0; Height = 0; PitchBytes = 0; Pixels = nullptr; UserData = nullptr; UserDataSize = 0; }
};

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
  const char*          OutputPath;  // container path, or directory for sequence providers
  float                Fps;         // Constant mode: the frame rate. Realtime mode: nominal rate hint only
  ImGuiAppAVTimingMode Timing;
  int                  Width;       // 0 = first frame's size (fixed thereafter; resize aborts recording)
  int                  Height;
  int                  BitrateKbps; // hint; lossless providers ignore
                                    // Metadata lives IN the video: while recording, the meta record stream (40-byte
                                    // header first, then framed records in emission order) is chunked across the frames'
                                    // BOTTOM EmbedRows pixel rows as 4x4-pixel luma blocks (black 16 / white 235, read
                                    // threshold 128 -- survives lossy encode). Per frame: u32 magic 'IMIL' |
                                    // u32 chunk_size | chunk (the stream's next bytes, up to capacity) | u32 ImHashData
                                    // checksum (CRC32c). Records self-describe, so reassembly is chunk concatenation in
                                    // frame order; a large record (state snapshot) legitimately spans frames. The only
                                    // loss mode is a corrupt frame, which truncates the stream at that point on read.
                                    // Capacity per frame = floor(W/4) * floor(EmbedRows/4) / 8 - 12 bytes.
  int                  EmbedRows;   // reserved bottom rows; multiple of 4

  ImGuiAppAVEncodeConfig() { OutputPath = nullptr; Fps = 60.0f; Timing = ImGuiAppAVTimingMode_Auto; Width = 0; Height = 0; BitrateKbps = 0; EmbedRows = 32; }
};

// Encoder provider vtable. Implementations allocate themselves (Create* in their own
// backends/imguiapp_impl_*.h header) and free themselves via Destroy.
struct ImGuiAppAVEncoder
{
  const char* Name;
  bool        SupportsRealtimePts; // provider can carry per-frame wall-clock PTS (true VFR)
  bool (*Open)(ImGuiAppAVEncoder* self, const ImGuiAppAVEncodeConfig* config);
  bool (*WriteFrame)(ImGuiAppAVEncoder* self, const ImGuiAppAVFrame* frame);   // PTS from frame->FrameID.TimeSec under Realtime
  void (*Close)(ImGuiAppAVEncoder* self);
  void (*Destroy)(ImGuiAppAVEncoder* self);
  void* UserData;                    // provider state

  ImGuiAppAVEncoder() { Name = nullptr; SupportsRealtimePts = false; Open = nullptr; WriteFrame = nullptr; Close = nullptr; Destroy = nullptr; UserData = nullptr; }
};

// Reconstructed meta-stream header (magic "IMAVMETA", version, fps, start TSC + QPC Hz).
// Field-exact contract with the recorder + parsers; a version bump follows any change.
struct ImGuiAppAVMetaHeader
{
  char  Magic[8]; // "IMAVMETA"
  ImU32 Version;  // 1
  float Fps;
  ImU64 StartTsc;
  ImU64 QpcHz;
  ImU64 StartQpc;
};

typedef int ImGuiAppRecordQueuePolicy;
enum ImGuiAppRecordQueuePolicy_
{
  ImGuiAppRecordQueuePolicy_Block = 0,   // never drop (benchmarks/tests); app stalls when the queue is full
  ImGuiAppRecordQueuePolicy_DropNewest,  // never stall (live capture); drops counted + WAL-logged
};

// Always-on in-memory ring of the last N seconds (frames QOI-compressed on capture,
// plus their meta records); the stream is chunked across the frames at dump time.
struct ImGuiAppRingConfig
{
  float Seconds;     // ring span
  int   MaxMemoryMB; // hard cap; oldest frames evicted when either bound binds
  float Fps;         // <= 0 (default) = keep EVERY frame; > 0 = explicit subsample opt-out of the encode-every-frame contract

  ImGuiAppRingConfig() { Seconds = 10.0f; MaxMemoryMB = 256; Fps = 0.0f; }
};

// One queued frame, pixels owned (CaptureFrame pixels are only valid until the next capture).
struct ImGuiAppAVJob
{
  int             Width;
  int             Height;
  ImGuiAppFrameID FrameID;
  ImVector<char>  Pixels; // tightly packed RGBA (pitch collapsed at copy)
};

// One ring entry: QOI-compressed pixels + the frame's already-framed meta records.
struct ImGuiAppAVRingEntry
{
  int             Width;
  int             Height;
  ImGuiAppFrameID FrameID;
  ImU32           StateHash;   // this frame's AppStateHash; the dump recomputes the chain over survivors
  int             ChainOffset; // byte offset of the IoFrame chain field within MetaRecords; -1 = none
  ImVector<char>  Qoi;
  ImVector<char>  MetaRecords;

  ImGuiAppAVRingEntry() { Width = 0; Height = 0; StateHash = 0; ChainOffset = -1; }
};

// Glue between the app, the platform backend's CaptureFrame, and one encoder. WriteFrame runs on a
// single encoder thread behind a bounded queue. Held by ImGuiApp::Recorder; methods defined in imguiapp.cpp.
struct ImGuiAppRecorder
{
  ImGuiApp*              App;
  ImGuiAppAVEncoder*     Encoder;
  ImGuiAppAVEncodeConfig Config;            // Timing resolved (never Auto) before the provider sees it
  char                   OutputPath[512];
  bool                   Active;
  bool                   EncoderOpen;
  int                    FixedWidth;        // 0 until the first captured frame locks the size
  int                    FixedHeight;
  ImU64                  AcceptedFrames;    // frames emitted this take (video frame ordinal), placeholders included
  ImU64                  LastEmittedIndex;  // FrameID.FrameIndex of the last emitted frame; 0 = none. Gap fill + duplicate guard
  bool                   NoSizeWarned;      // pre-first-capture miss with no size to synthesize at: WAL once
  ImVector<char>         PlaceholderPixels; // pause-glyph frame at the locked size, built lazily

  // Meta record stream (normal mode): the video is the only metadata store. Pending
  // bytes drain into each frame's pixel strip as chunks; the ring builds its stream
  // at dump time from per-entry records instead.
  ImGuiAppAVMetaHeader MetaHeader;
  ImVector<char>       MetaPending;       // header + framed records awaiting a strip
  int                  MetaPendingCursor; // first unstamped byte
  ImVector<char>       IdentityRecord;    // framed Identity, built at Begin (ring dumps re-emit it)
  ImU32                SchemaHash;        // Identity's schema hash: the io hash chain's seed
  ImU32                IoChain;           // running chain: chain_k = ImHashData(&state_hash_k, 4, chain_{k-1})
  ImU32                StreamDigest;      // incremental ImHashData over every logical-stream byte queued
  ImU64                StreamBytes;       // logical-stream byte count (header + records)
  bool                 EmitDigestNext;    // set by AppRecordEnd: the next emitted frame carries the Digest

  // Per-frame pending data (set during the frame, consumed by the pump)
  ImVector<char> PendingBlob;
  bool           PendingBlobSet;
  ImVector<char> PendingSnapshotRecord; // framed StateSnapshot record, if requested this frame

  // Attached input log (caller-owned): new frames since LastInputCount serialize each pump
  const ImGuiAppInputLog* InputLog;
  int                     LastInputCount;
  ImGuiID                 InputHdrComposition; // composition the last written InputHdr described
  ImVector<char>          InputHdrRecord;      // latest framed InputHdr (ring rewrites it at dump)

  // Raw-io capture shadow (IoFrame records): key downs as of the previous pump; the
  // first pump emits currently-down keys as transitions.
  bool IoShadowValid;
  bool IoKeyDown[ImGuiKey_NamedKey_COUNT];

  // Strip stamping scratch + one-shot WAL flags
  ImVector<char> EmbedStream;         // framed chunk stamped into the strip
  bool           EmbedTooShortWarned; // frame shorter than EmbedRows: WAL once

  // Encoder thread + bounded queue (normal mode only)
  std::thread               Thread;
  std::mutex                Mutex;
  std::condition_variable   CvPush;
  std::condition_variable   CvPop;
  ImVector<ImGuiAppAVJob*>  Queue; // FIFO; front = index 0
  int                       QueueDepth;
  ImGuiAppRecordQueuePolicy QueuePolicy;
  bool                      ThreadStop;
  bool                      EncodeFailed;
  ImU64                     DroppedFrames;

  // Ring mode
  bool                           IsRing;
  ImGuiAppRingConfig             Ring;
  ImVector<ImGuiAppAVRingEntry*> RingEntries; // FIFO; front = oldest
  size_t                         RingBytes;
  double                         RingLastAcceptSec;
  int                            DumpCount;

  ImGuiAppRecorder()
  {
    App              = nullptr;
    Encoder          = nullptr;
    OutputPath[0]    = 0;
    Active           = false;
    EncoderOpen      = false;
    FixedWidth       = 0;
    FixedHeight      = 0;
    AcceptedFrames   = 0;
    LastEmittedIndex = 0;
    NoSizeWarned     = false;
    memset(&MetaHeader, 0, sizeof(MetaHeader));
    MetaPendingCursor   = 0;
    PendingBlobSet      = false;
    InputLog            = nullptr;
    LastInputCount      = 0;
    InputHdrComposition = 0;
    SchemaHash          = 0;
    IoChain             = 0;
    StreamDigest        = 0;
    StreamBytes         = 0;
    EmitDigestNext      = false;
    IoShadowValid       = false;
    memset(IoKeyDown, 0, sizeof(IoKeyDown));
    EmbedTooShortWarned = false;
    QueueDepth          = 3;
    QueuePolicy         = ImGuiAppRecordQueuePolicy_Block;
    ThreadStop          = false;
    EncodeFailed        = false;
    DroppedFrames       = 0;
    IsRing              = false;
    RingBytes           = 0;
    RingLastAcceptSec   = -1e300;
    DumpCount           = 0;
  }
};
