// ImGuiAppLayer core: frame pipeline, typed app composition (layers / windows / sidebars / controls),
// state discipline (docs/phase-coherence.md).
//
// Index of this file (search for "[SECTION]"):
// [SECTION] Assert forensics (symbolized backtrace + WAL sink)
// [SECTION] Type schema registry (auto-materialized manifests)
// [SECTION] App shell + core phase layers (Task, Command, Status)
// [SECTION] Style/color mod runtime (desc apply; workbench style system)
// [SECTION] Display layer (windows, sidebars, hosted controls, .ini handler)
// [SECTION] Write-ahead log (ImGuiAppWAL)
// [SECTION] State snapshots + input record/replay (time travel)
// [SECTION] App bring-up (InitializeApp / UpdateApp / RenderApp / storage)

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imguiapp.h"
#include "imguiapp_av.h"   // AppRecordPump (OnEncodeFrame)
#include "imgui_internal.h"
#include "imguix.h"

#include <ctime>
#include <cstdio>
#include <cstdlib>                        // exit (assert sink)
#include <cstring>                        // memcpy (state snapshots)

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>                      // CaptureStackBackTrace, IsDebuggerPresent
#include <dbghelp.h>                      // SymInitialize, SymFromAddr, SymGetLineFromAddr64
#include <avrt.h>                         // MMCSS thread class (pacer wakeup QoS)
#include <intrin.h>                       // __rdtsc (frame id)
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "avrt.lib")
#else
#include <time.h>                         // clock_gettime (pacer/frame-id monotonic clock)
#endif

//-----------------------------------------------------------------------------
// [SECTION] Assert forensics (symbolized backtrace + WAL sink). See imguix_imconfig.h for IM_ASSERT routing.
//-----------------------------------------------------------------------------

static ImGuiAppWAL* g_app_assert_wal = nullptr;

namespace ImGui
{
  IMGUI_API void SetAppAssertWAL(ImGuiAppWAL* wal)
  {
      g_app_assert_wal = wal;
  }
}

IMGUI_API int ImStackTrace(char* out, int out_size, int skip_frames)
{
    if (out == nullptr || out_size <= 0)
      return 0;
    out[0] = 0;
#ifdef _WIN32
    HANDLE proc = GetCurrentProcess();
    static bool sym_ready = false;
    if (!sym_ready)
    {
      SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
      SymInitialize(proc, nullptr, TRUE);
      sym_ready = true;
    }

    void* frames[62];
    const int n = (int)CaptureStackBackTrace((DWORD)(skip_frames + 1), 62, frames, nullptr);

    char sym_buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* sym = (SYMBOL_INFO*)sym_buf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;

    int len = 0;
    for (int i = 0; i < n && len < out_size - 1; i++)
    {
      const DWORD64 addr = (DWORD64)frames[i];
      const char* name = "??";
      if (SymFromAddr(proc, addr, nullptr, sym))
        name = sym->Name;

      IMAGEHLP_LINE64 line;
      line.SizeOfStruct = sizeof(line);
      DWORD col = 0;
      if (SymGetLineFromAddr64(proc, addr, &col, &line))
        len += ImFormatString(out + len, (size_t)(out_size - len), "  #%-2d %s (%s:%d)\n", i, name, line.FileName, (int)line.LineNumber);
      else
        len += ImFormatString(out + len, (size_t)(out_size - len), "  #%-2d %s\n", i, name);
    }
    return len;
#else
    IM_UNUSED(skip_frames);
    return 0;
#endif
}

IMGUI_API void ImGuiAppAssertFail(const char* expr, const char* file, int line)
{
    // Re-entrancy guard: an assert inside the logging path must not recurse.
    static bool in_assert = false;
    if (in_assert)
      exit(3);
    in_assert = true;

    char stack[3072];
    ImStackTrace(stack, IM_ARRAYSIZE(stack), 1);

    ImGui::AppWALWrite(g_app_assert_wal, ImGuiAppWALLevel_Lifecycle, "ASSERT FAILED: (%s) at %s:%d\n%s", expr, file, line, stack);
    fprintf(stderr, "ASSERT FAILED: (%s) at %s:%d\n%s", expr, file, line, stack);
    fflush(stderr);

#ifdef _WIN32
    if (IsDebuggerPresent())
    {
      __debugbreak();
      in_assert = false;   // debugger may continue past the break; let later asserts report too
      return;
    }
#endif
    exit(3);
}

ImGuiApp::~ImGuiApp()
{
    Shutdown();
}

bool ImGuiApp::OnInitializePlatform(ImGuiAppConfig& config)
{
    IM_ASSERT(config.WindowTitle  != nullptr && "WindowTitle must be set in config passed to Initialize");
    IM_ASSERT(config.WindowWidth  >  0       && "WindowWidth must be set in config passed to Initialize");
    IM_ASSERT(config.WindowHeight >  0       && "WindowHeight must be set in config passed to Initialize");

    if (!ImGuiApp_GetPlatformBackend()->InitPlatform(this, config))
    {
        OnShutdownPlatform();
        return false;
    }
    return true;
}

void ImGuiApp::OnShutdownPlatform()
{
    // ImGuiX shutdown must precede backend teardown: the registered backend Shutdown hook
    // still needs platform/renderer resources alive.
    if (ImGuiX::GetCurrentContext() != nullptr)
    {
        ImGuiX::Shutdown();
        ImGuiX::DestroyContext();
    }

    ImGuiApp_GetPlatformBackend()->ShutdownPlatform(this);
    PlatformData = nullptr;
}

int ImGuiApp::Run(int argc, char** argv)
{
    if (!OnInitialize(argc, argv))
    {
        Shutdown();
        return 1;
    }

    return ImGuiApp_GetPlatformBackend()->RunLoop(this);
}

// Pacer bookkeeping the ImGuiAppPacer struct doesn't carry. Single slot: one paced app
// per process; the deadline chain re-anchors when a different app starts pacing.
struct ImGuiAppPacerState
{
  const ImGuiApp* App;
  double          NextDeadline; // on the monotonic app clock; < 0 = chain not started
  double          LastEnter;    // previous AppPacerWait entry (feeds LastFrameMs)
};
static ImGuiAppPacerState s_app_pacer = { nullptr, -1.0, -1.0 };

#ifdef _WIN32
// High-resolution waitable timer (Win10 1803+): sub-millisecond waits with no change to
// global scheduler resolution (never timeBeginPeriod: system-wide + power-hostile).
// Creation failure (older Windows) falls back to a coarse Sleep loop; the spin phase
// still lands the deadline exactly, just spinning longer.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
static HANDLE s_app_pacer_timer = nullptr;
static bool   s_app_pacer_timer_failed = false;
// MMCSS "Games" registration for the paced thread: scheduling QoS once the timed wait
// expires (the timer decides WHEN the thread is ready; MMCSS makes the scheduler run it
// promptly under contention). Best-effort; null = unregistered.
static HANDLE s_app_pacer_mmcss = nullptr;
static bool   s_app_pacer_mmcss_failed = false;
#endif

