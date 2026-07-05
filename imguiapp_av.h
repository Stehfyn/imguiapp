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
  int             PitchBytes; // row stride; encoders must honor it
  const void*     Pixels;     // RGBA8
  ImGuiAppFrameID FrameID;
  const void*     UserData;   // optional per-frame blob (meta stream record, never visible pixels)
  int             UserDataSize;

  ImGuiAppAVFrame() { Width = 0; Height = 0; PitchBytes = 0; Pixels = nullptr; UserData = nullptr; UserDataSize = 0; }
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


//-----------------------------------------------------------------------------
// [SECTION] Meta stream (embedded in the video)
//-----------------------------------------------------------------------------

// Typed-record stream written by the recorder (uniform across providers): fixed header
// (magic "IMAVMETA", version, fps, start TSC + QPC Hz), then {u32 type, u32 size, payload}.
typedef int ImGuiAppAVMetaRecordType;
enum ImGuiAppAVMetaRecordType_
{
  ImGuiAppAVMetaRecordType_Frame = 1,      // frame_index, tsc, time_sec, user blob
  ImGuiAppAVMetaRecordType_InputHdr,       // ImGuiAppInputLog layout (composition id, slot table); once per take. OPT-IN (AppRecordAttachInputLog)
  ImGuiAppAVMetaRecordType_InputFrame,     // frame_index + one input-log frame (TempData + dt) + state hash. OPT-IN derived checkpoint
  ImGuiAppAVMetaRecordType_StateSnapshot,  // composition id + snapshottable-state bytes (ImGuiAppStateHistory layout)
  // Raw input, recorded EVERY frame by default (the source events; O(1) per frame):
  // u64 tick | f32 mouse_x | f32 mouse_y (main-viewport-relative) | u8 mouse_buttons |
  // f32 wheel | f32 wheel_h | u32 state_hash (AppStateHash) |
  // u32 chain (chain_k = ImHashData(&state_hash_k, 4, chain_{k-1}), seeded by the
  // Identity schema hash: the hash SEQUENCE is reorder/splice-evident; ring dumps
  // recompute it over the surviving entries) |
  // u16 key_transition_count | {u16 imgui_key, u8 down}* | u16 char_count | {u16 utf16_unit}*
  ImGuiAppAVMetaRecordType_IoFrame,
  // Take identity, emitted ONCE immediately after the stream header (before any
  // Frame/IoFrame). Replay classifies hash mismatches against it: identity differs
  // from the replaying build -> declared version/schema mismatch (refuse before
  // replay); identity matches but the per-frame hash chain diverges at frame k ->
  // nondeterminism or corruption at k. Payload:
  // u32 applayer_version_num | u32 imgui_version_num | u32 composition_id |
  // u32 schema_hash (ImHashData over the snapshottable slot table: id + size +
  // temp_offset + temp_size per entry, StorageEntries order -- the layout state
  // hashes and snapshots depend on) | u32 embed_rows | u16 block_size | u16 reserved
  ImGuiAppAVMetaRecordType_Identity,
  // Take completeness proof, the stream's FINAL record (AppRecordEnd / each ring dump):
  // u64 stream_bytes (all logical-stream bytes preceding this record, header included) |
  // u32 digest (ImHashData over exactly those bytes, seed 0). Presence = complete take;
  // absence = truncation (crash) -- WAL-style honesty made checkable. Rides the final
  // frame's chunk: a take whose last frame lacks chunk room truncates it like any tail.
  ImGuiAppAVMetaRecordType_Digest,
  ImGuiAppAVMetaRecordType_AudioPcm,       // RESERVED: frame_index + sample format header + PCM chunk (no producer yet)
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
// plus their meta records); the stream is chunked across the frames at dump time.
struct ImGuiAppRingConfig
{
  float Seconds;     // ring span
  int   MaxMemoryMB; // hard cap; oldest frames evicted when either bound binds
  float Fps;         // <= 0 (default) = keep EVERY frame; > 0 = explicit subsample opt-out of the encode-every-frame contract

  ImGuiAppRingConfig() { Seconds = 10.0f; MaxMemoryMB = 256; Fps = 0.0f; }
};

// Result of AppAVMetaVerify: the full integrity ladder recomputed from a reconstructed
// meta stream. CorruptFrames is extraction-level knowledge (the stream walk cannot see
// frames the extractor dropped) -- callers with an extractor count overwrite it.
struct ImGuiAppAVStreamStats
{
  int   Frames;
  int   IoFrames;
  int   InputHdrs;
  int   InputFrames;
  int   Snapshots;
  int   Identities;
  ImU64 FirstTick;
  ImU64 LastTick;
  int   TickGaps;
  bool  ChainOk;
  int   ChainDivergesAt; // io-frame ordinal of the first divergence; -1 = none
  int   DigestState;     // 0 ok, 1 missing (truncated take), 2 mismatch (corruption)
  int   CorruptFrames;

