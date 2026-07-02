#pragma once

/*

Index of this file:
// [SECTION] Header mess
// [SECTION] Forward declarations and basic types
// [SECTION] Compile-time helpers (ImGuiType<>)
// [SECTION] Dear ImGui end-user API functions

// [SECTION] Helpers (ImGuiOnceUponAFrame, ImGuiTextFilter, ImGuiTextBuffer, ImGuiStorage, ImGuiListClipper, Math Operators, ImColor)
// [SECTION] Multi-Select API flags and structures (ImGuiMultiSelectFlags, ImGuiMultiSelectIO, ImGuiSelectionRequest, ImGuiSelectionBasicStorage, ImGuiSelectionExternalStorage)
// [SECTION] Font API (ImFontConfig, ImFontGlyph, ImFontGlyphRangesBuilder, ImFontAtlasFlags, ImFontAtlas, ImFontBaked, ImFont)
// [SECTION] Obsolete functions and types

*/

//-----------------------------------------------------------------------------
// [SECTION] Header mess
//-----------------------------------------------------------------------------

#include "imgui.h" 										    // IMGUI_API, ImGuiID, ImGuiStorage, ImBitArray, ImGuiTextIndex, ImChunkStream
#include "imgui_internal.h"               // ImStrncpy
#include "imapp_config.h"

// Version (please keep in sync if you bump it)
#define IMGUI_APPLAYER_VERSION      "0.4.0"
#define IMGUI_APPLAYER_VERSION_NUM  400

#include <mutex>                          // std::call_once
#include <tuple>                          // 
#include <type_traits>                    // 
#include <string_view>                    // 

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 26495)          // [Static Analyzer] Variable 'XXX' is uninitialized. Always initialize a member variable (type.6).
#endif

//-----------------------------------------------------------------------------
// [SECTION] Forward declarations and basic types
//-----------------------------------------------------------------------------

// Forward declarations: ImGuiStatic layer
template <typename T> struct ImGuiStatic; //

// Forward declarations: ImGuiApp layer
struct ImGuiApp;                          //
struct ImGuiAppBase;                      //
struct ImGuiAppLayerBase;                 //
struct ImGuiAppLayer;                     //
struct ImGuiAppTaskLayer;                 //
struct ImGuiAppCommandLayer;              //

// Forward declarations: ImGuiAppControl layer
struct ImGuiAppControlBase;                  //
template <typename PersistDataT, typename TempDataT, typename... DataDependencies>                struct ImGuiInterfaceAdapterBase; //
template <typename Base, typename PersistDataT, typename TempDataT, typename... DataDependencies> struct ImGuiInterfaceAdapter;     //
template <typename PersistDataT, typename TempDataT, typename... DataDependencies> struct ImGuiAppControl;            //

// Forward declarations: ImGuiAppWindow layer
struct ImGuiAppWindowBase;                   //

// Forward declarations: ImGuiAppSidebar layer
struct ImGuiAppSidebarBase;                  //

// Forward declarations: state history + input log (time travel / record-replay)
struct ImGuiAppStateHistory;                 //
struct ImGuiAppInputLog;                     //

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

// Write-ahead logger. Each record is appended AND flushed BEFORE the operation it names executes -- the
// database WAL discipline applied to the app pipeline -- so after a crash the tail of the file names the
// operation that was in flight. Attach one to ImGuiApp::WAL and the framework logs its own events: layer /
// window / sidebar / control push+pop, storage registration, command dispatch, and (at Frame level) every
// per-frame phase begin. No dependency beyond stdio; a null ImGuiApp::WAL costs one branch per site.
typedef int ImGuiAppWALLevel;
enum ImGuiAppWALLevel_
{
  ImGuiAppWALLevel_Off = 0,
  ImGuiAppWALLevel_Lifecycle,   // composition changes, storage, command dispatch
  ImGuiAppWALLevel_Frame,       // + per-frame per-layer phase begins (crash hunts; large files)
};

struct ImGuiAppWAL
{
  void*            File;        // FILE*; typed void* to keep <cstdio> out of this header
  int              Seq;         // monotonic record number
  ImGuiAppWALLevel Level;
  char             Path[256];

  ImGuiAppWAL() { File = nullptr; Seq = 0; Level = ImGuiAppWALLevel_Off; Path[0] = 0; }
};

