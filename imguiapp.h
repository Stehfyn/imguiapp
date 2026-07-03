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
#define IMGUI_APPLAYER_VERSION      "0.4.0"
#define IMGUI_APPLAYER_VERSION_NUM  400

#include <mutex>                          // std::call_once
#include <tuple>
#include <type_traits>
#include <string_view>

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

// Forward declarations: ImGuiAppWindow layer
struct ImGuiAppWindowBase;

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
  ImU64  FrameIndex;   // monotonic from run start (not ImGui's frame count: survives context recreation)
  ImU64  Tsc;          // __rdtsc / platform equivalent at frame begin
  double TimeSec;      // QPC seconds since run start

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
  float  TargetHz;         // <= 0 with Mode_Target = pace to primary monitor refresh
  float  SleepSlackMs;     // spin the last N ms (OS sleep granularity guard)
  // read-only telemetry
  double LastFrameMs;
  double LastWaitMs;
  ImU64  MissedDeadlines;  // frames that arrived after their deadline

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
  void*                  File;      // FILE*; typed void* to keep <cstdio> out of this header
  int                    Seq;       // monotonic record number
  ImGuiAppWALLevel       Level;
  const ImGuiAppFrameID* FrameID;   // optional (point at ImGuiApp::FrameID): prefixes records "[f:N tsc:T]"
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
template <typename T>
inline static void GenerateUniqueLabel(char* label, size_t size) { std::string_view sv = ImGuiType<T>::Name; ImFormatString(label, size, "%.*s##%d", (int)sv.size(), sv.data(), ImGuiType<T>::GetRelativeID()); }

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
struct ImGuiAppStyleModDesc
{
  ImGuiStyleVar Var;
  ImVec2        Value;
  bool          Active;

  ImGuiAppStyleModDesc()                                                { Var = 0; Value = ImVec2(0.0f, 0.0f); Active = true; }
  ImGuiAppStyleModDesc(ImGuiStyleVar var, float v, bool active = true)  { Var = var; Value = ImVec2(v, 0.0f); Active = active; }
  ImGuiAppStyleModDesc(ImGuiStyleVar var, ImVec2 v, bool active = true) { Var = var; Value = v; Active = active; }
};

// One authorable PushStyleColor override. Value is packed IM_COL32 RGBA.
struct ImGuiAppColorModDesc
{
  ImGuiCol Col;
  ImU32    Value;
  bool     Active;

  ImGuiAppColorModDesc()                                        { Col = 0; Value = 0; Active = true; }
  ImGuiAppColorModDesc(ImGuiCol col, ImU32 v, bool active = true) { Col = col; Value = v; Active = active; }
};

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

  // Advisory frame pacing. Backend run loops call this once per iteration before OnDrawFrame; Off
  // returns immediately (the call is unconditional in the loops). Sleeps until deadline - SleepSlackMs,
  // spins the rest on QPC; Fixed mode also forces io.DeltaTime to exactly 1/TargetHz.
  IMGUI_API void AppPacerWait(ImGuiApp* app);

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

  template <typename T>
  inline void PushAppSidebar(ImGuiApp* app, ImGuiViewport* vp, ImGuiDir dir, float size = 0.0f, ImGuiWindowFlags flags = 0);
  inline void PopAppSidebar(ImGuiApp* app);

  template <typename T>
  IMGUI_API inline void PushAppLayer(ImGuiApp* app);
  IMGUI_API inline void PopAppLayer(ImGuiApp* app);

  template <typename T>
  IMGUI_API inline void PushAppControl(ImGuiApp* app);
  IMGUI_API inline void PopAppControl(ImGuiApp* app);

  template <typename T>
  IMGUI_API inline void PushWindowControl(ImGuiApp* app, ImGuiAppWindowBase* window);

  template <typename T>
  IMGUI_API inline void PushSidebarControl(ImGuiApp* app, ImGuiAppSidebarBase* sidebar);

  IMGUI_API void ShowAppLayerDemo(bool* p_open = nullptr);

  // Push every Active (in-range) entry; returns the number pushed -- pop with PopStyleVar/PopStyleColor(count).
  IMGUI_API int PushAppStyleMods(const ImGuiAppStyleModDesc* mods, int count);
  IMGUI_API int PushAppColorMods(const ImGuiAppColorModDesc* mods, int count);

  // Monospace font for the generated-code inspector (space-padded alignment needs fixed width). Register
  // at font-init time; null leaves the inspector on the UI font.
  IMGUI_API void SetAppCodeFont(ImFont* font);
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
	bool Open = true;
  ImGuiWindow* Window = nullptr;
  ImGuiViewport* Viewport = nullptr;
  ImGuiWindowFlags Flags = ImGuiWindowFlags_None;
  ImVector<ImGuiAppControlBase*> Controls;

  // Optional first-use placement (applied with ImGuiCond_FirstUseEver, so saved .ini wins).
  bool   HasInitialPlacement = false;
  ImVec2 InitialPos = ImVec2(0.0f, 0.0f);
  ImVec2 InitialSize = ImVec2(0.0f, 0.0f);
};

