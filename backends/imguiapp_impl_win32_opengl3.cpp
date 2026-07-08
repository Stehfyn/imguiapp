// dear imgui app: Renderer Host for Win32 + OpenGL3/WGL (composes imgui_impl_win32 + imgui_impl_opengl3)
// This needs to be used along with the Win32 Platform Host (imguiapp_impl_win32: shared WndProc + message-pump run loop).

// Implemented features:
//  [X] Renderer: exposed ImGuiApp_ImplWin32OpenGL3_* frame lifecycle (imgui impl pattern), driven by ImGuiApp's frame phases.
//  [X] Platform: window/WGL-context creation + ImGui context ownership in InitPlatform/ShutdownPlatform.
//  [X] Multi-viewport: per-viewport WGL windows; pacing-aware per-viewport present skip.
//  [X] AV: synchronous glReadPixels CaptureFrame (GL 1.1 entry points only; stalls the pipeline).
// Missing features:
//  [ ] Headless modes (use win32-vulkan).

// You can use unmodified imguiapp_impl_* files in your project. See demos/ folder for examples of using this.
// Prefer including the entire imguiapp/ folder into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2026-07-08: Docs: Header block conformed to the backend anatomy (B1/B2 grammar).
//  2026-07-08: Lifecycle: Threaded ImGuiApp* through the frame lifecycle; backend data moved to app->BackendData, file-scope backend global removed; viewport hooks recover the app via the main viewport's GWLP_USERDATA.
//  2026-07-08: Misc: Exposed ImGuiApp_ImplWin32OpenGL3_* frame lifecycle (imgui impl pattern); host owns the ImGui context it creates; backend-internal symbols prefixed; IMGUI_DISABLE guards added.

#include "imguiapp.h"
#ifndef IMGUI_DISABLE
#include "imguiapp_impl_win32_opengl3.h"
#include "imguiapp_impl_win32.h"
#include "imguiapp_internal.h"

#include "imgui_impl_opengl3.h"
#include "imgui_impl_win32.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

#include <cstdio>
#include <cstring>

extern LRESULT WINAPI ImGuiApp_ImplWin32_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam); // copied from imguiapp_impl_win32.h ('#if 0' there keeps <windows.h> out of the header)

// Private impl of the opaque app->PlatformData handle (defined per platform host TU; exactly one links per build).
struct ImGuiAppPlatformData
{
    struct WGLWindowData { HDC hDC; };
    HWND          Hwnd;
    WNDCLASSEXA   WindowClass;
    HGLRC         MainGLRC;
    WGLWindowData MainWindow;
    bool          OwnsImGuiContext; // this host created the ImGui context (none existed)
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

    // Platform window/loop state sidecar, owned by app->PlatformData (freed in ShutdownPlatform).
    ImGuiAppPlatformData* State;

    ImGuiApp_ImplWin32OpenGL3_Data() { memset((void*)this, 0, sizeof(*this)); }
};

// Backend data stored in app->BackendData (the io userdata slots belong to the wrapped imgui backends).
static ImGuiApp_ImplWin32OpenGL3_Data* ImGuiApp_ImplWin32OpenGL3_GetBackendData(ImGuiApp* app)
{
    return app != nullptr ? (ImGuiApp_ImplWin32OpenGL3_Data*)app->BackendData : nullptr;
}

// Context-free viewport hooks recover the app through the main viewport's window user data
// (set at the end of InitPlatform; the same slot the shared WndProc reads for WM_TIMER repaints).
static ImGuiApp* ImGuiApp_ImplWin32OpenGL3_GetApp()
{
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    if (main_viewport == nullptr || main_viewport->PlatformHandle == nullptr)
        return nullptr;
    return (ImGuiApp*)::GetWindowLongPtr((HWND)main_viewport->PlatformHandle, GWLP_USERDATA);
}

