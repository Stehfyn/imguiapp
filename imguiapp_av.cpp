// dear imgui app, v0.5.0 WIP
// (recorder + encoder + decoder/playback)

// ImGuiAppAV seam implementation: recorder, encoder thread, meta stream writer/parsers, flight-recorder
// ring (recorder types in imguiapp.h, meta stream in imguiapp_internal.h; docs/designs.md (av-design)).
//
// Index of this file (search for "[SECTION]"):
// [SECTION] Clocks + meta stream primitives
// [SECTION] Recorder state
// [SECTION] Encoder thread + queue
// [SECTION] Meta record builders
// [SECTION] Embedded metadata (pixel strip)
// [SECTION] Per-frame pump
// [SECTION] Public API: begin/end, blobs, ring
// [SECTION] Meta-only run recorder (F70)
// [SECTION] Meta stream parsers
// [SECTION] Run index (F62)
// [SECTION] State-at-tick (F64)

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imguiapp.h"
#ifndef IMGUI_DISABLE
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495) // [Static Analyzer] uninitialized member (type.6); memset ctors
#endif
#include "imguiapp_internal.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>               // strtoull (WAL command-tick parse)
#ifndef IMGUIAPP_DISABLE_DEFAULT_THREAD_FUNCS
#include <mutex>                 // default ImGuiAppThreadFuncs impl
#include <condition_variable>
#endif
#include <thread>                // capture drain backoff + default ImGuiAppThreadFuncs impl
#include <chrono>                // std::chrono (recorder thread timing)

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>             // QueryPerformanceCounter/Frequency (recorder clocks)
#include <intrin.h>              // __rdtsc (stream clock domain)
#else
#include <time.h>                // clock_gettime (stream clock domain)
#endif

//-----------------------------------------------------------------------------
// [SECTION] Clocks + meta stream primitives
//-----------------------------------------------------------------------------

static ImU64 AvClockTsc()
{
#ifdef _WIN32
    return (ImU64)__rdtsc();
#else
    // Must share a domain with AppClockTsc (imguiapp.cpp) -- stream StartTsc and
    // FrameID.Tsc are compared: both fall back to CLOCK_MONOTONIC nanoseconds.
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ImU64)ts.tv_sec * 1000000000ull + (ImU64)ts.tv_nsec;
#endif
}

static ImU64 AvClockHz()
{
#ifdef _WIN32
    LARGE_INTEGER f;
    ::QueryPerformanceFrequency(&f);
    return (ImU64)f.QuadPart;
#else
    return 1000000000ull;
#endif
}

static ImU64 AvClockCounter()
{
#ifdef _WIN32
    LARGE_INTEGER c;
    ::QueryPerformanceCounter(&c);
    return (ImU64)c.QuadPart;
#else
    return (ImU64)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}

// Stream header (ImGuiAppAVMetaHeader) is declared in imguiapp.h -- field-exact
// contract shared with the parsers below and the F62 run index.
static const char* AV_META_MAGIC = "IMAVMETA";

// One framed record: {u32 type, u32 size, payload}. Append-only; buf may already hold records.
static void AvRecordAppend(ImVector<char>* buf, ImU32 type, const void* payload, ImU32 size)
{
    const int base = buf->Size;
    buf->resize(base + (int)(sizeof(ImU32) * 2 + size));
    char* dst = buf->Data + base;
    memcpy(dst, &type, sizeof(type));
    dst += sizeof(ImU32);
    memcpy(dst, &size, sizeof(size));
    dst += sizeof(ImU32);
    if (size > 0)
        memcpy(dst, payload, size);
}

// Little scratch writer for multi-piece payloads.
static void AvPut(ImVector<char>* buf, const void* src, int size)
{
    const int base = buf->Size;
    buf->resize(base + size);
    memcpy(buf->Data + base, src, (size_t)size);
}

//-----------------------------------------------------------------------------
// [SECTION] Recorder state
//-----------------------------------------------------------------------------

IMGUI_API void ImGui::AppAVDestroyEncoder(ImGuiAppAVEncoder* encoder)
{
    if (encoder == nullptr)
        return;
    // Providers must make Close idempotent; the recorder closes exactly once after the
    // last WriteFrame, so a second Close here on an already-closed encoder is a no-op.
    if (encoder->CloseFn != nullptr)
        encoder->CloseFn(encoder);
    if (encoder->DestroyFn != nullptr)
        encoder->DestroyFn(encoder);
}

//-----------------------------------------------------------------------------
// [SECTION] Encoder thread + queue
//-----------------------------------------------------------------------------

// Default ImGuiAppThreadFuncs: std::thread behind void* handles (docs/house-style-audit.md Δ2).
#ifndef IMGUIAPP_DISABLE_DEFAULT_THREAD_FUNCS
static void* AppThreadCreateDefault(void (*fn)(void*), void* arg) { return IM_NEW(std::thread)(fn, arg); }
static void  AppThreadJoinDefault(void* t)                        { std::thread* th = (std::thread*)t; if (th->joinable()) th->join(); IM_DELETE(th); }
static void* AppMutexCreateDefault()                              { return IM_NEW(std::mutex)(); }
static void  AppMutexDestroyDefault(void* m)                      { IM_DELETE((std::mutex*)m); }
static void  AppMutexLockDefault(void* m)                         { ((std::mutex*)m)->lock(); }
static void  AppMutexUnlockDefault(void* m)                       { ((std::mutex*)m)->unlock(); }
static void* AppCondCreateDefault()                               { return IM_NEW(std::condition_variable)(); }
static void  AppCondDestroyDefault(void* cv)                      { IM_DELETE((std::condition_variable*)cv); }
static void  AppCondWaitDefault(void* cv, void* m)
{
    std::unique_lock<std::mutex> lock(*(std::mutex*)m, std::adopt_lock);
    ((std::condition_variable*)cv)->wait(lock);
    lock.release();   // caller keeps the re-acquired mutex
}
static void  AppCondSignalDefault(void* cv)                       { ((std::condition_variable*)cv)->notify_one(); }
static void  AppCondBroadcastDefault(void* cv)                    { ((std::condition_variable*)cv)->notify_all(); }

static const ImGuiAppThreadFuncs GAppThreadFuncsDefault =
{
    AppThreadCreateDefault, AppThreadJoinDefault,
    AppMutexCreateDefault, AppMutexDestroyDefault, AppMutexLockDefault, AppMutexUnlockDefault,
    AppCondCreateDefault, AppCondDestroyDefault, AppCondWaitDefault, AppCondSignalDefault, AppCondBroadcastDefault,
};
#endif // #ifndef IMGUIAPP_DISABLE_DEFAULT_THREAD_FUNCS

// Reads the process-state slot; null resolves to the std::thread default table (unless stripped).
static const ImGuiAppThreadFuncs* AppThreadFuncs()
{
#ifndef IMGUIAPP_DISABLE_DEFAULT_THREAD_FUNCS
    return AppState().ThreadFuncs != nullptr ? AppState().ThreadFuncs : &GAppThreadFuncsDefault;
#else
    return AppState().ThreadFuncs;
#endif
}

IMGUI_API void ImGui::SetAppThreadFuncs(const ImGuiAppThreadFuncs* funcs)
{
#ifndef IMGUIAPP_DISABLE_DEFAULT_THREAD_FUNCS
    AppState().ThreadFuncs = funcs;
#else
    AppState().ThreadFuncs = funcs;
#endif
}

// Opaque body of ImGuiAppRecorder::Thread: handles from the active ImGuiAppThreadFuncs.
struct ImGuiAppRecorderThread
{
    void* Worker;
    void* Mutex;   // guards rec->Queue / QueueDepth / QueuePolicy / ThreadStop
    void* CvPush;  // signalled when the queue has room
    void* CvPop;   // signalled when the queue has work
};

static void AvEncoderThread(void* arg)
{
    ImGuiAppRecorder* rec = (ImGuiAppRecorder*)arg;
    const ImGuiAppThreadFuncs* tf = AppThreadFuncs();
    for (;;)
    {
        ImGuiAppAVQueuedFrame* job = nullptr;
        tf->MutexLockFn(rec->Thread->Mutex);
        while (rec->Queue.Size == 0 && !rec->ThreadStop)
            tf->CondWaitFn(rec->Thread->CvPop, rec->Thread->Mutex);
        if (rec->Queue.Size == 0 && rec->ThreadStop)
        {
            tf->MutexUnlockFn(rec->Thread->Mutex);
            return;
        }
        job = rec->Queue.Data[0];
        rec->Queue.erase(rec->Queue.begin());
        tf->CondSignalFn(rec->Thread->CvPush);
        tf->MutexUnlockFn(rec->Thread->Mutex);

        ImGuiAppAVFrame frame;
        frame.Width = job->Width;
        frame.Height = job->Height;
        frame.PitchBytes = job->Width * 4;
        frame.Pixels = job->Pixels.Data;
        frame.FrameID = job->FrameID;
        if (!rec->Encoder->WriteFrameFn(rec->Encoder, &frame))
            rec->EncodeFailed = true;   // sticky; reported at AppRecordEnd
        IM_DELETE(job);
    }
}

// Push with the configured full-queue policy. Returns false when the frame was dropped.
static bool AvQueuePush(ImGuiAppRecorder* rec, ImGuiAppAVQueuedFrame* job)
{
    const ImGuiAppThreadFuncs* tf = AppThreadFuncs();
    tf->MutexLockFn(rec->Thread->Mutex);
    if (rec->QueuePolicy == ImGuiAppRecordQueuePolicy_Block)
    {
        while (rec->Queue.Size >= rec->QueueDepth && !rec->ThreadStop)
            tf->CondWaitFn(rec->Thread->CvPush, rec->Thread->Mutex);
        if (rec->ThreadStop)
        {
            tf->MutexUnlockFn(rec->Thread->Mutex);
            IM_DELETE(job);
            return false;
        }
    }
    else if (rec->Queue.Size >= rec->QueueDepth)
    {
        rec->DroppedFrames++;
        tf->MutexUnlockFn(rec->Thread->Mutex);
        IM_DELETE(job);
        return false;
    }
    rec->Queue.push_back(job);
    tf->CondSignalFn(rec->Thread->CvPop);
    tf->MutexUnlockFn(rec->Thread->Mutex);
    return true;
}

//-----------------------------------------------------------------------------
// [SECTION] Meta record builders
//-----------------------------------------------------------------------------

// Frame record payload: u64 frame_index | u64 tsc | f64 time_sec | u32 user_size | user bytes.
static void AvBuildFrameRecord(ImVector<char>* out, const ImGuiAppFrameID* id, const void* user, ImU32 user_size)
{
    ImVector<char> payload;
    AvPut(&payload, &id->FrameIndex, sizeof(ImU64));
    AvPut(&payload, &id->Tsc, sizeof(ImU64));
    AvPut(&payload, &id->TimeSec, sizeof(double));
    AvPut(&payload, &user_size, sizeof(ImU32));
    if (user_size > 0)
        AvPut(&payload, user, (int)user_size);
    AvRecordAppend(out, ImGuiAppAVMetaRecordType_Frame, payload.Data, (ImU32)payload.Size);
}

