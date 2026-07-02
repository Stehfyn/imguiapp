#pragma once

/*

Index of this file:
// [SECTION] Header mess
// [SECTION] Reflection field helpers (VisitAppFields, DrawAppField, EditAppField)
// [SECTION] Node rendering (BeginAppNode, EndAppNode, DrawAppNodeFields, EditAppNodeFields)
// [SECTION] Design-phase node drafts (ImGuiAppFieldType, ImGuiAppFieldDesc, ImGuiAppNodeDraft)
// [SECTION] Graph topology and persistence (ImGuiAppNodeLink, link capture, save/load)
// [SECTION] Code generation (GenerateAppControlCode)

This header isolates the reflection-driven, data-driven node tooling for ImGuiAppLayer.
Like ImStructTable, it pulls in the C++20 reflection library and a little STL; that
dependency is deliberately kept out of the lean core (imgui_applayer.h). Public entry
points stay imgui-shaped: pointer parameters, char[] buffers, ImGuiID, ImVector.

Reflection-capable subset: the C++20 reflection library only reflects aggregates, and it
miscounts (explodes) on raw array members such as char Label[128] -- the same limitation
ImStructTable documents (wrap arrays in a struct). So data driven through these helpers
should be a plain-scalar aggregate. Codegen (later steps) emits exactly such aggregates.

*/

//-----------------------------------------------------------------------------
// [SECTION] Header mess
//-----------------------------------------------------------------------------

#include "imgui.h"
#include "imgui_internal.h"               // ImFormatString
#include "imgui_applayer.h"               // ImGuiApp, IM_LABEL_SIZE, ImGuiType<>
#include "reflect/reflect"                // reflect::for_each, member_name, get, type_name

#include <format>                         // std::format (field stringify, confined to this header)
#include <string>                         // std::string (transient, copied into char[] at the boundary)
#include <string_view>                    // reflect member/type names
#include <type_traits>                    // dispatch traits

//-----------------------------------------------------------------------------
// [SECTION] Reflection field helpers (VisitAppFields, DrawAppField, EditAppField)
//-----------------------------------------------------------------------------

namespace ImGui
{
  // True when std::format can stringify U with "{}". The disabled primary std::formatter
  // is not default-constructible; enabled specializations are. Standard detection idiom.
  template <typename U>
  inline constexpr bool ImIsFormattable = std::is_default_constructible_v<std::formatter<std::remove_cvref_t<U>, char>>;

  // True for a fixed-size char buffer member (e.g. char Label[128]) -> edited as text.
  template <typename U>
  inline constexpr bool ImIsCharArray = std::is_array_v<U> && std::is_same_v<std::remove_cv_t<std::remove_extent_t<U>>, char>;

  // Read-only render of a single reflected field value. Never mutates.
  template <typename T>
  inline void DrawAppField(const char* label, const T* value)
  {
    IM_ASSERT(value != nullptr);

    if constexpr (ImIsCharArray<T>)
    {
      ImGui::Text("%s: %s", label, *value);
    }
    else if constexpr (ImIsFormattable<T>)
    {
      char buf[IM_LABEL_SIZE];
      std::string s = std::format("{}", *value);
      ImFormatString(buf, IM_ARRAYSIZE(buf), "%s", s.c_str());
      ImGui::Text("%s: %s", label, buf);
    }
    else
    {
      std::string_view tn = reflect::type_name(*value);
      ImGui::Text("%s: <%.*s>", label, (int)tn.size(), tn.data());
    }
  }

  // Editable widget for a single reflected field value. Returns true if the value changed.
  // Falls back to a read-only render for types with no obvious editor.
  template <typename T>
  inline bool EditAppField(const char* label, T* value)
  {
    IM_ASSERT(value != nullptr);

    if constexpr (ImIsCharArray<T>)
      return ImGui::InputText(label, *value, std::extent_v<T>);
    else if constexpr (std::is_same_v<T, bool>)
      return ImGui::Checkbox(label, value);
    else if constexpr (std::is_same_v<T, float>)
      return ImGui::DragFloat(label, value);
    else if constexpr (std::is_same_v<T, double>)
      return ImGui::InputDouble(label, value);
    else if constexpr (std::is_same_v<T, ImVec2>)
      return ImGui::DragFloat2(label, &value->x);
    else if constexpr (std::is_same_v<T, ImVec4>)
      return ImGui::ColorEdit4(label, &value->x);
    else if constexpr (std::is_integral_v<T>)
    {
      int v = (int)*value;
      bool changed = ImGui::DragInt(label, &v);
      if (changed)
        *value = (T)v;
      return changed;
    }
    else
    {
      DrawAppField(label, value);
      return false;
    }
  }

  // Visit each reflected field of an aggregate: visitor(int index, std::string_view name, auto& value).
  // The value is passed by reference so visitors may read or mutate it in place. Pass a const T*
  // to visit fields read-only (value deduces const).
  template <typename T, typename Visitor>
  inline void VisitAppFields(T* obj, Visitor visitor)
  {
    IM_ASSERT(obj != nullptr);

    reflect::for_each([&](auto I)
    {
      visitor((int)I, reflect::member_name<I>(*obj), reflect::get<I>(*obj));
    }, *obj);
  }
}

//-----------------------------------------------------------------------------
// [SECTION] Node rendering (BeginAppNode, EndAppNode, DrawAppNodeFields, EditAppNodeFields)
//-----------------------------------------------------------------------------

namespace ImGui
{
  // imnodes node scaffold. imnodes itself stays confined to imgui_applayer_nodes.cpp.
  IMGUI_API void BeginAppNode(int id, const char* title);
  IMGUI_API void EndAppNode();

