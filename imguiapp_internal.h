#pragma once

// Tool-UI interfaces (the imgui_internal.h analog, Phase A4). Everything under IMGUIX_DISABLE_TOOLS is
// compiled out in a lean build -- so a lean consumer that includes this header sees NO UI API. The core
// model/codegen (imguiapp_nodes.h), interpreter (imguiapp_preview.h), recorder/decoder (imguiapp_av.h) and
// runtime (imguiapp.h) interfaces stay in their own UNCONDITIONAL core headers, included here for the types
// the tool-UI decls reference.

#include "imguiapp.h"
#include "imgui_internal.h"   // ImFormatString
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
  //=================== graph MODEL + CODEGEN + editor UI decls (folded from imguiapp_nodes.h) ===================

/*

Index of this file:
// [SECTION] Header mess
// [SECTION] Reflection field helpers (VisitAppFields, DrawAppField, EditAppField)
// [SECTION] Node rendering (BeginAppNode, EndAppNode, DrawAppNodeFields, EditAppNodeFields)
// [SECTION] Design-phase node drafts (ImGuiAppFieldType, ImGuiAppFieldDesc, ImGuiAppNodeDraft)
// [SECTION] Graph topology and persistence (ImGuiAppNodeLink, link capture, save/load)
// [SECTION] Code generation (GenerateAppControlCode)

Reflection-driven node tooling. Uses the applayer's reflection port (imguiapp_reflect.h,
via imguiapp.h) and some STL kept out of imguiapp.h. Public entry points stay
imgui-shaped: pointer parameters, char[] buffers, ImGuiID, ImVector.

Reflection subset: aggregates only (no user ctors); the port's patched member count
handles raw array members such as char Label[128] correctly (one member each).

*/

//-----------------------------------------------------------------------------
// [SECTION] Header mess
//-----------------------------------------------------------------------------



struct ImGuiCanvasState;                  // canvas engine state (imguiapp_canvas.h), opaque here

//-----------------------------------------------------------------------------
// [SECTION] Reflection field helpers (VisitAppFields, DrawAppField, EditAppField)
//-----------------------------------------------------------------------------

namespace ImGui
{
  // True when std::format can stringify U with "{}": the disabled primary std::formatter
  // is not default-constructible; enabled specializations are.
  template <typename U>
  inline constexpr bool ImIsFormattable = std::is_default_constructible_v<std::formatter<std::remove_cvref_t<U>, char>>;

  // True for a fixed-size char buffer member (e.g. char Label[128]) -> edited as text.
  template <typename U>
  inline constexpr bool ImIsCharArray = std::is_array_v<U> && std::is_same_v<std::remove_cv_t<std::remove_extent_t<U>>, char>;

  // Read-only render of one reflected field value.
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
      std::string_view tn = ImAppReflect::type_name(*value);
      ImGui::Text("%s: <%.*s>", label, (int)tn.size(), tn.data());
    }
  }

  // Editable widget for one reflected field value. Returns true if the value changed.
  // Falls back to a read-only render for types with no editor.
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
  // The value is passed by reference; pass a const T* to visit read-only.
  template <typename T, typename Visitor>
  inline void VisitAppFields(T* obj, Visitor visitor)
  {
    IM_ASSERT(obj != nullptr);

    ImAppReflect::for_each([&](auto I)
    {
      visitor((int)I, ImAppReflect::member_name<I>(*obj), ImAppReflect::get<I>(*obj));
    }, *obj);
  }
}

//-----------------------------------------------------------------------------
// [SECTION] Node rendering (BeginAppNode, EndAppNode, DrawAppNodeFields, EditAppNodeFields)
//-----------------------------------------------------------------------------

namespace ImGui
{
  // Canvas-engine node scaffold (see imguiapp_canvas.h): titled node between CanvasBegin/End.
  IMGUI_API void BeginAppNode(::ImGuiCanvasState* c, int id, const char* title);
  IMGUI_API void EndAppNode(::ImGuiCanvasState* c);

  // Renamable node scaffold: the title bar shows *name, turns into an inline text box when clicked.
  // Commits on Enter or focus loss, cancels on Escape. *editing_node_id is caller-owned (-1 = none);
  // set on title click, cleared when the edit ends. Pair with EndAppNode().
  IMGUI_API void BeginAppNodeRenamable(::ImGuiCanvasState* c, int id, char* name, int name_size, int* editing_node_id);

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

// A draft describes a node whose backing C++ type does not exist yet; codegen emits a
// reflection-capable aggregate from it. Fields use the plain-scalar vocabulary codegen can emit.
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

// Graph-model types are aggregates (default member initializers, no ctors): the build-time
// reflection walk (imguiapp.h) materializes their manifests for live mirrors and codegen.
struct ImGuiAppFieldDesc
{
  char              Name[IM_LABEL_SIZE]       = "";
  ImGuiAppFieldType Type                      = ImGuiAppFieldType_Float;
  int               ArraySize                 = 128; // buffer length for ImGuiAppFieldType_String; ignored otherwise
  char              StructType[IM_LABEL_SIZE] = "";  // referenced struct type name for ImGuiAppFieldType_Struct
};

// The control's four authorable virtual methods (F78.5). A hand-written body opts one method out of the
// modeled/stub codegen: the emitter emits the body verbatim, and the DLL preview compiles + runs it.
enum ImGuiAppControlMethod_
{
  ImGuiAppControlMethod_OnInitialize = 0,
  ImGuiAppControlMethod_OnGetCommand,
  ImGuiAppControlMethod_OnUpdate,
  ImGuiAppControlMethod_OnRender,
  ImGuiAppControlMethod_COUNT
};
#define IMGUIAPP_CONTROL_BODY_MAX 2048   // per-method custom C++ body cap (fixed buffer -> draft stays memcpy-safe)

// One drafted control: a name, its persisted and per-frame field sets, and optional hand-written method bodies.
struct ImGuiAppNodeDraft
{
  char                        Name[IM_LABEL_SIZE] = "NewControl";
  ImVector<ImGuiAppFieldDesc> PersistFields;
  ImVector<ImGuiAppFieldDesc> TempFields;
  // F78.5: optional custom C++ per method (indexed by ImGuiAppControlMethod_). Empty = modeled/stub codegen.
  // A non-empty body is emitted verbatim as that method's body; the body sees the method's generated params
  // (app / data / temp_data / last_temp_data / dt / cmd) and the emitted Data/TempData struct fields. Fixed
  // buffers (not ImVector) keep the draft trivially copyable inside node vectors. Interpreter reflects, not runs.
  char                        MethodBody[ImGuiAppControlMethod_COUNT][IMGUIAPP_CONTROL_BODY_MAX] = {};
};

namespace ImGui
{
  // C++ base type spelling for a field type.
  IMGUI_API const char* AppFieldTypeName(ImGuiAppFieldType type);

  // Draft field-list mutators.
  IMGUI_API void AppNodeDraftAddField(ImVector<ImGuiAppFieldDesc>* fields, const char* name, ImGuiAppFieldType type);
  IMGUI_API void AppNodeDraftRemoveField(ImVector<ImGuiAppFieldDesc>* fields, int index);

  // Inspector UI: rename the draft and add/remove/edit its persist and temp fields.
  IMGUI_API void EditAppNodeDraft(ImGuiAppNodeDraft* draft);

  // Persist/temp field editors without the Name input, for hosts that edit the name elsewhere.
  IMGUI_API void EditAppNodeDraftFields(ImGuiAppNodeDraft* draft);

  // Render a draft's fields as read-only node rows (no reflection: the type does not exist yet).
  IMGUI_API void DrawAppNodeDraft(const ImGuiAppNodeDraft* draft);
}

//-----------------------------------------------------------------------------
// [SECTION] Typed graph kinds (node/layer/port/edge discriminators)
//-----------------------------------------------------------------------------

// A node mirrors one slot of the live ImGuiApp object model (imguiapp.h). App is the singleton
// root; Layers push via PushAppLayer<T>, Windows/Sidebars via PushAppWindow/Sidebar<T>, Controls (the
// only draftable kind) via PushAppControl<T>.
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
  ImGuiAppNodeKind_Note,      // non-semantic annotation frame (F48/R1): titled rect, excluded from codegen/validation. APPEND ONLY -- serialized as int
  ImGuiAppNodeKind_Op,        // logic/compare/select/min-max operator (F54): operator in TypeName, IsBuiltin=false, folds into the consumer's expression (F55). APPEND ONLY -- serialized as int
  ImGuiAppNodeKind_Layout,    // dock-builder composition node (F57): Region/Split/Tabs variant in TypeName; Layout layer's first domain. APPEND ONLY -- serialized as int
  ImGuiAppNodeKind_COUNT,
};

// The five CORE layer classes are permanent, one each, immutable type -- codegen emits
// PushAppLayer<ImGuiAppXxxLayer>. Custom is a user-authored ImGuiAppLayer subclass: the NODE'S NAME is
// its class name, any number may exist, codegen emits the subclass skeleton plus PushAppLayer<Name>.
typedef int ImGuiAppLayerType;
enum ImGuiAppLayerType_
{
  ImGuiAppLayerType_Task = 0,
  ImGuiAppLayerType_Command,
  ImGuiAppLayerType_Status,
  ImGuiAppLayerType_Layout,
  ImGuiAppLayerType_Display,
  ImGuiAppLayerType_Custom,
  ImGuiAppLayerType_COUNT,
};

// DataOut/DataIn carry the runtime data flow; ChildOut/ChildIn carry containment. Layers are root
// composition slots with no containment sockets. A Control has one DataOut (its PersistData), one
// multi-link DataIn (the runtime keys app->Data by PersistData TYPE), and one ChildOut.
typedef int ImGuiAppPortKind;
enum ImGuiAppPortKind_
{
  ImGuiAppPortKind_DataOut = 0,
  ImGuiAppPortKind_DataIn,
  ImGuiAppPortKind_ChildOut,
  ImGuiAppPortKind_ChildIn,
  ImGuiAppPortKind_COUNT,
};

// Data dependency (producer -> consumer) or containment (child -> parent); derived from the linked
// ports' kinds at capture time.
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
// (ImGuiAppNodePort::Id), never array-derived, so they survive node reorder/delete. Kind's default
// member initializer keeps { id, start, end } brace-init and the legacy save/load format working.
struct ImGuiAppNodeLink
{
  int              Id;
  int              StartAttr;
  int              EndAttr;
  ImGuiAppEdgeKind Kind = ImGuiAppEdgeKind_Data;
  bool             Soft = false; // Optional data dependency (live mirror): drawn dimmed; transient, never saved
};

namespace ImGui
{
  // Draw the model's links as wires on the canvas (between CanvasBegin/End).
  IMGUI_API void DrawAppNodeLinks(::ImGuiCanvasState* c, const ImVector<ImGuiAppNodeLink>* links);

  // After CanvasEnd: fold the canvas's wire events into the link model. New links take ids from
  // *next_link_id (incremented). Returns true if the model changed.
  IMGUI_API bool CaptureAppNodeLinks(::ImGuiCanvasState* c, ImVector<ImGuiAppNodeLink>* links, int* next_link_id);

  // Persist / restore a draft and its links as imgui-style text. Return false on file error.
  IMGUI_API bool SaveAppNodeGraph(const char* path, const ImGuiAppNodeDraft* draft, const ImVector<ImGuiAppNodeLink>* links);
  IMGUI_API bool LoadAppNodeGraph(const char* path, ImGuiAppNodeDraft* draft, ImVector<ImGuiAppNodeLink>* links);

  // Multi-draft variants, same text format (one "[Draft]" section per draft): a single-draft file
  // loads as a one-element vector. *drafts is cleared before loading.
  IMGUI_API bool SaveAppNodeGraphMulti(const char* path, const ImVector<ImGuiAppNodeDraft>* drafts, const ImVector<ImGuiAppNodeLink>* links);
  IMGUI_API bool LoadAppNodeGraphMulti(const char* path, ImVector<ImGuiAppNodeDraft>* drafts, ImVector<ImGuiAppNodeLink>* links);
}

//-----------------------------------------------------------------------------
// [SECTION] Code generation (GenerateAppControlCode)
//-----------------------------------------------------------------------------