// InputHdr payload: u32 composition_id | u32 frame_size | u32 slot_count | {u32 id, s32 offset, s32 size}*.
static void AvBuildInputHdrRecord(ImVector<char>* out, const ImGuiAppInputLog* log)
{
    ImVector<char> payload;
    const ImU32 comp = (ImU32)log->CompositionID;
    const ImU32 frame_size = (ImU32)log->FrameSize;
    const ImU32 slots = (ImU32)log->SlotIds.Size;
    AvPut(&payload, &comp, sizeof(ImU32));
    AvPut(&payload, &frame_size, sizeof(ImU32));
    AvPut(&payload, &slots, sizeof(ImU32));
    for (int s = 0; s < log->SlotIds.Size; s++)
    {
        const ImU32 id = (ImU32)log->SlotIds.Data[s];
        const ImS32 off = (ImS32)log->SlotOffsets.Data[s];
        const ImS32 size = (ImS32)log->SlotSizes.Data[s];
        AvPut(&payload, &id, sizeof(ImU32));
        AvPut(&payload, &off, sizeof(ImS32));
        AvPut(&payload, &size, sizeof(ImS32));
    }
    out->clear();
    AvRecordAppend(out, ImGuiAppAVMetaRecordType_InputHdr, payload.Data, (ImU32)payload.Size);
}

// InputFrame payload: u64 frame_index | u32 state_hash | frame_size bytes (dt + TempData slots).
static void AvBuildInputFrameRecord(ImVector<char>* out, ImU64 frame_index, ImU32 state_hash, const char* frame_bytes, int frame_size)
{
    ImVector<char> payload;
    AvPut(&payload, &frame_index, sizeof(ImU64));
    AvPut(&payload, &state_hash, sizeof(ImU32));
    AvPut(&payload, frame_bytes, frame_size);
    AvRecordAppend(out, ImGuiAppAVMetaRecordType_InputFrame, payload.Data, (ImU32)payload.Size);
}

// StateSnapshot payload: u32 composition_id | u64 frame_index | u32 slot_count |
// {u32 id, s32 size}* | state bytes (slots concatenated in StorageEntries order).
static bool AvBuildStateSnapshotRecord(ImVector<char>* out, ImGuiApp* app, ImU64 frame_index)
{
    ImVector<char> payload;
    const ImU32 comp = (ImU32)ImGui::GetAppCompositionID(app);
    AvPut(&payload, &comp, sizeof(ImU32));
    AvPut(&payload, &frame_index, sizeof(ImU64));

    ImU32 slots = 0;
    for (int i = 0; i < app->StorageEntries.Size; i++)
        if (app->StorageEntries.Data[i].Size > 0 && app->StorageEntries.Data[i].Ptr != nullptr)
            slots++;
    if (slots == 0)
        return false;
    AvPut(&payload, &slots, sizeof(ImU32));
    for (int i = 0; i < app->StorageEntries.Size; i++)
    {
        const ImGuiAppStorageEntry* e = &app->StorageEntries.Data[i];
        if (e->Size <= 0 || e->Ptr == nullptr)
            continue;
        const ImU32 id = (ImU32)e->ID;
        const ImS32 size = (ImS32)e->Size;
        AvPut(&payload, &id, sizeof(ImU32));
        AvPut(&payload, &size, sizeof(ImS32));
    }
    for (int i = 0; i < app->StorageEntries.Size; i++)
    {
        const ImGuiAppStorageEntry* e = &app->StorageEntries.Data[i];
        if (e->Size <= 0 || e->Ptr == nullptr)
            continue;
        AvPut(&payload, e->Ptr, e->Size);
    }
    out->clear();
    AvRecordAppend(out, ImGuiAppAVMetaRecordType_StateSnapshot, payload.Data, (ImU32)payload.Size);
    return true;
}

// Serialize input-log frames recorded since the last pump into framed records.
static void AvCollectInputRecords(ImGuiAppRecorder* rec, ImVector<char>* out)
{
    const ImGuiAppInputLog* log = rec->InputLog;
    if (log == nullptr || log->FrameSize == 0)
        return;
    if (log->CompositionID != rec->InputHdrComposition && log->SlotIds.Size > 0)
    {
        const bool first_hdr = rec->InputHdrComposition == 0;
        AvBuildInputHdrRecord(&rec->InputHdrRecord, log);
        rec->InputHdrComposition = log->CompositionID;
        if (!first_hdr)
            rec->LastInputCount = 0;   // composition changed: the log restarted; frames before attach stay excluded
        AvPut(out, rec->InputHdrRecord.Data, rec->InputHdrRecord.Size);
    }
    if (log->Count < rec->LastInputCount)
        rec->LastInputCount = 0;   // caller cleared between takes
    for (int f = rec->LastInputCount; f < log->Count; f++)
    {
        const char* bytes = log->Frames.Data + f * log->FrameSize;
        const ImU32 hash = f < log->StateHashes.Size ? (ImU32)log->StateHashes.Data[f] : 0;
        AvBuildInputFrameRecord(out, rec->App->FrameID.FrameIndex, hash, bytes, log->FrameSize);
    }
    rec->LastInputCount = log->Count;
}

// Identity payload: the take's declared identity (see the record-type comment). The
// schema hash covers exactly the slot layout AppStateHash and snapshots depend on.
static void AvBuildIdentityRecord(ImVector<char>* out, const ImGuiApp* app, int embed_rows, ImU32* out_schema)
{
    const ImU32 schema = ImGui::AppStateSchemaHash(app);   // id+size+temp range per snapshottable slot
    if (out_schema != nullptr)
        *out_schema = schema;

    ImVector<char> payload;
    const ImU32 applayer_ver = (ImU32)IMGUIAPP_VERSION_NUM;
    const ImU32 imgui_ver = (ImU32)IMGUI_VERSION_NUM;
    const ImU32 comp = (ImU32)ImGui::GetAppCompositionID(app);
    const ImU32 schema32 = (ImU32)schema;
    const ImU32 rows = (ImU32)embed_rows;
    const ImU16 block = 4;
    const ImU16 reserved = 0;
    AvPut(&payload, &applayer_ver, sizeof(ImU32));
    AvPut(&payload, &imgui_ver, sizeof(ImU32));
    AvPut(&payload, &comp, sizeof(ImU32));
    AvPut(&payload, &schema32, sizeof(ImU32));
    AvPut(&payload, &rows, sizeof(ImU32));
    AvPut(&payload, &block, sizeof(ImU16));
    AvPut(&payload, &reserved, sizeof(ImU16));
    AvRecordAppend(out, ImGuiAppAVMetaRecordType_Identity, payload.Data, (ImU32)payload.Size);
}

// IoFrame payload: layout frozen -- docs/designs.md (av-design) record catalog; ring dumps recompute the
// chain over surviving entries (out_chain_offset reports the field's offset for that patch; -1 if unused).
// Capture at the pump (post-Render): wheel/text come from g.InputEventsTrail (EndFrame zeroes io's), mouse
// is MAIN-VIEWPORT-RELATIVE (replay is window-position independent), key transitions diff against the
// previous pump's shadow (the first pump emits currently-down keys).
static void AvBuildIoFrameRecord(ImGuiAppRecorder* rec, ImVector<char>* out, ImU64 tick, ImU32* out_state_hash, int* out_chain_offset)
{
    const ImGuiIO& io = ImGui::GetIO();
    const ImGuiContext* ctx = ImGui::GetCurrentContext();
    const ImVec2 vp_pos = ImGui::GetMainViewport()->Pos;

    ImVector<char> payload;
    AvPut(&payload, &tick, sizeof(ImU64));
    const float mx = io.MousePos.x - vp_pos.x;
    const float my = io.MousePos.y - vp_pos.y;
    AvPut(&payload, &mx, sizeof(float));
    AvPut(&payload, &my, sizeof(float));
    ImU8 buttons = 0;
    for (int b = 0; b < 5; b++)
        if (io.MouseDown[b])
            buttons |= (ImU8)(1 << b);
    AvPut(&payload, &buttons, sizeof(ImU8));

    float wheel = 0.0f;
    float wheel_h = 0.0f;
    ImVector<ImWchar16> chars;
    for (int e = 0; e < ctx->InputEventsTrail.Size; e++)
    {
        const ImGuiInputEvent* ev = &ctx->InputEventsTrail.Data[e];
        if (ev->Type == ImGuiInputEventType_MouseWheel)
        {
            wheel += ev->MouseWheel.WheelY;
            wheel_h += ev->MouseWheel.WheelX;
        }
        else if (ev->Type == ImGuiInputEventType_Text)
        {
            const unsigned int cp = ev->Text.Char;
            if (cp <= 0xFFFF)
            {
                chars.push_back((ImWchar16)cp);
            }
            else
            {
                chars.push_back((ImWchar16)(0xD800 + ((cp - 0x10000) >> 10)));
                chars.push_back((ImWchar16)(0xDC00 + ((cp - 0x10000) & 0x3FF)));
            }
        }
    }
    AvPut(&payload, &wheel, sizeof(float));
    AvPut(&payload, &wheel_h, sizeof(float));

    const ImU32 hash = (ImU32)ImGui::AppStateHash(rec->App);
    AvPut(&payload, &hash, sizeof(ImU32));
    if (out_state_hash != nullptr)
        *out_state_hash = hash;

    // Normal mode advances the running chain here; ring mode patches this field at dump.
    rec->IoChain = (ImU32)ImHashData(&hash, sizeof(ImU32), rec->IoChain);
    const int chain_at = payload.Size;
    AvPut(&payload, &rec->IoChain, sizeof(ImU32));
    if (out_chain_offset != nullptr)
        *out_chain_offset = out->Size + 8 + chain_at;   // 8 = record framing (type + size) preceding the payload

    ImU16 transitions = 0;
    const int t_count_at = payload.Size;
    AvPut(&payload, &transitions, sizeof(ImU16));
    for (int k = 0; k < ImGuiKey_NamedKey_COUNT; k++)
    {
        const bool down = io.KeysData[k].Down;
        if (down == (rec->IoShadowValid ? rec->IoKeyDown[k] : false))
            continue;
        const ImU16 key = (ImU16)(ImGuiKey_NamedKey_BEGIN + k);
        const ImU8 d = down ? 1 : 0;
        AvPut(&payload, &key, sizeof(ImU16));
        AvPut(&payload, &d, sizeof(ImU8));
        transitions++;
        rec->IoKeyDown[k] = down;
    }
    if (!rec->IoShadowValid)
    {
        for (int k = 0; k < ImGuiKey_NamedKey_COUNT; k++)
            rec->IoKeyDown[k] = io.KeysData[k].Down;
        rec->IoShadowValid = true;
    }
    memcpy(payload.Data + t_count_at, &transitions, sizeof(transitions));

    const ImU16 char_count = (ImU16)chars.Size;
    AvPut(&payload, &char_count, sizeof(ImU16));
    if (chars.Size > 0)
        AvPut(&payload, chars.Data, chars.Size * (int)sizeof(ImWchar16));

    AvRecordAppend(out, ImGuiAppAVMetaRecordType_IoFrame, payload.Data, (ImU32)payload.Size);
}

