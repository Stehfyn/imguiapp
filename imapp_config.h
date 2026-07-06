#pragma once

// Applayer compile-time configuration (the imconfig.h analog): #define switches only.

// Defined -> the authoring tools (Composer, graph editor UI, canvas engine, preview surfaces)
// compile out, leaving the runtime (model, codegen, recorder, decoder, interpreter core, anim, AV).
// CMake option IMGUIX_ENABLE_TOOLS=OFF defines it build-wide.
// #define IMGUIX_DISABLE_TOOLS