namespace ImGui
{
  // Emit an ImGuiAppControl from a draft: <Name>Data, <Name>TempData, and the control skeleton with
  // stubbed overrides. A String field emits char[N], which falls outside the reflection subset.
  // Appends to *out.
  IMGUI_API void GenerateAppControlCode(const ImGuiAppNodeDraft* draft, ImGuiTextBuffer* out);
}

//-----------------------------------------------------------------------------
// [SECTION] Typed node graph (ports, nodes, graph model, factory, codegen, persistence)
//-----------------------------------------------------------------------------

// One pin on a node, stored (never index-derived) so its id is stable across reorder/delete.
struct ImGuiAppNodePort
{
  int              Id                  = 0; // from ImGuiAppGraph::NextId; == canvas pin id
  ImGuiAppPortKind Kind                = ImGuiAppPortKind_DataOut;
  char             Name[IM_LABEL_SIZE] = "";
  ImGuiID          DataTypeId          = 0; // ImGuiType<PersistData>::ID for DataOut/DataIn (the data-flow key); 0 otherwise
};

struct ImGuiAppCommandDesc
{
  char Name[IM_LABEL_SIZE] = "NewCommand";
};

// Op node inline operand (F55): the token an operand pin folds to when it is NOT wired to a producer --
// an expression primary the AppEventExprCheck grammar accepts (a field ref "data->armed", a literal "0"
// "true", a dep ref "random_time->max_timer_secs"). Parallel to the operator's DataIn pins by index; a
// wired pin (a nested Op result) overrides its token. A missing entry is empty.
struct ImGuiAppOpOperand
{
  char Text[IM_LABEL_SIZE] = "";
};

// OnRender records raw input into TempData (zeroed every frame); OnUpdate receives BOTH this frame's
// TempData and last frame's, deriving events by comparing them. An edge names which comparison the
// generated OnUpdate guards with.
typedef int ImGuiAppEventEdge;
enum ImGuiAppEventEdge_
{
  ImGuiAppEventEdge_Rising = 0,   // temp && !last   -- became true this frame
  ImGuiAppEventEdge_Falling,      // !temp && last   -- became false this frame
  ImGuiAppEventEdge_Changed,      // temp ^ last     -- either transition
  ImGuiAppEventEdge_Active,       // temp            -- level: every frame while true
  ImGuiAppEventEdge_COUNT,
};

// SetField mutates PersistData (OnUpdate is the sole mutator); EmitCommand latches a persist bool in
// OnUpdate that OnGetCommand emits.
typedef int ImGuiAppEventAction;
enum ImGuiAppEventAction_
{
  ImGuiAppEventAction_SetField = 0,
  ImGuiAppEventAction_EmitCommand,
  ImGuiAppEventAction_COUNT,
};

// One authored event on a Control: "when <TempField> <edge> -> <action>". Codegen emits a guarded
// block in OnUpdate (plus, for EmitCommand, the latch + OnGetCommand emission).
struct ImGuiAppEventDesc
{
  char                TempField[IM_LABEL_SIZE] = ""; // watched TempData field
  ImGuiAppEventEdge   Edge                     = ImGuiAppEventEdge_Changed;
  ImGuiAppEventAction Action                   = ImGuiAppEventAction_SetField;
  char                DstField[IM_LABEL_SIZE]  = ""; // SetField: PersistData destination
  char                Expr[IM_LABEL_SIZE]      = ""; // SetField: source expression (emitted verbatim); empty -> temp_data-><TempField>
  char                Command[IM_LABEL_SIZE]   = ""; // EmitCommand: one of the control's selected commands
};

// One node in the authored graph. Embeds ImGuiAppNodeDraft so the rename/field-edit/codegen helpers
// apply verbatim and a legacy "[Draft]" maps 1:1 to a Control node. Most fields are kind-specific.
struct ImGuiAppNode
{
  int                            Id                          = 0;                      // from NextId; == canvas node id
  ImGuiAppNodeKind               Kind                        = ImGuiAppNodeKind_Control;
  ImGuiAppNodeDraft              Draft;                                                // Draft.Name is the node label; PersistFields/TempFields used by Control
  bool                           IsBuiltin                   = false;                  // true: backed by a compiled C++ type (palette), not drafted
  char                           TypeName[IM_LABEL_SIZE]     = "";                     // C++ type to Push<> (builtin window/sidebar/layer/control)
  char                           DataTypeName[IM_LABEL_SIZE] = "";                     // builtin control PersistData type name; empty => "<Name>Data"
  ImGuiAppLayerType              LayerType                   = ImGuiAppLayerType_Task; // Layer nodes only
  bool                           HasInitialPlacement         = false;                  // Window/Sidebar first-use placement
  ImVec2                         InitialPos                  = ImVec2(0.0f, 0.0f);
  ImVec2                         InitialSize                 = ImVec2(0.0f, 0.0f);
  ImGuiDir                       DockDir                     = ImGuiDir_Down;          // Sidebar dock direction; Layout node: split side (F57)
  float                          DockSize                    = 0.0f;                   // Sidebar size; Layout node: split fraction/size (F57)
  char                           RegionRef[IM_LABEL_SIZE]    = "";                     // Window/Sidebar: name of the Layout region node it docks into (F57); empty = none
  ImGuiWindowFlags               Flags                       = ImGuiWindowFlags_None;  // Window/Sidebar flags
  ImVec2                         GridPos                     = ImVec2(0.0f, 0.0f);     // persisted canvas position
  bool                           HasGridPos                  = false;
  bool                           _NeedsPlace                 = false;                  // apply GridPos to the canvas before the next submission
  int                            BodyAttrId                  = 0;                      // dedicated non-port static-attribute id for the node body
  bool                           IsLive                      = false;                  // mirrored from a running app object (read-only)
  bool                           IsPromoted                  = false;                  // design control whose emitted data type matches a live node (transient)
  ImGuiID                        LiveKey                     = 0;                      // stable upsert key for a live node (so its position survives re-mirroring)
  ImVector<ImGuiAppCommandDesc>  Commands;                                             // CommandLayer: definitions. Control: selected commands emitted by OnGetCommand.
  ImVector<ImGuiAppEventDesc>    Events;                                               // Control: authored temp-vs-last-temp events (see ImGuiAppEventDesc)
  ImVector<ImGuiAppStyleModDesc> StyleMods;                                            // Window/Sidebar/Control: authored style-var overrides (emitted into SetupApp)
  ImVector<ImGuiAppColorModDesc> ColorMods;                                            // Window/Sidebar/Control: authored style-color overrides (same lifecycle)
  ImVector<ImGuiAppNodePort>     Ports;
  ImVector<ImGuiAppOpOperand>    OpOperands;                                           // Op node: inline operand token per operand pin (by index); a wired pin overrides it (F55)
  int                            FieldList                   = 0;                      // Field node: which list it belongs to on its owner (0 = Persist, 1 = Temp)
  int                            PersistStructId             = -1;                     // Control: Struct node its PersistData was exploded into (-1 = inline)
  int                            TempStructId                = -1;                     // Control: Struct node its TempData was exploded into (-1 = inline)
  bool                           GroupCollapsed              = false;                  // descendants hidden behind the group title bar (transient, not serialized)
  bool                           Hidden                      = false;                  // not submitted to the canvas (transient, not serialized)
  ImVec2                         NoteSize                    = ImVec2(320.0f, 180.0f); // Note kind: annotation-frame footprint (model units)
  ImU32                          NoteColor                   = 0;                      // Note kind: frame tint (0 = default kind hue)
};

// Per-data-edge field assignment: emits one "data->Dst = dep->Src;" line in OnUpdate. Keyed by LinkId,
// kept off the link (an ImVector member would break ImGuiAppNodeLink's aggregate brace-init).
struct ImGuiAppFieldBinding
{
  int  LinkId                  = 0;
  char DstField[IM_LABEL_SIZE] = "";
  char SrcField[IM_LABEL_SIZE] = "";
};

// One user keymap override (F74, post-100 horizon): a chord (Key+Mods) rebound to an editor command Id.
// The keymap is a SPARSE diff -- only verbs the user changed appear here; the factory chord stays the
// registry Key/Mods. Key == ImGuiKey_None encodes an explicit unbind. Keyed by the stable command Id,
// never an array index, so reordering the registry never corrupts a saved keymap.
struct ImGuiAppKeyBinding
{
  int      CmdId = -1;
  ImGuiKey Key   = ImGuiKey_None;
  int      Mods  = 0;
};

// Per-branch camera memory: the pan/zoom a drill-down scope was LEFT with, keyed by the scope
// node's id (-1 = root) -- never by depth, so sibling branches keep independent cameras.
// Transient editor state carried by the graph (like ViewScope), not serialized.
struct ImGuiAppScopeCamera
{
  int    ScopeId = -1;
  ImVec2 Pan     = ImVec2(0.0f, 0.0f);
  float  Zoom    = 1.0f;
};

// Scope-local node placement: a member's position INSIDE a drilled scope, keyed by (scope node,
// member node). Each drill-down interior owns its own arrangement -- moving a node inside a group
// never moves it at the composition root (GridPos), and vice versa. First entry into a scope
// falls back to GridPos, then the interior read-back writes here. Serialized (layout is model
// state, like Pos=).
struct ImGuiAppScopePlacement
{
  int    ScopeId = -1;
  int    NodeId  = -1;
  ImVec2 Pos     = ImVec2(0.0f, 0.0f);
};

// Scope-local member ORDER (F58): the authored left-to-right / push order of a scope's members,
// keyed by scope node. One record holds the whole sequence -- an order IS a sequence, so it is stored
// as one; the flat per-(scope,node) index alternative scatters a single order across N rows and needs
// a sort to read it back. ScopeId == -1 is the composition root (the phase-layer order). Serialized
// (model state, like Place=); AppScopeSequenceIds returns members in THIS order when a record exists,
// else the derived sequence. The core phase layers may never be reordered here (AppGraphValidate
// rejects a record that permutes them).
struct ImGuiAppScopeOrder
{
  int           ScopeId = -1;
  ImVector<int> NodeIds;
};

// One cached trunk-route drawing primitive, in MODEL units (line-to / arc / cubic). The route is
// computed once from model inputs and only re-derived when those inputs move -- the camera never
// re-routes a settled link (transient, not serialized).
struct ImGuiAppTrunkSeg
{
  int    Kind = 0;                        // 0 = line-to P0; 1 = arc (P0 center, R, A0..A1); 2 = cubic (P0,P1 cps, P2 end)
  ImVec2 P0   = ImVec2(0.0f, 0.0f);
  ImVec2 P1   = ImVec2(0.0f, 0.0f);
  ImVec2 P2   = ImVec2(0.0f, 0.0f);
  float  R    = 0.0f;
  float  A0   = 0.0f;
  float  A1   = 0.0f;
};

struct ImGuiAppTrunkRoute
{
  int                       OwnerId = 0;
  ImVec2                    StartM  = ImVec2(0.0f, 0.0f);   // model endpoints + obstacle key: recompute triggers
  ImVec2                    EndM    = ImVec2(0.0f, 0.0f);
  ImVec4                    KeyA    = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  ImVec4                    KeyB    = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  ImVector<ImGuiAppTrunkSeg> Segs;
};

// One cluster's original position captured at layer-drag start, so groups pushed by a dragged
// edge stay STUCK to it and return with it within the same drag (transient, not serialized).
struct ImGuiAppDragStick
{
  int   NodeId = 0;
  float OrigY  = 0.0f;
};

// One group frame as PUBLISHED by the frames pass, in model units -- the single producer of
// group geometry. Consumers (drag clamping) read last frame's publication: a coherent T-1 pair
// (docs/phase-coherence.md rule 1). Transient, not serialized.
struct ImGuiAppGroupFrame
{
  int    OwnerId = 0;
  ImVec2 MinM = ImVec2(0.0f, 0.0f);
  ImVec2 MaxM = ImVec2(0.0f, 0.0f);
};

namespace ImGui
{
  struct ImGuiAppGraphHostCmd;   // fwd (declared with the palette API below)
  struct ImGuiAppGraphIssue;     // fwd (declared with the validation API below)
}