  // Renamable node scaffold: the title bar shows *name and turns into an inline text box on
  // double-click (list-view rename). Commits on Enter or focus loss, cancels on Escape. *editing_node_id
  // is caller-owned single-slot state holding the id of the node being renamed (or -1 for none); the
  // helper sets it on double-click and clears it when the edit ends. Returns true if *name changed this
  // frame. Pair with EndAppNode(). imnodes suppresses node-drag while the box is active, so typing and
  // text-selection drags do not move the node.
  IMGUI_API bool BeginAppNodeRenamable(int id, char* name, int name_size, int* editing_node_id);

  // Render every reflected field of an aggregate as read-only labelled rows in the current node.
  template <typename T>
  inline void DrawAppNodeFields(const T* data)
  {
    IM_ASSERT(data != nullptr);

    VisitAppFields(data, [](int idx, std::string_view name, const auto& value)
    {
      IM_UNUSED(idx);
      char label[IM_LABEL_SIZE];
      ImFormatString(label, IM_ARRAYSIZE(label), "%.*s", (int)name.size(), name.data());
      DrawAppField(label, &value);
    });
  }

  // Editable reflected field rows inside the current node. Returns true if any value changed.
  // Item width is clamped so nodes stay compact (imnodes nodes auto-size to their content).
  template <typename T>
  inline bool EditAppNodeFields(T* data)
  {
    IM_ASSERT(data != nullptr);

    bool changed = false;
    VisitAppFields(data, [&changed](int idx, std::string_view name, auto& value)
    {
      IM_UNUSED(idx);
      char label[IM_LABEL_SIZE];
      ImFormatString(label, IM_ARRAYSIZE(label), "%.*s", (int)name.size(), name.data());
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
      changed |= ImGui::EditAppField(label, &value);
    });
    return changed;
  }
}

//-----------------------------------------------------------------------------
// [SECTION] Design-phase node drafts (ImGuiAppFieldType, ImGuiAppFieldDesc, ImGuiAppNodeDraft)
//-----------------------------------------------------------------------------

// A draft describes a node whose backing C++ type does not exist yet: the user designs its
// fields here, then codegen (later steps) emits a reflection-capable aggregate from it. Once
// emitted and compiled, that aggregate is reflected by the helpers above. Drafts therefore use
// the same plain-scalar field vocabulary the codegen can emit.
typedef int ImGuiAppFieldType;
enum ImGuiAppFieldType_
{
  ImGuiAppFieldType_Float = 0,
  ImGuiAppFieldType_Int,
  ImGuiAppFieldType_Bool,
  ImGuiAppFieldType_Double,
  ImGuiAppFieldType_Vec2,    // ImVec2
  ImGuiAppFieldType_Vec4,    // ImVec4
  ImGuiAppFieldType_String,  // char[ArraySize]
  ImGuiAppFieldType_Struct,  // a nested struct, named by StructType
  ImGuiAppFieldType_COUNT,
};

struct ImGuiAppFieldDesc
{
  char              Name[IM_LABEL_SIZE];
  ImGuiAppFieldType Type;
  int               ArraySize;   // buffer length for ImGuiAppFieldType_String; ignored otherwise
  char              StructType[IM_LABEL_SIZE];   // referenced struct type name for ImGuiAppFieldType_Struct

  ImGuiAppFieldDesc() { Name[0] = 0; Type = ImGuiAppFieldType_Float; ArraySize = 128; StructType[0] = 0; }
};

// One drafted control: a name plus its persisted and per-frame field sets. Ports/links are
// derived from graph edges in a later step, so they are not stored here yet.
struct ImGuiAppNodeDraft
{
  char                        Name[IM_LABEL_SIZE];
  ImVector<ImGuiAppFieldDesc> PersistFields;
  ImVector<ImGuiAppFieldDesc> TempFields;

  ImGuiAppNodeDraft() { ImStrncpy(Name, "NewControl", IM_ARRAYSIZE(Name)); }
};

namespace ImGui
{
  // C++ base type spelling for a field type. Shared by the draft editor and codegen.
  IMGUI_API const char* AppFieldTypeName(ImGuiAppFieldType type);

  // Draft field-list mutators (pointer-in, imgui-style).
  IMGUI_API void AppNodeDraftAddField(ImVector<ImGuiAppFieldDesc>* fields, const char* name, ImGuiAppFieldType type);
  IMGUI_API void AppNodeDraftRemoveField(ImVector<ImGuiAppFieldDesc>* fields, int index);

  // Inspector UI: rename the draft and add/remove/edit its persist and temp fields.
  IMGUI_API void EditAppNodeDraft(ImGuiAppNodeDraft* draft);

  // Just the persist/temp field editors (no Name input). Used where the name is edited elsewhere --
  // e.g. inside a renamable node title -- so the body would otherwise show a duplicate Name field.
  IMGUI_API void EditAppNodeDraftFields(ImGuiAppNodeDraft* draft);

  // Render a draft's fields as read-only node rows (no reflection: the type does not exist yet).
  IMGUI_API void DrawAppNodeDraft(const ImGuiAppNodeDraft* draft);
}

//-----------------------------------------------------------------------------
// [SECTION] Typed graph kinds (node/layer/port/edge discriminators)
//-----------------------------------------------------------------------------