static void AppPacerShutdownTimer()
{
#ifdef _WIN32
    if (s_app_pacer_timer != nullptr)
    {
        ::CloseHandle(s_app_pacer_timer);
        s_app_pacer_timer = nullptr;
    }
    s_app_pacer_timer_failed = false;
    if (s_app_pacer_mmcss != nullptr)
    {
        ::AvRevertMmThreadCharacteristics(s_app_pacer_mmcss);
        s_app_pacer_mmcss = nullptr;
    }
    s_app_pacer_mmcss_failed = false;
#endif
}

static float AppPacerPrimaryRefreshHz()
{
#ifdef _WIN32
    DEVMODEA dm = {};
    dm.dmSize = sizeof(dm);
    if (::EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
        return (float)dm.dmDisplayFrequency;
#endif
    return 60.0f;
}

// Per-viewport present deadlines (secondary platform windows; main viewport never skips).
struct ImGuiAppViewportPace
{
  ImGuiID ViewportId;
  double  NextDeadline;
  ImU64   LastSeenFrame; // FrameID.FrameIndex; feeds lazy pruning of vanished viewports
};
static ImVector<ImGuiAppViewportPace> s_app_vp_pace;

// Refresh rate of the monitor hosting a viewport. ImGuiPlatformMonitor carries no
// refresh field, so win32 resolves it from the HMONITOR the platform backend stored in
// PlatformHandle; cached per monitor (stale after a display-mode change until restart).
static float AppPacerViewportRefreshHz(const ImGuiViewport* viewport)
{
#ifdef _WIN32
    struct CachedHz
    {
      void* Handle;
      float Hz;
    };
    static ImVector<CachedHz> s_monitor_hz;

    const ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    const short monitor_idx = ((const ImGuiViewportP*)viewport)->PlatformMonitor;   // internal field; public ImGuiViewport has no monitor index
    void* hmon = nullptr;
    if (monitor_idx >= 0 && monitor_idx < pio.Monitors.Size)
        hmon = pio.Monitors[monitor_idx].PlatformHandle;
    if (hmon == nullptr)
        return AppPacerPrimaryRefreshHz();

    for (int i = 0; i < s_monitor_hz.Size; i++)
        if (s_monitor_hz.Data[i].Handle == hmon)
            return s_monitor_hz.Data[i].Hz;

    float hz = 60.0f;
    MONITORINFOEXA mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfoA((HMONITOR)hmon, &mi))
    {
        DEVMODEA dm = {};
        dm.dmSize = sizeof(dm);
        if (::EnumDisplaySettingsExA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm, 0) && dm.dmDisplayFrequency > 1)
            hz = (float)dm.dmDisplayFrequency;
    }
    CachedHz cached;
    cached.Handle = hmon;
    cached.Hz = hz;
    s_monitor_hz.push_back(cached);
    return hz;
#else
    IM_UNUSED(viewport);
    return 60.0f;
#endif
}

bool ImGuiApp::Initialize(const ImGuiAppConfig* config)
{
    IM_ASSERT(config != nullptr);

    if (Initialized)
        Shutdown();

    ShutdownPending = false;

    ImGuiAppConfig cfg = *config;

    if (!OnInitializePlatform(cfg))
        return false;

    ImGui::InitializeApp(this, &cfg);
    Initialized = true;
    return true;
}

void ImGuiApp::Shutdown()
{
    if (!Initialized && PlatformData == nullptr && Layers.empty() && Windows.empty() && Sidebars.empty() && StorageEntries.empty())
        return;

    // One paced app per process by design: the timer/MMCSS teardown is unconditional, so
    // another live app's pacing hiccups for one frame and self-heals on its next wait.
    // AvRevertMmThreadCharacteristics is only valid on the thread that registered --
    // Shutdown must run on the paced (main) thread.
    if (s_app_pacer.App == this)
        s_app_pacer = { nullptr, -1.0, -1.0 };
    AppPacerShutdownTimer();

    ImGui::ShutdownApp(this);
    OnShutdownPlatform();

    Platform        = ImGuiAppPlatform();
    Initialized     = false;
    ShutdownPending = false;
}

// Monotonic wall clock in seconds; self-contained (must work without an ImGui context).
static double AppClockNowSec()
{
#ifdef _WIN32
    static LARGE_INTEGER s_freq = {};
    if (s_freq.QuadPart == 0)
        ::QueryPerformanceFrequency(&s_freq);
    LARGE_INTEGER now;
    ::QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)s_freq.QuadPart;
#else
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

static ImU64 AppClockTsc()
{
#if defined(_WIN32) || defined(__x86_64__) || defined(__i386__)
    return (ImU64)__rdtsc();
#else
    return (ImU64)(AppClockNowSec() * 1e9);
#endif
}

// Process-wide run epoch: FrameID.TimeSec is seconds since the first stamped frame of
// any app in the process, so multiple apps share one timeline.
static double s_app_run_epoch = -1.0;

void ImGuiApp::Frame()
{
    OnDrawFrame();
    OnRenderFrame();
    OnEncodeFrame();
    OnPresentFrame();
}

void ImGuiApp::OnDrawFrame()
{
    // Frame identity first: everything this frame emits (WAL lines, captured video
    // frames, sidecar records) correlates through this id.
    if (s_app_run_epoch < 0.0)
        s_app_run_epoch = AppClockNowSec();
    FrameID.FrameIndex++;
    FrameID.Tsc = AppClockTsc();
    FrameID.TimeSec = AppClockNowSec() - s_app_run_epoch;

    ImGuiX::BeginFrame();
    DrawFrame(this);
}

void ImGuiApp::OnRenderFrame()
{
    ImGuiAppFrameConfig frame_config;
    frame_config.ClearColor = ClearColor;
    ImGuiX::EndFrame(&frame_config);
}

void ImGuiApp::OnEncodeFrame()
{
    // Runs between render and present: CaptureFrame reads the frame just rendered
    // (double-buffered staging returns frame N-1; the FrameID travels with the pixels).
    if (Recorder != nullptr)
        ImGui::AppRecordPump(Recorder);
}

void ImGuiApp::OnPresentFrame()
{
    ImGuiAppFrameConfig frame_config;
    frame_config.ClearColor = ClearColor;
    ImGuiX::PresentFrame(&frame_config);
}

void ImGuiApp::OnExecuteCommand(ImGuiAppCommand cmd)
{
    if (cmd == ImGuiAppCommand_Shutdown)
        ShutdownPending = true;
}

