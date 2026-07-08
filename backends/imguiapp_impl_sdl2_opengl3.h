#pragma once
#include "imguiapp.h"      // IMGUI_API, ImGuiApp, ImGuiAppConfig, ImGuiAppFrameConfig

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
