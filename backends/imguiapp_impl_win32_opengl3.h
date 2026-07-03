#pragma once
#include "imguix.h"

struct ImGuiApp;
struct ImGuiAppPlatformState;

IMGUIX_API bool ImGuiApp_Win32OpenGL3_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config);
IMGUIX_API void ImGuiApp_Win32OpenGL3_ShutdownPlatform(ImGuiApp* app);
