#pragma once

/*

Index of this file:
// [SECTION] Header mess
// [SECTION] Forward declarations and basic types
// [SECTION] Compile-time helpers (ImGuiStatic<>, ImGuiType<>)
// [SECTION] Dear ImGui end-user API functions

*/

//-----------------------------------------------------------------------------
// [SECTION] Header mess
//-----------------------------------------------------------------------------

#include "imgui.h" 										    // IMGUI_API, ImGuiID, ImGuiStorage, ImBitArray, ImGuiTextIndex, ImChunkStream
#include "imgui_internal.h"               // ImStrncpy
#include "imguiapp_config.h"

// Keep VERSION and VERSION_NUM in sync.
#define IMGUI_APPLAYER_VERSION      "0.4.1"
#define IMGUI_APPLAYER_VERSION_NUM  401

#include <mutex>                          // std::call_once
#include <tuple>
#include <type_traits>
#include <string_view>
#include <array>                          // compile-time declaration spellings (ImGuiAppVecSpelling)

// Compile-time reflection (imguiapp_reflect.h, the applayer's port of qlibs/reflect):
// powers the live mirror's field introspection.
// windows.h's min/max macros (leaked by platform-backend TUs) break its std::min.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include "imguiapp_reflect.h"
#pragma pop_macro("max")
#pragma pop_macro("min")
#define IMGUIAPP_HAS_REFLECT 1

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 26495)          // [Static Analyzer] Variable 'XXX' is uninitialized. Always initialize a member variable (type.6).
#endif

//-----------------------------------------------------------------------------
// [SECTION] Forward declarations and basic types
//-----------------------------------------------------------------------------

// Forward declarations: ImGuiStatic layer
template <typename T> struct ImGuiStatic;

// Forward declarations: ImGuiApp layer
struct ImGuiApp;
struct ImGuiAppBase;
struct ImGuiAppLayerBase;
struct ImGuiAppLayer;
struct ImGuiAppTaskLayer;
struct ImGuiAppCommandLayer;

// Forward declarations: ImGuiAppControl layer
struct ImGuiAppControlBase;
template <typename PersistDataT, typename TempDataT, typename... DataDependencies>                struct ImGuiInterfaceAdapterBase;
template <typename Base, typename PersistDataT, typename TempDataT, typename... DataDependencies> struct ImGuiInterfaceAdapter;
template <typename PersistDataT, typename TempDataT, typename... DataDependencies> struct ImGuiAppControl;

// Forward declarations: ImGuiAppDisplay layer
struct ImGuiAppWindowBase;
struct ImGuiAppGraph;   // authored node graph (imguiapp_nodes.h)

// Forward declarations: ImGuiAppSidebar layer
struct ImGuiAppSidebarBase;

// Forward declarations: state history + input log (time travel / record-replay)
struct ImGuiAppStateHistory;
struct ImGuiAppInputLog;

enum ImGuiAppCommand
{
  ImGuiAppCommand_None,
  ImGuiAppCommand_Shutdown,
  ImGuiAppCommand_COUNT,
};

enum ImGuiAppCommandPrivate
{
  ImGuiAppCommandPrivate_ = ImGuiAppCommand_COUNT,
};

// Frame identity: one id per frame, taken at the top of OnDrawFrame. The correlation key across
// video frames, sidecar records, WAL lines, and test logs (docs/av-design.md).
struct ImGuiAppFrameID
{
  ImU64  FrameIndex; // monotonic from run start (not ImGui's frame count: survives context recreation)
  ImU64  Tsc;        // __rdtsc / platform equivalent at frame begin
  double TimeSec;    // QPC seconds since run start

  ImGuiAppFrameID() { FrameIndex = 0; Tsc = 0; TimeSec = 0.0; }
};

// Advisory frame pacer. Backend run loops call AppPacerWait once per iteration, before OnDrawFrame;
// Off returns immediately. The pacer decides what time the app SIMULATES; video timing is separate
// (imapp_av.h ImGuiAppAVTimingMode) -- honest-realtime video takes PTS from FrameID.TimeSec.
typedef int ImGuiAppPacerMode;
enum ImGuiAppPacerMode_
{
  ImGuiAppPacerMode_Off = 0,     // free-run; vsync/present mode governs
  ImGuiAppPacerMode_Target,      // pace wall clock to TargetHz (sleep + spin hybrid)
  ImGuiAppPacerMode_Fixed,       // Target pacing AND io.DeltaTime forced to exactly 1/TargetHz (determinism: replay, tests)
};

struct ImGuiAppPacer
{
  ImGuiAppPacerMode Mode;
  float             TargetHz;        // <= 0 with Mode_Target = pace to primary monitor refresh
  float             SleepSlackMs;    // spin the last N ms (OS sleep granularity guard)
                                     // read-only telemetry
  double            LastFrameMs;
  double            LastWaitMs;
  ImU64             MissedDeadlines; // frames that arrived after their deadline

  ImGuiAppPacer() { Mode = ImGuiAppPacerMode_Off; TargetHz = 0.0f; SleepSlackMs = 2.0f; LastFrameMs = 0.0; LastWaitMs = 0.0; MissedDeadlines = 0; }
};

// Write-ahead logger. Each record is appended and flushed BEFORE the operation it names executes, so after
// a crash the file tail names the in-flight operation. Attach to ImGuiApp::WAL; null = silent.
typedef int ImGuiAppWALLevel;
enum ImGuiAppWALLevel_
{
  ImGuiAppWALLevel_Off = 0,
  ImGuiAppWALLevel_Lifecycle,   // composition changes, storage, command dispatch
  ImGuiAppWALLevel_Frame,       // + per-frame per-layer phase begins (crash hunts; large files)
};

struct ImGuiAppWAL
{
  void*                  File;    // FILE*; typed void* to keep <cstdio> out of this header
  int                    Seq;     // monotonic record number
  ImGuiAppWALLevel       Level;
  const ImGuiAppFrameID* FrameID; // optional (point at ImGuiApp::FrameID): prefixes records "[tick:N tsc:T]"
  char                   Path[256];

  ImGuiAppWAL() { File = nullptr; Seq = 0; Level = ImGuiAppWALLevel_Off; FrameID = nullptr; Path[0] = 0; }
};

struct ImGuiAppAVFrame;   // imguiapp_av.h: one captured frame (pixels + FrameID + per-frame blob)
struct ImGuiAppRecorder;  // imguiapp_av.h: active recording; OnEncodeFrame pumps it

// Platform backend interface. The core app layer depends only on this vtable. Exactly one backend
// translation unit is linked per build; it defines ImGuiApp_GetPlatformBackend().
struct ImGuiAppPlatformBackend
{
  bool (*InitPlatform)(ImGuiApp* app, ImGuiAppConfig& config);
  void (*ShutdownPlatform)(ImGuiApp* app);
  int  (*RunLoop)(ImGuiApp* app);
  // Optional (null = backend cannot capture; recording fails with a clear error). Readback in the
  // encode phase (after render, before present). Encode-every-frame contract: the FIRST call of a
  // take returns the current frame (synchronously if the pipeline is unprimed); steady state may
  // return frame N-1's pixels PROVIDED out_frame->FrameID carries the id recorded at copy time (a
  // zeroed id gets the pumping frame's stamped by the recorder); a call with no new frame rendered
  // since the last one returns the freshest unreturned copy if already GPU-complete (never block),
  // else false -- callers drain the tail by re-calling after the GPU settles. Never return the
  // same FrameIndex twice. Pixels stay valid until the next CaptureFrame call.
  bool (*CaptureFrame)(ImGuiApp* app, ImGuiAppAVFrame* out_frame);
};

IMGUI_API const ImGuiAppPlatformBackend* ImGuiApp_GetPlatformBackend();

//-----------------------------------------------------------------------------
// [SECTION] Compile-time helpers (ImGuiStatic<>, ImGuiType<>)
//-----------------------------------------------------------------------------

#ifndef ImFuncSig
#ifdef _MSC_VER
#define ImFuncSig __FUNCSIG__
#else
#define ImFuncSig __PRETTY_FUNCTION__
#endif 
#endif 

#ifndef IM_LABEL_SIZE
#define IM_LABEL_SIZE 256
#endif 

#ifndef ImParseTypeStart
#ifdef _MSC_VER
#define ImParseTypeStart "::"
#define ImParseTypeStart2 " "
#else
#define ImParseTypeStart '='
#endif 
#endif 

#ifndef ImParseTypeStart2
#define ImParseTypeStart2 ImParseTypeStart
#endif

#ifndef ImParseTypeEnd
#ifdef _MSC_VER
#define ImParseTypeEnd ">"
#else
#define ImParseTypeEnd ']'
#endif 
#endif 


