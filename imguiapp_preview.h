#pragma once

// Previewer interpreter core (F66 design: docs/previewer-design.md). A SECOND backend for the object
// model beside codegen: it builds a real ImGuiApp from the authored graph and evaluates the model every
// frame -- so RegisterAppStorage, the four-phase pipeline, and ImGuiAppStateHistory apply verbatim.
// One ImGuiAppPreviewControl carries dynamic Persist|LastTemp|Temp byte buffers laid out from an
// effective-field manifest; one evaluator is a value-returning walk of the AppEventExprCheck grammar.
//
// F67 scope = App/Layer/Window/Sidebar/Control(design-draft)/Struct/Field/events/commands. Op-fold
// evaluation (F55) and animation-builtin update (F56) are named stubs -- see imguiapp_preview.cpp.

#include "imguiapp_nodes.h"   // ImGuiAppGraph, ImGuiApp, IM_LABEL_SIZE, field/event model

struct ImGuiAppPreview;   // opaque per-document interpreter session (heap, no TU globals)

namespace ImGui
{
  // Build a session from an authored graph: push the framework core layers (Task/Command/Window), then one
  // interpreter control per Control node in dependency-topo order with its storage registered. Returns null
  // on a dependency cycle (reason in err). The graph is borrowed read-only and never mutated by running it.
  IMGUI_API ImGuiAppPreview* AppPreviewCreate(const ImGuiAppGraph* graph, char* err, int err_size);
  IMGUI_API void             AppPreviewDestroy(ImGuiAppPreview* session);

  // The interpreter's real ImGuiApp -- for contract-suite reuse (F69) and inspection.
  IMGUI_API ImGuiApp*        AppPreviewApp(ImGuiAppPreview* session);

  // Drive one frame at dt (UpdateApp then RenderApp): Task in topo order, Command collect/dispatch-once,
  // Window render. Advances the session tick.
  IMGUI_API void             AppPreviewFrame(ImGuiAppPreview* session, float dt);

  // Scripted input seam: the headless equivalent of a widget recording into TempData during OnRender.
  // Sticky until changed. F68 replaces this with real widget input on the composed window surface.
  // Returns false when the node id / temp field name is unknown. Value is coerced to the field's type.
  IMGUI_API bool             AppPreviewSetInput(ImGuiAppPreview* session, int node_id, const char* temp_field, double value);

  // Read a running control's live Persist field, coerced to double. False when the node/field is unknown.
  IMGUI_API bool             AppPreviewGetPersist(ImGuiAppPreview* session, int node_id, const char* persist_field, double* out_value);

  // Dispatched-command log (design 4.3): one entry per dispatch in first-emission order.
  IMGUI_API int              AppPreviewDispatchCount(const ImGuiAppPreview* session);
  IMGUI_API int              AppPreviewDispatchCommandAt(const ImGuiAppPreview* session, int index);   // command value, -1 out of range
  IMGUI_API const char*      AppPreviewCommandName(const ImGuiAppPreview* session, int command_value); // "" if unknown
}
