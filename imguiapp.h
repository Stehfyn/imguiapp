// dear imgui app, v0.5.0 WIP
// (main application-layer header; internal API in imguiapp_internal.h; config in imappconfig.h)

#pragma once

/*

Index of this file:
// [SECTION] Header mess
// [SECTION] Forward declarations and basic types
// [SECTION] Dear ImGui end-user API functions
// [SECTION] Flags & Enumerations
// [SECTION] AV recording types + interface
// [SECTION] Configuration structs
// [SECTION] Helpers (interface base, state-delta events, RNG, diagnostics, style/color/data descs)
// [SECTION] App object model (layers, items, controls, ImGuiApp, adapter/control templates)
// [SECTION] Inline composition API (Push/Pop layers, windows, sidebars, controls; needs a complete ImGuiApp)

*/

//-----------------------------------------------------------------------------
// [SECTION] Header mess
//-----------------------------------------------------------------------------

#include "imappconfig.h"    // applayer compile-time switches (first, like imconfig.h)
#include "imgui.h"          // IMGUI_API, ImGuiID, ImGuiStorage, ImBitArray, ImGuiTextIndex, ImChunkStream
#include "imgui_internal.h" // ImStrncpy
// windows.h's min/max macros (leaked by platform-backend TUs) break imguiapp_reflect.h's std::min.
#include "imguiapp_reflect.h" // compile-time type-identity + reflection (qlibs/reflect port): ImGuiAppStatic/Type, ImApp{GenerateLabel,TypeDisplayName,NulTerminate}, field walk (AppReflectFields), ImGuiAppLiveFieldDesc/ImGuiAppTypeSchema, ImAppIndexSeq + <type_traits> for the template front (Δ2)

// Version scheme: "0.Y.Z WIP" while unreleased; NUM = Y*100 + Z, strictly monotonic (keep both in
// sync). Version-stamp grammar for comments: "Since 0.Y.Z (Month Year, NUM)".
#define IMGUIAPP_VERSION     "0.5.0 WIP"
#define IMGUIAPP_VERSION_NUM 500
// Assert the compiled library and the including TU agree on version + core struct layouts.
// ImGui::InitializeApp calls it; apps embedding the applayer as a DLL should call it too.
#define IMGUIAPP_CHECKVERSION() ImGui::AppDebugCheckVersionAndDataLayout(IMGUIAPP_VERSION, sizeof(ImGuiApp), sizeof(ImGuiAppConfig), sizeof(ImGuiAppFrameConfig))

// Platform seams for diagnostics (harness + backends); no Im* equivalent exists upstream.
#ifndef IMGUIAPP_ERROR_PRINTF
#include <stdio.h>
#define IMGUIAPP_ERROR_PRINTF(_FMT,...) fprintf(stderr, _FMT, ##__VA_ARGS__)    // You can override the default error output by editing imappconfig.h
#endif
#ifndef IMGUIAPP_ABORT
#include <stdlib.h>
#define IMGUIAPP_ABORT()                abort()                                 // You can override the default fatal-exit handler by editing imappconfig.h
#endif

#ifndef IMGUI_DISABLE


#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495) // [Static Analyzer] Variable 'XXX' is uninitialized. Always initialize a member variable (type.6).
#endif
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
//-----------------------------------------------------------------------------
// [SECTION] Forward declarations and basic types
//-----------------------------------------------------------------------------

// Forward declarations: ImGuiApp layer
struct ImGuiApp;
struct ImGuiAppBase;
struct ImGuiAppCommandLayer;
struct ImGuiAppDisplayNodeBase;
struct ImGuiAppLayer;
struct ImGuiAppLayerBase;
struct ImGuiAppNodeBase;
struct ImGuiAppTaskLayer;

// Forward declarations: ImGuiAppControl layer
template <typename PersistDataT, typename TempDataT, typename... DataDependencies>
struct ImGuiAppControl;
struct ImGuiAppControlBase;
template <typename PersistDataT, typename TempDataT, typename... DataDependencies>
struct ImGuiAppControlMirrorAdapter;
template <typename... DataDependencies>
struct ImGuiAppDependencySlots;
template <typename Base, typename PersistDataT, typename TempDataT, typename... DataDependencies>
struct ImGuiAppInterfaceAdapter;
template <typename PersistDataT, typename TempDataT, typename... DataDependencies>
struct ImGuiAppInterfaceAdapterBase;

// Forward declarations: ImGuiAppDisplay layer
struct ImGuiAppWindowBase;

// Forward declarations: ImGuiAppSidebar layer
struct ImGuiAppSidebarBase;

// Forward declarations: state history + input log (time travel / record-replay)
struct ImGuiAppInputLog;
struct ImGuiAppStateHistory;

// Configuration + basic structs (defined in [SECTION] Configuration structs / Helpers below).
struct ImGuiAppColorModDesc;
struct ImGuiAppConfig;
struct ImGuiAppDataBinding;
struct ImGuiAppStyleModDesc;
struct ImGuiAppThreadFuncs;
struct ImGuiAppFileSystemFuncs;
struct ImGuiAppWAL;

// Enumerations (bodies in [SECTION] Flags & Enumerations below).
enum ImGuiAppCommand : int;            // -> enum ImGuiAppCommand         // dispatched app command

// Flags (full enum lists in [SECTION] Flags & Enumerations below).
typedef int ImGuiAppAVTimingMode;      // -> enum ImGuiAppAVTimingMode_      // Enum: what time the recorded video claims
typedef int ImGuiAppFrameFlags;        // -> enum ImGuiAppFrameFlags_        // Flags: per-frame clear/present/platform-window control
typedef int ImGuiAppHeadlessMode;      // -> enum ImGuiAppHeadlessMode_      // Enum: windowed / null / offscreen
typedef int ImGuiAppPacerMode;         // -> enum ImGuiAppPacerMode_         // Enum: advisory frame-pacer level
typedef int ImGuiAppRecordQueuePolicy; // -> enum ImGuiAppRecordQueuePolicy_ // Enum: encoder queue overflow policy
typedef int ImGuiAppStyle;             // -> enum ImGuiAppStyle_             // Enum: bundled style preset
typedef int ImGuiAppWALLevel;          // -> enum ImGuiAppWALLevel_          // Enum: write-ahead-log verbosity

//-----------------------------------------------------------------------------
// [SECTION] Dear ImGui end-user API functions
//-----------------------------------------------------------------------------

namespace ImGui
{
    // Lifecycle
    IMGUI_API bool        AppDebugCheckVersionAndDataLayout(const char* version, size_t sz_app, size_t sz_config, size_t sz_frame_config); // IMGUIAPP_CHECKVERSION() target
    IMGUI_API void        InitializeApp(ImGuiApp* app, const ImGuiAppConfig* config = nullptr);
    IMGUI_API void        ShutdownApp(ImGuiApp* app);
    IMGUI_API void        UpdateApp(ImGuiApp* app);           // dt = GetIO().DeltaTime
    IMGUI_API void        UpdateApp(ImGuiApp* app, float dt); // explicit dt (replay injects here)
    IMGUI_API void        RenderApp(const ImGuiApp* app);
    IMGUI_API void        DrawAppFrame(ImGuiApp* app);         // the draw phase's layer/widget submission body (UpdateApp + OnLayout + RenderApp)

    // Composition: push/pop layers, windows, sidebars, controls (templates defined in the inline section below)
    template <typename T>
    IMGUI_API inline void PushAppLayer(ImGuiApp* app);
    IMGUI_API void        PopAppLayer(ImGuiApp* app);

    template <typename T>
    IMGUI_API inline void PushAppSidebar(ImGuiApp* app, ImGuiViewport* vp, ImGuiDir dir, float size = 0.0f, ImGuiWindowFlags flags = 0);
    IMGUI_API void        PopAppSidebar(ImGuiApp* app);

    template <typename T>
    IMGUI_API inline void PushAppWindow(ImGuiApp* app);
    IMGUI_API void        PopAppWindow(ImGuiApp* app);

    // instance: client-chosen discriminator; 0 = the type singleton, any other value keys a distinct
    // instance of the same control data type. binds routes dependencies to specific producer instances;
    // unrouted ones resolve to the pusher's instance id, then the singleton. Producer pushed first either way.
    template <typename T>
    IMGUI_API inline T*   AppControlCreate(ImGuiApp* app, ImGuiID instance, const ImGuiAppDataBinding* binds, int binds_count, const char* host_kind, const char* host_label);
    template <typename T>
    IMGUI_API inline void PushAppControl(ImGuiApp* app, ImGuiID instance = 0, const ImGuiAppDataBinding* binds = nullptr, int binds_count = 0);
    IMGUI_API void        PopAppControl(ImGuiApp* app);
    // No PopWindowControl/PopSidebarControl: a window/sidebar control is owned by its host item
    // and pops with it (PopAppWindow / PopAppSidebar).
    template <typename T>
    IMGUI_API inline void PushWindowControl(ImGuiApp* app, ImGuiAppWindowBase* window, ImGuiID instance = 0, const ImGuiAppDataBinding* binds = nullptr, int binds_count = 0);
    template <typename T>
    IMGUI_API inline void PushSidebarControl(ImGuiApp* app, ImGuiAppSidebarBase* sidebar, ImGuiID instance = 0, const ImGuiAppDataBinding* binds = nullptr, int binds_count = 0);

