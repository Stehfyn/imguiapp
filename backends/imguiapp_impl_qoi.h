// ImGuiAppAV encoder backend: QOI image sequence.
// Zero-dependency, lossless, byte-stable across machines -- the CI/golden-image
// provider. Output: <OutputPath>/NNNNNN.qoi + index.tsv (frame_index, time_sec).
// SupportsRealtimePts: true (index.tsv carries per-frame wall-clock time exactly).
// QOI encoding implemented from the specification (qoiformat.org), no third-party code.

#pragma once

#include "imguiapp_internal.h"
#ifndef IMGUI_DISABLE

IMGUI_API ImGuiAppAVEncoder* ImGuiApp_ImplQoi_CreateEncoder();

// Reassemble the meta stream chunked across a take's frame strips: walks
// <dir>/NNNNNN.qoi in order, decodes, reads each strip. Lossless frames make the
// stream byte-exact; a corrupt/missing frame truncates it at that point.
IMGUI_API bool               ImGuiApp_ImplQoi_ExtractEmbeddedMeta(const char* dir, int embed_rows, ImVector<char>* out_meta);

// Decode ONE frame image on demand (the playback debugger's F63 scrub side): load
// <dir>/NNNNNN.qoi for frame_ordinal (== ImGuiAppRunTick::FrameImage) and fill out_rgba
// with tightly packed RGBA8. The per-backend counterpart of the mp4 sample decode.
IMGUI_API bool               ImGuiApp_ImplQoi_DecodeFrame(const char* dir, int frame_ordinal, ImVector<char>* out_rgba, int* out_w, int* out_h);

#endif // #ifndef IMGUI_DISABLE
