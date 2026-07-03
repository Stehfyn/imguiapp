// Shared win32 platform layer: window procedure + message-pump main loop.
// Renderer-agnostic; linked alongside whichever win32 renderer backend the build selects.

#include "imgui_applayer.h"
#include "imapp_impl_win32_state.h"

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
            app->OnDrawFrame();
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
    HWND hwnd = (HWND)app->Platform.NativeWindowHandle;
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
        if (hwnd && ::IsIconic(hwnd))
        {
            ::Sleep(10);
            continue;
        }
        ImGui::AppPacerWait(app);   // unconditional; Off returns immediately. Modal WM_TIMER repaints stay unpaced.
        app->OnDrawFrame();
    }

    app->Shutdown();
    return 0;
}