struct ImGuiInterface { char Label[IM_LABEL_SIZE] = {}; ImGuiInterface() = default; virtual ~ImGuiInterface() = default; };

template <typename T>
struct ImGuiStatic
{
  inline static constexpr const char*      _FunctionSignature()                { return ImFuncSig; }
  inline static constexpr bool             _StartsWith(std::string_view sv, std::string_view prefix) { return sv.size() >= prefix.size() && sv.substr(0, prefix.size()) == prefix; }
  inline static constexpr std::string_view _StripTypeKeyword(std::string_view sv)
  {
    return _StartsWith(sv, "struct ") ? sv.substr(7) :
           _StartsWith(sv, "class ")  ? sv.substr(6) :
           _StartsWith(sv, "enum ")   ? sv.substr(5) :
           _StartsWith(sv, "union ")  ? sv.substr(6) : sv;
  }
  inline static constexpr std::string_view _StripDisplayScope(std::string_view sv)
  {
    sv = _StripTypeKeyword(sv);
    size_t scope = sv.rfind("::");
    return _StripTypeKeyword(scope == std::string_view::npos ? sv : sv.substr(scope + 2));
  }
  inline static constexpr std::string_view _ParseType(std::string_view sv)
  {
    constexpr std::string_view clang_marker = "T = ";
    size_t start = sv.find(clang_marker);
    if (start != std::string_view::npos)
    {
      start += clang_marker.size();
      size_t end = sv.find(';', start);
      size_t bracket_end = sv.find(']', start);
      if (end == std::string_view::npos || (bracket_end != std::string_view::npos && bracket_end < end))
        end = bracket_end;
      if (end == std::string_view::npos)
        end = sv.size();
      return _StripDisplayScope(sv.substr(start, end - start));
    }

    constexpr std::string_view msvc_marker = "ImGuiStatic<";
    start = sv.find(msvc_marker);
    if (start != std::string_view::npos)
    {
      start += msvc_marker.size();
      size_t depth = 1;
      for (size_t i = start; i < sv.size(); ++i)
      {
        if (sv[i] == '<')
          ++depth;
        else if (sv[i] == '>' && --depth == 0)
          return _StripDisplayScope(sv.substr(start, i - start));
      }
    }

    size_t end = sv.rfind(ImParseTypeEnd);
    auto sv2 = sv.substr(0, end);
    start = (sv2.rfind(ImParseTypeStart) > sv2.rfind(ImParseTypeStart2)) ? sv2.rfind(ImParseTypeStart) : sv2.rfind(ImParseTypeStart2);
    start = start >= end ? 0 : start;
    return _StripDisplayScope((sv.size() > end) && (end >= (start + 2)) ? sv.substr(start + 2, end - (start + 1)) : sv);
  }
  inline static constexpr ImGuiID          _ConstantHash(std::string_view sv)  { return *sv.data() ? static_cast<ImGuiID>(*sv.data()) + 33 * _ConstantHash(sv.data() + 1) : 5381; }
  inline static           ImGuiID          GetRelativeID()                     { std::call_once(_Initialized, []() { Count = 1; }); return Count++; }
  static constexpr        std::string_view Name                                { _ParseType(_FunctionSignature()) };
  static constexpr        ImGuiID          ID                                  { _ConstantHash(Name) };
  inline static           int              Count;
  inline static           std::once_flag   _Initialized;
};

template <typename T>
using ImGuiType = ImGuiStatic<std::remove_cvref_t<std::remove_pointer_t<T>>>;

template <typename T>
inline static void GenerateLabel(char* label, size_t size) { std::string_view sv = ImGuiType<T>::Name; ImFormatString(label, size, "%.*s", (int)sv.size(), sv.data()); }

// State-delta event helpers over (this frame, last frame): rising = started, falling = ended, changed = either.
inline static bool ImAppRising (bool now, bool last) { return now && !last; }
inline static bool ImAppFalling(bool now, bool last) { return !now && last; }
inline static bool ImAppChanged(bool now, bool last) { return now ^ last; }
template <typename T>
inline static bool ImAppChanged(const T& now, const T& last) { return !(now == last); }

