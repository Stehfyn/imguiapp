// dear imgui app: Renderer Host for Win32 + Vulkan (composes imgui_impl_win32 + imgui_impl_vulkan)
// This needs to be used along with the Win32 Platform Host (imguiapp_impl_win32: shared WndProc + message-pump run loop).

// Implemented features:
//  [X] Renderer: exposed ImGuiApp_ImplWin32Vulkan_* frame lifecycle (imgui impl pattern), driven by ImGuiApp's frame phases.
//  [X] Platform: window/instance/device/swapchain creation + ImGui context ownership in InitPlatform/ShutdownPlatform.
//  [X] Multi-viewport: upstream vulkan viewport hooks wrapped for pacing-aware per-viewport present skip.
//  [X] AV: pipelined staging-buffer CaptureFrame (no pipeline stall; FrameID travels with the pixels).
//  [X] Headless: Offscreen render target behind a hidden input window (ImGuiAppHeadlessMode_Offscreen).

#pragma once
#include "imguiapp.h"      // IMGUI_API, ImGuiApp, ImGuiAppConfig, ImGuiAppFrameConfig, ImGuiAppHeadlessMode
#ifndef IMGUI_DISABLE

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

#endif // #ifndef IMGUI_DISABLE
