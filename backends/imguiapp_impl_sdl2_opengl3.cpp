// dear imgui app: Renderer Host for SDL2 + OpenGL3 (composes imgui_impl_sdl2 + imgui_impl_opengl3)
// This needs to be used along with the SDL2 Platform Host (imguiapp_impl_sdl2: shared browser run loop).

// Implemented features:
//  [X] Renderer: exposed ImGuiApp_ImplSDL2OpenGL3_* frame lifecycle (imgui impl pattern), driven by ImGuiApp's frame phases.
//  [X] Platform: window/GL-context creation + ImGui context ownership in InitPlatform/ShutdownPlatform.
// Missing features:
//  [ ] AV: CaptureFrame readback (recording unavailable on this host; use win32-vulkan).

// You can use unmodified imguiapp_impl_* files in your project. See demos/ folder for examples of using this.
// Prefer including the entire imguiapp/ folder into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2026-07-08: Lifecycle: Per-host PlatformData type + exported ImGuiApp_ImplSDL2OpenGL3_GetPlatformBackend(); hosts coexist in one binary (build-selected binding wires the active one).
//  2026-07-08: Docs: Header block conformed to the backend anatomy (B1/B2 grammar).
//  2026-07-08: Lifecycle: Threaded ImGuiApp* through the frame lifecycle; backend data moved to app->BackendData, file-scope backend global removed.
//  2026-07-08: Misc: Exposed ImGuiApp_ImplSDL2OpenGL3_* frame lifecycle (imgui impl pattern); host owns the ImGui context it creates; backend-internal symbols prefixed; IMGUI_DISABLE guards added.

#include "imguiapp.h"
#ifndef IMGUI_DISABLE
#include "imguiapp_impl_sdl2_opengl3.h"
#include "imguiapp_impl_sdl2.h"

#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>
#if defined(IMGUI_IMPL_OPENGL_ES2) || defined(IMGUI_IMPL_OPENGL_ES3)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

// Private impl of the opaque app->PlatformData slot (per-host type; hosts coexist as ordinary TUs).
struct ImGuiApp_ImplSDL2OpenGL3_PlatformData
{
    SDL_Window*   Window;
    SDL_GLContext GLContext;
    bool          OwnsImGuiContext; // this host created the ImGui context (none existed)
};

struct ImGuiApp_ImplSDL2OpenGL3_Data
{
    SDL_Window*   Window;
    SDL_GLContext GLContext;
    bool          PlatformBackendInitialized;
    bool          RendererBackendInitialized;

    ImGuiApp_ImplSDL2OpenGL3_Data() { memset((void*)this, 0, sizeof(*this)); }
};

// Backend data stored in app->BackendData (the io userdata slots belong to the wrapped imgui backends).
static ImGuiApp_ImplSDL2OpenGL3_Data* ImGuiApp_ImplSDL2OpenGL3_GetBackendData(ImGuiApp* app)
{
    return app != nullptr ? (ImGuiApp_ImplSDL2OpenGL3_Data*)app->BackendData : nullptr;
}

static bool ImGuiApp_ImplSDL2OpenGL3_IsInitInfoValid(const ImGuiApp_ImplSDL2OpenGL3_InitInfo* init_info)
{
    return init_info != nullptr &&
           init_info->Window != nullptr &&
           init_info->GLContext != nullptr;
}

bool ImGuiApp_ImplSDL2OpenGL3_Init(ImGuiApp* app, const ImGuiApp_ImplSDL2OpenGL3_InitInfo* init_info)
{
    IMGUI_CHECKVERSION();
    IM_ASSERT(app != nullptr && app->BackendData == nullptr && "Already initialized a platform backend!");
    IM_ASSERT(ImGuiApp_ImplSDL2OpenGL3_IsInitInfoValid(init_info) && "ImGuiApp_ImplSDL2OpenGL3_Init: invalid init_info.");
    if (app == nullptr || app->BackendData != nullptr || !ImGuiApp_ImplSDL2OpenGL3_IsInitInfoValid(init_info))
        return false;

    ImGuiApp_ImplSDL2OpenGL3_Data* bd = IM_NEW(ImGuiApp_ImplSDL2OpenGL3_Data)();
    app->BackendData = bd;
    bd->Window = (SDL_Window*)init_info->Window;
    bd->GLContext = (SDL_GLContext)init_info->GLContext;

    if (!ImGui_ImplSDL2_InitForOpenGL(bd->Window, bd->GLContext))
    {
        ImGuiApp_ImplSDL2OpenGL3_Shutdown(app);
        return false;
    }
    bd->PlatformBackendInitialized = true;

    if (!ImGui_ImplOpenGL3_Init(init_info->GlslVersion))
    {
        ImGuiApp_ImplSDL2OpenGL3_Shutdown(app);
        return false;
    }
    bd->RendererBackendInitialized = true;
    return true;
}

void ImGuiApp_ImplSDL2OpenGL3_Shutdown(ImGuiApp* app)
{
    ImGuiApp_ImplSDL2OpenGL3_Data* bd = ImGuiApp_ImplSDL2OpenGL3_GetBackendData(app);
    IM_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
    if (bd == nullptr)
        return;

    if (bd->RendererBackendInitialized)
        ImGui_ImplOpenGL3_Shutdown();
    if (bd->PlatformBackendInitialized)
        ImGui_ImplSDL2_Shutdown();

    app->BackendData = nullptr;
    IM_DELETE(bd);
}