    // Composition identity
    // Identity of the app's composition (layers, windows/sidebars, controls, in order). Changes exactly
    // when something is pushed or popped; mirrors poll it and reconcile only on change.
    IMGUI_API ImGuiID     GetAppCompositionID(const ImGuiApp* app);

    // Storage registration (size > 0 => snapshottable; a TempData byte range enables input record/replay)
    IMGUI_API void        RegisterAppStorage(ImGuiApp* app, ImGuiID id, void* ptr, void (*destroy)(void*));
    IMGUI_API void        RegisterAppStorage(ImGuiApp* app, ImGuiID id, void* ptr, int size, void (*destroy)(void*));                                 // size > 0 => snapshottable
    IMGUI_API void        RegisterAppStorage(ImGuiApp* app, ImGuiID id, void* ptr, int size, int temp_offset, int temp_size, void (*destroy)(void*)); // + input (TempData) byte range
    template <typename T>
    IMGUI_API inline void DestroyAppStorageValue(void* ptr);
    
    // Register a control's instance data (id-keyed). snapshottable => registers inst_size + TempData byte range
    // (snapshot/replay); otherwise registers opaque (caller passes the type-derived sizes/offset/destroy).
    IMGUI_API void        RegisterAppControlStorage(ImGuiApp* app, ImGuiID id, void* instance_data, bool snapshottable, int inst_size, int temp_offset, int temp_size, void (*destroy)(void*));
    // Type-erased front of PushAppControl / PushWindowControl / PushSidebarControl: WAL-logs the push
    // (host_kind/host_label = owning window/sidebar, or null), labels the control, registers its instance
    // storage. The typed template caller then wires _InstanceID/_InstanceData and ResolveDependencies.
    IMGUI_API void        AppControlRegisterStorage(ImGuiApp* app, ImGuiAppControlBase* control, const char* name, ImGuiID data_type_id, ImGuiID instance, void* instance_data, bool snapshottable, int inst_size, int temp_offset, int temp_size, void (*destroy)(void*), const char* host_kind, const char* host_label);
    // Type-erased tail: append the wired node to its owning list, initialize and attach it.
    IMGUI_API void        AppControlPush(ImGuiApp* app, ImVector<ImGuiAppDisplayNodeBase*>* list, ImGuiAppDisplayNodeBase* node);
    IMGUI_API void        UnregisterAppStorage(ImGuiApp* app, ImGuiID id);                                                                            // destroys + removes one entry
    IMGUI_API void        ClearAppStorage(ImGuiApp* app);

    // Internal helper for the inline Push templates below (defined in imguiapp.cpp; must stay here because
    // those public templates call it). ShutdownAppNodes -- cpp-only -- lives in imguiapp_internal.h.
    IMGUI_API void        AppDeduplicateItemLabel(char* label, int label_size, const ImVector<ImGuiAppWindowBase*>* windows, const ImVector<ImGuiAppSidebarBase*>* sidebars);
    // Dependency-slot key resolution for the adapter template below (order: explicit binding ->
    // the control's own instance id -> the type singleton). Defined in imguiapp.cpp.
    IMGUI_API ImGuiID     AppResolveDependencyKey(const ImGuiApp* app, ImGuiID type_id, ImGuiID instance_id, const ImGuiAppDataBinding* binds, int binds_count, bool* out_optional);
    // Non-template tail of PushAppLayer/Window/Sidebar (WAL-log the push + dedup label + append + attach/init).
    // The template caller constructs the item (IM_NEW<T>) and its type name; these do everything else, in imguiapp.cpp.
    IMGUI_API void        AppRegisterLayer(ImGuiApp* app, ImGuiAppLayerBase* layer, const char* name);
    IMGUI_API void        AppRegisterWindow(ImGuiApp* app, ImGuiAppWindowBase* window, const char* name);
    IMGUI_API void        AppRegisterSidebar(ImGuiApp* app, ImGuiAppSidebarBase* sidebar, const char* name, ImGuiViewport* vp, ImGuiDir dir, float size, ImGuiWindowFlags flags = 0);

    // State snapshot / time-travel
    // Snapshot appends snapshottable state to the ring (layout rebuilt + history cleared on composition
    // change); Restore copies snapshot `index` (0 = oldest) into the live app. False = nothing
    // snapshottable / invalid index or composition.
    IMGUI_API bool        AppStateSnapshot(ImGuiApp* app, ImGuiAppStateHistory* h);
    IMGUI_API bool        AppStateRestore(ImGuiApp* app, ImGuiAppStateHistory* h, int index);
    IMGUI_API void        AppStateHistoryClear(ImGuiAppStateHistory* h);

    // State hashing (per-frame fingerprint + slot-layout schema hash)
    // Hash of the Persist + LastTemp prefix of every snapshottable instance -- the same
    // per-frame fingerprint AppInputRecord stores. 0 when nothing snapshottable exists.
    IMGUI_API ImGuiID     AppStateHash(const ImGuiApp* app);

    // Fingerprint of the snapshottable slot LAYOUT (id + size + temp range per entry, in StorageEntries
    // order) -- what state hashes and snapshots depend on. Carried in the take's Identity record; F64's
    // reconstruction identity gate requires equality. 0 when nothing snapshottable exists.
    IMGUI_API ImU32       AppStateSchemaHash(const ImGuiApp* app);

    // Input record / replay
    // AppInputRecord appends this frame's inputs (every control's TempData + dt) + resulting state hash; call
    // once per frame AFTER RenderApp. AppInputReplay re-runs the frames through UpdateApp (no rendering) --
    // restore the starting state first. out_first_divergence: first frame whose hash differs; -1 = deterministic.
    IMGUI_API bool        AppInputRecord(ImGuiApp* app, ImGuiAppInputLog* log, float dt);
    IMGUI_API bool        AppInputReplay(ImGuiApp* app, const ImGuiAppInputLog* log, int* out_first_divergence);
    IMGUI_API void        AppInputLogClear(ImGuiAppInputLog* log);

    // Frame pacing
    // Advisory frame pacing. Backend run loops call this once per iteration before OnDrawFrame; Off
    // returns immediately (the call is unconditional in the loops). The clock and the wait come from
    // the client-installed impl seam (ImGuiAppPacer::Impl; null free-runs); Fixed mode also forces
    // io.DeltaTime to exactly 1/TargetHz.
    IMGUI_API void        AppPacerWait(ImGuiApp* app);

    // The rate the pacer actually paces at: TargetHz when positive, else the primary
    // monitor's refresh rate (the same resolution AppPacerWait performs). Callers that
    // need the frame rate (e.g. an encode config) read it here instead of guessing.
    IMGUI_API float       AppPacerResolveHz(const ImGuiApp* app);

    // Consulted by the backend's per-viewport present hook (Renderer_SwapBuffers /
    // Platform_RenderWindow). True = present this frame; false = skip (contents unchanged
    // on that monitor until its next deadline). Main viewport never skips; Off pacer never skips.
    IMGUI_API bool        AppPacerViewportShouldPresent(ImGuiApp* app, ImGuiViewport* viewport);

    // Write-ahead log
    // AppWALWrite appends one record and flushes to disk BEFORE returning; records below the WAL's level
    // are dropped. All three are null-safe on wal.
    IMGUI_API bool        AppWALOpen(ImGuiAppWAL* wal, const char* path, ImGuiAppWALLevel level);
    IMGUI_API void        AppWALClose(ImGuiAppWAL* wal);
    IMGUI_API void        AppWALWrite(ImGuiAppWAL* wal, ImGuiAppWALLevel level, const char* fmt, ...) IM_FMTARGS(3);
    IMGUI_API void        AppWALWriteV(ImGuiAppWAL* wal, ImGuiAppWALLevel level, const char* fmt, va_list args) IM_FMTLIST(3);

    // WAL sink for IM_ASSERT failures routed to ImAppAssertFail.
    IMGUI_API void        SetAppAssertWAL(ImGuiAppWAL* wal);

    // Thread backend (like SetAllocatorFunctions). Null = restore the std::thread default;
    // asserts if IMGUIAPP_DISABLE_DEFAULT_THREAD_FUNCS stripped it. Set before recording starts.
    IMGUI_API void        SetAppThreadFuncs(const ImGuiAppThreadFuncs* funcs);

    // Filesystem backend (same seam grammar). Null = restore the libc + std::filesystem default;
    // asserts at use if IMGUIAPP_DISABLE_DEFAULT_FILESYSTEM_FUNCS stripped it.
    IMGUI_API void        SetAppFileSystemFuncs(const ImGuiAppFileSystemFuncs* funcs);