// splitmix64, no global state. Keep the seed in PersistData (seed in OnInitialize, step only through the
// seed) so snapshots and replay reproduce it; hidden effect sources (rand(), clocks) break replay.
inline static ImU64 ImAppRandom(ImU64* seed)
{
  ImU64 z = (*seed += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
inline static float ImAppRandomFloat(ImU64* seed, float mn, float mx)   // uniform in [mn, mx)
{
  return mn + (mx - mn) * (float)(ImAppRandom(seed) >> 40) / (float)(1ull << 24);
}
inline static int ImAppRandomInt(ImU64* seed, int mn, int mx)           // uniform in [mn, mx]
{
  return mx <= mn ? mn : mn + (int)(ImAppRandom(seed) % (ImU64)(mx - mn + 1));
}

// Best-effort symbolized backtrace of the caller as "  #N name (file:line)" lines; returns characters
// written. skip_frames drops innermost frames (0 = caller). Win32 only; other platforms write "".
IMGUI_API int ImStackTrace(char* out, int out_size, int skip_frames = 0);

// IM_ASSERT sink (wired via IMGUI_USER_CONFIG): logs expr/file/line + ImStackTrace to the SetAppAssertWAL
// sink and stderr, flushes, then __debugbreak()s under a debugger or exits with code 3 -- never the CRT popup.
IMGUI_API void ImGuiAppAssertFail(const char* expr, const char* file, int line);

// One authorable style-var override: Value.x for float vars, both lanes for ImVec2 vars; Active is
// runtime-toggleable.
// Aggregate (default member initializers, no ctors) so the build-time reflection walk sees
// its members. Float-valued vars brace-init as { var, ImVec2(v, 0.0f) }.
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
  ImGuiID TypeID;           // ImGuiType<Dep>::ID of the dependency being routed
  ImGuiID Instance;         // producer's instance id (0 = the type singleton)
  bool    Optional = false; // absent producer resolves to null instead of asserting; the consumer
                            // handles null (and is rebound live when the producer is pushed/popped)
};

// Storage key for a control's instance data in ImGuiApp::Data: instance 0 keeps the bare
// data type id (the type singleton), any other instance qualifies it.
inline ImGuiID ImGuiAppInstanceKey(ImGuiID type_id, ImGuiID instance)
{
  if (instance == 0)
    return type_id;
  return (ImGuiID)ImHashData(&instance, sizeof(instance), type_id);
}

//-----------------------------------------------------------------------------
// [SECTION] Dear ImGui end-user API functions
//-----------------------------------------------------------------------------

namespace ImGui
{
  IMGUI_API void InitializeApp(ImGuiApp* app, const ImGuiAppConfig* config = nullptr);
  IMGUI_API void ShutdownApp(ImGuiApp* app);
  IMGUI_API void UpdateApp(ImGuiApp* app);                    // dt = GetIO().DeltaTime
  IMGUI_API void UpdateApp(ImGuiApp* app, float dt);          // explicit dt (replay injects here)
  IMGUI_API void RenderApp(const ImGuiApp* app);
  IMGUI_API void RegisterAppStorage(ImGuiApp* app, ImGuiID id, void* ptr, void (*destroy)(void*));
  IMGUI_API void RegisterAppStorage(ImGuiApp* app, ImGuiID id, void* ptr, int size, void (*destroy)(void*));   // size > 0 => snapshottable
  IMGUI_API void RegisterAppStorage(ImGuiApp* app, ImGuiID id, void* ptr, int size, int temp_offset, int temp_size, void (*destroy)(void*));   // + input (TempData) byte range
  IMGUI_API void UnregisterAppStorage(ImGuiApp* app, ImGuiID id);   // destroys + removes one entry
  IMGUI_API void ClearAppStorage(ImGuiApp* app);

  // Snapshot appends snapshottable state to the ring (layout rebuilt + history cleared on composition
  // change); Restore copies snapshot `index` (0 = oldest) into the live app. False = nothing
  // snapshottable / invalid index or composition.
  IMGUI_API bool AppStateSnapshot(ImGuiApp* app, ImGuiAppStateHistory* h);
  IMGUI_API bool AppStateRestore(ImGuiApp* app, ImGuiAppStateHistory* h, int index);
  IMGUI_API void AppStateHistoryClear(ImGuiAppStateHistory* h);

  // AppInputRecord appends this frame's inputs (every control's TempData + dt) and resulting state hash;
  // call once per frame AFTER RenderApp. AppInputReplay re-runs the recorded frames through UpdateApp (no
  // rendering) -- restore the starting state first. out_first_divergence (if non-null) receives the first
  // frame whose state hash differs from the recording; -1 = deterministic reproduction.
  IMGUI_API bool AppInputRecord(ImGuiApp* app, ImGuiAppInputLog* log, float dt);
  IMGUI_API bool AppInputReplay(ImGuiApp* app, const ImGuiAppInputLog* log, int* out_first_divergence);
  IMGUI_API void AppInputLogClear(ImGuiAppInputLog* log);

  // Hash of the Persist + LastTemp prefix of every snapshottable instance -- the same
  // per-frame fingerprint AppInputRecord stores. 0 when nothing snapshottable exists.
  IMGUI_API ImGuiID AppStateHash(const ImGuiApp* app);

  // Advisory frame pacing. Backend run loops call this once per iteration before OnDrawFrame; Off
  // returns immediately (the call is unconditional in the loops). Sleeps until deadline - SleepSlackMs,
  // spins the rest on QPC; Fixed mode also forces io.DeltaTime to exactly 1/TargetHz.
  IMGUI_API void AppPacerWait(ImGuiApp* app);

  // The rate the pacer actually paces at: TargetHz when positive, else the primary
  // monitor's refresh rate (the same resolution AppPacerWait performs). Callers that
  // need the frame rate (e.g. an encode config) read it here instead of guessing.
  IMGUI_API float AppPacerResolveHz(const ImGuiApp* app);

  // Consulted by the backend's per-viewport present hook (Renderer_SwapBuffers /
  // Platform_RenderWindow). True = present this frame; false = skip (contents unchanged
  // on that monitor until its next deadline). Main viewport never skips; Off pacer never skips.
  IMGUI_API bool AppPacerViewportShouldPresent(ImGuiApp* app, ImGuiViewport* viewport);

  // AppWALWrite appends one record and flushes to disk BEFORE returning; records below the WAL's level
  // are dropped. All three are null-safe on wal.
  IMGUI_API bool AppWALOpen(ImGuiAppWAL* wal, const char* path, ImGuiAppWALLevel level);
  IMGUI_API void AppWALClose(ImGuiAppWAL* wal);
  IMGUI_API void AppWALWrite(ImGuiAppWAL* wal, ImGuiAppWALLevel level, const char* fmt, ...) IM_FMTARGS(3);

  // WAL sink for IM_ASSERT failures routed to ImGuiAppAssertFail.
  IMGUI_API void SetAppAssertWAL(ImGuiAppWAL* wal);

  // Identity of the app's composition (layers, windows/sidebars, controls, in order). Changes exactly
  // when something is pushed or popped; mirrors poll it and reconcile only on change.
  IMGUI_API ImGuiID GetAppCompositionID(const ImGuiApp* app);

  // Controls sorted by the resolved dependency wiring: every producer before its consumers,
  // composition order among independents. Rebuilt when the composition changes. ONLY the Task
  // layer's OnUpdate pass iterates this -- update is the pass where producers write what
  // consumers read same-frame. Command collection and rendering stay composition order.
  IMGUI_API const ImVector<ImGuiAppControlBase*>* AppRebuildUpdateOrder(ImGuiApp* app);

  template <typename T>
  inline void PushAppSidebar(ImGuiApp* app, ImGuiViewport* vp, ImGuiDir dir, float size = 0.0f, ImGuiWindowFlags flags = 0);
  inline void PopAppSidebar(ImGuiApp* app);

  template <typename T>
  IMGUI_API inline void PushAppLayer(ImGuiApp* app);
  IMGUI_API inline void PopAppLayer(ImGuiApp* app);

  // instance: client-chosen discriminator; 0 = the type singleton (bare type-id key), any other
  // value keys a distinct instance of the same control data type. binds routes individual
  // dependencies to specific producer instances; an unrouted dependency resolves to the pushing
  // control's own instance id, then to the singleton (producer must be pushed first either way).
  template <typename T>
  IMGUI_API inline void PushAppControl(ImGuiApp* app, ImGuiID instance = 0, const ImGuiAppDataBinding* binds = nullptr, int binds_count = 0);
  IMGUI_API inline void PopAppControl(ImGuiApp* app);

  template <typename T>
  IMGUI_API inline void PushWindowControl(ImGuiApp* app, ImGuiAppWindowBase* window, ImGuiID instance = 0, const ImGuiAppDataBinding* binds = nullptr, int binds_count = 0);

  template <typename T>
  IMGUI_API inline void PushSidebarControl(ImGuiApp* app, ImGuiAppSidebarBase* sidebar, ImGuiID instance = 0, const ImGuiAppDataBinding* binds = nullptr, int binds_count = 0);

  // host: the PROCESS's real app, offered as the "Host app" live-mirror perspective
  // (strictly read-only there: time scrub is disabled for the host -- restoring its
  // state from inside its own render would mutate mid-frame).
  IMGUI_API void ShowAppLayerDemo(bool* p_open = nullptr, ImGuiApp* host = nullptr);

  // The demo Composer's document graph inside `host` (its GraphDocData storage entry), or null
  // before composition. Test harnesses drive the editor camera through it.
  IMGUI_API ::ImGuiAppGraph* AppLayerDemoGraph(ImGuiApp* host);

  // App-time transport (F29): number of state snapshots the running composer has recorded of its
  // snapshottable controls (0 = none). Drives the toolbar scrubber; exposed for the headless scrub test.
  IMGUI_API int AppComposerAppTimeFrames(ImGuiApp* host);

  // Push every Active (in-range) entry; returns the number pushed -- pop with PopStyleVar/PopStyleColor(count).
  IMGUI_API int PushAppStyleMods(const ImGuiAppStyleModDesc* mods, int count);
  IMGUI_API int PushAppColorMods(const ImGuiAppColorModDesc* mods, int count);

  // Monospace font for the generated-code inspector (space-padded alignment needs fixed width). Register
  // at font-init time; null leaves the inspector on the UI font.
  IMGUI_API void SetAppCodeFont(::ImGuiAppGraph* g, ImFont* font);   // code panels; null -> UI font
}

struct ImGuiAppLayerBase : ImGuiInterface
{
  virtual void OnAttach(ImGuiApp*)        const = 0;
  virtual void OnDetach(ImGuiApp*)        const = 0;
  virtual void OnUpdate(ImGuiApp*, float) const = 0;
  virtual void OnRender(const ImGuiApp*)  const = 0;
};

struct ImGuiAppItemBase : ImGuiInterface
{
  // Authored style/color overrides applied around the item's submission. OnStylePush latches the pushed
  // counts and OnStylePop pops those counts, so toggling Active mid-frame cannot unbalance the stacks.
  ImVector<ImGuiAppStyleModDesc> StyleMods;
  ImVector<ImGuiAppColorModDesc> ColorMods;
  mutable int                    _StylePushCount = 0;
  mutable int                    _ColorPushCount = 0;

  virtual void OnInitialize(ImGuiApp*)                         const = 0;
  virtual void OnShutdown(ImGuiApp*)                           const = 0;
  virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*) const = 0;
  virtual void OnUpdate(const ImGuiApp* app, float dt)         const = 0;
  virtual void OnRender(const ImGuiApp*)                       const = 0;
  virtual void OnStylePush(const ImGuiApp*)                    const;
  virtual void OnStylePop(const ImGuiApp*)                     const;
};

