#include "imguiapp_impl_sdl2_opengl3.h"

#include "imguiapp_impl_sdl2.h"
#include "imguiapp.h"

#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>
#if defined(IMGUI_IMPL_OPENGL_ES2) || defined(IMGUI_IMPL_OPENGL_ES3)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

// Private impl of the opaque app->PlatformData handle (defined per platform host TU; exactly one links per build).
struct ImGuiAppPlatformData
{
    SDL_Window*   Window;
    SDL_GLContext GLContext;
    bool          OwnsImGuiContext; // this host created the ImGui context (none existed)
};

namespace
{
    struct ImGuiApp_ImplSDL2OpenGL3_Data
    {
        SDL_Window*   Window;
        SDL_GLContext GLContext;
        bool          PlatformBackendInitialized;
        bool          RendererBackendInitialized;

        ImGuiApp_ImplSDL2OpenGL3_Data() { memset((void*)this, 0, sizeof(*this)); }
    };

    // IM_NEW'd at Init, freed by Shutdown; reached through the accessor (docs/house-style-audit.md Δ4).
    ImGuiApp_ImplSDL2OpenGL3_Data* GBackend = nullptr;

    ImGuiApp_ImplSDL2OpenGL3_Data* ImGuiApp_ImplSDL2OpenGL3_GetBackendData() { return GBackend; }

    bool IsInitInfoValid(const ImGuiApp_ImplSDL2OpenGL3_InitInfo* init_info)
    {
        return init_info != nullptr &&
               init_info->Window != nullptr &&
               init_info->GLContext != nullptr;
    }
}

bool ImGuiApp_ImplSDL2OpenGL3_Init(const ImGuiApp_ImplSDL2OpenGL3_InitInfo* init_info)
{
    IM_ASSERT(GBackend == nullptr && "Already initialized a platform backend!");
    IM_ASSERT(IsInitInfoValid(init_info) && "ImGuiApp_ImplSDL2OpenGL3_Init: invalid init_info.");
    if (GBackend != nullptr || !IsInitInfoValid(init_info))
        return false;

    GBackend = IM_NEW(ImGuiApp_ImplSDL2OpenGL3_Data)();
    GBackend->Window = (SDL_Window*)init_info->Window;
    GBackend->GLContext = (SDL_GLContext)init_info->GLContext;

    if (!ImGui_ImplSDL2_InitForOpenGL(GBackend->Window, GBackend->GLContext))
    {
        ImGuiApp_ImplSDL2OpenGL3_Shutdown();
        return false;
    }
    GBackend->PlatformBackendInitialized = true;

    if (!ImGui_ImplOpenGL3_Init(init_info->GlslVersion))
    {
        ImGuiApp_ImplSDL2OpenGL3_Shutdown();
        return false;
    }
    GBackend->RendererBackendInitialized = true;
    return true;
}

void ImGuiApp_ImplSDL2OpenGL3_Shutdown()
{
    ImGuiApp_ImplSDL2OpenGL3_Data* bd = ImGuiApp_ImplSDL2OpenGL3_GetBackendData();
    IM_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
    if (bd == nullptr)
        return;

    if (bd->RendererBackendInitialized)
        ImGui_ImplOpenGL3_Shutdown();
    if (bd->PlatformBackendInitialized)
        ImGui_ImplSDL2_Shutdown();

    GBackend = nullptr;
    IM_DELETE(bd);
}

void ImGuiApp_ImplSDL2OpenGL3_NewFrame()
{
    ImGuiApp_ImplSDL2OpenGL3_Data* bd = ImGuiApp_ImplSDL2OpenGL3_GetBackendData();
    IM_ASSERT(bd != nullptr && "Backend not initialized! Did you call ImGuiApp_ImplSDL2OpenGL3_Init()?");
    IM_UNUSED(bd);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
}

void ImGuiApp_ImplSDL2OpenGL3_RenderDrawData(ImDrawData* draw_data, const ImGuiAppFrameConfig* config)
{
    ImGuiApp_ImplSDL2OpenGL3_Data* bd = ImGuiApp_ImplSDL2OpenGL3_GetBackendData();
    IM_ASSERT(bd != nullptr && "Backend not initialized! Did you call ImGuiApp_ImplSDL2OpenGL3_Init()?");
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
void ImGuiApp_ImplSDL2OpenGL3_PresentFrame(const ImGuiAppFrameConfig* config)
{
    ImGuiApp_ImplSDL2OpenGL3_Data* bd = ImGuiApp_ImplSDL2OpenGL3_GetBackendData();
    if (bd == nullptr || config == nullptr)
        return;
    if ((config->Flags & ImGuiAppFrameFlags_NoPresent) == 0)
        SDL_GL_SwapWindow(bd->Window);
}

bool ImGuiApp_ImplSDL2OpenGL3_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config)
{
    ImGuiAppPlatformData* state = IM_NEW(ImGuiAppPlatformData)();
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
    if (!ImGuiApp_ImplSDL2OpenGL3_Init(&init_info))
        return false;

    ImGui::GetIO().ConfigFlags |= config.ConfigFlags;

    app->Platform.Name               = config.Platform.Name;
    app->Platform.NativeWindowHandle = state->Window;
    return true;
}

void ImGuiApp_ImplSDL2OpenGL3_ShutdownPlatform(ImGuiApp* app)
{
    // Graphics first (wrapped imgui backends need the window/GL context alive), then the host.
    if (ImGuiApp_ImplSDL2OpenGL3_GetBackendData() != nullptr)
        ImGuiApp_ImplSDL2OpenGL3_Shutdown();

    ImGuiAppPlatformData* state = app->PlatformData;
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

static const ImGuiAppPlatformBackend GPlatformBackend =
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

const ImGuiAppPlatformBackend* ImGuiApp_GetPlatformBackend() { return &GPlatformBackend; }