// Queue framed records onto the take's meta stream; frames drain it chunk by chunk. Crash-honesty lives in
// the container (fragmented mp4 / per-frame QOI files): every completed fragment's frames carry their
// chunks. The running digest covers every logical-stream byte in queue order (ImHashData chains via its seed).
static void AvMetaQueue(ImGuiAppRecorder* rec, const ImVector<char>* framed)
{
    if (framed->Size == 0)
        return;
    const int base = rec->MetaPending.Size;
    rec->MetaPending.resize(base + framed->Size);
    memcpy(rec->MetaPending.Data + base, framed->Data, (size_t)framed->Size);
    rec->StreamDigest = (ImU32)ImHashData(framed->Data, (size_t)framed->Size, rec->StreamDigest);
    rec->StreamBytes += (ImU64)framed->Size;
}

// Digest payload: u64 stream_bytes (all logical-stream bytes preceding this record, header included) |
// u32 digest over exactly those bytes (seed 0). The stream's FINAL record: presence = complete take,
// absence = truncation. If the take's last frame has no chunk room left, the digest truncates like any tail bytes.
static void AvBuildDigestRecord(ImVector<char>* out, ImU64 stream_bytes, ImU32 digest)
{
    ImVector<char> payload;
    AvPut(&payload, &stream_bytes, sizeof(ImU64));
    AvPut(&payload, &digest, sizeof(ImU32));
    AvRecordAppend(out, ImGuiAppAVMetaRecordType_Digest, payload.Data, (ImU32)payload.Size);
}

//-----------------------------------------------------------------------------
// [SECTION] Embedded metadata (pixel strip)
//-----------------------------------------------------------------------------

// Strip format is the frozen contract (imguiapp_internal.h meta section / docs/designs.md (av-design)):
// bottom EmbedRows rows, 4x4 luma blocks (black 16 / white 235), bit index by * blocks_per_row + bx,
// MSB-first per stream byte. Per frame: u32 'IMIL' | u32 chunk_size | chunk | u32 ImHashData(chunk); size 0 valid.
static void AvStampChunk(ImGuiAppRecorder* rec, char* rgba, int w, int h, ImVector<char>* stream, int* cursor)
{
    const int embed_rows = rec->Config.EmbedRows;
    if (h <= embed_rows)
    {
        if (!rec->EmbedTooShortWarned)
        {
            rec->EmbedTooShortWarned = true;
            ImGui::AppWALWrite(rec->App->WAL, ImGuiAppWALLevel_Lifecycle, "av: frame height %d <= EmbedRows %d, strip not stamped", h, embed_rows);
        }
        return;
    }

    const int blocks_x = w / 4;
    const int block_rows = embed_rows / 4;
    const long long capacity_bits = (long long)blocks_x * block_rows;
    const long long capacity_bytes = capacity_bits / 8 - 12;   // 'IMIL' + size + checksum framing
    if (capacity_bytes < 0)
        return;

    const long long remaining = (long long)stream->Size - *cursor;
    const ImU32 size = (ImU32)(remaining < capacity_bytes ? (remaining > 0 ? remaining : 0) : capacity_bytes);
    const char* chunk = stream->Data + *cursor;
    const ImU32 checksum = ImHashData(size > 0 ? chunk : "", (size_t)size, 0);
    *cursor += (int)size;

    ImVector<char>* s = &rec->EmbedStream;
    s->clear();
    AvPut(s, "IMIL", 4);
    AvPut(s, &size, 4);
    if (size > 0)
        AvPut(s, chunk, (int)size);
    AvPut(s, &checksum, 4);

    const int y0 = h - embed_rows;
    const long long total_bits = (long long)s->Size * 8;
    for (int by = 0; by < block_rows; by++)
    {
        for (int bx = 0; bx < blocks_x; bx++)
        {
            const long long bit = (long long)by * blocks_x + bx;
            unsigned char luma = 16;
            if (bit < total_bits)
            {
                const unsigned char byte = (unsigned char)s->Data[bit >> 3];
                if (byte & (0x80 >> (bit & 7)))
                    luma = 235;
            }
            for (int py = 0; py < 4; py++)
            {
                unsigned char* px = (unsigned char*)rgba + ((size_t)(y0 + by * 4 + py) * w + (size_t)bx * 4) * 4;
                for (int pxi = 0; pxi < 4; pxi++)
                {
                    px[0] = luma;
                    px[1] = luma;
                    px[2] = luma;
                    px[3] = 255;
                    px += 4;
                }
            }
        }
    }
    // Trailing pixels right of the block grid (w % 4): blacked for an unambiguous strip.
    const int grid_w = blocks_x * 4;
    for (int y = y0; y < h && grid_w < w; y++)
    {
        unsigned char* px = (unsigned char*)rgba + ((size_t)y * w + grid_w) * 4;
        for (int x = grid_w; x < w; x++)
        {
            px[0] = 16;
            px[1] = 16;
            px[2] = 16;
            px[3] = 255;
            px += 4;
        }
    }
}

//-----------------------------------------------------------------------------
// [SECTION] Per-frame pump
//-----------------------------------------------------------------------------

// Emits one frame: meta records built on the app thread, pixels to the ring or the
// encoder thread. consume_pendings binds the caller-set blob/snapshot to this frame
// (real current frames only; placeholders and backlog frames must not steal them).
static void AvEmitFrame(ImGuiAppRecorder* rec, const ImGuiAppAVFrame* frame, bool consume_pendings)
{
    ImVector<char> meta;
    const bool with_blob = consume_pendings && rec->PendingBlobSet;
    const void* user = with_blob ? (const void*)rec->PendingBlob.Data : nullptr;
    const ImU32 user_size = with_blob ? (ImU32)rec->PendingBlob.Size : 0;
    AvBuildFrameRecord(&meta, &frame->FrameID, user, user_size);
    ImU32 io_state_hash = 0;
    int io_chain_offset = -1;
    AvBuildIoFrameRecord(rec, &meta, frame->FrameID.FrameIndex, &io_state_hash, &io_chain_offset);   // raw input: every frame, by default

    AvCollectInputRecords(rec, &meta);

    if (consume_pendings && rec->PendingSnapshotRecord.Size > 0)
    {
        AvPut(&meta, rec->PendingSnapshotRecord.Data, rec->PendingSnapshotRecord.Size);
        rec->PendingSnapshotRecord.clear();
    }
    if (consume_pendings)
    {
        rec->PendingBlobSet = false;
        rec->PendingBlob.clear();
    }

    if (rec->IsRing)
    {
        rec->RingLastAcceptSec = frame->FrameID.TimeSec;

        // Ring pixels are stamped at DUMP time (evictions would tear a capture-time
        // stream); entries keep their raw pixels and their frame's records.
        ImGuiAppAVRingEntry* entry = IM_NEW(ImGuiAppAVRingEntry)();
        entry->Width = frame->Width;
        entry->Height = frame->Height;
        entry->FrameID = frame->FrameID;
        if (!ImGui::AppAVImageEncode(frame->Pixels, frame->Width, frame->Height, frame->PitchBytes, &entry->Qoi))
        {
            IM_DELETE(entry);
            return;
        }
        entry->StateHash = io_state_hash;
        entry->ChainOffset = io_chain_offset;
        entry->MetaRecords.swap(meta);
        rec->RingEntries.push_back(entry);
        rec->RingBytes += (size_t)entry->Qoi.Size + (size_t)entry->MetaRecords.Size;

        // Evict oldest while either bound binds.
        const double span_limit = (double)rec->Ring.Seconds;
        const size_t byte_limit = (size_t)rec->Ring.MaxMemoryMB * 1024u * 1024u;
        while (rec->RingEntries.Size > 1)
        {
            ImGuiAppAVRingEntry* oldest = rec->RingEntries.Data[0];
            const bool too_old = frame->FrameID.TimeSec - oldest->FrameID.TimeSec > span_limit;
            const bool too_big = rec->RingBytes > byte_limit;
            if (!too_old && !too_big)
                break;
            rec->RingBytes -= (size_t)oldest->Qoi.Size + (size_t)oldest->MetaRecords.Size;
            IM_DELETE(oldest);
            rec->RingEntries.erase(rec->RingEntries.begin());
        }
    }
    else
    {
        // Queue this frame's records onto the stream, copy pixels, stamp the next chunk.
        AvMetaQueue(rec, &meta);

        // Take finalization: the Digest rides the FINAL frame's chunk, covering every
        // stream byte before it (including this frame's own records, excluding itself).
        if (rec->EmitDigestNext)
        {
            ImVector<char> digest;
            AvBuildDigestRecord(&digest, rec->StreamBytes, rec->StreamDigest);
            AvMetaQueue(rec, &digest);
            rec->EmitDigestNext = false;
        }

        ImGuiAppAVQueuedFrame* job = IM_NEW(ImGuiAppAVQueuedFrame)();
        job->Width = frame->Width;
        job->Height = frame->Height;
        job->FrameID = frame->FrameID;
        job->Pixels.resize(frame->Width * frame->Height * 4);
        const char* src = (const char*)frame->Pixels;
        char* dst = job->Pixels.Data;
        const int row = frame->Width * 4;
        for (int y = 0; y < frame->Height; y++)
        {
            memcpy(dst, src, (size_t)row);
            src += frame->PitchBytes;
            dst += row;
        }
        AvStampChunk(rec, job->Pixels.Data, frame->Width, frame->Height, &rec->MetaPending, &rec->MetaPendingCursor);

        // Consumed front compacts so a long take's stream buffer stays bounded.
        if (rec->MetaPendingCursor > 65536)
        {
            rec->MetaPending.erase(rec->MetaPending.begin(), rec->MetaPending.begin() + rec->MetaPendingCursor);
            rec->MetaPendingCursor = 0;
        }

        const ImU64 drops_before = rec->DroppedFrames;
        if (!AvQueuePush(rec, job) && rec->DroppedFrames == drops_before + 1 && drops_before == 0)
            ImGui::AppWALWrite(rec->App->WAL, ImGuiAppWALLevel_Lifecycle, "av: encoder queue full, dropping frames (DropNewest)");
    }

    rec->AcceptedFrames++;
    rec->LastEmittedIndex = frame->FrameID.FrameIndex;
}

// Pause-glyph frame at the locked size: dark gray field, two centered light bars. Encodes
// "the app produced no pixels for this frame" (minimized, capture gap) without breaking
// the take's frame-id contiguity.
static void AvEmitPlaceholder(ImGuiAppRecorder* rec, ImU64 frame_index)
{
    const int w = rec->FixedWidth;
    const int h = rec->FixedHeight;
    if (w <= 0 || h <= 0)
        return;

    if (rec->PlaceholderPixels.Size != w * h * 4)
    {
        rec->PlaceholderPixels.resize(w * h * 4);
        unsigned int* px = (unsigned int*)rec->PlaceholderPixels.Data;
        const unsigned int bg = IM_COL32(32, 32, 36, 255);
        const unsigned int bar = IM_COL32(200, 200, 205, 255);
        const int bar_w = w * 8 / 100;
        const int bar_h = h * 40 / 100;
        const int gap = w * 8 / 100;
        const int y0 = (h - bar_h) / 2;
        const int lx0 = w / 2 - gap / 2 - bar_w;
        const int rx0 = w / 2 + gap / 2;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                const bool in_bar = y >= y0 && y < y0 + bar_h &&
                                        ((x >= lx0 && x < lx0 + bar_w) || (x >= rx0 && x < rx0 + bar_w));
                px[(size_t)y * w + x] = in_bar ? bar : bg;
            }
    }

    ImGuiAppAVFrame frame;
    frame.Width = w;
    frame.Height = h;
    frame.PitchBytes = w * 4;
    frame.Pixels = rec->PlaceholderPixels.Data;
    // The frame never rendered, so it has no measured clocks: synthesis-time timestamps
    // under its REAL index keep the id sequence contiguous and the timeline monotonic.
    frame.FrameID = rec->App->FrameID;
    frame.FrameID.FrameIndex = frame_index;
    AvEmitFrame(rec, &frame, false);
}

