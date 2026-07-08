// dear imgui app, v0.5.0 WIP
// (headless test harness)

// ImGuiAppTestHarness (imguiapp_internal.h): Test Engine + headless rendering +
// frame encoding + WAL, one entry point, one frame id across every artifact
// (docs/designs.md (av-design)). The only applayer TU that links the imgui test engine.
//
// Index of this file (search for "[SECTION]"):
// [SECTION] Filesystem helpers (mkdir, artifact cleanup)
// [SECTION] Harness run (AppTestHarnessRun)

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imguiapp.h"
#ifndef IMGUI_DISABLE
#ifdef IMGUI_ENABLE_TEST_ENGINE
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
#include "backends/imguiapp_impl_qoi.h"
#ifdef IMGUIX_HAS_LIBAV
#include "backends/imguiapp_impl_libav.h"
#endif
#include "imgui_te_engine.h"
#include "imgui_te_ui.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <algorithm>
#ifndef IMGUIAPP_DISABLE_DEFAULT_FILESYSTEM_FUNCS
#include <filesystem>            // default ImGuiAppFileSystemFuncs impl only
#include <string>
#endif

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

//-----------------------------------------------------------------------------
// [SECTION] Filesystem helpers (mkdir, artifact cleanup)
//-----------------------------------------------------------------------------

// Default ImGuiAppFileSystemFuncs: platform libc mkdir/remove + std::filesystem scan --
// the only place std::filesystem may appear (docs/house-style-audit.md Δ2).
#ifndef IMGUIAPP_DISABLE_DEFAULT_FILESYSTEM_FUNCS
static bool AppFsMkdirOneDefault(const char* path)
{
#ifdef _WIN32
    const int rc = _mkdir(path);
#else
    const int rc = mkdir(path, 0755);
#endif
    return rc == 0 || errno == EEXIST;
}

// Create every missing directory on the path (separators '/' or '\').
static bool AppFsCreateDirRecursiveDefault(const char* path)
{
    char buf[512];
    ImFormatString(buf, IM_ARRAYSIZE(buf), "%s", path);
    for (char* c = buf + 1; *c; c++)
    {
        if (*c != '/' && *c != '\\')
            continue;
        const char sep = *c;
        *c = 0;
        if (buf[0] != 0 && buf[strlen(buf) - 1] != ':' && !AppFsMkdirOneDefault(buf))
            return false;
        *c = sep;
    }
    return AppFsMkdirOneDefault(buf);
}

static bool AppFsRemoveFileDefault(const char* path) { return remove(path) == 0; }

static bool AppFsRemoveDirDefault(const char* path)
{
#ifdef _WIN32
    return _rmdir(path) == 0;
#else
    return rmdir(path) == 0;
#endif
}

static void AppFsScanDirDefault(const char* dir, void (*visit)(const char* name, ImU64 size_bytes, void* user_data), void* user_data)
{
    std::error_code fs_ec;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, fs_ec))
    {
        if (!entry.is_regular_file(fs_ec))
            continue;
        visit(entry.path().filename().string().c_str(), (ImU64)entry.file_size(fs_ec), user_data);
    }
}

static const ImGuiAppFileSystemFuncs GAppFileSystemFuncsDefault =
{
    AppFsCreateDirRecursiveDefault,
    AppFsRemoveFileDefault,
    AppFsRemoveDirDefault,
    AppFsScanDirDefault,
};
#endif // #ifndef IMGUIAPP_DISABLE_DEFAULT_FILESYSTEM_FUNCS

// Reads the process-state slot; null resolves to the libc + std::filesystem default (unless stripped).
IMGUI_API const ImGuiAppFileSystemFuncs* ImGui::AppFileSystemFuncs()
{
#ifndef IMGUIAPP_DISABLE_DEFAULT_FILESYSTEM_FUNCS
    return AppState().FileSystemFuncs != nullptr ? AppState().FileSystemFuncs : &GAppFileSystemFuncsDefault;
#else
    IM_ASSERT(AppState().FileSystemFuncs != nullptr && "IMGUIAPP_DISABLE_DEFAULT_FILESYSTEM_FUNCS stripped the default; call ImGui::SetAppFileSystemFuncs() first.");
    return AppState().FileSystemFuncs;
#endif
}

IMGUI_API void ImGui::SetAppFileSystemFuncs(const ImGuiAppFileSystemFuncs* funcs)
{
    AppState().FileSystemFuncs = funcs;
}

