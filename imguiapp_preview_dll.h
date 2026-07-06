#pragma once

// DLL preview backend (F78 design: docs/dll-preview-design.md). The high-fidelity sibling of the F67/F68
// interpreter: instead of interpreting the graph it emits the real program (GenerateAppPreviewModuleCode),
// compiles it at runtime into a SELF-CONTAINED DLL (the module owns its entire runtime -- its own ImGui
// context + allocator + app), loads it, and drives it by COPY MARSHALLING: the host copies a control's
// TempData bytes in, ticks a frame, and copies Persist/Temp bytes out. Nothing -- pointer, context,
// allocator -- is shared across the boundary, so the preview works no matter how the consumer links imguix
// (static or shared). When no toolset is present the composer falls back to the interpreter; that is the
// no-compiler path, not an error.

#include "imguiapp_nodes.h"   // ImGuiAppGraph

struct ImGuiAppPreviewDll;   // opaque per-document session (heap, no TU globals)

namespace ImGui
{
  // True when a pinned cl.exe + its environment were located on this box (cached). When false,
  // AppPreviewDllCreate returns null with a note and the composer uses the interpreter.
  IMGUI_API bool                 AppPreviewDllToolsetAvailable();

  // Emit the module for `graph`, compile it into `scratch_dir` (created if absent) as a self-contained DLL,
  // load it, verify its ImGuiAppPreview_ABI() matches the host, and create the running instance. Returns
  // null on no-toolset OR any compile/load/ABI failure, with the reason (compiler diagnostics on a compile
  // failure) in `err`. The graph is borrowed read-only. Free with AppPreviewDllDestroy.
  IMGUI_API ImGuiAppPreviewDll*  AppPreviewDllCreate(const ImGuiAppGraph* graph, const char* scratch_dir, char* err, int err_size);
  IMGUI_API void                 AppPreviewDllDestroy(ImGuiAppPreviewDll* session);

  // Advance the DLL instance one frame at dt, inside its own context.
  IMGUI_API void                 AppPreviewDllTick(ImGuiAppPreviewDll* session, float dt);

  // Copy a control's bytes across the boundary by label. `temp` true = the TempData input range, false =
  // Persist+LastTemp state. CopyOut returns bytes read into `out` (<= cap); CopyIn returns bytes written
  // from `in` (<= the range). 0 = control/label unknown or opaque (not snapshottable).
  IMGUI_API int                  AppPreviewDllCopyOut(ImGuiAppPreviewDll* session, const char* control_label, bool temp, void* out, int cap);
  IMGUI_API int                  AppPreviewDllCopyIn(ImGuiAppPreviewDll* session, const char* control_label, bool temp, const void* in, int size);

  // Recompile the (edited) graph into a new self-contained DLL and hot-swap, preserving each surviving
  // control's Persist bytes by COPY (out of the old instance, into the new by label) -- the F68 preserve
  // policy on the DLL path. A compile failure keeps the last-good instance running and returns false (err set).
  IMGUI_API bool                 AppPreviewDllReload(ImGuiAppPreviewDll* session, const ImGuiAppGraph* graph, char* err, int err_size);

  // F78.5 in-panel render: close the "live" loop by moving the DLL app's rendered frame across the boundary as
  // COPIED BYTES (draw data + font atlas), then CPU-rasterizing it host-side -- no GPU/context/pointer shared.
  // SetDisplaySize resizes the DLL's own viewport so its NEXT tick renders at the panel size.
  IMGUI_API void                 AppPreviewDllSetDisplaySize(ImGuiAppPreviewDll* session, int w, int h);

  // Copy the last-ticked frame's draw data + font atlas out of the DLL and rasterize it into `out_rgba` (a
  // `w*h*4` RGBA32 buffer, resized as needed), cleared to `clear_col` (IM_COL32) then filled with the DLL's
  // triangles (per-vertex color * atlas alpha, clipped per command). Returns true when any geometry landed
  // (the frame is non-blank); false when the module lacks the frame ABI or produced nothing (interpreter
  // fallback). Call after AppPreviewDllTick so the frame reflects the current tick.
  IMGUI_API bool                 AppPreviewDllRasterizeFrame(ImGuiAppPreviewDll* session, int w, int h, unsigned int clear_col, ImVector<unsigned char>* out_rgba);
}