    // Authored style/color overrides
    // Push every Active (in-range) entry; returns the number pushed -- pop with PopStyleVar/PopStyleColor(count).
    IMGUI_API int         PushAppStyleMods(const ImGuiAppStyleModDesc* mods, int count);
    IMGUI_API int         PushAppColorMods(const ImGuiAppColorModDesc* mods, int count);

    template <typename Visitor>
    IMGUI_API inline void ForEachAppNode(ImGuiApp* app, Visitor visitor);
    template <typename Visitor>
    IMGUI_API inline void ForEachAppNode(const ImGuiApp* app, Visitor visitor);

    // Demo
    // host: the PROCESS's real app, offered as the "Host app" live-mirror perspective
    // (strictly read-only there: time scrub is disabled for the host -- restoring its
    // state from inside its own render would mutate mid-frame).
    IMGUI_API void        ShowAppDemo(bool* p_open = nullptr, ImGuiApp* host = nullptr);
} // namespace ImGui

//-----------------------------------------------------------------------------
// [SECTION] Flags & Enumerations
//-----------------------------------------------------------------------------

// Frame/app configuration (relocated from the switch-only imappconfig.h).
enum ImGuiAppFrameFlags_
{
    ImGuiAppFrameFlags_None              = 0,
    ImGuiAppFrameFlags_NoClear           = 1 << 0,
    ImGuiAppFrameFlags_NoPresent         = 1 << 1,
    ImGuiAppFrameFlags_NoPlatformWindows = 1 << 2,
};

enum ImGuiAppStyle_
{
    ImGuiAppStyle_Dark    = 0,
    ImGuiAppStyle_Light   = 1,
    ImGuiAppStyle_Classic = 2,
};

enum ImGuiAppHeadlessMode_
{
    ImGuiAppHeadlessMode_None = 0,  // normal windowed app
    ImGuiAppHeadlessMode_Null,      // no GPU, no pixels (test engine only; backend CaptureFrame stays null)
    ImGuiAppHeadlessMode_Offscreen, // GPU renders to an offscreen target; no OS window, CaptureFrame works
};

enum ImGuiAppCommand : int
{
    ImGuiAppCommand_None,
    ImGuiAppCommand_Shutdown,
    ImGuiAppCommand_COUNT,
};

// Advisory frame pacer level (see ImGuiAppPacer).
enum ImGuiAppPacerMode_
{
    ImGuiAppPacerMode_Off = 0, // free-run; vsync/present mode governs
    ImGuiAppPacerMode_Target,  // pace wall clock to TargetHz (sleep + spin hybrid)
    ImGuiAppPacerMode_Fixed,   // Target pacing AND io.DeltaTime forced to exactly 1/TargetHz (determinism: replay, tests)
};

// Write-ahead logger level (see ImGuiAppWAL).
enum ImGuiAppWALLevel_
{
    ImGuiAppWALLevel_Off = 0,
    ImGuiAppWALLevel_Lifecycle, // composition changes, storage, command dispatch
    ImGuiAppWALLevel_Frame,     // + per-frame per-layer phase begins (crash hunts; large files)
};

// What time the video claims. A video is honest about realtime only under Realtime.
enum ImGuiAppAVTimingMode_
{
    ImGuiAppAVTimingMode_Auto = 0, // follow the pacer: Fixed pacer -> Constant, else Realtime
    ImGuiAppAVTimingMode_Constant, // CFR: frame N plays at N/Fps (synthetic timeline; matches Fixed dt)
    ImGuiAppAVTimingMode_Realtime, // VFR: PTS = FrameID.TimeSec (wall clock; a 50ms hitch plays as 50ms)
};

enum ImGuiAppRecordQueuePolicy_
{
    ImGuiAppRecordQueuePolicy_Block = 0,  // never drop (benchmarks/tests); app stalls when the queue is full
    ImGuiAppRecordQueuePolicy_DropNewest, // never stall (live capture); drops counted + WAL-logged
};

//-----------------------------------------------------------------------------
// [SECTION] AV recording types + interface
//-----------------------------------------------------------------------------
// Public so clients can drive recording: supply an encoder provider, tune the encode/ring config,
// inspect/extend the recorder. Encoder IMPLEMENTATIONS live in backends/imguiapp_impl_*.h. The rest of
// the AV seam (decoder, meta-stream verify, AppRecord* API) is in imguiapp_internal.h. See docs/designs.md (av-design).

// Frame identity: one id per frame, taken at the top of OnDrawFrame. The correlation key across video
// frames, sidecar records, WAL lines, and test logs (docs/designs.md (av-design)). Defined HERE (not imguiapp.h)
// so this header is self-contained -- ImGuiAppAVFrame holds one by value.
struct ImGuiAppFrameID
{
    ImU64  FrameIndex; // monotonic from run start (not ImGui's frame count: survives context recreation)
    ImU64  Tsc;        // __rdtsc / platform equivalent at frame begin
    double TimeSec;    // QPC seconds since run start

    ImGuiAppFrameID() { memset((void*)this, 0, sizeof(*this)); }
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

    ImGuiAppAVFrame() { memset((void*)this, 0, sizeof(*this)); }
};

struct ImGuiAppAVEncodeConfig
{
    const char*          OutputPath;  // container path, or directory for sequence providers
    float                Fps;         // Constant mode: the frame rate. Realtime mode: nominal rate hint only
    ImGuiAppAVTimingMode Timing;
    int                  Width;       // 0 = first frame's size (fixed thereafter; resize aborts recording)
    int                  Height;
    int                  BitrateKbps; // hint; lossless providers ignore
                                      // Metadata lives IN the video: while recording, the meta record stream is
                                      // chunked across the frames' bottom EmbedRows pixel rows as 4x4 luma blocks
                                      // (survives lossy encode). Format frozen: see docs/designs.md (av-design).
    int                  EmbedRows;   // reserved bottom rows; multiple of 4

    ImGuiAppAVEncodeConfig()
    {
        OutputPath  = nullptr;
        Fps         = 60.0f;
        Timing      = ImGuiAppAVTimingMode_Auto;
        Width       = 0;
        Height      = 0;
        BitrateKbps = 0;
        EmbedRows   = 32;
    }
};

// Encoder provider vtable. Implementations allocate themselves (Create* in their own
// backends/imguiapp_impl_*.h header) and free themselves via Destroy.
struct ImGuiAppAVEncoder
{
    const char* Name;
    bool        SupportsRealtimePts; // provider can carry per-frame wall-clock PTS (true VFR)
    bool        (*OpenFn)(ImGuiAppAVEncoder* self, const ImGuiAppAVEncodeConfig* config);
    bool        (*WriteFrameFn)(ImGuiAppAVEncoder* self, const ImGuiAppAVFrame* frame); // PTS from frame->FrameID.TimeSec under Realtime
    void        (*CloseFn)(ImGuiAppAVEncoder* self);
    void        (*DestroyFn)(ImGuiAppAVEncoder* self);
    void*       UserData; // provider state

    ImGuiAppAVEncoder() { memset((void*)this, 0, sizeof(*this)); }
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

// Always-on in-memory ring of the last N seconds (frames QOI-compressed on capture,
// plus their meta records); the stream is chunked across the frames at dump time.
struct ImGuiAppRingConfig
{
    float Seconds;     // ring span
    int   MaxMemoryMB; // hard cap; oldest frames evicted when either bound binds
    float Fps;         // <= 0 (default) = keep EVERY frame; > 0 = explicit subsample opt-out of the encode-every-frame contract

    ImGuiAppRingConfig()
    {
        Seconds     = 10.0f;
        MaxMemoryMB = 256;
        Fps         = 0.0f;
    }
};

// One queued frame, pixels owned (CaptureFrame pixels are only valid until the next capture).
struct ImGuiAppAVQueuedFrame
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

    ImGuiAppAVRingEntry()
    {
        Width       = 0;
        Height      = 0;
        StateHash   = 0;
        ChainOffset = -1;
    }
};

// Thread backend used by the recorder's encoder thread. Default = std::thread (imguiapp.cpp);
// override via SetAppThreadFuncs() BEFORE recording starts. All hooks required.
struct ImGuiAppThreadFuncs
{
    void* (*ThreadCreateFn)(void (*fn)(void*), void* arg); // start a thread running fn(arg); returns handle
    void  (*ThreadJoinFn)(void* thread);                    // join + destroy the handle
    void* (*MutexCreateFn)();
    void  (*MutexDestroyFn)(void* mutex);
    void  (*MutexLockFn)(void* mutex);
    void  (*MutexUnlockFn)(void* mutex);
    void* (*CondCreateFn)();
    void  (*CondDestroyFn)(void* cond);
    void  (*CondWaitFn)(void* cond, void* mutex);           // atomically unlock, wait, relock
    void  (*CondSignalFn)(void* cond);
    void  (*CondBroadcastFn)(void* cond);
};