namespace
{
// Best-effort removal of one recording's artifacts. A QOI take is a directory
// (NNNNNN.qoi + index.tsv); everything else is "<video>" plus recorder/provider
// side files.
void HarnessRemoveRecordingArtifacts(const char* video_path, bool is_qoi_dir)
{
    const ImGuiAppFileSystemFuncs* fs = ImGui::AppFileSystemFuncs();
    char path[600];
    if (is_qoi_dir)
    {
        for (int i = 0; ; i++)
        {
            ImFormatString(path, IM_ARRAYSIZE(path), "%s/%06d.qoi", video_path, i);
            if (!fs->RemoveFileFn(path))
                break;
        }
        ImFormatString(path, IM_ARRAYSIZE(path), "%s/index.tsv", video_path);
        fs->RemoveFileFn(path);
        fs->RemoveDirFn(video_path);
    }
    else
    {
        fs->RemoveFileFn(video_path);
    }
}
} // namespace

//-----------------------------------------------------------------------------
// [SECTION] Harness run (AppTestHarnessRun)
//-----------------------------------------------------------------------------

IMGUI_API int ImGui::AppTestHarnessRun(ImGuiApp* app, const ImGuiAppTestHarnessConfig* config)
{
    IM_ASSERT(app != nullptr && config != nullptr);
    if (app == nullptr || config == nullptr)
        return 2;
    IM_ASSERT(config->Name != nullptr && config->RegisterTests != nullptr && "harness config requires Name and RegisterTests");
    if (config->Name == nullptr || config->RegisterTests == nullptr)
        return 2;

    const char* artifact_dir = config->ArtifactDir != nullptr ? config->ArtifactDir : ".";
    if (!ImGui::AppFileSystemFuncs()->CreateDirRecursiveFn(artifact_dir))
    {
        IMGUIAPP_ERROR_PRINTF("[harness] cannot create artifact dir '%s'\n", artifact_dir);
        return 2;
    }

    // WAL first: every later step logs through it, frame-id prefixed.
    char wal_path[560];
    ImFormatString(wal_path, IM_ARRAYSIZE(wal_path), "%s/%s.wal", artifact_dir, config->Name);
    ImGuiAppWAL wal;
    const bool wal_open = AppWALOpen(&wal, wal_path, config->WALLevel);
    wal.FrameID = &app->FrameID;
    ImGuiAppWAL* backup_wal = app->WAL;
    app->WAL = wal_open ? &wal : backup_wal;
    SetAppAssertWAL(wal_open ? &wal : nullptr);

    app->Pacer.Mode = config->PacerMode;
    app->Pacer.TargetHz = config->Fps;

    ImGuiAppConfig app_config;
    app_config.PlatformName    = config->Name;
    app_config.ConfigFlags     = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;   // no viewports: deterministic single target
    app_config.Headless        = config->Headless;
    app_config.PersistSettings = false;   // a test run must never touch the user's ini
    app_config.WindowTitle     = config->Name;
    app_config.WindowWidth     = 1280;
    app_config.WindowHeight    = 800;

    bool initialized = app->Initialize(&app_config);
    if (!initialized && config->Headless != ImGuiAppHeadlessMode_None)
    {
        // Backend without headless support (or headless init failure): witness it, run windowed.
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "harness: headless init failed, falling back to windowed");
        app_config.Headless = ImGuiAppHeadlessMode_None;
        initialized = app->Initialize(&app_config);
    }
    if (config->EffectiveHeadless != nullptr)
        *config->EffectiveHeadless = initialized ? app_config.Headless : ImGuiAppHeadlessMode_None;
    if (!initialized)
    {
        IMGUIAPP_ERROR_PRINTF("[harness] app initialization failed\n");
        SetAppAssertWAL(nullptr);
        app->WAL = backup_wal;
        AppWALClose(&wal);
        return 2;
    }

    // Recorder: config encoder is caller-owned; the harness default (ffmpeg on PATH,
    // else QOI sequence) is harness-owned and destroyed at teardown.
    char video_path[560];
    video_path[0] = 0;
    ImGuiAppAVEncoder* encoder = config->Encoder;
    bool encoder_owned = false;
    bool video_is_qoi_dir = false;
    ImGuiAppRecorder* recorder = nullptr;
    int embed_rows = 0;
    if (config->RecordVideo)
    {
        ImGuiAppAVEncodeConfig enc_config;
        enc_config.Fps = config->Fps;
        enc_config.Timing = config->Timing;
        embed_rows = enc_config.EmbedRows;
        if (encoder != nullptr)
        {
            ImFormatString(video_path, IM_ARRAYSIZE(video_path), "%s/%s.mp4", artifact_dir, config->Name);
            enc_config.OutputPath = video_path;
            recorder = AppRecordBegin(app, encoder, &enc_config);
        }
        else
        {
            // Default: libav mp4 when the SDK was linked, else the zero-dependency QOI sequence.
            encoder_owned = true;
#ifdef IMGUIX_HAS_LIBAV
            encoder = ImGuiApp_ImplLibav_CreateEncoder();
            ImFormatString(video_path, IM_ARRAYSIZE(video_path), "%s/%s.mp4", artifact_dir, config->Name);
            enc_config.OutputPath = video_path;
            recorder = AppRecordBegin(app, encoder, &enc_config);
            if (recorder == nullptr)
                ImGui::AppAVDestroyEncoder(encoder);
#endif
            if (recorder == nullptr)
            {
                encoder = ImGuiApp_ImplQoi_CreateEncoder();
                ImFormatString(video_path, IM_ARRAYSIZE(video_path), "%s/%s", artifact_dir, config->Name);
                enc_config.OutputPath = video_path;
                recorder = AppRecordBegin(app, encoder, &enc_config);
                video_is_qoi_dir = true;
            }
        }
        if (recorder == nullptr)
        {
            AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "harness: video recording unavailable, running without");
            if (encoder_owned)
                ImGui::AppAVDestroyEncoder(encoder);
            encoder = nullptr;
            encoder_owned = false;
            video_path[0] = 0;
        }
    }

    ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO& test_io = ImGuiTestEngine_GetIO(engine);
    test_io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
    test_io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
    test_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
    test_io.ConfigLogToTTY = true;

    ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());
    config->RegisterTests(engine);
    ImGuiTestEngine_QueueTests(engine, ImGuiTestGroup_Tests, config->TestFilter, ImGuiTestRunFlags_RunFromCommandLine);
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "harness: '%s' queued (filter '%s')", config->Name,
                config->TestFilter != nullptr ? config->TestFilter : "*");

    // Frame loop: the app's four phases with the test engine's swap hooks around present.
    ImVector<ImU64> ft_frame;
    ImVector<ImU64> ft_tsc;
    ImVector<double> ft_ms;
    ImU64 backup_tsc = 0;
    double backup_sec = 0.0;
    bool aborted = false;
    int frame = 0;
    const int max_frames = 200000;
    for (;;)
    {
        AppPacerWait(app);
        app->OnDrawFrame();
        if (config->Headless == ImGuiAppHeadlessMode_None)
            ImGuiTestEngine_ShowTestEngineWindows(engine, nullptr);
        app->OnRenderFrame();
        app->OnEncodeFrame();
        ImGuiTestEngine_PreSwap(engine);
        app->OnPresentFrame();
        ImGuiTestEngine_PostSwap(engine);

        if (frame > 0)
        {
            ft_frame.push_back(app->FrameID.FrameIndex);
            ft_tsc.push_back(app->FrameID.Tsc - backup_tsc);
            ft_ms.push_back((app->FrameID.TimeSec - backup_sec) * 1000.0);
        }
        backup_tsc = app->FrameID.Tsc;
        backup_sec = app->FrameID.TimeSec;

        if (frame > 4 && ImGuiTestEngine_IsTestQueueEmpty(engine))
            break;
        if (app->ShutdownPending || ++frame > max_frames)
        {
            aborted = true;
            break;
        }
    }

    int count_tested = 0;
    int count_success = 0;
    ImGuiTestEngine_GetResult(engine, count_tested, count_success);
    const bool all_passed = !aborted && count_tested > 0 && count_success == count_tested;
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "harness: %d/%d passed%s", count_success, count_tested,
                aborted ? " (ABORTED)" : "");

    // Teardown order: engine stops before the app tears down the ImGui context; the
    // engine context dies after the ImGui context (mirrors tests_main).
    ImGuiTestEngine_Stop(engine);
    if (recorder != nullptr)
        AppRecordEnd(recorder);   // clears app->Recorder
    if (encoder_owned)
        ImGui::AppAVDestroyEncoder(encoder);
    app->Shutdown();
    ImGuiTestEngine_DestroyContext(engine);

    // Frame-time artifact + console percentiles (real per-frame cost in every posture).
    char csv_path[560];
    ImFormatString(csv_path, IM_ARRAYSIZE(csv_path), "%s/%s.frametimes.csv", artifact_dir, config->Name);
    if (ImFileHandle csv = ImFileOpen(csv_path, "w"))
    {
        ImFilePrintf(csv, "frame_index\ttsc_delta\tms\n");
        for (int i = 0; i < ft_ms.Size; i++)
            ImFilePrintf(csv, "%llu\t%llu\t%.4f\n", (unsigned long long)ft_frame[i], (unsigned long long)ft_tsc[i], ft_ms[i]);
        ImFileClose(csv);
    }
    if (ft_ms.Size > 0)
    {
        ImVector<double> sorted = ft_ms;
        struct Func { static int IMGUI_CDECL SampleComparerByValue(const void* lhs, const void* rhs) { const double a = *(const double*)lhs, b = *(const double*)rhs; return (a < b) ? -1 : (a > b) ? 1 : 0; } };
        ImQsort(sorted.Data, (size_t)sorted.Size, sizeof(double), Func::SampleComparerByValue);
        const double p50 = sorted[(int)(0.50 * (sorted.Size - 1))];
        const double p95 = sorted[(int)(0.95 * (sorted.Size - 1))];
        const double p99 = sorted[(int)(0.99 * (sorted.Size - 1))];
        printf("[harness] %s: %d/%d passed, %d frames, ms p50=%.3f p95=%.3f p99=%.3f max=%.3f\n",
               config->Name, count_success, count_tested, ft_ms.Size, p50, p95, p99, sorted[sorted.Size - 1]);
    }

    // The recording verifies ITSELF: extract the embedded meta stream back out of the
    // finalized take and recompute the integrity ladder. Headless verification is the
    // default -- a failure here fails the run and pins the criterion in the WAL.
    bool verify_failed = false;
    if (config->VerifyRecording && video_path[0] != 0)
    {
        ImVector<char> meta;
        bool extracted = false;
        if (video_is_qoi_dir)
            extracted = ImGuiApp_ImplQoi_ExtractEmbeddedMeta(video_path, embed_rows, &meta);
#ifdef IMGUIX_HAS_LIBAV
        else
            extracted = ImGuiApp_ImplLibav_ExtractEmbeddedMeta(video_path, embed_rows, &meta);
#endif
        ImGuiAppAVStreamStats stats;
        const bool verified = extracted && AppAVMetaVerify(meta.Data, meta.Size, &stats);
        verify_failed = !verified;
        printf("[harness] verify %s: frames=%d io_frames=%d ticks=%llu..%llu gaps=%d identities=%d "
               "input_hdrs=%d input_frames=%d snapshots=%d chain=%s digest=%s\n",
               verified ? "OK" : "FAILED",
               stats.Frames, stats.IoFrames,
               (unsigned long long)stats.FirstTick, (unsigned long long)stats.LastTick, stats.TickGaps,
               stats.Identities, stats.InputHdrs, stats.InputFrames, stats.Snapshots,
               stats.ChainOk ? "ok" : "BROKEN",
               stats.DigestState == 0 ? "ok" : stats.DigestState == 1 ? "MISSING" : "MISMATCH");
        if (!extracted)
            AppWALWrite(&wal, ImGuiAppWALLevel_Lifecycle, "harness: verify FAILED -- no embedded meta stream extracted");
        else if (verify_failed)
            AppWALWrite(&wal, ImGuiAppWALLevel_Lifecycle,
                        "harness: verify FAILED -- frames=%d io_frames=%d gaps=%d identities=%d chain_diverges_at=%d digest_state=%d",
                        stats.Frames, stats.IoFrames, stats.TickGaps, stats.Identities, stats.ChainDivergesAt, stats.DigestState);
    }
    const bool run_passed = all_passed && !verify_failed;

    SetAppAssertWAL(nullptr);
    app->WAL = backup_wal;
    AppWALClose(&wal);

    if (run_passed && !config->KeepArtifactsOnPass)
    {
        if (video_path[0] != 0)
            HarnessRemoveRecordingArtifacts(video_path, video_is_qoi_dir);
        remove(csv_path);
        if (wal_open)
            remove(wal_path);
    }

    return run_passed ? 0 : 1;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif // IMGUI_ENABLE_TEST_ENGINE
#endif // #ifndef IMGUI_DISABLE
