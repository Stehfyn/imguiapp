// dear imgui app, v0.5.0 WIP
// (main application-layer code)

// ImGuiAppLayer core: frame pipeline, typed app composition (layers / windows / sidebars / controls),
// state discipline (docs/bug-classes.md).
//
// Index of this file (search for "[SECTION]"):
// [SECTION] Assert forensics (symbolized backtrace + WAL sink)
// [SECTION] Type schema registry (auto-materialized manifests)
// [SECTION] App shell + core phase layers (Task, Command, Status)
// [SECTION] Style/color mod runtime (desc apply; workbench style system)
// [SECTION] Display layer (windows, sidebars, hosted controls, .ini handler)
// [SECTION] App bring-up (InitializeApp / UpdateApp / RenderApp / storage)
// [SECTION] State snapshots + input record/replay (time travel)
// [SECTION] Frame pacing (advisory pacer; per-viewport present gating)
// [SECTION] Write-ahead log (ImGuiAppWAL)
// [SECTION] Authored style/color mods (PushAppStyleMods / PushAppColorMods)

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
#include "imguiapp_internal.h"   // process state root + internal structures/api shared with the satellite TUs

#include <cstdio>
#include <cstdlib>                        // exit (assert sink)
#include <cstring>                        // memcpy (state snapshots)

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>                      // CaptureStackBackTrace, IsDebuggerPresent
#include <dbghelp.h>                      // SymInitialize, SymFromAddr, SymGetLineFromAddr64
#include <intrin.h>                       // __rdtsc (frame id)
#pragma comment(lib, "dbghelp.lib")
#else
#include <time.h>                         // clock_gettime (pacer/frame-id monotonic clock)
#endif

//-----------------------------------------------------------------------------
// [SECTION] Assert forensics (symbolized backtrace + WAL sink)
//-----------------------------------------------------------------------------
// See imguix_imconfig.h for IM_ASSERT routing.

ImGuiAppProcessState* GImGuiAppState = nullptr;   // THE process-wide state root (struct + accessor: imguiapp_internal.h); safe before main
static bool           GImGuiAppInAssert = false;  // assert-handler re-entry guard; file scope so the crash path touches no accessor

IMGUI_API int ImAppStackTrace(char* out, int out_size, int skip_frames)
{
    if (out == nullptr || out_size <= 0)
        return 0;
    out[0] = 0;
#ifdef _WIN32
    HANDLE proc = GetCurrentProcess();
    bool& sym_ready = AppState().AssertSymReady;
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
#endif // #ifdef _WIN32
}

IMGUI_API void ImAppAssertFail(const char* expr, const char* file, int line)
{
    // Re-entrancy guard: an assert inside the logging path must not recurse.
    if (GImGuiAppInAssert)
        exit(3);
    GImGuiAppInAssert = true;

    char stack[3072];
    ImAppStackTrace(stack, IM_ARRAYSIZE(stack), 1);

    ImGui::AppWALWrite(AppAssert().WAL, ImGuiAppWALLevel_Lifecycle, "ASSERT FAILED: (%s) at %s:%d\n%s", expr, file, line, stack);
    IMGUIAPP_ERROR_PRINTF("ASSERT FAILED: (%s) at %s:%d\n%s", expr, file, line, stack);
    fflush(stderr);

    // Flight recorder (F15): dump every armed ring beside the assert WAL before we exit, so the last
    // seconds of frames survive the crash the way the WAL log does.
    char reason[IM_LABEL_SIZE + 128];
    ImFormatString(reason, IM_ARRAYSIZE(reason), "ASSERT: (%s) at %s:%d", expr, file, line);
    ImGui::AppDumpAssertRings(reason);

#ifdef _WIN32
    if (IsDebuggerPresent())
    {
        __debugbreak();
        GImGuiAppInAssert = false;   // debugger may continue past the break; let later asserts report too
        return;
    }
#endif
    exit(3);
}

// fprintf analog over the ImFile* seam: length-query + heap format, then ImFileWrite --
// no truncation, and client-overridden file functions keep working.
IMGUI_API int ImFilePrintf(ImFileHandle file, const char* fmt, ...)
{
    if (file == nullptr)
        return 0;
    va_list args;
    va_start(args, fmt);
    va_list args_copy;
    va_copy(args_copy, args);
    const int len = ImFormatStringV(nullptr, 0, fmt, args);
    va_end(args);
    if (len <= 0)
    {
        va_end(args_copy);
        return len;
    }
    ImVector<char> buf;
    buf.resize(len + 1);
    ImFormatStringV(buf.Data, (size_t)buf.Size, fmt, args_copy);
    va_end(args_copy);
    return (int)ImFileWrite(buf.Data, 1, (ImU64)len, file);
}