// Platform backend interface. The core app layer depends only on this vtable; it never
// includes any concrete backend. Exactly one backend translation unit is linked per build
// (selected by CMake), and that backend defines ImGuiApp_GetPlatformBackend().
struct ImGuiAppPlatformBackend
{
  bool (*InitPlatform)(ImGuiApp* app, ImGuiAppConfig& config);
  void (*ShutdownPlatform)(ImGuiApp* app);
  int  (*RunLoop)(ImGuiApp* app);
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

// Event identification, named. OnUpdate receives BOTH this frame's TempData and last frame's precisely so user
// code can derive events from state deltas instead of storing flags: rising = it started, falling = it ended,
// changed = either transition (the demo's `temp_data->hovered ^ last_temp_data->hovered`). These helpers are
// that comparison vocabulary spelled out; the raw operators remain equally idiomatic.
inline static bool ImAppRising (bool now, bool last) { return now && !last; }
inline static bool ImAppFalling(bool now, bool last) { return !now && last; }
inline static bool ImAppChanged(bool now, bool last) { return now ^ last; }
template <typename T>
inline static bool ImAppChanged(const T& now, const T& last) { return !(now == last); }

// Deterministic effects. A hidden effect source (rand(), a clock) breaks the replay theorem: OnUpdate stops
// being a pure function of (state, input, dt). Keep the effect IN the state instead -- a PRNG whose seed
// lives in PersistData is stepped by OnUpdate like any other field, so snapshots capture it and replays
// reproduce it exactly. splitmix64: one u64 of state, solid distribution, no global anywhere. Seed once in
// OnInitialize (an init-time clock read is fine -- it becomes state), then only ever step through the seed.
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

// Best-effort symbolized backtrace of the CALLER, written into out as "  #N name (file:line)" lines. Returns
// the number of characters written. skip_frames drops innermost frames (0 = include the caller of this
// function). Win32: CaptureStackBackTrace + DbgHelp; other platforms currently write an empty string.
IMGUI_API int ImStackTrace(char* out, int out_size, int skip_frames = 0);

// IM_ASSERT sink (wired via IMGUI_USER_CONFIG -> imguix_imconfig.h): logs expr/file/line + ImStackTrace to
// the WAL registered with ImGui::SetAppAssertWAL and to stderr, flushes (write-ahead), then __debugbreak()s
// under a debugger or exits with code 3 -- never the blocking CRT assert popup.
IMGUI_API void ImGuiAppAssertFail(const char* expr, const char* file, int line);

// One authorable style-var override on a window/sidebar/control: which var, the value to push, and a
// runtime-toggleable Active flag. Value is self-describing (Value.x for float vars, both lanes for ImVec2
// vars -- every ImGuiStyleVar is one of the two), unlike ImGuiStyleMod whose union needs GetStyleVarInfo to
// interpret and whose fields carry pop-restore ("backup") semantics.
struct ImGuiAppStyleModDesc
{
  ImGuiStyleVar Var;
  ImVec2        Value;
  bool          Active;

  ImGuiAppStyleModDesc()                                                { Var = 0; Value = ImVec2(0.0f, 0.0f); Active = true; }
  ImGuiAppStyleModDesc(ImGuiStyleVar var, float v, bool active = true)  { Var = var; Value = ImVec2(v, 0.0f); Active = active; }
  ImGuiAppStyleModDesc(ImGuiStyleVar var, ImVec2 v, bool active = true) { Var = var; Value = v; Active = active; }
};

// The color twin: one authorable PushStyleColor override. Value is a packed IM_COL32 RGBA (the palette-constant
// form), converted to/from floats only at the editing boundary.
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
// (Note that ImGui:: being a namespace, you can add extra ImGui:: functions in your own separate file. Please don't modify imgui source files!)
//-----------------------------------------------------------------------------

namespace ImGui
{
  IMGUI_API void InitializeApp(ImGuiApp* app, const ImGuiAppConfig* config = nullptr);
  IMGUI_API void ShutdownApp(ImGuiApp* app);
  IMGUI_API void UpdateApp(ImGuiApp* app);                    // dt = GetIO().DeltaTime
  IMGUI_API void UpdateApp(ImGuiApp* app, float dt);          // explicit dt: the transition function's real signature (replay injects here)
  IMGUI_API void RenderApp(const ImGuiApp* app);
  IMGUI_API void RegisterAppStorage(ImGuiApp* app, ImGuiID id, void* ptr, void (*destroy)(void*));
  IMGUI_API void RegisterAppStorage(ImGuiApp* app, ImGuiID id, void* ptr, int size, void (*destroy)(void*));   // size > 0 => snapshottable
  IMGUI_API void RegisterAppStorage(ImGuiApp* app, ImGuiID id, void* ptr, int size, int temp_offset, int temp_size, void (*destroy)(void*));   // + input (TempData) byte range
  IMGUI_API void UnregisterAppStorage(ImGuiApp* app, ImGuiID id);   // destroys + removes one entry (pop symmetry)
  IMGUI_API void ClearAppStorage(ImGuiApp* app);

