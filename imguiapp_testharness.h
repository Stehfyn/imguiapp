// ImGuiAppTestHarness: Test Engine + headless rendering + frame encoding + WAL in one
// entry point, all sharing ImGuiAppFrameID (docs/av-design.md section on the harness).
// The only applayer header that drags the imgui test engine; apps that never test
// don't include it.

#pragma once

#include "imguiapp_av.h"

struct ImGuiTestEngine;

struct ImGuiAppTestHarnessConfig
{
  const char*          Name;                 // artifact base name
  const char*          ArtifactDir;          // receives <Name>.mp4/.avmeta/.wal/.frametimes.csv
  ImGuiAppHeadlessMode Headless;             // Offscreen = GPU pixels without a window (video on CI)
  bool                 RecordVideo;          // requires Offscreen or a windowed app
  bool                 KeepArtifactsOnPass;  // false: artifacts survive only failures
  ImGuiAppPacerMode    PacerMode;            // Fixed = Reproduce posture; Target/Off = Witness (honest-clock) posture
  float                Fps;                  // Fixed pacer rate / Constant-timing rate
  ImGuiAppAVTimingMode Timing;               // Auto: Fixed pacer -> Constant video, else Realtime
  ImGuiAppAVEncoder*   Encoder;              // null = harness default: libav when the SDK is linked, else QOI sequence
  ImGuiAppWALLevel     WALLevel;
  const char*          TestFilter;           // test-engine filter; null = all
  void (*RegisterTests)(ImGuiTestEngine* engine);   // required

  ImGuiAppTestHarnessConfig()
  {
    Name = nullptr;
    ArtifactDir = nullptr;
    Headless = ImGuiAppHeadlessMode_Offscreen;
    RecordVideo = true;
    KeepArtifactsOnPass = false;
    PacerMode = ImGuiAppPacerMode_Fixed;
    Fps = 60.0f;
    Timing = ImGuiAppAVTimingMode_Auto;
    Encoder = nullptr;
    WALLevel = ImGuiAppWALLevel_Frame;
    TestFilter = nullptr;
    RegisterTests = nullptr;
  }
};

namespace ImGui
{
  // Runs the queued tests to completion (or abort) and returns a ctest-ready exit code.
  // Per frame: AppPacerWait -> FrameID -> NewFrame -> app frame -> render -> CaptureFrame ->
  // recorder -> PreSwap/present/PostSwap. The WAL's FrameID source is set, so every
  // event-source line carries the frame index that names the video frame.
  IMGUI_API int AppTestHarnessRun(ImGuiApp* app, const ImGuiAppTestHarnessConfig* config);
}