static bool ImGuiApp_ImplWin32OpenGL3_IsInitInfoValid(const ImGuiApp_ImplWin32OpenGL3_InitInfo* init_info)
{
    return init_info != nullptr &&
           init_info->Hwnd != nullptr &&
           init_info->MainDC != nullptr &&
           init_info->MainGLRC != nullptr;
}

static bool ImGuiApp_ImplWin32OpenGL3_CreateDeviceWGL(HWND hWnd, ImGuiAppPlatformData::WGLWindowData* data, HGLRC* main_glrc)
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

static void ImGuiApp_ImplWin32OpenGL3_CleanupDeviceWGL(HWND hWnd, ImGuiAppPlatformData::WGLWindowData* data)
{
    if (data == nullptr || data->hDC == nullptr)
        return;
    wglMakeCurrent(nullptr, nullptr);
    ::ReleaseDC(hWnd, data->hDC);
    data->hDC = nullptr;
}

//--------------------------------------------------------------------------------------------------------
// MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
// This is an _advanced_ and _optional_ feature, allowing the backend to create and handle multiple viewports simultaneously.
// If you are new to dear imgui or creating a new binding for dear imgui, it is recommended that you completely ignore this section first..
// Per-viewport hooks installed into ImGuiPlatformIO; they run as context-free callbacks and reach
// backend state through the GetBackendData accessor.
//--------------------------------------------------------------------------------------------------------

static void ImGuiApp_ImplWin32OpenGL3_Renderer_CreateWindow(ImGuiViewport* viewport)
{
    ImGuiApp_ImplWin32OpenGL3_Data* bd = ImGuiApp_ImplWin32OpenGL3_GetBackendData(ImGuiApp_ImplWin32OpenGL3_GetApp());
    IM_ASSERT(bd != nullptr && bd->State != nullptr);
    IM_ASSERT(viewport->RendererUserData == nullptr);
    if (bd == nullptr || bd->State == nullptr)
        return;

    ImGuiAppPlatformData::WGLWindowData* data = IM_NEW(ImGuiAppPlatformData::WGLWindowData)();
    if (!ImGuiApp_ImplWin32OpenGL3_CreateDeviceWGL((HWND)viewport->PlatformHandle, data, &bd->State->MainGLRC))
    {
        IM_DELETE(data);
        return;
    }
    viewport->RendererUserData = data;
}

static void ImGuiApp_ImplWin32OpenGL3_Renderer_DestroyWindow(ImGuiViewport* viewport)
{
    if (viewport->RendererUserData != nullptr)
    {
        ImGuiAppPlatformData::WGLWindowData* data = (ImGuiAppPlatformData::WGLWindowData*)viewport->RendererUserData;
        ImGuiApp_ImplWin32OpenGL3_CleanupDeviceWGL((HWND)viewport->PlatformHandle, data);
        IM_DELETE(data);
        viewport->RendererUserData = nullptr;
    }
}

// Per-viewport pacing wrappers: installed into platform_io, so they run as context-free
// callbacks and reach backend state through the GetBackendData accessor. See the pacing
// fields on ImGuiApp_ImplWin32OpenGL3_Data.
static void ImGuiApp_ImplWin32OpenGL3_Platform_RenderWindow(ImGuiViewport* viewport, void*)
{
    ImGuiApp_ImplWin32OpenGL3_Data* bd = ImGuiApp_ImplWin32OpenGL3_GetBackendData(ImGuiApp_ImplWin32OpenGL3_GetApp());
    if (bd == nullptr || bd->State == nullptr)
        return;
    const bool present = ImGui::AppPacerViewportShouldPresent(bd->App, viewport);
    bd->VpSkip.SetBool(viewport->ID, !present);
    if (!present)
        return;
    if (ImGuiAppPlatformData::WGLWindowData* data = (ImGuiAppPlatformData::WGLWindowData*)viewport->RendererUserData)
        wglMakeCurrent(data->hDC, bd->State->MainGLRC);
}