IMGUI_API void ImGui::AppRecordPump(ImGuiAppRecorder* rec)
{
    if (rec == nullptr || !rec->Active)
        return;

    const ImGuiAppPlatformBackend* backend = ImGuiAppGetPlatformBackend();
    if (backend->CaptureFrameFn == nullptr)
        return;

    // Explicit ring subsampling (Ring.Fps > 0) opts OUT of the encode-every-frame
    // contract: no gap synthesis there, sparse is the point.
    const bool subsampled = rec->IsRing && rec->Ring.Fps > 0.0f;

    ImGuiAppAVFrame captured;
    const bool have_frame = backend->CaptureFrameFn(rec->App, &captured)
                       && captured.Pixels != nullptr && captured.Width > 0 && captured.Height > 0;
    if (!have_frame)
    {
        // No new pixels this pump. When the CURRENT frame's copy may still arrive through
        // the pipelined path (bubble), synthesize nothing for it; fill only ids strictly
        // below it. Frame-scoped pendings must not shift onto another frame's identity.
        rec->PendingBlobSet = false;
        rec->PendingBlob.clear();
        rec->PendingSnapshotRecord.clear();

        if (rec->LastEmittedIndex == 0)
        {
            // Pre-first-capture: without a locked size there is nothing to synthesize at.
            if (rec->FixedWidth == 0)
            {
                if (rec->Config.Width > 0 && rec->Config.Height > 0)
                {
                    rec->FixedWidth = rec->Config.Width;
                    rec->FixedHeight = rec->Config.Height;
                    rec->LastEmittedIndex = rec->App->FrameID.FrameIndex > 0 ? rec->App->FrameID.FrameIndex - 1 : 0;
                }
                else if (!rec->NoSizeWarned)
                {
                    rec->NoSizeWarned = true;
                    ImGui::AppWALWrite(rec->App->WAL, ImGuiAppWALLevel_Lifecycle, "av: capture not delivering and no size configured; frames before the first capture are not synthesized");
                }
                return;
            }
            return;
        }

        if (!subsampled)
            while (rec->LastEmittedIndex + 1 < rec->App->FrameID.FrameIndex && rec->Active)
                AvEmitPlaceholder(rec, rec->LastEmittedIndex + 1);
        return;
    }

    // Fixed-size contract: a resize mid-recording aborts rather than silently rescaling.
    if (rec->FixedWidth == 0)
    {
        rec->FixedWidth = rec->Config.Width > 0 ? rec->Config.Width : captured.Width;
        rec->FixedHeight = rec->Config.Height > 0 ? rec->Config.Height : captured.Height;
    }
    if (captured.Width != rec->FixedWidth || captured.Height != rec->FixedHeight)
    {
        ImGui::AppWALWrite(rec->App->WAL, ImGuiAppWALLevel_Lifecycle, "av: abort recording, frame %dx%d != locked %dx%d",
                           captured.Width, captured.Height, rec->FixedWidth, rec->FixedHeight);
        rec->Active = false;
        return;
    }

    // A backend that leaves the id empty gets stamped with the pumping frame's; pipelined
    // backends return frame N-1's pixels WITH their true id.
    if (captured.FrameID.FrameIndex == 0)
        captured.FrameID = rec->App->FrameID;

    // Duplicate guard: ids must be strictly increasing.
    if (rec->LastEmittedIndex > 0 && captured.FrameID.FrameIndex <= rec->LastEmittedIndex)
        return;

    if (rec->IsRing && subsampled)
    {
        const double min_step = 1.0 / (double)rec->Ring.Fps;
        if (captured.FrameID.TimeSec - rec->RingLastAcceptSec + 1e-9 < min_step)
        {
            rec->PendingBlobSet = false;
            rec->PendingBlob.clear();
            rec->PendingSnapshotRecord.clear();
            return;
        }
    }

    // Fill any hole strictly below the real frame, then emit it.
    if (!subsampled && rec->LastEmittedIndex > 0)
        while (rec->LastEmittedIndex + 1 < captured.FrameID.FrameIndex && rec->Active)
            AvEmitPlaceholder(rec, rec->LastEmittedIndex + 1);
    AvEmitFrame(rec, &captured, true);
}

// The final rendered frame's pipelined copy retires after the last pump: pump again (bounded) until
// nothing new appears, then close the id sequence with placeholders for frames that never produced pixels.
// A pending Digest reserves the take's LAST frame as its carrier (the digest must be the stream's final record).
static void AvDrainCapture(ImGuiAppRecorder* rec)
{
    const bool digest_pending = rec->EmitDigestNext && !rec->IsRing;
    rec->EmitDigestNext = false;   // no mid-drain emission may consume it

    int idle_tries = 0;
    for (int i = 0; i < 32 && rec->Active && idle_tries < 3; i++)
    {
        const ImU64 before = rec->AcceptedFrames;
        ImGui::AppRecordPump(rec);
        if (rec->AcceptedFrames > before)
        {
            idle_tries = 0;
            continue;
        }
        idle_tries++;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (rec->Active && rec->LastEmittedIndex > 0 && !(rec->IsRing && rec->Ring.Fps > 0.0f))
    {
        const ImU64 reserve = digest_pending ? 1 : 0;
        while (rec->LastEmittedIndex + reserve < rec->App->FrameID.FrameIndex && rec->Active)
            AvEmitPlaceholder(rec, rec->LastEmittedIndex + 1);
    }
    if (digest_pending && rec->Active && rec->LastEmittedIndex > 0)
    {
        rec->EmitDigestNext = true;
        AvEmitPlaceholder(rec, rec->LastEmittedIndex + 1);   // end-of-take marker frame
    }
}

//-----------------------------------------------------------------------------
// [SECTION] Public API: begin/end, blobs, ring
//-----------------------------------------------------------------------------

namespace ImGui
{

// Assert forensics (F15): every live ring recorder registers in AppAssert().RingRecorders so
// ImAppAssertFail can dump them all before the process exits -- the flight recording lands
// beside the assert WAL. Non-ring recorders (linear takes) do not register; no ring to snapshot.
static ImGuiAppRecorder* AvBeginCommon(ImGuiApp* app, ImGuiAppAVEncoder* encoder, const ImGuiAppAVEncodeConfig* config)
{
    IM_ASSERT(app != nullptr && encoder != nullptr && config != nullptr && config->OutputPath != nullptr);
    if (app == nullptr || encoder == nullptr || config == nullptr || config->OutputPath == nullptr)
        return nullptr;
    const ImGuiAppPlatformBackend* backend = ImGuiAppGetPlatformBackend();
    if (backend->CaptureFrameFn == nullptr)
    {
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "av: begin refused, platform backend has no CaptureFrame");
        return nullptr;
    }

    ImGuiAppRecorder* rec = IM_NEW(ImGuiAppRecorder)();
    rec->App = app;
    rec->Encoder = encoder;
    rec->Config = *config;
    ImStrncpy(rec->OutputPath, config->OutputPath, IM_ARRAYSIZE(rec->OutputPath));
    rec->Config.OutputPath = rec->OutputPath;   // config copy must not dangle on the caller's string
    if (rec->Config.Timing == ImGuiAppAVTimingMode_Auto)
        rec->Config.Timing = app->Pacer.Mode == ImGuiAppPacerMode_Fixed ? ImGuiAppAVTimingMode_Constant : ImGuiAppAVTimingMode_Realtime;

    // Realtime = live witnessing: the app must NEVER stall on the encoder -- the stall would distort the very
    // timeline being recorded. Block queue + realtime Encode every frame (the readback slowness that once
    // motivated a DropNewest default is fixed); DropNewest stays an explicit opt-in via AppRecordSetQueuePolicy.
    rec->QueuePolicy = ImGuiAppRecordQueuePolicy_Block;

    // Strip geometry is 4x4 blocks: EmbedRows clamps to a multiple of 4, minimum one
    // block row. The adjusted value is the take's contract (readers use the same rows).
    {
        const int requested = rec->Config.EmbedRows;
        int rows = requested < 4 ? 4 : requested & ~3;
        if (rows != requested)
        {
            rec->Config.EmbedRows = rows;
            AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "av: EmbedRows %d adjusted to %d (multiple of 4)", requested, rows);
        }
    }

    rec->MetaHeader.Version = 1;
    memcpy(rec->MetaHeader.Magic, AV_META_MAGIC, 8);
    rec->MetaHeader.Fps = rec->Config.Fps;
    rec->MetaHeader.StartTsc = AvClockTsc();
    rec->MetaHeader.QpcHz = AvClockHz();
    rec->MetaHeader.StartQpc = AvClockCounter();
    // The stream leads with the header, then the take's declared Identity -- before any Frame/IoFrame, so
    // replay classifies hash mismatches before touching frames. The digest covers these bytes too; the io
    // chain seeds from the schema hash, binding the hash sequence to the declared identity.
    AvBuildIdentityRecord(&rec->IdentityRecord, app, rec->Config.EmbedRows, &rec->SchemaHash);
    rec->IoChain = rec->SchemaHash;
    rec->MetaPending.resize((int)sizeof(rec->MetaHeader));
    memcpy(rec->MetaPending.Data, &rec->MetaHeader, sizeof(rec->MetaHeader));
    AvPut(&rec->MetaPending, rec->IdentityRecord.Data, rec->IdentityRecord.Size);
    rec->StreamDigest = (ImU32)ImHashData(rec->MetaPending.Data, (size_t)rec->MetaPending.Size, 0);
    rec->StreamBytes = (ImU64)rec->MetaPending.Size;
    rec->MetaPendingCursor = 0;
    return rec;
}

IMGUI_API ImGuiAppRecorder* AppRecordBegin(ImGuiApp* app, ImGuiAppAVEncoder* encoder, const ImGuiAppAVEncodeConfig* config)
{
    ImGuiAppRecorder* rec = AvBeginCommon(app, encoder, config);
    if (rec == nullptr)
        return nullptr;

    if (!encoder->OpenFn(encoder, &rec->Config))
    {
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "av: encoder '%s' failed to open '%s'", encoder->Name, rec->OutputPath);
        IM_DELETE(rec);
        return nullptr;
    }
    rec->EncoderOpen = true;

    rec->Active = true;
    IM_ASSERT(AppThreadFuncs() != nullptr && "No thread backend: call ImGui::SetAppThreadFuncs() (IMGUIAPP_DISABLE_DEFAULT_THREAD_FUNCS is set).");
    rec->Thread = IM_NEW(ImGuiAppRecorderThread)();
    rec->Thread->Mutex  = AppThreadFuncs()->MutexCreateFn();
    rec->Thread->CvPush = AppThreadFuncs()->CondCreateFn();
    rec->Thread->CvPop  = AppThreadFuncs()->CondCreateFn();
    rec->Thread->Worker = AppThreadFuncs()->ThreadCreateFn(AvEncoderThread, rec);
    if (app->Recorder == nullptr)
        app->Recorder = rec;   // OnEncodeFrame pumps it; explicit AppRecordPump remains valid
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "av: recording '%s' via %s (%s)", rec->OutputPath, encoder->Name,
                rec->Config.Timing == ImGuiAppAVTimingMode_Constant ? "constant" : "realtime");
    return rec;
}