struct ImGuiAppWindowBase : ImGuiAppItemBase
{
	bool                           Open     = true;
	ImGuiWindow*                   Window   = nullptr;
	ImGuiViewport*                 Viewport = nullptr;
	ImGuiWindowFlags               Flags    = ImGuiWindowFlags_None;
	ImVector<ImGuiAppControlBase*> Controls;

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

// One reflected member of a control's Persist/Temp aggregate (live-mirror introspection).
// Name/TypeName point at static storage (reflect materializes null-terminated fixed_strings).
typedef int ImGuiAppLiveFieldKind;
enum ImGuiAppLiveFieldKind_
{
  ImGuiAppLiveFieldKind_Opaque = 0,   // unmapped type: Size bytes, TypeName from reflect
  ImGuiAppLiveFieldKind_Bool,
  ImGuiAppLiveFieldKind_S32,
  ImGuiAppLiveFieldKind_U32,
  ImGuiAppLiveFieldKind_F32,
  ImGuiAppLiveFieldKind_F64,
  ImGuiAppLiveFieldKind_Vec2,
  ImGuiAppLiveFieldKind_Vec4,
  ImGuiAppLiveFieldKind_CharArray,    // char[Size]
};

struct ImGuiAppLiveFieldDesc
{
  const char*           Name;
  const char*           TypeName;
  const char*           ElemTypeName; // ImVector element type (schema fields); null otherwise
  int                   Offset;       // within the Persist (or Temp) struct
  int                   Size;
  ImGuiAppLiveFieldKind Kind;
  bool                  Exact;        // TypeName is the member's declared C++ spelling (emit verbatim)
};

struct ImGuiAppControlBase : ImGuiAppItemBase
{
  // Re-expose the compile-time-erased data identity for live mirrors. Defaults inert; ImGuiAppControl<>
  // overrides from its pack.
  virtual ImGuiID GetControlDataID()                              const { return 0; }
  virtual int     GetControlDependencyIDs(ImGuiID* out, int cap)  const { IM_UNUSED(out); IM_UNUSED(cap); return 0; }
  virtual void    GetControlDataTypeName(char* out, int out_size) const { if (out && out_size > 0) out[0] = 0; }
  virtual void    GetControlTempDataTypeName(char* out, int out_size) const { if (out && out_size > 0) out[0] = 0; }
  // Reflected members of PersistDataT (temp_data false) or TempDataT (true). out null = count only.
  virtual int     GetControlFields(ImGuiAppLiveFieldDesc* out, int cap, bool temp_data) const { IM_UNUSED(out); IM_UNUSED(cap); IM_UNUSED(temp_data); return 0; }
  // Live instance memory of the RUNNING control (read-only by contract). False before initialization.
  virtual bool    GetControlLiveData(const void** out_persist, const void** out_temp) const { IM_UNUSED(out_persist); IM_UNUSED(out_temp); return false; }
  // False = outside the reflectable contract (not trivially copyable): opaque, exactly like snapshots.
  virtual bool    IsControlDataReflectable(bool temp_data) const { IM_UNUSED(temp_data); return false; }
  // Rebind the cached dependency pointers from their resolved keys. AppRebuildUpdateOrder calls
  // this after any push/pop, so a re-pushed producer's fresh instance data is picked up (and a
  // popped producer with live consumers asserts instead of dangling).
  virtual void    RefreshControlDependencyData(const ImGuiApp* app) { IM_UNUSED(app); }
  // Declared dependency TYPE ids (the compile-time pack, before resolution): what CAN be wired.
  // GetControlDependencyIDs returns where each slot is wired NOW (resolved storage keys).
  virtual int     GetControlDependencyTypeIDs(ImGuiID* out, int cap) const { IM_UNUSED(out); IM_UNUSED(cap); return 0; }
  // Per-slot Optional flags, same slot order as the id queries (mirror draws soft wires dimmed).
  virtual int     GetControlDependencyOptional(bool* out, int cap) const { IM_UNUSED(out); IM_UNUSED(cap); return 0; }
  // Re-route one declared dependency at runtime (Composer edge rewiring, no pop/re-push): the
  // slot whose type matches bind->TypeID re-resolves to that producer instance and the app's
  // update order rebuilds. False when TypeID is not in this control's pack.
  virtual bool    SetControlDependencyBinding(ImGuiApp* app, const ImGuiAppDataBinding* bind) { IM_UNUSED(app); IM_UNUSED(bind); return false; }
};

// Snapshot contract: aggregate + trivially copyable = byte-snapshottable. Owning containers
// are outside it. This governs SNAPSHOTS only; field enumeration is the weaker
// ImGuiAppFieldsVisible contract below. `using ImGuiAppOpaque = void;` opts out of both.
template <typename T>
inline constexpr bool ImGuiAppDataReflectable = std::is_aggregate_v<T>
                                             && std::is_trivially_copyable_v<T>
                                             && !requires { typename T::ImGuiAppOpaque; };

//-----------------------------------------------------------------------------
// Type schema registry: per-type field manifests BUILT AUTOMATICALLY AT COMPILE TIME from
// the reflection walk (imguiapp_reflect.h) and materialized into a runtime registry anyone can read (live
// mirrors, codegen, inspectors). No hand-authored manifests: instantiating a control (or
// reaching a type through another type's members) registers its manifest transitively.
// Snapshot reflectability (trivially-copyable byte copies) is separate and stricter.
//-----------------------------------------------------------------------------

struct ImGuiAppTypeSchema
{
  const char*                  TypeName; // display name (scope-stripped, matches ImGuiType<T>::Name)
  const ImGuiAppLiveFieldDesc* Fields;   // declaration order
  int                          Count;
  int                          Size;     // sizeof(T)
};

IMGUI_API void                        ImGuiAppRegisterTypeSchema(const ImGuiAppTypeSchema* schema);
IMGUI_API const ImGuiAppTypeSchema*   ImGuiAppFindTypeSchema(const char* type_name);

#ifdef IMGUIAPP_HAS_REFLECT

// Field-enumeration contract: the reflection walk handles any aggregate (the patched probe counts array
// members correctly), bounded by the 64-binding visit chain. The tag opts out explicitly.
template <typename T>
consteval bool ImGuiAppFieldsVisibleFn()
{
  if constexpr (!std::is_aggregate_v<T> || requires { typename T::ImGuiAppOpaque; })
    return false;
  else
    return ImAppReflect::size<T>() <= 64u;
}
template <typename T>
inline constexpr bool ImGuiAppFieldsVisible = ImGuiAppFieldsVisibleFn<T>();

// Display name in static null-terminated storage. Uses ImGuiType's signature parser (not
// reflect's, which cannot parse function-local types), so registry keys match
// GetControlDataTypeName by construction.
template <typename T>
inline const char* ImGuiAppTypeDisplayName()
{
  static char name[IM_LABEL_SIZE] = "";
  if (name[0] == 0)
    GenerateLabel<std::remove_cvref_t<T>>(name, IM_ARRAYSIZE(name));
  return name;
}

template <typename M> struct ImGuiAppVecElemOf { using Type = void; };
template <typename E> struct ImGuiAppVecElemOf<ImVector<E>> { using Type = E; };

// Compile-time declaration spellings for members reflect's scope/template-stripping
// type_name cannot spell: "ImVector<Elem>" and "Pointee*".
template <typename E>
struct ImGuiAppVecSpelling
{
  static constexpr bool PtrElem = std::is_pointer_v<E>;
  static constexpr std::string_view Elem = ImGuiType<E>::Name;   // ImGuiType strips the pointer; re-spell it
  static constexpr auto Make()
  {
    std::array<char, 9 + Elem.size() + 3> a{};
    std::size_t k = 0;
    for (char c : std::string_view{"ImVector<"})
      a[k++] = c;
    for (char c : Elem)
      a[k++] = c;
    if (PtrElem)
      a[k++] = '*';
    a[k++] = '>';
    return a;
  }
  static constexpr std::array<char, 9 + Elem.size() + 3> Value = Make();
};

template <typename P>
struct ImGuiAppPtrSpelling
{
  static constexpr std::string_view Pointee = ImGuiType<P>::Name;
  static constexpr auto Make()
  {
    std::array<char, Pointee.size() + 2> a{};
    std::size_t k = 0;
    for (char c : Pointee)
      a[k++] = c;
    a[k++] = '*';
    return a;
  }
  static constexpr std::array<char, Pointee.size() + 2> Value = Make();
};

template <typename T>
inline int ImGuiAppReflectFields(ImGuiAppLiveFieldDesc* out, int cap);

// Transitive automatic registration: materializes T's manifest into the runtime registry
// and, through the field walk, the manifest of every visible aggregate T reaches (members
// and ImVector elements). Reentrancy-safe: the entry registers before its fields fill.
template <typename T>
inline void ImGuiAppEnsureTypeRegistered()
{
  if constexpr (ImGuiAppFieldsVisible<T>)
  {
    constexpr int n = (int)ImAppReflect::size<T>();
    const char* type_name = ImGuiAppTypeDisplayName<T>();
    if (ImGuiAppFindTypeSchema(type_name) != nullptr)
      return;
    static ImGuiAppLiveFieldDesc fields[n > 0 ? n : 1];
    static ImGuiAppTypeSchema schema;
    schema.TypeName = type_name;
    schema.Fields = fields;
    schema.Count = 0;
    schema.Size = (int)sizeof(T);
    ImGuiAppRegisterTypeSchema(&schema);
    schema.Count = ImGuiAppReflectFields<T>(fields, n > 0 ? n : 1);
  }
}


// Aggregate walk shared by every ImGuiAppControl<> instantiation. Types outside the
// visibility contract yield zero fields rather than failing to compile.
template <typename T>
inline int ImGuiAppReflectFields(ImGuiAppLiveFieldDesc* out, int cap)
{
  if constexpr (ImGuiAppFieldsVisible<T>)
  {
    constexpr int n = (int)ImAppReflect::size<T>();
    if (out == nullptr || cap <= 0)
      return n;
    int written = 0;
    ImAppReflect::for_each<T>([&](auto I)
    {
      constexpr auto i = decltype(I)::value;
      using M = std::remove_cvref_t<decltype(ImAppReflect::get<i>(std::declval<T&>()))>;
      using E = typename ImGuiAppVecElemOf<M>::Type;
      if (written >= cap)
        return;
      ImGuiAppLiveFieldDesc* d = &out[written++];
      d->Name = ImAppReflect::member_name<i, T>().data();
      d->ElemTypeName = nullptr;
      d->Exact = true;
      d->Offset = (int)ImAppReflect::offset_of<i, T>();
      d->Size = (int)sizeof(M);
      if constexpr (std::is_same_v<M, bool>)                                        { d->Kind = ImGuiAppLiveFieldKind_Bool;      d->TypeName = "bool"; }
      else if constexpr (std::is_same_v<M, float>)                                  { d->Kind = ImGuiAppLiveFieldKind_F32;       d->TypeName = "float"; }
      else if constexpr (std::is_same_v<M, double>)                                 { d->Kind = ImGuiAppLiveFieldKind_F64;       d->TypeName = "double"; }
      else if constexpr (std::is_same_v<M, ImVec2>)                                 { d->Kind = ImGuiAppLiveFieldKind_Vec2;      d->TypeName = "ImVec2"; }
      else if constexpr (std::is_same_v<M, ImVec4>)                                 { d->Kind = ImGuiAppLiveFieldKind_Vec4;      d->TypeName = "ImVec4"; }
      else if constexpr (std::is_array_v<M> && std::is_same_v<std::remove_extent_t<M>, char>) { d->Kind = ImGuiAppLiveFieldKind_CharArray; d->TypeName = "char"; }
      else if constexpr (std::is_enum_v<M> && sizeof(M) == 4)                       { d->Kind = ImGuiAppLiveFieldKind_S32;       d->TypeName = "int"; }
      else if constexpr (std::is_integral_v<M> && std::is_signed_v<M> && sizeof(M) == 4)   { d->Kind = ImGuiAppLiveFieldKind_S32; d->TypeName = "int"; }
      else if constexpr (std::is_integral_v<M> && std::is_unsigned_v<M> && sizeof(M) == 4) { d->Kind = ImGuiAppLiveFieldKind_U32; d->TypeName = "unsigned int"; }
      else if constexpr (!std::is_same_v<E, void>)
      {
        // ImVector member: exact spelling; a non-pointer element is registered so codegen can
        // recurse into it (a pointer element's pointee is not owned -- never mirrored).
        d->Kind = ImGuiAppLiveFieldKind_Opaque;
        d->TypeName = ImGuiAppVecSpelling<E>::Value.data();
        if constexpr (!std::is_pointer_v<E>)
        {
          d->ElemTypeName = ImGuiAppTypeDisplayName<E>();
          ImGuiAppEnsureTypeRegistered<E>();
        }
      }
      else if constexpr (std::is_pointer_v<M>)
      {
        d->Kind = ImGuiAppLiveFieldKind_Opaque;
        d->TypeName = ImGuiAppPtrSpelling<std::remove_cv_t<std::remove_pointer_t<M>>>::Value.data();
      }
      else if constexpr (ImGuiAppFieldsVisible<M>)
      {
        // Nested visible aggregate: registered transitively, mirrored by codegen.
        d->Kind = ImGuiAppLiveFieldKind_Opaque;
        d->TypeName = ImGuiAppTypeDisplayName<M>();
        ImGuiAppEnsureTypeRegistered<M>();
      }
      else
      {
        // Leaf outside every contract: bytes, honestly labelled.
        d->Kind = ImGuiAppLiveFieldKind_Opaque;
        d->TypeName = ImGuiAppTypeDisplayName<M>();
        d->Exact = false;
      }
    });
    return written;
  }
  else
  {
    IM_UNUSED(out);
    IM_UNUSED(cap);
    return 0;
  }
}
#endif

template <typename PersistDataT, typename TempDataT, typename... DataDependencies>
struct ImGuiInterfaceAdapterBase : ImGuiInterface
{
  virtual void OnInitialize(ImGuiApp*, PersistDataT*, const DataDependencies*...)                                                const = 0;
  virtual void OnShutdown(ImGuiApp*, PersistDataT*, const DataDependencies*...)                                                  const = 0;
  virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*, const PersistDataT*, const TempDataT*, const DataDependencies*...) const = 0;
  virtual void OnUpdate(float, PersistDataT*, const TempDataT*, const TempDataT*, const DataDependencies*...)                    const = 0;
  virtual void OnRender(const PersistDataT*, TempDataT*, const DataDependencies*...)                                             const = 0;
};