// Canvas view settings: presentation-only, never model state. Not serialized by Save/Load; the
// host may persist them across sessions.
struct ImGuiAppGraphViewState
{
  bool  SnapGrid;
  bool  OvGrid;
  bool  OvBands;
  bool  OvFrames;
  bool  OvMinimap;
  bool  TreeOpen;  // outliner sidebar shown; the host keeps its width across a collapse
  bool  InspOpen;  // inspector sidebar shown; the host keeps its width across a collapse
  float Zoom; // canvas zoom, wheel-driven, [0.3, 2.5]; authored positions stay zoom-independent
};

// Undo history: serialized-graph snapshots, linear with a cursor at the live state; pushing after
// an undo truncates the redo tail. Snapshot/label strings are owned heap allocations.
// A saved subtree: serialized nodes + links, instantiable into any graph.
struct ImGuiAppPrefab
{
  char  Name[64];
  char* Data;   // serialized subtree, owned
};

struct ImGuiAppEditorUndo
{
  ImVector<char*>              Snaps;            // owned snapshot strings, oldest..newest
  ImVector<char*>              Labels;           // owned operation names, parallel to Snaps (derived by diffing)
  int                          Cursor = -1;      // index of the snapshot matching the live graph (-1 = empty)
  const struct ImGuiAppGraph*  Owner = nullptr;  // identity guard
  ImGuiID                      LiveHash = 0;     // hash of Snaps[Cursor], for cheap change detection
};

struct ImGuiAppPreview;       // F68 live preview session (imguiapp_preview.h); opaque, held by pointer here
struct ImGuiAppMetaRecorder;  // F70 meta-only run recorder (imguiapp_av.h); opaque, held by pointer here
struct ImGuiAppPreviewDll;    // F78 DLL preview session (imguiapp_preview_dll.h); opaque, held by pointer here

// Editor session state, one per graph (the document): the editor's cross-frame values ride the
// model object they describe, like Selection/ViewScope/ScopeCams. All transient, not serialized.
struct ImGuiAppEditorState
{
  mutable ImGuiCanvasState*            Canvas = nullptr;        // this graph's canvas engine (created on first editor frame; freed with the process)
  bool                                 DragWasDetach = false;
  mutable int                          HoverNode = -1;          // brushing bus: render-phase reports (TempData), read next frame
  mutable int                          HoverLink = -1;
  mutable int                          HoverNodeSrc = 0;
  mutable int                          HoverLinkSrc = 0;
  mutable int                          HoverPrevNode = -1;      // ... and last frame's, what readers see
  mutable int                          HoverPrevLink = -1;
  mutable int                          HoverPrevNodeSrc = 0;
  mutable int                          HoverPrevLinkSrc = 0;
  mutable int                          HoverFrame = -1;
  ImGuiAppGraphViewState               View = { false, true, true, true, true, true, true, 1.0f };   // snap off; overlays + sidebars on; 1:1
  mutable ImVector<ImGui::ImGuiAppGraphIssue> IssuesCache;
  mutable ImGuiID                      IssuesSig = 0;
  mutable bool                         IssuesValid = false;
  mutable ImGuiStorage                 IssuesSeverity;          // node id -> worst severity
  ImVector<int>                        PoolIds;                 // node ids the canvas holds
  ImVector<int>                        PrevPoolIds;
  char                                 StatusHint[256] = "";
  int                                  StatusSev = 0;
  float                                OverlayAlpha = 0.55f;        // F38: animated overlay opacity (rest->hover->gesture, faded per motion table)
  ImVec2                               GizmoRectMin = ImVec2(0.0f, 0.0f);   // F38: last-drawn gizmo cluster rect (screen), drives the hover ladder + test
  ImVec2                               GizmoRectMax = ImVec2(0.0f, 0.0f);
  ImVec2                               EditorRectMin = ImVec2(0.0f, 0.0f);  // F38: last editor canvas rect (screen), for gesture detection + test
  ImVec2                               EditorRectMax = ImVec2(0.0f, 0.0f);
  ImVec2                               GizmoCenters[8] = {};                // F40: viewport gizmo centres (screen), in draw order, for the click-path test
  int                                  GizmoCount = 0;
  mutable const ImGui::ImGuiAppGraphHostCmd* HostCmds = nullptr;  // registered per frame; host-owned
  mutable int                          HostCmdCount = 0;
  mutable int                          HostCmdPicked = -1;
  mutable bool                         AddPaletteRequest = false;   // one-shot
  mutable bool                         CmdPaletteRequest = false;   // one-shot: open the Space operator palette (F34)
  int                                  KeymapCapture = -1;          // F75: command id whose chord the keymap editor is capturing, or -1
  mutable bool                         AlignMenuRequest = false;    // one-shot: open the Shift+A align/distribute submenu (F48/R3)
  mutable bool                         FitAllRequest = false;       // one-shot
  int                                  AutoLayoutCountdown = 2;     // launch default is a TIDIED layout: fires once real sizes exist (frame 2)
  float                                UniformCardW = 0.0f;         // one normalized width for every non-layer node (model units; grows to fit the widest need, deadbanded)
  bool                                 HelpOverlay = false;         // F1 shortcut cheat sheet
  bool                                 QuickInspector = false;      // N: floating quick inspector
  bool                                 QuickInspectorPin = false;   // F41: pinned -> stays on QuickInspectorNode instead of following the selection
  int                                  QuickInspectorNode = -1;     // F41: the pinned node (valid only while QuickInspectorPin)
  bool                                 PrevShowLive = true;
  bool                                 TitleEditing = false;        // one node renames at a time
  ImVec2                               AddPopupGrid = ImVec2(0.0f, 0.0f);
  int                                  CtxNodeId = -1;
  int                                  CtxLinkId = -1;
  int                                  FitAllCountdown = 0;
  ImGuiTextFilter                      AddFilter;                   // add-palette search
  ImGuiTextFilter                      CmdFilter;                   // command-palette search
  int                                  ErrSeqSeen = 0;
  double                               ErrTime = -1000.0;
  int                                  DropSrcAttr = -1;
  int                                  AppliedSel = -1;
  int                                  OutlinerRename = -1;         // node id being renamed in the tree, -1 = none
  bool                                 OutlinerRenameFocus = false;
  bool                                 OutlinerKindVis[ImGuiAppNodeKind_COUNT] = { true, true, true, true, true, true, true, true, true };
  ImGuiTextFilter                      OutlinerFilter;
  bool                                 OutputShowErr = true;        // Output panel severity filters
  bool                                 OutputShowWarn = true;
  bool                                 OutputShowInfo = true;
  ImGuiTextFilter                      OutputFilter;                // Output panel text filter
  ImVector<ImGuiAppStyleModDesc>       StyleClipMods;               // style-section clipboard (session-lived, value-typed)
  ImVector<ImGuiAppColorModDesc>       StyleClipCols;
  bool                                 StyleClipHas = false;
  char*                                ClipText = nullptr;          // serialized partial graph, or null (owned)
  int                                  ClipPaste = 0;               // cascade counter so repeated pastes fan out
  ImVector<ImGuiAppPrefab>             Prefabs;                     // saved subtrees (owned strings)
  ImFont*                              CodeFont = nullptr;          // code panels; null -> UI font
  ImGuiAppEditorUndo                   Undo;
  ImVec4                               ScopeWallRect = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);   // scope-interior walls incl. face/end bands, model units (min.xy, max.xy); published by the walls pass, consumed same-frame by the strip + portal passes
  ImVec4                               ScopeStripRow = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);   // the face band's order-strip row, model units
  bool                                 ScopeWallValid = false;                            // rects above valid this frame (window/sidebar scope only)
  int                                  ScopeWallScope = -1;                               // scope the wall rect belongs to (hysteresis resets on change)
  ImVector<ImVec4>                     ScopeStripRects;                                   // order-strip chip rects, screen space (published per frame for hit-tests/tests)
  ImVector<int>                        ScopeStripNodes;                                   // node id per chip, parallel to ScopeStripRects
  ImVec4                               ScopeVoid[4] = {};                                 // figure-ground carve: the four dim bands outside the walls, screen space (top/bottom/left/right); published per frame for tests
  bool                                 ScopeVoidValid = false;                            // ScopeVoid valid this frame (drawn with the walls)
  int                                  TelegraphPin = -1;                                 // F50: pin hovered as a wire-drag target this frame (-1 = none)
  bool                                 TelegraphOk = false;                               // F50: would the hovered target connect legally to the drag source
  int                                  StripDragNode = -1;                                // F60: order-strip chip being drag-reordered (node id; -1 = idle). Latched pre-submission, hit-tested against last frame's ScopeStripRects
  mutable ImGuiAppPreview*             Preview = nullptr;                                 // F68: live preview interpreter session for this document (heap; opaque; freed with the process / rebuilt on Reinit)
  bool                                 PreviewRun = true;                                 // F68: run vs pause the previewed model (widgets stay interactive while paused)
  mutable ImGuiID                      PreviewSig = 0;                                    // F68: AppGraphSignature the preview was last reconciled at; a change reconciles (preserve-by-field) next frame
  ImGuiAppMetaRecorder*                PreviewRec = nullptr;                              // F70: active preview-session take (meta-only run container), or null
  ImGuiAppInputLog                     PreviewRecInput;                                   // F70: opt-in replay layer recorded alongside this take (AppInputRecord per driven frame)
  ImU64                                PreviewRecTick = 0;                                // F70: ticks recorded this take (== the exported run's tick spine)
  int                                  PreviewRecSnapEvery = 30;                          // F70: StateSnapshot cadence in ticks (a nearest-snapshot restore point every N)
  char                                 PreviewRecPath[256] = "headless-artifacts/preview-session.meta";  // F70: exported container path
  bool                                 PreviewUseDll = false;                             // F78.5: preview backend selector -- DLL (compiled real program) vs the interpreter (default)
  mutable ImGuiAppPreviewDll*          PreviewDll = nullptr;                              // F78.5: compiled-DLL preview session (heap; opaque; created lazily, freed on Reinit / process exit)
  mutable ImGuiID                      PreviewDllSig = 0;                                 // F78.5: AppGraphSignature the DLL preview was last compiled at; a change recompiles + hot-swaps
  mutable ImTextureData*               PreviewDllTex = nullptr;                           // F78.5: host texture holding the CPU-rasterized DLL frame (created per panel size)
  int                                  PreviewDllTexW = 0;                                // F78.5: PreviewDllTex width
  int                                  PreviewDllTexH = 0;                                // F78.5: PreviewDllTex height
  ImVector<unsigned char>              PreviewDllRgba;                                    // F78.5: reused RGBA32 scratch the DLL frame rasterizes into
  char                                 PreviewDllErr[192] = "";                           // F78.5: last DLL compile/load diagnostic (shown in the panel; empty = ok)
};

