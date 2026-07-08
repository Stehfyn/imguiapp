#pragma once

// dear imgui app, v0.5.0 WIP
// (applayer compile-time configuration -- the imconfig.h analog: #define switches only)

// Macro namespaces (N18): IMGUIAPP_* = applayer library defines; IMGUIX_* = imguix umbrella
// build switches. Nothing else claims either prefix.

// Export grammar (B4): applayer + backends/* export through IMGUI_API (the applayer ships as
// part of the imgui build unit); IMGUIX_API belongs to the imguix umbrella TU alone.

// Defined -> the authoring tools (Composer, graph editor UI, canvas engine, preview surfaces)
// compile out, leaving the runtime (model, codegen, recorder, decoder, interpreter core, anim, AV).
// CMake option IMGUIX_ENABLE_TOOLS=OFF defines it build-wide.
// #define IMGUIX_DISABLE_TOOLS

// Defined -> strip the std::thread default thread backend (<thread>/<mutex> leave the build);
// the app must call ImGui::SetAppThreadFuncs() before any recording starts.
// #define IMGUIAPP_DISABLE_DEFAULT_THREAD_FUNCS

// Override the diagnostic-output / fatal-exit seams (harness + backends; defaults in imguiapp.h).
// #define IMGUIAPP_ERROR_PRINTF(_FMT,...) MyErrorLog(_FMT, ##__VA_ARGS__)
// #define IMGUIAPP_ABORT()                MyFatalHandler()