// A node mirrors one slot of the live ImGuiApp object model. App is the singleton root that owns the
// Layers/Windows/Sidebars/Controls vectors (imgui_applayer.h: ImGuiApp). Layers are fixed C++ types
// (Task/Command/Status/Window) pushed via PushAppLayer<T>; Windows/Sidebars via PushAppWindow/Sidebar<T>;
// Controls (the only draftable kind) via PushAppControl<T>.
typedef int ImGuiAppNodeKind;
enum ImGuiAppNodeKind_
{
  ImGuiAppNodeKind_App = 0,
  ImGuiAppNodeKind_Layer,
  ImGuiAppNodeKind_Window,
  ImGuiAppNodeKind_Sidebar,
  ImGuiAppNodeKind_Control,
  ImGuiAppNodeKind_Struct,    // a standalone data struct (PersistData/TempData), wired into a control's DataIn
  ImGuiAppNodeKind_Field,     // one field of a struct, "exploded" out for per-field wiring (drives bindings)
  ImGuiAppNodeKind_COUNT,
};

// The four CORE layer classes (imgui_applayer.h: ImGuiAppTaskLayer..ImGuiAppWindowLayer) are the frame's
// phases: permanent, one each, immutable type -- codegen emits PushAppLayer<ImGuiAppXxxLayer>. Custom is a
// user-authored ImGuiAppLayer subclass: the NODE'S NAME is its class name, any number may exist, and codegen
// emits the subclass skeleton plus PushAppLayer<Name>.
typedef int ImGuiAppLayerType;
enum ImGuiAppLayerType_
{
  ImGuiAppLayerType_Task = 0,
  ImGuiAppLayerType_Command,
  ImGuiAppLayerType_Status,
  ImGuiAppLayerType_Window,
  ImGuiAppLayerType_Custom,
  ImGuiAppLayerType_COUNT,
};

// A port is an imnodes attribute with a role. DataOut/DataIn carry the runtime data flow; ChildOut/ChildIn
// carry containment (which window/sidebar/app owns a node). Layers are root composition slots and have no
// containment sockets. A Control has one DataOut (its PersistData), one multi-link DataIn (all its dependencies
// -- the runtime keys app->Data by PersistData TYPE, so one type-keyed intake is faithful), and one ChildOut.
typedef int ImGuiAppPortKind;
enum ImGuiAppPortKind_
{
  ImGuiAppPortKind_DataOut = 0,
  ImGuiAppPortKind_DataIn,
  ImGuiAppPortKind_ChildOut,
  ImGuiAppPortKind_ChildIn,
  ImGuiAppPortKind_COUNT,
};

// An edge is either a data dependency (producer PersistData -> consumer dependency) or a containment edge
// (child -> parent). The kind is derived from the linked ports' kinds at capture time.
typedef int ImGuiAppEdgeKind;
enum ImGuiAppEdgeKind_
{
  ImGuiAppEdgeKind_Data = 0,
  ImGuiAppEdgeKind_Containment,
  ImGuiAppEdgeKind_COUNT,
};

//-----------------------------------------------------------------------------
// [SECTION] Graph topology and persistence (ImGuiAppNodeLink, link capture, save/load)
//-----------------------------------------------------------------------------

// A user-created edge between two node ports (source -> target). StartAttr/EndAttr are STABLE port ids
// (ImGuiAppNodePort::Id), not array-derived, so they survive node reorder/delete. Kind is a trailing field
// with a default member initializer: brace-init like { id, start, end } still compiles (value-inits Kind to
// _Data) so the legacy 3-int aggregate usage and save/load format keep working.
struct ImGuiAppNodeLink
{
  int Id;
  int StartAttr;
  int EndAttr;
  ImGuiAppEdgeKind Kind = ImGuiAppEdgeKind_Data;
};

namespace ImGui
{
  // Draw the model's links inside the current node editor. Wraps imnodes (confined to the .cpp).
  IMGUI_API void DrawAppNodeLinks(const ImVector<ImGuiAppNodeLink>* links);

  // After EndNodeEditor: fold imnodes create/destroy events into the link model. New links take
  // ids from *next_link_id (incremented). Returns true if the model changed.
  IMGUI_API bool CaptureAppNodeLinks(ImVector<ImGuiAppNodeLink>* links, int* next_link_id);

  // Persist / restore a draft and its links as imgui-style text. Return false on file error.
  IMGUI_API bool SaveAppNodeGraph(const char* path, const ImGuiAppNodeDraft* draft, const ImVector<ImGuiAppNodeLink>* links);
  IMGUI_API bool LoadAppNodeGraph(const char* path, ImGuiAppNodeDraft* draft, ImVector<ImGuiAppNodeLink>* links);

  // Multi-draft variants: persist / restore a whole graph of drafts plus its links. Same text
  // format as the single-draft pair -- each draft is one "[Draft]" section -- so a single-draft
  // file loads as a one-element vector and round-trips. *drafts is cleared before loading.
  IMGUI_API bool SaveAppNodeGraphMulti(const char* path, const ImVector<ImGuiAppNodeDraft>* drafts, const ImVector<ImGuiAppNodeLink>* links);
  IMGUI_API bool LoadAppNodeGraphMulti(const char* path, ImVector<ImGuiAppNodeDraft>* drafts, ImVector<ImGuiAppNodeLink>* links);
}

//-----------------------------------------------------------------------------
// [SECTION] Code generation (GenerateAppControlCode)
//-----------------------------------------------------------------------------

namespace ImGui
{
  // Emit a hand-written-looking ImGuiAppControl from a draft: the persist aggregate (<Name>Data),
  // the per-frame aggregate (<Name>TempData) and the control skeleton with stubbed overrides.
  // Scalar-only emissions are reflection-capable and round-trip back into the helpers above; a
  // String field emits char[N], which (like any raw array) falls outside the reflection subset.
  // Output is appended to *out.
  IMGUI_API void GenerateAppControlCode(const ImGuiAppNodeDraft* draft, ImGuiTextBuffer* out);
}