void ImGuiApp::DrawFrame(ImGuiApp* app)
{
    IM_ASSERT(app != nullptr);
    if (app == nullptr)
        return;

    // Fixed pacing feeds the exact timestep through the explicit-dt overload -- the
    // same injection seam replay uses -- so io.DeltaTime (set by the platform backend)
    // never leaks wall-clock jitter into a deterministic run.
    if (app->Pacer.Mode == ImGuiAppPacerMode_Fixed && app->Pacer.TargetHz > 0.0f)
        ImGui::UpdateApp(app, 1.0f / app->Pacer.TargetHz);
    else
        ImGui::UpdateApp(app);
    ImGui::RenderApp(app);
}

//-----------------------------------------------------------------------------
// [SECTION] Type schema registry (auto-materialized manifests; see imguiapp.h)
//-----------------------------------------------------------------------------

// Function-local static: registration can run from static initializers and from control
// construction across TUs, so the registry must not itself race static-init order.
static ImVector<const ImGuiAppTypeSchema*>* AppTypeSchemas()
{
    static ImVector<const ImGuiAppTypeSchema*> schemas;
    return &schemas;
}

void ImGuiAppRegisterTypeSchema(const ImGuiAppTypeSchema* schema)
{
    // Count may still be 0 here: ImGuiAppEnsureTypeRegistered registers the entry before
    // filling its fields so cyclic member reachability terminates.
    IM_ASSERT(schema != nullptr && schema->TypeName != nullptr && schema->Fields != nullptr);
    IM_ASSERT(ImGuiAppFindTypeSchema(schema->TypeName) == nullptr);   // one schema per display name
    AppTypeSchemas()->push_back(schema);
}

const ImGuiAppTypeSchema* ImGuiAppFindTypeSchema(const char* type_name)
{
    if (type_name == nullptr || type_name[0] == 0)
        return nullptr;
    ImVector<const ImGuiAppTypeSchema*>* schemas = AppTypeSchemas();
    for (int i = 0; i < schemas->Size; i++)
        if (strcmp(schemas->Data[i]->TypeName, type_name) == 0)
            return schemas->Data[i];
    return nullptr;
}

//-----------------------------------------------------------------------------
// [SECTION] App shell + core phase layers (Task, Command, Status)
//-----------------------------------------------------------------------------

void ImGuiAppTaskLayer::OnAttach(ImGuiApp* app) const
{
    IM_UNUSED(app);
}

void ImGuiAppTaskLayer::OnDetach(ImGuiApp* app) const
{
    IM_UNUSED(app);
}

void ImGuiAppTaskLayer::OnUpdate(ImGuiApp* app, float dt) const
{
    // OnUpdate consumes the TempData recorded by last frame's OnRender and mutates PersistData.
    // Runs before the Command layer collects OnGetCommand, so state updated this frame can emit
    // a command the same frame. Dependency order: every producer updates before its consumers,
    // so a dependent reads THIS frame's dependency data regardless of hosting order.
    const ImVector<ImGuiAppControlBase*>* order = ImGui::AppRebuildUpdateOrder(app);
    for (int i = 0; i < order->Size; i++)
        order->Data[i]->OnUpdate(app, dt);
}

void ImGuiAppTaskLayer::OnRender(const ImGuiApp* app) const
{
    IM_UNUSED(app);
}

void ImGuiAppCommandLayer::OnAttach(ImGuiApp* app) const
{
    IM_UNUSED(app);
}

void ImGuiAppCommandLayer::OnDetach(ImGuiApp* app) const
{
    IM_UNUSED(app);
}

void ImGuiAppCommandLayer::OnUpdate(ImGuiApp* app, float dt) const
{
    IM_UNUSED(dt);

    // At most one command per control; each distinct command dispatched once, in first-emission order.
    // Dedup scans linearly: apps extend the enum past ImGuiAppCommand_COUNT, so a COUNT-sized
    // bit array cannot represent user commands.
    ImVector<ImGuiAppCommand> cmds;
    ImGui::ForEachAppControl(app, [app, &cmds](ImGuiAppControlBase* control, ImGuiAppWindowBase* host)
    {
        IM_UNUSED(host);
        ImGuiAppCommand cmd = ImGuiAppCommand_None;
        control->OnGetCommand(app, &cmd);
        if (cmd == ImGuiAppCommand_None)
            return;
        for (int i = 0; i < cmds.Size; i++)
            if (cmds.Data[i] == cmd)
                return;
        cmds.push_back(cmd);
    });

    for (int i = 0; i < cmds.Size; i++)
    {
        ImGui::AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "execute command %d", (int)cmds.Data[i]);
        app->OnExecuteCommand(cmds.Data[i]);
    }
}

void ImGuiAppCommandLayer::OnRender(const ImGuiApp* app) const
{
    IM_UNUSED(app);
}

void ImGuiAppStatusLayer::OnAttach(ImGuiApp* app) const
{
    IM_UNUSED(app);
}

void ImGuiAppStatusLayer::OnDetach(ImGuiApp* app) const
{
    IM_UNUSED(app);
}

void ImGuiAppStatusLayer::OnUpdate(ImGuiApp* app, float dt) const
{
    IM_UNUSED(app);
    IM_UNUSED(dt);
}

void ImGuiAppStatusLayer::OnRender(const ImGuiApp* app) const
{
    IM_ASSERT(app != nullptr);
    if (app == nullptr)
        return;

    const ImGuiIO& io = ImGui::GetIO();
    const char* app_platform = app->Platform.Name != nullptr ? app->Platform.Name : "unknown";
    const char* imgui_platform = io.BackendPlatformName != nullptr ? io.BackendPlatformName : "unknown";
    const char* renderer = io.BackendRendererName != nullptr ? io.BackendRendererName : "unknown";

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 padding = ImVec2(ImGui::GetFontSize() * 0.5f, ImGui::GetFontSize() * 0.5f);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(viewport->WorkPos + ImVec2(padding.x, viewport->WorkSize.y - padding.y), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.65f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoInputs;

    if (ImGui::Begin("AppLayerStatus", nullptr, flags))
    {
        ImGui::Text("App: %s", app_platform);
        ImGui::Text("Platform: %s", imgui_platform);
        ImGui::Text("Renderer: %s", renderer);
    }
    ImGui::End();
}

namespace
{
  static void AppDisplayLayerSettingsHandler_ClearAll(ImGuiContext*, ImGuiSettingsHandler*)
  {
  }

  static void AppDisplayLayerSettingsHandler_ReadInit(ImGuiContext*, ImGuiSettingsHandler*)
  {
  }

  static void* AppDisplayLayerSettingsHandler_ReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* name)
  {
    return nullptr;
  }

  static void AppDisplayLayerSettingsHandler_ReadLine(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line)
  {
  }

  static void AppDisplayLayerSettingsHandler_ApplyAll(ImGuiContext*, ImGuiSettingsHandler*)
  {
  }

  static void AppDisplayLayerSettingsHandler_WriteAll(ImGuiContext*, ImGuiSettingsHandler*, ImGuiTextBuffer*)
  {
  }
}

