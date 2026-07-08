// dear imgui app: AV Encoder Backend for Windows Media Foundation mp4 (H.264/HEVC)
// This needs to be used along with the recorder (ImGui::AppRecordBegin) and a capture-capable Renderer Host.

// Implemented features:
//  [X] AV: mp4 output with no external exe; the only TU linking mfplat/mfreadwrite.
//  [X] AV: Realtime PTS (per-sample timestamps are native to IMFSinkWriter; SupportsRealtimePts true).

// Explicit choice, never a silent default: lossy + driver-variant output is wrong
// for test artifacts.

// You can use unmodified imguiapp_impl_* files in your project. See demos/ folder for examples of using this.
// Prefer including the entire imguiapp/ folder into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#pragma once

#include "imguiapp_internal.h"
#ifndef IMGUI_DISABLE

IMGUI_API ImGuiAppAVEncoder* ImGuiApp_ImplMediaFoundation_CreateEncoder();

#endif // #ifndef IMGUI_DISABLE
