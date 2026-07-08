// dear imgui app: Platform Host for Win32 (shared window procedure + message-pump main loop)
// This needs to be used along with a sibling Renderer Host (imguiapp_impl_win32_opengl3, imguiapp_impl_win32_vulkan)
// Implemented features:
//  [X] Platform: Shared WndProc chaining the wrapped imgui_impl_win32 handler.
//  [X] Platform: Paced message-pump main loop; minimized app sleeps unless recording.

#pragma once
#include "imguiapp.h"      // ImGuiApp
#ifndef IMGUI_DISABLE

// Shared win32 message-pump main loop (renderer-agnostic).
int ImGuiApp_ImplWin32_RunLoop(ImGuiApp* app);

// Shared win32 window procedure; sibling renderer hosts install it in their WNDCLASSEX.
// - Intentionally commented out in a '#if 0' block to avoid dragging dependencies on <windows.h> from this header.
// - You should COPY the line below into your .cpp code to forward declare the function.
#if 0
extern LRESULT WINAPI ImGuiApp_ImplWin32_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

#endif // #ifndef IMGUI_DISABLE
