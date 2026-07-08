// dear imgui app: Platform Host for SDL2/Emscripten (canvas sizing + browser main loop)
// This needs to be used along with a sibling Renderer Host (imguiapp_impl_sdl2_opengl3, imguiapp_impl_sdl2_wgpu)
// Implemented features:
//  [X] Platform: emscripten main loop driving app->Frame(); CSS-driven canvas resize.

// You can use unmodified imguiapp_impl_* files in your project. See demos/ folder for examples of using this.
// Prefer including the entire imguiapp/ folder into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#pragma once
#include "imguiapp.h"      // ImGuiApp
#ifndef IMGUI_DISABLE

// Shared SDL2/emscripten main loop (renderer-agnostic).
IMGUI_API int ImGuiApp_ImplSDL2_RunLoop(ImGuiApp* app);

#endif // #ifndef IMGUI_DISABLE