IMGUI_API bool AppRecordIsActive(const ImGuiAppRecorder* rec)
{
    return rec != nullptr && rec->Active;
}

IMGUI_API ImU64 AppRecordFrameCount(const ImGuiAppRecorder* rec)
{
    return rec != nullptr ? rec->AcceptedFrames : 0;
}

IMGUI_API void AppRecordSetQueuePolicy(ImGuiAppRecorder* rec, ImGuiAppRecordQueuePolicy policy, int depth)
{
    IM_ASSERT(rec != nullptr && depth > 0);
    if (rec == nullptr || depth <= 0)
        return;
    if (rec->Thread != nullptr)
    {
        AppThreadFuncs()->MutexLockFn(rec->Thread->Mutex);
        rec->QueuePolicy = policy;
        rec->QueueDepth = depth;
        AppThreadFuncs()->CondBroadcastFn(rec->Thread->CvPush);
        AppThreadFuncs()->MutexUnlockFn(rec->Thread->Mutex);
        return;
    }
    rec->QueuePolicy = policy;   // no encoder thread yet (ring mode / before begin): plain writes
    rec->QueueDepth = depth;
}

IMGUI_API void AppRecordEnd(ImGuiAppRecorder* rec)
{
    IM_ASSERT(rec != nullptr);
    if (rec == nullptr)
        return;
    if (rec->Active)
    {
        rec->EmitDigestNext = !rec->IsRing;   // the drained tail frame carries the Digest (ring digests at dump)
        AvDrainCapture(rec);                  // the last rendered frame's copy retires after the last pump
        if (rec->EmitDigestNext)
            AppWALWrite(rec->App->WAL, ImGuiAppWALLevel_Lifecycle, "av: no final frame retired; Digest truncated (take reads as incomplete)");
    }
    rec->Active = false;

    if (!rec->IsRing)
    {
        if (rec->Thread != nullptr)
        {
            const ImGuiAppThreadFuncs* tf = AppThreadFuncs();
            tf->MutexLockFn(rec->Thread->Mutex);
            rec->ThreadStop = true;
            tf->CondBroadcastFn(rec->Thread->CvPop);
            tf->CondBroadcastFn(rec->Thread->CvPush);
            tf->MutexUnlockFn(rec->Thread->Mutex);
            tf->ThreadJoinFn(rec->Thread->Worker);
            tf->CondDestroyFn(rec->Thread->CvPop);
            tf->CondDestroyFn(rec->Thread->CvPush);
            tf->MutexDestroyFn(rec->Thread->Mutex);
            IM_DELETE(rec->Thread);
            rec->Thread = nullptr;
        }
        if (rec->EncoderOpen)
        {
            rec->Encoder->CloseFn(rec->Encoder);
            rec->EncoderOpen = false;
        }
        if (rec->MetaPending.Size > rec->MetaPendingCursor)
            AppWALWrite(rec->App->WAL, ImGuiAppWALLevel_Lifecycle, "av: %d meta bytes had no frame left to carry them (stream tail truncated)",
                        rec->MetaPending.Size - rec->MetaPendingCursor);
        if (rec->DroppedFrames > 0)
            AppWALWrite(rec->App->WAL, ImGuiAppWALLevel_Lifecycle, "av: recording ended, %llu frames dropped", (unsigned long long)rec->DroppedFrames);
        if (rec->EncodeFailed)
            AppWALWrite(rec->App->WAL, ImGuiAppWALLevel_Lifecycle, "av: encoder '%s' reported write failures", rec->Encoder->Name);
    }

    for (int i = 0; i < rec->RingEntries.Size; i++)
        IM_DELETE(rec->RingEntries.Data[i]);
    rec->RingEntries.clear();
    for (int i = 0; i < rec->Queue.Size; i++)
        IM_DELETE(rec->Queue.Data[i]);
    rec->Queue.clear();
    if (rec->App != nullptr && rec->App->Recorder == rec)
        rec->App->Recorder = nullptr;   // OnEncodeFrame must never pump a freed recorder
    ImVector<ImGuiAppRecorder*>& assert_recorders = AppAssert().RingRecorders;
    for (int i = assert_recorders.Size - 1; i >= 0; i--)
        if (assert_recorders.Data[i] == rec)
            assert_recorders.erase(assert_recorders.Data + i);   // no dangling recorder in the assert dump list
    IM_DELETE(rec);
}

IMGUI_API void AppRecordSetFrameData(ImGuiAppRecorder* rec, const void* data, int size)
{
    IM_ASSERT(rec != nullptr);
    if (rec == nullptr || data == nullptr || size <= 0)
        return;
    rec->PendingBlob.resize(size);
    memcpy(rec->PendingBlob.Data, data, (size_t)size);
    rec->PendingBlobSet = true;
}

IMGUI_API void AppRecordSnapshotState(ImGuiAppRecorder* rec, ImGuiApp* app)
{
    IM_ASSERT(rec != nullptr && app != nullptr);
    if (rec == nullptr || app == nullptr)
        return;
    // The snapshot travels as the frame's typed StateSnapshot record (the user blob stays
    // free for AppRecordSetFrameData); the frame_index inside it binds video frame N to
    // app state N.
    AvBuildStateSnapshotRecord(&rec->PendingSnapshotRecord, app, app->FrameID.FrameIndex);
}

IMGUI_API void AppRecordAttachInputLog(ImGuiAppRecorder* rec, const ImGuiAppInputLog* log)
{
    IM_ASSERT(rec != nullptr);
    if (rec == nullptr)
        return;
    rec->InputLog = log;
    rec->LastInputCount = log != nullptr ? log->Count : 0;   // frames before attach are not part of this take
    rec->InputHdrComposition = 0;
}

IMGUI_API ImGuiAppRecorder* AppRecordBeginRing(ImGuiApp* app, ImGuiAppAVEncoder* encoder, const ImGuiAppAVEncodeConfig* config, const ImGuiAppRingConfig* ring)
{
    IM_ASSERT(ring != nullptr);
    if (ring == nullptr)
        return nullptr;
    ImGuiAppRecorder* rec = AvBeginCommon(app, encoder, config);
    if (rec == nullptr)
        return nullptr;
    // The provider stays closed until a dump; ring compression is synchronous on the app
    // thread (QOI is fast), so no encoder thread either.
    rec->IsRing = true;
    rec->Ring = *ring;
    rec->Active = true;
    if (app->Recorder == nullptr)
        app->Recorder = rec;   // OnEncodeFrame pumps it; explicit AppRecordPump remains valid
    AppAssert().RingRecorders.push_back(rec);   // dump-on-assert (F15); removed in AppRecordEnd
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "av: flight recorder armed (%.1fs / %dMB @ %.0f fps)",
                rec->Ring.Seconds, rec->Ring.MaxMemoryMB, rec->Ring.Fps);
    return rec;
}

IMGUI_API bool AppRecordDumpRing(ImGuiAppRecorder* rec, const char* reason)
{
    IM_ASSERT(rec != nullptr && rec->IsRing);
    if (rec == nullptr || !rec->IsRing)
        return false;
    AvDrainCapture(rec);   // pull the newest retired copy in before snapshotting the ring
    if (rec->RingEntries.Size == 0)
        return false;

    rec->DumpCount++;
    char path[560];
    if (rec->DumpCount == 1)
        ImStrncpy(path, rec->OutputPath, IM_ARRAYSIZE(path));
    else
    {
        // "-N" before the extension when one exists; appended otherwise.
        const char* dot = strrchr(rec->OutputPath, '.');
        const char* slash_f = strrchr(rec->OutputPath, '/');
        const char* slash_b = strrchr(rec->OutputPath, '\\');
        const char* slash = slash_b > slash_f ? slash_b : slash_f;
        if (dot != nullptr && dot > slash)
            ImFormatString(path, IM_ARRAYSIZE(path), "%.*s-%d%s", (int)(dot - rec->OutputPath), rec->OutputPath, rec->DumpCount, dot);
        else
            ImFormatString(path, IM_ARRAYSIZE(path), "%s-%d", rec->OutputPath, rec->DumpCount);
    }

    AppWALWrite(rec->App->WAL, ImGuiAppWALLevel_Lifecycle, "av: ring dump -> '%s' (%d frames, reason: %s)",
                path, rec->RingEntries.Size, reason != nullptr ? reason : "unspecified");

    ImGuiAppAVEncodeConfig dump_config = rec->Config;
    dump_config.OutputPath = path;
    dump_config.Width = rec->RingEntries.Data[0]->Width;
    dump_config.Height = rec->RingEntries.Data[0]->Height;
    if (!rec->Encoder->OpenFn(rec->Encoder, &dump_config))
    {
        AppWALWrite(rec->App->WAL, ImGuiAppWALLevel_Lifecycle, "av: ring dump failed, encoder '%s' would not open", rec->Encoder->Name);
        return false;
    }

    // The dump's meta stream, built now (evictions make a capture-time stream
    // impossible): header, reason marker, the take's input header, then every surviving
    // entry's records. Chunked across the dump's frames at restamp below.
    ImVector<char> stream;
    int cursor = 0;
    AvPut(&stream, &rec->MetaHeader, (int)sizeof(rec->MetaHeader));
    AvPut(&stream, rec->IdentityRecord.Data, rec->IdentityRecord.Size);
    if (reason != nullptr && reason[0])
    {
        // Reason marker: a Frame record with sentinel index ~0 whose user blob is the reason text.
        ImVector<char> marker;
        ImGuiAppFrameID sentinel;
        sentinel.FrameIndex = (ImU64)-1;
        AvBuildFrameRecord(&marker, &sentinel, reason, (ImU32)strlen(reason));
        AvPut(&stream, marker.Data, marker.Size);
    }
    if (rec->InputHdrRecord.Size > 0)
        AvPut(&stream, rec->InputHdrRecord.Data, rec->InputHdrRecord.Size);
    // Survivor records; the io hash chain is recomputed HERE over exactly the surviving
    // sequence (eviction changed it, so capture-time chains are meaningless) from the
    // Identity schema seed, patched in place via each entry's recorded field offset.
    ImU32 chain = rec->SchemaHash;
    for (int i = 0; i < rec->RingEntries.Size; i++)
    {
        ImGuiAppAVRingEntry* entry = rec->RingEntries.Data[i];
        const int base = stream.Size;
        AvPut(&stream, entry->MetaRecords.Data, entry->MetaRecords.Size);
        chain = (ImU32)ImHashData(&entry->StateHash, sizeof(ImU32), chain);
        if (entry->ChainOffset >= 0 && base + entry->ChainOffset + (int)sizeof(ImU32) <= stream.Size)
            memcpy(stream.Data + base + entry->ChainOffset, &chain, sizeof(chain));
    }
    // Final record: the dump's Digest over every stream byte before it.
    {
        const ImU32 digest = (ImU32)ImHashData(stream.Data, (size_t)stream.Size, 0);
        ImVector<char> digest_rec;
        AvBuildDigestRecord(&digest_rec, (ImU64)stream.Size, digest);
        AvPut(&stream, digest_rec.Data, digest_rec.Size);
    }

    bool ok = true;
    ImVector<char> rgba;
    for (int i = 0; i < rec->RingEntries.Size; i++)
    {
        ImGuiAppAVRingEntry* entry = rec->RingEntries.Data[i];
        int w = 0;
        int h = 0;
        if (!AppAVImageDecode(entry->Qoi.Data, entry->Qoi.Size, &rgba, &w, &h))
        {
            ok = false;
            break;
        }
        AvStampChunk(rec, rgba.Data, w, h, &stream, &cursor);
        ImGuiAppAVFrame frame;
        frame.Width = w;
        frame.Height = h;
        frame.PitchBytes = w * 4;
        frame.Pixels = rgba.Data;
        frame.FrameID = entry->FrameID;
        if (!rec->Encoder->WriteFrameFn(rec->Encoder, &frame))
        {
            ok = false;
            break;
        }
    }
    rec->Encoder->CloseFn(rec->Encoder);
    if (cursor < stream.Size)
        AppWALWrite(rec->App->WAL, ImGuiAppWALLevel_Lifecycle, "av: ring dump stream truncated (%d of %d bytes carried)", cursor, stream.Size);
    return ok;
}

