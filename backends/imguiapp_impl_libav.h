// ImGuiAppAV encoder backend: libav* (linked ffmpeg SDK, scripts/get-ffmpeg.ps1 ->
// third_party/ffmpeg). True VFR with EXACT per-frame PTS: AVFrame->pts carries
// FrameID.TimeSec directly (microsecond timebase) -- no wallclock residual, unlike the
// process-pipe backend. GPL SDK variant links libx264; distributing binaries linked
// against it is GPL.
// SupportsRealtimePts: true (exact).

#pragma once

#include "imguiapp_av.h"

IMGUI_API ImGuiAppAVEncoder* ImGuiApp_ImplLibav_CreateEncoder();

// Decode a recording and reassemble the input log stamped into its frames
// (ImGuiAppAVEncodeConfig::EmbedInputLog). Frames whose checksum fails are skipped and
// counted in out_corrupt_frames (null ok). False when the file has no embedded log.
IMGUI_API bool ImGuiApp_ImplLibav_ReadEmbeddedInputLog(const char* video_path, int embed_rows, ImGuiAppInputLog* out_log, int* out_corrupt_frames);
