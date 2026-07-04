#pragma once

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

#include "imgui.h"
#include "imgui_internal.h"               // ImFormatString
#include "imguiapp.h"                     // ImGuiApp, IM_LABEL_SIZE, ImGuiType<>, ImAppReflect

#include <format>                         // std::format (field stringify, confined to this header)
#include <string>                         // std::string (transient, copied into char[] at the boundary)
#include <string_view>                    // reflect member/type names
#include <type_traits>                    // dispatch traits

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

// One drafted control: a name plus its persisted and per-frame field sets.
struct ImGuiAppNodeDraft
{
  char                        Name[IM_LABEL_SIZE] = "NewControl";
  ImVector<ImGuiAppFieldDesc> PersistFields;
  ImVector<ImGuiAppFieldDesc> TempFields;
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
  ImGuiAppNodeKind_COUNT,
};

// The four CORE layer classes are permanent, one each, immutable type -- codegen emits
// PushAppLayer<ImGuiAppXxxLayer>. Custom is a user-authored ImGuiAppLayer subclass: the NODE'S NAME is
// its class name, any number may exist, codegen emits the subclass skeleton plus PushAppLayer<Name>.
typedef int ImGuiAppLayerType;
enum ImGuiAppLayerType_
{
  ImGuiAppLayerType_Task = 0,
  ImGuiAppLayerType_Command,
  ImGuiAppLayerType_Status,
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
  ImGuiDir                       DockDir                     = ImGuiDir_Down;          // Sidebar dock direction
  float                          DockSize                    = 0.0f;                   // Sidebar size
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
  int                            FieldList                   = 0;                      // Field node: which list it belongs to on its owner (0 = Persist, 1 = Temp)
  int                            PersistStructId             = -1;                     // Control: Struct node its PersistData was exploded into (-1 = inline)
  int                            TempStructId                = -1;                     // Control: Struct node its TempData was exploded into (-1 = inline)
  bool                           GroupCollapsed              = false;                  // descendants hidden behind a proxy chip (transient, not serialized)
  bool                           Hidden                      = false;                  // not submitted to the canvas (transient, not serialized)
};

// Per-data-edge field assignment: emits one "data->Dst = dep->Src;" line in OnUpdate. Keyed by LinkId,
// kept off the link (an ImVector member would break ImGuiAppNodeLink's aggregate brace-init).
struct ImGuiAppFieldBinding
{
  int  LinkId                  = 0;
  char DstField[IM_LABEL_SIZE] = "";
  char SrcField[IM_LABEL_SIZE] = "";
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
  ImGuiAppGraphViewState               View = { false, true, true, true, true, 1.0f };   // snap off; overlays on; 1:1
  mutable ImVector<ImGui::ImGuiAppGraphIssue> IssuesCache;
  mutable ImGuiID                      IssuesSig = 0;
  mutable bool                         IssuesValid = false;
  mutable ImGuiStorage                 IssuesSeverity;          // node id -> worst severity
  ImVector<int>                        PoolIds;                 // node ids the canvas holds
  ImVector<int>                        PrevPoolIds;
  char                                 StatusHint[256] = "";
  int                                  StatusSev = 0;
  mutable const ImGui::ImGuiAppGraphHostCmd* HostCmds = nullptr;  // registered per frame; host-owned
  mutable int                          HostCmdCount = 0;
  mutable int                          HostCmdPicked = -1;
  mutable bool                         AddPaletteRequest = false;   // one-shot
  mutable bool                         FitAllRequest = false;       // one-shot
  int                                  AutoLayoutCountdown = 2;     // launch default is a TIDIED layout: fires once real sizes exist (frame 2)
  bool                                 HelpOverlay = false;         // F1 shortcut cheat sheet
  bool                                 QuickInspector = false;      // N: floating quick inspector
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
  int                                  ToastSeq = -1;
  float                                ToastT0 = 0.0f;
  int                                  AppliedSel = -1;
  int                                  OutlinerRename = -1;         // node id being renamed in the tree, -1 = none
  bool                                 OutlinerRenameFocus = false;
  bool                                 OutlinerKindVis[ImGuiAppNodeKind_COUNT] = { true, true, true, true, true, true, true };
  ImGuiTextFilter                      OutlinerFilter;
  ImVector<ImGuiAppStyleModDesc>       StyleClipMods;               // style-section clipboard (session-lived, value-typed)
  ImVector<ImGuiAppColorModDesc>       StyleClipCols;
  bool                                 StyleClipHas = false;
  char*                                ClipText = nullptr;          // serialized partial graph, or null (owned)
  int                                  ClipPaste = 0;               // cascade counter so repeated pastes fan out
  ImVector<ImGuiAppPrefab>             Prefabs;                     // saved subtrees (owned strings)
  ImFont*                              CodeFont = nullptr;          // code panels; null -> UI font
  ImGuiAppEditorUndo                   Undo;
};

