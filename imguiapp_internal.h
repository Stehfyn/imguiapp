#pragma once

// Tool-UI interfaces (the imgui_internal.h analog, Phase A4). Everything under IMGUIX_DISABLE_TOOLS is
// compiled out in a lean build -- so a lean consumer that includes this header sees NO UI API. The core
// model/codegen (imguiapp_nodes.h), interpreter (imguiapp_preview.h), recorder/decoder (imguiapp_av.h) and
// runtime (imguiapp.h) interfaces stay in their own UNCONDITIONAL core headers, included here for the types
// the tool-UI decls reference.

#include "imguiapp.h"
#include "imguiapp_nodes.h"        // graph MODEL + CODEGEN (core) -- the types the editor UI decls reference
#include "imguiapp_preview.h"      // interpreter CORE (F67)

#ifndef IMGUIX_DISABLE_TOOLS

#include "imguiapp_canvas.h"       // canvas UI (itself gated)
#include "imguiapp_preview_dll.h"  // DLL preview SURFACE (F78)

namespace ImGui
{
  // The demo Composer's document graph inside `host` (its GraphDocData storage entry), or null before
  // composition. Test harnesses drive the editor camera through it. (Was in imguiapp.h; tool-coupled.)
  IMGUI_API ::ImGuiAppGraph* AppLayerDemoGraph(ImGuiApp* host);

  // Monospace font for the generated-code inspector (space-padded alignment needs fixed width). Register
  // at font-init; null leaves the inspector on the UI font. (Was in imguiapp.h; tool-coupled.)
  IMGUI_API void SetAppCodeFont(::ImGuiAppGraph* g, ImFont* font);

  //---------------------------------------------------------------------------
  // F68 preview SURFACE + brushing (relocated from imguiapp_preview.h, Phase A3). Definitions live in
  // imguiapp_preview.cpp under the same IMGUIX_DISABLE_TOOLS guard.
  //---------------------------------------------------------------------------

  // On-camera surface (design 8.1): with it enabled, each interpreted control's OnRender submits its
  // manifest-bound field widgets into the CURRENT ImGui window (the composer's Preview panel). Disabled
  // (the default / F67 CORE path) the controls issue no ImGui calls, so the interpreter runs headless.
  IMGUI_API void             AppPreviewSetSurface(ImGuiAppPreview* session, bool on);

  // Render the interpreter's controls without advancing the model -- RenderApp only, no Task/Command pass.
  // The paused half of run/pause: widgets still show (and can be poked) but the simulation is frozen.
  IMGUI_API void             AppPreviewRender(ImGuiAppPreview* session);

  // Selection brushing (design 8.2), composer -> preview: the selected/hovered node's widget group haloes
  // in the surface. Pass -1 for none. Set before the frame; consumed while the controls render.
  IMGUI_API void             AppPreviewSetBrush(ImGuiAppPreview* session, int selected_node_id, int hover_node_id);

  // Preview -> composer: the node whose widget group the mouse rested over this frame (-1 = none). Cheap to
  // read after AppPreviewFrame/Render; the composer forwards it to AppGraphHoverNode so the canvas haloes.
  IMGUI_API int              AppPreviewHoveredNode(const ImGuiAppPreview* session);

  // Preview -> composer: the node whose widget group was clicked (latched until taken). The composer takes
  // it once per frame and promotes it to the primary selection; -1 = no pending click.
  IMGUI_API int              AppPreviewTakeClickedNode(ImGuiAppPreview* session);

  // --- Editor-UI decls relocated from imguiapp_nodes.h land below (Phase A2). ---
}

#endif // IMGUIX_DISABLE_TOOLS
