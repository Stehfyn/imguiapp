#include "imguiapp_impl_sdl2_opengl3.h"

#include "imguiapp_impl_sdl2_state.h"
#include "imguiapp.h"

#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>
#if defined(IMGUI_IMPL_OPENGL_ES2) || defined(IMGUI_IMPL_OPENGL_ES3)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

namespace
{
    struct ImGuiApp_ImplSDL2OpenGL3_InitInfo
    {
        void*       Window;
        void*       GLContext;
        const char* GlslVersion;
    };

    struct ImGuiApp_ImplSDL2OpenGL3_Data
    {
        SDL_Window*   Window;
        SDL_GLContext GLContext;
        bool          PlatformBackendInitialized;
        bool          RendererBackendInitialized;

        ImGuiApp_ImplSDL2OpenGL3_Data() { memset((void*)this, 0, sizeof(*this)); }
    };

    // IM_NEW'd at Init, freed by ShutdownBackend (docs/house-style-audit.md Δ4).
    ImGuiApp_ImplSDL2OpenGL3_Data* GBackend = nullptr;

    bool IsInitInfoValid(const ImGuiApp_ImplSDL2OpenGL3_InitInfo* init_info)
    {
        return init_info != nullptr &&
               init_info->Window != nullptr &&
               init_info->GLContext != nullptr;
    }

    void ShutdownBackend(void* user_data)
    {
        ImGuiApp_ImplSDL2OpenGL3_Data* bd = (ImGuiApp_ImplSDL2OpenGL3_Data*)user_data;
        IM_ASSERT(bd != nullptr);
        if (bd == nullptr)
            return;

        if (bd->RendererBackendInitialized)
            ImGui_ImplOpenGL3_Shutdown();
        if (bd->PlatformBackendInitialized)
            ImGui_ImplSDL2_Shutdown();

        if (GBackend == bd)
            GBackend = nullptr;
        IM_DELETE(bd);
    }

    void NewFrame(void* user_data)
    {
        IM_UNUSED(user_data);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
    }

    void RenderDrawData(ImDrawData* draw_data, const ImGuiAppFrameConfig* config, void* user_data)
    {
        ImGuiApp_ImplSDL2OpenGL3_Data* bd = (ImGuiApp_ImplSDL2OpenGL3_Data*)user_data;
        IM_ASSERT(bd != nullptr);
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

    // Present phase (ImGuiX::PresentFrame): the encode phase runs between RenderDrawData
    // and this, reading back the frame just rendered before it goes on screen.
    void PresentFrame(const ImGuiAppFrameConfig* config, void* user_data)
    {
        ImGuiApp_ImplSDL2OpenGL3_Data* bd = (ImGuiApp_ImplSDL2OpenGL3_Data*)user_data;
        if (bd == nullptr || config == nullptr)
            return;
        if ((config->Flags & ImGuiAppFrameFlags_NoPresent) == 0)
            SDL_GL_SwapWindow(bd->Window);
    }
}

static bool ImGuiApp_ImplSDL2OpenGL3_Init(const ImGuiApp_ImplSDL2OpenGL3_InitInfo* init_info)
{
    if (ImGuiX::GetCurrentContext() == nullptr)
        ImGuiX::CreateContext();

    IM_ASSERT(IsInitInfoValid(init_info) && "ImGuiApp_ImplSDL2OpenGL3_Init: invalid init_info.");
    if (!IsInitInfoValid(init_info))
        return false;

    ImGuiX::Shutdown();
    IM_ASSERT(GBackend == nullptr && "Already initialized a platform backend!");

    GBackend = IM_NEW(ImGuiApp_ImplSDL2OpenGL3_Data)();
    GBackend->Window = (SDL_Window*)init_info->Window;
    GBackend->GLContext = (SDL_GLContext)init_info->GLContext;

    if (!ImGui_ImplSDL2_InitForOpenGL(GBackend->Window, GBackend->GLContext))
    {
        IM_DELETE(GBackend);
        GBackend = nullptr;
        return false;
    }
    GBackend->PlatformBackendInitialized = true;

    if (!ImGui_ImplOpenGL3_Init(init_info->GlslVersion))
    {
        ShutdownBackend(GBackend);
        return false;
    }
    GBackend->RendererBackendInitialized = true;

    ImGuiXInitInfo imguix_init_info;
    imguix_init_info.Backend.Name = "imguiapp_impl_sdl2_opengl3";
    imguix_init_info.Backend.UserData = GBackend;
    imguix_init_info.Backend.Shutdown = ShutdownBackend;
    imguix_init_info.Backend.NewFrame = NewFrame;
    imguix_init_info.Backend.RenderDrawData = RenderDrawData;
    imguix_init_info.Backend.PresentFrame = PresentFrame;

    if (!ImGuiX::Initialize(&imguix_init_info))
    {
        ShutdownBackend(GBackend);
        return false;
    }

    return true;
}

bool ImGuiApp_ImplSDL2OpenGL3_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config)
{
    ImGuiAppPlatformState* state = IM_NEW(ImGuiAppPlatformState)();
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
    state->Running = true;

    config.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

    ImGuiX::CreateContext();

    ImGuiApp_ImplSDL2OpenGL3_InitInfo init_info;
    init_info.Window      = state->Window;
    init_info.GLContext   = state->GLContext;
    init_info.GlslVersion = glsl_version;
    if (!ImGuiApp_ImplSDL2OpenGL3_Init(&init_info))
    {
        ImGuiX::DestroyContext();
        return false;
    }

    ImGui::GetIO().ConfigFlags |= config.ConfigFlags;

    app->Platform.Name               = config.Platform.Name;
    app->Platform.NativeWindowHandle = state->Window;
    return true;
}

void ImGuiApp_ImplSDL2OpenGL3_ShutdownPlatform(ImGuiApp* app)
{
    ImGuiAppPlatformState* state = static_cast<ImGuiAppPlatformState*>(app->PlatformData);
    if (state == nullptr)
        return;
    state->Running = false;
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
};

const ImGuiAppPlatformBackend* ImGuiApp_GetPlatformBackend() { return &GPlatformBackend; }
