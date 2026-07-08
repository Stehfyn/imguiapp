#pragma once
#include "imguiapp.h"      // IMGUI_API, ImGuiApp, ImGuiAppConfig, ImGuiAppFrameConfig, ImGuiAppHeadlessMode

// Initialization data for ImGuiApp_ImplWin32Vulkan_Init()
struct ImGuiApp_ImplWin32Vulkan_InitInfo
{
    void*                Hwnd;             // HWND
    unsigned int         MinImageCount;    // clamped to >= 3
    bool                 EnableValidation;
    ImGuiAppHeadlessMode Headless;
    int                  OffscreenWidth;   // Offscreen mode render target size
    int                  OffscreenHeight;
};

// Frame lifecycle (imgui impl pattern); registered on the seam via ImGuiAppPlatformBackend.
IMGUI_API bool ImGuiApp_ImplWin32Vulkan_Init(const ImGuiApp_ImplWin32Vulkan_InitInfo* init_info);
IMGUI_API void ImGuiApp_ImplWin32Vulkan_Shutdown();
IMGUI_API void ImGuiApp_ImplWin32Vulkan_NewFrame();
IMGUI_API void ImGuiApp_ImplWin32Vulkan_RenderDrawData(ImDrawData* draw_data, const ImGuiAppFrameConfig* config);
IMGUI_API void ImGuiApp_ImplWin32Vulkan_PresentFrame(const ImGuiAppFrameConfig* config);

IMGUI_API bool ImGuiApp_ImplWin32Vulkan_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config);
IMGUI_API void ImGuiApp_ImplWin32Vulkan_ShutdownPlatform(ImGuiApp* app);