  // Time travel. Snapshot appends the app's snapshottable state to the ring (rebuilding the layout and
  // clearing history when the composition changed); Restore copies snapshot `index` (0 = oldest) back into
  // the live app. Both return false when nothing snapshottable exists / index or composition is invalid.
  IMGUI_API bool AppStateSnapshot(ImGuiApp* app, ImGuiAppStateHistory* h);
  IMGUI_API bool AppStateRestore(ImGuiApp* app, ImGuiAppStateHistory* h, int index);
  IMGUI_API void AppStateHistoryClear(ImGuiAppStateHistory* h);

  // Record/replay. AppInputRecord appends this frame's inputs (every control's TempData + dt) and the
  // resulting state hash -- call it once per frame AFTER RenderApp. AppInputReplay re-runs the recorded
  // frames through UpdateApp (no rendering; injected inputs stand in for it) -- restore the starting state
  // first (AppStateRestore or a fresh identical bring-up). If out_first_divergence is non-null it receives
  // the first frame whose state hash differs from the recording (-1 = deterministic reproduction), which
  // names the exact frame a control consulted a hidden effect (rand(), clock, ...) instead of its state.
  IMGUI_API bool AppInputRecord(ImGuiApp* app, ImGuiAppInputLog* log, float dt);
  IMGUI_API bool AppInputReplay(ImGuiApp* app, const ImGuiAppInputLog* log, int* out_first_divergence);
  IMGUI_API void AppInputLogClear(ImGuiAppInputLog* log);

  // Write-ahead log. AppWALWrite appends one record and flushes it to disk BEFORE returning (that ordering is
  // the whole point); records below the WAL's level are dropped. All three are null-safe on wal.
  IMGUI_API bool AppWALOpen(ImGuiAppWAL* wal, const char* path, ImGuiAppWALLevel level);
  IMGUI_API void AppWALClose(ImGuiAppWAL* wal);
  IMGUI_API void AppWALWrite(ImGuiAppWAL* wal, ImGuiAppWALLevel level, const char* fmt, ...) IM_FMTARGS(3);

  // Assert forensics. When IMGUI_USER_CONFIG routes IM_ASSERT to ImGuiAppAssertFail (imguix does), a failed
  // assert writes the expression, location and a symbolized ImStackTrace to this WAL sink + stderr, then
  // breaks into the debugger if one is attached or exits -- never the blocking CRT popup.
  IMGUI_API void SetAppAssertWAL(ImGuiAppWAL* wal);

  // Identity of the app's composition: layers (count), windows/sidebars (labels, in order), controls (data type
  // ids, in update order). Changes exactly when something is pushed or popped, so mirrors of the object model
  // (e.g. the node editor's live graph) can poll it cheaply and reconcile only on change.
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

  // Apply desc arrays to the shared style/color stacks: pushes every Active (in-range) entry, returns the number
  // pushed -- pop with PopStyleVar/PopStyleColor(count). One vocabulary, three users: composed items apply their
  // authored StyleMods/ColorMods through these, and the composer's own chrome palette is defined in the same terms.
  IMGUI_API int PushAppStyleMods(const ImGuiAppStyleModDesc* mods, int count);
  IMGUI_API int PushAppColorMods(const ImGuiAppColorModDesc* mods, int count);

  // Monospace font for the dogfooded editor's generated-code inspector. Space-padded column alignment (e.g. the
  // generated AppCommand enum) only lines up in a fixed-width font; the app's UI font is proportional. The host
  // loads a mono face (e.g. Consolas) at font-init time and registers it here; null leaves the inspector on the UI font.
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
  // Authored style-var/color overrides, applied around the item's submission. The base OnStylePush pushes every
  // Active entry and latches the pushed counts; OnStylePop pops those latched counts, so flipping Active between
  // push and pop cannot unbalance the stacks. Override only for exotic non-desc styling.
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
  // Live-introspection hooks: re-expose the compile-time-erased data identity so a node editor can mirror the
  // running control graph WITHOUT reflection. Defaults inert; ImGuiAppControl<> overrides from its pack.
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

// App state history: byte snapshots of every SNAPSHOTTABLE storage entry (the push helpers register a size
// automatically when a control's instance data is trivially copyable; opaque entries are skipped). Because
// OnUpdate is the sole mutator and all durable state lives in app->Data, restoring a frame's bytes IS time
// travel -- the state discipline pays out as deterministic scrub/replay for ANY framework app, no per-app
// code. A ring of MaxFrames snapshots; the layout is keyed to GetAppCompositionID and rebuilt (history
// cleared) when the composition changes.
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

// Input log: the OTHER half of the determinism theorem. A frame's inputs are exactly the controls' TempData
// (recorded by OnRender) plus dt -- bytes per frame, not kilobytes -- so re-running OnUpdate from one starting
// snapshot with those inputs injected reproduces the whole run. Each frame also stores a hash of the
// resulting STATE (the Persist+LastTemp prefix of every instance), so replay can pinpoint the first frame a
// control diverged: hidden effects (rand(), clocks) become a diagnosed defect instead of silent drift.
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
  bool                           Initialized;