// Instance-qualified type id (ImHash* family): hash-combines a data type id with an instance
// number to key a control's instance data in ImGuiApp::Data. instance 0 keeps the bare type id
// (the type singleton); any other instance qualifies it.
// Dependency-slot key resolution for the adapter template (imguiapp.h): explicit binding wins,
// then the control's own instance id (when a producer instance exists under it), then the type
// singleton. Optional is only carried by an explicit binding.
IMGUI_API ImGuiID ImGui::AppResolveDependencyKey(const ImGuiApp* app, ImGuiID type_id, ImGuiID instance_id, const ImGuiAppDataBinding* binds, int binds_count, bool* out_optional)
{
    *out_optional = false;
    for (int i = 0; i < binds_count; i++)
        if (binds[i].TypeID == type_id)
        {
            *out_optional = binds[i].Optional;
            return ImAppHashType(type_id, binds[i].Instance);
        }
    if (instance_id != 0)
    {
        const ImGuiID own_key = ImAppHashType(type_id, instance_id);
        if (app->Data.GetVoidPtr(own_key) != nullptr)
            return own_key;
    }
    return type_id;
}

IMGUI_API ImGuiID ImAppHashType(ImGuiID type_id, ImGuiID instance)
{
    if (instance == 0)
        return type_id;
    return (ImGuiID)ImHashData(&instance, sizeof(instance), type_id);
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

    if (!ImGuiAppGetPlatformBackend()->InitPlatformFn(this, config))
    {
        OnShutdownPlatform();
        return false;
    }
    return true;
}

void ImGuiApp::OnShutdownPlatform()
{
    ImGuiAppGetPlatformBackend()->ShutdownPlatformFn(this);
    PlatformData = nullptr;
}

int ImGuiApp::Run(int argc, char** argv)
{
    if (!OnInitialize(argc, argv))
    {
        Shutdown();
        return 1;
    }

    return ImGuiAppGetPlatformBackend()->RunLoopFn(this);
}

// Process-wide pacer state (one paced app per process); only the deadline-chain fields
// clear on a paced app's Shutdown (see Shutdown).
static ImGuiAppPacerState& AppPacer() { return AppState().Pacer; }

// Optional-hook fallbacks (null hook or non-positive answer = the documented default).
static float AppPacerFuncsPrimaryHz(const ImGuiAppPacerFuncs* funcs)
{
    const float hz = funcs != nullptr && funcs->PrimaryRefreshHzFn != nullptr ? funcs->PrimaryRefreshHzFn() : 0.0f;
    return hz > 0.0f ? hz : 60.0f;
}

static float AppPacerFuncsViewportHz(const ImGuiAppPacerFuncs* funcs, const ImGuiViewport* viewport)
{
    const float hz = funcs->ViewportRefreshHzFn != nullptr ? funcs->ViewportRefreshHzFn(viewport) : 0.0f;
    return hz > 0.0f ? hz : AppPacerFuncsPrimaryHz(funcs);
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

    // One paced app per process by design. The funcs' teardown hook releases its wait
    // machinery (timers, thread QoS registrations); it must run on the paced (main) thread.
    ImGuiAppPacerState& pacer = AppPacer();
    if (pacer.App == this)
    {
        pacer.App = nullptr;
        pacer.NextDeadline = -1.0;
        pacer.LastEnter = -1.0;
    }
    if (Pacer.Funcs != nullptr && Pacer.Funcs->ShutdownFn != nullptr)
        Pacer.Funcs->ShutdownFn();

    ImGui::ShutdownApp(this);
    OnShutdownPlatform();

    PlatformName         = nullptr;
    PlatformWindowHandle = nullptr;
    Initialized          = false;
    ShutdownPending      = false;
}

// Monotonic wall clock in seconds; self-contained (must work without an ImGui context).
static double AppClockNowSec()
{
#ifdef _WIN32
    ImU64& freq = AppState().QpcHz;
    if (freq == 0)
    {
        LARGE_INTEGER f;
        ::QueryPerformanceFrequency(&f);
        freq = (ImU64)f.QuadPart;
    }
    LARGE_INTEGER now;
    ::QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq;
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
// any app in the process, so multiple apps share one timeline. Initialized once, on the
// first call (i.e. the first stamped frame), via C++11 thread-safe local-static init.
static double AppRunEpoch()
{
    double& epoch = AppState().RunEpoch;
    if (epoch == 0.0)
        epoch = AppClockNowSec();
    return epoch;
}

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
    const double epoch = AppRunEpoch();
    FrameID.FrameIndex++;
    FrameID.Tsc = AppClockTsc();
    FrameID.TimeSec = AppClockNowSec() - epoch;

    ImGuiAppGetPlatformBackend()->NewFrameFn(this);
    ImGui::NewFrame();
    ImGui::DrawAppFrame(this);
}

void ImGuiApp::OnRenderFrame()
{
    ImGuiAppFrameConfig frame_config;
    frame_config.ClearColor = ClearColor;
    ImGui::Render();
    ImGuiAppGetPlatformBackend()->RenderDrawDataFn(this, ImGui::GetDrawData(), &frame_config);
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
    const ImGuiAppPlatformBackend* backend = ImGuiAppGetPlatformBackend();
    if (backend->PresentFrameFn == nullptr)   // legacy single-hook: RenderDrawData presented
        return;
    ImGuiAppFrameConfig frame_config;
    frame_config.ClearColor = ClearColor;
    backend->PresentFrameFn(this, &frame_config);
}

