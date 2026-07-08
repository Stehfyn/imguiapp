// dear imgui app: Platform Host for SDL2/Emscripten (canvas sizing + browser main loop)
// This needs to be used along with a sibling Renderer Host (imguiapp_impl_sdl2_opengl3, imguiapp_impl_sdl2_wgpu)

// CHANGELOG
//  2026-07-08: Backend-internal symbols prefixed; IMGUI_DISABLE guards added.

#include "imguiapp.h"
#include "imguiapp_impl_sdl2.h"
#ifndef IMGUI_DISABLE

#include "imgui_impl_sdl2.h"
#include <SDL.h>
#include <emscripten.h>
#include <emscripten/html5.h>

static void ImGuiApp_ImplSDL2_ResizeCanvasToCssSize(SDL_Window* window)
{
    double css_width = 0.0;
    double css_height = 0.0;
    if (emscripten_get_element_css_size("#canvas", &css_width, &css_height) != EMSCRIPTEN_RESULT_SUCCESS)
        return;

    const double pixel_ratio = emscripten_get_device_pixel_ratio();
    const int canvas_width = ImMax(1, (int)(css_width * pixel_ratio));
    const int canvas_height = ImMax(1, (int)(css_height * pixel_ratio));

    int current_width = 0;
    int current_height = 0;
    emscripten_get_canvas_element_size("#canvas", &current_width, &current_height);
    if (current_width != canvas_width || current_height != canvas_height)
    {
        emscripten_set_canvas_element_size("#canvas", canvas_width, canvas_height);
        if (window != nullptr)
            SDL_SetWindowSize(window, (int)css_width, (int)css_height);
    }
}

int ImGuiApp_ImplSDL2_RunLoop(ImGuiApp* app)
{
    emscripten_set_main_loop_arg([](void* ud)
    {
        ImGuiApp* app = (ImGuiApp*)ud;
        if (app->PlatformData == nullptr) // not initialized, or already shut down
            return;

        SDL_Window* window = (SDL_Window*)app->Platform.NativeWindowHandle;
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
            {
                app->Shutdown();
                return;
            }
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window))
            {
                app->Shutdown();
                return;
            }
        }

        ImGuiApp_ImplSDL2_ResizeCanvasToCssSize(window);
        app->Frame();
    }, app, 0, true);

    return 0;
}

#endif // #ifndef IMGUI_DISABLE
