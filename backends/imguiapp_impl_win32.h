// dear imgui app: Platform Host for Win32 (shared window procedure + message-pump main loop)
// This needs to be used along with a sibling Renderer Host (imguiapp_impl_win32_opengl3, imguiapp_impl_win32_vulkan)
// Implemented features:
//  [X] Platform: Shared WndProc chaining the wrapped imgui_impl_win32 handler.
//  [X] Platform: Paced message-pump main loop; minimized app sleeps unless recording.

#pragma once
#include <windows.h>

struct ImGuiApp;

// Shared win32 window procedure; sibling renderer hosts install it in their WNDCLASSEX.
LRESULT WINAPI ImGuiApp_ImplWin32_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Shared win32 message-pump main loop (renderer-agnostic).
int ImGuiApp_ImplWin32_RunLoop(ImGuiApp* app);