struct ImGuiAppLayer : ImGuiAppLayerBase
{
  virtual void OnAttach(ImGuiApp*)        const override {}
  virtual void OnDetach(ImGuiApp*)        const override {}
  virtual void OnUpdate(ImGuiApp*, float) const override {}
  virtual void OnRender(const ImGuiApp*)  const override {}
};

struct ImGuiAppTaskLayer : ImGuiAppLayer
{
  virtual void OnAttach(ImGuiApp*)        const override final;
  virtual void OnDetach(ImGuiApp*)        const override final;
  virtual void OnUpdate(ImGuiApp*, float) const override final;
  virtual void OnRender(const ImGuiApp*)  const override final;
};

struct ImGuiAppCommandLayer : ImGuiAppLayer
{
  virtual void OnAttach(ImGuiApp*)        const override final;
  virtual void OnDetach(ImGuiApp*)        const override final;
  virtual void OnUpdate(ImGuiApp*, float) const override final;
  virtual void OnRender(const ImGuiApp*)  const override final;
};

struct ImGuiAppStatusLayer : ImGuiAppLayer
{
  virtual void OnAttach(ImGuiApp*)        const override final;
  virtual void OnDetach(ImGuiApp*)        const override final;
  virtual void OnUpdate(ImGuiApp*, float) const override final;
  virtual void OnRender(const ImGuiApp*)  const override final;
};

struct ImGuiAppLayoutLayer : ImGuiAppLayer
{
  virtual void OnAttach(ImGuiApp*)        const override final;
  virtual void OnDetach(ImGuiApp*)        const override final;
  virtual void OnUpdate(ImGuiApp*, float) const override final;
  virtual void OnRender(const ImGuiApp*)  const override final;
};

struct ImGuiAppDisplayLayer : ImGuiAppLayer
{
  virtual void OnAttach(ImGuiApp*)        const override final;
  virtual void OnDetach(ImGuiApp*)        const override final;
  virtual void OnUpdate(ImGuiApp*, float) const override final;
  virtual void OnRender(const ImGuiApp*)  const override final;
};

struct ImGuiAppBase : ImGuiInterface
{
  virtual void OnExecuteCommand(ImGuiAppCommand cmd) = 0;
  bool ShutdownPending = false;
};

struct ImGuiAppStorageEntry
{
  ImGuiID ID         = 0;
  void*   Ptr        = nullptr;
  int     Size       = 0; // byte size when the data is snapshottable (trivially copyable); 0 = opaque
  int     TempOffset = 0; // byte range of the TempData member inside the instance data -- the
  int     TempSize   = 0; // control's per-frame INPUT; [0, TempOffset) is Persist + LastTemp (state)
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
  ImVector<char>    Frames;              // MaxFrames * FrameSize bytes
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

  ImGuiAppInputLog() { CompositionID = 0; FrameSize = 0; Count = 0; }
};

struct ImGuiApp : ImGuiAppBase
{
  ImGuiStorage                   Data;
  ImVector<ImGuiAppStorageEntry> StorageEntries;
  ImVector<ImGuiAppLayerBase*>   Layers;
  ImVector<ImGuiAppWindowBase*>  Windows;
  ImVector<ImGuiAppSidebarBase*> Sidebars;
  ImVector<ImGuiAppControlBase*> Controls;
  ImVector<ImGuiAppControlBase*> UpdateOrder;         // dependency-sorted OnUpdate iteration (AppRebuildUpdateOrder)
  int                            CompositionRevision; // bumped by every storage register/unregister; unlike the composition HASH, pop+repush of the same type still advances it
  int                            UpdateOrderRevision; // revision UpdateOrder and the cached dependency bindings were built at
  ImGuiAppPlatform               Platform;
  ImVec4                         ClearColor;
  void*                          PlatformData;
  ImGuiAppWAL*                   WAL;      // optional write-ahead logger (caller-owned); null = silent
  ImGuiAppRecorder*              Recorder; // active recording (AppRecordBegin registers, AppRecordEnd clears); null = none
  ImGuiAppFrameID                FrameID;  // updated at the top of OnDrawFrame; correlation key for WAL/video/sidecar
  ImGuiAppPacer                  Pacer;    // advisory; consulted by the backend run loop via AppPacerWait
  bool                           Initialized;

