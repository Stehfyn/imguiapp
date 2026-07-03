// ImGuiAppTestHarness (imguiapp_testharness.h): Test Engine + headless rendering +
// frame encoding + WAL, one entry point, one frame id across every artifact
// (docs/av-design.md). The only applayer TU that links the imgui test engine.

#include "imguiapp_testharness.h"
#ifdef IMGUIX_HAS_LIBAV
#include "backends/imguiapp_impl_libav.h"
#endif
#include "backends/imguiapp_impl_qoi.h"
#include "imgui_te_engine.h"
#include "imgui_te_ui.h"
#include "imgui_internal.h"   // ImFormatString

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <algorithm>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace
{
  bool HarnessMkdirOne(const char* path)
  {
#ifdef _WIN32
    const int rc = _mkdir(path);
#else
    const int rc = mkdir(path, 0755);
#endif
    return rc == 0 || errno == EEXIST;
  }

  bool HarnessMkdirRecursive(const char* path)
  {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char* c = buf + 1; *c; c++)
    {
      if (*c != '/' && *c != '\\')
        continue;
      const char sep = *c;
      *c = 0;
      if (buf[0] != 0 && buf[strlen(buf) - 1] != ':' && !HarnessMkdirOne(buf))
        return false;
      *c = sep;
    }
    return HarnessMkdirOne(buf);
  }

  // Best-effort removal of one recording's artifacts. A QOI take is a directory
  // (NNNNNN.qoi + index.tsv); everything else is "<video>" plus recorder/provider
  // side files.
  void HarnessRemoveRecordingArtifacts(const char* video_path, bool is_qoi_dir)
  {
    char path[600];
    if (is_qoi_dir)
    {
      for (int i = 0; ; i++)
      {
        snprintf(path, sizeof(path), "%s/%06d.qoi", video_path, i);
        if (remove(path) != 0)
          break;
      }
      snprintf(path, sizeof(path), "%s/index.tsv", video_path);
      remove(path);
#ifdef _WIN32
      _rmdir(video_path);
#else
      rmdir(video_path);
#endif
    }
    else
    {
      remove(video_path);
    }
    snprintf(path, sizeof(path), "%s.avmeta", video_path);
    remove(path);
    snprintf(path, sizeof(path), "%s.ffmpeg.log", video_path);
    remove(path);
    snprintf(path, sizeof(path), "%s.remux.txt", video_path);
    remove(path);
  }
}

IMGUI_API int ImGui::AppTestHarnessRun(ImGuiApp* app, const ImGuiAppTestHarnessConfig* config)
{
  IM_ASSERT(app != nullptr && config != nullptr);
  if (app == nullptr || config == nullptr)
    return 2;
  IM_ASSERT(config->Name != nullptr && config->RegisterTests != nullptr && "harness config requires Name and RegisterTests");
  if (config->Name == nullptr || config->RegisterTests == nullptr)
    return 2;

  const char* artifact_dir = config->ArtifactDir != nullptr ? config->ArtifactDir : ".";
  if (!HarnessMkdirRecursive(artifact_dir))
  {
    fprintf(stderr, "[harness] cannot create artifact dir '%s'\n", artifact_dir);
    return 2;
  }

  // WAL first: every later step logs through it, frame-id prefixed.
  char wal_path[560];
  ImFormatString(wal_path, IM_ARRAYSIZE(wal_path), "%s/%s.wal", artifact_dir, config->Name);
  ImGuiAppWAL wal;
  const bool wal_open = AppWALOpen(&wal, wal_path, config->WALLevel);
  wal.FrameID = &app->FrameID;
  ImGuiAppWAL* prev_wal = app->WAL;
  app->WAL = wal_open ? &wal : prev_wal;
  SetAppAssertWAL(wal_open ? &wal : nullptr);

  app->Pacer.Mode = config->PacerMode;
  app->Pacer.TargetHz = config->Fps;

  ImGuiAppConfig app_config;
  app_config.Platform.Name   = config->Name;
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
  if (!initialized)
  {
    fprintf(stderr, "[harness] app initialization failed\n");
    SetAppAssertWAL(nullptr);
    app->WAL = prev_wal;
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
  if (config->RecordVideo)
  {
    ImGuiAppAVEncodeConfig enc_config;
    enc_config.Fps = config->Fps;
    enc_config.Timing = config->Timing;
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
      encoder = ImGuiAppAV_CreateLibavEncoder();
      ImFormatString(video_path, IM_ARRAYSIZE(video_path), "%s/%s.mp4", artifact_dir, config->Name);
      enc_config.OutputPath = video_path;
      recorder = AppRecordBegin(app, encoder, &enc_config);
      if (recorder == nullptr)
        ImGuiAppAV_DestroyEncoder(encoder);
#endif
      if (recorder == nullptr)
      {
        encoder = ImGuiAppAV_CreateQoiSequenceEncoder();
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
        ImGuiAppAV_DestroyEncoder(encoder);
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
  ImU64 prev_tsc = 0;
  double prev_sec = 0.0;
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
      ft_tsc.push_back(app->FrameID.Tsc - prev_tsc);
      ft_ms.push_back((app->FrameID.TimeSec - prev_sec) * 1000.0);
    }
    prev_tsc = app->FrameID.Tsc;
    prev_sec = app->FrameID.TimeSec;

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
    ImGuiAppAV_DestroyEncoder(encoder);
  app->Shutdown();
  ImGuiTestEngine_DestroyContext(engine);

  // Frame-time artifact + console percentiles (real per-frame cost in every posture).
  char csv_path[560];
  ImFormatString(csv_path, IM_ARRAYSIZE(csv_path), "%s/%s.frametimes.csv", artifact_dir, config->Name);
  if (FILE* csv = fopen(csv_path, "w"))
  {
    fprintf(csv, "frame_index\ttsc_delta\tms\n");
    for (int i = 0; i < ft_ms.Size; i++)
      fprintf(csv, "%llu\t%llu\t%.4f\n", (unsigned long long)ft_frame[i], (unsigned long long)ft_tsc[i], ft_ms[i]);
    fclose(csv);
  }
  if (ft_ms.Size > 0)
  {
    ImVector<double> sorted = ft_ms;
    std::sort(sorted.begin(), sorted.end());
    const double p50 = sorted[(int)(0.50 * (sorted.Size - 1))];
    const double p95 = sorted[(int)(0.95 * (sorted.Size - 1))];
    const double p99 = sorted[(int)(0.99 * (sorted.Size - 1))];
    printf("[harness] %s: %d/%d passed, %d frames, ms p50=%.3f p95=%.3f p99=%.3f max=%.3f\n",
           config->Name, count_success, count_tested, ft_ms.Size, p50, p95, p99, sorted[sorted.Size - 1]);
  }

  SetAppAssertWAL(nullptr);
  app->WAL = prev_wal;
  AppWALClose(&wal);

  if (all_passed && !config->KeepArtifactsOnPass)
  {
    if (video_path[0] != 0)
      HarnessRemoveRecordingArtifacts(video_path, video_is_qoi_dir);
    remove(csv_path);
    if (wal_open)
      remove(wal_path);
  }

  return all_passed ? 0 : 1;
}