static void ImGuiApp_ImplWin32OpenGL3_Renderer_RenderWindow(ImGuiViewport* viewport, void* render_arg)
{
    ImGuiApp_ImplWin32OpenGL3_Data* bd = ImGuiApp_ImplWin32OpenGL3_GetBackendData(ImGuiApp_ImplWin32OpenGL3_GetApp());
    if (bd == nullptr || bd->VpSkip.GetBool(viewport->ID))
        return;
    if (bd->UnderlyingRendererRenderWindow != nullptr)
        bd->UnderlyingRendererRenderWindow(viewport, render_arg);
}

static void ImGuiApp_ImplWin32OpenGL3_Renderer_SwapBuffers(ImGuiViewport* viewport, void*)
{
    ImGuiApp_ImplWin32OpenGL3_Data* bd = ImGuiApp_ImplWin32OpenGL3_GetBackendData(ImGuiApp_ImplWin32OpenGL3_GetApp());
    if (bd == nullptr || bd->VpSkip.GetBool(viewport->ID))
        return;
    if (ImGuiAppPlatformData::WGLWindowData* data = (ImGuiAppPlatformData::WGLWindowData*)viewport->RendererUserData)
        ::SwapBuffers(data->hDC);
}

// Registration only: teardown rides the wrapped backends' Shutdown (they call
// platform_io.ClearPlatformHandlers/ClearRendererHandlers). The RenderWindow wrapper
// covers the upstream GL renderer's per-viewport draw (registered by ImGui_ImplOpenGL3_Init)
// so a pacing-skipped viewport draws nothing.
static void ImGuiApp_ImplWin32OpenGL3_InitMultiViewportSupport(ImGuiApp_ImplWin32OpenGL3_Data* bd)
{
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    IM_ASSERT(platform_io.Renderer_CreateWindow  == nullptr);
    IM_ASSERT(platform_io.Renderer_DestroyWindow == nullptr);
    IM_ASSERT(platform_io.Renderer_SwapBuffers   == nullptr);
    IM_ASSERT(platform_io.Platform_RenderWindow  == nullptr);
    platform_io.Renderer_CreateWindow  = ImGuiApp_ImplWin32OpenGL3_Renderer_CreateWindow;
    platform_io.Renderer_DestroyWindow = ImGuiApp_ImplWin32OpenGL3_Renderer_DestroyWindow;
    platform_io.Renderer_SwapBuffers   = ImGuiApp_ImplWin32OpenGL3_Renderer_SwapBuffers;
    platform_io.Platform_RenderWindow  = ImGuiApp_ImplWin32OpenGL3_Platform_RenderWindow;
    bd->UnderlyingRendererRenderWindow = platform_io.Renderer_RenderWindow;
    platform_io.Renderer_RenderWindow  = ImGuiApp_ImplWin32OpenGL3_Renderer_RenderWindow;
}

static void ImGuiApp_ImplWin32OpenGL3_GetClientSize(void* hwnd, int* width, int* height)
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