// The whole authored graph. One monotonic id allocator shared by every node/port/body-attr/link:
// ids are globally unique, never reused.
struct ImGuiAppGraph
{
  ImVector<ImGuiAppNode>         Nodes;
  ImVector<ImGuiAppNodeLink>     Links;
  ImVector<ImGuiAppFieldBinding> Bindings;
  ImVector<int>                  Selection;                            // multi-selection (node ids); the single selected_node_id is primary
  ImVector<int>                  ViewScope;                            // drill-down scope stack (node ids, outer->inner); empty = whole app; transient, not serialized
  ImVector<ImGuiAppScopeCamera>  ScopeCams;                            // per-branch camera memory (transient, not serialized)
  ImVector<ImGuiAppTrunkRoute>   _TrunkRoutes;                         // cached trunk routes, model units (transient, not serialized)
  ImVector<ImGuiAppDragStick>    _DragStick;                           // cluster originals for the active layer drag (transient)
  int                            _DragStickAnchor           = 0;       // layer node the sticks belong to (0 = no active capture)
  ImVector<ImVec4>               _GroupDragOrig;                       // (id, x, y) member originals for the active group-chip drag (transient)
  ImVec2                         _GroupDragMouse0           = ImVec2(0.0f, 0.0f); // model-space mouse origin of that drag (transient)
  ImVec4                         _GroupDragFrame0           = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // the dragged group's frame at drag start, model (transient)
  ImVec2                         _GroupDragApplied          = ImVec2(0.0f, 0.0f); // displacement actually granted so far (greedy catch-up; transient)
  bool                           _GroupDragMoved            = false;   // chip gesture latch: drag vs fold-click (transient)
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
  char                           LastLinkErr[IM_LABEL_SIZE] = "";      // last refused-link reason; transient, NOT in Save/Load
  int                            LastLinkErrSeq             = 0;       // bumped on every rejection
  ImGuiApp*                      LiveApp                    = nullptr; // running app this graph mirrors (set by BuildAppLiveGraph, read-only to
                                                                       // codegen); null = no live source; transient, NOT in Save/Load
  int                            _ScopeSig                  = -1;      // editor scope-change detector (transient; -1 = first frame)
  int                            _ScopeCamId                = -1;      // scope id the camera currently shows (-1 = root; transient)
  int                            _PendingFit                = 0;       // deferred fit-all countdown after a scope change (transient)
};

namespace ImGui
{
  // AddNode/AddBuiltin stamp the kind's mandatory ports and a body-attr id; the returned pointer is
  // valid only until the next node is added (Nodes may reallocate). Find* resolve by search, never by
  // index. RemoveNode also sweeps incident links and orphaned bindings.
  IMGUI_API int                 AppGraphAllocId(ImGuiAppGraph* g);
  IMGUI_API ImGuiAppNode*       AppGraphAddNode(ImGuiAppGraph* g, ImGuiAppNodeKind kind, const char* name);
  IMGUI_API ImGuiAppNode*       AppGraphAddBuiltin(ImGuiAppGraph* g, ImGuiAppNodeKind kind, const char* type_name, const char* data_type_name);
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
  IMGUI_API bool                AppInspectorSection(const char* str_id, const char* icon, const char* label, bool* enabled, bool* kebab_clicked);

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
  IMGUI_API bool                AppGraphTopoOrder(const ImGuiAppGraph* g, ImVector<int>* out_control_ids, char* err, int err_size, bool include_live = false);

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
  };
  IMGUI_API void                AppGraphSetHostCommands(const ImGuiAppGraph* g, const ImGuiAppGraphHostCmd* cmds, int count);
  IMGUI_API int                 AppGraphConsumeHostCommand(const ImGuiAppGraph* g);   // picked host cmd id since last call, or -1

  // One-shot: open the add-node palette at the canvas center on the editor's next submission (same
  // palette as RMB / Space / the + gizmo).
  IMGUI_API void                AppGraphRequestAddPalette(const ImGuiAppGraph* g);

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
    ImU32 KindDefault;
    // Layer-type accents
    ImU32 LayerTask;
    ImU32 LayerCommand;  // also marks hidden nodes
    ImU32 LayerStatus;
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

  // Ensure the four foundation layers (Window, Task, Command, Status) exist in g -- adds missing,
  // never duplicates. They are permanent (AppGraphRemoveNode refuses them). Call on new/empty graphs
  // and after load.
  IMGUI_API void                AppGraphEnsureFoundation(ImGuiAppGraph* g);

  // One-shot tidy layout: design nodes as a left-to-right containment tree, siblings stacked, parents
  // centered. L key inside the editor; also exposed for a toolbar. Layers keep their own column
  // packing; live-mirror nodes are left in place.
  IMGUI_API void                AppGraphAutoLayout(ImGuiAppGraph* g, bool show_live);
}