//-----------------------------------------------------------------------------
// [SECTION] Typed node graph (ports, nodes, graph model, factory, codegen, persistence)
//-----------------------------------------------------------------------------

// One imnodes attribute on a node, stored (never index-derived) so its id is stable across reorder/delete.
struct ImGuiAppNodePort
{
  int              Id;          // from ImGuiAppGraph::NextId; == imnodes attribute id
  ImGuiAppPortKind Kind;
  char             Name[IM_LABEL_SIZE];
  ImGuiID          DataTypeId;  // ImGuiType<PersistData>::ID for DataOut/DataIn (the data-flow key); 0 otherwise

  ImGuiAppNodePort() { Id = 0; Kind = ImGuiAppPortKind_DataOut; Name[0] = 0; DataTypeId = 0; }
};

struct ImGuiAppCommandDesc
{
  char Name[IM_LABEL_SIZE];

  ImGuiAppCommandDesc() { ImStrncpy(Name, "NewCommand", IM_ARRAYSIZE(Name)); }
};

// The framework's event idiom, made authorable. OnRender records raw input into TempData (zeroed every frame);
// OnUpdate receives BOTH this frame's TempData and last frame's, so user code derives events by comparing them --
// the demo's `temp_data->hovered ^ last_temp_data->hovered` (Breathing) and `temp_data->generate` (RandomTime).
// An edge names which comparison the generated OnUpdate guards with.
typedef int ImGuiAppEventEdge;
enum ImGuiAppEventEdge_
{
  ImGuiAppEventEdge_Rising = 0,   // temp && !last   -- became true this frame (a click / press)
  ImGuiAppEventEdge_Falling,      // !temp && last   -- became false this frame (a release)
  ImGuiAppEventEdge_Changed,      // temp ^ last     -- either transition (Breathing's hover edge)
  ImGuiAppEventEdge_Active,       // temp            -- level: every frame while true
  ImGuiAppEventEdge_COUNT,
};

// What the event does when its edge fires. SetField mutates PersistData (OnUpdate is the sole mutator);
// EmitCommand routes through the command pipeline: OnUpdate latches a persist bool, OnGetCommand emits it.
typedef int ImGuiAppEventAction;
enum ImGuiAppEventAction_
{
  ImGuiAppEventAction_SetField = 0,
  ImGuiAppEventAction_EmitCommand,
  ImGuiAppEventAction_COUNT,
};

// One authored event on a Control: "when <TempField> <edge> -> <action>". Codegen turns each into a guarded
// block in OnUpdate (and, for EmitCommand, the latch + OnGetCommand emission).
struct ImGuiAppEventDesc
{
  char                TempField[IM_LABEL_SIZE];   // watched TempData field
  ImGuiAppEventEdge   Edge;
  ImGuiAppEventAction Action;
  char                DstField[IM_LABEL_SIZE];    // SetField: PersistData destination
  char                Expr[IM_LABEL_SIZE];        // SetField: source expression (emitted verbatim); empty -> temp_data-><TempField>
  char                Command[IM_LABEL_SIZE];     // EmitCommand: one of the control's selected commands

  ImGuiAppEventDesc() { TempField[0] = 0; Edge = ImGuiAppEventEdge_Changed; Action = ImGuiAppEventAction_SetField; DstField[0] = 0; Expr[0] = 0; Command[0] = 0; }
};

// One node in the authored graph. Embeds ImGuiAppNodeDraft so the existing rename/field-edit/codegen
// helpers apply verbatim and a legacy "[Draft]" maps 1:1 to a Control node. Most fields are kind-specific.
struct ImGuiAppNode
{
  int               Id;            // from NextId; == imnodes node id
  ImGuiAppNodeKind  Kind;
  ImGuiAppNodeDraft Draft;         // Draft.Name is the node label; PersistFields/TempFields used by Control
  bool              IsBuiltin;     // true: backed by a compiled C++ type (palette), not drafted
  char              TypeName[IM_LABEL_SIZE];      // C++ type to Push<> (builtin window/sidebar/layer/control)
  char              DataTypeName[IM_LABEL_SIZE];  // builtin control PersistData type name; empty => "<Name>Data"
  ImGuiAppLayerType LayerType;     // Layer nodes only
  bool              HasInitialPlacement;          // Window/Sidebar first-use placement
  ImVec2            InitialPos;
  ImVec2            InitialSize;
  ImGuiDir          DockDir;       // Sidebar dock direction
  float             DockSize;      // Sidebar size
  ImGuiWindowFlags  Flags;         // Window/Sidebar flags
  ImVec2            GridPos;        // persisted canvas position
  bool              HasGridPos;
  bool              _NeedsPlace;    // apply GridPos to imnodes before the next BeginNode
  int               BodyAttrId;     // dedicated non-port static-attribute id for the node body
  bool              IsLive;         // mirrored from a running app object (read-only)
  bool              IsPromoted;     // design control whose emitted data type matches a live node (transient)
  ImGuiID           LiveKey;        // stable upsert key for a live node (so its position survives re-mirroring)
  ImVector<ImGuiAppCommandDesc> Commands; // CommandLayer: definitions. Control: selected commands emitted by OnGetCommand.
  ImVector<ImGuiAppEventDesc>   Events;   // Control: authored temp-vs-last-temp events (see ImGuiAppEventDesc)
  ImVector<ImGuiAppStyleModDesc> StyleMods; // Window/Sidebar/Control: authored style-var overrides (emitted into SetupApp)
  ImVector<ImGuiAppColorModDesc> ColorMods; // Window/Sidebar/Control: authored style-color overrides (same lifecycle)
  ImVector<ImGuiAppNodePort> Ports;
  int               FieldList;       // Field node: which list it belongs to on its owner (0 = Persist, 1 = Temp)
  int               PersistStructId; // Control: Struct node its PersistData was exploded into (-1 = inline)
  int               TempStructId;    // Control: Struct node its TempData was exploded into (-1 = inline)
  bool              GroupCollapsed;  // owner whose group is folded: its descendants are hidden behind a proxy chip (transient view state, not serialized)
  bool              Hidden;          // outliner eye toggle / isolate: not submitted to the canvas (transient view state, not serialized)