  ImGuiApp() : CompositionRevision(0), UpdateOrderRevision(-1), PlatformData(nullptr), WAL(nullptr), Recorder(nullptr), Initialized(false) {}
  virtual ~ImGuiApp();
  int                         Run(int argc, char** argv);
  bool                        Initialize(const ImGuiAppConfig* config);
  bool                        IsInitialized() const { return Initialized; }
  virtual void                Shutdown();
  static void                 DrawFrame(ImGuiApp* app);
  virtual bool                OnInitialize(int argc, char** argv) { return true; }
  virtual void                OnLayout() {}
  // One frame = the four phases in order: draw (frame id, NewFrame, app layers/widgets),
  // render (draw data -> GPU, platform windows), encode (recorder pump reads the frame
  // just rendered), present. Backend run loops call Frame(); override a phase to extend it.
  void                        Frame();
  virtual void                OnDrawFrame();
  virtual void                OnRenderFrame();
  virtual void                OnEncodeFrame();
  virtual void                OnPresentFrame();
  virtual void                OnExecuteCommand(ImGuiAppCommand cmd) override;
  virtual bool                OnInitializePlatform(ImGuiAppConfig& config);
  virtual void                OnShutdownPlatform();
};

template <typename Base, typename PersistDataT, typename TempDataT, typename... DataDependencies>
struct ImGuiInterfaceAdapter : Base, ImGuiInterfaceAdapterBase<PersistDataT, TempDataT, DataDependencies...>
{
    // Created, registered in ImGuiApp::Data, and bound here by PushAppControl<>() before OnInitialize.
    struct InstanceData
    {
      PersistDataT PersistData;
      TempDataT    LastTempData;
      TempDataT    TempData;
    } *_InstanceData = nullptr;

    // Instance identity + dependency routing, resolved once by PushAppControl<>() before OnInitialize.
    ImGuiID                          _InstanceID = 0;
    std::tuple<DataDependencies*...> _Dependencies = {};
    ImGuiID                          _DependencyKeys[sizeof...(DataDependencies) > 0 ? sizeof...(DataDependencies) : 1] = {};
    bool                             _DependencyOptional[sizeof...(DataDependencies) > 0 ? sizeof...(DataDependencies) : 1] = {};

    // Resolution order: explicit binding -> this control's own instance id -> the type singleton.
    // Asserts when the resolved producer was not pushed before this control, unless the binding
    // marks the dependency Optional (then null, rebound live as the producer comes and goes).
    template <typename Dep>
    inline Dep* ResolveDependency(const ImGuiApp* app, const ImGuiAppDataBinding* binds, int binds_count, int slot)
    {
      const ImGuiID type_id = ImGuiType<Dep>::ID;
      for (int i = 0; i < binds_count; i++)
        if (binds[i].TypeID == type_id)
        {
          _DependencyKeys[slot] = ImGuiAppInstanceKey(type_id, binds[i].Instance);
          _DependencyOptional[slot] = binds[i].Optional;
          Dep* data = static_cast<Dep*>(app->Data.GetVoidPtr(_DependencyKeys[slot]));
          IM_ASSERT(data != nullptr || binds[i].Optional);
          return data;
        }
      if (_InstanceID != 0)
      {
        const ImGuiID own_key = ImGuiAppInstanceKey(type_id, _InstanceID);
        Dep* data = static_cast<Dep*>(app->Data.GetVoidPtr(own_key));
        if (data != nullptr)
        {
          _DependencyKeys[slot] = own_key;
          return data;
        }
      }
      _DependencyKeys[slot] = type_id;
      Dep* data = static_cast<Dep*>(app->Data.GetVoidPtr(type_id));
      IM_ASSERT(data != nullptr);
      return data;
    }

    inline void ResolveDependencies(const ImGuiApp* app, const ImGuiAppDataBinding* binds, int binds_count)
    {
      int slot = 0;
      IM_UNUSED(slot);
      _Dependencies = { ResolveDependency<DataDependencies>(app, binds, binds_count, slot++)... };
    }

    // Re-fetch each cached dependency pointer by its resolved key. Asserts when a producer was
    // popped while this consumer is still alive -- except Optional dependencies, which go null.
    template <typename Dep>
    inline Dep* LookupDependency(const ImGuiApp* app, int slot) const
    {
      Dep* data = static_cast<Dep*>(app->Data.GetVoidPtr(_DependencyKeys[slot]));
      IM_ASSERT(data != nullptr || _DependencyOptional[slot]);
      return data;
    }

    inline void RebindDependencies(const ImGuiApp* app)
    {
      int slot = 0;
      IM_UNUSED(slot);
      _Dependencies = { LookupDependency<DataDependencies>(app, slot++)... };
    }

    inline std::tuple<DataDependencies*...> GetAllDependencyData(const ImGuiApp* app) const { IM_UNUSED(app); return _Dependencies; }

    virtual void OnInitialize(ImGuiApp*, PersistDataT*, const DataDependencies*...) const override {}
    virtual void OnInitialize(ImGuiApp* app) const override final
    {
      IM_ASSERT(_InstanceData != nullptr);
      std::apply([=, this](DataDependencies*... dependencies) { OnInitialize(app, &_InstanceData->PersistData, dependencies...); }, GetAllDependencyData(app));
    }

    virtual void OnShutdown(ImGuiApp*, PersistDataT*, const DataDependencies*...) const override {}
    virtual void OnShutdown(ImGuiApp* app) const override final
    {
      std::apply([=, this](DataDependencies*... dependencies) { OnShutdown(app, &_InstanceData->PersistData, dependencies...); }, GetAllDependencyData(app));
    }

    virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*, const PersistDataT*, const TempDataT*, const DataDependencies*...) const override {}
    virtual void OnGetCommand(const ImGuiApp* app, ImGuiAppCommand* cmd) const override final
    {
      std::apply([=, this](DataDependencies*... dependencies) { OnGetCommand(app, cmd, &_InstanceData->PersistData, &_InstanceData->TempData, dependencies...); }, GetAllDependencyData(app));
    }

    virtual void OnUpdate(float, PersistDataT*, const TempDataT*, const TempDataT*, const DataDependencies*...) const override {}
    virtual void OnUpdate(const ImGuiApp* app, float dt) const override final
    {
      std::apply([=, this](DataDependencies*... dependencies) { OnUpdate(dt, &_InstanceData->PersistData, &_InstanceData->TempData, &_InstanceData->LastTempData, dependencies...); }, GetAllDependencyData(app));
      _InstanceData->LastTempData = _InstanceData->TempData;
    }

    virtual void OnRender(const PersistDataT*, TempDataT*, const DataDependencies*...) const override {}
    virtual void OnRender(const ImGuiApp* app) const override final
    {
      _InstanceData->TempData = {};
      std::apply([=, this](DataDependencies*... dependencies) { OnRender(&_InstanceData->PersistData, &_InstanceData->TempData, dependencies...); }, GetAllDependencyData(app));
    }

};

template <typename PersistDataT, typename TempDataT, typename... DataDependencies>
struct ImGuiAppControl : ImGuiInterfaceAdapter<ImGuiAppControlBase, PersistDataT, TempDataT, DataDependencies...>
{
  using ControlDataType = PersistDataT;
  using ControlInstanceDataType = ImGuiInterfaceAdapter<ImGuiAppControlBase, PersistDataT, TempDataT, DataDependencies...>::InstanceData;

  // Constructing any control materializes its data types' manifests (and everything they
  // reach) into the runtime schema registry.
  ImGuiAppControl()
  {
#ifdef IMGUIAPP_HAS_REFLECT
    ImGuiAppEnsureTypeRegistered<PersistDataT>();
    ImGuiAppEnsureTypeRegistered<TempDataT>();
#endif
  }

  // Instance-qualified storage keys -- the same keys app->Data uses. Dependency ids are the
  // RESOLVED producer keys (push-time routing), so mirrors draw the actual wiring.
  virtual ImGuiID GetControlDataID() const override final { return ImGuiAppInstanceKey(ImGuiType<PersistDataT>::ID, this->_InstanceID); }

  virtual int GetControlDependencyIDs(ImGuiID* out, int cap) const override final
  {
    const int count = (int)(sizeof...(DataDependencies));
    if (out == nullptr || cap <= 0)
      return count;
    const int n = count < cap ? count : cap;
    for (int i = 0; i < n; i++)
      out[i] = this->_DependencyKeys[i];
    return n;
  }

  virtual void GetControlDataTypeName(char* out, int out_size) const override final { GenerateLabel<PersistDataT>(out, (size_t)out_size); }
  virtual void GetControlTempDataTypeName(char* out, int out_size) const override final { GenerateLabel<TempDataT>(out, (size_t)out_size); }

  virtual int GetControlFields(ImGuiAppLiveFieldDesc* out, int cap, bool temp_data) const override final
  {
#ifdef IMGUIAPP_HAS_REFLECT
    return temp_data ? ImGuiAppReflectFields<TempDataT>(out, cap) : ImGuiAppReflectFields<PersistDataT>(out, cap);
#else
    IM_UNUSED(out); IM_UNUSED(cap); IM_UNUSED(temp_data);
    return 0;
#endif
  }

