// dear imgui app: Renderer Host for Win32 + Vulkan (composes imgui_impl_win32 + imgui_impl_vulkan)
// This needs to be used along with the Win32 Platform Host (imguiapp_impl_win32: shared WndProc + message-pump run loop).

// Implemented features:
//  [X] Renderer: exposed ImGuiApp_ImplWin32Vulkan_* frame lifecycle (imgui impl pattern), driven by ImGuiApp's frame phases.
//  [X] Platform: window/instance/device/swapchain creation + ImGui context ownership in InitPlatform/ShutdownPlatform.
//  [X] Multi-viewport: upstream vulkan viewport hooks wrapped for pacing-aware per-viewport present skip.
//  [X] AV: pipelined staging-buffer CaptureFrame (no pipeline stall; FrameID travels with the pixels).
//  [X] Headless: Offscreen render target behind a hidden input window (ImGuiAppHeadlessMode_Offscreen).

// You can use unmodified imguiapp_impl_* files in your project. See demos/ folder for examples of using this.
// Prefer including the entire imguiapp/ folder into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

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

// Frame lifecycle (imgui impl pattern, app-threaded); registered on the seam via ImGuiAppPlatformBackend.
// Backend data lives in app->BackendData (io.BackendXxxUserData analog; both io slots belong to the wrapped imgui backends).
IMGUI_API bool ImGuiApp_ImplWin32Vulkan_Init(ImGuiApp* app, const ImGuiApp_ImplWin32Vulkan_InitInfo* init_info);
IMGUI_API void ImGuiApp_ImplWin32Vulkan_Shutdown(ImGuiApp* app);
IMGUI_API void ImGuiApp_ImplWin32Vulkan_NewFrame(ImGuiApp* app);
IMGUI_API void ImGuiApp_ImplWin32Vulkan_RenderDrawData(ImGuiApp* app, ImDrawData* draw_data, const ImGuiAppFrameConfig* config);
IMGUI_API void ImGuiApp_ImplWin32Vulkan_PresentFrame(ImGuiApp* app, const ImGuiAppFrameConfig* config);

IMGUI_API bool ImGuiApp_ImplWin32Vulkan_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config);
IMGUI_API void ImGuiApp_ImplWin32Vulkan_ShutdownPlatform(ImGuiApp* app);

// This host's seam vtable; the app's wiring hands it to the app layer as ImGuiAppGetPlatformBackend().
IMGUI_API const ImGuiAppPlatformBackend* ImGuiApp_ImplWin32Vulkan_GetPlatformBackend();

#endif // #ifndef IMGUI_DISABLE