  ImGuiAppNode()
  {
    Id = 0; Kind = ImGuiAppNodeKind_Control; IsBuiltin = false; TypeName[0] = 0; DataTypeName[0] = 0;
    LayerType = ImGuiAppLayerType_Task; HasInitialPlacement = false; InitialPos = ImVec2(0.0f, 0.0f);
    InitialSize = ImVec2(0.0f, 0.0f); DockDir = ImGuiDir_Down; DockSize = 0.0f; Flags = ImGuiWindowFlags_None;
    GridPos = ImVec2(0.0f, 0.0f); HasGridPos = false; _NeedsPlace = false; BodyAttrId = 0;
    IsLive = false; IsPromoted = false; LiveKey = 0; FieldList = 0; PersistStructId = -1; TempStructId = -1;
    GroupCollapsed = false;
    Hidden = false;
  }
};

// Optional per-data-edge field assignment: emits one "data->Dst = dep->Src;" line in OnUpdate. Kept off the
// link (an ImVector member would make ImGuiAppNodeLink a non-aggregate and break brace-init); keyed by LinkId.
struct ImGuiAppFieldBinding
{
  int  LinkId;
  char DstField[IM_LABEL_SIZE];
  char SrcField[IM_LABEL_SIZE];

  ImGuiAppFieldBinding() { LinkId = 0; DstField[0] = 0; SrcField[0] = 0; }
};

// The whole authored graph: nodes, typed links, field bindings, and one monotonic id allocator shared by
// every node/port/body-attr/link so ids are globally unique and never reused.
struct ImGuiAppGraph
{
  ImVector<ImGuiAppNode>         Nodes;
  ImVector<ImGuiAppNodeLink>     Links;
  ImVector<ImGuiAppFieldBinding> Bindings;
  ImVector<int>                  Selection;   // multi-selection (node ids); the single selected_node_id is primary
  ImVector<int>                  ViewScope;   // drill-down scope stack (node ids, outer->inner); empty = whole app.
                                              // Transient view state (not serialized): Tab enters the selected
                                              // node's composition, Esc goes up, breadcrumb segments jump.
  int NextId;
  int EditingNodeId;             // node whose title is being renamed inline, or -1
  char LastLinkErr[IM_LABEL_SIZE];  // last refused-link reason; transient UI state, NOT in Save/Load
  int  LastLinkErrSeq;              // bumped on every rejection -> demo edge-triggers the fade

  ImGuiAppGraph() { NextId = 1; EditingNodeId = -1; LastLinkErr[0] = 0; LastLinkErrSeq = 0; }
};

namespace ImGui
{
  // Allocation / factory. AddNode/AddBuiltin push an empty node then stamp the kind's mandatory ports and a
  // body-attr id, returning a pointer valid until the next node is added (Nodes may reallocate). Find* resolve
  // by search, never by index. RemoveNode also sweeps incident links and orphaned bindings.
  IMGUI_API int                 AppGraphAllocId(ImGuiAppGraph* g);
  IMGUI_API ImGuiAppNode*       AppGraphAddNode(ImGuiAppGraph* g, ImGuiAppNodeKind kind, const char* name);
  IMGUI_API ImGuiAppNode*       AppGraphAddBuiltin(ImGuiAppGraph* g, ImGuiAppNodeKind kind, const char* type_name, const char* data_type_name);
  IMGUI_API void                AppGraphRemoveNode(ImGuiAppGraph* g, int node_id);
  IMGUI_API ImGuiAppNode*       AppGraphFindNode(ImGuiAppGraph* g, int node_id);
  IMGUI_API ImGuiAppNodePort*   AppGraphFindPort(ImGuiAppGraph* g, int port_id, ImGuiAppNode** out_owner);
  IMGUI_API bool                AppGraphHasLayerType(const ImGuiAppGraph* g, ImGuiAppLayerType type);
  IMGUI_API void                AppNodeAddCommand(ImGuiAppNode* n, const char* name);
  IMGUI_API void                AppNodeRemoveCommand(ImGuiAppNode* n, int index);

  // The runtime data-flow key for a node named <node_name>: ConstantHash of the sanitized "<Name>Data" the
  // codegen emits -- equals ImGuiType<<Name>Data>::ID, so a design DataOut port shares the live storage key.
  IMGUI_API ImGuiID             AppNodeStructTypeId(const char* node_name);

  // Stable fold of the codegen-DETERMINING authored (!IsLive) graph state: changes iff the emitted C++ would
  // change, so a panel can show fresh|STALE. Excludes positions/ids/live-mirror churn. char[] hashed as
  // NUL-terminated string (ctors zero only byte 0, so ImHashData over the fixed buffer would be unstable).
  IMGUI_API ImGuiID             AppGraphSignature(const ImGuiAppGraph* g);