//-----------------------------------------------------------------------------
// [SECTION] Style/color mod runtime (desc apply; workbench style system)
//-----------------------------------------------------------------------------

int ImGui::PushAppStyleMods(const ImGuiAppStyleModDesc* mods, int count)
{
    int pushed = 0;
    for (int i = 0; i < count; i++)
    {
      const ImGuiAppStyleModDesc& mod = mods[i];
      if (!mod.Active || mod.Var < 0 || mod.Var >= ImGuiStyleVar_COUNT)
        continue;
      const ImGuiStyleVarInfo* info = ImGui::GetStyleVarInfo(mod.Var);
      if (info == nullptr || info->DataType != ImGuiDataType_Float)
        continue;
      if (info->Count == 2)
        ImGui::PushStyleVar(mod.Var, mod.Value);
      else
        ImGui::PushStyleVar(mod.Var, mod.Value.x);
      pushed++;
    }
    return pushed;
}

int ImGui::PushAppColorMods(const ImGuiAppColorModDesc* mods, int count)
{
    int pushed = 0;
    for (int i = 0; i < count; i++)
    {
      const ImGuiAppColorModDesc& mod = mods[i];
      if (!mod.Active || mod.Col < 0 || mod.Col >= ImGuiCol_COUNT)
        continue;
      ImGui::PushStyleColor(mod.Col, mod.Value);
      pushed++;
    }
    return pushed;
}

void ImGuiAppItemBase::OnStylePush(const ImGuiApp* app) const
{
    IM_UNUSED(app);

    _StylePushCount = ImGui::PushAppStyleMods(StyleMods.Data, StyleMods.Size);
    _ColorPushCount = ImGui::PushAppColorMods(ColorMods.Data, ColorMods.Size);
}

void ImGuiAppItemBase::OnStylePop(const ImGuiApp* app) const
{
    IM_UNUSED(app);

    if (_StylePushCount > 0)
      ImGui::PopStyleVar(_StylePushCount);
    if (_ColorPushCount > 0)
      ImGui::PopStyleColor(_ColorPushCount);
    _StylePushCount = 0;
    _ColorPushCount = 0;
}

//-----------------------------------------------------------------------------
// [SECTION] Display layer (windows, sidebars, hosted controls, .ini handler)
//-----------------------------------------------------------------------------

void ImGuiAppLayoutLayer::OnAttach(ImGuiApp* app) const
{
    IM_UNUSED(app);
}

void ImGuiAppLayoutLayer::OnDetach(ImGuiApp* app) const
{
    IM_UNUSED(app);
}

void ImGuiAppLayoutLayer::OnUpdate(ImGuiApp* app, float dt) const
{
    IM_UNUSED(dt);
    IM_ASSERT(app != nullptr);
    if (app == nullptr)
        return;
    app->OnLayout();
}

void ImGuiAppLayoutLayer::OnRender(const ImGuiApp* app) const
{
    IM_UNUSED(app);
}

void ImGuiAppDisplayLayer::OnAttach(ImGuiApp* app) const
{
    IM_UNUSED(app);

    if (ImGui::FindSettingsHandler("AppDisplayLayer") != nullptr)
        return;

    ImGuiSettingsHandler ini_handler;
    ini_handler.TypeName = "AppDisplayLayer";
    ini_handler.TypeHash = ImHashStr("AppDisplayLayer");
    ini_handler.ClearAllFn = AppDisplayLayerSettingsHandler_ClearAll;
    ini_handler.ReadInitFn = AppDisplayLayerSettingsHandler_ReadInit;
    ini_handler.ReadOpenFn = AppDisplayLayerSettingsHandler_ReadOpen;
    ini_handler.ReadLineFn = AppDisplayLayerSettingsHandler_ReadLine;
    ini_handler.ApplyAllFn = AppDisplayLayerSettingsHandler_ApplyAll;
    ini_handler.WriteAllFn = AppDisplayLayerSettingsHandler_WriteAll;
    ImGui::AddSettingsHandler(&ini_handler);
}

void ImGuiAppDisplayLayer::OnDetach(ImGuiApp* app) const
{
    IM_UNUSED(app);
}

void ImGuiAppDisplayLayer::OnUpdate(ImGuiApp* app, float dt) const
{
    // Hosted controls update in the Task layer; hosts here always see this frame's control state.
    for (auto& sidebar : app->Sidebars)
      sidebar->OnUpdate(app, dt);

    for (auto& window : app->Windows)
      window->OnUpdate(app, dt);
}