  ImGuiAppAVStreamStats() { Frames = 0; IoFrames = 0; InputHdrs = 0; InputFrames = 0; Snapshots = 0; Identities = 0; FirstTick = 0; LastTick = 0; TickGaps = 0; ChainOk = false; ChainDivergesAt = -1; DigestState = 1; CorruptFrames = 0; }
};

//-----------------------------------------------------------------------------
// [SECTION] Run index (F62): tick-keyed landing over a reconstructed meta stream
//-----------------------------------------------------------------------------
// One walk of the reconstructed meta buffer (structurally the traversal AppAVMetaVerify
// performs) records each record's tick and payload offset instead of only counting them.
// The result is the playback debugger's index (docs/playback-debugger-design.md section 3):
// no new byte format, same records, a richer landing. All offsets index the buffer the
// index owns; the shipped single-record readers (AppAVMetaRead*) consume from there.

// Decoded Identity record: the trust gate for state reconstruction (F64). A reconstruction
// app is legal only when its composition id and schema hash equal these.
struct ImGuiAppRunMeta
{
  ImU32   ApplayerVersion;
  ImU32   ImguiVersion;
  ImGuiID CompositionID;
  ImU32   SchemaHash;
  ImU32   EmbedRows;
  ImU16   BlockSize;

  ImGuiAppRunMeta() { ApplayerVersion = 0; ImguiVersion = 0; CompositionID = 0; SchemaHash = 0; EmbedRows = 0; BlockSize = 0; }
};

// One tick (FirstTick..LastTick). The Frame spine plus byte offsets to THIS tick's
// IoFrame / InputFrame / StateSnapshot record payloads (-1 when the tick has none).
struct ImGuiAppRunTick
{
  ImU64  Tick;           // == Frame/IoFrame frame_index (the single correlation key)
  ImU64  Tsc;            // Frame.tsc
  double TimeSec;        // Frame.time_sec (PTS; also QOI index.tsv)
  int    FrameImage;     // frame ordinal for decode (mp4 sample / QOI NNNNNN); -1 = none
  int    IoOffset;       // payload offset of this tick's IoFrame record (-1 if none)
  ImU32  StateHash;      // IoFrame.state_hash (recorded fingerprint)
  ImU32  Chain;          // IoFrame.chain recomputed from the Identity seed (divergence lives here)
  int    InputOffset;    // InputFrame payload offset (-1 if not opt-in this tick)
  int    SnapshotOffset; // StateSnapshot payload offset at this tick (-1 if none)

  ImGuiAppRunTick() { Tick = 0; Tsc = 0; TimeSec = 0.0; FrameImage = -1; IoOffset = -1; StateHash = 0; Chain = 0; InputOffset = -1; SnapshotOffset = -1; }
};

// F62 build product: the per-tick index, the snapshot list, and the verify ladder, all from
// ONE walk. Heap-owned by AppRunOpen; AppRunClose frees. Opaque to the snapshot contract.
struct ImGuiAppRunIndex
{
  ImVector<char>            Meta;          // owned copy of the reconstructed meta buffer (offsets index into it)
  ImGuiAppAVMetaHeader      Header;        // fps + start clocks
  ImGuiAppRunMeta           Identity;      // decoded Identity (composition_id, schema_hash, versions)
  ImVector<ImGuiAppRunTick> Ticks;         // FirstTick..LastTick in emission order
  ImVector<int>             SnapshotTicks; // ascending tick-array indices where SnapshotOffset>=0 (nearest lookup)
  ImGuiAppAVStreamStats     Stats;         // == AppAVMetaVerify output over the same bytes
};

namespace ImGui
{
  // Close (if open) then Destroy any provider's encoder via its vtable. Null-safe.
  IMGUI_API void AppAVDestroyEncoder(ImGuiAppAVEncoder* encoder);

  // Recompute the integrity ladder over a reconstructed meta stream: tick contiguity
  // (Frame records), io completeness (IoFrames == Frames), hash chain from the
  // Identity seed, end-of-stream digest. True only when every criterion holds.
  IMGUI_API bool AppAVMetaVerify(const void* meta, int meta_size, ImGuiAppAVStreamStats* out_stats);

  // Memory-stream parsers over a reconstructed meta stream (40-byte header + framed
  // records). Extraction from a recording is per-backend:
  // ImGuiApp_ImplLibav_ExtractEmbeddedMeta (mp4), ImGuiApp_ImplQoi_ExtractEmbeddedMeta
  // (frame sequence). A truncated tail parses as end-of-stream.
  IMGUI_API bool AppAVMetaDump(const void* meta, int meta_size);   // TSV to stdout (debug helper)