  // Typed links. CanLink validates an attempted edge (kind pairing, no self/dup, no duplicate dep type, no
  // cycle) and writes a reason to err on rejection. CaptureAppGraphLinks folds imnodes create/destroy events,
  // refusing illegal creations; returns true if the model changed.
  IMGUI_API bool                AppGraphCanLink(ImGuiAppGraph* g, int start_port, int end_port, char* err, int err_size);
  IMGUI_API bool                CaptureAppGraphLinks(ImGuiAppGraph* g, char* err, int err_size);

  // Per-edge field-binding editor (call inside an attribute). Lists/edits the bindings for one data link.
  IMGUI_API void                EditAppDataEdgeBindings(ImGuiAppGraph* g, int link_id);

  // Render the whole typed graph inside the current window (wraps BeginNodeEditor..EndNodeEditor). app may be
  // null (design-only); when non-null, builtin control bodies can reflect live data. *selected_node_id is the
  // window-level selection (caller-owned, -1 = none): the editor reconciles it both ways (canvas<->tree) and
  // clears dangling ids. show_live hides (never deletes) read-only live-mirror nodes when false.
  IMGUI_API void                ShowAppGraphEditor(ImGuiApp* app, ImGuiAppGraph* g, int* selected_node_id, bool show_live);

  // "Play": render a design Control node's effective Persist/Temp fields as a live mock panel of real ImGui
  // widgets (float->drag, bool->checkbox, vec->drag2/4, string->read-only box), so you can see the authored UI
  // without compiling. Numeric values are scratch (ImGuiStorage), not wired to anything. node_id < 0 -> hint.
  IMGUI_API void                AppGraphRenderMockPanel(ImGuiAppGraph* g, int node_id);

  // Roomy inspector for one node's authored data (name, Persist/Temp fields, window/sidebar props). Live nodes
  // are read-only. node_id < 0 -> a "select a node" hint. Edits mutate the graph in place.
  IMGUI_API void                EditAppNodeInspector(ImGuiAppGraph* g, int node_id);
  IMGUI_API void                EditAppNodeInspectorEx(ImGuiAppGraph* g, int node_id, ImGuiApp* live_app);   // + live style write-back (see workbench §3.5)

  // Inspector section header (workbench §5.1, the Unity/UE component anatomy): collapse triangle + icon +
  // label, optional enable checkbox, optional kebab whose click the caller answers with its own popup.
  // Open state is session-lived per window. Shared with hosts so panel and project inspectors match.
  IMGUI_API bool                AppInspectorSection(const char* str_id, const char* icon, const char* label, bool* enabled, bool* kebab_clicked);

  // Origin breadcrumb for a selected node: "sel: MainWindow > Mixer [design]" / "[live]" / "[promoted]" /
  // "sel: -" when id < 0 or unknown. char[] out, no references; encapsulates the containment-parent walk.
  IMGUI_API void                AppGraphSelectionBreadcrumb(const ImGuiAppGraph* g, int node_id, char* buf, int buf_size);

  // Mirror the running app's controls into *g WITHOUT reflection: remove all prior live nodes, then add one
  // read-only live Control node per pushed control (keyed by GetControlDataID) plus the data edges between
  // them, and flag design control nodes whose emitted data type matches a live node. Design (non-live) nodes
  // are untouched. Safe to call every frame.
  IMGUI_API void                BuildAppLiveGraph(const ImGuiApp* app, ImGuiAppGraph* g);

  // Scene-hierarchy / outliner panel (call inside your own child/window): a tree of the running app's
  // composition -- Layers, Windows, Sidebars, Controls -- plus the graph's authored nodes. Clicking a graph
  // row selects that node in the editor. *selected_node_id is caller-owned selection state (-1 = none).
  // show_live mirrors the canvas toggle: when false, live-mirror rows are not listed (never deleted).
  IMGUI_API void                ShowAppGraphTree(const ImGuiApp* app, ImGuiAppGraph* g, int* selected_node_id, bool show_live = true);

  // Topologically order the Control nodes by data dependency (producers before consumers). Returns false and
  // writes err on a cycle. out_control_ids receives node ids in push order.
  IMGUI_API bool                AppGraphTopoOrder(const ImGuiAppGraph* g, ImVector<int>* out_control_ids, char* err, int err_size);

  // One authoring problem found by AppGraphValidate. Severity: 1 = warning, 2 = error. NodeId is the node to
  // reveal when the row is clicked (-1 for whole-graph issues like a dependency cycle).
  struct ImGuiAppGraphIssue
  {
    int  NodeId;
    int  Severity;
    char Text[256];

    ImGuiAppGraphIssue() { NodeId = -1; Severity = 1; Text[0] = 0; }
  };

  // Static-analyze the authored (design) graph and append any problems to *out: empty structs, struct/string
  // fields missing their type/size, controls whose exploded data struct is missing, blank/duplicate names,
  // empty windows, and dependency cycles. Live-mirror nodes are skipped (read-only). Clears nothing.
  IMGUI_API void                AppGraphValidate(const ImGuiAppGraph* g, ImVector<ImGuiAppGraphIssue>* out);