// The whole authored graph. One monotonic id allocator shared by every node/port/body-attr/link:
// ids are globally unique, never reused.
struct ImGuiAppGraph
{
  ImVector<ImGuiAppNode>         Nodes;
  ImVector<ImGuiAppNodeLink>     Links;
  ImVector<ImGuiAppFieldBinding> Bindings;
  ImVector<ImGuiAppKeyBinding>   Keymap;                               // F74: user input->command overrides (serialized; sparse diff from the registry-default chords)
  ImVector<int>                  Selection;                            // multi-selection (node ids); the single selected_node_id is primary
  ImVector<int>                  ViewScope;                            // drill-down scope stack (node ids, outer->inner); empty = whole app; transient, not serialized
  ImVector<ImGuiAppScopeCamera>  ScopeCams;                            // per-branch camera memory (transient, not serialized)
  ImVector<ImGuiAppScopePlacement> ScopePlacements;                    // scope-local member positions (serialized; root layout stays in GridPos)
  ImVector<ImGuiAppScopeOrder>   ScopeOrders;                          // per-scope authored member order (F58; serialized; empty = derive the sequence)
  ImVector<ImGuiAppTrunkRoute>   _TrunkRoutes;                         // cached trunk routes, model units (transient, not serialized)
  ImVector<ImGuiAppDragStick>    _DragStick;                           // cluster originals for the active layer drag (transient)
  int                            _DragStickAnchor           = 0;       // layer node the sticks belong to (0 = no active capture)
  ImVector<ImVec4>               _GroupDragOrig;                       // (id, x, y) member originals for the active group drag (transient)
  ImVec2                         _GroupDragMouse0           = ImVec2(0.0f, 0.0f); // model-space mouse origin of that drag (transient)
  ImVec4                         _GroupDragFrame0           = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // the dragged group's frame at drag start, model (transient)
  ImVec2                         _GroupDragApplied          = ImVec2(0.0f, 0.0f); // displacement actually granted so far (greedy catch-up; transient)
  bool                           _GroupDragMoved            = false;   // gesture latch: drag vs fold-click (transient)
  int                            _GroupDragPending          = -1;      // owner whose settled group-drag write defers to the post-CanvasEnd update pass; -1 = none (transient)
  bool                           _LayerBoxValid             = false;   // this frame's layer-column box, the deferred group-drag clamp's solid obstacle, MODEL units (transient)
  ImVec2                         _LayerBoxMin               = ImVec2(0.0f, 0.0f);
  ImVec2                         _LayerBoxMax               = ImVec2(0.0f, 0.0f);
  ImVector<ImGuiAppGroupFrame>   _GroupFrames;                         // THIS frame's published group frames, model units (transient)
  ImVector<ImGuiAppGroupFrame>   _GroupFramesPrev;                     // last frame's publication, what consumers read (transient)
  mutable ImGuiAppEditorState*   _Ed                        = nullptr; // editor session state; opaque to reflection/serialization (created on first editor use, freed with the process)
  float                          _LayerUniformW             = 0.0f;    // uniform layer-column content width, model units; 0 = floor (transient)
  int                            _LayerDragId               = 0;       // layer node under an active vertical drag, 0 = none (transient)
  float                          _LayerDragMouseY0          = 0.0f;    // screen-y mouse origin of that drag (transient)
  float                          _LayerDragNodeY0           = 0.0f;    // dragged layer's model y at grab (transient)
  float                          _LayerDragMaxY             = 0.0f;    // clamp edges captured at grab, model units (transient)
  float                          _LayerDragMinY             = 0.0f;
  int                            NextId                     = 1;
  int                            EditingNodeId              = -1;      // node whose title is being renamed inline, or -1
  char                           LastLinkErr[IM_LABEL_SIZE] = "";      // last editor notice (refused link/composition), a full sentence; transient, NOT in Save/Load
  int                            LastLinkErrSeq             = 0;       // bumped on every notice
  ImGuiApp*                      LiveApp                    = nullptr; // running app this graph mirrors (set by BuildAppLiveGraph, read-only to
                                                                       // codegen); null = no live source; transient, NOT in Save/Load
  int                            _ScopeSig                  = -1;      // editor scope-change detector (transient; -1 = first frame)
  int                            _ScopeCamId                = -1;      // scope id the camera currently shows (-1 = root; transient)
  int                            _PendingFit                = 0;       // deferred fit-all countdown after a scope change (transient)
  int                            Revision                   = 0;       // monotonic authored-content version; AppGraphSyncRevision bumps it when the signature changes (transient, session-local)
  ImGuiID                        _SigCache                  = 0;       // last signature seen by AppGraphSyncRevision (revision bookkeeping; transient)
  ImGuiID                        GenSignature               = 0;       // AppGraphSignature captured at the last codegen; 0 = never generated this session. Code is STALE when the live signature differs (transient, NOT serialized)
};

//-----------------------------------------------------------------------------
// [SECTION] Embeddable Composer control (generated-shell bootstrap)
//-----------------------------------------------------------------------------
// The Composer packaged as a library ImGuiAppControl: an app PushAppControl<>s it to host the node
// editor in its own composition. It owns the graph it edits and drives ShowAppGraphEditor, so the
// editor implementation stays library code -- a generated host shell hosts the Composer by pushing
// this type, never by re-emitting the editor. Host is the app running the control (handed to the
// editor for its live mirror); null = design-only.
struct ImGuiAppComposerControlData
{
  ImGuiApp*     Host     = nullptr;
  ImGuiAppGraph Graph;
  int           Selected = -1;
};

struct ImGuiAppComposerControlTempData
{
};

struct ImGuiAppComposerControl : ImGuiAppControl<ImGuiAppComposerControlData, ImGuiAppComposerControlTempData>
{
  virtual void OnInitialize(ImGuiApp* app, ImGuiAppComposerControlData* data) const override;
  virtual void OnRender(const ImGuiAppComposerControlData* data, ImGuiAppComposerControlTempData* temp_data) const override;
};

namespace ImGui
{
  // AddNode/AddBuiltin stamp the kind's mandatory ports and a body-attr id; the returned pointer is
  // valid only until the next node is added (Nodes may reallocate). Find* resolve by search, never by
  // index. RemoveNode also sweeps incident links and orphaned bindings.
  IMGUI_API int                 AppGraphAllocId(ImGuiAppGraph* g);
  IMGUI_API ImGuiAppNode*       AppGraphAddNode(ImGuiAppGraph* g, ImGuiAppNodeKind kind, const char* name);
  IMGUI_API ImGuiAppNode*       AppGraphAddBuiltin(ImGuiAppGraph* g, ImGuiAppNodeKind kind, const char* type_name, const char* data_type_name);
  // Add a logic Op node (F54). op_token is one of the AppEventExprCheck-checkable operators (AND/OR/XOR/NOT,
  // ==/!=/</<=/>/>=, select, min/max); null or unknown => the default (AND). TypeName carries the operator and
  // fixes the operand-pin arity; the result DataOut is stamped DataTypeId=0 (opts out of one-producer-per-type).
  IMGUI_API ImGuiAppNode*       AppGraphAddOp(ImGuiAppGraph* g, const char* op_token);
  IMGUI_API void                AppGraphRemoveNode(ImGuiAppGraph* g, int node_id);
  IMGUI_API ImGuiAppNode*       AppGraphFindNode(ImGuiAppGraph* g, int node_id);
  IMGUI_API ImGuiAppNodePort*   AppGraphFindPort(ImGuiAppGraph* g, int port_id, ImGuiAppNode** out_owner);
  IMGUI_API bool                AppGraphHasLayerType(const ImGuiAppGraph* g, ImGuiAppLayerType type);
  IMGUI_API void                AppNodeAddCommand(ImGuiAppNode* n, const char* name);
  IMGUI_API void                AppNodeRemoveCommand(ImGuiAppNode* n, int index);

  // Runtime data-flow key for a node named <node_name>: ConstantHash of the sanitized "<Name>Data"
  // codegen emits == ImGuiType<<Name>Data>::ID, so a design DataOut port shares the live storage key.
  IMGUI_API ImGuiID             AppNodeStructTypeId(const char* node_name);

  // Stable fold of the codegen-determining authored (!IsLive) state: changes iff the emitted C++ would
  // change. Excludes positions/ids/live-mirror churn. char[] hashed as NUL-terminated string (ctors
  // zero only byte 0, so ImHashData over the fixed buffer would be unstable).
  IMGUI_API ImGuiID             AppGraphSignature(const ImGuiAppGraph* g);

  // What kinds compose into a drilled scope (F28): the single truth behind the interior palette and the
  // empty-scope wall caption. A live non-layer scope (window/sidebar/control/struct mirror) takes nothing
  // -- read-only -- so this returns false for every kind; the authored twin admits its members.
  IMGUI_API bool                AppScopeKindComposable(const ImGuiAppGraph* g, int scope_id, ImGuiAppNodeKind kind);

  // Origin vocabulary (F26): the one colour shared by the canvas title-bar dot, the outliner row tint and
  // the demo legend. Live and Promoted (a design control whose emitted data type matches a live node) each
  // get a distinct mark; plain design returns 0 (no push -> default row colour). Single source, so the
  // three surfaces cannot drift.
  IMGUI_API ImU32               AppGraphOriginColor(const ImGuiAppNode* n);

  // Codegen freshness (F17). The signature is the single source of truth: AppGraphSyncRevision folds
  // it once per frame and bumps Revision on any content change; AppGraphMarkGenerated stamps the
  // signature at codegen time; the graph is STALE while the live signature differs from that stamp
  // (FRESH == generated this session AND unchanged since). GenSignature/Revision are session-local
  // (reset on load), so a freshly loaded graph reads as never-generated.
  IMGUI_API int                 AppGraphSyncRevision(ImGuiAppGraph* g);
  IMGUI_API void                AppGraphMarkGenerated(ImGuiAppGraph* g);
  IMGUI_API bool                AppGraphCodeStale(const ImGuiAppGraph* g);
  IMGUI_API bool                AppGraphCodeFresh(const ImGuiAppGraph* g);

  // Count the codegen self-diagnostics embedded in generated text (F19): the "// WARNING" comments the
  // emitter drops for degenerate constructs and the "// codegen aborted" banner. Scans the emitted C++
  // itself (single source: the emitter), never re-deriving the conditions. out_list (optional) receives
  // each marker line trimmed of leading indent, one per line.
  IMGUI_API int                 AppScanCodegenWarnings(const char* code, ImGuiTextBuffer* out_list);

  // CanLink validates an attempted edge (kind pairing, no self/dup, no duplicate dep type, no cycle),
  // writing a reason to err on rejection. CaptureAppGraphLinks folds the canvas wire events, refusing
  // illegal creations; returns true if the model changed.
  IMGUI_API bool                AppGraphCanLink(ImGuiAppGraph* g, int start_port, int end_port, char* err, int err_size);
  IMGUI_API bool                CaptureAppGraphLinks(ImGuiAppGraph* g, char* err, int err_size);

  // Per-edge field-binding editor for one data link (call inside an attribute).
  IMGUI_API void                EditAppDataEdgeBindings(ImGuiAppGraph* g, int link_id);

  // Render the whole typed graph inside the current window. app may be null (design-only); when
  // non-null, builtin control bodies can reflect live data. *selected_node_id is caller-owned (-1 =
  // none): the editor reconciles it both ways (canvas<->tree) and clears dangling ids. show_live hides
  // (never deletes) live-mirror nodes when false.
  IMGUI_API void                ShowAppGraphEditor(ImGuiApp* app, ImGuiAppGraph* g, int* selected_node_id, bool show_live);

  // Editor command registry (F34): one table is the single source for the editor's verbs. Each declares
  // the surfaces it appears on (bitmask); the Space palette renders from it, and the four-roads
  // completeness test iterates it to check every verb is reachable from every surface it declares.
  enum ImGuiAppCmdSurface_
  {
    ImGuiAppCmdSurface_Palette  = 1 << 0,   // the Space operator palette
    ImGuiAppCmdSurface_Menu     = 1 << 1,   // a right-click context menu
    ImGuiAppCmdSurface_Shortcut = 1 << 2,   // a keyboard shortcut
    ImGuiAppCmdSurface_Gizmo    = 1 << 3,   // an on-canvas gizmo/overlay button
  };
  struct ImGuiAppEditorCommand
  {
    int              Id;         // dispatch id (matches the palette run() switch)
    const char*      Icon;       // FontAwesome glyph, or "" if none
    const char*      Label;      // "Edit: Delete selection"
    const char*      Shortcut;   // display string, e.g. "Ctrl+Z" or ""
    ImGuiKey         Key;        // shortcut key (ImGuiKey_None if none)
    int              Mods;       // ImGuiMod_* for the shortcut
    int              Surfaces;   // ImGuiAppCmdSurface_ bitmask
    ImGuiAppNodeKind AddKind;    // add verbs carry their kind; ImGuiAppNodeKind_COUNT otherwise
  };
  IMGUI_API int                          AppGraphEditorCommandCount();
  IMGUI_API const ImGuiAppEditorCommand* AppGraphEditorCommandAt(int index);
  IMGUI_API bool                         AppGraphEditorCommandAvailable(const ImGuiAppGraph* g, const ImGuiAppEditorCommand* c);

