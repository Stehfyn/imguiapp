#include "imguiapp_impl_win32_opengl3.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_win32.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

#include "imguiapp_impl_win32_state.h"
#include "imguiapp.h"
#include "imguiapp_internal.h"

#include <cstdio>
#include <cstring>

namespace
{
    struct ImGuiApp_ImplWin32OpenGL3_InitInfo
    {
        void*       Hwnd;
        void*       MainDC;
        void*       MainGLRC;
        const char* GlslVersion;
    };

    struct ImGuiApp_ImplWin32OpenGL3_Data
    {
        void* Hwnd;
        void* MainDC;
        void* MainGLRC;
        bool  PlatformBackendInitialized;
        bool  RendererBackendInitialized;

        // Per-viewport pacing (secondary platform windows): the decision is made ONCE per
        // viewport per frame in Platform_RenderWindow (the first per-viewport hook
        // RenderPlatformWindowsDefault runs) and consumed by the draw + swap hooks. A skipped
        // viewport does no GL work and no swap; its window keeps its last contents.
        ImGuiApp*      App;                  // pacer app (source of per-viewport pacing decisions)
        ImGuiStorage   VpSkip;               // viewport ID -> skip present this frame
        void         (*UnderlyingRendererRenderWindow)(ImGuiViewport*, void*);

        // Frame capture (AV readback).
        ImU64          CaptureLastReturned; // highest FrameID.FrameIndex handed out; a repeat call with no new frame returns false
        ImVector<char> CaptureRead;         // glReadPixels scratch, GL's bottom-up row order
        ImVector<char> CaptureRgba;         // top-down RGBA handed to callers; valid until the next capture
    };

    ImGuiApp_ImplWin32OpenGL3_Data GBackend;
    static ImGuiAppPlatformState* GState = nullptr;

    bool IsInitInfoValid(const ImGuiApp_ImplWin32OpenGL3_InitInfo* init_info)
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

    // Per-viewport pacing wrappers: installed into platform_io, so they run as context-free
    // callbacks and reach backend state through the GBackend singleton. See the pacing fields
    // on ImGuiApp_ImplWin32OpenGL3_Data.
    static void Hook_Platform_RenderWindow(ImGuiViewport* viewport, void*)
    {
        if (GState == nullptr)
            return;
        const bool present = ImGui::AppPacerViewportShouldPresent(GBackend.App, viewport);
        GBackend.VpSkip.SetBool(viewport->ID, !present);
        if (!present)
            return;
        if (ImGuiAppPlatformState::WGLWindowData* data = (ImGuiAppPlatformState::WGLWindowData*)viewport->RendererUserData)
            wglMakeCurrent(data->hDC, GState->MainGLRC);
    }

    static void Pace_Renderer_RenderWindow(ImGuiViewport* viewport, void* render_arg)
    {
        if (GBackend.VpSkip.GetBool(viewport->ID))
            return;
        if (GBackend.UnderlyingRendererRenderWindow != nullptr)
            GBackend.UnderlyingRendererRenderWindow(viewport, render_arg);
    }

    static void Hook_Renderer_SwapBuffers(ImGuiViewport* viewport, void*)
    {
        if (GBackend.VpSkip.GetBool(viewport->ID))
            return;
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
        ImGuiApp_ImplWin32OpenGL3_Data* bd = (ImGuiApp_ImplWin32OpenGL3_Data*)user_data;
        IM_ASSERT(bd != nullptr);
        if (bd == nullptr)
            return;

        if (bd->RendererBackendInitialized)
            ImGui_ImplOpenGL3_Shutdown();
        if (bd->PlatformBackendInitialized)
            ImGui_ImplWin32_Shutdown();

        *bd = ImGuiApp_ImplWin32OpenGL3_Data();
    }

    void NewFrame(void* user_data)
    {
        IM_UNUSED(user_data);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
    }

    void RenderDrawData(ImDrawData* draw_data, const ImGuiAppFrameConfig* config, void* user_data)
    {
        ImGuiApp_ImplWin32OpenGL3_Data* bd = (ImGuiApp_ImplWin32OpenGL3_Data*)user_data;
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
        ImGuiApp_ImplWin32OpenGL3_Data* bd = (ImGuiApp_ImplWin32OpenGL3_Data*)user_data;
        if (bd == nullptr || config == nullptr)
            return;
        if ((config->Flags & ImGuiAppFrameFlags_NoPresent) == 0)
            ::SwapBuffers((HDC)bd->MainDC);
    }

