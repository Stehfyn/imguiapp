#pragma once
#include <windows.h>

struct ImGuiApp;

struct ImGuiAppPlatformState
{
    struct WGLWindowData { HDC hDC; };
    HWND          Hwnd;
    WNDCLASSEXA   WindowClass;
    HGLRC         MainGLRC;
    WGLWindowData MainWindow;
};

LRESULT WINAPI ImGuiApp_ImplWin32_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Shared win32 message-pump main loop (renderer-agnostic). Defined in imguiapp_impl_win32.cpp.
int ImGuiApp_ImplWin32_RunLoop(ImGuiApp* app);