void ImGuiApp::OnExecuteCommand(ImGuiAppCommand cmd)
{
    if (cmd == ImGuiAppCommand_Shutdown)
        ShutdownPending = true;
}

void ImGui::DrawAppFrame(ImGuiApp* app)
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
// [SECTION] Type schema registry (auto-materialized manifests)
//-----------------------------------------------------------------------------
// Manifest grammar + registration surface: see imguiapp.h.

// Registration can run from static initializers across TUs; AppState's lazy pointer anchor keeps
// the registry safe from static-init order.
static ImVector<const ImGuiAppTypeSchema*>* AppTypeSchemas() { return &AppState().TypeSchemas; }

void ImGui::AppRegisterTypeSchema(const ImGuiAppTypeSchema* schema)
{
    // Count may still be 0 here: AppEnsureTypeRegistered registers the entry before
    // filling its fields so cyclic member reachability terminates.
    IM_ASSERT(schema != nullptr && schema->TypeName != nullptr && schema->Fields != nullptr);
    IM_ASSERT(AppFindTypeSchema(schema->TypeName) == nullptr && "One schema per display name.");
    AppTypeSchemas()->push_back(schema);
}

const ImGuiAppTypeSchema* ImGui::AppFindTypeSchema(const char* type_name)
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
    // OnUpdate consumes the TempData recorded by last frame's OnDraw and mutates PersistData; runs before
    // the Command layer collects OnGetCommand, so state updated this frame can emit a command the same frame.
    // Dependency order: every producer updates before its consumers, regardless of hosting order.
    const ImVector<ImGuiAppControlBase*>* order = ImGui::AppRebuildUpdateOrder(app);
    for (int i = 0; i < order->Size; i++)
        order->Data[i]->OnUpdate(app, dt);
}

void ImGuiAppTaskLayer::OnDraw(const ImGuiApp* app) const
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

void ImGuiAppCommandLayer::OnDraw(const ImGuiApp* app) const
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

void ImGuiAppStatusLayer::OnDraw(const ImGuiApp* app) const
{
    IM_ASSERT(app != nullptr);
    if (app == nullptr)
        return;

    const ImGuiIO& io = ImGui::GetIO();
    const char* app_platform = app->PlatformName != nullptr ? app->PlatformName : "unknown";
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
} // namespace

//-----------------------------------------------------------------------------
// [SECTION] Style/color mod runtime (desc apply; workbench style system)
//-----------------------------------------------------------------------------

// Defaults for the public config/pacer structs (declared bare in the header, imgui-style; the default
// value is documented in the // = value column next to each member).
ImGuiAppConfig::ImGuiAppConfig()
{
    PlatformName                = nullptr;
    ConfigFlags                 = 0;
    Style                       = ImGuiAppStyle_Dark;
    ClearColor                  = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    FontScale                   = 1.0f;
    DpiScale                    = 1.0f;
    Headless                    = ImGuiAppHeadlessMode_None;
    PersistSettings             = true;
    WindowTitle                 = nullptr;
    WindowWidth                 = 0;
    WindowHeight                = 0;
}

ImGuiAppPacer::ImGuiAppPacer()
{
    Mode            = ImGuiAppPacerMode_Off;
    TargetHz        = 0.0f;
    SleepSlackMs    = 2.0f;
    Funcs           = nullptr;
    LastFrameMs     = 0.0;
    LastWaitMs      = 0.0;
    MissedDeadlines = 0;
}

// Bracket an item's submission with its authored style/color overrides: push before an item renders, pop
// after; the returned counts pop exactly what was pushed, so stacks stay balanced even if an entry's Active
// toggles mid-frame. File-local render-loop detail, not a polymorphic hook -- items expose StyleMods/ColorMods as data.
namespace
{
ImGuiAppStyleScope PushItemStyle(const ImGuiAppItemBase* item)
{
    ImGuiAppStyleScope s;
    s.Vars   = ImGui::PushAppStyleMods(item->StyleMods.Data, item->StyleMods.Size);
    s.Colors = ImGui::PushAppColorMods(item->ColorMods.Data, item->ColorMods.Size);
    return s;
}

void PopItemStyle(ImGuiAppStyleScope s)
{
    if (s.Colors > 0)
        ImGui::PopStyleColor(s.Colors);
    if (s.Vars > 0)
        ImGui::PopStyleVar(s.Vars);
}
} // namespace