// Filesystem backend for the OS/harness glue (artifact dirs, take cleanup, the editor's
// project-file scan). Default = platform libc + std::filesystem (imguiapp.cpp); override via
// SetAppFileSystemFuncs(). All hooks required.
struct ImGuiAppFileSystemFuncs
{
    bool (*CreateDirRecursiveFn)(const char* path);  // create every missing directory on the path; true when it exists after
    bool (*RemoveFileFn)(const char* path);          // one file; false when absent
    bool (*RemoveDirFn)(const char* path);           // one EMPTY directory
    // Visit every regular file directly inside dir (no recursion): visit(file name, size in bytes, user_data).
    void (*ScanDirFn)(const char* dir, void (*visit)(const char* name, ImU64 size_bytes, void* user_data), void* user_data);
};

struct ImGuiAppRecorderThread; // Opaque encoder-thread state (handles from ImGuiAppThreadFuncs), defined in imguiapp.cpp

// Glue between the app, the platform backend's CaptureFrame, and one encoder. WriteFrame runs on a
// single encoder thread behind a bounded queue. Held by ImGuiApp::Recorder; methods defined in imguiapp.cpp.
struct ImGuiAppRecorder
{
    ImGuiApp*              App;
    ImGuiAppAVEncoder*     Encoder;
    ImGuiAppAVEncodeConfig Config; // Timing resolved (never Auto) before the provider sees it
    char                   OutputPath[512];
    bool                   Active;
    bool                   EncoderOpen;
    int                    FixedWidth; // 0 until the first captured frame locks the size
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

    // Encoder thread + bounded queue (normal mode only). Thread opaque (docs/house-style-audit.md Δ2); null in ring mode.
    ImGuiAppRecorderThread*          Thread;
    ImVector<ImGuiAppAVQueuedFrame*> Queue; // FIFO; front = index 0; guarded by Thread->Mutex while recording
    int                              QueueDepth;
    ImGuiAppRecordQueuePolicy        QueuePolicy;
    bool                             ThreadStop;
    bool                             EncodeFailed;
    ImU64                            DroppedFrames;

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
        Thread           = nullptr;
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

//-----------------------------------------------------------------------------
// [SECTION] Configuration structs
//-----------------------------------------------------------------------------

struct ImGuiAppFrameConfig
{
    ImVec4             ClearColor;
    ImGuiAppFrameFlags Flags;

    ImGuiAppFrameConfig()
        : ClearColor(0.0f, 0.0f, 0.0f, 1.0f), Flags(ImGuiAppFrameFlags_None)
    {
    }
};

struct ImGuiAppConfig
{
    const char*          PlatformName;    // = NULL   app label for diagnostics (status overlay)
    ImGuiConfigFlags     ConfigFlags;     // = 0
    ImGuiAppStyle        Style;           // = ImGuiAppStyle_Dark
    ImVec4               ClearColor;      // = (0, 0, 0, 1)
    float                FontScale;       // = 1.0f
    float                DpiScale;        // = 1.0f
    ImGuiAppHeadlessMode Headless;        // = ImGuiAppHeadlessMode_None
    bool                 PersistSettings; // = true
    const char*          WindowTitle;     // = NULL
    int                  WindowWidth;     // = 0
    int                  WindowHeight;    // = 0

    IMGUI_API ImGuiAppConfig();
};

// Pacer platform seam: the platform half of pacing behind one function table, like
// ImGuiAppPlatformBackend. The pacer owns the deadline chain (imguiapp.cpp, platform-free);
// the CLIENT installs the funcs supplying the clock, the wait, and the refresh queries.
// Install BEFORE pacing starts. Null Funcs = no wait machinery: the loop free-runs (Fixed
// mode still forces its deterministic dt). NowFn/WaitUntilFn required; the rest optional
// (null = the documented fallback).
struct ImGuiAppPacerFuncs
{
    double (*NowFn)();                                             // monotonic seconds (the pacer's time domain)
    void   (*WaitUntilFn)(double time_sec, float sleep_slack_ms);  // block until NowFn() >= time_sec, landing it exactly; slack = spin-window hint
    void   (*ShutdownFn)();                                        // optional: release wait machinery (timers, thread QoS); called by ImGuiApp::Shutdown
    float  (*PrimaryRefreshHzFn)();                                // optional: primary monitor refresh; null or <= 0 = 60 assumed
    float  (*ViewportRefreshHzFn)(const ImGuiViewport* viewport);  // optional: hosting monitor's refresh; null or <= 0 = primary
};

// Advisory frame pacer. Backend run loops call AppPacerWait once per iteration, before OnDrawFrame;
// Off returns immediately. The pacer decides what time the app SIMULATES; video timing is separate
// (imapp_av.h ImGuiAppAVTimingMode) -- honest-realtime video takes PTS from FrameID.TimeSec.
struct ImGuiAppPacer
{
    ImGuiAppPacerMode Mode;            // = ImGuiAppPacerMode_Off
    float             TargetHz;        // = 0.0f  // <= 0 with Mode_Target = pace to primary monitor refresh
    float             SleepSlackMs;    // = 2.0f  // spin the last N ms (OS sleep granularity guard)
    const ImGuiAppPacerFuncs* Funcs;   // = NULL  // platform seam, client-installed; null = free-run (no wait)
    double            LastFrameMs;     // = 0.0
    double            LastWaitMs;      // = 0.0
    ImU64             MissedDeadlines; // = 0     // frames that arrived after their deadline

    IMGUI_API ImGuiAppPacer();
};

// Write-ahead logger. Each record is appended and flushed BEFORE the operation it names executes, so after
// a crash the file tail names the in-flight operation. Attach to ImGuiApp::WAL; null = silent.
struct ImGuiAppWAL
{
    ImFileHandle           File; // libc handle behind the ImFile* seam (imgui_internal.h typedef)
    int                    Seq;  // monotonic record number
    ImGuiAppWALLevel       Level;
    const ImGuiAppFrameID* FrameID; // optional (point at ImGuiApp::FrameID): prefixes records "[tick:N tsc:T]"
    char                   Path[256];

