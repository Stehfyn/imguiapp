// ImGuiAppAV encoder backend: libav* (linked ffmpeg SDK, scripts/get-ffmpeg.ps1 ->
// third_party/ffmpeg). True VFR with EXACT per-frame PTS: AVFrame->pts carries
// FrameID.TimeSec directly (microsecond timebase) -- no wallclock residual, unlike the
// process-pipe backend. GPL SDK variant links libx264; distributing binaries linked
// against it is GPL.
// SupportsRealtimePts: true (exact).

#pragma once

#include "imguiapp_av.h"

IMGUI_API ImGuiAppAVEncoder* ImGuiApp_ImplLibav_CreateEncoder();

// Decode a recording and reassemble the meta stream chunked across its frames'
// strips (40-byte header + framed records; parse with the ImGui::AppAVMeta* seam
// readers). A corrupt frame truncates the stream at that point -- crash-honest.
IMGUI_API bool ImGuiApp_ImplLibav_ExtractEmbeddedMeta(const char* video_path, int embed_rows, ImVector<char>* out_meta);

// Extract + parse the input log in one call; out_corrupt_frames (null ok) counts
// checksum-failed frames. False when the file carries no embedded stream or no log.
IMGUI_API bool ImGuiApp_ImplLibav_ReadEmbeddedInputLog(const char* video_path, int embed_rows, ImGuiAppInputLog* out_log, int* out_corrupt_frames);