void ImGuiApp_ImplSDL2OpenGL3_NewFrame(ImGuiApp* app)
{
    ImGuiApp_ImplSDL2OpenGL3_Data* bd = ImGuiApp_ImplSDL2OpenGL3_GetBackendData(app);
    IM_ASSERT(bd != nullptr && "App or backend not initialized! Did you call ImGuiApp_ImplSDL2OpenGL3_Init()?");
    IM_UNUSED(bd);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
}

void ImGuiApp_ImplSDL2OpenGL3_RenderDrawData(ImGuiApp* app, ImDrawData* draw_data, const ImGuiAppFrameConfig* config)
{
    ImGuiApp_ImplSDL2OpenGL3_Data* bd = ImGuiApp_ImplSDL2OpenGL3_GetBackendData(app);
    IM_ASSERT(bd != nullptr && "App or backend not initialized! Did you call ImGuiApp_ImplSDL2OpenGL3_Init()?");
    if (bd == nullptr)
        return;

    int width = 0;
    int height = 0;
    SDL_GL_GetDrawableSize(bd->Window, &width, &height);

    glViewport(0, 0, width, height);

    if ((config->Flags & ImGuiAppFrameFlags_NoClear) == 0)
    {
        const ImVec4& clear_color = config->ClearColor;
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    ImGui_ImplOpenGL3_RenderDrawData(draw_data);
}

// Present phase: the encode phase runs between RenderDrawData and this, reading
// back the frame just rendered before it goes on screen.
void ImGuiApp_ImplSDL2OpenGL3_PresentFrame(ImGuiApp* app, const ImGuiAppFrameConfig* config)
{
    ImGuiApp_ImplSDL2OpenGL3_Data* bd = ImGuiApp_ImplSDL2OpenGL3_GetBackendData(app);
    if (bd == nullptr || config == nullptr)
        return;
    if ((config->Flags & ImGuiAppFrameFlags_NoPresent) == 0)
        SDL_GL_SwapWindow(bd->Window);
}

bool ImGuiApp_ImplSDL2OpenGL3_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config)
{
    ImGuiApp_ImplSDL2OpenGL3_PlatformData* state = IM_NEW(ImGuiApp_ImplSDL2OpenGL3_PlatformData)();
    app->PlatformData = state;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
        return false;

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

#if defined(IMGUI_IMPL_OPENGL_ES2)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    const char* glsl_version = "#version 100";
#elif defined(IMGUI_IMPL_OPENGL_ES3)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    const char* glsl_version = "#version 300 es";
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    const char* glsl_version = nullptr;
#endif

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    state->Window = SDL_CreateWindow(config.WindowTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, config.WindowWidth, config.WindowHeight, window_flags);
    if (state->Window == nullptr)
    {
        SDL_Quit();
        return false;
    }

    state->GLContext = SDL_GL_CreateContext(state->Window);
    if (state->GLContext == nullptr)
    {
        SDL_DestroyWindow(state->Window);
        state->Window = nullptr;
        SDL_Quit();
        return false;
    }
    SDL_GL_MakeCurrent(state->Window, state->GLContext);
    SDL_GL_SetSwapInterval(1);

    config.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

    if (ImGui::GetCurrentContext() == nullptr)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        state->OwnsImGuiContext = true;
    }

    ImGuiApp_ImplSDL2OpenGL3_InitInfo init_info;
    init_info.Window      = state->Window;
    init_info.GLContext   = state->GLContext;
    init_info.GlslVersion = glsl_version;
    if (!ImGuiApp_ImplSDL2OpenGL3_Init(app, &init_info))
        return false;

    ImGui::GetIO().ConfigFlags |= config.ConfigFlags;

    app->PlatformName         = config.PlatformName;
    app->PlatformWindowHandle = state->Window;
    return true;
}

void ImGuiApp_ImplSDL2OpenGL3_ShutdownPlatform(ImGuiApp* app)
{
    // Graphics first (wrapped imgui backends need the window/GL context alive), then the host.
    if (ImGuiApp_ImplSDL2OpenGL3_GetBackendData(app) != nullptr)
        ImGuiApp_ImplSDL2OpenGL3_Shutdown(app);

    ImGuiApp_ImplSDL2OpenGL3_PlatformData* state = (ImGuiApp_ImplSDL2OpenGL3_PlatformData*)app->PlatformData;
    if (state == nullptr)
        return;
    if (state->OwnsImGuiContext)
    {
        ImGui::DestroyContext();
        state->OwnsImGuiContext = false;
    }
    if (state->GLContext != nullptr)
    {
        SDL_GL_DeleteContext(state->GLContext);
        state->GLContext = nullptr;
    }
    if (state->Window != nullptr)
    {
        SDL_DestroyWindow(state->Window);
        state->Window = nullptr;
    }
    SDL_Quit();

    IM_DELETE(state);
    app->PlatformData = nullptr;
}

static const ImGuiAppPlatformBackend ImGuiApp_ImplSDL2OpenGL3_PlatformBackend =
{
    ImGuiApp_ImplSDL2OpenGL3_InitPlatform,
    ImGuiApp_ImplSDL2OpenGL3_ShutdownPlatform,
    ImGuiApp_ImplSDL2_RunLoop,
    nullptr, // CaptureFrame
    "imguiapp_impl_sdl2_opengl3",
    ImGuiApp_ImplSDL2OpenGL3_Shutdown,
    ImGuiApp_ImplSDL2OpenGL3_NewFrame,
    ImGuiApp_ImplSDL2OpenGL3_RenderDrawData,
    ImGuiApp_ImplSDL2OpenGL3_PresentFrame,
};

const ImGuiAppPlatformBackend* ImGuiApp_ImplSDL2OpenGL3_GetPlatformBackend() { return &ImGuiApp_ImplSDL2OpenGL3_PlatformBackend; }

#endif // #ifndef IMGUI_DISABLE