    ImGuiAppWAL() { memset((void*)this, 0, sizeof(*this)); }
};

// Platform backend interface. The core app layer depends only on this vtable. Backends coexist as
// ordinary TUs, each exporting ImGuiApp_ImplXXX_GetPlatformBackend(); the app's wiring (imguix.cpp's
// build-selected binding, or the app itself) defines ImGuiAppGetPlatformBackend() once.
struct ImGuiAppPlatformBackend
{
    bool (*InitPlatformFn)(ImGuiApp* app, ImGuiAppConfig& config);
    void (*ShutdownPlatformFn)(ImGuiApp* app);
    int (*RunLoopFn)(ImGuiApp* app);
    // Optional (null = backend cannot capture; recording fails with a clear error). Readback in the encode
    // phase (after render, before present). Encode-every-frame contract: steady state may return frame N-1's
    // pixels (FrameID stamped at copy time), drain the tail by re-calling, never block, never repeat a FrameIndex.
    bool (*CaptureFrameFn)(ImGuiApp* app, ImGuiAppAVFrame* out_frame);
    // Frame lifecycle: the backend's exposed ImGuiApp_ImplXXX_* functions (imgui impl pattern),
    // driven by the app's frame phases (OnDrawFrame/OnRenderFrame/OnPresentFrame). PresentFrame
    // optional (null = RenderDrawData presents, legacy single-hook).
    const char* Name;
    void (*ShutdownFn)(ImGuiApp* app);
    void (*NewFrameFn)(ImGuiApp* app);
    void (*RenderDrawDataFn)(ImGuiApp* app, ImDrawData* draw_data, const ImGuiAppFrameConfig* config);
    void (*PresentFrameFn)(ImGuiApp* app, const ImGuiAppFrameConfig* config);
};

IMGUI_API const ImGuiAppPlatformBackend* ImGuiAppGetPlatformBackend();

//-----------------------------------------------------------------------------
// [SECTION] Helpers (interface base, state-delta events, RNG, diagnostics, style/color/data descs)
//-----------------------------------------------------------------------------

// The compile-time type-identity + reflection layer (ImFuncSig / IM_LABEL_SIZE / ImParseType* macros,
// ImGuiAppStatic<> / ImGuiAppType<> / ImAppNulTerminate / ImAppFormatLabel / ImAppTypeDisplayName, and
// the AppReflectFields walk) lives in imguiapp_reflect.h, included at the top of this file.

// Polymorphic root of the app object model: the common base the app owns layers/items/adapters by, deleted
// virtually through a base pointer. Carries NO data -- identity (Label) lives on the concrete branches, so
// a mixed-in secondary interface adds no duplicate Label. Virtuals: this hierarchy ONLY (house-style-audit.md Δ1).
struct ImGuiAppInterface
{
    ImGuiAppInterface()          = default;
    virtual ~ImGuiAppInterface() = default;
};


// State-delta event helpers over (this frame, last frame): rising = started, falling = ended, changed = either.
inline static bool ImAppRising(bool now, bool last) { return now && !last; }
inline static bool ImAppFalling(bool now, bool last) { return !now && last; }
inline static bool ImAppChanged(bool now, bool last) { return now ^ last; }
template <typename T>
inline static bool ImAppChanged(const T& now, const T& last) { return !(now == last); }

// Splitmix64, no global state. Keep the seed in PersistData (seed in OnInitialize, step only through the
// seed) so snapshots and replay reproduce it; hidden effect sources (rand(), clocks) break replay.
inline static ImU64 ImAppRandom(ImU64* seed)
{
    ImU64 z = (*seed += 0x9E3779B97F4A7C15ull);
    z       = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z       = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
inline static float ImAppRandomFloat(ImU64* seed, float mn, float mx) // uniform in [mn, mx)
{
    return mn + (mx - mn) * (float)(ImAppRandom(seed) >> 40) / (float)(1ull << 24);
}
inline static int ImAppRandomInt(ImU64* seed, int mn, int mx) // uniform in [mn, mx]
{
    return mx <= mn ? mn : mn + (int)(ImAppRandom(seed) % (ImU64)(mx - mn + 1));
}


// One authorable style-var override: Value.x for float vars, both lanes for ImVec2 vars; Active is
// runtime-toggleable. Aggregate (default member initializers, no ctors) so the build-time reflection
// walk sees its members. Float-valued vars brace-init as { var, ImVec2(v, 0.0f) }.
struct ImGuiAppStyleModDesc
{
    ImGuiStyleVar Var    = 0;
    ImVec2        Value  = ImVec2(0.0f, 0.0f);
    bool          Active = true;
};

// One authorable PushStyleColor override. Value is packed IM_COL32 RGBA.
// Aggregate for the same reason as ImGuiAppStyleModDesc.
struct ImGuiAppColorModDesc
{
    ImGuiCol Col    = 0;
    ImU32    Value  = 0;
    bool     Active = true;
};

// Routes one of a control's data dependencies to a specific producer instance at push time.
// TypeID names WHICH dependency of the pack is routed; Instance names the producer.
struct ImGuiAppDataBinding
{
    ImGuiID TypeID;           // ImGuiAppType<Dep>::ID of the dependency being routed
    ImGuiID Instance;         // producer's instance id (0 = the type singleton)
    bool    Optional = false; // absent producer resolves to null instead of asserting; the consumer
                              // handles null (and is rebound live when the producer is pushed/popped)
};

// Instance-qualified type id (ImHash* family): hash-combines a data type id with an instance
// number to key a control's instance data in ImGuiApp::Data. instance 0 keeps the bare type id
// (the type singleton); any other instance qualifies it.
IMGUI_API ImGuiID ImAppHashType(ImGuiID type_id, ImGuiID instance);
//-----------------------------------------------------------------------------
// [SECTION] App object model (layers, items, controls, ImGuiApp, adapter/control templates)
//-----------------------------------------------------------------------------

// A node: an authorable item in the composition tree + the live-mirror surface -- compile-time-erased
// data identity re-exposed on the type-erased base, so tools inspect any node without knowing its
// concrete template pack. Mirror hooks default inert (imguiapp.cpp); ImGuiAppControlMirrorAdapter<>
// overrides each from its pack. (ImGuiAppLiveFieldDesc lives in imguiapp_reflect.h, included at the top.)
struct ImGuiAppNodeBase : ImGuiAppInterface
{
    char Label[IM_LABEL_SIZE]; // type name by default; deduplicated with "##N" on real collisions

    // Lifetime bracket: once per node object (push creates -> OnInitialize; pop destroys -> OnShutdown).
    virtual void OnInitialize(ImGuiApp*) const                         = 0;
    virtual void OnShutdown(ImGuiApp*) const                           = 0;
    virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*) const = 0;
    virtual void OnUpdate(ImGuiApp* app, float dt) const               = 0;
    virtual void OnDraw(const ImGuiApp*) const                         = 0;

    // Membership bracket, distinct from the lifetime pair: push fires OnInitialize -> OnAttach, pop
    // fires OnDetach -> OnShutdown, and recomposition may detach/re-attach a node without destroying it.
    virtual void OnAttach(ImGuiApp*) const                             { }
    virtual void OnDetach(ImGuiApp*) const                             { }

    // Data identity
    virtual ImGuiID GetDataID() const;                                                  // instance-qualified storage key of PersistData
    virtual void    GetDataTypeName(char* out, int out_size) const;                     // PersistData type name
    virtual void    GetTempDataTypeName(char* out, int out_size) const;                 // TempData type name

    // Reflection + live memory
    virtual int     GetFields(ImGuiAppLiveFieldDesc* out, int cap, bool temp_data) const; // reflected members of Persist (false) / Temp (true); out null = count
    virtual bool    IsDataReflectable(bool temp_data) const;                            // false = not trivially copyable: opaque, exactly like snapshots
    virtual bool    GetLiveData(const void** out_persist, const void** out_temp) const; // live instance memory of the RUNNING node (read-only); false before init

    // Dependency wiring
    virtual int     GetDependencyIDs(ImGuiID* out, int cap) const;                      // RESOLVED producer keys -- where each slot is wired NOW (out null = count)
    virtual int     GetDependencyTypeIDs(ImGuiID* out, int cap) const;                  // DECLARED dependency type ids -- the compile-time pack, what CAN be wired
    virtual int     GetDependencyOptional(bool* out, int cap) const;                    // per-slot Optional flags, same order as the id queries
    virtual void    RefreshDependencyData(const ImGuiApp* app);                         // rebind cached dependency pointers from resolved keys (AppRebuildUpdateOrder, after any push/pop)
    virtual bool    SetDependencyBinding(ImGuiApp* app, const ImGuiAppDataBinding* bind); // re-route one declared dependency at runtime (Composer rewiring); false = TypeID not in pack
};

// Type marker: a layer node -- a domain in the stack (Task/Command/Status/Layout/Display or custom);
// N3 moves each domain's hosted node list onto its layer.
struct ImGuiAppLayerBase : ImGuiAppNodeBase
{
};

// A display node: submits ImGui widgets during the draw phase. The display layer brackets each
// node's submission with the authored style/color overrides below via ImGui::PushAppStyleMods/
// PushAppColorMods + the matching Pop (imgui idiom: explicit push/pop around submission). Plain
// data, not a virtual hook -- this IS the per-item style customization point.
struct ImGuiAppDisplayNodeBase : ImGuiAppNodeBase
{
    ImVector<ImGuiAppStyleModDesc> StyleMods;
    ImVector<ImGuiAppColorModDesc> ColorMods;
};

struct ImGuiAppWindowBase : ImGuiAppDisplayNodeBase
{
    bool                                Open     = true;
    ImGuiWindow*                        Window   = nullptr;
    ImGuiViewport*                      Viewport = nullptr;
    ImGuiWindowFlags                    Flags    = ImGuiWindowFlags_None;
    ImVector<ImGuiAppDisplayNodeBase*>  Children;

    // Optional first-use placement (applied with ImGuiCond_FirstUseEver, so saved .ini wins).
    bool   HasInitialPlacement = false;
    ImVec2 InitialPos          = ImVec2(0.0f, 0.0f);
    ImVec2 InitialSize         = ImVec2(0.0f, 0.0f);
};

struct ImGuiAppSidebarBase : ImGuiAppWindowBase
{
    ImGuiDir DockDir = ImGuiDir_None;
    float    Size    = 0.0f;
};

// Type marker: a data-carrying node (Persist/Temp/dependency pack behind the ImGuiAppNodeBase
// mirror surface). Display-based while every control renders in a display slot; N4/N5 split the
// pure-compute kind out to the Task layer.
struct ImGuiAppControlBase : ImGuiAppDisplayNodeBase
{
};

// The reflectability contracts, type-schema registry, and reflection field-walk these adapters drive
// (ImAppDataReflectable, ImGuiAppTypeSchema, AppReflectFields, AppEnsureTypeRegistered, ImAppFormatLabel, ...)
// all live in imguiapp_reflect.h, included at the top of this file.

// TempData for a control with no per-frame input: pass as TempDataT instead of authoring an
// empty per-control struct (OnDraw records nothing, OnUpdate compares nothing).
struct ImGuiAppNoTempData {};

template <typename PersistDataT, typename TempDataT, typename... DataDependencies>
struct ImGuiAppInterfaceAdapterBase : ImGuiAppInterface
{
    virtual void OnInitialize(ImGuiApp*, PersistDataT*, const DataDependencies*...) const                                                 = 0;
    virtual void OnShutdown(ImGuiApp*, PersistDataT*, const DataDependencies*...) const                                                   = 0;
    virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*, const PersistDataT*, const TempDataT*, const DataDependencies*...) const = 0;
    virtual void OnUpdate(float, PersistDataT*, const TempDataT*, const TempDataT*, const DataDependencies*...) const                     = 0;
    virtual void OnDraw(const PersistDataT*, TempDataT*, const DataDependencies*...) const                                                = 0;
};

struct ImGuiAppLayer : ImGuiAppLayerBase
{
    virtual void OnInitialize(ImGuiApp*)                         const override { }
    virtual void OnShutdown(ImGuiApp*)                           const override { }
    virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*) const override { }
    virtual void OnUpdate(ImGuiApp*, float)                      const override { }
    virtual void OnDraw(const ImGuiApp*)                         const override { }
};