  // Remappable input->command binding (F74, post-100 horizon). The registry Key/Mods are the factory DEFAULT
  // chord; the graph's Keymap holds sparse user overrides. Dispatch resolves a pressed chord to a command Id
  // through the effective (override-or-default) map and runs it through the same path the palette uses --
  // replacing the hardcoded per-key checks. Delete (wire-aware), Tab/Esc (scope nav) keep dedicated handlers
  // and are not rebindable this phase; Space / Ctrl+P (the palette openers) are reserved.
  IMGUI_API void AppKeymapDefaultChord(int cmd_id, ImGuiKey* out_key, int* out_mods);
  IMGUI_API void AppKeymapEffectiveChord(const ImGuiAppGraph* g, int cmd_id, ImGuiKey* out_key, int* out_mods);
  IMGUI_API bool AppKeymapCommandRebindable(int cmd_id);
  IMGUI_API bool AppKeymapChordReserved(ImGuiKey key, int mods);
  IMGUI_API bool AppKeymapRebind(ImGuiAppGraph* g, int cmd_id, ImGuiKey key, int mods);   // false if not rebindable or the chord is reserved
  IMGUI_API void AppKeymapReset(ImGuiAppGraph* g, int cmd_id);                            // drop the override (back to the default)
  IMGUI_API void AppKeymapResetAll(ImGuiAppGraph* g);
  IMGUI_API int  AppKeymapConflict(const ImGuiAppGraph* g, ImGuiKey key, int mods, int except_cmd_id);   // colliding verb id, or -1
  IMGUI_API int  AppKeymapResolveChord(const ImGuiAppGraph* g, ImGuiKey key, int mods);   // command id bound to this chord, or -1
  IMGUI_API void AppGraphShowKeymapEditor(ImGuiAppGraph* g);                              // F75: rebind UI (call inside your own window)

  // Render a design Control's effective Persist/Temp fields as a mock panel of real ImGui widgets
  // (values are scratch), or -- for a LIVE node with live_app -- the running control's reflected
  // members with their current values. node_id < 0 -> hint.
  IMGUI_API void                AppGraphRenderMockPanel(ImGuiAppGraph* g, int node_id, ImGuiApp* live_app = nullptr);

  // Inspector for one node's authored data. Live nodes are read-only. node_id < 0 -> hint. Edits
  // mutate the graph in place.
  IMGUI_API void                EditAppNodeInspector(ImGuiAppGraph* g, int node_id);
  IMGUI_API void                EditAppNodeInspectorEx(ImGuiAppGraph* g, int node_id, ImGuiApp* live_app);   // + live style write-back (see workbench §3.5)
  IMGUI_API void                EditAppNodesInspectorMulti(ImGuiAppGraph* g);   // multi-selection: intersection editing (style across all selected)

  // Inspector section header (workbench §5.1): collapse triangle + icon + label, optional enable
  // checkbox, optional kebab whose click the caller answers with its own popup. Open state is
  // session-lived per window.
  // persist_seed != 0 keys the open/collapsed state by that seed (e.g. node kind) instead of the id
  // stack -- so a collapse persists across every node of the same kind (F41), not per node instance.
  IMGUI_API bool                AppInspectorSection(const char* str_id, const char* icon, const char* label, bool* enabled, bool* kebab_clicked, ImGuiID persist_seed = 0);

  // Origin breadcrumb for a selected node: "sel: MainWindow > Mixer [design]" / "[live]" /
  // "[promoted]" / "sel: -" when id < 0 or unknown.
  IMGUI_API void                AppGraphSelectionBreadcrumb(const ImGuiAppGraph* g, int node_id, char* buf, int buf_size);

  // Mirror the running app's controls into *g: remove all prior live nodes, add one read-only live
  // Control node per pushed control (keyed by GetControlDataID) plus their data edges, and flag design
  // controls whose emitted data type matches a live node. Design nodes untouched. Safe every frame.
  IMGUI_API void                BuildAppLiveGraph(const ImGuiApp* app, ImGuiAppGraph* g);

  // Outliner panel (call inside your own child/window): the running app's composition plus the graph's
  // authored nodes. Clicking a graph row selects that node in the editor. *selected_node_id is
  // caller-owned (-1 = none). show_live false hides (never deletes) live-mirror rows.
  IMGUI_API void                ShowAppGraphTree(const ImGuiApp* app, ImGuiAppGraph* g, int* selected_node_id, bool show_live = true);

  // Topologically order the Control nodes by data dependency (producers before consumers). Returns
  // false and writes err on a cycle. out_control_ids receives node ids in push order. include_live
  // false = authored domain only (validation/health); true = the full mirrored composition (codegen).
  // priority (F59): optional push-order preference (the concatenated F58 authored orders) -- among ready
  // zero-in-degree controls the earliest-ranked one drains first, so a topologically legal authored order
  // emits verbatim; null reproduces the plain node-order sort.
  IMGUI_API bool                AppGraphTopoOrder(const ImGuiAppGraph* g, ImVector<int>* out_control_ids, char* err, int err_size, bool include_live = false, ImVector<int>* out_cycle = nullptr, const ImVector<int>* priority = nullptr);

  // Data-dependency (topo) cycle surfacing (F21). Fills out_nodes with the controls the topo sort could
  // not schedule -- the cycle plus anything it blocks -- and out_name (optional) with a member's name.
  // Returns the count (0 when acyclic). The Select verb jumps the selection to out_nodes.
  IMGUI_API int                 AppGraphDependencyCycle(const ImGuiAppGraph* g, ImVector<int>* out_nodes, char* out_name, int name_size);

  // One problem found by AppGraphValidate. Severity: 1 = warning, 2 = error. NodeId is the node to
  // reveal (-1 for whole-graph issues).
  struct ImGuiAppGraphIssue
  {
    int  NodeId;
    int  Severity;
    char Text[256];

    ImGuiAppGraphIssue() { NodeId = -1; Severity = 1; Text[0] = 0; }
  };

  // Static-analyze the design graph, appending problems to *out (clears nothing). Live-mirror nodes
  // are skipped.
  IMGUI_API void                AppGraphValidate(const ImGuiAppGraph* g, ImVector<ImGuiAppGraphIssue>* out);

  // Brushing across coordinated views: any panel reports the datum under the mouse; every panel reads
  // LAST frame's report and highlights it (one-frame latency). The source lets a view skip echoing its
  // own hover. Single-editor-instance state.
  typedef int ImGuiAppHoverSource;
  enum ImGuiAppHoverSource_
  {
    ImGuiAppHoverSource_None = 0,
    ImGuiAppHoverSource_Canvas,      // the node editor
    ImGuiAppHoverSource_Tree,        // the outliner
    ImGuiAppHoverSource_Inspector,   // inspector / binding rows
    ImGuiAppHoverSource_External,    // problems list, code panel, any host-app panel
  };
  // Context keymap hint + transient refused-link errors, computed by ShowAppGraphEditor every frame.
  // Render it OUTSIDE the canvas (host status bar). severity: 0 = plain hint, 2 = error (refused
  // link). Valid for the frame after the editor ran.
  IMGUI_API const char*         AppGraphStatusHint(const ImGuiAppGraph* g, int* out_severity);

  IMGUI_API void                AppGraphHoverNode(const ImGuiAppGraph* g, int node_id, ImGuiAppHoverSource source);
  IMGUI_API void                AppGraphHoverLink(const ImGuiAppGraph* g, int link_id, ImGuiAppHoverSource source);
  IMGUI_API int                 AppGraphHoveredNode(const ImGuiAppGraph* g, ImGuiAppHoverSource* out_source);   // -1 = none; out_source may be null
  IMGUI_API int                 AppGraphHoveredLink(const ImGuiAppGraph* g, ImGuiAppHoverSource* out_source);

  // Host verbs surfaced in the canvas command palette (workbench W2). Register each frame before
  // ShowAppGraphEditor (pointers must outlive the frame); a picked command is reported back through
  // AppGraphConsumeHostCommand (one-frame latency, the editor never calls the host).
  struct ImGuiAppGraphHostCmd
  {
    const char* Label;    // e.g. "File: Save graph"
    const char* Shortcut; // displayed dim + right-aligned; "" = none
    int         Id;       // host-defined, returned by AppGraphConsumeHostCommand
    ImGuiKey    Key = ImGuiKey_None;   // optional keyboard road; the editor records a match, host owns the meaning
    int         Mods = 0;              // ImGuiMod_ combo required with Key
  };
  IMGUI_API void                AppGraphSetHostCommands(const ImGuiAppGraph* g, const ImGuiAppGraphHostCmd* cmds, int count);
  IMGUI_API int                 AppGraphConsumeHostCommand(const ImGuiAppGraph* g);   // picked host cmd id since last call, or -1

  // One-shot: open the add-node palette at the canvas center on the editor's next submission (same
  // palette as RMB / Space / the + gizmo).
  IMGUI_API void                AppGraphRequestAddPalette(const ImGuiAppGraph* g);
  IMGUI_API void                AppGraphRequestCmdPalette(const ImGuiAppGraph* g);   // open the Space operator palette (F34)

  // One-shot: frame the whole graph on the editor's next submission.
  IMGUI_API void                AppGraphRequestFitAll(const ImGuiAppGraph* g);

  // Composer chrome palette, derived from the current ImGuiStyle theme (AppComposerStyleFromTheme):
  // neutrals ride the WindowBg -> Text axis, semantic hues are pulled toward Text for light-theme
  // legibility. Fields are final packed IM_COL32 values; overwrite after derivation to customize.
  struct ImGuiAppComposerStyle
  {
    // Node kind accents
    ImU32 KindLayer;
    ImU32 KindWindow;
    ImU32 KindSidebar;
    ImU32 KindControl;
    ImU32 KindStruct;
    ImU32 KindField;
    ImU32 KindOp;
    ImU32 KindDefault;
    // Layer-type accents
    ImU32 LayerTask;
    ImU32 LayerCommand;  // also marks hidden nodes
    ImU32 LayerStatus;
    ImU32 LayerLayout;
    ImU32 LayerDisplay;
    ImU32 AccentNeutral; // fallback accent for typeless rows/ports
                         // Pins
    ImU32 PinData;
    ImU32 PinChild;
    ImU32 PinTie;
    ImU32 PinDefault;
    // Diagnostics
    ImU32 SevError;
    ImU32 SevWarn;
    ImU32 ErrorText;
    ImU32 Danger;
    // Live mirror / promotion
    ImU32 OriginLive;
    ImU32 OriginPromoted;
    ImU32 DotLive;
    ImU32 DotPromoted;
    ImU32 DotDrift;
    // Overlay accents
    ImU32 Gold;
    // Field chrome (flat draw-list widgets)
    ImU32 FieldBg;
    ImU32 FieldBgHovered;
    ImU32 FieldBgEdit;
    ImU32 FieldBorder;
    ImU32 FieldText;
    ImU32 TextMuted;    // idle glyphs (disclosure chevrons)
    ImU32 TextOnAccent; // near-black text/numerals over accent fills
    ImU32 DarkOutline;  // near-black rings over accent fills
                        // Group boxes + numbered rail
    ImU32 GroupFill;
    ImU32 GroupOutline;
    ImU32 GroupTitleBg; // opaque: grid must not bleed through text
    ImU32 RailLine;
  };
  IMGUI_API ImGuiAppComposerStyle* AppComposerGetStyle();
  // Recompute all composer colors from the current ImGuiStyle theme (requires a live context).
  // Runs lazily on first AppComposerGetStyle; call again after switching themes. Re-deriving the
  // global style also reseeds AppGraphChromeTheme, discarding live inspector edits.
  IMGUI_API void AppComposerStyleFromTheme(ImGuiAppComposerStyle* style);

  // Composer chrome scalar idiom -- one table for the motion (F38) and type/space ladders (F39). Not
  // theme-derived; the single source so no overlay hard-codes an alpha, and no chrome text hard-codes
  // an off-ladder size. Type tiers are RATIOS of the body font size; spacing steps in SpaceQuantum em.
  struct ImGuiAppComposerMotion
  {
    // Motion + quietness (F38)
    float OverlayRest    = 0.55f;    // an idle canvas overlay sits quiet
    float OverlayHover   = 1.00f;    // ... brightens to full when the pointer rests on it
    float OverlayGesture = 0.20f;    // ... and recedes during a wire-drag / marquee (get out of the way)
    float FadeMs         = 150.0f;   // single linear alpha fade between those states (and for transient chrome)
    // Typography ladder (F39): chrome text tiers, strictly descending
    float TypeBody       = 1.00f;    // primary chrome text
    float TypeSecondary  = 0.90f;    // secondary tier (chip labels, sub-rows)
    float TypeCaption    = 0.80f;    // smallest tier (dense readouts, code gutter, scope strip)
    // Spacing quantum (F39): chrome gaps step in this em fraction
    float SpaceQuantum   = 0.25f;
  };
  IMGUI_API ImGuiAppComposerMotion* AppComposerGetMotion();