// F15: dump every registered ring recorder before the process exits on an assert. Called by
// ImAppAssertFail after it writes the assert WAL, so the flight recording lands beside it.
IMGUI_API int AppDumpAssertRings(const char* reason)
{
    int dumped = 0;
    const ImVector<ImGuiAppRecorder*>& assert_recorders = AppAssert().RingRecorders;
    for (int i = 0; i < assert_recorders.Size; i++)
        if (AppRecordDumpRing(assert_recorders.Data[i], reason))
            dumped++;
    return dumped;
}

//-----------------------------------------------------------------------------
// [SECTION] Meta-only run recorder (F70)
//-----------------------------------------------------------------------------
// A preview session records without a video pipeline: it drives its own ImGuiApp and emits the SAME TLV
// stream the video recorder embeds (reusing the AvBuild* writers above), minus the pixel pipeline. Rec
// carries only the writers' state; its encoder thread + queue never start. (ImGuiAppMetaRecorder is defined above.)

IMGUI_API ImGuiAppMetaRecorder* AppMetaRecordBegin(ImGuiApp* app, float fps, int embed_rows)
{
    IM_ASSERT(app != nullptr);
    if (app == nullptr)
        return nullptr;

    ImGuiAppMetaRecorder* mr = IM_NEW(ImGuiAppMetaRecorder)();
    mr->Rec.App = app;
    mr->EmbedRows = embed_rows < 4 ? 4 : embed_rows & ~3;   // 4x4 blocks: clamp like AvBeginCommon

    ImGuiAppAVMetaHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.Magic, AV_META_MAGIC, 8);
    hdr.Version = 1;
    hdr.Fps = fps;
    hdr.StartTsc = AvClockTsc();
    hdr.QpcHz = AvClockHz();
    hdr.StartQpc = AvClockCounter();
    mr->Meta.resize((int)sizeof(hdr));
    memcpy(mr->Meta.Data, &hdr, sizeof(hdr));

    // Identity leads the stream (before any Frame/IoFrame); the io hash chain seeds from its schema hash.
    AvBuildIdentityRecord(&mr->Rec.IdentityRecord, app, mr->EmbedRows, &mr->Rec.SchemaHash);
    AvPut(&mr->Meta, mr->Rec.IdentityRecord.Data, mr->Rec.IdentityRecord.Size);
    mr->Rec.IoChain = mr->Rec.SchemaHash;
    return mr;
}

IMGUI_API void AppMetaRecordAttachInputLog(ImGuiAppMetaRecorder* mr, const ImGuiAppInputLog* log)
{
    IM_ASSERT(mr != nullptr);
    if (mr == nullptr)
        return;
    mr->Rec.InputLog = log;
    mr->Rec.LastInputCount = log != nullptr ? log->Count : 0;   // frames before attach are not part of this take
    mr->Rec.InputHdrComposition = 0;
}

IMGUI_API void AppMetaRecordTick(ImGuiAppMetaRecorder* mr, ImU64 tick, double time_sec, bool snapshot)
{
    IM_ASSERT(mr != nullptr && mr->Rec.App != nullptr);
    if (mr == nullptr || mr->Rec.App == nullptr)
        return;
    mr->Rec.App->FrameID.FrameIndex = tick;   // AvCollectInputRecords stamps this tick's InputFrame with it

    ImVector<char> out;

    // Frame first (the tick spine == frame ordinal; RunTickSlot needs it before its Io/Input/Snapshot).
    ImGuiAppFrameID id;
    id.FrameIndex = tick;
    id.Tsc = 0;
    id.TimeSec = time_sec;
    AvBuildFrameRecord(&out, &id, nullptr, 0);
    AvPut(&mr->Meta, out.Data, out.Size);

    // IoFrame: raw input -> this tick's state hash + chain link (mouse/keys empty in the headless drive).
    out.clear();
    AvBuildIoFrameRecord(&mr->Rec, &out, tick, nullptr, nullptr);
    AvPut(&mr->Meta, out.Data, out.Size);

    // InputHdr (once) + this tick's InputFrame, when an input log is attached (opt-in replay layer).
    if (mr->Rec.InputLog != nullptr)
    {
        out.clear();
        AvCollectInputRecords(&mr->Rec, &out);
        AvPut(&mr->Meta, out.Data, out.Size);
    }

    // StateSnapshot at snapshot ticks: the whole snapshottable block (nearest-snapshot restore point).
    if (snapshot)
    {
        out.clear();
        if (AvBuildStateSnapshotRecord(&out, mr->Rec.App, tick))
            AvPut(&mr->Meta, out.Data, out.Size);
    }
}

IMGUI_API void AppMetaRecordEnd(ImGuiAppMetaRecorder* mr, ImVector<char>* out_meta)
{
    IM_ASSERT(mr != nullptr);
    if (mr == nullptr)
        return;

    // Digest: the stream's FINAL record -- ImHashData over every preceding logical-stream byte (seed 0).
    const ImU64 stream_bytes = (ImU64)mr->Meta.Size;
    const ImU32 digest = (ImU32)ImHashData(mr->Meta.Data, (size_t)stream_bytes, 0);
    ImVector<char> out;
    AvBuildDigestRecord(&out, stream_bytes, digest);
    AvPut(&mr->Meta, out.Data, out.Size);

    if (out_meta != nullptr)
        out_meta->swap(mr->Meta);
    IM_DELETE(mr);
}

//-----------------------------------------------------------------------------
// [SECTION] Meta stream parsers
//-----------------------------------------------------------------------------

struct ImGuiAppAVMetaReader
{
    ImVector<char>       Bytes;
    ImGuiAppAVMetaHeader Header;
    int                  Cursor; // first record byte
};

static bool AvMetaInit(const void* meta, int meta_size, ImGuiAppAVMetaReader* r)
{
    if (meta == nullptr || meta_size < (int)sizeof(ImGuiAppAVMetaHeader))
        return false;
    r->Bytes.resize(meta_size);
    memcpy(r->Bytes.Data, meta, (size_t)meta_size);
    memcpy(&r->Header, r->Bytes.Data, sizeof(r->Header));
    if (memcmp(r->Header.Magic, AV_META_MAGIC, 8) != 0 || r->Header.Version != 1)
        return false;
    r->Cursor = (int)sizeof(ImGuiAppAVMetaHeader);
    return true;
}

// Advances to the next record; false at end or on a truncated tail (a crash mid-write
// leaves a partial record -- readers treat it as end-of-stream, WAL-style).
static bool AvMetaNext(ImGuiAppAVMetaReader* r, ImU32* out_type, const char** out_payload, ImU32* out_size)
{
    // 64-bit arithmetic: a corrupted size near UINT32_MAX must not wrap the bounds check.
    if ((ImS64)r->Cursor + (ImS64)(sizeof(ImU32) * 2) > (ImS64)r->Bytes.Size)
        return false;
    ImU32 type = 0;
    ImU32 size = 0;
    memcpy(&type, r->Bytes.Data + r->Cursor, sizeof(type));
    memcpy(&size, r->Bytes.Data + r->Cursor + sizeof(type), sizeof(size));
    if ((ImS64)r->Cursor + (ImS64)(sizeof(ImU32) * 2) + (ImS64)size > (ImS64)r->Bytes.Size)
        return false;
    *out_type = type;
    *out_payload = r->Bytes.Data + r->Cursor + sizeof(ImU32) * 2;
    *out_size = size;
    r->Cursor += (int)(sizeof(ImU32) * 2 + size);
    return true;
}

IMGUI_API bool AppAVMetaVerify(const void* meta, int meta_size, ImGuiAppAVStreamStats* out_stats)
{
    ImGuiAppAVStreamStats stats;
    bool header_ok = false;
    ImGuiAppAVMetaReader r;
    if (AvMetaInit(meta, meta_size, &r))
    {
        header_ok = true;
        ImU32 chain = 0;          // recomputed from the Identity seed across IoFrames
        bool chain_seeded = false;
        ImU32 type = 0;
        const char* p = nullptr;
        ImU32 size = 0;
        while (AvMetaNext(&r, &type, &p, &size))
        {
            switch (type)
            {
            case ImGuiAppAVMetaRecordType_Frame:
            {
                ImU64 tick = 0;
                memcpy(&tick, p, 8);
                if (tick == (ImU64)-1)
                    break;   // ring-dump reason marker
                if (stats.Frames > 0 && tick != stats.LastTick + 1)
                    stats.TickGaps++;
                if (stats.Frames == 0)
                    stats.FirstTick = tick;
                stats.LastTick = tick;
                stats.Frames++;
                break;
            }
            case ImGuiAppAVMetaRecordType_Identity:
            {
                if (size >= 24 && stats.Identities == 0)
                {
                    ImU32 schema_hash = 0;
                    memcpy(&schema_hash, p + 12, 4);
                    chain = schema_hash;   // the io chain's seed
                    chain_seeded = true;
                }
                stats.Identities++;
                break;
            }
            case ImGuiAppAVMetaRecordType_Digest:
            {
                if (size >= 12)
                {
                    ImU64 digest_bytes = 0;
                    ImU32 stored = 0;
                    memcpy(&digest_bytes, p, 8);
                    memcpy(&stored, p + 8, 4);
                    // The record's own start offset: everything before it is covered.
                    const ImS64 record_at = (ImS64)r.Cursor - 8 - (ImS64)size;
                    stats.DigestState = 2;
                    if ((ImS64)digest_bytes == record_at)
                    {
                        const ImU32 computed = (ImU32)ImHashData(r.Bytes.Data, (size_t)digest_bytes, 0);
                        stats.DigestState = computed == stored ? 0 : 2;
                    }
                }
                break;
            }
            case ImGuiAppAVMetaRecordType_InputHdr:      stats.InputHdrs++;   break;
            case ImGuiAppAVMetaRecordType_InputFrame:    stats.InputFrames++; break;
            case ImGuiAppAVMetaRecordType_StateSnapshot: stats.Snapshots++;   break;
            case ImGuiAppAVMetaRecordType_IoFrame:
            {
                // payload: u64 tick | 2*f32 mouse | u8 buttons | 2*f32 wheel | u32 state_hash | u32 chain | ...
                if (size >= 33)
                {
                    ImU32 state_hash = 0;
                    ImU32 stored_chain = 0;
                    memcpy(&state_hash, p + 25, 4);
                    memcpy(&stored_chain, p + 29, 4);
                    chain = (ImU32)ImHashData(&state_hash, sizeof(ImU32), chain);
                    if (stored_chain != chain && stats.ChainDivergesAt < 0)
                        stats.ChainDivergesAt = stats.IoFrames;
                }
                stats.IoFrames++;
                break;
            }
            default: break;
            }
        }
        stats.ChainOk = chain_seeded && stats.ChainDivergesAt < 0 && stats.IoFrames > 0;
    }

    const bool contiguous = stats.TickGaps == 0 && stats.Frames > 0 && (ImU64)stats.Frames == stats.LastTick - stats.FirstTick + 1;
    const bool io_complete = stats.IoFrames == stats.Frames;   // the every-frame contract extends to input
    const bool pass = header_ok && contiguous && io_complete && stats.ChainOk && stats.Identities > 0 && stats.DigestState == 0;
    if (out_stats != nullptr)
        *out_stats = stats;
    return pass;
}

