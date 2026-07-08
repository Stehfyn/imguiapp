// dear imgui app: AV Encoder Backend for Windows Media Foundation mp4 (H.264/HEVC)
// This needs to be used along with the recorder (ImGui::AppRecordBegin) and a capture-capable Renderer Host.

// Implemented features:
//  [X] AV: mp4 output with no external exe; the only TU linking mfplat/mfreadwrite.
//  [X] AV: Realtime PTS (per-sample timestamps are native to IMFSinkWriter; SupportsRealtimePts true).

// Explicit choice, never a silent default: lossy + driver-variant output is wrong
// for test artifacts.

#pragma once

#include "imguiapp_internal.h"
#ifndef IMGUI_DISABLE

IMGUI_API ImGuiAppAVEncoder* ImGuiApp_ImplMediaFoundation_CreateEncoder();

#endif // #ifndef IMGUI_DISABLE
