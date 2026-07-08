// ImGuiAppAV encoder backend: Windows Media Foundation mp4 (H.264/HEVC).
// No external exe; the only TU linking mfplat/mfreadwrite.
// SupportsRealtimePts: true (per-sample timestamps are native to IMFSinkWriter).
// Explicit choice, never a silent default: lossy + driver-variant output is wrong
// for test artifacts.

#pragma once

#include "imguiapp_internal.h"
#ifndef IMGUI_DISABLE

IMGUI_API ImGuiAppAVEncoder* ImGuiApp_ImplMediaFoundation_CreateEncoder();

#endif // #ifndef IMGUI_DISABLE