    // Synchronous backbuffer readback: this TU links only GL 1.1 entry points (no
    // loader), so no PBO ring -- glReadPixels stalls the pipeline for the transfer.
    // Every call returns the CURRENT frame (encode phase runs before present, so
    // GL_BACK still holds it); a repeat call with no new frame rendered returns false.
    bool CaptureFrame(ImGuiApp* app, ImGuiAppAVFrame* out_frame)
    {
        ImGuiApp_ImplWin32OpenGL3_Data* bd = &GBackend;
        if (out_frame == nullptr || bd->MainDC == nullptr || bd->MainGLRC == nullptr)
            return false;
        if (app != nullptr && app->FrameID.FrameIndex <= bd->CaptureLastReturned)
            return false;   // drain/gap call: this frame was already handed out

        int width = 0;
        int height = 0;
        GetClientSize(bd->Hwnd, &width, &height);   // resize mid-recording: report it; the recorder aborts by contract
        if (width <= 0 || height <= 0)
            return false;

        const int row_bytes = width * 4;
        bd->CaptureRead.resize(row_bytes * height);
        bd->CaptureRgba.resize(row_bytes * height);

        wglMakeCurrent((HDC)bd->MainDC, (HGLRC)bd->MainGLRC);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, bd->CaptureRead.Data);

        // GL rows are bottom-up; the recorder expects top-down like the vulkan path.
        for (int y = 0; y < height; y++)
            memcpy(bd->CaptureRgba.Data + (size_t)y * row_bytes,
                   bd->CaptureRead.Data + (size_t)(height - 1 - y) * row_bytes,
                   (size_t)row_bytes);

        out_frame->Width = width;
        out_frame->Height = height;
        out_frame->PitchBytes = row_bytes;
        out_frame->Pixels = bd->CaptureRgba.Data;
        if (app != nullptr)
        {
            out_frame->FrameID = app->FrameID;   // synchronous path: pixels ARE the current frame
            bd->CaptureLastReturned = app->FrameID.FrameIndex;
        }
        return true;
    }
}

static bool ImGuiApp_ImplWin32OpenGL3_Init(const ImGuiApp_ImplWin32OpenGL3_InitInfo* init_info)
{
    if (ImGuiX::GetCurrentContext() == nullptr)
        ImGuiX::CreateContext();

    IM_ASSERT(IsInitInfoValid(init_info) && "ImGuiApp_ImplWin32OpenGL3_Init: invalid init_info.");
    if (!IsInitInfoValid(init_info))
        return false;

    ImGuiX::Shutdown();

    GBackend.Hwnd = init_info->Hwnd;
    GBackend.MainDC = init_info->MainDC;
    GBackend.MainGLRC = init_info->MainGLRC;

    if (!ImGui_ImplWin32_InitForOpenGL(init_info->Hwnd))
    {
        GBackend = ImGuiApp_ImplWin32OpenGL3_Data();
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
    imguix_init_info.Backend.Name = "imguiapp_impl_win32_opengl3";
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

bool ImGuiApp_ImplWin32OpenGL3_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config)
{
    if (config.Headless != ImGuiAppHeadlessMode_None)
    {
        std::fprintf(stderr, "imguiapp_impl_win32_opengl3: headless modes are not implemented for this backend (use win32-vulkan).\n");
        return false;
    }

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

    ImGuiApp_ImplWin32OpenGL3_InitInfo init_info;
    init_info.Hwnd        = state->Hwnd;
    init_info.MainDC      = state->MainWindow.hDC;
    init_info.MainGLRC    = state->MainGLRC;
    init_info.GlslVersion = nullptr;
    if (!ImGuiApp_ImplWin32OpenGL3_Init(&init_info))
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

        // Wrap the upstream GL renderer's per-viewport draw (registered by
        // ImGui_ImplOpenGL3_Init above) so a pacing-skipped viewport draws nothing.
        GBackend.App = app;
        GBackend.UnderlyingRendererRenderWindow = platform_io.Renderer_RenderWindow;
        platform_io.Renderer_RenderWindow = Pace_Renderer_RenderWindow;
    }

    app->Platform.Name               = config.Platform.Name;
    app->Platform.NativeWindowHandle = state->Hwnd;
    ::SetWindowLongPtr(state->Hwnd, GWLP_USERDATA, (LONG_PTR)app);
    return true;
}

void ImGuiApp_ImplWin32OpenGL3_ShutdownPlatform(ImGuiApp* app)
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
    GBackend.App = nullptr;
    GBackend.UnderlyingRendererRenderWindow = nullptr;
    GBackend.VpSkip.Clear();

    IM_DELETE(state);
    app->PlatformData = nullptr;
}

static const ImGuiAppPlatformBackend GPlatformBackend =
{
    ImGuiApp_ImplWin32OpenGL3_InitPlatform,
    ImGuiApp_ImplWin32OpenGL3_ShutdownPlatform,
    ImGuiApp_ImplWin32_RunLoop,
    CaptureFrame,
};

const ImGuiAppPlatformBackend* ImGuiApp_GetPlatformBackend() { return &GPlatformBackend; }
