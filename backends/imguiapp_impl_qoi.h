// ImGuiAppAV encoder backend: QOI image sequence.
// Zero-dependency, lossless, byte-stable across machines -- the CI/golden-image
// provider. Output: <OutputPath>/NNNNNN.qoi + index.tsv (frame_index, time_sec).
// SupportsRealtimePts: true (index.tsv carries per-frame wall-clock time exactly).
// QOI encoding implemented from the specification (qoiformat.org), no third-party code.

#pragma once

#include "imguiapp_av.h"

IMGUI_API ImGuiAppAVEncoder* ImGuiApp_ImplQoi_CreateEncoder();
