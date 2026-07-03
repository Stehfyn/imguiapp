// ImGuiAppAV encoder backend: ffmpeg process pipe (the default video provider).
// Spawns ffmpeg and feeds rawvideo RGBA on stdin:
//   ffmpeg -f rawvideo -pix_fmt rgba -s WxH -r <fps> -i - <extra_args> <OutputPath>
// No link-time dependency; Open fails cleanly when the exe is absent.
// SupportsRealtimePts: false -- rawvideo stdin has no PTS channel. Under Realtime
// timing each captured frame is emitted round(real_delta * Fps) times into a CFR
// stream (honest to within 1/Fps); Close writes <OutputPath>.remux.txt with the
// concat-demuxer command that rebuilds an exact-VFR file from the sidecar timestamps.

#pragma once

#include "imguiapp_av.h"

// ffmpeg_exe = nullptr searches PATH. extra_args = nullptr -> "-c:v libx264 -preset veryfast -crf 18".
IMGUI_API ImGuiAppAVEncoder* ImGuiAppAV_CreateFfmpegEncoder(const char* ffmpeg_exe, const char* extra_args);
