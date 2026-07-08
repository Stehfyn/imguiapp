#pragma once

// Applayer compile-time configuration (the imconfig.h analog): #define switches only.

// Export grammar (B4): applayer + backends/* export through IMGUI_API (the applayer ships as
// part of the imgui build unit); IMGUIX_API belongs to the imguix umbrella TU alone.

// Defined -> the authoring tools (Composer, graph editor UI, canvas engine, preview surfaces)
// compile out, leaving the runtime (model, codegen, recorder, decoder, interpreter core, anim, AV).
// CMake option IMGUIX_ENABLE_TOOLS=OFF defines it build-wide.
// #define IMGUIX_DISABLE_TOOLS

// Defined -> strip the std::thread default thread backend (<thread>/<mutex> leave the build);
// the app must call ImGui::SetAppThreadFuncs() before any recording starts.
// #define IMGUIAPP_DISABLE_DEFAULT_THREAD_FUNCS