  ImGuiApp() : PlatformData(nullptr), WAL(nullptr), Initialized(false) {}
  virtual ~ImGuiApp();
  int                         Run(int argc, char** argv);
  bool                        Initialize(const ImGuiAppConfig* config);
  bool                        IsInitialized() const { return Initialized; }
  virtual void                Shutdown();
  static void                 DrawFrame(ImGuiApp* app);
  virtual bool                OnInitialize(int argc, char** argv) { return true; }
  virtual void                OnDrawFrame();
  virtual void                OnExecuteCommand(ImGuiAppCommand cmd) override;
  virtual bool                OnInitializePlatform(ImGuiAppConfig& config);
  virtual void                OnShutdownPlatform();
};

template <typename Base, typename PersistDataT, typename TempDataT, typename... DataDependencies>
struct ImGuiInterfaceAdapter : Base, ImGuiInterfaceAdapterBase<PersistDataT, TempDataT, DataDependencies...>
{
    // Instance data for this control, created and stored in ImGuiApp::Data by PushAppControl<>(), and accessible from _InstanceData
    mutable struct InstanceData
    {
      PersistDataT PersistData;
      TempDataT    LastTempData;
      TempDataT    TempData;
    } *_InstanceData;

    // If you assert here, it means that either this control's _InstanceData was not allocated and inserted into ImGuiApp::Data (performed by PushAppControl<>()) or
    // a defined DataDependency was not properly pushed before this control (also performed by PushAppControl<>() for each dependency).
    template <typename T> inline T* GetData(const ImGuiApp* app) const { T* data = static_cast<T*>(app->Data.GetVoidPtr(ImGuiType<T>::ID)); IM_ASSERT(data); return static_cast<T*>(data); }
    inline std::tuple<DataDependencies*...> GetAllDependencyData(const ImGuiApp* app) const { return { GetData<DataDependencies>(app)... }; }

    //
    //
    //
    //
    virtual void OnInitialize(ImGuiApp*, PersistDataT*, const DataDependencies*...) const override {}
    virtual void OnInitialize(ImGuiApp* app) const override final
    {
      _InstanceData = reinterpret_cast<InstanceData*>(GetData<PersistDataT>(app)); // Cache pointer to instance data
      std::apply([=, this](DataDependencies*... dependencies) { OnInitialize(app, &_InstanceData->PersistData, dependencies...); }, GetAllDependencyData(app));
    }

    //
    //
    //
    //
    virtual void OnShutdown(ImGuiApp*, PersistDataT*, const DataDependencies*...) const override {}
    virtual void OnShutdown(ImGuiApp* app) const override final
    {
      std::apply([=, this](DataDependencies*... dependencies) { OnShutdown(app, &_InstanceData->PersistData, dependencies...); }, GetAllDependencyData(app));
    }

    //
    //
    //
    //
    virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*, const PersistDataT*, const TempDataT*, const DataDependencies*...) const override {}
    virtual void OnGetCommand(const ImGuiApp* app, ImGuiAppCommand* cmd) const override final
    {
      std::apply([=, this](DataDependencies*... dependencies) { OnGetCommand(app, cmd, &_InstanceData->PersistData, &_InstanceData->TempData, dependencies...); }, GetAllDependencyData(app));
    }

    //
    //
    //
    //
    virtual void OnUpdate(float, PersistDataT*, const TempDataT*, const TempDataT*, const DataDependencies*...) const override {}
    virtual void OnUpdate(const ImGuiApp* app, float dt) const override final
    {
      std::apply([=, this](DataDependencies*... dependencies) { OnUpdate(dt, &_InstanceData->PersistData, &_InstanceData->TempData, &_InstanceData->LastTempData, dependencies...); }, GetAllDependencyData(app));
      _InstanceData->LastTempData = _InstanceData->TempData;
    }

    //
    //
    //
    //
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

  // Live-introspection overrides: the dependency ids are the per-dependency ImGuiType<>::ID values -- the same
  // keys app->Data is keyed by -- so a mirror can resolve producers by id with no reflection.
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