struct ImGuiAppTaskLayer : ImGuiAppLayer
{
    virtual void OnAttach(ImGuiApp*)        const override final;
    virtual void OnDetach(ImGuiApp*)        const override final;
    virtual void OnUpdate(ImGuiApp*, float) const override final;
    virtual void OnDraw(const ImGuiApp*)    const override final;
};

struct ImGuiAppCommandLayer : ImGuiAppLayer
{
    virtual void OnAttach(ImGuiApp*)        const override final;
    virtual void OnDetach(ImGuiApp*)        const override final;
    virtual void OnUpdate(ImGuiApp*, float) const override final;
    virtual void OnDraw(const ImGuiApp*)    const override final;
};

struct ImGuiAppStatusLayer : ImGuiAppLayer
{
    virtual void OnAttach(ImGuiApp*)        const override final;
    virtual void OnDetach(ImGuiApp*)        const override final;
    virtual void OnUpdate(ImGuiApp*, float) const override final;
    virtual void OnDraw(const ImGuiApp*)    const override final;
};

struct ImGuiAppLayoutLayer : ImGuiAppLayer
{
    virtual void OnAttach(ImGuiApp*)        const override final;
    virtual void OnDetach(ImGuiApp*)        const override final;
    virtual void OnUpdate(ImGuiApp*, float) const override final;
    virtual void OnDraw(const ImGuiApp*)    const override final;
};

struct ImGuiAppDisplayLayer : ImGuiAppLayer
{
    virtual void OnAttach(ImGuiApp*)        const override final;
    virtual void OnDetach(ImGuiApp*)        const override final;
    virtual void OnUpdate(ImGuiApp*, float) const override final;
    virtual void OnDraw(const ImGuiApp*)    const override final;
};

// Root node: driven by the frame phases (OnDrawFrame/OnRenderFrame/OnPresentFrame), not the node
// hooks; inert here so the app participates in the one tree (D2).
struct ImGuiAppBase : ImGuiAppNodeBase
{
    virtual void OnExecuteCommand(ImGuiAppCommand cmd) = 0;
    bool         ShutdownPending                       = false;

    virtual void OnInitialize(ImGuiApp*)                         const override { }
    virtual void OnShutdown(ImGuiApp*)                           const override { }
    virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*) const override { }
    virtual void OnUpdate(ImGuiApp*, float)                      const override { }
    virtual void OnDraw(const ImGuiApp*)                         const override { }
};

struct ImGuiAppStorageEntry
{
    ImGuiID ID             = 0;
    void*   Ptr            = nullptr;
    int     Size           = 0; // byte size when the data is snapshottable (trivially copyable); 0 = opaque
    int     TempOffset     = 0; // byte range of the TempData member inside the instance data -- the
    int     TempSize       = 0; // control's per-frame INPUT; [0, TempOffset) is Persist + LastTemp (state)
    void (*Destroy)(void*) = nullptr;
};

// Ring of MaxFrames byte snapshots of every snapshottable storage entry (opaque entries skipped). Layout
// is keyed to GetAppCompositionID and rebuilt (history cleared) when the composition changes.
struct ImGuiAppStateHistory
{
    ImGuiID           CompositionID = 0;   // layout is valid for exactly this composition
    int               FrameSize     = 0;   // bytes per snapshot (sum of slot sizes)
    int               MaxFrames     = 600; // ring capacity (default 600 ~ 10s at 60fps)
    int               Count         = 0;   // valid snapshots
    int               Head          = 0;   // ring write index
    ImVector<ImGuiID> SlotIds;             // storage entry per slot, in StorageEntries order
    ImVector<int>     SlotSizes;
    ImVector<char>    Frames; // MaxFrames * FrameSize bytes
};

// Input log: per frame, every control's TempData + dt, plus a hash of the resulting state
// (Persist+LastTemp prefix of every instance) so replay can pinpoint the first divergent frame.
struct ImGuiAppInputLog
{
    ImGuiID           CompositionID; // layout is valid for exactly this composition
    int               FrameSize;     // bytes per frame: sum of temp sizes + sizeof(float) dt
    int               Count;         // recorded frames
    ImVector<ImGuiID> SlotIds;       // storage entry per slot, in StorageEntries order
    ImVector<int>     SlotOffsets;   // TempData offset within each instance
    ImVector<int>     SlotSizes;     // TempData size
    ImVector<char>    Frames;        // Count * FrameSize bytes, appended (caller clears between takes)
    ImVector<ImGuiID> StateHashes;   // per-frame post-update state hash (replay divergence reference)

    ImGuiAppInputLog()
    {
        CompositionID = 0;
        FrameSize     = 0;
        Count         = 0;
    }
};

struct ImGuiApp : ImGuiAppBase
{
    // State: composition (push order), platform/presentation, services (null = inactive)
    ImGuiStorage                       Data;                          // id-keyed instance storage (controls' Persist/Temp blocks)
    ImVector<ImGuiAppStorageEntry>     StorageEntries;
    ImVector<ImGuiAppLayerBase*>       Layers;
    ImVector<ImGuiAppWindowBase*>      Windows;
    ImVector<ImGuiAppSidebarBase*>     Sidebars;
    ImVector<ImGuiAppDisplayNodeBase*> Controls;
    ImVector<ImGuiAppNodeBase*>        UpdateOrder;                   // dependency-sorted OnUpdate iteration (AppRebuildUpdateOrder)
    int                                CompositionRevision = 0;       // bumped by every storage register/unregister (pop+repush of the same type still advances it)
    int                                UpdateOrderRevision = -1;      // revision UpdateOrder + the cached dependency bindings were built at
    const char*                        PlatformName        = nullptr; // app label for diagnostics (StatusLayer null-guards it); from config
    void*                              PlatformWindowHandle = nullptr; // main window handle (set by the host's InitPlatform; read by the sibling platform host's run loop)
    ImVec4                             ClearColor;
    void*                              PlatformData        = nullptr; // platform host window/loop state (io userdata-slot analog; owned by the backend's InitPlatform/ShutdownPlatform)
    void*                              BackendData         = nullptr; // host backend data (io.BackendXxxUserData analog; owned by ImGuiApp_ImplXXX_Init/Shutdown)
    ImGuiAppWAL*                       WAL                 = nullptr; // optional write-ahead logger (caller-owned); null = silent
    ImGuiAppRecorder*                  Recorder            = nullptr; // active recording (AppRecordBegin registers, AppRecordEnd clears); null = none
    ImGuiAppFrameID                    FrameID;                       // stamped at the top of OnDrawFrame; correlation key for WAL/video/sidecar
    ImGuiAppPacer                      Pacer;                         // advisory; consulted by the backend run loop via AppPacerWait
    bool                               Initialized         = false;

    // Lifecycle. One frame = the four phases in order: draw (frame id, NewFrame, app layers/widgets),
    // render (draw data -> GPU, platform windows), encode (recorder pump reads the frame just
    // rendered), present. Backend run loops call Frame(); override a phase or hook to extend it.
    virtual      ~ImGuiApp();
    int          Run(int argc, char** argv);
    bool         Initialize(const ImGuiAppConfig* config);
    virtual void Shutdown();
    bool         IsInitialized() const { return Initialized; }
    void         Frame();
    virtual bool OnInitialize(int argc, char** argv) { return true; }
    virtual void OnLayout() {}
    virtual void OnDrawFrame();
    virtual void OnRenderFrame();
    virtual void OnEncodeFrame();
    virtual void OnPresentFrame();
    virtual void OnExecuteCommand(ImGuiAppCommand cmd) override;
    virtual bool OnInitializePlatform(ImGuiAppConfig& config);
    virtual void OnShutdownPlatform();
};

// Dependency-slot wiring, factored per pack: instance identity + the routed producer keys and
// cached pointers every consumer carries. PushAppControl<>() seats _InstanceID then resolves the
// slots; AppRebuildUpdateOrder rebinds them after any push/pop.
template <typename... DataDependencies>
struct ImGuiAppDependencySlots
{
    ImGuiID _InstanceID                                                                            = 0;
    void*   _Dependencies[sizeof...(DataDependencies) > 0 ? sizeof...(DataDependencies) : 1]       = {};
    ImGuiID _DependencyKeys[sizeof...(DataDependencies) > 0 ? sizeof...(DataDependencies) : 1]     = {};
    bool    _DependencyOptional[sizeof...(DataDependencies) > 0 ? sizeof...(DataDependencies) : 1] = {};

    using DepSeq = typename ImAppMakeIndexSeq<sizeof...(DataDependencies)>::Type;

    inline void ResolveDependencies(const ImGuiApp* app, const ImGuiAppDataBinding* binds, int binds_count) { ResolveSlots(app, binds, binds_count, DepSeq()); }
    inline void RebindDependencies(const ImGuiApp* app)                                                     { RebindSlots(app, DepSeq()); }