IMGUI_API bool AppAVMetaDump(const void* meta, int meta_size)
{
    ImGuiAppAVMetaReader r;
    if (!AvMetaInit(meta, meta_size, &r))
        return false;
    printf("# meta stream: v%u fps=%.3f start_tsc=%llu qpc_hz=%llu\n", r.Header.Version, r.Header.Fps,
           (unsigned long long)r.Header.StartTsc, (unsigned long long)r.Header.QpcHz);
    printf("type\tframe\ttsc\ttime_sec\tsize\n");
    ImU32 type = 0;
    const char* p = nullptr;
    ImU32 size = 0;
    while (AvMetaNext(&r, &type, &p, &size))
    {
        switch (type)
        {
        case ImGuiAppAVMetaRecordType_Frame:
        {
            ImU64 idx = 0;
            ImU64 tsc = 0;
            double sec = 0.0;
            ImU32 user = 0;
            memcpy(&idx, p, 8); memcpy(&tsc, p + 8, 8); memcpy(&sec, p + 16, 8); memcpy(&user, p + 24, 4);
            printf("frame\t%llu\t%llu\t%.6f\t%u\n", (unsigned long long)idx, (unsigned long long)tsc, sec, user);
            break;
        }
        case ImGuiAppAVMetaRecordType_InputHdr:
            printf("input_hdr\t-\t-\t-\t%u\n", size);
            break;
        case ImGuiAppAVMetaRecordType_InputFrame:
        {
            ImU64 idx = 0;
            memcpy(&idx, p, 8);
            printf("input\t%llu\t-\t-\t%u\n", (unsigned long long)idx, size);
            break;
        }
        case ImGuiAppAVMetaRecordType_StateSnapshot:
        {
            ImU64 idx = 0;
            memcpy(&idx, p + 4, 8);
            printf("state\t%llu\t-\t-\t%u\n", (unsigned long long)idx, size);
            break;
        }
        case ImGuiAppAVMetaRecordType_None: default:
            printf("type%u\t-\t-\t-\t%u\n", type, size);
            break;
        }
    }
    return true;
}

IMGUI_API bool AppAVMetaReadInputLog(const void* meta, int meta_size, ImGuiAppInputLog* out_log)
{
    IM_ASSERT(out_log != nullptr);
    if (out_log == nullptr)
        return false;
    ImGuiAppAVMetaReader r;
    if (!AvMetaInit(meta, meta_size, &r))
        return false;
    AppInputLogClear(out_log);

    bool have_hdr = false;
    ImU32 type = 0;
    const char* p = nullptr;
    ImU32 size = 0;
    while (AvMetaNext(&r, &type, &p, &size))
    {
        if (type == ImGuiAppAVMetaRecordType_InputHdr)
        {
            // A second header restarts the take (composition changed mid-recording).
            AppInputLogClear(out_log);
            ImU32 comp = 0;
            ImU32 frame_size = 0;
            ImU32 slots = 0;
            memcpy(&comp, p, 4); memcpy(&frame_size, p + 4, 4); memcpy(&slots, p + 8, 4);
            if ((ImS64)size < 12 + (ImS64)slots * 12)   // 64-bit: corrupted slot count must not wrap
                return false;
            out_log->CompositionID = (ImGuiID)comp;
            out_log->FrameSize = (int)frame_size;
            const char* s = p + 12;
            for (ImU32 i = 0; i < slots; i++)
            {
                ImU32 id = 0;
                ImS32 off = 0;
                ImS32 sz = 0;
                memcpy(&id, s, 4); memcpy(&off, s + 4, 4); memcpy(&sz, s + 8, 4);
                s += 12;
                out_log->SlotIds.push_back((ImGuiID)id);
                out_log->SlotOffsets.push_back((int)off);
                out_log->SlotSizes.push_back((int)sz);
            }
            have_hdr = true;
        }
        else if (type == ImGuiAppAVMetaRecordType_InputFrame && have_hdr)
        {
            if ((int)size != 12 + out_log->FrameSize)
                return false;
            ImU32 hash = 0;
            memcpy(&hash, p + 8, 4);
            const int base = out_log->Frames.Size;
            out_log->Frames.resize(base + out_log->FrameSize);
            memcpy(out_log->Frames.Data + base, p + 12, (size_t)out_log->FrameSize);
            out_log->StateHashes.push_back((ImGuiID)hash);
            out_log->Count++;
        }
    }
    return have_hdr && out_log->Count > 0;
}

