// dear imgui app: Renderer Host for SDL2 + OpenGL3 (composes imgui_impl_sdl2 + imgui_impl_opengl3)
// This needs to be used along with the SDL2 Platform Host (imguiapp_impl_sdl2: shared browser run loop).

// Implemented features:
//  [X] Renderer: exposed ImGuiApp_ImplSDL2OpenGL3_* frame lifecycle (imgui impl pattern), driven by ImGuiApp's frame phases.
//  [X] Platform: window/GL-context creation + ImGui context ownership in InitPlatform/ShutdownPlatform.
// Missing features:
//  [ ] AV: CaptureFrame readback (recording unavailable on this host; use win32-vulkan).

#pragma once
#include "imguiapp.h"      // IMGUI_API, ImGuiApp, ImGuiAppConfig, ImGuiAppFrameConfig
#ifndef IMGUI_DISABLE

// Initialization data for ImGuiApp_ImplSDL2OpenGL3_Init()
struct ImGuiApp_ImplSDL2OpenGL3_InitInfo
{
    void*       Window;      // SDL_Window*
    void*       GLContext;   // SDL_GLContext
    const char* GlslVersion; // nullptr = imgui_impl_opengl3 default
};

// Frame lifecycle (imgui impl pattern); registered on the seam via ImGuiAppPlatformBackend.
IMGUI_API bool ImGuiApp_ImplSDL2OpenGL3_Init(const ImGuiApp_ImplSDL2OpenGL3_InitInfo* init_info);
IMGUI_API void ImGuiApp_ImplSDL2OpenGL3_Shutdown();
IMGUI_API void ImGuiApp_ImplSDL2OpenGL3_NewFrame();
IMGUI_API void ImGuiApp_ImplSDL2OpenGL3_RenderDrawData(ImDrawData* draw_data, const ImGuiAppFrameConfig* config);
IMGUI_API void ImGuiApp_ImplSDL2OpenGL3_PresentFrame(const ImGuiAppFrameConfig* config);

IMGUI_API bool ImGuiApp_ImplSDL2OpenGL3_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config);
IMGUI_API void ImGuiApp_ImplSDL2OpenGL3_ShutdownPlatform(ImGuiApp* app);

#endif // #ifndef IMGUI_DISABLE