    // If you assert here, a dependency's producer was not pushed before this control (PushAppControl<>()
    // seats producers first), or a popped producer left a non-Optional consumer alive.
    template <typename Dep> inline Dep* GetData(size_t slot) const { return (Dep*)_Dependencies[slot]; }
    template <typename Dep> inline Dep* ResolveDependency(const ImGuiApp* app, const ImGuiAppDataBinding* binds, int binds_count, int slot)
    {
        _DependencyKeys[slot] = ImGui::AppResolveDependencyKey(app, ImGuiAppType<Dep>::ID, _InstanceID, binds, binds_count, &_DependencyOptional[slot]);
        Dep* data             = (Dep*)app->Data.GetVoidPtr(_DependencyKeys[slot]);
        IM_ASSERT(data != nullptr || _DependencyOptional[slot]);
        return data;
    }
    template <typename Dep> inline Dep* LookupDependency(const ImGuiApp* app, size_t slot) const
    {
        Dep* data = (Dep*)app->Data.GetVoidPtr(_DependencyKeys[slot]);
        IM_ASSERT(data != nullptr || _DependencyOptional[slot]);
        return data;
    }
    template <size_t... Is> inline void ResolveSlots(const ImGuiApp* app, const ImGuiAppDataBinding* binds, int binds_count, ImAppIndexSeq<Is...>)
    {
        int expand[] = { 0, (_Dependencies[Is] = (void*)ResolveDependency<DataDependencies>(app, binds, binds_count, (int)Is), 0)... }; // braced init: left-to-right slots
        IM_UNUSED(expand);
    }
    template <size_t... Is> inline void RebindSlots(const ImGuiApp* app, ImAppIndexSeq<Is...>)
    {
        int expand[] = { 0, (_Dependencies[Is] = (void*)LookupDependency<DataDependencies>(app, Is), 0)... };
        IM_UNUSED(expand);
    }
};

template <typename Base, typename PersistDataT, typename TempDataT, typename... DataDependencies>
struct ImGuiAppInterfaceAdapter : Base, ImGuiAppInterfaceAdapterBase<PersistDataT, TempDataT, DataDependencies...>, ImGuiAppDependencySlots<DataDependencies...>
{
    // Instance data for this control, created in ImGuiApp::Data and bound here by PushAppControl<>() before OnInitialize.
    struct InstanceData
    {
        PersistDataT PersistData;
        TempDataT    LastTempData;
        TempDataT    TempData;
    }* _InstanceData = nullptr;

    // Slot wiring lives on the ImGuiAppDependencySlots base; re-open the names this side of the
    // dependent-base boundary for the typed apply tail below.
    using Slots  = ImGuiAppDependencySlots<DataDependencies...>;
    using DepSeq = typename Slots::DepSeq;
    template <typename Dep> inline Dep* GetData(size_t slot) const { return Slots::template GetData<Dep>(slot); }

    //
    //
    //
    //
    virtual void OnInitialize(ImGuiApp*, PersistDataT*, const DataDependencies*...) const override {}
    virtual void OnInitialize(ImGuiApp* app) const override final { IM_ASSERT(_InstanceData != nullptr); ApplyInitialize(app, DepSeq()); }

    //
    //
    //
    //
    virtual void OnShutdown(ImGuiApp*, PersistDataT*, const DataDependencies*...) const override {}
    virtual void OnShutdown(ImGuiApp* app) const override final { ApplyShutdown(app, DepSeq()); }

    //
    //
    //
    //
    virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*, const PersistDataT*, const TempDataT*, const DataDependencies*...) const override {}
    virtual void OnGetCommand(const ImGuiApp* app, ImGuiAppCommand* cmd) const override final { ApplyGetCommand(app, cmd, DepSeq()); }

    //
    //
    //
    //
    virtual void OnUpdate(float, PersistDataT*, const TempDataT*, const TempDataT*, const DataDependencies*...) const override {}
    virtual void OnUpdate(ImGuiApp* app, float dt) const override final { IM_UNUSED(app); ApplyUpdate(dt, DepSeq()); _InstanceData->LastTempData = _InstanceData->TempData; }

    //
    //
    //
    //
    virtual void OnDraw(const PersistDataT*, TempDataT*, const DataDependencies*...) const override {}
    virtual void OnDraw(const ImGuiApp* app) const override final { IM_UNUSED(app); _InstanceData->TempData = {}; ApplyDraw(DepSeq()); }

    // Typed apply: the index pack and the type pack expand in lockstep, re-attaching each opaque
    // slot's static type at the call boundary (no tuple/apply; see ImAppIndexSeq).
    template <size_t... Is> inline void ApplyInitialize(ImGuiApp* app, ImAppIndexSeq<Is...>) const                             { OnInitialize(app, &_InstanceData->PersistData, GetData<DataDependencies>(Is)...); }
    template <size_t... Is> inline void ApplyShutdown(ImGuiApp* app, ImAppIndexSeq<Is...>) const                               { OnShutdown(app, &_InstanceData->PersistData, GetData<DataDependencies>(Is)...); }
    template <size_t... Is> inline void ApplyGetCommand(const ImGuiApp* app, ImGuiAppCommand* cmd, ImAppIndexSeq<Is...>) const { OnGetCommand(app, cmd, &_InstanceData->PersistData, &_InstanceData->TempData, GetData<DataDependencies>(Is)...); }
    template <size_t... Is> inline void ApplyUpdate(float dt, ImAppIndexSeq<Is...>) const                                      { OnUpdate(dt, &_InstanceData->PersistData, &_InstanceData->TempData, &_InstanceData->LastTempData, GetData<DataDependencies>(Is)...); }
    template <size_t... Is> inline void ApplyDraw(ImAppIndexSeq<Is...>) const                                                  { OnDraw(&_InstanceData->PersistData, &_InstanceData->TempData, GetData<DataDependencies>(Is)...); }
};

// Live-mirror implementation over the adapter's pack: the ImGuiAppControlMirrorBase hooks,
// compile-time erased from PersistDataT/TempDataT/DataDependencies. Machinery only -- user code
// subclasses ImGuiAppControl<> below and never sees this layer.
template <typename PersistDataT, typename TempDataT, typename... DataDependencies>
struct ImGuiAppControlMirrorAdapter : ImGuiAppInterfaceAdapter<ImGuiAppControlBase, PersistDataT, TempDataT, DataDependencies...>
{
    // Instance-qualified storage keys -- the same keys app->Data uses. Dependency ids are the
    // RESOLVED producer keys (push-time routing), so mirrors draw the actual wiring.
    virtual ImGuiID GetDataID() const override final { return ImAppHashType(ImGuiAppType<PersistDataT>::ID, this->_InstanceID); }

    virtual int GetDependencyIDs(ImGuiID* out, int cap) const override final
    {
        const int count = (int)(sizeof...(DataDependencies));
        if (out == nullptr || cap <= 0)
            return count;
        const int n = count < cap ? count : cap;
        for (int i = 0; i < n; i++)
            out[i] = this->_DependencyKeys[i];
        return n;
    }

    virtual void GetDataTypeName(char* out, int out_size) const override final { ImAppFormatLabel<PersistDataT>(out, (size_t)out_size); }
    virtual void GetTempDataTypeName(char* out, int out_size) const override final { ImAppFormatLabel<TempDataT>(out, (size_t)out_size); }

    virtual int GetFields(ImGuiAppLiveFieldDesc* out, int cap, bool temp_data) const override final
    {
#ifdef IMGUIAPP_HAS_REFLECT
        return temp_data ? ImGui::AppReflectFields<TempDataT>(out, cap) : ImGui::AppReflectFields<PersistDataT>(out, cap);
#else
        IM_UNUSED(out);
        IM_UNUSED(cap);
        IM_UNUSED(temp_data);
        return 0;
#endif
    }

    virtual bool IsDataReflectable(bool temp_data) const override final
    {
        return temp_data ? ImAppDataReflectable<TempDataT> : ImAppDataReflectable<PersistDataT>;
    }

    virtual void RefreshDependencyData(const ImGuiApp* app) override final
    {
        this->RebindDependencies(app);
    }

    virtual int GetDependencyTypeIDs(ImGuiID* out, int cap) const override final
    {
        const int count = (int)(sizeof...(DataDependencies));
        if (out == nullptr || cap <= 0)
            return count;
        const ImGuiID ids[] = {(ImGuiID)0, ImGuiAppType<DataDependencies>::ID...}; // leading 0 -> never zero-size
        const int     n     = count < cap ? count : cap;
        for (int i = 0; i < n; i++)
            out[i] = ids[i + 1];
        return n;
    }

    virtual int GetDependencyOptional(bool* out, int cap) const override final
    {
        const int count = (int)(sizeof...(DataDependencies));
        if (out == nullptr || cap <= 0)
            return count;
        const int n = count < cap ? count : cap;
        for (int i = 0; i < n; i++)
            out[i] = this->_DependencyOptional[i];
        return n;
    }

