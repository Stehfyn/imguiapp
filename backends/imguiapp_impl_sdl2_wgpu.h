#pragma once
#include "imguiapp.h"      // IMGUI_API, ImGuiApp, ImGuiAppConfig, ImGuiAppFrameConfig

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