  // F38 read-backs: the animated overlay alpha this frame, and the last-drawn gizmo cluster / editor
  // canvas rects (screen). Exposed so the motion ladder is on-camera verifiable.
  IMGUI_API float AppGraphEditorOverlayAlpha(const ImGuiAppGraph* g);
  IMGUI_API void  AppGraphEditorGizmoRect(const ImGuiAppGraph* g, ImVec2* out_min, ImVec2* out_max);
  IMGUI_API void  AppGraphEditorCanvasRect(const ImGuiAppGraph* g, ImVec2* out_min, ImVec2* out_max);

  // F40: viewport gizmo centres (screen), in draw order (0 Add, 1 Frame, 2 Fit, 3 Tidy, 4 Snap,
  // 5 Overlays, 6 View-scope). Draw-list buttons carry no id, so the click-path test targets these.
  IMGUI_API int    AppGraphEditorGizmoCount(const ImGuiAppGraph* g);
  IMGUI_API ImVec2 AppGraphEditorGizmoCenter(const ImGuiAppGraph* g, int index);

  // F41: open the quick inspector pinned to a node ("Inspect here"); read back the pinned node (-1 when
  // unpinned or the quick inspector is closed).
  IMGUI_API void AppGraphInspectHere(const ImGuiAppGraph* g, int node_id);
  IMGUI_API int  AppGraphEditorQuickInspectNode(const ImGuiAppGraph* g);

  // The composer chrome's push-stack palette, exposed read-write (stable pointer): the project
  // inspector's Theme section edits it live. Col slots are semantic and fixed; Value/Active are the
  // editable half. Seeded from AppComposerGetStyle.
  struct ImGuiAppChromeTheme
  {
    ImGuiAppColorModDesc Combo[8]; // dropdown fields (enum combos, struct picker): field + popup + rows
    ImGuiAppColorModDesc Edit[4];  // in-place editors (InputText/InputInt): transparent frame over the drawn bg
  };
  IMGUI_API ImGuiAppChromeTheme*    AppGraphChromeTheme();

  // Canvas view settings live on the graph (`_Ed.View`); stable pointer for host persistence.
  IMGUI_API ImGuiAppEditorState*    AppGraphEditorState(const ImGuiAppGraph* g);   // created on first use
  IMGUI_API ImGuiAppGraphViewState* AppGraphViewState(ImGuiAppGraph* g);

  // The graph's canvas-engine state (see imguiapp_canvas.h): hosts and tests can query geometry
  // or drive the camera. Created on the graph's first editor frame.
  IMGUI_API ::ImGuiCanvasState* AppGraphEditorCanvas(const ImGuiAppGraph* g);

  // Scope interior: a data edge crossing the current drill-down boundary docks on the wall as a
  // portal chip (inbound producers left, outbound consumers right). Derived per frame from
  // Links + ViewScope -- no ids, no persistence, no codegen, no validation.
  struct ImGuiAppScopePortal
  {
    int  LinkId;        // the crossing data edge
    int  InsidePortId;  // the in-scope endpoint's port (the chip wires to this pin)
    int  OutsideNodeId; // the off-scope node the chip names; click jumps to its scope
    bool Inbound;       // true: outside producer -> inside consumer (left wall); false: right wall

    ImGuiAppScopePortal() { LinkId = -1; InsidePortId = -1; OutsideNodeId = -1; Inbound = true; }
  };
  IMGUI_API void                AppScopeCollectPortals(const ImGuiAppGraph* g, ImVector<ImGuiAppScopePortal>* out);

  // One-line owner readout for the scope wall title bar: a window's placement
  // ("320x240 @ (64,48) . AlwaysAutoResize"), a sidebar's dock ("dock Down . auto . AutoResize").
  // Empty for kinds without placement/dock config.
  IMGUI_API void                AppNodeConfigSummary(const ImGuiAppNode* n, char* buf, int buf_size);

  // Cached validation, keyed by AppGraphSignature (+ bindings): cheap enough to query every frame.
  // Worst per-node severity: 0 = clean, 1 = warning, 2 = error.
  IMGUI_API const ImVector<ImGuiAppGraphIssue>* AppGraphIssuesCached(const ImGuiAppGraph* g);
  IMGUI_API int                 AppGraphNodeSeverity(const ImGuiAppGraph* g, int node_id);

  // Type-check one authored event's Expr against the control's effective field lists. Grammar: field
  // refs `temp_data->x` / `last_temp_data->x` / `data->x` / `<dep_param>-><field>`, nested struct
  // members via '.', bool/int/float literals, parens, scalar operators at C precedence
  // (! unary- * / % + - ^ comparisons && ||). `^` pairs bools or ints. The result type must fit
  // DstField. Empty Expr is valid (codegen copies the watched temp field). Returns true when
  // well-typed; else writes a diagnostic to err.
  IMGUI_API bool                AppEventExprCheck(const ImGuiAppGraph* g, const ImGuiAppNode* n, const ImGuiAppEventDesc* ev, char* err, int err_size);

  // One node's contribution to the last whole-graph emission: [LineBegin, LineEnd) in the generated
  // text. A node may own several spans (type definitions AND its bring-up line in SetupApp).
  struct ImGuiAppCodeSpan
  {
    int NodeId;
    int LineBegin;
    int LineEnd;

    ImGuiAppCodeSpan() { NodeId = -1; LineBegin = 0; LineEnd = 0; }
  };

  // Whole-graph codegen: data structs + controls with derived DataDependencies (topo order) + one
  // bring-up function pushing layers, then windows/sidebars, then controls. Appends to *out. The Ex
  // variant also records the per-node source map (out_spans may be null; cleared first).
  IMGUI_API void                GenerateAppGraphCode(const ImGuiAppGraph* g, ImGuiTextBuffer* out);
  IMGUI_API void                GenerateAppGraphCodeEx(const ImGuiAppGraph* g, ImGuiTextBuffer* out, ImVector<ImGuiAppCodeSpan>* out_spans);

  // Generated-shell bootstrap: the composition body from GenerateAppGraphCode (the SINGLE emitter),
  // wrapped in the host scaffold a standalone program needs -- a concrete ImGuiApp that composes via
  // the emitted SetupApp on its first initialized frame, plus main() running it. A composition that
  // hosts an ImGuiAppComposerControl thus emits a shell that runs the Composer against the library.
  IMGUI_API void                GenerateAppShellCode(const ImGuiAppGraph* g, ImGuiTextBuffer* out);

#ifndef IMGUIAPP_PREVIEW_ABI
#define IMGUIAPP_PREVIEW_ABI 20260706u   // host<->DLL preview ABI tag (F78); bump on any layout/vtable/signature change
#endif
  // DLL preview module (F78): the shell composition body + host scaffold, but the entry point is a C-ABI
  // (extern "C" __declspec(dllexport) ImGuiAppPreview_Create/_Destroy/_ABI) instead of main(). A runtime-
  // compiled module hands the host a composed ImGuiApp built by the same SetupApp, crossing the boundary as
  // a framework base pointer. IMGUIAPP_PREVIEW_ABI is baked into both host and module; a load-time mismatch
  // (stale headers / wrong toolset) is refused rather than crashing on a layout skew.
  IMGUI_API void                GenerateAppPreviewModuleCode(const ImGuiAppGraph* g, ImGuiTextBuffer* out);

  // Per-node codegen: emits only the code one node produces -- a Control's struct(s) with derived
  // deps, the CommandLayer's AppCommand enum + dispatch, a bring-up line, or (App node) the whole
  // composition. Appends to *out.
  IMGUI_API void                GenerateAppNodeCode(const ImGuiAppGraph* g, const ImGuiAppNode* n, ImGuiTextBuffer* out, ImGuiApp* live_app = nullptr);   // live control + live_app -> reflected real structs

  // Persist / restore the whole graph as imgui-style text. LoadAppGraph also ingests the legacy
  // single/multi "[Draft]" format (each becomes a Control node). Return false on file error.
  IMGUI_API bool                SaveAppGraph(const char* path, const ImGuiAppGraph* g);
  IMGUI_API bool                LoadAppGraph(const char* path, ImGuiAppGraph* g);

  // Headless codegen: load a graph file, write its generated C++ to out_header_path. Returns false on
  // a load or write error.
  IMGUI_API bool                AppGraphGenerateToFiles(const char* graph_path, const char* out_header_path);

  // Line diff of two graphs' generated C++ (' ' context, '-' only in a, '+' only in b). Appends to *out.
  IMGUI_API void                AppGraphDiffCode(const ImGuiAppGraph* a, const ImGuiAppGraph* b, ImGuiTextBuffer* out);

  // Parse C `struct <Name> { <type> <field>; ... };` blocks out of C++ source, adding one Struct node
  // per struct (fields, types mapped back). New nodes laid out at `origin`. Returns structs added.
  IMGUI_API int                 AppGraphImportStructsFromCode(ImGuiAppGraph* g, const char* code, ImVec2 origin);

  // Parse the F16 control emitter's output back into Control nodes (F22). Each `struct <Name> :
  // ImGuiAppControl<<Data>, <TempData>[, deps...]>` becomes a Control node; its PersistFields/TempFields
  // are read from the referenced Data/TempData struct blocks in the same source. New nodes laid out at
  // `origin`. Returns controls added. (Deps, command selections and event blocks import in later passes.)
  IMGUI_API int                 AppGraphImportControlsFromCode(ImGuiAppGraph* g, const char* code, ImVec2 origin);

  // Whole-program import (F23): seed the foundation layers, then import controls (and, as they land,
  // custom layers, standalone structs, windows/sidebars and hosting) so that emit -> import -> emit is a
  // byte-equal fixed point. Returns controls added.
  IMGUI_API int                 AppGraphImportProgram(ImGuiAppGraph* g, const char* code);

  // Undo / redo. The editor calls AppGraphCheckpoint once per frame; snapshots coalesce while the
  // mouse is held or a widget is being edited, so a drag or inline rename folds into one step.
  // Ctrl+Z / Ctrl+Y are handled inside ShowAppGraphEditor; Undo/Redo are also exposed for a toolbar.
  IMGUI_API void                AppGraphCheckpoint(ImGuiAppGraph* g);
  IMGUI_API void                AppGraphUndo(ImGuiAppGraph* g);
  IMGUI_API void                AppGraphRedo(ImGuiAppGraph* g);
  IMGUI_API bool                AppGraphCanUndo(const ImGuiAppGraph* g);
  IMGUI_API bool                AppGraphCanRedo(const ImGuiAppGraph* g);

  // Undo-history introspection: Count = snapshots, Cursor = the one the live graph matches
  // (0..Count-1, -1 if empty), Goto = jump to a snapshot index.
  IMGUI_API int                 AppGraphHistoryCount(const ImGuiAppGraph* g);
  IMGUI_API int                 AppGraphHistoryCursor(const ImGuiAppGraph* g);
  IMGUI_API void                AppGraphHistoryGoto(ImGuiAppGraph* g, int index);
  IMGUI_API const char*         AppGraphHistoryLabel(const ImGuiAppGraph* g, int index);   // derived operation name ("Add Mixer", "Rename A -> B"); "" if unknown

  // Prefabs: SavePrefab serializes the roots' containment subtrees under a name (replacing one of the
  // same name); Instantiate stamps a saved prefab into g with fresh ids at `origin` and selects it.
  // Count/Name enumerate the registry (in-memory, session-lived).
  IMGUI_API void                AppGraphSavePrefab(const ImGuiAppGraph* g, const ImVector<int>& roots, const char* name);
  IMGUI_API int                 AppGraphPrefabCount(const ImGuiAppGraph* g);
  IMGUI_API const char*         AppGraphPrefabName(const ImGuiAppGraph* g, int index);
  IMGUI_API int                 AppGraphInstantiatePrefab(ImGuiAppGraph* g, int index, ImVec2 origin);

  // Seed the starter prefab library (producer/consumer pair, event->command control) when the registry
  // is empty. The registry itself persists in a "<graph>.prefabs" sidecar written/read by Save/LoadAppGraph.
  IMGUI_API void                AppGraphSeedStarterPrefabs(ImGuiAppGraph* g);

