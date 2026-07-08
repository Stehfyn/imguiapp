// dear imgui app: Renderer Host for Win32 + OpenGL3/WGL (composes imgui_impl_win32 + imgui_impl_opengl3)
// This needs to be used along with the Win32 Platform Host (imguiapp_impl_win32: shared WndProc + message-pump run loop).

// Implemented features:
//  [X] Renderer: exposed ImGuiApp_ImplWin32OpenGL3_* frame lifecycle (imgui impl pattern), driven by ImGuiApp's frame phases.
//  [X] Platform: window/WGL-context creation + ImGui context ownership in InitPlatform/ShutdownPlatform.
//  [X] Multi-viewport: per-viewport WGL windows; pacing-aware per-viewport present skip.
//  [X] AV: synchronous glReadPixels CaptureFrame (GL 1.1 entry points only; stalls the pipeline).
// Missing features:
//  [ ] Headless modes (use win32-vulkan).

#pragma once
#include "imguiapp.h"      // IMGUI_API, ImGuiApp, ImGuiAppConfig, ImGuiAppFrameConfig
#ifndef IMGUI_DISABLE

// Initialization data for ImGuiApp_ImplWin32OpenGL3_Init()
struct ImGuiApp_ImplWin32OpenGL3_InitInfo
{
    void*       Hwnd;        // HWND
    void*       MainDC;      // HDC of the main window
    void*       MainGLRC;    // HGLRC shared across viewports
    const char* GlslVersion; // nullptr = imgui_impl_opengl3 default
};

// Frame lifecycle (imgui impl pattern); registered on the seam via ImGuiAppPlatformBackend.
IMGUI_API bool ImGuiApp_ImplWin32OpenGL3_Init(const ImGuiApp_ImplWin32OpenGL3_InitInfo* init_info);
IMGUI_API void ImGuiApp_ImplWin32OpenGL3_Shutdown();
IMGUI_API void ImGuiApp_ImplWin32OpenGL3_NewFrame();
IMGUI_API void ImGuiApp_ImplWin32OpenGL3_RenderDrawData(ImDrawData* draw_data, const ImGuiAppFrameConfig* config);
IMGUI_API void ImGuiApp_ImplWin32OpenGL3_PresentFrame(const ImGuiAppFrameConfig* config);

IMGUI_API bool ImGuiApp_ImplWin32OpenGL3_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config);
IMGUI_API void ImGuiApp_ImplWin32OpenGL3_ShutdownPlatform(ImGuiApp* app);

#endif // #ifndef IMGUI_DISABLE