  // Reproduction: restore the snapshot, then AppInputReplay (imguiapp.h) -- its
  // divergence check pinpoints any non-determinism.
  IMGUI_API bool AppAVMetaReadInputLog(const void* meta, int meta_size, ImGuiAppInputLog* out_log);
  IMGUI_API bool AppAVMetaReadStateSnapshot(const void* meta, int meta_size, ImVector<char>* out_bytes, ImGuiID* out_composition_id);

  // F62 loader/index. Build the tick index (docs/playback-debugger-design.md section 3)
  // from a reconstructed meta buffer -- ONE linear walk reusing the same TLV traversal
  // AppAVMetaVerify performs, landing each record's tick + payload offset. The path->buffer
  // step is the per-backend extractor (ImGuiApp_Impl{Libav,Qoi}_ExtractEmbeddedMeta), same
  // as the harness's own VerifyRecording: this core seam never names a provider. Returns a
  // heap index (AppRunClose frees) or null on a bad/absent header.
  IMGUI_API ImGuiAppRunIndex*      AppRunOpen(const void* meta, int meta_size);
  IMGUI_API void                   AppRunClose(ImGuiAppRunIndex* run);
  IMGUI_API int                    AppRunTickCount(const ImGuiAppRunIndex* run);          // == Ticks.Size; 0 on null
  IMGUI_API const ImGuiAppRunTick* AppRunTickAt(const ImGuiAppRunIndex* run, int i);      // null when out of range

  // encoder is REQUIRED (providers live in backends/imguiapp_impl_*.h; the core seam
  // cannot pick one). Fails (null) when the platform backend has no CaptureFrame.
  IMGUI_API ImGuiAppRecorder* AppRecordBegin(ImGuiApp* app, ImGuiAppAVEncoder* encoder, const ImGuiAppAVEncodeConfig* config);

  // Per-frame pump: capture -> queue for encode -> meta stream chunk. Call once per frame
  // between render and present (the double-buffered CaptureFrame makes the exact
  // position forgiving). Null-safe; no-op when inactive.
  IMGUI_API void              AppRecordPump(ImGuiAppRecorder* rec);
  IMGUI_API bool              AppRecordIsActive(const ImGuiAppRecorder* rec);
  IMGUI_API ImU64             AppRecordFrameCount(const ImGuiAppRecorder* rec);   // frames accepted this take (the video frame ordinal); 0 on null
  IMGUI_API void              AppRecordSetQueuePolicy(ImGuiAppRecorder* rec, ImGuiAppRecordQueuePolicy policy, int depth /*= 3*/);
  IMGUI_API void              AppRecordEnd(ImGuiAppRecorder* rec);   // flush queue, Close provider, free rec

  // This frame's user blob (copied immediately; lands in the meta stream Frame record).
  IMGUI_API void AppRecordSetFrameData(ImGuiAppRecorder* rec, const void* data, int size);

  // Serialize the app's snapshottable state (ImGuiAppStateHistory byte layout) as this
  // frame's blob: video frame N <-> app state N; restoring the bytes IS time travel.
  IMGUI_API void AppRecordSnapshotState(ImGuiAppRecorder* rec, ImGuiApp* app);

  // OPT-IN derived checkpoint layer: raw io records by default (IoFrame); attaching a
  // live input log ADDITIONALLY serializes its TempData frames (InputHdr/InputFrame)
  // into the stream, enabling render-free replay + per-control divergence attribution.
  // Cost is O(sum of TempData) per frame -- attach deliberately. Keep calling
  // AppInputRecord once per frame as usual.
  IMGUI_API void AppRecordAttachInputLog(ImGuiAppRecorder* rec, const ImGuiAppInputLog* log);

  IMGUI_API ImGuiAppRecorder* AppRecordBeginRing(ImGuiApp* app, ImGuiAppAVEncoder* encoder, const ImGuiAppAVEncodeConfig* config, const ImGuiAppRingConfig* ring);

  // Encode the ring's contents to disk NOW (assert hook, test failure, hotkey, user
  // code); reason lands in the WAL and a stream marker record. The ring keeps recording;
  // repeated dumps get "-2", "-3" path suffixes. When a ring recorder exists, the
  // IM_ASSERT sink (ImGuiAppAssertFail) dumps it with the failed expression as reason.
  IMGUI_API bool AppRecordDumpRing(ImGuiAppRecorder* rec, const char* reason);

  // Assert forensics (F15): dump every live ring recorder (each auto-registered by AppRecordBeginRing)
  // with `reason`. ImGuiAppAssertFail calls this before exit so the flight recording lands beside the
  // assert WAL. Returns the number of rings successfully dumped.
  IMGUI_API int AppDumpAssertRings(const char* reason);

} // namespace ImGui