  // Brushing across coordinated views: any panel reports the graph datum under the mouse; every panel
  // reads LAST frame's report and highlights that same datum (one-frame latency, imperceptible). The
  // source lets a view skip echoing its own hover -- the canvas already outlines its natively hovered
  // node. Single-editor-instance state, like the editor's other function statics.
  typedef int ImGuiAppHoverSource;
  enum ImGuiAppHoverSource_
  {
    ImGuiAppHoverSource_None = 0,
    ImGuiAppHoverSource_Canvas,      // the node editor
    ImGuiAppHoverSource_Tree,        // the outliner
    ImGuiAppHoverSource_Inspector,   // inspector / binding rows
    ImGuiAppHoverSource_External,    // problems list, code panel, any host-app panel
  };
  // Context keymap hint (what the mouse does right now, given its hover target) + transient refused-link
  // errors, computed by ShowAppGraphEditor every frame. A host status bar renders it OUTSIDE the canvas --
  // the game-editor pattern: hints live in the window's status bar, never over the viewport. severity:
  // 0 = plain hint, 2 = error (refused link). Valid for the frame after the editor ran.
  IMGUI_API const char*         AppGraphStatusHint(int* out_severity);

  IMGUI_API void                AppGraphHoverNode(int node_id, ImGuiAppHoverSource source);
  IMGUI_API void                AppGraphHoverLink(int link_id, ImGuiAppHoverSource source);
  IMGUI_API int                 AppGraphHoveredNode(ImGuiAppHoverSource* out_source);   // -1 = none; out_source may be null
  IMGUI_API int                 AppGraphHoveredLink(ImGuiAppHoverSource* out_source);

  // Host verbs surfaced in the canvas command palette (workbench W2: the palette is the completeness proof --
  // one searchable surface reaching editor AND document verbs). The host registers its commands each frame
  // before ShowAppGraphEditor (pointers must outlive the frame); a picked command is reported back through
  // AppGraphConsumeHostCommand -- the edit-intent idiom, one-frame latency, the editor never calls the host.
  struct ImGuiAppGraphHostCmd
  {
    const char* Label;      // e.g. "File: Save graph"
    const char* Shortcut;   // displayed dim + right-aligned; "" = none
    int         Id;         // host-defined, returned by AppGraphConsumeHostCommand
  };
  IMGUI_API void                AppGraphSetHostCommands(const ImGuiAppGraphHostCmd* cmds, int count);
  IMGUI_API int                 AppGraphConsumeHostCommand();   // picked host cmd id since last call, or -1

  // Host toolbars' "compose" entry point: opens the add-node palette at the canvas center on the editor's
  // next submission (one-shot; same palette as RMB / Space / the + gizmo).
  IMGUI_API void                AppGraphRequestAddPalette();

  // The composer chrome's own push-stack palette, stated in desc terms and exposed read-write (stable
  // pointer): the project inspector's Theme section edits it live -- the composer styles itself with the
  // machinery it teaches. Col slots are semantic and fixed; Value/Active are the editable half.
  struct ImGuiAppChromeTheme
  {
    ImGuiAppColorModDesc Combo[8];   // dropdown fields (enum combos, struct picker): field + popup + rows
    ImGuiAppColorModDesc Edit[4];    // in-place editors (InputText/InputInt): transparent frame over the drawn bg
  };
  IMGUI_API ImGuiAppChromeTheme*    AppGraphChromeTheme();

  // Canvas view settings: snap-to-grid + the overlays popover's toggles. Presentation-only, never model state.
  // Exposed (stable pointer, single editor instance) so the host can persist them across sessions.
  struct ImGuiAppGraphViewState
  {
    bool  SnapGrid;
    bool  OvGrid;
    bool  OvBands;
    bool  OvFrames;
    bool  OvMinimap;
    float Zoom;      // canvas zoom, wheel-driven, [0.3, 2.5]; authored positions stay zoom-independent
  };
  IMGUI_API ImGuiAppGraphViewState* AppGraphViewState();

  // Cached validation, keyed by AppGraphSignature (+ bindings): cheap enough to query every frame, so
  // problems can render ambiently where they live (canvas severity dot, outliner underline, inspector
  // header) instead of only in a list. Worst per-node severity: 0 = clean, 1 = warning, 2 = error.
  IMGUI_API const ImVector<ImGuiAppGraphIssue>* AppGraphIssuesCached(const ImGuiAppGraph* g);
  IMGUI_API int                 AppGraphNodeSeverity(const ImGuiAppGraph* g, int node_id);

  // Type-check one authored event's Expr against the control's effective field lists. The grammar is tiny on
  // purpose (events stay analyzable data, not strings): field refs `temp_data->x` / `last_temp_data->x` /
  // `data->x` / `<dep_param>-><field>`, nested struct members via '.', bool/int/float literals, parens, and
  // scalar operators at C precedence (! unary- * / % + - ^ comparisons && ||). `^` pairs bools (the change
  // idiom) or ints. The result type must fit DstField. Empty Expr is valid (codegen copies the watched temp
  // field). Returns true when well-typed; else writes a diagnostic to err. Used by AppGraphValidate and the
  // events editor; exposed so tests can drive it directly.
  IMGUI_API bool                AppEventExprCheck(const ImGuiAppGraph* g, const ImGuiAppNode* n, const ImGuiAppEventDesc* ev, char* err, int err_size);

  // One node's contribution to the last whole-graph emission: [LineBegin, LineEnd) in the generated text.
  // A node may own several spans (its type definitions AND its bring-up line in SetupApp). This is the
  // code<->canvas source map: a code panel can highlight, scroll to, and select nodes from their lines.
  struct ImGuiAppCodeSpan
  {
    int NodeId;
    int LineBegin;
    int LineEnd;

    ImGuiAppCodeSpan() { NodeId = -1; LineBegin = 0; LineEnd = 0; }
  };

