#pragma once
#include "imguix.h"

struct ImGuiApp;
struct ImGuiAppPlatformState;

IMGUIX_API bool ImGuiApp_ImplSDL2WGPU_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config);
IMGUIX_API void ImGuiApp_ImplSDL2WGPU_ShutdownPlatform(ImGuiApp* app);