    virtual bool SetDependencyBinding(ImGuiApp* app, const ImGuiAppDataBinding* bind) override final
    {
        if (app == nullptr || bind == nullptr)
            return false;
        constexpr int count = (int)(sizeof...(DataDependencies));
        const ImGuiID ids[] = {(ImGuiID)0, ImGuiAppType<DataDependencies>::ID...};
        for (int slot = 0; slot < count; slot++)
        {
            if (ids[slot + 1] != bind->TypeID)
                continue;
            this->_DependencyKeys[slot]     = ImAppHashType(bind->TypeID, bind->Instance);
            this->_DependencyOptional[slot] = bind->Optional;
            this->RebindDependencies(app);
            app->CompositionRevision++; // rewiring changes the dependency DAG: update order must rebuild
            return true;
        }
        return false;
    }

    virtual bool GetLiveData(const void** out_persist, const void** out_temp) const override final
    {
        if (this->_InstanceData == nullptr)
            return false;
        if (out_persist != nullptr)
            *out_persist = &this->_InstanceData->PersistData;
        if (out_temp != nullptr)
            *out_temp = &this->_InstanceData->TempData;
        return true;
    }
};

template <typename PersistDataT, typename TempDataT, typename... DataDependencies>
struct ImGuiAppControl : ImGuiAppControlMirrorAdapter<PersistDataT, TempDataT, DataDependencies...>
{
    using ControlDataType         = PersistDataT;
    using ControlInstanceDataType = ImGuiAppInterfaceAdapter<ImGuiAppControlBase, PersistDataT, TempDataT, DataDependencies...>::InstanceData;

    ImGuiAppControl() { ImGui::AppEnsureTypeRegistered<PersistDataT>(); ImGui::AppEnsureTypeRegistered<TempDataT>(); } // materialize both data manifests into the schema registry
};

template <typename T>
struct ImGuiAppWindow : ImGuiAppWindowBase
{
    virtual void OnInitialize(ImGuiApp*)                         const override { };
    virtual void OnShutdown(ImGuiApp*)                           const override { };
    virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*) const override { };
    virtual void OnUpdate(ImGuiApp* app, float dt)               const override { };
    virtual void OnDraw(const ImGuiApp*)                         const override { };
};

template <typename T>
struct ImGuiAppSidebar : ImGuiAppSidebarBase
{
    virtual void OnInitialize(ImGuiApp*)                         const override { };
    virtual void OnShutdown(ImGuiApp*)                           const override { };
    virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*) const override { };
    virtual void OnUpdate(ImGuiApp* app, float dt)               const override { };
    virtual void OnDraw(const ImGuiApp*)                         const override { };
};

//-----------------------------------------------------------------------------
// [SECTION] Inline composition API (Push/Pop layers, windows, sidebars, controls; needs a complete ImGuiApp)
//-----------------------------------------------------------------------------

namespace ImGui
{
    template <typename T>
    IMGUI_API inline void DestroyAppStorageValue(void* ptr)
    {
        IM_DELETE((T*)ptr);
    }

    template <typename T>
    IMGUI_API inline void PushAppLayer(ImGuiApp* app)
    {
        IM_ASSERT(app);
        char label[IM_LABEL_SIZE];
        ImAppFormatLabel<T>(label, sizeof(label));
        T* layer = IM_NEW(T)();
        AppRegisterLayer(app, layer, label);
    }

    // The sole instance of a window type keeps its bare class name; a second live instance of the
    // same type gets "##N" so imgui window ids stay distinct.
    template <typename T>
    IMGUI_API inline void PushAppWindow(ImGuiApp* app)
    {
        IM_ASSERT(app);
        char label[IM_LABEL_SIZE];
        ImAppFormatLabel<T>(label, sizeof(label));
        T* window = IM_NEW(T)();
        AppRegisterWindow(app, window, label);
    }

    template <typename T>
    IMGUI_API inline void PushAppSidebar(ImGuiApp* app, ImGuiViewport* vp, ImGuiDir dir, float size, ImGuiWindowFlags flags)
    {
        IM_ASSERT(app);
        char label[IM_LABEL_SIZE];
        ImAppFormatLabel<T>(label, sizeof(label));
        T* sidebar = IM_NEW(T)();
        AppRegisterSidebar(app, sidebar, label, vp, dir, size, flags);
    }

    // Shared body of PushAppControl / PushWindowControl / PushSidebarControl: generate the label, key the
    // instance data by (control data type, instance), construct control T + its instance data, register its
    // storage (snapshottable when trivially copyable), wire _InstanceID/_InstanceData, resolve bindings.
    template <typename T>
    IMGUI_API inline T* AppControlCreate(ImGuiApp* app, ImGuiID instance, const ImGuiAppDataBinding* binds, int binds_count, const char* host_kind, const char* host_label)
    {
        IM_ASSERT(app);
        char label[IM_LABEL_SIZE];
        ImAppFormatLabel<T>(label, sizeof(label));
        T* control = IM_NEW(T)();
        using Inst = typename T::ControlInstanceDataType;
        Inst* instance_data = IM_NEW(Inst)();
        ImGuiID data_type_id = ImGuiAppType<typename T::ControlDataType>::ID;

        // Instance data is keyed by (control data type, instance); AppControlRegisterStorage hashes + asserts uniqueness.
        // Trivially-copyable instance data is snapshottable (size + TempData byte range); heap-owning data is opaque.
        AppControlRegisterStorage(app, control, label, data_type_id, instance, instance_data,
                                  std::is_trivially_copyable_v<Inst>, (int)sizeof(Inst),
                                  (int)offsetof(Inst, TempData),
                                  (int)sizeof(instance_data->TempData),
                                  DestroyAppStorageValue<Inst>,
                                  host_kind,
                                  host_label);
        control->_InstanceID   = instance;
        control->_InstanceData = instance_data;
        control->ResolveDependencies(app, binds, binds_count);
        return control;
    }

    template <typename T>
    IMGUI_API inline void PushAppControl(ImGuiApp* app, ImGuiID instance, const ImGuiAppDataBinding* binds, int binds_count)
    {
        IM_ASSERT(app);
        AppControlPush(app, &app->Controls, AppControlCreate<T>(app, instance, binds, binds_count, nullptr, nullptr));
    }

    // Host a control inside a window: instance data registers in app->Data as usual, but the control joins
    // window->Children and renders between the host window's Begin/End (no Begin of its own).
    template <typename T>
    IMGUI_API inline void PushWindowControl(ImGuiApp* app, ImGuiAppWindowBase* window, ImGuiID instance, const ImGuiAppDataBinding* binds, int binds_count)
    {
        IM_ASSERT(app);
        IM_ASSERT(window);
        AppControlPush(app, &window->Children, AppControlCreate<T>(app, instance, binds, binds_count, "window", window->Label));
    }

    template <typename T>
    IMGUI_API inline void PushSidebarControl(ImGuiApp* app, ImGuiAppSidebarBase* sidebar, ImGuiID instance, const ImGuiAppDataBinding* binds, int binds_count)
    {
        IM_ASSERT(app);
        IM_ASSERT(sidebar);
        AppControlPush(app, &sidebar->Children, AppControlCreate<T>(app, instance, binds, binds_count, "sidebar", sidebar->Label));
    }

    // Visit every pushed control in update order: app-level, then sidebar-hosted, then window-hosted.
    // visitor(control, host) with host == nullptr for app-level. The single shared enumeration of "all controls".
    template <typename Visitor>
    IMGUI_API inline void ForEachAppNode(ImGuiApp* app, Visitor visitor)
    {
        IM_ASSERT(app);
        for (int i = 0; i < app->Controls.Size; i++)
            visitor(app->Controls.Data[i], (ImGuiAppWindowBase*)nullptr);
        for (int s = 0; s < app->Sidebars.Size; s++)
            for (int i = 0; i < app->Sidebars.Data[s]->Children.Size; i++)
                visitor(app->Sidebars.Data[s]->Children.Data[i], (ImGuiAppWindowBase*)app->Sidebars.Data[s]);
        for (int w = 0; w < app->Windows.Size; w++)
            for (int i = 0; i < app->Windows.Data[w]->Children.Size; i++)
                visitor(app->Windows.Data[w]->Children.Data[i], app->Windows.Data[w]);
    }

    template <typename Visitor>
    IMGUI_API inline void ForEachAppNode(const ImGuiApp* app, Visitor visitor)
    {
        IM_ASSERT(app);
        for (int i = 0; i < app->Controls.Size; i++)
            visitor((const ImGuiAppNodeBase*)app->Controls.Data[i], (const ImGuiAppWindowBase*)nullptr);
        for (int s = 0; s < app->Sidebars.Size; s++)
            for (int i = 0; i < app->Sidebars.Data[s]->Children.Size; i++)
                visitor((const ImGuiAppNodeBase*)app->Sidebars.Data[s]->Children.Data[i], (const ImGuiAppWindowBase*)app->Sidebars.Data[s]);
        for (int w = 0; w < app->Windows.Size; w++)
            for (int i = 0; i < app->Windows.Data[w]->Children.Size; i++)
                visitor((const ImGuiAppNodeBase*)app->Windows.Data[w]->Children.Data[i], (const ImGuiAppWindowBase*)app->Windows.Data[w]);
    }
} // namespace ImGui

#ifdef _MSC_VER
#pragma warning(pop)
#endif
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#endif // #ifndef IMGUI_DISABLE