// ImGuiAppControlBase: inert defaults for the live-mirror data-identity surface. ImGuiAppControlMirrorAdapter<>
// overrides every one of these from its (PersistDataT, TempDataT, DataDependencies...) pack; a plain
// ImGuiAppControlBase (or a control with nothing reflectable) keeps them.
ImGuiID ImGuiAppControlBase::GetDataID() const { return 0; }
int     ImGuiAppControlBase::GetDependencyIDs(ImGuiID* out, int cap) const { IM_UNUSED(out); IM_UNUSED(cap); return 0; }
int     ImGuiAppControlBase::GetDependencyTypeIDs(ImGuiID* out, int cap) const { IM_UNUSED(out); IM_UNUSED(cap); return 0; }
int     ImGuiAppControlBase::GetDependencyOptional(bool* out, int cap) const { IM_UNUSED(out); IM_UNUSED(cap); return 0; }
void    ImGuiAppControlBase::GetDataTypeName(char* out, int out_size) const { if (out && out_size > 0) out[0] = 0; }
void    ImGuiAppControlBase::GetTempDataTypeName(char* out, int out_size) const { if (out && out_size > 0) out[0] = 0; }
int     ImGuiAppControlBase::GetFields(ImGuiAppLiveFieldDesc* out, int cap, bool temp_data) const { IM_UNUSED(out); IM_UNUSED(cap); IM_UNUSED(temp_data); return 0; }
bool    ImGuiAppControlBase::IsDataReflectable(bool temp_data) const { IM_UNUSED(temp_data); return false; }
bool    ImGuiAppControlBase::GetLiveData(const void** out_persist, const void** out_temp) const { IM_UNUSED(out_persist); IM_UNUSED(out_temp); return false; }
void    ImGuiAppControlBase::RefreshDependencyData(const ImGuiApp* app) { IM_UNUSED(app); }
bool    ImGuiAppControlBase::SetDependencyBinding(ImGuiApp* app, const ImGuiAppDataBinding* bind) { IM_UNUSED(app); IM_UNUSED(bind); return false; }

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

void ImGuiAppLayoutLayer::OnDraw(const ImGuiApp* app) const
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
    for (ImGuiAppSidebarBase* sidebar : app->Sidebars)
        sidebar->OnUpdate(app, dt);

    for (ImGuiAppWindowBase* window : app->Windows)
        window->OnUpdate(app, dt);
}

void ImGuiAppDisplayLayer::OnDraw(const ImGuiApp* app) const
{
    for (ImGuiAppSidebarBase* sidebar : app->Sidebars)
    {
        const ImGuiAppStyleScope sidebar_scope = PushItemStyle(sidebar);

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
            sidebar->OnDraw(app);
        }
        else
        {
            sidebar->Open = false;
        }
        ImGui::End();

        PopItemStyle(sidebar_scope);

        // Controls render their own windows; submit them outside the sidebar's Begin/End.
        for (ImGuiAppControlBase* control : sidebar->Controls)
        {
            const ImGuiAppStyleScope control_scope = PushItemStyle(control);
            control->OnDraw(app);
            PopItemStyle(control_scope);
        }
    }

    for (ImGuiAppWindowBase* window : app->Windows)
    {
        // Closed window: composition member stays (mirror, wiring), but nothing renders -- no Begin,
        // no OnDraw, no hosted controls. Reopen by writing Open (outliner eye / host UI).
        if (!window->Open)
            continue;

        const ImGuiAppStyleScope window_scope = PushItemStyle(window);

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
            window->OnDraw(app);

            // Hosted controls render INSIDE the host window (child regions, not their own Begin/End).
            // Style mods bracket OnDraw only: they style the control's region but not its popups.
            for (ImGuiAppControlBase* control : window->Controls)
            {
                const ImGuiAppStyleScope control_scope = PushItemStyle(control);
                control->OnDraw(app);
                PopItemStyle(control_scope);
            }
        }
        ImGui::End();

        PopItemStyle(window_scope);
    }

    for (ImGuiAppControlBase* control : app->Controls)
    {
        const ImGuiAppStyleScope control_scope = PushItemStyle(control);
        control->OnDraw(app);
        PopItemStyle(control_scope);
    }
}