IMGUI_API bool AppAVMetaReadStateSnapshot(const void* meta, int meta_size, ImVector<char>* out_bytes, ImGuiID* out_composition_id)
{
    IM_ASSERT(out_bytes != nullptr);
    if (out_bytes == nullptr)
        return false;
    ImGuiAppAVMetaReader r;
    if (!AvMetaInit(meta, meta_size, &r))
        return false;

    // First snapshot wins: it is the take's starting state for reproduction runs.
    ImU32 type = 0;
    const char* p = nullptr;
    ImU32 size = 0;
    while (AvMetaNext(&r, &type, &p, &size))
    {
        if (type != ImGuiAppAVMetaRecordType_StateSnapshot)
            continue;
        if (size < 16)
            return false;
        ImU32 comp = 0;
        ImU32 slots = 0;
        memcpy(&comp, p, 4);
        memcpy(&slots, p + 12, 4);
        const ImS64 table = 16 + (ImS64)slots * 8;   // 64-bit: corrupted slot count must not wrap
        if ((ImS64)size < table)
            return false;
        if (out_composition_id != nullptr)
            *out_composition_id = (ImGuiID)comp;
        out_bytes->resize((int)(size - table));
        memcpy(out_bytes->Data, p + table, (size_t)(size - table));
        return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
// [SECTION] Run index (F62)
//-----------------------------------------------------------------------------

// Tick -> Ticks[] slot. Records land right after their Frame, so the last-pushed tick is
// the common hit; a reverse scan covers the rare out-of-order (ring dump) case.
static int RunTickSlot(const ImGuiAppRunIndex* run, ImU64 tick)
{
    for (int i = run->Ticks.Size - 1; i >= 0; i--)
        if (run->Ticks.Data[i].Tick == tick)
            return i;
    return -1;
}

IMGUI_API ImGuiAppRunIndex* AppRunOpen(const void* meta, int meta_size)
{
    ImGuiAppAVMetaReader r;
    if (!AvMetaInit(meta, meta_size, &r))
        return nullptr;

    ImGuiAppRunIndex* run = IM_NEW(ImGuiAppRunIndex)();
    run->Meta.resize(meta_size);
    memcpy(run->Meta.Data, meta, (size_t)meta_size);   // record offsets index THIS copy
    run->Header = r.Header;

    // The shipped verify ladder over the same bytes: authoritative counts, chain, digest, gaps.
    AppAVMetaVerify(meta, meta_size, &run->Stats);

    // ONE linear walk (the AppAVMetaVerify traversal): Frame records lay the tick spine in
    // emission order (== QOI NNNNNN / mp4 sample ordinal); Io/Input/Snapshot land on their
    // own tick by frame_index; the io chain recomputes from the Identity seed per tick.
    ImU32 chain = 0;
    bool identity_decoded = false;
    int frame_ordinal = 0;
    ImU32 type = 0;
    const char* p = nullptr;
    ImU32 size = 0;
    while (AvMetaNext(&r, &type, &p, &size))
    {
        const int payload_off = (int)(p - r.Bytes.Data);
        switch (type)
        {
        case ImGuiAppAVMetaRecordType_Identity:
            // applayer_ver | imgui_ver | composition_id | schema_hash | embed_rows | block | rsvd
            if (size >= 24 && !identity_decoded)
            {
                ImU32 av = 0, iv = 0, comp = 0, sh = 0, rows = 0;
                ImU16 block = 0;
                memcpy(&av, p, 4); memcpy(&iv, p + 4, 4); memcpy(&comp, p + 8, 4);
                memcpy(&sh, p + 12, 4); memcpy(&rows, p + 16, 4); memcpy(&block, p + 20, 2);
                run->Identity.ApplayerVersion = av;
                run->Identity.ImguiVersion = iv;
                run->Identity.CompositionID = (ImGuiID)comp;
                run->Identity.SchemaHash = sh;
                run->Identity.EmbedRows = rows;
                run->Identity.BlockSize = block;
                chain = sh;   // the io chain's seed
                identity_decoded = true;
            }
            break;
        case ImGuiAppAVMetaRecordType_Frame:
        {
            ImU64 tick = 0;
            memcpy(&tick, p, 8);
            if (tick == (ImU64)-1)
                break;   // ring-dump reason marker: not a real tick
            ImGuiAppRunTick t;
            t.Tick = tick;
            memcpy(&t.Tsc, p + 8, 8);
            memcpy(&t.TimeSec, p + 16, 8);
            t.FrameImage = frame_ordinal++;
            run->Ticks.push_back(t);
            break;
        }
        case ImGuiAppAVMetaRecordType_IoFrame:
        {
            if (size >= 33)
            {
                ImU64 tick = 0;
                ImU32 state_hash = 0;
                memcpy(&tick, p, 8);
                memcpy(&state_hash, p + 25, 4);
                chain = (ImU32)ImHashData(&state_hash, sizeof(ImU32), chain);
                const int slot = RunTickSlot(run, tick);
                if (slot >= 0)
                {
                    run->Ticks.Data[slot].IoOffset = payload_off;
                    run->Ticks.Data[slot].StateHash = state_hash;
                    run->Ticks.Data[slot].Chain = chain;
                }
            }
            break;
        }
        case ImGuiAppAVMetaRecordType_InputFrame:
        {
            if (size >= 12)
            {
                ImU64 tick = 0;
                memcpy(&tick, p, 8);
                const int slot = RunTickSlot(run, tick);
                if (slot >= 0)
                    run->Ticks.Data[slot].InputOffset = payload_off;
            }
            break;
        }
        case ImGuiAppAVMetaRecordType_StateSnapshot:
        {
            if (size >= 16)
            {
                ImU64 tick = 0;
                memcpy(&tick, p + 4, 8);   // comp(4) then frame_index
                const int slot = RunTickSlot(run, tick);
                if (slot >= 0)
                {
                    run->Ticks.Data[slot].SnapshotOffset = payload_off;
                    run->SnapshotTicks.push_back(slot);   // ascending: snapshots arrive in tick order
                }
            }
            break;
        }
        default: break;
        }
    }
    return run;
}

IMGUI_API void AppRunClose(ImGuiAppRunIndex* run)
{
    IM_DELETE(run);   // null-safe
}

IMGUI_API int AppRunTickCount(const ImGuiAppRunIndex* run)
{
    return run != nullptr ? run->Ticks.Size : 0;
}

IMGUI_API const ImGuiAppRunTick* AppRunTickAt(const ImGuiAppRunIndex* run, int i)
{
    if (run == nullptr || i < 0 || i >= run->Ticks.Size)
        return nullptr;
    return &run->Ticks.Data[i];
}

IMGUI_API int AppRunViewCount(const ImGuiAppRunView* view)
{
    return view != nullptr ? AppRunTickCount(view->Run) : 0;
}

IMGUI_API bool AppRunViewShow(ImGuiAppRunView* view, int i)
{
    if (view == nullptr)
        return false;
    const ImGuiAppRunTick* t = AppRunTickAt(view->Run, i);
    if (t == nullptr)
        return false;
    // The slider index addresses Ticks[i].Tick directly: step ALWAYS lands on an exact tick.
    view->Scrub = i;
    view->ShownTick = t->Tick;
    view->ShownImage = -1;
    if (t->FrameImage < 0 || view->Decode == nullptr)
        return true;   // ring-dump gap or tick-only scrub: the tick landed, there is nothing to blit
    int w = 0;
    int h = 0;
    if (!view->Decode(view->DecodeUser, t->FrameImage, &view->Pixels, &w, &h))
        return false;   // hard decode failure: ShownImage stays -1
    view->Width = w;
    view->Height = h;
    view->ShownImage = t->FrameImage;
    return true;
}

//-----------------------------------------------------------------------------
// [SECTION] State-at-tick (F64)
//-----------------------------------------------------------------------------

// Greatest SnapshotTicks entry whose tick-array index is <= tick_index (binary search over the
// ascending list). Returns the SnapshotTicks slot, or -1 when no snapshot lands at/before N.
static int RunNearestSnapshot(const ImGuiAppRunIndex* run, int tick_index)
{
    int lo = 0;
    int hi = run->SnapshotTicks.Size - 1;
    int best = -1;
    while (lo <= hi)
    {
        const int mid = (lo + hi) / 2;
        if (run->SnapshotTicks.Data[mid] <= tick_index)
        {
            best = mid;
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }
    }
    return best;
}

// State bytes of the StateSnapshot record whose PAYLOAD begins at payload_off in run->Meta: skip
// the {comp, frame_index, slot table} preamble, copy the concatenated slot bytes. The record's TLV
// size sits at payload_off-4 (AvRecordAppend framing). Layout mirrors AvBuildStateSnapshotRecord.
static bool RunReadSnapshotBytes(const ImGuiAppRunIndex* run, int payload_off, ImVector<char>* out)
{
    if (payload_off < 8 || payload_off + 4 > run->Meta.Size)
        return false;
    ImU32 size = 0;
    memcpy(&size, run->Meta.Data + payload_off - 4, sizeof(size));
    if (size < 16 || (ImS64)payload_off + (ImS64)size > (ImS64)run->Meta.Size)
        return false;
    const char* p = run->Meta.Data + payload_off;
    ImU32 slots = 0;
    memcpy(&slots, p + 12, sizeof(slots));           // comp(4) + frame_index(8) then slot count
    const ImS64 table = 16 + (ImS64)slots * 8;       // 64-bit: a corrupted slot count must not wrap
    if ((ImS64)size < table)
        return false;
    const int len = (int)((ImS64)size - table);
    out->resize(len);
    if (len > 0)
        memcpy(out->Data, p + table, (size_t)len);
    return true;
}

// Reconstructed input-log frame index of Ticks[k] == count of ticks in [0,k) carrying an InputFrame
// (records land in tick/emission order, so this is that tick's position in AppAVMetaReadInputLog).
static int RunInputCountBefore(const ImGuiAppRunIndex* run, int k)
{
    int n = 0;
    const int lim = k < run->Ticks.Size ? k : run->Ticks.Size;
    for (int i = 0; i < lim; i++)
        if (run->Ticks.Data[i].InputOffset >= 0)
            n++;
    return n;
}

// The identity gate, single-sourced (section 5.1): reconstruction is legal only for the recorded
// composition + schema. UI readouts render THIS verdict, never a re-implemented compare.
IMGUI_API bool AppRunIdentityMatches(const ImGuiApp* app, const ImGuiAppRunIndex* run)
{
    return app != nullptr && run != nullptr
        && GetAppCompositionID(app) == run->Identity.CompositionID
        && AppStateSchemaHash(app) == run->Identity.SchemaHash;
}

IMGUI_API bool AppRunStateAtTick(ImGuiApp* recon_app, const ImGuiAppRunIndex* run, int tick_index, ImGuiAppRunState* out)
{
    ImGuiAppRunState st;
    if (out != nullptr)
        *out = st;
    IM_ASSERT(recon_app != nullptr && run != nullptr);
    if (recon_app == nullptr || run == nullptr || tick_index < 0 || tick_index >= run->Ticks.Size)
        return false;

    const ImGuiAppRunTick& tN = run->Ticks.Data[tick_index];
    st.TickIndex = tick_index;
    st.Tick = tN.Tick;
    st.RecordedStateHash = tN.StateHash;
    st.CmdFirst = tN.WalFirst;
    st.CmdCount = tN.WalCount;

    if (!AppRunIdentityMatches(recon_app, run))
    {
        if (out != nullptr)
            *out = st;
        return false;
    }

    // Nearest snapshot S <= N (section 5.2). No snapshot at/before N: values unavailable, degrade.
    const int snap_slot = RunNearestSnapshot(run, tick_index);
    if (snap_slot < 0)
    {
        if (out != nullptr)
            *out = st;
        return false;
    }
    const int sS = run->SnapshotTicks.Data[snap_slot];
    const ImGuiAppRunTick& tS = run->Ticks.Data[sS];
    st.SnapshotTickIndex = sS;
    st.SnapshotTick = tS.Tick;

    // Restore the recorded snapshot bytes: AppStateSnapshot builds the recon app's slot layout (frame
    // 0 = its current state), then we overwrite frame 0 with the recorded bytes and restore -- the
    // mirror image of AppStateRestore. Schema equality above guarantees layout + size agreement.
    ImGuiAppStateHistory h;
    ImVector<char> snap_bytes;
    if (!AppStateSnapshot(recon_app, &h)
      || !RunReadSnapshotBytes(run, tS.SnapshotOffset, &snap_bytes)
      || snap_bytes.Size != h.FrameSize)
    {
        if (out != nullptr)
            *out = st;
        return false;
    }
    memcpy(h.Frames.Data, snap_bytes.Data, (size_t)h.FrameSize);
    if (!AppStateRestore(recon_app, &h, 0))
    {
        if (out != nullptr)
            *out = st;
        return false;
    }

    // Replay ticks (S, N] (section 5.3): a sub-slice of the reconstructed input log through
    // AppInputReplay -- restore already placed tick S's TempData, which tick S+1's update consumes.
    int first_div = -1;
    if (sS < tick_index)
    {
        ImGuiAppInputLog full;
        const int first_lf = RunInputCountBefore(run, sS + 1);      // log frame of tick S+1
        const int last_lf = RunInputCountBefore(run, tick_index + 1) - 1;   // log frame of tick N (inclusive)
        if (!AppAVMetaReadInputLog(run->Meta.Data, run->Meta.Size, &full)
        || first_lf < 0 || last_lf < first_lf || last_lf >= full.Count)
        {
            if (out != nullptr)
                *out = st;
            return false;   // no opt-in input to bridge S -> N: values only at snapshot ticks
        }
        ImGuiAppInputLog sub;
        sub.CompositionID = full.CompositionID;
        sub.FrameSize = full.FrameSize;
        sub.SlotIds = full.SlotIds;
        sub.SlotOffsets = full.SlotOffsets;
        sub.SlotSizes = full.SlotSizes;
        sub.Count = last_lf - first_lf + 1;
        sub.Frames.resize(sub.Count * sub.FrameSize);
        memcpy(sub.Frames.Data, full.Frames.Data + (ImS64)first_lf * full.FrameSize, (size_t)sub.Count * (size_t)sub.FrameSize);
        for (int f = first_lf; f <= last_lf; f++)
            sub.StateHashes.push_back(full.StateHashes.Data[f]);
        if (!AppInputReplay(recon_app, &sub, &first_div))
        {
            if (out != nullptr)
                *out = st;
            return false;
        }
        st.ReplayedFrames = sub.Count;
    }

    st.FirstDivergence = first_div;
    st.ReconstructedHash = AppStateHash(recon_app);
    st.Reconstructed = true;
    if (out != nullptr)
        *out = st;
    return true;
}

IMGUI_API bool AppRunAttachWal(ImGuiAppRunIndex* run, const char* wal_path)
{
    IM_ASSERT(run != nullptr);
    if (run == nullptr)
        return false;

    // Re-parsing clears the prior annotation (idempotent).
    run->Commands.clear();
    for (int i = 0; i < run->Ticks.Size; i++)
    {
        run->Ticks.Data[i].WalFirst = -1;
        run->Ticks.Data[i].WalCount = 0;
    }
    if (wal_path == nullptr)
        return false;

    size_t size = 0;
    void* bytes = ImFileLoadToMemory(wal_path, "rb", &size, 1);   // +1: null-terminated for line scan
    if (bytes == nullptr)
        return false;

    // "[tick:N ...] ... execute command K" (imguiapp.cpp Command layer). Bucket by tick; lines arrive
    // in tick order (WAL is append-only per frame), so the per-tick slice is contiguous.
    char* line = (char*)bytes;
    char* buf_end = (char*)bytes + size;
    while (line < buf_end)
    {
        char* nl = strchr(line, '\n');
        if (nl != nullptr)
            *nl = 0;
        const char* tk = strstr(line, "[tick:");
        const char* ex = strstr(line, "execute command ");
        if (tk != nullptr && ex != nullptr)
        {
            ImGuiAppRunCommand cmd;
            cmd.Tick = (ImU64)strtoull(tk + 6, nullptr, 10);
            cmd.CommandId = (int)strtol(ex + 16, nullptr, 10);   // 16 = strlen("execute command ")
            run->Commands.push_back(cmd);
        }
        if (nl == nullptr)
            break;
        line = nl + 1;
    }
    IM_FREE(bytes);

    // Fill each tick's contiguous slice into Commands (tick == Ticks[i].Tick).
    for (int c = 0; c < run->Commands.Size; c++)
    {
        for (int i = 0; i < run->Ticks.Size; i++)
        {
            if (run->Ticks.Data[i].Tick != run->Commands.Data[c].Tick)
                continue;
            if (run->Ticks.Data[i].WalFirst < 0)
                run->Ticks.Data[i].WalFirst = c;
            run->Ticks.Data[i].WalCount++;
            break;
        }
    }
    return true;
}

} // namespace ImGui

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif // #ifndef IMGUI_DISABLE