  // Visit every pushed control -- app-level first, then sidebar-hosted, then window-hosted (update order).
  // visitor(ImGuiAppControlBase* control, ImGuiAppWindowBase* host) with host == nullptr for app-level. This is
  // the ONE enumeration the layers, the command pipeline, and object-model mirrors (the node editor) share, so
  // "all controls" cannot drift between them.
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
        const ImGuiID data_id = control->GetControlDataID();   // the control's instance data dies with it
        IM_DELETE(control);
        if (data_id != 0)
          UnregisterAppStorage(app, data_id);
      }
  }

  template <typename T>
  inline void PushAppLayer(ImGuiApp* app)
  {
      //IM_STATIC_ASSERT((std::is_base_of_v<ImGuiAppLayerBase, T>));
      IM_ASSERT(app);

      char name[IM_LABEL_SIZE];
      GenerateLabel<T>(name, sizeof(name));
      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push layer %s", name);

      app->Layers.push_back(IM_NEW(T)());
      if (app->Layers.back()->Label[0] == 0)   // stamp the type name so per-layer WAL/frame records read well
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

      // Use the control's data type hash for instance data storage/retrieval (so other controls which depend on instance_data->ControlData may access it)
      ImGuiID id = ImGuiType<typename T::ControlDataType>::ID;

      // Ensure we are not pushing a duplicate instance of this control data type
      typename T::ControlInstanceDataType* instance_data = static_cast<typename T::ControlInstanceDataType*>(app->Data.GetVoidPtr(id));
      IM_ASSERT(nullptr == instance_data);

      T* control = IM_NEW(T)();
      IM_ASSERT(control);

      instance_data = IM_NEW(typename T::ControlInstanceDataType)();
      IM_ASSERT(instance_data);

      app->Data.SetVoidPtr(id, instance_data);
      // Trivially-copyable instance data registers its size (byte-snapshot time travel) and its TempData byte
      // range (input recording for replay); anything owning heap (ImVector members etc.) registers opaque.
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
      const ImGuiID data_id = control->GetControlDataID();   // pop symmetry: push registered it, pop frees it,
      IM_DELETE(control);                                    // so the same control type can be pushed again
      if (data_id != 0)
        UnregisterAppStorage(app, data_id);
  }

  // Host a control INSIDE a window: its instance data is registered in app->Data like any control, but it is
  // appended to window->Controls so the WindowLayer renders it between the host window's Begin/End (a
  // window-hosted control submits into the current window, e.g. via BeginChild, rather than its own Begin).
  template <typename T>
  IMGUI_API inline void PushWindowControl(ImGuiApp* app, ImGuiAppWindowBase* window)
  {
      IM_ASSERT(app && window);

      char name[IM_LABEL_SIZE];
      GenerateLabel<T>(name, sizeof(name));
      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push control %s into window '%s'", name, window->Label);

      // Use the control's data type hash for instance data storage/retrieval (so other controls which depend on instance_data->ControlData may access it)
      ImGuiID id = ImGuiType<typename T::ControlDataType>::ID;

      // Ensure we are not pushing a duplicate instance of this control data type
      typename T::ControlInstanceDataType* instance_data = static_cast<typename T::ControlInstanceDataType*>(app->Data.GetVoidPtr(id));
      IM_ASSERT(nullptr == instance_data);

      T* control = IM_NEW(T)();
      IM_ASSERT(control);

      instance_data = IM_NEW(typename T::ControlInstanceDataType)();
      IM_ASSERT(instance_data);

      app->Data.SetVoidPtr(id, instance_data);
      // Trivially-copyable instance data registers its size (byte-snapshot time travel) and its TempData byte
      // range (input recording for replay); anything owning heap (ImVector members etc.) registers opaque.
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

      // Use the control's data type hash for instance data storage/retrieval (so other controls which depend on instance_data->ControlData may access it)
      id = ImGuiType<typename T::ControlDataType>::ID;

      // Ensure we are not pushing a duplicate instance of this control data type
      instance_data = static_cast<decltype(instance_data)>(app->Data.GetVoidPtr(id));
      IM_ASSERT(nullptr == instance_data);

      control = IM_NEW(T)();
      IM_ASSERT(control);

      instance_data = IM_NEW(typename T::ControlInstanceDataType)();
      IM_ASSERT(instance_data);

      app->Data.SetVoidPtr(id, instance_data);
      // Trivially-copyable instance data registers its size (byte-snapshot time travel) and its TempData byte
      // range (input recording for replay); anything owning heap (ImVector members etc.) registers opaque.
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
