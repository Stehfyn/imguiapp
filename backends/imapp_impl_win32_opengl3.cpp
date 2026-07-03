#include "imapp_impl_win32_opengl3.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_win32.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

#include "imapp_impl_win32_state.h"
#include "imgui_applayer.h"

namespace
{
    struct ImGuiApp_Win32OpenGL3_InitInfo
    {
        void*       Hwnd;
        void*       MainDC;
        void*       MainGLRC;
        const char* GlslVersion;
    };

    struct ImGuiApp_Win32OpenGL3_Data
    {
        void* Hwnd;
        void* MainDC;
        void* MainGLRC;
        bool  PlatformBackendInitialized;
        bool  RendererBackendInitialized;
    };

    ImGuiApp_Win32OpenGL3_Data GBackend;
    static ImGuiAppPlatformState* GState = nullptr;

    bool IsInitInfoValid(const ImGuiApp_Win32OpenGL3_InitInfo* init_info)
    {
        return init_info != nullptr &&
               init_info->Hwnd != nullptr &&
               init_info->MainDC != nullptr &&
               init_info->MainGLRC != nullptr;
    }

    static bool CreateDeviceWGL(HWND hWnd, ImGuiAppPlatformState::WGLWindowData* data, HGLRC* main_glrc)
    {
        HDC hDc = ::GetDC(hWnd);
        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize      = sizeof(pfd);
        pfd.nVersion   = 1;
        pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;

        const int pf = ::ChoosePixelFormat(hDc, &pfd);
        if (pf == 0) { ::ReleaseDC(hWnd, hDc); return false; }
        if (::SetPixelFormat(hDc, pf, &pfd) == FALSE) { ::ReleaseDC(hWnd, hDc); return false; }
        ::ReleaseDC(hWnd, hDc);

        data->hDC = ::GetDC(hWnd);
        if (*main_glrc == nullptr)
            *main_glrc = wglCreateContext(data->hDC);
        return data->hDC != nullptr && *main_glrc != nullptr;
    }

    static void CleanupDeviceWGL(HWND hWnd, ImGuiAppPlatformState::WGLWindowData* data)
    {
        if (data == nullptr || data->hDC == nullptr)
            return;
        wglMakeCurrent(nullptr, nullptr);
        ::ReleaseDC(hWnd, data->hDC);
        data->hDC = nullptr;
    }

    static void Hook_Renderer_CreateWindow(ImGuiViewport* viewport)
    {
        IM_ASSERT(GState != nullptr);
        IM_ASSERT(viewport->RendererUserData == nullptr);
        if (GState == nullptr)
            return;

        ImGuiAppPlatformState::WGLWindowData* data = IM_NEW(ImGuiAppPlatformState::WGLWindowData)();
        if (!CreateDeviceWGL((HWND)viewport->PlatformHandle, data, &GState->MainGLRC))
        {
            IM_DELETE(data);
            return;
        }
        viewport->RendererUserData = data;
    }

    static void Hook_Renderer_DestroyWindow(ImGuiViewport* viewport)
    {
        if (viewport->RendererUserData != nullptr)
        {
            ImGuiAppPlatformState::WGLWindowData* data = (ImGuiAppPlatformState::WGLWindowData*)viewport->RendererUserData;
            CleanupDeviceWGL((HWND)viewport->PlatformHandle, data);
            IM_DELETE(data);
            viewport->RendererUserData = nullptr;
        }
    }

    static void Hook_Platform_RenderWindow(ImGuiViewport* viewport, void*)
    {
        if (GState == nullptr)
            return;
        if (ImGuiAppPlatformState::WGLWindowData* data = (ImGuiAppPlatformState::WGLWindowData*)viewport->RendererUserData)
            wglMakeCurrent(data->hDC, GState->MainGLRC);
    }

    static void Hook_Renderer_SwapBuffers(ImGuiViewport* viewport, void*)
    {
        if (ImGuiAppPlatformState::WGLWindowData* data = (ImGuiAppPlatformState::WGLWindowData*)viewport->RendererUserData)
            ::SwapBuffers(data->hDC);
    }

