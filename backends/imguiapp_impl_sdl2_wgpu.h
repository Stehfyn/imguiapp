// dear imgui app: Renderer Host for SDL2 + WebGPU/Dawn (composes imgui_impl_sdl2 + imgui_impl_wgpu)
// This needs to be used along with the SDL2 Platform Host (imguiapp_impl_sdl2: shared browser run loop).

// Implemented features:
//  [X] Renderer: exposed ImGuiApp_ImplSDL2WGPU_* frame lifecycle (imgui impl pattern; RenderDrawData submits AND presents -- no PresentFrame hook).
//  [X] Platform: window/surface/device creation + ImGui context ownership in InitPlatform/ShutdownPlatform.
// Missing features:
//  [ ] AV: CaptureFrame readback (recording unavailable on this host; use win32-vulkan).

#pragma once
#include "imguiapp.h"      // IMGUI_API, ImGuiApp, ImGuiAppConfig, ImGuiAppFrameConfig
#ifndef IMGUI_DISABLE

// Initialization data for ImGuiApp_ImplSDL2WGPU_Init()
struct ImGuiApp_ImplSDL2WGPU_InitInfo
{
    void*       Window;         // SDL_Window*
    const char* CanvasSelector; // nullptr = "#canvas"
};

// Frame lifecycle (imgui impl pattern); registered on the seam via ImGuiAppPlatformBackend.
// No PresentFrame: RenderDrawData submits/presents (legacy single-hook).
IMGUI_API bool ImGuiApp_ImplSDL2WGPU_Init(const ImGuiApp_ImplSDL2WGPU_InitInfo* init_info);
IMGUI_API void ImGuiApp_ImplSDL2WGPU_Shutdown();
IMGUI_API void ImGuiApp_ImplSDL2WGPU_NewFrame();
IMGUI_API void ImGuiApp_ImplSDL2WGPU_RenderDrawData(ImDrawData* draw_data, const ImGuiAppFrameConfig* config);

IMGUI_API bool ImGuiApp_ImplSDL2WGPU_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config);
IMGUI_API void ImGuiApp_ImplSDL2WGPU_ShutdownPlatform(ImGuiApp* app);

#endif // #ifndef IMGUI_DISABLE