  // Whole-graph codegen: data structs + controls with derived DataDependencies (topo order) + one bring-up
  // function pushing layers, then windows/sidebars, then controls. Appends to *out. The Ex variant also
  // records the per-node source map (out_spans may be null; it is cleared first).
  IMGUI_API void                GenerateAppGraphCode(const ImGuiAppGraph* g, ImGuiTextBuffer* out);
  IMGUI_API void                GenerateAppGraphCodeEx(const ImGuiAppGraph* g, ImGuiTextBuffer* out, ImVector<ImGuiAppCodeSpan>* out_spans);

  // Per-node codegen for the inspector: emits only the code a single selected node produces -- a Control's
  // struct(s) with derived deps, the CommandLayer's AppCommand enum + dispatch, a window/sidebar/layer
  // bring-up line, or (App node) the whole composition. Appends to *out.
  IMGUI_API void                GenerateAppNodeCode(const ImGuiAppGraph* g, const ImGuiAppNode* n, ImGuiTextBuffer* out);

  // Persist / restore the whole graph as imgui-style text. LoadAppGraph also ingests the legacy single/multi
  // "[Draft]" format (each becomes a Control node). The four legacy Save/Load*Graph[Multi] functions above are
  // unchanged. Return false on file error.
  IMGUI_API bool                SaveAppGraph(const char* path, const ImGuiAppGraph* g);
  IMGUI_API bool                LoadAppGraph(const char* path, ImGuiAppGraph* g);

  // Headless codegen: load a graph file and write its whole-graph generated C++ to out_header_path, no GUI.
  // Returns false on a load or write error. (Bake graphs in a build step.)
  IMGUI_API bool                AppGraphGenerateToFiles(const char* graph_path, const char* out_header_path);

  // Line diff of two graphs' generated C++ (unified-style: ' ' context, '-' only in a, '+' only in b). Appends
  // to *out. Use to show what changed between an edited graph and a baseline (e.g. the last-saved version).
  IMGUI_API void                AppGraphDiffCode(const ImGuiAppGraph* a, const ImGuiAppGraph* b, ImGuiTextBuffer* out);

  // Round-trip: parse C `struct <Name> { <type> <field>; ... };` blocks out of arbitrary C++ source and add one
  // Struct node per struct (with its fields, types mapped back from C++). The inverse of the struct codegen --
  // paste a POD header to reconstruct nodes. New nodes are laid out starting at `origin`. Returns structs added.
  IMGUI_API int                 AppGraphImportStructsFromCode(ImGuiAppGraph* g, const char* code, ImVec2 origin);

  // Undo / redo. The editor calls AppGraphCheckpoint once per frame to capture settled mutations into an
  // in-memory history (snapshots coalesce while the mouse is held or a widget is being edited, so a drag or
  // inline rename folds into one step). Ctrl+Z / Ctrl+Y are handled inside ShowAppGraphEditor; the Undo/Redo
  // entry points are also exposed for a toolbar. Can* report whether a step is available for this graph.
  IMGUI_API void                AppGraphCheckpoint(ImGuiAppGraph* g);
  IMGUI_API void                AppGraphUndo(ImGuiAppGraph* g);
  IMGUI_API void                AppGraphRedo(ImGuiAppGraph* g);
  IMGUI_API bool                AppGraphCanUndo(const ImGuiAppGraph* g);
  IMGUI_API bool                AppGraphCanRedo(const ImGuiAppGraph* g);

  // Undo-history introspection for a time-travel scrubber: Count = number of snapshots, Cursor = the one the
  // live graph currently matches (0..Count-1, -1 if empty), Goto = jump the live graph to a snapshot index.
  IMGUI_API int                 AppGraphHistoryCount(const ImGuiAppGraph* g);
  IMGUI_API int                 AppGraphHistoryCursor(const ImGuiAppGraph* g);
  IMGUI_API void                AppGraphHistoryGoto(ImGuiAppGraph* g, int index);
  IMGUI_API const char*         AppGraphHistoryLabel(const ImGuiAppGraph* g, int index);   // derived operation name ("Add Mixer", "Rename A -> B"); "" if unknown

  // Prefabs: reusable named subtrees. SavePrefab serializes the roots' containment subtrees under a name
  // (replacing one of the same name); Instantiate stamps a saved prefab into g with fresh ids at `origin` and
  // selects it. Count/Name enumerate the registry (in-memory, session-lived). Reuses the copy/paste machinery.
  IMGUI_API void                AppGraphSavePrefab(const ImGuiAppGraph* g, const ImVector<int>& roots, const char* name);
  IMGUI_API int                 AppGraphPrefabCount();
  IMGUI_API const char*         AppGraphPrefabName(int index);
  IMGUI_API int                 AppGraphInstantiatePrefab(ImGuiAppGraph* g, int index, ImVec2 origin);

  // Guarantee the four-layer framework foundation (Window, Task, Command, Status) exists in g -- adds any that
  // are missing, never duplicates. These layers are permanent (AppGraphRemoveNode refuses them) and are the base
  // every app builds windows/controls/commands on top of. Call on new/empty graphs and after load.
  IMGUI_API void                AppGraphEnsureFoundation(ImGuiAppGraph* g);

  // One-shot tidy layout: arrange design nodes as a left-to-right containment tree (window -> hosted controls
  // -> data structs -> fields), siblings stacked and parents centered. Bound to the L key inside the editor and
  // exposed for a toolbar button. Layers keep their own column packing; live-mirror nodes are left in place.
  IMGUI_API void                AppGraphAutoLayout(ImGuiAppGraph* g, bool show_live);
}
