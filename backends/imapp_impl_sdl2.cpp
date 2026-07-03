// Shared SDL2/emscripten platform layer: canvas sizing + emscripten main loop.
// Renderer-agnostic; linked alongside whichever SDL2 renderer backend the build selects.

#include "imgui_applayer.h"
#include "imapp_impl_sdl2_state.h"

#include "imgui_impl_sdl2.h"
#include <SDL.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <algorithm>

namespace
{
    void ResizeCanvasToCssSize(SDL_Window* window)
    {
        double css_width = 0.0;
        double css_height = 0.0;
        if (emscripten_get_element_css_size("#canvas", &css_width, &css_height) != EMSCRIPTEN_RESULT_SUCCESS)
            return;

        const double pixel_ratio = emscripten_get_device_pixel_ratio();
        const int canvas_width = std::max(1, (int)(css_width * pixel_ratio));
        const int canvas_height = std::max(1, (int)(css_height * pixel_ratio));

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
}

int ImGuiApp_ImplSDL2_RunLoop(ImGuiApp* app)
{
    emscripten_set_main_loop_arg([](void* ud)
    {
        ImGuiApp* app = static_cast<ImGuiApp*>(ud);
        ImGuiAppPlatformState* s = static_cast<ImGuiAppPlatformState*>(app->PlatformData);
        if (s == nullptr || !s->Running)
            return;

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
                event.window.windowID == SDL_GetWindowID(s->Window))
            {
                app->Shutdown();
                return;
            }
        }

        ResizeCanvasToCssSize(s->Window);
        app->Frame();
    }, app, 0, true);

    return 0;
}