  virtual bool IsControlDataReflectable(bool temp_data) const override final
  {
    return temp_data ? ImGuiAppDataReflectable<TempDataT> : ImGuiAppDataReflectable<PersistDataT>;
  }

  virtual void RefreshControlDependencyData(const ImGuiApp* app) override final
  {
    this->RebindDependencies(app);
  }

  virtual int GetControlDependencyTypeIDs(ImGuiID* out, int cap) const override final
  {
    const int count = (int)(sizeof...(DataDependencies));
    if (out == nullptr || cap <= 0)
      return count;
    const ImGuiID ids[] = { (ImGuiID)0, ImGuiType<DataDependencies>::ID... }; // leading 0 -> never zero-size
    const int n = count < cap ? count : cap;
    for (int i = 0; i < n; i++)
      out[i] = ids[i + 1];
    return n;
  }

  virtual int GetControlDependencyOptional(bool* out, int cap) const override final
  {
    const int count = (int)(sizeof...(DataDependencies));
    if (out == nullptr || cap <= 0)
      return count;
    const int n = count < cap ? count : cap;
    for (int i = 0; i < n; i++)
      out[i] = this->_DependencyOptional[i];
    return n;
  }

  virtual bool SetControlDependencyBinding(ImGuiApp* app, const ImGuiAppDataBinding* bind) override final
  {
    if (app == nullptr || bind == nullptr)
      return false;
    constexpr int count = (int)(sizeof...(DataDependencies));
    const ImGuiID ids[] = { (ImGuiID)0, ImGuiType<DataDependencies>::ID... };
    for (int slot = 0; slot < count; slot++)
    {
      if (ids[slot + 1] != bind->TypeID)
        continue;
      this->_DependencyKeys[slot] = ImGuiAppInstanceKey(bind->TypeID, bind->Instance);
      this->_DependencyOptional[slot] = bind->Optional;
      this->RebindDependencies(app);
      app->CompositionRevision++;   // rewiring changes the dependency DAG: update order must rebuild
      return true;
    }
    return false;
  }

  virtual bool GetControlLiveData(const void** out_persist, const void** out_temp) const override final
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

template <typename T>
struct ImGuiAppWindow : ImGuiAppWindowBase
{
  ImGuiAppWindow() { GenerateLabel<T>(this->Label, sizeof(this->Label)); }   // bare class name; PushAppWindow suffixes only real duplicates

  virtual void OnInitialize(ImGuiApp*)                         const override {};
  virtual void OnShutdown(ImGuiApp*)                           const override {};
  virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*) const override {};
  virtual void OnUpdate(const ImGuiApp* app, float dt)         const override {};
  virtual void OnRender(const ImGuiApp*)                       const override {};
};

template <typename T>
struct ImGuiAppSidebar : ImGuiAppSidebarBase
{
  ImGuiAppSidebar() { GenerateLabel<T>(this->Label, sizeof(this->Label)); }   // bare class name; PushAppSidebar suffixes only real duplicates

  virtual void OnInitialize(ImGuiApp*)                         const override {};
  virtual void OnShutdown(ImGuiApp*)                           const override {};
  virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*) const override {};
  virtual void OnUpdate(const ImGuiApp* app, float dt)         const override {};
  virtual void OnRender(const ImGuiApp*)                       const override {};
};

namespace ImGui
{
  template <typename T>
  inline void DestroyAppStorageValue(void* ptr)
  {
      IM_DELETE(static_cast<T*>(ptr));
  }

  // Visit every pushed control in update order: app-level, then sidebar-hosted, then window-hosted.
  // visitor(control, host) with host == nullptr for app-level. The single shared enumeration of "all controls".
  template <typename Visitor>
  inline void ForEachAppControl(ImGuiApp* app, Visitor visitor)
  {
      IM_ASSERT(app);
      for (int i = 0; i < app->Controls.Size; i++)
        visitor(app->Controls.Data[i], (ImGuiAppWindowBase*)nullptr);
      for (int s = 0; s < app->Sidebars.Size; s++)
        for (int i = 0; i < app->Sidebars.Data[s]->Controls.Size; i++)
          visitor(app->Sidebars.Data[s]->Controls.Data[i], (ImGuiAppWindowBase*)app->Sidebars.Data[s]);
      for (int w = 0; w < app->Windows.Size; w++)
        for (int i = 0; i < app->Windows.Data[w]->Controls.Size; i++)
          visitor(app->Windows.Data[w]->Controls.Data[i], app->Windows.Data[w]);
  }

  template <typename Visitor>
  inline void ForEachAppControl(const ImGuiApp* app, Visitor visitor)
  {
      IM_ASSERT(app);
      for (int i = 0; i < app->Controls.Size; i++)
        visitor((const ImGuiAppControlBase*)app->Controls.Data[i], (const ImGuiAppWindowBase*)nullptr);
      for (int s = 0; s < app->Sidebars.Size; s++)
        for (int i = 0; i < app->Sidebars.Data[s]->Controls.Size; i++)
          visitor((const ImGuiAppControlBase*)app->Sidebars.Data[s]->Controls.Data[i], (const ImGuiAppWindowBase*)app->Sidebars.Data[s]);
      for (int w = 0; w < app->Windows.Size; w++)
        for (int i = 0; i < app->Windows.Data[w]->Controls.Size; i++)
          visitor((const ImGuiAppControlBase*)app->Windows.Data[w]->Controls.Data[i], (const ImGuiAppWindowBase*)app->Windows.Data[w]);
  }

