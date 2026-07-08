#pragma once
#include "imguix.h"

struct ImGuiApp;
struct ImGuiAppPlatformData;

IMGUIX_API bool ImGuiApp_ImplWin32Vulkan_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config);
IMGUIX_API void ImGuiApp_ImplWin32Vulkan_ShutdownPlatform(ImGuiApp* app);
