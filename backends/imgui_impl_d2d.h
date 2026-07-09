// dear imgui: Renderer Backend for Direct2D (ID2D1DeviceContext)
// This needs to be used along with a Platform Backend (e.g. Win32)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'ID2D1Bitmap1*' as texture identifier. Read the FAQ about ImTextureID/ImTextureRef!
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (ImGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Texture updates support for dynamic font atlas (ImGuiBackendFlags_RendererHasTextures).
//  [X] Renderer: Textured axis-aligned quads (glyphs, images) drawn natively (FillOpacityMask / DrawBitmap).
// Missing features or Issues:
//  [ ] Renderer: Per-vertex color gradients are approximated (flat fill per triangle with the averaged vertex color).
//  [ ] Renderer: Multi-viewports support (secondary windows are the host's business; see imguiapp_impl_win32_dcomp).

// Important: RenderDrawData must be called between BeginDraw()/EndDraw() on the device context passed to
// Init, with the target bitmap already set -- Direct2D owns the pass bracket, this backend only records into it.

// You can use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#pragma once
#include "imgui.h"      // IMGUI_IMPL_API
#ifndef IMGUI_DISABLE

struct ID2D1DeviceContext;

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
IMGUI_IMPL_API bool ImGui_ImplD2D_Init(ID2D1DeviceContext* device_context);
IMGUI_IMPL_API void ImGui_ImplD2D_Shutdown();
IMGUI_IMPL_API void ImGui_ImplD2D_NewFrame();
IMGUI_IMPL_API void ImGui_ImplD2D_RenderDrawData(ImDrawData* draw_data);

// (Advanced) Use e.g. if you need to precisely control the timing of texture updates (e.g. for staged rendering), by setting ImDrawData::Textures = NULL to handle this manually.
IMGUI_IMPL_API void ImGui_ImplD2D_UpdateTexture(ImTextureData* tex);

// [BETA] Selected render state data shared with callbacks.
// This is temporarily stored in GetPlatformIO().Renderer_RenderState during the ImGui_ImplD2D_RenderDrawData() call.
// (Please open an issue if you feel you need access to more data)
struct ImGui_ImplD2D_RenderState
{
    ID2D1DeviceContext* DeviceContext;
};

#endif // #ifndef IMGUI_DISABLE