  inline void ShutdownAppControls(ImGuiApp* app, ImVector<ImGuiAppControlBase*>& controls)
  {
      IM_ASSERT(app);

      while (!controls.empty())
      {
        ImGuiAppControlBase* control = controls.back();
        controls.pop_back();
        if (app->WAL != nullptr)
        {
          char dt[IM_LABEL_SIZE];
          control->GetControlDataTypeName(dt, IM_ARRAYSIZE(dt));
          AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "shutdown control <%s>", dt);
        }
        control->OnShutdown(app);
        const ImGuiID data_id = control->GetControlDataID();   // read before delete
        IM_DELETE(control);
        if (data_id != 0)
          UnregisterAppStorage(app, data_id);
      }
  }

  template <typename T>
  inline void PushAppLayer(ImGuiApp* app)
  {
      IM_ASSERT(app);

      char name[IM_LABEL_SIZE];
      GenerateLabel<T>(name, sizeof(name));
      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push layer %s", name);

      app->Layers.push_back(IM_NEW(T)());
      if (app->Layers.back()->Label[0] == 0)   // default Label to the type name
        ImStrncpy(app->Layers.back()->Label, name, IM_ARRAYSIZE(app->Layers.back()->Label));
      app->Layers.back()->OnAttach(app);
  }

  inline void PopAppLayer(ImGuiApp* app)
  {
      IM_ASSERT(app);

      if (app->Layers.empty())
        return;

      ImGuiAppLayerBase* layer = app->Layers.back();
      app->Layers.pop_back();
      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "pop layer %s", layer->Label);
      layer->OnDetach(app);
      IM_DELETE(layer);
  }

  // The sole instance of a window type keeps its bare class name; a second live instance of the
  // same type gets "##N" so imgui window ids stay distinct.
  inline void AppDeduplicateItemLabel(char* label, int label_size, const ImVector<ImGuiAppWindowBase*>* windows, const ImVector<ImGuiAppSidebarBase*>* sidebars)
  {
    char base[IM_LABEL_SIZE];
    ImStrncpy(base, label, IM_ARRAYSIZE(base));
    for (int suffix = 2; ; suffix++)
    {
      bool taken = false;
      if (windows != nullptr)
        for (int i = 0; i < windows->Size && !taken; i++)
          taken = strcmp(windows->Data[i]->Label, label) == 0;
      if (sidebars != nullptr)
        for (int i = 0; i < sidebars->Size && !taken; i++)
          taken = strcmp(sidebars->Data[i]->Label, label) == 0;
      if (!taken)
        return;
      ImFormatString(label, (size_t)label_size, "%s##%d", base, suffix);
    }
  }

  template <typename T>
  inline void PushAppWindow(ImGuiApp* app)
  {
    IM_ASSERT(app);
    char name[IM_LABEL_SIZE];
    GenerateLabel<T>(name, sizeof(name));
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push window %s", name);
    T* window = IM_NEW(T)();
    IM_ASSERT(window);
    AppDeduplicateItemLabel(window->Label, IM_ARRAYSIZE(window->Label), &app->Windows, &app->Sidebars);
    app->Windows.push_back(window);
    app->Windows.back()->OnInitialize(app);
  }

  inline void PopAppWindow(ImGuiApp* app)
  {
    IM_ASSERT(app);

    if (app->Windows.empty())
      return;

    ImGuiAppWindowBase* window = app->Windows.back();
    app->Windows.pop_back();
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "pop window '%s'", window->Label);
    ShutdownAppControls(app, window->Controls);
    window->OnShutdown(app);
    IM_DELETE(window);
  }

  template <typename T>
  inline void PushAppSidebar(ImGuiApp* app, ImGuiViewport* vp, ImGuiDir dir, float size, ImGuiWindowFlags flags)
  {
    IM_ASSERT(app);
    char name[IM_LABEL_SIZE];
    GenerateLabel<T>(name, sizeof(name));
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push sidebar %s", name);
    T* sidebar = IM_NEW(T)();
    IM_ASSERT(sidebar);
    AppDeduplicateItemLabel(sidebar->Label, IM_ARRAYSIZE(sidebar->Label), &app->Windows, &app->Sidebars);
    sidebar->Viewport = vp;
    sidebar->DockDir = dir;
    sidebar->Size = size;
    sidebar->Flags = flags;
    app->Sidebars.push_back(sidebar);
    app->Sidebars.back()->OnInitialize(app);
  }

  inline void PopAppSidebar(ImGuiApp* app)
  {
    IM_ASSERT(app);

    if (app->Sidebars.empty())
      return;
    ImGuiAppSidebarBase* sidebar = app->Sidebars.back();
    app->Sidebars.pop_back();
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "pop sidebar '%s'", sidebar->Label);
    ShutdownAppControls(app, sidebar->Controls);
    sidebar->OnShutdown(app);
    IM_DELETE(sidebar);
  }

  template <typename T>
  inline void PushAppControl(ImGuiApp* app, ImGuiID instance, const ImGuiAppDataBinding* binds, int binds_count)
  {
      IM_ASSERT(app);

      char name[IM_LABEL_SIZE];
      GenerateLabel<T>(name, sizeof(name));
      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push control %s (instance %u)", name, (unsigned)instance);

      // Instance data is keyed by the instance-qualified data type id so dependents can resolve it.
      ImGuiID id = ImGuiAppInstanceKey(ImGuiType<typename T::ControlDataType>::ID, instance);

      // One instance per (control data type, instance id).
      typename T::ControlInstanceDataType* instance_data = static_cast<typename T::ControlInstanceDataType*>(app->Data.GetVoidPtr(id));
      IM_ASSERT(nullptr == instance_data);

      T* control = IM_NEW(T)();
      IM_ASSERT(control);
      ImGuiAppControlBase* control_base = control;
      ImStrncpy(control_base->Label, name, sizeof(control_base->Label));

      instance_data = IM_NEW(typename T::ControlInstanceDataType)();
      IM_ASSERT(instance_data);

      app->Data.SetVoidPtr(id, instance_data);
      // Trivially-copyable instance data registers its size + TempData byte range (snapshot/replay);
      // heap-owning data registers opaque.
      {
        const bool snapshottable = std::is_trivially_copyable_v<typename T::ControlInstanceDataType>;
        RegisterAppStorage(app, id, instance_data,
            snapshottable ? (int)sizeof(typename T::ControlInstanceDataType) : 0,
            snapshottable ? (int)((char*)&instance_data->TempData - (char*)instance_data) : 0,
            snapshottable ? (int)sizeof(instance_data->TempData) : 0,
            DestroyAppStorageValue<typename T::ControlInstanceDataType>);
      }
      control->_InstanceID = instance;
      control->_InstanceData = instance_data;
      control->ResolveDependencies(app, binds, binds_count);
      app->Controls.push_back(control);
      app->Controls.back()->OnInitialize(app);
  }

  inline void PopAppControl(ImGuiApp* app)
  {
      IM_ASSERT(app);

      if (app->Controls.empty())
        return;

      ImGuiAppControlBase* control = app->Controls.back();
      app->Controls.pop_back();
      if (app->WAL != nullptr)
      {
        char dt[IM_LABEL_SIZE];
        control->GetControlDataTypeName(dt, IM_ARRAYSIZE(dt));
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "pop control <%s>", dt);
      }
      control->OnShutdown(app);
      const ImGuiID data_id = control->GetControlDataID();   // read before delete; pop frees what push registered
      IM_DELETE(control);
      if (data_id != 0)
        UnregisterAppStorage(app, data_id);
  }

  // Host a control inside a window: instance data registers in app->Data as usual, but the control joins
  // window->Controls and renders between the host window's Begin/End (no Begin of its own).
  template <typename T>
  IMGUI_API inline void PushWindowControl(ImGuiApp* app, ImGuiAppWindowBase* window, ImGuiID instance, const ImGuiAppDataBinding* binds, int binds_count)
  {
      IM_ASSERT(app && window);

      char name[IM_LABEL_SIZE];
      GenerateLabel<T>(name, sizeof(name));
      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push control %s (instance %u) into window '%s'", name, (unsigned)instance, window->Label);

      // Instance data is keyed by the instance-qualified data type id so dependents can resolve it.
      ImGuiID id = ImGuiAppInstanceKey(ImGuiType<typename T::ControlDataType>::ID, instance);

      // One instance per (control data type, instance id).
      typename T::ControlInstanceDataType* instance_data = static_cast<typename T::ControlInstanceDataType*>(app->Data.GetVoidPtr(id));
      IM_ASSERT(nullptr == instance_data);

      T* control = IM_NEW(T)();
      IM_ASSERT(control);
      ImGuiAppControlBase* control_base = control;
      ImStrncpy(control_base->Label, name, sizeof(control_base->Label));

      instance_data = IM_NEW(typename T::ControlInstanceDataType)();
      IM_ASSERT(instance_data);

      app->Data.SetVoidPtr(id, instance_data);
      // Trivially-copyable instance data registers its size + TempData byte range (snapshot/replay);
      // heap-owning data registers opaque.
      {
        const bool snapshottable = std::is_trivially_copyable_v<typename T::ControlInstanceDataType>;
        RegisterAppStorage(app, id, instance_data,
            snapshottable ? (int)sizeof(typename T::ControlInstanceDataType) : 0,
            snapshottable ? (int)((char*)&instance_data->TempData - (char*)instance_data) : 0,
            snapshottable ? (int)sizeof(instance_data->TempData) : 0,
            DestroyAppStorageValue<typename T::ControlInstanceDataType>);
      }
      control->_InstanceID = instance;
      control->_InstanceData = instance_data;
      control->ResolveDependencies(app, binds, binds_count);
      window->Controls.push_back(control);
      window->Controls.back()->OnInitialize(app);
  }

  template <typename T>
  IMGUI_API inline void PushSidebarControl(ImGuiApp* app, ImGuiAppSidebarBase* sidebar, ImGuiID instance, const ImGuiAppDataBinding* binds, int binds_count)
  {
      ImGuiID id;
      T* control;
      typename T::ControlInstanceDataType* instance_data;

      IM_ASSERT(app);

      char name[IM_LABEL_SIZE];
      GenerateLabel<T>(name, sizeof(name));
      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push control %s (instance %u) into sidebar '%s'", name, (unsigned)instance, sidebar ? sidebar->Label : "(null)");

      // Instance data is keyed by the instance-qualified data type id so dependents can resolve it.
      id = ImGuiAppInstanceKey(ImGuiType<typename T::ControlDataType>::ID, instance);

      // One instance per (control data type, instance id).
      instance_data = static_cast<decltype(instance_data)>(app->Data.GetVoidPtr(id));
      IM_ASSERT(nullptr == instance_data);

      control = IM_NEW(T)();
      IM_ASSERT(control);
      ImGuiAppControlBase* control_base = control;
      ImStrncpy(control_base->Label, name, sizeof(control_base->Label));

      instance_data = IM_NEW(typename T::ControlInstanceDataType)();
      IM_ASSERT(instance_data);

      app->Data.SetVoidPtr(id, instance_data);
      // Trivially-copyable instance data registers its size + TempData byte range (snapshot/replay);
      // heap-owning data registers opaque.
      {
        const bool snapshottable = std::is_trivially_copyable_v<typename T::ControlInstanceDataType>;
        RegisterAppStorage(app, id, instance_data,
            snapshottable ? (int)sizeof(typename T::ControlInstanceDataType) : 0,
            snapshottable ? (int)((char*)&instance_data->TempData - (char*)instance_data) : 0,
            snapshottable ? (int)sizeof(instance_data->TempData) : 0,
            DestroyAppStorageValue<typename T::ControlInstanceDataType>);
      }
      control->_InstanceID = instance;
      control->_InstanceData = instance_data;
      control->ResolveDependencies(app, binds, binds_count);
      sidebar->Controls.push_back(control);
      sidebar->Controls.back()->OnInitialize(app);
  }
}

#ifdef _MSC_VER
#pragma warning (pop)
#endif
