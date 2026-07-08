// dear imgui app: Renderer Host for SDL2 + WebGPU/Dawn (composes imgui_impl_sdl2 + imgui_impl_wgpu)
// This needs to be used along with the SDL2 Platform Host (imguiapp_impl_sdl2: shared browser run loop).

// Implemented features:
//  [X] Renderer: exposed ImGuiApp_ImplSDL2WGPU_* frame lifecycle (imgui impl pattern; RenderDrawData submits AND presents -- no PresentFrame hook).
//  [X] Platform: window/surface/device creation + ImGui context ownership in InitPlatform/ShutdownPlatform.
// Missing features:
//  [ ] AV: CaptureFrame readback (recording unavailable on this host; use win32-vulkan).

// You can use unmodified imguiapp_impl_* files in your project. See demos/ folder for examples of using this.
// Prefer including the entire imguiapp/ folder into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#pragma once
#include "imguiapp.h"      // IMGUI_API, ImGuiApp, ImGuiAppConfig, ImGuiAppFrameConfig
#ifndef IMGUI_DISABLE

// Initialization data for ImGuiApp_ImplSDL2WGPU_Init()
struct ImGuiApp_ImplSDL2WGPU_InitInfo
{
    void*       Window;         // SDL_Window*
    const char* CanvasSelector; // nullptr = "#canvas"
};

// Frame lifecycle (imgui impl pattern, app-threaded); registered on the seam via ImGuiAppPlatformBackend.
// Backend data lives in app->BackendData (io.BackendXxxUserData analog; both io slots belong to the wrapped imgui backends).
// No PresentFrame: RenderDrawData submits/presents (legacy single-hook).
IMGUI_API bool ImGuiApp_ImplSDL2WGPU_Init(ImGuiApp* app, const ImGuiApp_ImplSDL2WGPU_InitInfo* init_info);
IMGUI_API void ImGuiApp_ImplSDL2WGPU_Shutdown(ImGuiApp* app);
IMGUI_API void ImGuiApp_ImplSDL2WGPU_NewFrame(ImGuiApp* app);
IMGUI_API void ImGuiApp_ImplSDL2WGPU_RenderDrawData(ImGuiApp* app, ImDrawData* draw_data, const ImGuiAppFrameConfig* config);

IMGUI_API bool ImGuiApp_ImplSDL2WGPU_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config);
IMGUI_API void ImGuiApp_ImplSDL2WGPU_ShutdownPlatform(ImGuiApp* app);

#endif // #ifndef IMGUI_DISABLE
