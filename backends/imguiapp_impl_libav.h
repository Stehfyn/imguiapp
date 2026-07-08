// ImGuiAppAV encoder backend: libav* (linked ffmpeg SDK, scripts/get-ffmpeg.ps1 ->
// third_party/ffmpeg). True VFR with EXACT per-frame PTS: AVFrame->pts carries
// FrameID.TimeSec directly (microsecond timebase) -- no wallclock residual, unlike the
// process-pipe backend. GPL SDK variant links libx264; distributing binaries linked
// against it is GPL.
// SupportsRealtimePts: true (exact).

#pragma once

#include "imguiapp_internal.h"
#ifndef IMGUI_DISABLE

IMGUI_API ImGuiAppAVEncoder* ImGuiApp_ImplLibav_CreateEncoder();

// Decode a recording and reassemble the meta stream chunked across its frames'
// strips (40-byte header + framed records; parse with the ImGui::AppAVMeta* seam
// readers). A corrupt frame truncates the stream at that point -- crash-honest.
IMGUI_API bool ImGuiApp_ImplLibav_ExtractEmbeddedMeta(const char* video_path, int embed_rows, ImVector<char>* out_meta);

// Decode ONE frame image on demand (the playback debugger's F63 scrub side): linearly
// decode to frame_ordinal (== ImGuiAppRunTick::FrameImage) and fill out_rgba with tightly
// packed RGBA8. The mp4 counterpart of ImGuiApp_ImplQoi_DecodeFrame.
IMGUI_API bool ImGuiApp_ImplLibav_DecodeFrame(const char* video_path, int frame_ordinal, ImVector<char>* out_rgba, int* out_w, int* out_h);

// Extract + parse the input log in one call; out_corrupt_frames (null ok) counts
// checksum-failed frames. False when the file carries no embedded stream or no log.
IMGUI_API bool ImGuiApp_ImplLibav_ReadEmbeddedInputLog(const char* video_path, int embed_rows, ImGuiAppInputLog* out_log, int* out_corrupt_frames);

#endif // #ifndef IMGUI_DISABLE