void ImGuiAppDisplayLayer::OnRender(const ImGuiApp* app) const
{
    for (auto& sidebar : app->Sidebars)
    {
      sidebar->OnStylePush(app);

      if (sidebar->Window && (sidebar->Flags & ImGuiWindowFlags_AlwaysAutoResize))
      {
        const bool horizontal = (ImGuiDir_Left == sidebar->DockDir) || (ImGuiDir_Right == sidebar->DockDir);
        const int  idx = horizontal ? 0 : 1;
        float ideal = sidebar->Window->ContentSizeIdeal[idx] + (2.0f * ImGui::GetStyle().WindowPadding[idx]);

        // Wrapped/auto-width content reports a collapsed ideal width (it wraps to the current
        // bar width); clamp to a font-scaled minimum.
        if (horizontal)
          ideal = ImMax(ideal, ImGui::GetFontSize() * 8.0f);

        sidebar->Size = ideal;
      }

      if (ImGui::BeginViewportSideBar(sidebar->Label, sidebar->Viewport, sidebar->DockDir, sidebar->Size, sidebar->Flags))
      {
        sidebar->Open = true;
        sidebar->Window = ImGui::GetCurrentWindow();
        sidebar->OnRender(app);
      }
      else
      {
        sidebar->Open = false;
			}
      ImGui::End();

      sidebar->OnStylePop(app);

      // Controls render their own windows; submit them outside the sidebar's Begin/End.
      for (auto& control : sidebar->Controls)
      {
        control->OnStylePush(app);
        control->OnRender(app);
        control->OnStylePop(app);
      }
    }

    for (auto& window : app->Windows)
    {
      window->OnStylePush(app);

      // Never fight a dock binding: SetNextWindowPos undocks a docked window by design
      // (BeginDocked's PosUndock), so placement only applies to windows with no dock home
      // (live window or saved settings).
      bool dock_bound = false;
      if (window->HasInitialPlacement)
      {
        if (const ImGuiWindow* iw = ImGui::FindWindowByName(window->Label))
          dock_bound = iw->DockId != 0;
        else if (const ImGuiWindowSettings* ws = ImGui::FindWindowSettingsByID(ImHashStr(window->Label)))
          dock_bound = ws->DockId != 0;
      }
      if (window->HasInitialPlacement && !dock_bound)
      {
        if (window->InitialSize.x > 0.0f && window->InitialSize.y > 0.0f)
          ImGui::SetNextWindowSize(window->InitialSize, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(window->InitialPos, ImGuiCond_FirstUseEver);
      }

      if (ImGui::Begin(window->Label, &window->Open, window->Flags))
      {
        window->Window = ImGui::GetCurrentWindow();
        window->OnRender(app);

        // Hosted controls render INSIDE the host window (child regions, not their own Begin/End).
        // Style mods bracket OnRender only: they style the control's region but not its popups.
        for (auto& control : window->Controls)
        {
          control->OnStylePush(app);
          control->OnRender(app);
          control->OnStylePop(app);
        }
      }
      ImGui::End();

      window->OnStylePop(app);
    }

    for (auto& control : app->Controls)
    {
      control->OnStylePush(app);
      control->OnRender(app);
      control->OnStylePop(app);
    }
}

namespace ImGui
{
  //-----------------------------------------------------------------------------
  // [SECTION] Write-ahead log (ImGuiAppWAL)
  //
  // Contract: a record is on disk BEFORE the operation it names runs; fflush on every record.
  // Lifecycle level = composition changes only; Frame level = per-frame records.
  //-----------------------------------------------------------------------------

  IMGUI_API float AppPacerResolveHz(const ImGuiApp* app)
  {
      IM_ASSERT(app != nullptr);
      if (app == nullptr)
        return 60.0f;
      return app->Pacer.TargetHz > 0.0f ? app->Pacer.TargetHz : AppPacerPrimaryRefreshHz();
  }

  IMGUI_API void AppPacerWait(ImGuiApp* app)
  {
      IM_ASSERT(app != nullptr);
      if (app == nullptr || app->Pacer.Mode == ImGuiAppPacerMode_Off)
        return;

      ImGuiAppPacer* p = &app->Pacer;
      float hz = p->TargetHz;
      if (hz <= 0.0f)
      {
        hz = AppPacerPrimaryRefreshHz();
        // Fixed mode's dt injection (DrawFrame) reads TargetHz; resolve it ONCE so the
        // deterministic timestep exists and never re-tracks a monitor change mid-run.
        if (p->Mode == ImGuiAppPacerMode_Fixed)
          p->TargetHz = hz;
      }
      const double period = 1.0 / (double)hz;

      const double now = AppClockNowSec();
      if (s_app_pacer.App != app || s_app_pacer.NextDeadline < 0.0)
      {
        // First paced frame (or the paced app changed): establish the deadline chain, no wait.
        s_app_pacer.App = app;
        s_app_pacer.NextDeadline = now + period;
        s_app_pacer.LastEnter = now;
        return;
      }

      p->LastFrameMs = (now - s_app_pacer.LastEnter) * 1000.0;
      s_app_pacer.LastEnter = now;

      // Deadline chain (previous deadline + period), never now + period: chaining absorbs
      // jitter without drifting. Arriving late is a miss; re-anchor so one long frame
      // doesn't cascade into a chase.
      double deadline = s_app_pacer.NextDeadline;
      if (now > deadline)
      {
        p->MissedDeadlines++;
        deadline = now;
      }

      // Wait to deadline - slack without touching global scheduler state, spin the
      // remainder on the monotonic clock to land the deadline exactly.
      const double slack = (double)p->SleepSlackMs * 0.001;
      const double sleep_until = deadline - slack;
      double t = now;
#ifdef _WIN32
      if (s_app_pacer_timer == nullptr && !s_app_pacer_timer_failed)
      {
        s_app_pacer_timer = ::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (s_app_pacer_timer == nullptr)
          s_app_pacer_timer_failed = true;
      }
      if (s_app_pacer_mmcss == nullptr && !s_app_pacer_mmcss_failed)
      {
        DWORD mmcss_task_index = 0;
        s_app_pacer_mmcss = ::AvSetMmThreadCharacteristicsW(L"Games", &mmcss_task_index);
        if (s_app_pacer_mmcss == nullptr)
          s_app_pacer_mmcss_failed = true;
      }
      if (s_app_pacer_timer != nullptr && t < sleep_until)
      {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)((sleep_until - t) * 1e7);   // negative = relative, 100ns units
        if (due.QuadPart < 0 && ::SetWaitableTimer(s_app_pacer_timer, &due, 0, nullptr, nullptr, FALSE))
          ::WaitForSingleObject(s_app_pacer_timer, INFINITE);
        t = AppClockNowSec();
      }
      while (t < sleep_until)   // fallback (timer unavailable) or residual: coarse sleep
      {
        ::Sleep(1);
        t = AppClockNowSec();
      }
#else
      while (t < sleep_until)
      {
        timespec req = { 0, 1000000 };
        nanosleep(&req, nullptr);
        t = AppClockNowSec();
      }
#endif
      while (t < deadline)
        t = AppClockNowSec();

      p->LastWaitMs = (t - now) * 1000.0;
      s_app_pacer.NextDeadline = deadline + period;
  }

  IMGUI_API bool AppPacerViewportShouldPresent(ImGuiApp* app, ImGuiViewport* viewport)
  {
      if (app == nullptr || viewport == nullptr)
        return true;
      if (app->Pacer.Mode == ImGuiAppPacerMode_Off)
        return true;
      if (viewport == GetMainViewport())
        return true;   // the run loop's AppPacerWait already paces the main viewport

      const float hz = AppPacerViewportRefreshHz(viewport);
      const double period = 1.0 / (double)(hz > 1.0f ? hz : 60.0f);
      const double now = AppClockNowSec();

      ImGuiAppViewportPace* pace = nullptr;
      for (int i = 0; i < s_app_vp_pace.Size; i++)
        if (s_app_vp_pace.Data[i].ViewportId == viewport->ID)
        {
          pace = &s_app_vp_pace.Data[i];
          break;
        }
      if (pace == nullptr)
      {
        // Lazy prune: entries unseen for ~10s at 60fps are vanished viewports.
        if (s_app_vp_pace.Size > 8)
          for (int i = s_app_vp_pace.Size - 1; i >= 0; i--)
            if (s_app_vp_pace.Data[i].LastSeenFrame + 600 < app->FrameID.FrameIndex)
              s_app_vp_pace.erase(s_app_vp_pace.begin() + i);
        ImGuiAppViewportPace fresh;
        fresh.ViewportId = viewport->ID;
        fresh.NextDeadline = now + period;
        fresh.LastSeenFrame = app->FrameID.FrameIndex;
        s_app_vp_pace.push_back(fresh);
        return true;   // first sighting presents and starts the deadline chain
      }

      pace->LastSeenFrame = app->FrameID.FrameIndex;
      if (now + 1e-6 < pace->NextDeadline)
        return false;

      // Deadline chain like AppPacerWait: advance by whole periods; re-anchor when badly
      // late so a stall doesn't cascade into a burst of presents.
      pace->NextDeadline += period;
      if (now - pace->NextDeadline > 4.0 * period)
        pace->NextDeadline = now + period;
      return true;
  }

  IMGUI_API bool AppWALOpen(ImGuiAppWAL* wal, const char* path, ImGuiAppWALLevel level)
  {
      IM_ASSERT(wal != nullptr && path != nullptr);
      if (wal == nullptr || path == nullptr)
        return false;

      AppWALClose(wal);
      FILE* f = fopen(path, "wt");
      if (f == nullptr)
        return false;
      wal->File = f;
      wal->Seq = 0;
      wal->Level = level;
      ImStrncpy(wal->Path, path, IM_ARRAYSIZE(wal->Path));
      AppWALWrite(wal, ImGuiAppWALLevel_Lifecycle, "wal open (level %d)", (int)level);
      return true;
  }

  IMGUI_API void AppWALClose(ImGuiAppWAL* wal)
  {
      if (wal == nullptr || wal->File == nullptr)
        return;
      AppWALWrite(wal, ImGuiAppWALLevel_Lifecycle, "wal close");
      fclose((FILE*)wal->File);
      wal->File = nullptr;
      wal->Level = ImGuiAppWALLevel_Off;
  }

  IMGUI_API void AppWALWrite(ImGuiAppWAL* wal, ImGuiAppWALLevel level, const char* fmt, ...)
  {
      if (wal == nullptr || wal->File == nullptr || level > wal->Level)
        return;

      char msg[512];
      va_list args;
      va_start(args, fmt);
      ImFormatStringV(msg, IM_ARRAYSIZE(msg), fmt, args);
      va_end(args);

      // WAL must also work before/after the ImGui context's lifetime.
      const int frame = ImGui::GetCurrentContext() != nullptr ? ImGui::GetFrameCount() : -1;
      FILE* f = (FILE*)wal->File;
      if (wal->FrameID != nullptr)
        fprintf(f, "[%06d f%05d] [tick:%llu tsc:%llu] %s\n", wal->Seq++, frame,
                (unsigned long long)wal->FrameID->FrameIndex, (unsigned long long)wal->FrameID->Tsc, msg);
      else
        fprintf(f, "[%06d f%05d] %s\n", wal->Seq++, frame, msg);
      fflush(f);   // write-ahead guarantee
  }

  IMGUI_API void RegisterAppStorage(ImGuiApp* app, ImGuiID id, void* ptr, void (*destroy)(void*))
  {
      RegisterAppStorage(app, id, ptr, 0, 0, 0, destroy);   // opaque: not snapshottable, no input range
  }

  IMGUI_API void RegisterAppStorage(ImGuiApp* app, ImGuiID id, void* ptr, int size, void (*destroy)(void*))
  {
      RegisterAppStorage(app, id, ptr, size, 0, 0, destroy);
  }

  IMGUI_API void RegisterAppStorage(ImGuiApp* app, ImGuiID id, void* ptr, int size, int temp_offset, int temp_size, void (*destroy)(void*))
  {
      IM_ASSERT(app);
      IM_ASSERT(id != 0);
      IM_ASSERT(ptr != nullptr);
      IM_ASSERT(destroy != nullptr);

      if (app == nullptr || id == 0 || ptr == nullptr)
        return;

      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "register storage 0x%08X (%d bytes, temp %d+%d)", (unsigned)id, size, temp_offset, temp_size);

      for (const ImGuiAppStorageEntry& entry : app->StorageEntries)
      {
        IM_ASSERT(entry.ID != id && "ImGuiApp storage entry already registered.");
        if (entry.ID == id)
          return;
      }

      app->CompositionRevision++;

      ImGuiAppStorageEntry entry;
      entry.ID = id;
      entry.Ptr = ptr;
      entry.Size = size;
      entry.TempOffset = temp_offset;
      entry.TempSize = temp_size;
      entry.Destroy = destroy;
      app->StorageEntries.push_back(entry);
  }

  IMGUI_API void AppStateHistoryClear(ImGuiAppStateHistory* h)
  {
      IM_ASSERT(h != nullptr);
      if (h == nullptr)
        return;
      h->CompositionID = 0;
      h->FrameSize = 0;
      h->Count = 0;
      h->Head = 0;
      h->SlotIds.clear();
      h->SlotSizes.clear();
      h->Frames.clear();
  }

  //-----------------------------------------------------------------------------
  // [SECTION] State snapshots + input record/replay (time travel)
  //
  // OnUpdate is the sole state mutator and all durable state lives in registered storage,
  // so a byte copy of the snapshottable entries IS the app's state at that frame.
  //-----------------------------------------------------------------------------

  IMGUI_API bool AppStateSnapshot(ImGuiApp* app, ImGuiAppStateHistory* h)
  {
      IM_ASSERT(app != nullptr && h != nullptr);
      if (app == nullptr || h == nullptr || h->MaxFrames <= 0)
        return false;

      // A snapshot is only valid against the composition it was taken from; rebuild the slot
      // layout and restart the timeline on change.
      const ImGuiID comp = GetAppCompositionID(app);
      if (comp != h->CompositionID || h->SlotIds.Size == 0)
      {
        AppStateHistoryClear(h);
        h->CompositionID = comp;
        for (int i = 0; i < app->StorageEntries.Size; i++)
        {
          const ImGuiAppStorageEntry& e = app->StorageEntries[i];
          if (e.Size <= 0 || e.Ptr == nullptr)
            continue;
          h->SlotIds.push_back(e.ID);
          h->SlotSizes.push_back(e.Size);
          h->FrameSize += e.Size;
        }
        if (h->FrameSize == 0)
          return false;
        h->Frames.resize(h->MaxFrames * h->FrameSize);
      }

      char* dst = h->Frames.Data + h->Head * h->FrameSize;
      for (int s = 0; s < h->SlotIds.Size; s++)
      {
        const void* src = app->Data.GetVoidPtr(h->SlotIds[s]);
        if (src == nullptr)   // entry vanished without a composition change; invalidate
        {
          AppStateHistoryClear(h);
          return false;
        }
        memcpy(dst, src, (size_t)h->SlotSizes[s]);
        dst += h->SlotSizes[s];
      }
      h->Head = (h->Head + 1) % h->MaxFrames;
      if (h->Count < h->MaxFrames)
        h->Count++;
      return true;
  }

  // Hash of the Persist + LastTemp prefix of every snapshottable instance. TempData is this
  // frame's raw input, not state: excluded so record-time and replay-time hashes align.
  IMGUI_API ImGuiID AppStateHash(const ImGuiApp* app)
  {
      IM_ASSERT(app != nullptr);
      if (app == nullptr)
        return 0;
      ImGuiID h = 0;
      for (int i = 0; i < app->StorageEntries.Size; i++)
      {
        const ImGuiAppStorageEntry& e = app->StorageEntries[i];
        const int state_bytes = e.TempSize > 0 ? e.TempOffset : e.Size;   // no temp range: whole block is state
        if (e.Size <= 0 || e.Ptr == nullptr || state_bytes <= 0)
          continue;
        h = ImHashData(e.Ptr, (size_t)state_bytes, h);
      }
      return h;
  }

  IMGUI_API void AppInputLogClear(ImGuiAppInputLog* log)
  {
      IM_ASSERT(log != nullptr);
      if (log == nullptr)
        return;
      log->CompositionID = 0;
      log->FrameSize = 0;
      log->Count = 0;
      log->SlotIds.clear();
      log->SlotOffsets.clear();
      log->SlotSizes.clear();
      log->Frames.clear();
      log->StateHashes.clear();
  }

  IMGUI_API bool AppInputRecord(ImGuiApp* app, ImGuiAppInputLog* log, float dt)
  {
      IM_ASSERT(app != nullptr && log != nullptr);
      if (app == nullptr || log == nullptr)
        return false;

      const ImGuiID comp = GetAppCompositionID(app);
      if (comp != log->CompositionID || log->FrameSize == 0)
      {
        AppInputLogClear(log);
        log->CompositionID = comp;
        log->FrameSize = (int)sizeof(float);   // dt travels with the frame
        for (int i = 0; i < app->StorageEntries.Size; i++)
        {
          const ImGuiAppStorageEntry& e = app->StorageEntries[i];
          if (e.Size <= 0 || e.TempSize <= 0 || e.Ptr == nullptr)
            continue;
          log->SlotIds.push_back(e.ID);
          log->SlotOffsets.push_back(e.TempOffset);
          log->SlotSizes.push_back(e.TempSize);
          log->FrameSize += e.TempSize;
        }
        if (log->SlotIds.Size == 0)
        {
          AppInputLogClear(log);
          return false;   // nothing records input: nothing to replay
        }
      }

      const int base = log->Frames.Size;
      log->Frames.resize(base + log->FrameSize);
      char* dst = log->Frames.Data + base;
      memcpy(dst, &dt, sizeof(float));
      dst += sizeof(float);
      for (int s = 0; s < log->SlotIds.Size; s++)
      {
        const char* inst = (const char*)app->Data.GetVoidPtr(log->SlotIds[s]);
        if (inst == nullptr)
        {
          AppInputLogClear(log);
          return false;
        }
        memcpy(dst, inst + log->SlotOffsets[s], (size_t)log->SlotSizes[s]);
        dst += log->SlotSizes[s];
      }
      log->StateHashes.push_back(AppStateHash(app));
      log->Count++;
      return true;
  }

  IMGUI_API bool AppInputReplay(ImGuiApp* app, const ImGuiAppInputLog* log, int* out_first_divergence)
  {
      if (out_first_divergence != nullptr)
        *out_first_divergence = -1;
      IM_ASSERT(app != nullptr && log != nullptr);
      if (app == nullptr || log == nullptr || log->Count == 0)
        return false;
      if (GetAppCompositionID(app) != log->CompositionID)
        return false;

      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "replay %d recorded frames", log->Count);

      // UpdateApp consumes the TempData already in place; injection AFTER update stands in for
      // that frame's RenderApp recording. The state hash compares post-update, pre-inject --
      // exactly what AppInputRecord hashed.
      for (int f = 0; f < log->Count; f++)
      {
        const char* src = log->Frames.Data + f * log->FrameSize;
        float dt = 0.0f;
        memcpy(&dt, src, sizeof(float));
        src += sizeof(float);

        UpdateApp(app, dt);

        if (out_first_divergence != nullptr && *out_first_divergence < 0 && AppStateHash(app) != log->StateHashes[f])
          *out_first_divergence = f;

        for (int s = 0; s < log->SlotIds.Size; s++)
        {
          char* inst = (char*)app->Data.GetVoidPtr(log->SlotIds[s]);
          if (inst == nullptr)
            return false;
          memcpy(inst + log->SlotOffsets[s], src, (size_t)log->SlotSizes[s]);
          src += log->SlotSizes[s];
        }
      }
      return true;
  }

  IMGUI_API bool AppStateRestore(ImGuiApp* app, ImGuiAppStateHistory* h, int index)
  {
      IM_ASSERT(app != nullptr && h != nullptr);
      if (app == nullptr || h == nullptr || index < 0 || index >= h->Count)
        return false;
      if (GetAppCompositionID(app) != h->CompositionID)
        return false;

      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "restore state snapshot %d/%d", index, h->Count);

      const int oldest = (h->Head - h->Count + h->MaxFrames) % h->MaxFrames;
      const char* src = h->Frames.Data + ((oldest + index) % h->MaxFrames) * h->FrameSize;
      for (int s = 0; s < h->SlotIds.Size; s++)
      {
        void* dst = app->Data.GetVoidPtr(h->SlotIds[s]);
        if (dst == nullptr)
          return false;
        memcpy(dst, src, (size_t)h->SlotSizes[s]);
        src += h->SlotSizes[s];
      }
      return true;
  }

  IMGUI_API void UnregisterAppStorage(ImGuiApp* app, ImGuiID id)
  {
      IM_ASSERT(app);
      IM_ASSERT(id != 0);
      if (app == nullptr || id == 0)
        return;

      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "unregister storage 0x%08X", (unsigned)id);

      for (int i = 0; i < app->StorageEntries.Size; i++)
      {
        ImGuiAppStorageEntry& entry = app->StorageEntries[i];
        if (entry.ID != id)
          continue;
        if (entry.Destroy != nullptr && entry.Ptr != nullptr)
          entry.Destroy(entry.Ptr);
        app->StorageEntries.erase(app->StorageEntries.Data + i);
        app->Data.SetVoidPtr(id, nullptr);   // ImGuiStorage keeps the key; a null slot reads as absent
        app->CompositionRevision++;
        return;
      }
  }

  IMGUI_API ImGuiID GetAppCompositionID(const ImGuiApp* app)
  {
      IM_ASSERT(app);
      if (app == nullptr)
        return 0;

      ImGuiID h = ImHashData(&app->Layers.Size, sizeof(app->Layers.Size), 0);
      for (int i = 0; i < app->Windows.Size; i++)
        h = ImHashStr(app->Windows.Data[i]->Label, 0, h);
      for (int i = 0; i < app->Sidebars.Size; i++)
        h = ImHashStr(app->Sidebars.Data[i]->Label, 0, h);
      ForEachAppControl(app, [&h](const ImGuiAppControlBase* control, const ImGuiAppWindowBase* host)
      {
        IM_UNUSED(host);
        const ImGuiID id = control->GetControlDataID();
        h = ImHashData(&id, sizeof(id), h);
      });
      return h;
  }

  IMGUI_API const ImVector<ImGuiAppControlBase*>* AppRebuildUpdateOrder(ImGuiApp* app)
  {
      IM_ASSERT(app);

      // Revision, not the composition hash: popping and re-pushing the same control type returns
      // to an identical hash, but the control object and its instance data are NEW allocations --
      // the order and every consumer's cached dependency pointers must rebuild anyway.
      if (app->CompositionRevision != app->UpdateOrderRevision)
      {
        app->UpdateOrder.resize(0);
        ImVector<ImGuiAppControlBase*> nodes;
        ForEachAppControl(app, [&nodes](ImGuiAppControlBase* control, ImGuiAppWindowBase* host)
        {
            IM_UNUSED(host);
            nodes.push_back(control);
        });

        // Stable topological emit over the resolved dependency wiring: each pass emits, in
        // composition order, every control whose producers are all already emitted. Push-time
        // resolution requires a producer to exist before its consumer pushes, so cycles cannot
        // be composed and every pass progresses.
        ImVector<bool> emitted;
        emitted.resize(nodes.Size);
        for (int i = 0; i < emitted.Size; i++)
          emitted[i] = false;
        int remaining = nodes.Size;
        while (remaining > 0)
        {
          int progressed = 0;
          for (int i = 0; i < nodes.Size; i++)
          {
            if (emitted[i])
              continue;
            ImGuiID deps[64];
            const int dep_count = nodes[i]->GetControlDependencyIDs(deps, IM_ARRAYSIZE(deps));
            bool ready = true;
            for (int d = 0; d < dep_count && ready; d++)
              for (int j = 0; j < nodes.Size && ready; j++)
                if (!emitted[j] && j != i && nodes[j]->GetControlDataID() == deps[d])
                  ready = false;
            if (!ready)
              continue;
            emitted[i] = true;
            app->UpdateOrder.push_back(nodes[i]);
            progressed++;
          }
          IM_ASSERT(progressed > 0);
          if (progressed == 0)
          {
            for (int i = 0; i < nodes.Size; i++)
              if (!emitted[i])
                app->UpdateOrder.push_back(nodes[i]);
            break;
          }
          remaining -= progressed;
        }
        for (int i = 0; i < app->UpdateOrder.Size; i++)
          app->UpdateOrder.Data[i]->RefreshControlDependencyData(app);
        app->UpdateOrderRevision = app->CompositionRevision;
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "update order rebuilt (%d controls, revision %d)", app->UpdateOrder.Size, app->CompositionRevision);
      }
      return &app->UpdateOrder;
  }

  IMGUI_API void ClearAppStorage(ImGuiApp* app)
  {
      IM_ASSERT(app);
      if (app == nullptr)
        return;

      for (int i = app->StorageEntries.Size - 1; i >= 0; --i)
      {
        ImGuiAppStorageEntry& entry = app->StorageEntries[i];
        if (entry.Destroy != nullptr && entry.Ptr != nullptr)
          entry.Destroy(entry.Ptr);
      }

      app->StorageEntries.clear();
      app->Data.Clear();
  }

  //-----------------------------------------------------------------------------
  // [SECTION] App bring-up (InitializeApp / UpdateApp / RenderApp / storage)
  //-----------------------------------------------------------------------------

  IMGUI_API void InitializeApp(ImGuiApp* app, const ImGuiAppConfig* config)
  {
      IM_ASSERT(app);
      IM_ASSERT(app->Layers.empty() && "ImGui app already has layers. ShutdownApp() before re-initializing.");
      if (app == nullptr || !app->Layers.empty())
        return;

      if (config != nullptr)
      {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= config->ConfigFlags;
        if (!config->PersistSettings)
          io.IniFilename = nullptr;

        switch (config->Style)
        {
        case ImGuiAppStyle_Light:   ImGui::StyleColorsLight();   break;
        case ImGuiAppStyle_Classic: ImGui::StyleColorsClassic(); break;
        default:                    ImGui::StyleColorsDark();    break;
        }

        // DPI: the platform backend fills DpiScale with the startup monitor's scale before this
        // runs. Fonts rescale per monitor via ConfigDpiScaleFonts (Begin overwrites FontScaleDpi);
        // ImGuiStyle metrics scale once here -- imgui does not rescale them on monitor change.
        ImGuiStyle& style = ImGui::GetStyle();
        if (config->FontScale > 0.0f)
          style.FontScaleMain = config->FontScale;
        if (config->DpiScale > 0.0f && config->DpiScale != 1.0f)
        {
          style.ScaleAllSizes(config->DpiScale);
          style.FontScaleDpi = config->DpiScale;
        }
        io.ConfigDpiScaleFonts     = true;
        io.ConfigDpiScaleViewports = true;

        app->ClearColor = config->ClearColor;
      }

      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "initialize app");
      app->ShutdownPending = false;
      PushAppLayer<ImGuiAppTaskLayer>(app);
      PushAppLayer<ImGuiAppCommandLayer>(app);
      PushAppLayer<ImGuiAppStatusLayer>(app);
      PushAppLayer<ImGuiAppLayoutLayer>(app);
      PushAppLayer<ImGuiAppDisplayLayer>(app);
  }

  IMGUI_API void ShutdownApp(ImGuiApp* app)
  {
      IM_ASSERT(app);
      if (app == nullptr)
        return;

      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "shutdown app");

      while (!app->Sidebars.empty())
        PopAppSidebar(app);
      while (!app->Windows.empty())
        PopAppWindow(app);
      ShutdownAppControls(app, app->Controls);
      while (!app->Layers.empty())
        PopAppLayer(app);

      ClearAppStorage(app);
      app->ShutdownPending = false;
  }

  IMGUI_API void UpdateApp(ImGuiApp* app)
  {
      UpdateApp(app, GetIO().DeltaTime);
  }

  IMGUI_API void UpdateApp(ImGuiApp* app, float dt)
  {
      IM_ASSERT(app);

      AppWALWrite(app->WAL, ImGuiAppWALLevel_Frame, "frame update begin");
      for (auto& layer : app->Layers)
      {
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Frame, "update %s", layer->Label);
        layer->OnUpdate(app, dt);
      }
  }

  IMGUI_API void RenderApp(const ImGuiApp* app)
  {
      IM_ASSERT(app);

      AppWALWrite(app->WAL, ImGuiAppWALLevel_Frame, "frame render begin");
      for (auto& layer : app->Layers)
      {
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Frame, "render %s", layer->Label);
        layer->OnRender(app);
      }
  }
}
