#pragma once

#include "imgui.h"

// F77: imguix-core shared library. Applayer entry points are exported from the core DLL by the linker
// (WINDOWS_EXPORT_ALL_SYMBOLS), so IMGUIX_API stays empty when building the core; consumers import.
// Defined here because imguiapp_config.h is the header common to every applayer public header.
#ifndef IMGUIX_API
#if defined(_WIN32) && !defined(IMGUIX_CORE_EXPORTS)
#define IMGUIX_API __declspec(dllimport)
#else
#define IMGUIX_API
#endif
#endif

struct ImGuiApp;

typedef int ImGuiAppFrameFlags;
enum ImGuiAppFrameFlags_
{
    ImGuiAppFrameFlags_None              = 0,
    ImGuiAppFrameFlags_NoClear           = 1 << 0,
    ImGuiAppFrameFlags_NoPresent         = 1 << 1,
    ImGuiAppFrameFlags_NoPlatformWindows = 1 << 2,
};

struct ImGuiAppFrameConfig
{
    ImVec4             ClearColor;
    ImGuiAppFrameFlags Flags;

    ImGuiAppFrameConfig()
        : ClearColor(0.0f, 0.0f, 0.0f, 1.0f)
        , Flags(ImGuiAppFrameFlags_None)
    {
    }
};

enum ImGuiAppStyle_
{
    ImGuiAppStyle_Dark    = 0,
    ImGuiAppStyle_Light   = 1,
    ImGuiAppStyle_Classic = 2,
};
typedef int ImGuiAppStyle;

struct ImGuiAppPlatform
{
    const char* Name;
    void*       NativeWindowHandle;
};

typedef int ImGuiAppHeadlessMode;
enum ImGuiAppHeadlessMode_
{
    ImGuiAppHeadlessMode_None = 0,   // normal windowed app
    ImGuiAppHeadlessMode_Null,       // no GPU, no pixels (test engine only; backend CaptureFrame stays null)
    ImGuiAppHeadlessMode_Offscreen,  // GPU renders to an offscreen target; no OS window, CaptureFrame works
};

struct ImGuiAppConfig
{
    ImGuiAppPlatform     Platform;
    ImGuiConfigFlags     ConfigFlags;
    ImGuiAppStyle        Style;
    ImVec4               ClearColor;
    float                FontScale;
    float                DpiScale;
    ImGuiAppHeadlessMode Headless;
    bool                 PersistSettings;
    const char*          WindowTitle;
    int                  WindowWidth;
    int                  WindowHeight;

    ImGuiAppConfig()
    {
        Platform.Name               = nullptr;
        Platform.NativeWindowHandle = nullptr;
        ConfigFlags                 = 0;
        Style                       = ImGuiAppStyle_Dark;
        ClearColor                  = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        FontScale                   = 1.0f;
        DpiScale                    = 1.0f;
        Headless                    = ImGuiAppHeadlessMode_None;
        PersistSettings             = true;
        WindowTitle                 = nullptr;
        WindowWidth                 = 0;
        WindowHeight                = 0;
    }
};