    void GetClientSize(void* hwnd, int* width, int* height)
    {
        IM_ASSERT(width != nullptr);
        IM_ASSERT(height != nullptr);

        RECT rect = {};
        if (hwnd != nullptr && ::GetClientRect((HWND)hwnd, &rect))
        {
            *width = rect.right - rect.left;
            *height = rect.bottom - rect.top;
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        *width = (int)io.DisplaySize.x;
        *height = (int)io.DisplaySize.y;
    }

    void ShutdownBackend(void* user_data)
    {
        ImGuiApp_Win32OpenGL3_Data* bd = (ImGuiApp_Win32OpenGL3_Data*)user_data;
        IM_ASSERT(bd != nullptr);
        if (bd == nullptr)
            return;

        if (bd->RendererBackendInitialized)
            ImGui_ImplOpenGL3_Shutdown();
        if (bd->PlatformBackendInitialized)
            ImGui_ImplWin32_Shutdown();

        *bd = ImGuiApp_Win32OpenGL3_Data();
    }

    void NewFrame(void* user_data)
    {
        IM_UNUSED(user_data);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
    }

    void RenderDrawData(ImDrawData* draw_data, const ImGuiAppFrameConfig* config, void* user_data)
    {
        ImGuiApp_Win32OpenGL3_Data* bd = (ImGuiApp_Win32OpenGL3_Data*)user_data;
        IM_ASSERT(bd != nullptr);
        if (bd == nullptr)
            return;

        int width = 0;
        int height = 0;
        GetClientSize(bd->Hwnd, &width, &height);

        glViewport(0, 0, width, height);

        if ((config->Flags & ImGuiAppFrameFlags_NoClear) == 0)
        {
            const ImVec4& clear_color = config->ClearColor;
            glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        ImGui_ImplOpenGL3_RenderDrawData(draw_data);

        if ((config->Flags & ImGuiAppFrameFlags_NoPlatformWindows) == 0 &&
            (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            wglMakeCurrent((HDC)bd->MainDC, (HGLRC)bd->MainGLRC);
        }
    }

    // Present phase (ImGuiX::PresentFrame): the encode phase runs between RenderDrawData
    // and this, reading back the frame just rendered before it goes on screen.
    void PresentFrame(const ImGuiAppFrameConfig* config, void* user_data)
    {
        ImGuiApp_Win32OpenGL3_Data* bd = (ImGuiApp_Win32OpenGL3_Data*)user_data;
        if (bd == nullptr || config == nullptr)
            return;
        if ((config->Flags & ImGuiAppFrameFlags_NoPresent) == 0)
            ::SwapBuffers((HDC)bd->MainDC);
    }
}

static bool ImGuiApp_Win32OpenGL3_Init(const ImGuiApp_Win32OpenGL3_InitInfo* init_info)
{
    if (ImGuiX::GetCurrentContext() == nullptr)
        ImGuiX::CreateContext();

    IM_ASSERT(IsInitInfoValid(init_info) && "ImGuiApp_Win32OpenGL3_Init: invalid init_info.");
    if (!IsInitInfoValid(init_info))
        return false;

    ImGuiX::Shutdown();

    GBackend.Hwnd = init_info->Hwnd;
    GBackend.MainDC = init_info->MainDC;
    GBackend.MainGLRC = init_info->MainGLRC;

    if (!ImGui_ImplWin32_InitForOpenGL(init_info->Hwnd))
    {
        GBackend = ImGuiApp_Win32OpenGL3_Data();
        return false;
    }
    GBackend.PlatformBackendInitialized = true;

    if (!ImGui_ImplOpenGL3_Init(init_info->GlslVersion))
    {
        ShutdownBackend(&GBackend);
        return false;
    }
    GBackend.RendererBackendInitialized = true;

    ImGuiXInitInfo imguix_init_info;
    imguix_init_info.Backend.Name = "imapp_impl_win32_opengl3";
    imguix_init_info.Backend.UserData = &GBackend;
    imguix_init_info.Backend.Shutdown = ShutdownBackend;
    imguix_init_info.Backend.NewFrame = NewFrame;
    imguix_init_info.Backend.RenderDrawData = RenderDrawData;
    imguix_init_info.Backend.PresentFrame = PresentFrame;

    if (!ImGuiX::Initialize(&imguix_init_info))
    {
        ShutdownBackend(&GBackend);
        return false;
    }

    return true;
}

bool ImGuiApp_Win32OpenGL3_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config)
{
    ImGuiAppPlatformState* state = IM_NEW(ImGuiAppPlatformState)();
    app->PlatformData = state;

    GState = state;

    ImGui_ImplWin32_EnableDpiAwareness();
    const float main_scale    = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    const int   window_width  = (int)(config.WindowWidth  * main_scale);
    const int   window_height = (int)(config.WindowHeight * main_scale);
    config.DpiScale    = main_scale;
    config.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;

    HINSTANCE instance = ::GetModuleHandle(nullptr);
    state->WindowClass = { sizeof(state->WindowClass), CS_OWNDC | CS_BYTEALIGNCLIENT | CS_BYTEALIGNWINDOW, ImGuiApp_ImplWin32_WndProc, 0L, 0L, instance, nullptr, LoadCursor(nullptr, IDC_ARROW), (HBRUSH)(GetStockObject(BLACK_BRUSH)), nullptr, "ImGuiXWindow", nullptr };
    ::RegisterClassExA(&state->WindowClass);
    state->Hwnd = ::CreateWindowA(state->WindowClass.lpszClassName, config.WindowTitle, WS_OVERLAPPEDWINDOW, 100, 100, window_width, window_height, nullptr, nullptr, state->WindowClass.hInstance, nullptr);
    if (state->Hwnd == nullptr)
    {
        GState = nullptr;
        return false;
    }

    if (!CreateDeviceWGL(state->Hwnd, &state->MainWindow, &state->MainGLRC))
    {
        ::DestroyWindow(state->Hwnd);
        state->Hwnd = nullptr;
        GState = nullptr;
        return false;
    }
    wglMakeCurrent(state->MainWindow.hDC, state->MainGLRC);

    ::ShowWindow(state->Hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(state->Hwnd);

    ImGuiX::CreateContext();

    ImGuiApp_Win32OpenGL3_InitInfo init_info;
    init_info.Hwnd        = state->Hwnd;
    init_info.MainDC      = state->MainWindow.hDC;
    init_info.MainGLRC    = state->MainGLRC;
    init_info.GlslVersion = nullptr;
    if (!ImGuiApp_Win32OpenGL3_Init(&init_info))
    {
        ImGuiX::DestroyContext();
        GState = nullptr;
        return false;
    }

    ImGui::GetIO().ConfigFlags |= config.ConfigFlags;

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        IM_ASSERT(platform_io.Renderer_CreateWindow  == nullptr);
        IM_ASSERT(platform_io.Renderer_DestroyWindow == nullptr);
        IM_ASSERT(platform_io.Renderer_SwapBuffers   == nullptr);
        IM_ASSERT(platform_io.Platform_RenderWindow  == nullptr);
        platform_io.Renderer_CreateWindow  = Hook_Renderer_CreateWindow;
        platform_io.Renderer_DestroyWindow = Hook_Renderer_DestroyWindow;
        platform_io.Renderer_SwapBuffers   = Hook_Renderer_SwapBuffers;
        platform_io.Platform_RenderWindow  = Hook_Platform_RenderWindow;
    }

    app->Platform.Name               = config.Platform.Name;
    app->Platform.NativeWindowHandle = state->Hwnd;
    ::SetWindowLongPtr(state->Hwnd, GWLP_USERDATA, (LONG_PTR)app);
    return true;
}

void ImGuiApp_Win32OpenGL3_ShutdownPlatform(ImGuiApp* app)
{
    ImGuiAppPlatformState* state = static_cast<ImGuiAppPlatformState*>(app->PlatformData);
    if (state == nullptr)
        return;
    if (state->Hwnd != nullptr)
        ::SetWindowLongPtr(state->Hwnd, GWLP_USERDATA, 0);
    if (state->Hwnd != nullptr)
        CleanupDeviceWGL(state->Hwnd, &state->MainWindow);
    if (state->MainGLRC != nullptr)
    {
        wglDeleteContext(state->MainGLRC);
        state->MainGLRC = nullptr;
    }
    if (state->Hwnd != nullptr)
    {
        ::DestroyWindow(state->Hwnd);
        state->Hwnd = nullptr;
    }
    if (state->WindowClass.lpszClassName != nullptr && state->WindowClass.hInstance != nullptr)
    {
        ::UnregisterClassA(state->WindowClass.lpszClassName, state->WindowClass.hInstance);
        state->WindowClass = {};
    }
    if (GState == state)
        GState = nullptr;

    IM_DELETE(state);
    app->PlatformData = nullptr;
}

static const ImGuiAppPlatformBackend GPlatformBackend =
{
    ImGuiApp_Win32OpenGL3_InitPlatform,
    ImGuiApp_Win32OpenGL3_ShutdownPlatform,
    ImGuiApp_ImplWin32_RunLoop,
};

const ImGuiAppPlatformBackend* ImGuiApp_GetPlatformBackend() { return &GPlatformBackend; }
