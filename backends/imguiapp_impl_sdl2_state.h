#pragma once
#include <SDL.h>

struct ImGuiApp;

struct ImGuiAppPlatformState
{
    SDL_Window*   Window;
    SDL_GLContext GLContext;
    bool          Running;
};

// Shared SDL2/emscripten main loop (renderer-agnostic). Defined in imguiapp_impl_sdl2.cpp.
int ImGuiApp_ImplSDL2_RunLoop(ImGuiApp* app);