struct ImGuiAppSidebarBase : ImGuiAppWindowBase
{
  ImGuiDir DockDir = ImGuiDir_None;
  float    Size = 0.0f;
};

struct ImGuiAppControlBase : ImGuiAppItemBase
{
  // Re-expose the compile-time-erased data identity for live mirrors. Defaults inert; ImGuiAppControl<>
  // overrides from its pack.
  virtual ImGuiID GetControlDataID()                              const { return 0; }
  virtual int     GetControlDependencyIDs(ImGuiID* out, int cap)  const { IM_UNUSED(out); IM_UNUSED(cap); return 0; }
  virtual void    GetControlDataTypeName(char* out, int out_size) const { if (out && out_size > 0) out[0] = 0; }
};

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

struct ImGuiAppWindowLayer : ImGuiAppLayer
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
  ImGuiID ID = 0;
  void* Ptr = nullptr;
  int Size = 0;                       // byte size when the data is snapshottable (trivially copyable); 0 = opaque
  int TempOffset = 0;                 // byte range of the TempData member inside the instance data -- the
  int TempSize = 0;                   // control's per-frame INPUT; [0, TempOffset) is Persist + LastTemp (state)
  void (*Destroy)(void*) = nullptr;
};

// Ring of MaxFrames byte snapshots of every snapshottable storage entry (opaque entries skipped). Layout
// is keyed to GetAppCompositionID and rebuilt (history cleared) when the composition changes.
struct ImGuiAppStateHistory
{
  ImGuiID           CompositionID;   // layout is valid for exactly this composition
  int               FrameSize;       // bytes per snapshot (sum of slot sizes)
  int               MaxFrames;       // ring capacity (default 600 ~ 10s at 60fps)
  int               Count;           // valid snapshots
  int               Head;            // ring write index
  ImVector<ImGuiID> SlotIds;         // storage entry per slot, in StorageEntries order
  ImVector<int>     SlotSizes;
  ImVector<char>    Frames;          // MaxFrames * FrameSize bytes

  ImGuiAppStateHistory() { CompositionID = 0; FrameSize = 0; MaxFrames = 600; Count = 0; Head = 0; }
};

// Input log: per frame, every control's TempData + dt, plus a hash of the resulting state
// (Persist+LastTemp prefix of every instance) so replay can pinpoint the first divergent frame.
struct ImGuiAppInputLog
{
  ImGuiID           CompositionID;   // layout is valid for exactly this composition
  int               FrameSize;       // bytes per frame: sum of temp sizes + sizeof(float) dt
  int               Count;           // recorded frames
  ImVector<ImGuiID> SlotIds;         // storage entry per slot, in StorageEntries order
  ImVector<int>     SlotOffsets;     // TempData offset within each instance
  ImVector<int>     SlotSizes;       // TempData size
  ImVector<char>    Frames;          // Count * FrameSize bytes, appended (caller clears between takes)
  ImVector<ImGuiID> StateHashes;     // per-frame post-update state hash (replay divergence reference)

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
  ImGuiAppPlatform               Platform;
  ImVec4                         ClearColor;
  void*                          PlatformData;
  ImGuiAppWAL*                   WAL;           // optional write-ahead logger (caller-owned); null = silent
  ImGuiAppRecorder*              Recorder;      // active recording (AppRecordBegin registers, AppRecordEnd clears); null = none
  ImGuiAppFrameID                FrameID;       // updated at the top of OnDrawFrame; correlation key for WAL/video/sidecar
  ImGuiAppPacer                  Pacer;         // advisory; consulted by the backend run loop via AppPacerWait
  bool                           Initialized;

  ImGuiApp() : PlatformData(nullptr), WAL(nullptr), Recorder(nullptr), Initialized(false) {}
  virtual ~ImGuiApp();
  int                         Run(int argc, char** argv);
  bool                        Initialize(const ImGuiAppConfig* config);
  bool                        IsInitialized() const { return Initialized; }
  virtual void                Shutdown();
  static void                 DrawFrame(ImGuiApp* app);
  virtual bool                OnInitialize(int argc, char** argv) { return true; }
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
    // Created and registered in ImGuiApp::Data by PushAppControl<>().
    mutable struct InstanceData
    {
      PersistDataT PersistData;
      TempDataT    LastTempData;
      TempDataT    TempData;
    } *_InstanceData;