  // Ensure the four foundation layers (Window, Task, Command, Status) exist in g -- adds missing,
  // never duplicates. They are permanent (AppGraphRemoveNode refuses them). Call on new/empty graphs
  // and after load.
  IMGUI_API void                AppGraphEnsureFoundation(ImGuiAppGraph* g);

  // One-shot tidy layout: design nodes as a left-to-right containment tree, siblings stacked, parents
  // centered. L key inside the editor; also exposed for a toolbar. Layers keep their own column
  // packing; live-mirror nodes are left in place.
  IMGUI_API void                AppGraphAutoLayout(ImGuiAppGraph* g, bool show_live);
  // F44: drilled-scope tidy -- members left->right in execution order, this scope's placements only
  // (root GridPos untouched); falls back to AppGraphAutoLayout at root / non-sequential scopes.
  IMGUI_API void                AppScopeSequenceTidy(ImGuiAppGraph* g, bool show_live);
  // Direct members of the current scope in execution order; an authored F58 order overrides the derivation.
  IMGUI_API void                AppScopeSequenceIds(const ImGuiAppGraph* g, ImVector<int>* out);
  // F60 write gestures: reorder a scope member by authoring the F58 order record. MoveMember drops node_id at
  // new_slot; Nudge shifts it one slot (dir<0 earlier, dir>0 later). Both act on the CURRENT drilled scope,
  // refuse a move that permutes the core phase layers, and return true when the record changed.
  IMGUI_API bool                AppScopeOrderMoveMember(ImGuiAppGraph* g, int node_id, int new_slot);
  IMGUI_API bool                AppScopeOrderNudge(ImGuiAppGraph* g, int node_id, int dir);
}
  //=================== interpreter core + reconcile (folded from imguiapp_preview.h) ===================

// Previewer interpreter core (F66 design: docs/previewer-design.md). A SECOND backend for the object
// model beside codegen: it builds a real ImGuiApp from the authored graph and evaluates the model every
// frame -- so RegisterAppStorage, the four-phase pipeline, and ImGuiAppStateHistory apply verbatim.
// One ImGuiAppPreviewControl carries dynamic Persist|LastTemp|Temp byte buffers laid out from an
// effective-field manifest; one evaluator is a value-returning walk of the AppEventExprCheck grammar.
//
// F67 scope = App/Layer/Window/Sidebar/Control(design-draft)/Struct/Field/events/commands. Op-fold
// evaluation (F55) and animation-builtin update (F56) are named stubs -- see imguiapp_preview.cpp.


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

  //---------------------------------------------------------------------------
  // Edit-while-running reconcile (F67 interpreter CORE). The F68 preview SURFACE + brushing API is UI --
  // it moved to imguiapp_internal.h (Phase A3), gated behind IMGUIX_DISABLE_TOOLS.
  //---------------------------------------------------------------------------

  // Edit-while-running reconciliation (design 7). Rebuild the population from the (edited) borrowed graph,
  // preserving every surviving (sanitized name, ImGuiAppFieldType) Persist/LastTemp slot byte-for-byte and
  // default-initialising the rest -- a rewire changes behaviour next frame WITHOUT losing unrelated fields.
  // Refuses (keeps the running population intact) on a dependency cycle, reason in err. False = refused.
  IMGUI_API bool             AppPreviewReconcile(ImGuiAppPreview* session, char* err, int err_size);
}

//=================== animation builtins (folded from imguiapp_anim.h) ===================
// Animation builtin controls (F56): dt-driven Task-phase animators, each a compiled ImGuiAppControl with a
// typed PersistData DataOut consumed downstream in dependency order. Registered into the Composer via
// AppGraphAddBuiltin (the RandomTime precedent). OnUpdate is the SOLE mutator and the whole accumulator lives
// in PersistData -- ImGuiAppStateHistory snapshot/restore then reproduces every trajectory byte-for-byte under
// Fixed-dt, so App-time scrub restores an animator's value. No TU globals, no static carry. Triggers obey the
// temp^last rising-edge idiom.


// Ease selector for ImAppTween.
enum ImAppEase_
{
  ImAppEase_Linear = 0,
  ImAppEase_Smoothstep,
};

// Interpolation curve on t in [0,1].
static inline float ImAppEase(int ease, float t)
{
  t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  return ease == ImAppEase_Smoothstep ? t * t * (3.0f - 2.0f * t) : t;
}

//-----------------------------------------------------------------------------
// Tween: eases a->b over duration seconds; restarts on a rising trigger.
//-----------------------------------------------------------------------------
struct ImAppTweenData
{
  float a;         // start value (param)
  float b;         // end value (param)
  float duration;  // seconds (param)
  int   ease;      // ImAppEase_ (param)
  float t;         // accumulator [0,1]
  float value;     // DataOut: eased a->b at t
  bool  done;      // DataOut: t reached 1
};
struct ImAppTweenTempData
{
  bool trigger;    // rising edge restarts
};
struct ImAppTween : ImGuiAppControl<ImAppTweenData, ImAppTweenTempData>
{
  virtual void OnInitialize(ImGuiApp* app, ImAppTweenData* data) const override final
  {
    IM_UNUSED(app);
    data->a = 0.0f;
    data->b = 1.0f;
    data->duration = 1.0f;
    data->ease = ImAppEase_Linear;
    data->t = 0.0f;
    data->value = data->a;
    data->done = false;
  }

  virtual void OnUpdate(float dt, ImAppTweenData* data, const ImAppTweenTempData* temp_data, const ImAppTweenTempData* last_temp_data) const override final
  {
    if (temp_data->trigger && !last_temp_data->trigger)   // rising: restart
    {
      data->t = 0.0f;
      data->done = false;
    }
    data->t += data->duration > 0.0f ? dt / data->duration : 1.0f;
    if (data->t > 1.0f)
      data->t = 1.0f;
    data->value = data->a + (data->b - data->a) * ImAppEase(data->ease, data->t);
    data->done = data->t >= 1.0f;
  }
};

//-----------------------------------------------------------------------------
// Timer: counts elapsed seconds; done latches at duration; restarts on a rising trigger.
//-----------------------------------------------------------------------------
struct ImAppTimerData
{
  float duration;  // seconds (param)
  float elapsed;   // accumulator
  bool  done;      // DataOut: elapsed >= duration
};
struct ImAppTimerTempData
{
  bool trigger;    // rising edge restarts
};
struct ImAppTimer : ImGuiAppControl<ImAppTimerData, ImAppTimerTempData>
{
  virtual void OnInitialize(ImGuiApp* app, ImAppTimerData* data) const override final
  {
    IM_UNUSED(app);
    data->duration = 1.0f;
    data->elapsed = 0.0f;
    data->done = false;
  }

  virtual void OnUpdate(float dt, ImAppTimerData* data, const ImAppTimerTempData* temp_data, const ImAppTimerTempData* last_temp_data) const override final
  {
    if (temp_data->trigger && !last_temp_data->trigger)   // rising: restart
    {
      data->elapsed = 0.0f;
      data->done = false;
    }
    data->elapsed += dt;
    data->done = data->elapsed >= data->duration;
  }
};

//-----------------------------------------------------------------------------
// Spring: damped-harmonic integration toward target; {value,velocity} is the accumulator.
//-----------------------------------------------------------------------------
struct ImAppSpringData
{
  float target;     // param
  float stiffness;  // param (k)
  float damping;    // param (c)
  float value;      // DataOut
  float velocity;   // accumulator
};
struct ImAppSpringTempData
{
};
struct ImAppSpring : ImGuiAppControl<ImAppSpringData, ImAppSpringTempData>
{
  virtual void OnInitialize(ImGuiApp* app, ImAppSpringData* data) const override final
  {
    IM_UNUSED(app);
    data->target = 1.0f;
    data->stiffness = 8.0f;
    data->damping = 2.0f;
    data->value = 0.0f;
    data->velocity = 0.0f;
  }

  virtual void OnUpdate(float dt, ImAppSpringData* data, const ImAppSpringTempData* temp_data, const ImAppSpringTempData* last_temp_data) const override final
  {
    IM_UNUSED(temp_data);
    IM_UNUSED(last_temp_data);
    data->velocity += (data->stiffness * (data->target - data->value) - data->damping * data->velocity) * dt;
    data->value += data->velocity * dt;
  }
};

//-----------------------------------------------------------------------------
// Pulse: free-running phase in [0,1); pulse is a one-frame flag on each wrap.
//-----------------------------------------------------------------------------
struct ImAppPulseData
{
  float period;   // seconds per cycle (param)
  float phase;    // accumulator [0,1)
  bool  pulse;    // DataOut: one-frame wrap flag
};
struct ImAppPulseTempData
{
};
struct ImAppPulse : ImGuiAppControl<ImAppPulseData, ImAppPulseTempData>
{
  virtual void OnInitialize(ImGuiApp* app, ImAppPulseData* data) const override final
  {
    IM_UNUSED(app);
    data->period = 1.0f;
    data->phase = 0.0f;
    data->pulse = false;
  }

  virtual void OnUpdate(float dt, ImAppPulseData* data, const ImAppPulseTempData* temp_data, const ImAppPulseTempData* last_temp_data) const override final
  {
    IM_UNUSED(temp_data);
    IM_UNUSED(last_temp_data);
    data->phase += data->period > 0.0f ? dt / data->period : 0.0f;
    if (data->phase >= 1.0f)
    {
      data->phase -= 1.0f;
      data->pulse = true;
    }
    else
    {
      data->pulse = false;
    }
  }
};

#ifndef IMGUIX_DISABLE_TOOLS

  //=================== canvas UI (folded from imguiapp_canvas.h, Phase A4) ===================
struct ImGuiCanvasStyle
{
  // Colors: set directly, no push/pop stack. CanvasCreate derives them from the current
  // ImGuiStyle theme (CanvasStyleFromTheme); hosts may overwrite after.
  ImU32 GridBg;
  ImU32 GridLine;
  ImU32 GridLinePrimary;
  ImU32 NodeBg;
  ImU32 NodeBgHovered;
  ImU32 NodeBgSelected;
  ImU32 NodeOutline;
  ImU32 NodeOutlineSelected;
  ImU32 TitleBar;       // per-node override via CanvasNextNodeTitleColor
  ImU32 TitleBarHovered;
  ImU32 TitleBarSelected;
  ImU32 TitleText;
  ImU32 TitleEditBg;    // InputText field bg while renaming a node title
  ImU32 Wire;
  ImU32 WireHovered;
  ImU32 WireSelected;
  ImU32 PinData;        // circle pins (data edges)
  ImU32 PinContainment; // square pins (containment edges)
  ImU32 PinHovered;
  ImU32 MiniMapBg;
  ImU32 MiniMapBgHovered;
  ImU32 MiniMapOutline;
  ImU32 MiniMapNodeBg;
  ImU32 MiniMapNodeBgHovered;
  ImU32 MiniMapNodeBgSelected;
  ImU32 MiniMapNodeOutline;
  ImU32 MiniMapLink;
  ImU32 MiniMapCanvas;  // the current-view rect inside the map
  ImU32 MiniMapCanvasOutline;

  // Metrics in MODEL units (the engine zooms them; hosts never pre-scale).
  float  GridSpacing;
  float  NodeRounding;
  ImVec2 NodePadding;
  float  NodeBorder;
  float  WireThickness;
  float  PinRadius;
  float  PinHoverRadius;

  bool GridLines;
  bool GridSnap; // snap node origins to GridSpacing while dragging
};

struct ImGuiCanvasIO
{
  // Bindings policy.
  bool  LmbPansEmptyCanvas; // LMB-drag on empty canvas pans (no box select)
  bool  RmbPans;            // RMB-drag pans; a short RMB click reports CanvasMenuRequest*
  bool  WheelZooms;         // cursor-centered; Ctrl+wheel zooms even over nodes/items
  float ZoomMin, ZoomMax;   // clamp (defaults 0.3 / 2.5)
};

struct ImGuiCanvasState;   // opaque; created/destroyed by the engine; passed explicitly, no context stack

