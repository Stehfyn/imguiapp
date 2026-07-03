// Internal QOI codec, implemented from the specification (qoiformat.org). Not an
// encoder backend: a file-format utility shared by the QOI sequence encoder backend
// (backends/imguiapp_impl_qoi.cpp) and the flight-recorder ring (imguiapp_av.cpp).

#pragma once

#include "imgui.h"

// rgba is 8-bit RGBA; pitch_bytes = row stride. out receives a complete .qoi file image.
IMGUI_API bool ImGuiAppAV_QoiEncode(const void* rgba, int width, int height, int pitch_bytes, ImVector<char>* out);

// out_rgba receives tightly packed 8-bit RGBA (width * height * 4 bytes).
IMGUI_API bool ImGuiAppAV_QoiDecode(const void* qoi_bytes, int qoi_size, ImVector<char>* out_rgba, int* out_width, int* out_height);