    // Asserts when this control's instance data or a DataDependency was not pushed before it (both done by PushAppControl<>()).
    template <typename T> inline T* GetData(const ImGuiApp* app) const { T* data = static_cast<T*>(app->Data.GetVoidPtr(ImGuiType<T>::ID)); IM_ASSERT(data); return static_cast<T*>(data); }
    inline std::tuple<DataDependencies*...> GetAllDependencyData(const ImGuiApp* app) const { return { GetData<DataDependencies>(app)... }; }

    virtual void OnInitialize(ImGuiApp*, PersistDataT*, const DataDependencies*...) const override {}
    virtual void OnInitialize(ImGuiApp* app) const override final
    {
      _InstanceData = reinterpret_cast<InstanceData*>(GetData<PersistDataT>(app));
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

  // Dependency ids are the ImGuiType<>::ID values -- the same keys app->Data uses.
  virtual ImGuiID GetControlDataID() const override final { return ImGuiType<PersistDataT>::ID; }

  virtual int GetControlDependencyIDs(ImGuiID* out, int cap) const override final
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

  virtual void GetControlDataTypeName(char* out, int out_size) const override final { GenerateLabel<PersistDataT>(out, (size_t)out_size); }
};

template <typename T>
struct ImGuiAppWindow : ImGuiAppWindowBase
{
  ImGuiAppWindow() { GenerateUniqueLabel<T>(this->Label, sizeof(this->Label)); }

  virtual void OnInitialize(ImGuiApp*)                         const override {};
  virtual void OnShutdown(ImGuiApp*)                           const override {};
  virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*) const override {};
  virtual void OnUpdate(const ImGuiApp* app, float dt)         const override {};
  virtual void OnRender(const ImGuiApp*)                       const override {};
};

template <typename T>
struct ImGuiAppSidebar : ImGuiAppSidebarBase
{
  ImGuiAppSidebar() { GenerateUniqueLabel<T>(this->Label, sizeof(this->Label)); }

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

  template <typename T>
  inline void PushAppWindow(ImGuiApp* app)
  {
    IM_ASSERT(app);
    char name[IM_LABEL_SIZE];
    GenerateLabel<T>(name, sizeof(name));
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push window %s", name);
    T* window = IM_NEW(T)();
    IM_ASSERT(window);
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
  inline void PushAppControl(ImGuiApp* app)
  {
      IM_ASSERT(app);

      char name[IM_LABEL_SIZE];
      GenerateLabel<T>(name, sizeof(name));
      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push control %s", name);

      // Instance data is keyed by the control's data type id so dependents can resolve it.
      ImGuiID id = ImGuiType<typename T::ControlDataType>::ID;

      // One instance per control data type.
      typename T::ControlInstanceDataType* instance_data = static_cast<typename T::ControlInstanceDataType*>(app->Data.GetVoidPtr(id));
      IM_ASSERT(nullptr == instance_data);

      T* control = IM_NEW(T)();
      IM_ASSERT(control);

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
  IMGUI_API inline void PushWindowControl(ImGuiApp* app, ImGuiAppWindowBase* window)
  {
      IM_ASSERT(app && window);

      char name[IM_LABEL_SIZE];
      GenerateLabel<T>(name, sizeof(name));
      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push control %s into window '%s'", name, window->Label);

      // Instance data is keyed by the control's data type id so dependents can resolve it.
      ImGuiID id = ImGuiType<typename T::ControlDataType>::ID;

      // One instance per control data type.
      typename T::ControlInstanceDataType* instance_data = static_cast<typename T::ControlInstanceDataType*>(app->Data.GetVoidPtr(id));
      IM_ASSERT(nullptr == instance_data);

      T* control = IM_NEW(T)();
      IM_ASSERT(control);

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
      window->Controls.push_back(control);
      window->Controls.back()->OnInitialize(app);
  }

  template <typename T>
  IMGUI_API inline void PushSidebarControl(ImGuiApp* app, ImGuiAppSidebarBase* sidebar)
  {
      ImGuiID id;
      T* control;
      typename T::ControlInstanceDataType* instance_data;

      IM_ASSERT(app);

      char name[IM_LABEL_SIZE];
      GenerateLabel<T>(name, sizeof(name));
      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push control %s into sidebar '%s'", name, sidebar ? sidebar->Label : "(null)");

      // Instance data is keyed by the control's data type id so dependents can resolve it.
      id = ImGuiType<typename T::ControlDataType>::ID;

      // One instance per control data type.
      instance_data = static_cast<decltype(instance_data)>(app->Data.GetVoidPtr(id));
      IM_ASSERT(nullptr == instance_data);

      control = IM_NEW(T)();
      IM_ASSERT(control);

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
      sidebar->Controls.push_back(control);
      sidebar->Controls.back()->OnInitialize(app);
  }
}

#ifdef _MSC_VER
#pragma warning (pop)
#endif