namespace ImGui
{
  IMGUI_API ImGuiCanvasState* CanvasCreate();
  IMGUI_API void              CanvasDestroy(ImGuiCanvasState* c);
  IMGUI_API ImGuiCanvasStyle* CanvasGetStyle(ImGuiCanvasState* c);
  IMGUI_API ImGuiCanvasIO*    CanvasGetIO(ImGuiCanvasState* c);
  // Recompute all style colors from the current ImGuiStyle theme (requires a live context).
  // Called by CanvasCreate; call again after switching themes.
  IMGUI_API void              CanvasStyleFromTheme(ImGuiCanvasStyle* style);

  // ---- frame ----------------------------------------------------------------------------------
  // CanvasBegin opens the child (fills the available region unless size is given), draws the grid,
  // and applies camera input per the IO policy. Submit nodes/pins/wires between Begin/End;
  // CanvasEnd draws wires + pins from THIS frame's geometry, resolves hover, runs the interaction
  // FSM, and latches events (valid until the next CanvasBegin).
  IMGUI_API void CanvasBegin(ImGuiCanvasState* c, const char* str_id, ImVec2 size /*= 0,0*/);
  IMGUI_API void CanvasEnd(ImGuiCanvasState* c);

  // Decoration hook: returns the canvas draw list switched to the BACKGROUND channel (above the grid,
  // below node plates; wires land in the same channel later, so they draw over these decorations).
  // Call between CanvasBegin and the first node; the next CanvasBeginNode restores the content channel.
  IMGUI_API ImDrawList* CanvasBackgroundDrawList(ImGuiCanvasState* c);

  // Annotation hook: the canvas child's draw list AFTER CanvasEnd. Appended commands render above
  // every merged channel (grid, plates, content) but stay inside the child's z-order -- annotations
  // ride the canvas, they never paint over other windows (the viewport foreground list does).
  IMGUI_API ImDrawList* CanvasAnnotationDrawList(ImGuiCanvasState* c);

  // ---- camera (pan in pixels, zoom unitless; screen = origin + pan + model * zoom) -------------
  IMGUI_API ImVec2 CanvasGetPan(const ImGuiCanvasState* c);
  IMGUI_API void   CanvasSetPan(ImGuiCanvasState* c, ImVec2 pan);
  IMGUI_API float  CanvasGetZoom(const ImGuiCanvasState* c);
  // Model -> screen scale (Zoom x host font DPI/user factor). Use for ALL model<->pixel
  // conversions; CanvasGetZoom is the logical camera only (font pushes, persisted view state).
  IMGUI_API float  CanvasGetScale(const ImGuiCanvasState* c);
  IMGUI_API void   CanvasSetZoom(ImGuiCanvasState* c, float zoom, ImVec2 keep_screen_pos);   // keeps that screen point fixed
  IMGUI_API ImVec2 CanvasToScreen(const ImGuiCanvasState* c, ImVec2 model);
  IMGUI_API ImVec2 CanvasFromScreen(const ImGuiCanvasState* c, ImVec2 screen);
  IMGUI_API void   CanvasCenterOn(ImGuiCanvasState* c, ImVec2 model_pos);
  IMGUI_API void   CanvasFitRect(ImGuiCanvasState* c, ImVec2 model_min, ImVec2 model_max, float margin_px);
  IMGUI_API void   CanvasFitNodes(ImGuiCanvasState* c, const int* node_ids, int count, float margin_px);
  IMGUI_API void   CanvasFitAll(ImGuiCanvasState* c, float margin_px);                       // over nodes submitted last frame

  // Minimap: call between CanvasBegin/CanvasEnd; drawn by CanvasEnd as a bottom-right inset fitted
  // to the node content's aspect within size_fraction of the canvas. Holding LMB over the map
  // continuously recenters the camera. Its rect is excluded from the FSM.
  IMGUI_API void   CanvasMiniMap(ImGuiCanvasState* c, float size_fraction);

  // ---- nodes (geometry in MODEL units, always) --------------------------------------------------
  // Between CanvasBeginNode/CanvasEndNode submit ordinary ImGui widgets; the engine renders them
  // under the zoomed font + scaled layout metrics and measures the node the SAME frame.
  IMGUI_API void   CanvasNextNodeTitle(ImGuiCanvasState* c, const char* title, ImU32 title_color /*= 0 -> style*/);
  // Per-node presentation intent, all consumed by the next CanvasBeginNode: kind word (muted,
  // right-aligned), origin dot (leading; ring form for promoted), framed badge after the name,
  // corner rounding in model units (< 0 = style), rule under the title band (0 none / 1 solid /
  // 2 dashed), and an edge stripe on one ImGuiCanvasPinSide_.
  IMGUI_API void   CanvasNextNodeTitleTag(ImGuiCanvasState* c, const char* tag);
  IMGUI_API void   CanvasNextNodeOriginDot(ImGuiCanvasState* c, ImU32 color, bool ring);
  IMGUI_API void   CanvasNextNodeTitleBadge(ImGuiCanvasState* c, const char* badge);
  IMGUI_API void   CanvasNextNodeRounding(ImGuiCanvasState* c, float model_rounding);
  IMGUI_API void   CanvasNextNodeWidth(ImGuiCanvasState* c, float model_width);          // normalized plate width; <= 0 = content-sized
  IMGUI_API float  CanvasNodeNeededWidth(const ImGuiCanvasState* c, int node_id);        // content-derived width need (0 until measured)
  IMGUI_API void   CanvasNextNodeHeaderRule(ImGuiCanvasState* c, int rule, ImU32 color);
  IMGUI_API void   CanvasNextNodeEdgeStripe(ImGuiCanvasState* c, int side, ImU32 color, float model_thickness);
  IMGUI_API void   CanvasNextNodeAlpha(ImGuiCanvasState* c, float alpha);   // < 1 dims plate + content (disabled look); consumed by the next CanvasBeginNode
  // Editable variant (host-driven rename): while *editing, the engine renders an InputText in the
  // title bar bound to buf and clears *editing when it deactivates. Pair with CanvasNodeDoubleClicked
  // to enter the state (the host decides whether a double-click renames or drills).
  IMGUI_API void   CanvasNextNodeTitleEditable(ImGuiCanvasState* c, char* buf, int buf_size, bool* editing, ImU32 title_color);
  IMGUI_API bool   CanvasBeginNode(ImGuiCanvasState* c, int node_id);   // false if culled (off-screen): body may be skipped, geometry persists
  IMGUI_API void   CanvasEndNode(ImGuiCanvasState* c);
  IMGUI_API ImVec2 CanvasNodePos(const ImGuiCanvasState* c, int node_id);        // model
  IMGUI_API void   CanvasSetNodePos(ImGuiCanvasState* c, int node_id, ImVec2 model_pos);
  IMGUI_API ImVec2 CanvasNodeSize(const ImGuiCanvasState* c, int node_id);       // model; this frame if submitted, else last known
  IMGUI_API const char* CanvasNodeTitleBadge(const ImGuiCanvasState* c, int node_id);   // title-bar badge text ("" if none / unknown node)
  IMGUI_API void   CanvasSetNodeDraggable(ImGuiCanvasState* c, int node_id, bool draggable);
  IMGUI_API void   CanvasSetNodeSolid(ImGuiCanvasState* c, int node_id, bool solid);   // solid nodes cannot be dragged into overlap with other solid nodes (slide to contact)
  // Declare a solid region (model units) for THIS frame: solid-node drags cannot enter it (the
  // next frame's drag consumes it, like node geometry). Call between CanvasBegin/CanvasEnd.
  IMGUI_API void   CanvasAddSolidRect(ImGuiCanvasState* c, ImVec2 model_min, ImVec2 model_max);

  // ---- pins + wires -----------------------------------------------------------------------------
  // Kind is the interaction role (which end pairs with which when wiring). Side is the node edge the
  // pin sits on and the direction its wire leaves -- orthogonal to Kind. Left/Right give the classic
  // horizontal data read; Top/Bottom give a vertical owner-over-child containment read.
  enum ImGuiCanvasPinKind_ { ImGuiCanvasPin_In = 0, ImGuiCanvasPin_Out = 1 };
  enum ImGuiCanvasPinShape_ { ImGuiCanvasPinShape_Circle = 0, ImGuiCanvasPinShape_Square = 1 };
  enum ImGuiCanvasPinSide_ { ImGuiCanvasPinSide_Left = 0, ImGuiCanvasPinSide_Right = 1, ImGuiCanvasPinSide_Top = 2, ImGuiCanvasPinSide_Bottom = 3 };
  IMGUI_API void   CanvasNextPinColor(ImGuiCanvasState* c, ImU32 color);   // 0 -> style (by shape); consumed by the next CanvasBeginPin
  IMGUI_API void   CanvasNextPinSide(ImGuiCanvasState* c, int side);   // override the next pin's edge; default derives from Kind (In->Left, Out->Right)
  IMGUI_API void   CanvasBeginPin(ImGuiCanvasState* c, int pin_id, int kind /*In|Out*/, int shape);
  IMGUI_API void   CanvasEndPin(ImGuiCanvasState* c);
  // Row-less edge pin: an at-most-one-per-edge singleton (e.g. containment parent/children) placed at
  // the center of its Side edge. Submits no widget and consumes no cursor -- call between BeginNode/EndNode.
  IMGUI_API void   CanvasEdgePin(ImGuiCanvasState* c, int pin_id, int kind /*In|Out*/, int shape, int side);
  IMGUI_API void   CanvasNextWireDashed(ImGuiCanvasState* c);   // the next CanvasWire draws dashed (optional dependency)
  IMGUI_API void   CanvasWire(ImGuiCanvasState* c, int wire_id, int pin_a, int pin_b, ImU32 color /*= 0 -> style*/);
  IMGUI_API bool   CanvasWireExists(const ImGuiCanvasState* c, int wire_id);      // wire submitted this frame (query after CanvasEnd)
  IMGUI_API ImVec2 CanvasPinPos(const ImGuiCanvasState* c, int pin_id);           // model

  // ---- selection + hover ------------------------------------------------------------------------
  IMGUI_API int    CanvasNumSelectedNodes(const ImGuiCanvasState* c);
  IMGUI_API void   CanvasGetSelectedNodes(const ImGuiCanvasState* c, int* out, int cap);
  IMGUI_API void   CanvasSelectNode(ImGuiCanvasState* c, int node_id, bool additive);
  IMGUI_API void   CanvasClearSelection(ImGuiCanvasState* c);
  IMGUI_API int    CanvasHoveredNode(const ImGuiCanvasState* c);                  // -1 = none (valid after CanvasEnd)
  IMGUI_API int    CanvasHoveredWire(const ImGuiCanvasState* c);
  IMGUI_API int    CanvasHoveredPin(const ImGuiCanvasState* c);
  IMGUI_API int    CanvasWireDragSource(const ImGuiCanvasState* c);               // pin an active wire-drag started from, else -1 (for drag-time can-link telegraph)
  IMGUI_API int    CanvasSelectedWire(const ImGuiCanvasState* c);                 // single click-selected wire; -1 = none
  IMGUI_API void   CanvasClearWireSelection(ImGuiCanvasState* c);

  // ---- events (latched by CanvasEnd; valid until the next CanvasBegin) ---------------------------
  IMGUI_API bool   CanvasWireCreated(const ImGuiCanvasState* c, int* out_pin_a, int* out_pin_b);   // drag completed pin->pin (snap or release)
  IMGUI_API bool   CanvasWireDropped(const ImGuiCanvasState* c, int* out_from_pin, ImVec2* out_model_pos);   // released on empty canvas
  IMGUI_API bool   CanvasWireDetached(const ImGuiCanvasState* c, int* out_wire_id, int* out_grabbed_end_pin); // endpoint dragged off a pin
  IMGUI_API bool   CanvasNodeDoubleClicked(const ImGuiCanvasState* c, int* out_node_id); // LMB double-click on a node
  IMGUI_API bool   CanvasMenuRequestNode(const ImGuiCanvasState* c, int* out_node_id);   // short RMB click resolution
  IMGUI_API bool   CanvasMenuRequestWire(const ImGuiCanvasState* c, int* out_wire_id);
  IMGUI_API bool   CanvasMenuRequestEmpty(const ImGuiCanvasState* c, ImVec2* out_model_pos);
}

  //=============== DLL preview surface (folded from imguiapp_preview_dll.h, Phase A4) ===============
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
