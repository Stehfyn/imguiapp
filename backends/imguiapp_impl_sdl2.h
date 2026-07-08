// dear imgui app: Platform Host for SDL2/Emscripten (canvas sizing + browser main loop)
// This needs to be used along with a sibling Renderer Host (imguiapp_impl_sdl2_opengl3, imguiapp_impl_sdl2_wgpu)
// Implemented features:
//  [X] Platform: emscripten main loop driving app->Frame(); CSS-driven canvas resize.

#pragma once
#include "imguiapp.h"      // ImGuiApp

// Shared SDL2/emscripten main loop (renderer-agnostic).
int ImGuiApp_ImplSDL2_RunLoop(ImGuiApp* app);