namespace ImGui
{
//-----------------------------------------------------------------------------
// [SECTION] App bring-up (InitializeApp / UpdateApp / RenderApp / storage)
//-----------------------------------------------------------------------------

// IMGUIAPP_CHECKVERSION() target: the compiled library and the including TU must agree on the
// version string and the core struct layouts (a mismatch = mixed headers/binaries).
IMGUI_API bool AppDebugCheckVersionAndDataLayout(const char* version, size_t sz_app, size_t sz_config, size_t sz_frame_config)
{
    bool error = false;
    if (strcmp(version, IMGUIAPP_VERSION) != 0)          { error = true; IM_ASSERT(strcmp(version, IMGUIAPP_VERSION) == 0 && "Mismatched IMGUIAPP_VERSION!"); }
    if (sz_app != sizeof(ImGuiApp))                      { error = true; IM_ASSERT(sz_app == sizeof(ImGuiApp) && "Mismatched sizeof(ImGuiApp)!"); }
    if (sz_config != sizeof(ImGuiAppConfig))             { error = true; IM_ASSERT(sz_config == sizeof(ImGuiAppConfig) && "Mismatched sizeof(ImGuiAppConfig)!"); }
    if (sz_frame_config != sizeof(ImGuiAppFrameConfig))  { error = true; IM_ASSERT(sz_frame_config == sizeof(ImGuiAppFrameConfig) && "Mismatched sizeof(ImGuiAppFrameConfig)!"); }
    return !error;
}

IMGUI_API void InitializeApp(ImGuiApp* app, const ImGuiAppConfig* config)
{
    IMGUIAPP_CHECKVERSION();
    IM_ASSERT(app != nullptr && "NULL ImGuiApp!");
    IM_ASSERT(app->Layers.empty() && "ImGui app already has layers. ShutdownApp() before re-initializing.");
    if (app == nullptr || !app->Layers.empty())
        return;

    if (config != nullptr)
    {
        ImGuiIO& io = GetIO();
        io.ConfigFlags |= config->ConfigFlags;
        if (!config->PersistSettings)
            io.IniFilename = nullptr;

        switch (config->Style)
        {
        case ImGuiAppStyle_Light:   StyleColorsLight();   break;
        case ImGuiAppStyle_Classic: StyleColorsClassic(); break;
        default:                    StyleColorsDark();    break;
        }

        // DPI: the platform backend fills DpiScale with the startup monitor's scale before this
        // runs. Fonts rescale per monitor via ConfigDpiScaleFonts (Begin overwrites FontScaleDpi);
        // ImGuiStyle metrics scale once here -- imgui does not rescale them on monitor change.
        ImGuiStyle& style = GetStyle();
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
    IM_ASSERT(app != nullptr && "NULL ImGuiApp!");
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
    IM_ASSERT(app != nullptr && "NULL ImGuiApp!");

    AppWALWrite(app->WAL, ImGuiAppWALLevel_Frame, "frame update begin");
    for (ImGuiAppLayerBase* layer : app->Layers)
    {
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Frame, "update %s", layer->Label);
        layer->OnUpdate(app, dt);
    }
}

IMGUI_API void RenderApp(const ImGuiApp* app)
{
    IM_ASSERT(app != nullptr && "NULL ImGuiApp!");

    AppWALWrite(app->WAL, ImGuiAppWALLevel_Frame, "frame render begin");
    for (ImGuiAppLayerBase* layer : app->Layers)
    {
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Frame, "render %s", layer->Label);
        layer->OnDraw(app);
    }
}

void PopAppLayer(ImGuiApp* app)
{
    IM_ASSERT(app != nullptr && "NULL ImGuiApp!");

    if (app->Layers.empty())
    {
        IM_ASSERT_USER_ERROR(0, "Calling PopAppLayer() too many times!");
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "overpop: PopAppLayer with none pushed");
        return;
    }

    ImGuiAppLayerBase* layer = app->Layers.back();
    app->Layers.pop_back();
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "pop layer %s", layer->Label);
    layer->OnDetach(app);
    IM_DELETE(layer);
}

void PopAppSidebar(ImGuiApp* app)
{
    IM_ASSERT(app != nullptr && "NULL ImGuiApp!");

    if (app->Sidebars.empty())
    {
        IM_ASSERT_USER_ERROR(0, "Calling PopAppSidebar() too many times!");
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "overpop: PopAppSidebar with none pushed");
        return;
    }
    ImGuiAppSidebarBase* sidebar = app->Sidebars.back();
    app->Sidebars.pop_back();
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "pop sidebar '%s'", sidebar->Label);
    ShutdownAppControls(app, sidebar->Controls);
    sidebar->OnShutdown(app);
    IM_DELETE(sidebar);
}

void PopAppWindow(ImGuiApp* app)
{
    IM_ASSERT(app != nullptr && "NULL ImGuiApp!");

    if (app->Windows.empty())
    {
        IM_ASSERT_USER_ERROR(0, "Calling PopAppWindow() too many times!");
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "overpop: PopAppWindow with none pushed");
        return;
    }

    ImGuiAppWindowBase* window = app->Windows.back();
    app->Windows.pop_back();
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "pop window '%s'", window->Label);
    ShutdownAppControls(app, window->Controls);
    window->OnShutdown(app);
    IM_DELETE(window);
}

void PopAppControl(ImGuiApp* app)
{
    IM_ASSERT(app != nullptr && "NULL ImGuiApp!");

    if (app->Controls.empty())
    {
        IM_ASSERT_USER_ERROR(0, "Calling PopAppControl() too many times!");
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "overpop: PopAppControl with none pushed");
        return;
    }

    ImGuiAppControlBase* control = app->Controls.back();
    app->Controls.pop_back();
    if (app->WAL != nullptr)
    {
        char dt[IM_LABEL_SIZE];
        control->GetDataTypeName(dt, IM_ARRAYSIZE(dt));
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "pop control <%s>", dt);
    }
    control->OnShutdown(app);
    const ImGuiID data_id = control->GetDataID();   // read before delete; pop frees what push registered
    IM_DELETE(control);
    if (data_id != 0)
        UnregisterAppStorage(app, data_id);
}

IMGUI_API ImGuiID GetAppCompositionID(const ImGuiApp* app)
{
    IM_ASSERT(app != nullptr && "NULL ImGuiApp!");
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
        const ImGuiID id = control->GetDataID();
        h = ImHashData(&id, sizeof(id), h);
      });
    return h;
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
    IM_ASSERT(app != nullptr && "NULL ImGuiApp!");
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