// Synchronous backbuffer readback: this TU links only GL 1.1 entry points (no
// loader), so no PBO ring -- glReadPixels stalls the pipeline for the transfer.
// Every call returns the CURRENT frame (encode phase runs before present, so
// GL_BACK still holds it); a repeat call with no new frame rendered returns false.
static bool ImGuiApp_ImplWin32OpenGL3_CaptureFrame(ImGuiApp* app, ImGuiAppAVFrame* out_frame)
{
    ImGuiApp_ImplWin32OpenGL3_Data* bd = ImGuiApp_ImplWin32OpenGL3_GetBackendData(app);
    if (bd == nullptr || out_frame == nullptr || bd->MainDC == nullptr || bd->MainGLRC == nullptr)
        return false;
    if (app != nullptr && app->FrameID.FrameIndex <= bd->CaptureLastReturned)
        return false;   // drain/gap call: this frame was already handed out

    int width = 0;
    int height = 0;
    ImGuiApp_ImplWin32OpenGL3_GetClientSize(bd->Hwnd, &width, &height);   // resize mid-recording: report it; the recorder aborts by contract
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

bool ImGuiApp_ImplWin32OpenGL3_Init(ImGuiApp* app, const ImGuiApp_ImplWin32OpenGL3_InitInfo* init_info)
{
    IM_ASSERT(app != nullptr && app->BackendData == nullptr && "Already initialized a platform backend!");
    IM_ASSERT(ImGuiApp_ImplWin32OpenGL3_IsInitInfoValid(init_info) && "ImGuiApp_ImplWin32OpenGL3_Init: invalid init_info.");
    if (app == nullptr || app->BackendData != nullptr || !ImGuiApp_ImplWin32OpenGL3_IsInitInfoValid(init_info))
        return false;

    ImGuiApp_ImplWin32OpenGL3_Data* bd = IM_NEW(ImGuiApp_ImplWin32OpenGL3_Data)();
    app->BackendData = bd;
    bd->App = app;
    bd->Hwnd = init_info->Hwnd;
    bd->MainDC = init_info->MainDC;
    bd->MainGLRC = init_info->MainGLRC;

    if (!ImGui_ImplWin32_InitForOpenGL(init_info->Hwnd))
    {
        ImGuiApp_ImplWin32OpenGL3_Shutdown(app);
        return false;
    }
    bd->PlatformBackendInitialized = true;

    if (!ImGui_ImplOpenGL3_Init(init_info->GlslVersion))
    {
        ImGuiApp_ImplWin32OpenGL3_Shutdown(app);
        return false;
    }
    bd->RendererBackendInitialized = true;
    return true;
}

void ImGuiApp_ImplWin32OpenGL3_Shutdown(ImGuiApp* app)
{
    ImGuiApp_ImplWin32OpenGL3_Data* bd = ImGuiApp_ImplWin32OpenGL3_GetBackendData(app);
    IM_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
    if (bd == nullptr)
        return;

    if (bd->RendererBackendInitialized)
        ImGui_ImplOpenGL3_Shutdown();
    if (bd->PlatformBackendInitialized)
        ImGui_ImplWin32_Shutdown();

    app->BackendData = nullptr;
    IM_DELETE(bd);
}

void ImGuiApp_ImplWin32OpenGL3_NewFrame(ImGuiApp* app)
{
    ImGuiApp_ImplWin32OpenGL3_Data* bd = ImGuiApp_ImplWin32OpenGL3_GetBackendData(app);
    IM_ASSERT(bd != nullptr && "App or backend not initialized! Did you call ImGuiApp_ImplWin32OpenGL3_Init()?");
    IM_UNUSED(bd);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
}

void ImGuiApp_ImplWin32OpenGL3_RenderDrawData(ImGuiApp* app, ImDrawData* draw_data, const ImGuiAppFrameConfig* config)
{
    ImGuiApp_ImplWin32OpenGL3_Data* bd = ImGuiApp_ImplWin32OpenGL3_GetBackendData(app);
    IM_ASSERT(bd != nullptr && "App or backend not initialized! Did you call ImGuiApp_ImplWin32OpenGL3_Init()?");
    if (bd == nullptr)
        return;

    int width = 0;
    int height = 0;
    ImGuiApp_ImplWin32OpenGL3_GetClientSize(bd->Hwnd, &width, &height);

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

// Present phase: the encode phase runs between RenderDrawData and this, reading
// back the frame just rendered before it goes on screen.
void ImGuiApp_ImplWin32OpenGL3_PresentFrame(ImGuiApp* app, const ImGuiAppFrameConfig* config)
{
    ImGuiApp_ImplWin32OpenGL3_Data* bd = ImGuiApp_ImplWin32OpenGL3_GetBackendData(app);
    if (bd == nullptr || config == nullptr)
        return;
    if ((config->Flags & ImGuiAppFrameFlags_NoPresent) == 0)
        ::SwapBuffers((HDC)bd->MainDC);
}

bool ImGuiApp_ImplWin32OpenGL3_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config)
{
    if (config.Headless != ImGuiAppHeadlessMode_None)
    {
        IMGUIAPP_ERROR_PRINTF("imguiapp_impl_win32_opengl3: headless modes are not implemented for this backend (use win32-vulkan).\n");
        return false;
    }

    ImGuiAppPlatformData* state = IM_NEW(ImGuiAppPlatformData)();
    app->PlatformData = state;

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
        return false;

    if (!ImGuiApp_ImplWin32OpenGL3_CreateDeviceWGL(state->Hwnd, &state->MainWindow, &state->MainGLRC))
    {
        ::DestroyWindow(state->Hwnd);
        state->Hwnd = nullptr;
        return false;
    }
    wglMakeCurrent(state->MainWindow.hDC, state->MainGLRC);

    ::ShowWindow(state->Hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(state->Hwnd);

    if (ImGui::GetCurrentContext() == nullptr)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        state->OwnsImGuiContext = true;
    }

    ImGuiApp_ImplWin32OpenGL3_InitInfo init_info;
    init_info.Hwnd        = state->Hwnd;
    init_info.MainDC      = state->MainWindow.hDC;
    init_info.MainGLRC    = state->MainGLRC;
    init_info.GlslVersion = nullptr;
    if (!ImGuiApp_ImplWin32OpenGL3_Init(app, &init_info))
        return false;
    ImGuiApp_ImplWin32OpenGL3_Data* bd = ImGuiApp_ImplWin32OpenGL3_GetBackendData(app);
    bd->State = state;   // viewport hooks reach the window/GL state through the backend data

    ImGui::GetIO().ConfigFlags |= config.ConfigFlags;

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        ImGuiApp_ImplWin32OpenGL3_InitMultiViewportSupport(bd);

    app->Platform.Name               = config.Platform.Name;
    app->Platform.NativeWindowHandle = state->Hwnd;
    ::SetWindowLongPtr(state->Hwnd, GWLP_USERDATA, (LONG_PTR)app);
    return true;
}

void ImGuiApp_ImplWin32OpenGL3_ShutdownPlatform(ImGuiApp* app)
{
    // Graphics first (wrapped imgui backends need the window/GL context alive), then the host.
    if (ImGuiApp_ImplWin32OpenGL3_GetBackendData(app) != nullptr)
        ImGuiApp_ImplWin32OpenGL3_Shutdown(app);

    ImGuiAppPlatformData* state = app->PlatformData;
    if (state == nullptr)
        return;
    if (state->OwnsImGuiContext)
    {
        ImGui::DestroyContext();
        state->OwnsImGuiContext = false;
    }
    if (state->Hwnd != nullptr)
        ::SetWindowLongPtr(state->Hwnd, GWLP_USERDATA, 0);
    if (state->Hwnd != nullptr)
        ImGuiApp_ImplWin32OpenGL3_CleanupDeviceWGL(state->Hwnd, &state->MainWindow);
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
    IM_DELETE(state);
    app->PlatformData = nullptr;
}

static const ImGuiAppPlatformBackend ImGuiApp_ImplWin32OpenGL3_PlatformBackend =
{
    ImGuiApp_ImplWin32OpenGL3_InitPlatform,
    ImGuiApp_ImplWin32OpenGL3_ShutdownPlatform,
    ImGuiApp_ImplWin32_RunLoop,
    ImGuiApp_ImplWin32OpenGL3_CaptureFrame,
    "imguiapp_impl_win32_opengl3",
    ImGuiApp_ImplWin32OpenGL3_Shutdown,
    ImGuiApp_ImplWin32OpenGL3_NewFrame,
    ImGuiApp_ImplWin32OpenGL3_RenderDrawData,
    ImGuiApp_ImplWin32OpenGL3_PresentFrame,
};

const ImGuiAppPlatformBackend* ImGuiAppGetPlatformBackend() { return &ImGuiApp_ImplWin32OpenGL3_PlatformBackend; }

#endif // #ifndef IMGUI_DISABLE
