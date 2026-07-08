// dear imgui app: Platform Host for Win32 (shared window procedure + message-pump main loop)
// This needs to be used along with a sibling Renderer Host (imguiapp_impl_win32_opengl3, imguiapp_impl_win32_vulkan)

// Implemented features:
//  [X] Platform: Shared WndProc chaining the wrapped imgui_impl_win32 handler.
//  [X] Platform: Paced message-pump main loop; minimized app sleeps unless recording.

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
//  2026-07-08: Misc: Backend-internal symbols prefixed; IMGUI_DISABLE guards added.

#include "imguiapp.h"
#ifndef IMGUI_DISABLE
#include "imguiapp_impl_win32.h"

#include "imgui_impl_win32.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI ImGuiApp_ImplWin32_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_TIMER:
        if (ImGuiApp* app = (ImGuiApp*)::GetWindowLongPtr(hWnd, GWLP_USERDATA))
            app->Frame();
        return 0;
    case WM_ENTERMENULOOP:
    case WM_ENTERSIZEMOVE:
        SetTimer(hWnd, 0, USER_TIMER_MINIMUM, 0);
        return 0;
    case WM_EXITMENULOOP:
    case WM_EXITSIZEMOVE:
        KillTimer(hWnd, 0);
        return 0;
    case WM_SIZE:
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

int ImGuiApp_ImplWin32_RunLoop(ImGuiApp* app)
{
    HWND hwnd = (HWND)app->PlatformWindowHandle;
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;
        // Recording must encode every frame -- a minimized app keeps running so the
        // recorder can synthesize pause frames (the backend renders nothing at 0 size).
        if (hwnd && ::IsIconic(hwnd) && app->Recorder == nullptr)
        {
            ::Sleep(10);
            continue;
        }
        ImGui::AppPacerWait(app);   // unconditional; Off returns immediately. Modal WM_TIMER repaints stay unpaced.
        app->Frame();
    }

    app->Shutdown();
    return 0;
}

#endif // #ifndef IMGUI_DISABLE