// Register a control's instance data (id-keyed). When snapshottable, forwards the type-derived size + TempData
// byte range (snapshot/replay); otherwise registers opaque. Callers supply the type-derived args (sizeof/offset).
IMGUI_API void RegisterAppControlStorage(ImGuiApp* app, ImGuiID id, void* instance_data, bool snapshottable, int inst_size, int temp_offset, int temp_size, void (*destroy)(void*))
{
    RegisterAppStorage(app, id, instance_data,
                       snapshottable ? inst_size : 0,
                       snapshottable ? temp_offset : 0,
                       snapshottable ? temp_size : 0,
                       destroy);
}

IMGUI_API void AppControlRegisterStorage(ImGuiApp* app, ImGuiAppControlBase* control, const char* name, ImGuiID data_type_id, ImGuiID instance, void* instance_data, bool snapshottable, int inst_size, int temp_offset, int temp_size, void (*destroy)(void*), const char* host_kind, const char* host_label)
{
    // Instance data is keyed by the instance-qualified data type id so dependents can resolve it.
    const ImGuiID id = ImAppHashType(data_type_id, instance);
    IM_ASSERT(app->Data.GetVoidPtr(id) == nullptr && "One instance per (control data type, instance id).");
    if (host_label == nullptr)
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push control %s (instance %u)", name, (unsigned)instance);
    else
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push control %s (instance %u) into %s '%s'", name, (unsigned)instance, host_kind, host_label);
    ImStrncpy(control->Label, name, sizeof(control->Label));
    app->Data.SetVoidPtr(id, instance_data);
    RegisterAppControlStorage(app, id, instance_data, snapshottable, inst_size, temp_offset, temp_size, destroy);
}

IMGUI_API void AppControlPush(ImGuiApp* app, ImVector<ImGuiAppControlBase*>* list, ImGuiAppControlBase* control)
{
    list->push_back(control);
    control->OnInitialize(app);
}

IMGUI_API void UnregisterAppStorage(ImGuiApp* app, ImGuiID id)
{
    IM_ASSERT(app != nullptr && "NULL ImGuiApp!");
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

IMGUI_API void ClearAppStorage(ImGuiApp* app)
{
    IM_ASSERT(app != nullptr && "NULL ImGuiApp!");
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

void AppDeduplicateItemLabel(char* label, int label_size, const ImVector<ImGuiAppWindowBase*>* windows, const ImVector<ImGuiAppSidebarBase*>* sidebars)
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

IMGUI_API void AppRegisterLayer(ImGuiApp* app, ImGuiAppLayerBase* layer, const char* name)
{
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push layer %s", name);
    app->Layers.push_back(layer);
    if (layer->Label[0] == 0) // default Label to the type name
        ImStrncpy(layer->Label, name, IM_ARRAYSIZE(layer->Label));
    layer->OnAttach(app);
}

IMGUI_API void AppRegisterWindow(ImGuiApp* app, ImGuiAppWindowBase* window, const char* name)
{
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push window %s", name);
    if (window->Label[0] == 0) // default Label to the type name (the one labeling path: push formats, this tail stamps)
        ImStrncpy(window->Label, name, IM_ARRAYSIZE(window->Label));
    AppDeduplicateItemLabel(window->Label, IM_ARRAYSIZE(window->Label), &app->Windows, &app->Sidebars);
    app->Windows.push_back(window);
    window->OnInitialize(app);
}

IMGUI_API void AppRegisterSidebar(ImGuiApp* app, ImGuiAppSidebarBase* sidebar, const char* name, ImGuiViewport* vp, ImGuiDir dir, float size, ImGuiWindowFlags flags)
{
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push sidebar %s", name);
    if (sidebar->Label[0] == 0) // default Label to the type name (the one labeling path: push formats, this tail stamps)
        ImStrncpy(sidebar->Label, name, IM_ARRAYSIZE(sidebar->Label));
    AppDeduplicateItemLabel(sidebar->Label, IM_ARRAYSIZE(sidebar->Label), &app->Windows, &app->Sidebars);
    sidebar->Viewport = vp;
    sidebar->DockDir  = dir;
    sidebar->Size     = size;
    sidebar->Flags    = flags;
    app->Sidebars.push_back(sidebar);
    sidebar->OnInitialize(app);
}

IMGUI_API const ImVector<ImGuiAppControlBase*>* AppRebuildUpdateOrder(ImGuiApp* app)
{
    IM_ASSERT(app != nullptr && "NULL ImGuiApp!");

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

        // Stable topological emit over the resolved dependency wiring: each pass emits, in composition order,
        // every control whose producers are all already emitted. Push-time resolution requires a producer to exist
        // before its consumer pushes, so cycles cannot be composed and every pass progresses.
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
                const int dep_count = nodes[i]->GetDependencyIDs(deps, IM_ARRAYSIZE(deps));
                bool ready = true;
                for (int d = 0; d < dep_count && ready; d++)
                    for (int j = 0; j < nodes.Size && ready; j++)
                        if (!emitted[j] && j != i && nodes[j]->GetDataID() == deps[d])
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
            app->UpdateOrder.Data[i]->RefreshDependencyData(app);
        app->UpdateOrderRevision = app->CompositionRevision;
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "update order rebuilt (%d controls, revision %d)", app->UpdateOrder.Size, app->CompositionRevision);
    }
    return &app->UpdateOrder;
}

// Composition push/pop helpers (declarations in imguiapp.h; the Push* templates there call these).
void ShutdownAppControls(ImGuiApp* app, ImVector<ImGuiAppControlBase*>& controls)
{
    IM_ASSERT(app != nullptr && "NULL ImGuiApp!");

    while (!controls.empty())
    {
        ImGuiAppControlBase* control = controls.back();
        controls.pop_back();
        if (app->WAL != nullptr)
        {
            char dt[IM_LABEL_SIZE];
            control->GetDataTypeName(dt, IM_ARRAYSIZE(dt));
            AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "shutdown control <%s>", dt);
        }
        control->OnShutdown(app);
        const ImGuiID data_id = control->GetDataID();   // read before delete
        IM_DELETE(control);
        if (data_id != 0)
            UnregisterAppStorage(app, data_id);
    }
}

//-----------------------------------------------------------------------------
// [SECTION] State snapshots + input record/replay (time travel)
//-----------------------------------------------------------------------------
//
// OnUpdate is the sole state mutator and all durable state lives in registered storage,
// so a byte copy of the snapshottable entries IS the app's state at that frame.

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

IMGUI_API ImU32 AppStateSchemaHash(const ImGuiApp* app)
{
    IM_ASSERT(app != nullptr);
    if (app == nullptr)
        return 0;
    ImGuiID schema = 0;
    for (int i = 0; i < app->StorageEntries.Size; i++)
    {
        const ImGuiAppStorageEntry& e = app->StorageEntries[i];
        if (e.Size <= 0 || e.Ptr == nullptr)
            continue;
        const ImU32 fields[4] = { (ImU32)e.ID, (ImU32)e.Size, (ImU32)e.TempOffset, (ImU32)e.TempSize };
        schema = ImHashData(fields, sizeof(fields), schema);
    }
    return (ImU32)schema;
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
    memcpy(dst, &dt, sizeof(dt));
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
        memcpy(&dt, src, sizeof(dt));
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

//-----------------------------------------------------------------------------
// [SECTION] Frame pacing (advisory pacer; per-viewport present gating)
//-----------------------------------------------------------------------------

IMGUI_API void AppPacerWait(ImGuiApp* app)
{
    IM_ASSERT(app != nullptr);
    if (app == nullptr || app->Pacer.Mode == ImGuiAppPacerMode_Off)
        return;

    ImGuiAppPacer* p = &app->Pacer;
    const ImGuiAppPacerFuncs* funcs = p->Funcs;

    float hz = p->TargetHz;
    if (hz <= 0.0f)
    {
        hz = AppPacerFuncsPrimaryHz(funcs);
        // Fixed mode's dt injection (DrawFrame) reads TargetHz; resolve it ONCE so the
        // deterministic timestep exists and never re-tracks a monitor change mid-run.
        if (p->Mode == ImGuiAppPacerMode_Fixed)
            p->TargetHz = hz;
    }
    const double period = 1.0 / (double)hz;

    // No funcs installed: nothing to wait WITH. Fixed keeps its deterministic dt (resolved
    // above); the loop free-runs at whatever the present mode allows.
    if (funcs == nullptr)
        return;
    IM_ASSERT(funcs->NowFn != nullptr && funcs->WaitUntilFn != nullptr && "ImGuiAppPacer::Funcs: NowFn and WaitUntilFn are required.");

    ImGuiAppPacerState& pacer = AppPacer();
    const double now = funcs->NowFn();
    if (pacer.App != app || pacer.NextDeadline < 0.0)
    {
        // First paced frame (or the paced app changed): establish the deadline chain, no wait.
        pacer.App = app;
        pacer.NextDeadline = now + period;
        pacer.LastEnter = now;
        return;
    }

    p->LastFrameMs = (now - pacer.LastEnter) * 1000.0;
    pacer.LastEnter = now;

    // Deadline chain (previous deadline + period), never now + period: chaining absorbs
    // jitter without drifting. Arriving late is a miss; re-anchor so one long frame
    // doesn't cascade into a chase.
    double deadline = pacer.NextDeadline;
    if (now > deadline)
    {
        p->MissedDeadlines++;
        deadline = now;
    }

    funcs->WaitUntilFn(deadline, p->SleepSlackMs);

    p->LastWaitMs = (funcs->NowFn() - now) * 1000.0;
    pacer.NextDeadline = deadline + period;
}

IMGUI_API float AppPacerResolveHz(const ImGuiApp* app)
{
    IM_ASSERT(app != nullptr);
    if (app == nullptr)
        return 60.0f;
    return app->Pacer.TargetHz > 0.0f ? app->Pacer.TargetHz : AppPacerFuncsPrimaryHz(app->Pacer.Funcs);
}

IMGUI_API bool AppPacerViewportShouldPresent(ImGuiApp* app, ImGuiViewport* viewport)
{
    if (app == nullptr || viewport == nullptr)
        return true;
    if (app->Pacer.Mode == ImGuiAppPacerMode_Off)
        return true;
    if (viewport == GetMainViewport())
        return true;   // the run loop's AppPacerWait already paces the main viewport

    const ImGuiAppPacerFuncs* funcs = app->Pacer.Funcs;
    if (funcs == nullptr || funcs->NowFn == nullptr)
        return true;   // no funcs = no clock to gate with; present every frame

    const float hz = AppPacerFuncsViewportHz(funcs, viewport);
    const double period = 1.0 / (double)(hz > 1.0f ? hz : 60.0f);
    const double now = funcs->NowFn();

    ImVector<ImGuiAppViewportPace>& vp_pace = AppPacer().ViewportPace;
    ImGuiAppViewportPace* pace = nullptr;
    for (int i = 0; i < vp_pace.Size; i++)
        if (vp_pace.Data[i].ViewportId == viewport->ID)
        {
            pace = &vp_pace.Data[i];
            break;
        }
    if (pace == nullptr)
    {
        // Lazy prune: entries unseen for ~10s at 60fps are vanished viewports.
        if (vp_pace.Size > 8)
            for (int i = vp_pace.Size - 1; i >= 0; i--)
                if (vp_pace.Data[i].LastSeenFrame + 600 < app->FrameID.FrameIndex)
                    vp_pace.erase(vp_pace.begin() + i);
        ImGuiAppViewportPace fresh;
        fresh.ViewportId = viewport->ID;
        fresh.NextDeadline = now + period;
        fresh.LastSeenFrame = app->FrameID.FrameIndex;
        vp_pace.push_back(fresh);
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

//-----------------------------------------------------------------------------
// [SECTION] Write-ahead log (ImGuiAppWAL)
//-----------------------------------------------------------------------------
//
// Contract: a record is on disk BEFORE the operation it names runs; fflush on every record.
// Lifecycle level = composition changes only; Frame level = per-frame records.

IMGUI_API bool AppWALOpen(ImGuiAppWAL* wal, const char* path, ImGuiAppWALLevel level)
{
    IM_ASSERT(wal != nullptr && path != nullptr);
    if (wal == nullptr || path == nullptr)
        return false;

    AppWALClose(wal);
    ImFileHandle f = ImFileOpen(path, "wt");
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
    ImFileClose(wal->File);
    wal->File = nullptr;
    wal->Level = ImGuiAppWALLevel_Off;
}

IMGUI_API void AppWALWriteV(ImGuiAppWAL* wal, ImGuiAppWALLevel level, const char* fmt, va_list args)
{
    if (wal == nullptr || wal->File == nullptr || level > wal->Level)
        return;

    char msg[512];
    ImFormatStringV(msg, IM_ARRAYSIZE(msg), fmt, args);

    // WAL must also work before/after the ImGui context's lifetime.
    const int frame = GetCurrentContext() != nullptr ? GetFrameCount() : -1;
    ImFileHandle f = wal->File;
    if (wal->FrameID != nullptr)
        ImFilePrintf(f, "[%06d f%05d] [tick:%llu tsc:%llu] %s\n", wal->Seq++, frame,
                     (unsigned long long)wal->FrameID->FrameIndex, (unsigned long long)wal->FrameID->Tsc, msg);
    else
        ImFilePrintf(f, "[%06d f%05d] %s\n", wal->Seq++, frame, msg);
    fflush(f);   // write-ahead guarantee
}

IMGUI_API void AppWALWrite(ImGuiAppWAL* wal, ImGuiAppWALLevel level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    AppWALWriteV(wal, level, fmt, args);
    va_end(args);
}

// Media Foundation backend's MFStartup/COM process refcount (lives on the state root so the
// backend TU carries no mutable static of its own).
IMGUI_API int& AppMediaFoundationStartupRefs()
{
    return AppState().MediaFoundationRefs;
}

IMGUI_API void SetAppAssertWAL(ImGuiAppWAL* wal)
{
    AppAssert().WAL = wal;
}

//-----------------------------------------------------------------------------
// [SECTION] Authored style/color mods (PushAppStyleMods / PushAppColorMods)
//-----------------------------------------------------------------------------

int PushAppStyleMods(const ImGuiAppStyleModDesc* mods, int count)
{
    int pushed = 0;
    for (int i = 0; i < count; i++)
    {
        const ImGuiAppStyleModDesc& mod = mods[i];
        if (!mod.Active || mod.Var < 0 || mod.Var >= ImGuiStyleVar_COUNT)
            continue;
        const ImGuiStyleVarInfo* info = GetStyleVarInfo(mod.Var);
        if (info == nullptr || info->DataType != ImGuiDataType_Float)
            continue;
        if (info->Count == 2)
            PushStyleVar(mod.Var, mod.Value);
        else
            PushStyleVar(mod.Var, mod.Value.x);
        pushed++;
    }
    return pushed;
}

int PushAppColorMods(const ImGuiAppColorModDesc* mods, int count)
{
    int pushed = 0;
    for (int i = 0; i < count; i++)
    {
        const ImGuiAppColorModDesc& mod = mods[i];
        if (!mod.Active || mod.Col < 0 || mod.Col >= ImGuiCol_COUNT)
            continue;
        PushStyleColor(mod.Col, mod.Value);
        pushed++;
    }
    return pushed;
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
