// ImGuiAppLayer data-driven node tooling, on the canvas engine (imguiapp_canvas.h):
// model-unit geometry, native camera, same-frame measurement. The engine dependency stays confined
// to this translation unit; imguiapp_nodes.h only forward-declares the canvas state.
//
// Index of this file (search for "[SECTION]"):
// [SECTION] Blender-style field widgets (node body)
// [SECTION] Typed node graph: allocation, factory, lookup
// [SECTION] Layer column packing + default node placement
// [SECTION] Phase-coherent geometry cache
// [SECTION] Typed links: resolve / validate / capture
// [SECTION] Per-edge field bindings editor
// [SECTION] Hover sync (brushing across coordinated views) + cached validation
// [SECTION] Inspector (component sections, style/color descs, project + multi-select)
// [SECTION] Whole-graph editor render (canvas, decorations, gizmos, palette, keyboard)
// [SECTION] Scope interior (walls, lifecycle brackets, boundary portals, density altitude)
// [SECTION] Tidy tree layout (measured-size layered DAG)
// [SECTION] Topological order + whole-graph codegen
// [SECTION] Event expression checking (AppEventExprCheck)
// [SECTION] Whole-graph persistence (SaveAppGraph / LoadAppGraph, legacy [Draft] ingest)
// [SECTION] Round-trip: parse C++ struct blocks back into Struct nodes
// [SECTION] Undo / redo (in-memory serialized snapshots, named steps)
// [SECTION] Copy / paste (subtree clipboard with id remap)
// [SECTION] Prefabs (named reusable subtrees)
// [SECTION] Live mirror: reflect the running app's controls into the model
// [SECTION] Scene-hierarchy tree (ECS-style outliner)

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imguiapp_nodes.h"
#include "imguiapp_canvas.h"
#include "IconsFontAwesome6.h"             // Font Awesome glyphs for layer roles (font merged by the host app)

#include <stdio.h>                         // sscanf (graph text parse)
#include <stdlib.h>                        // atoi (graph text parse)
#include <string.h>                        // strncmp (graph text parse)

namespace ImGui
{
  ImGuiAppEditorState* AppGraphEditorState(const ImGuiAppGraph* g)
  {
    if (g->_Ed == nullptr)
      g->_Ed = IM_NEW(ImGuiAppEditorState)();
    return g->_Ed;
  }

  // Per-window widget state keys (ImGui state storage): the BL widgets' shared edit focus + drag
  // scratch, and the rename scaffold's Begin/End latch. Window-scoped like ImGui's own ActiveId.
  static const ImGuiID kAppKeyBlEditing   = 0x424C0001;
  static const ImGuiID kAppKeyBlFocus     = 0x424C0002;
  static const ImGuiID kAppKeyBlDragId    = 0x424C0003;
  static const ImGuiID kAppKeyBlAccum     = 0x424C0004;
  static const ImGuiID kAppKeyBlPressX    = 0x424C0005;
  static const ImGuiID kAppKeyBlDragged   = 0x424C0006;
  static const ImGuiID kAppKeyWrapEditing = 0x424C0007;
  static const ImGuiID kAppKeyWrapOwner   = 0x424C0008;
  static const ImGuiID kAppKeyWrapId      = 0x424C0009;

  void BeginAppNode(ImGuiCanvasState* c, int id, const char* title)
  {
    IM_ASSERT(c != nullptr && title != nullptr);

    ImGui::CanvasNextNodeTitle(c, title, 0);
    ImGui::CanvasBeginNode(c, id);
  }

  void EndAppNode(ImGuiCanvasState* c)
  {
    ImGui::CanvasEndNode(c);

    // The engine cleared its edit flag on deactivation (Enter, Escape, click-away): hand it back.
    ImGuiStorage* st = ImGui::GetStateStorage();
    int* owner = (int*)st->GetVoidPtr(kAppKeyWrapOwner);
    if (owner != nullptr && *owner == st->GetInt(kAppKeyWrapId, -1) && !st->GetBool(kAppKeyWrapEditing, false))
      *owner = -1;
    st->SetVoidPtr(kAppKeyWrapOwner, nullptr);
    st->SetInt(kAppKeyWrapId, -1);
  }

  void BeginAppNodeRenamable(ImGuiCanvasState* c, int id, char* name, int name_size, int* editing_node_id)
  {
    IM_ASSERT(c != nullptr && name != nullptr && editing_node_id != nullptr);

    // Title-band click enters rename. Hit-test last frame's geometry (model units x this frame's
    // camera); the band is the frame-height strip at the node's top edge, matching CanvasEndNode.
    if (*editing_node_id != id && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
      const ImVec2 mn = ImGui::CanvasToScreen(c, ImGui::CanvasNodePos(c, id));
      const ImVec2 sz = ImGui::CanvasNodeSize(c, id) * ImGui::CanvasGetScale(c);   // model -> screen: full scale
      const float  th = ImGui::GetFrameHeight() * ImGui::CanvasGetZoom(c);         // host font already carries DPI: logical zoom
      const ImVec2 m = ImGui::GetIO().MousePos;
      if (sz.x > 0.0f && m.x >= mn.x && m.x < mn.x + sz.x && m.y >= mn.y && m.y < mn.y + th)
        *editing_node_id = id;
    }

    ImGuiStorage* st = ImGui::GetStateStorage();
    bool* editing = st->GetBoolRef(kAppKeyWrapEditing, false);
    *editing = *editing_node_id == id;
    st->SetVoidPtr(kAppKeyWrapOwner, editing_node_id);
    st->SetInt(kAppKeyWrapId, id);
    ImGui::CanvasNextNodeTitleEditable(c, name, name_size, editing, 0);
    ImGui::CanvasBeginNode(c, id);
  }

  const char* AppFieldTypeName(ImGuiAppFieldType type)
  {
    switch (type)
    {
    case ImGuiAppFieldType_Float:  return "float";
    case ImGuiAppFieldType_Int:    return "int";
    case ImGuiAppFieldType_Bool:   return "bool";
    case ImGuiAppFieldType_Double: return "double";
    case ImGuiAppFieldType_Vec2:   return "ImVec2";
    case ImGuiAppFieldType_Vec4:   return "ImVec4";
    case ImGuiAppFieldType_String: return "char";
    case ImGuiAppFieldType_Struct: return "struct";
    default:                       return "float";
    }
  }

  void AppNodeDraftAddField(ImVector<ImGuiAppFieldDesc>* fields, const char* name, ImGuiAppFieldType type)
  {
    IM_ASSERT(fields != nullptr);

    ImGuiAppFieldDesc desc;
    ImStrncpy(desc.Name, (name && name[0]) ? name : "field", IM_ARRAYSIZE(desc.Name));
    desc.Type = type;
    fields->push_back(desc);
  }

  void AppNodeDraftRemoveField(ImVector<ImGuiAppFieldDesc>* fields, int index)
  {
    IM_ASSERT(fields != nullptr);

    if (index >= 0 && index < fields->Size)
      fields->erase(fields->Data + index);
  }

  // Frame-height square so it aligns with the adjacent InputText/Combo. Returns true on click.
  static bool AppRowDeleteButton(const char* str_id)
  {
    const float sz = ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemAllowOverlap();
    const bool clicked = ImGui::InvisibleButton(str_id, ImVec2(sz, sz));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 center = ImVec2(pos.x + sz * 0.5f, pos.y + sz * 0.5f);
    if (hovered || held)
      dl->AddCircleFilled(center, sz * 0.5f, ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered));

    const ImU32 cross = ImGui::GetColorU32(ImGuiCol_Text, (hovered || held) ? 1.0f : 0.55f);
    const float arm = sz * 0.22f;
    const float th = ImMax(1.0f, IM_ROUND(sz * 0.09f));
    dl->AddLine(ImVec2(center.x - arm, center.y - arm), ImVec2(center.x + arm, center.y + arm), cross, th);
    dl->AddLine(ImVec2(center.x + arm, center.y - arm), ImVec2(center.x - arm, center.y + arm), cross, th);
    return clicked;
  }

  static bool AppRowReorderButton(const char* str_id, bool up, bool enabled)
  {
    const float sz = ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemAllowOverlap();
    const bool clicked = ImGui::InvisibleButton(str_id, ImVec2(sz, sz)) && enabled;
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c = ImVec2(pos.x + sz * 0.5f, pos.y + sz * 0.5f);
    if (hovered || held)
      dl->AddCircleFilled(c, sz * 0.5f, ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered));

    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text, !enabled ? 0.28f : (hovered || held) ? 1.0f : 0.62f);
    const float a = sz * 0.20f;
    if (up)
      dl->AddTriangleFilled(ImVec2(c.x - a, c.y + a * 0.7f), ImVec2(c.x + a, c.y + a * 0.7f), ImVec2(c.x, c.y - a * 0.85f), col);
    else
      dl->AddTriangleFilled(ImVec2(c.x - a, c.y - a * 0.7f), ImVec2(c.x + a, c.y - a * 0.7f), ImVec2(c.x, c.y + a * 0.85f), col);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return clicked;
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Theme-derived colors
  // All chrome colors derive from the current ImGuiStyle per call (live theme switches follow).
  // Neutrals ride the WindowBg -> Text axis; semantic hues are pulled toward Text so they stay
  // legible on light themes.
  //-----------------------------------------------------------------------------

  static ImU32 AppThemeMix(ImVec4 a, ImVec4 b, float t, float alpha)
  {
    ImVec4 c = ImLerp(a, b, t);
    c.w = alpha;
    return ColorConvertFloat4ToU32(c);
  }

  static ImU32 AppThemeCol(ImVec4 c, float alpha)
  {
    c.w = alpha;
    return ColorConvertFloat4ToU32(c);
  }

  // t = 0 -> WindowBg, t = 1 -> Text.
  static ImU32 AppThemeNeutral(float t, float alpha = 1.0f)
  {
    return AppThemeMix(GetStyleColorVec4(ImGuiCol_WindowBg), GetStyleColorVec4(ImGuiCol_Text), t, alpha);
  }

  // Near-black on any theme: dark text/outlines over accent fills.
  static ImU32 AppThemeDark(float alpha = 1.0f)
  {
    return AppThemeMix(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), GetStyleColorVec4(ImGuiCol_WindowBg), 0.07f, alpha);
  }

  static ImU32 AppThemeAccent(ImVec4 hue, float alpha = 1.0f)
  {
    return AppThemeCol(ImLerp(hue, GetStyleColorVec4(ImGuiCol_Text), 0.15f), alpha);
  }

  // Semantic accent hues (final color via AppThemeAccent).
  static const ImVec4 kAppHueTask       = ImVec4(0.43f, 0.59f, 0.80f, 1.0f);   // blue: logic
  static const ImVec4 kAppHueCommand    = ImVec4(0.82f, 0.59f, 0.35f, 1.0f);   // amber: commands + hidden markers
  static const ImVec4 kAppHueStatus     = ImVec4(0.55f, 0.73f, 0.47f, 1.0f);   // green: status
  static const ImVec4 kAppHueLayout     = ImVec4(0.42f, 0.72f, 0.72f, 1.0f);   // teal: workspace layout
  static const ImVec4 kAppHueDisplayLayer= ImVec4(0.65f, 0.53f, 0.82f, 1.0f);   // violet: windows
  static const ImVec4 kAppHueWindow     = ImVec4(0.47f, 0.67f, 0.90f, 1.0f);   // window kind + data pins
  static const ImVec4 kAppHueSidebar    = ImVec4(0.47f, 0.78f, 0.78f, 1.0f);
  static const ImVec4 kAppHueControl    = ImVec4(0.84f, 0.65f, 0.37f, 1.0f);
  static const ImVec4 kAppHueStruct     = ImVec4(0.67f, 0.57f, 0.86f, 1.0f);
  static const ImVec4 kAppHueField      = ImVec4(0.55f, 0.80f, 0.55f, 1.0f);
  static const ImVec4 kAppHuePinChild   = ImVec4(0.90f, 0.67f, 0.43f, 1.0f);   // containment pins
  static const ImVec4 kAppHuePinTie     = ImVec4(0.55f, 0.78f, 0.55f, 1.0f);   // tie pins
  static const ImVec4 kAppHueSevError   = ImVec4(0.88f, 0.36f, 0.32f, 1.0f);
  static const ImVec4 kAppHueSevWarn    = ImVec4(0.87f, 0.66f, 0.24f, 1.0f);
  static const ImVec4 kAppHueErrorText  = ImVec4(0.92f, 0.43f, 0.37f, 1.0f);
  static const ImVec4 kAppHueDanger     = ImVec4(0.83f, 0.43f, 0.43f, 1.0f);   // destructive row icons
  static const ImVec4 kAppHueLive       = ImVec4(0.35f, 0.47f, 0.65f, 1.0f);   // steel-blue: read-only live mirror
  static const ImVec4 kAppHuePromoted   = ImVec4(0.31f, 0.59f, 0.35f, 1.0f);   // green: design matches a live type
  static const ImVec4 kAppHueDotLive    = ImVec4(0.35f, 0.59f, 0.90f, 1.0f);   // diff dots: running mirror
  static const ImVec4 kAppHueDotPromoted= ImVec4(0.43f, 0.78f, 0.47f, 1.0f);   // diff dots: promoted
  static const ImVec4 kAppHueDotDrift   = ImVec4(0.88f, 0.71f, 0.35f, 1.0f);   // diff dots: design-only drift
  static const ImVec4 kAppHueGold       = ImVec4(0.86f, 0.67f, 0.35f, 1.0f);   // overlay accents (gizmos, hint panels)

  // Alpha override on a packed style color; rgb kept.
  static ImU32 AppColWithAlpha(ImU32 col, float alpha)
  {
    return (col & 0x00FFFFFF) | ((ImU32)(ImClamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f) << 24);
  }

  // Derived theme cache, one per process behind an accessor -- the same idiom as the type-schema
  // registry (imguiapp.cpp AppTypeSchemas). Input = the process-global ImGuiStyle; the version
  // gates re-derives (0 = not yet derived; bumped by each global re-derive).
  static ImGuiAppComposerStyle* AppComposerStyleStore()
  {
    static ImGuiAppComposerStyle style;
    return &style;
  }
  static int* AppComposerStyleVersion()
  {
    static int version = 0;
    return &version;
  }

  void AppComposerStyleFromTheme(ImGuiAppComposerStyle* style)
  {
    IM_ASSERT(GetCurrentContext() != nullptr && "AppComposerStyleFromTheme reads the current ImGuiStyle");
    style->KindLayer      = AppThemeNeutral(0.58f);
    style->KindWindow     = AppThemeAccent(kAppHueWindow);
    style->KindSidebar    = AppThemeAccent(kAppHueSidebar);
    style->KindControl    = AppThemeAccent(kAppHueControl);
    style->KindStruct     = AppThemeAccent(kAppHueStruct);
    style->KindField      = AppThemeAccent(kAppHueField);
    style->KindDefault    = AppThemeNeutral(0.79f);
    style->LayerTask      = AppThemeAccent(kAppHueTask);
    style->LayerCommand   = AppThemeAccent(kAppHueCommand);
    style->LayerStatus    = AppThemeAccent(kAppHueStatus);
    style->LayerLayout    = AppThemeAccent(kAppHueLayout);
    style->LayerDisplay   = AppThemeAccent(kAppHueDisplayLayer);
    style->AccentNeutral  = AppThemeNeutral(0.63f);
    style->PinData        = AppThemeAccent(kAppHueWindow);
    style->PinChild       = AppThemeAccent(kAppHuePinChild);
    style->PinTie         = AppThemeAccent(kAppHuePinTie);
    style->PinDefault     = AppThemeNeutral(0.60f);
    style->SevError       = AppThemeAccent(kAppHueSevError);
    style->SevWarn        = AppThemeAccent(kAppHueSevWarn);
    style->ErrorText      = AppThemeAccent(kAppHueErrorText);
    style->Danger         = AppThemeAccent(kAppHueDanger);
    style->OriginLive     = AppThemeAccent(kAppHueLive);
    style->OriginPromoted = AppThemeAccent(kAppHuePromoted);
    style->DotLive        = AppThemeAccent(kAppHueDotLive);
    style->DotPromoted    = AppThemeAccent(kAppHueDotPromoted);
    style->DotDrift       = AppThemeAccent(kAppHueDotDrift);
    style->Gold           = AppThemeAccent(kAppHueGold);
    style->FieldBg        = AppThemeNeutral(0.30f);
    style->FieldBgHovered = AppThemeNeutral(0.40f);
    style->FieldBgEdit    = AppThemeNeutral(0.13f);
    style->FieldBorder    = AppThemeCol(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), 0.235f);
    style->FieldText      = AppThemeNeutral(0.89f);
    style->TextMuted      = AppThemeNeutral(0.60f);
    style->TextOnAccent   = AppThemeDark();
    style->DarkOutline    = AppThemeDark(0.86f);
    style->GroupFill      = AppThemeNeutral(0.28f, 0.14f);
    style->GroupOutline   = AppThemeNeutral(0.68f, 0.55f);
    style->GroupTitleBg   = AppThemeNeutral(0.09f);
    style->RailLine       = AppThemeNeutral(0.58f, 0.55f);
    if (style == AppComposerStyleStore())
      (*AppComposerStyleVersion())++;
  }

  ImGuiAppComposerStyle* AppComposerGetStyle()
  {
    if ((*AppComposerStyleVersion()) == 0)
      AppComposerStyleFromTheme(AppComposerStyleStore());
    return AppComposerStyleStore();
  }

  // F38 motion table: scalar constants, defaulted in the struct. One process-level table (like the
  // color style) so every overlay reads the same ladder instead of hard-coding an alpha.
  ImGuiAppComposerMotion* AppComposerGetMotion()
  {
    static ImGuiAppComposerMotion motion;
    return &motion;
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Blender-style field widgets (node body)
  // Text: click to edit in place. Enum: hover step arrows + dropdown. Int: drag to scrub (Shift = fine),
  // click to type, arrows to step.
  //-----------------------------------------------------------------------------
  namespace
  {
    ImU32 AppBlFill()      { return AppComposerGetStyle()->FieldBg; }
    ImU32 AppBlFillHover() { return AppComposerGetStyle()->FieldBgHovered; }
    ImU32 AppBlFillEdit()  { return AppComposerGetStyle()->FieldBgEdit; }
    ImU32 AppBlBorder()    { return AppComposerGetStyle()->FieldBorder; }
    ImU32 AppBlText()      { return AppComposerGetStyle()->FieldText; }

    // Applied as GetFrameHeight * kBlRounding, materialized per scope.
    const float kBlRounding = 0.28f;
  }

  // Read-write: the project inspector's Theme section edits these live. Seeded from the composer
  // style; a global AppComposerStyleFromTheme re-derive reseeds it (inspector edits reset).
  ImGuiAppChromeTheme* AppGraphChromeTheme()
  {
    static ImGuiAppChromeTheme t;
    static int seeded_version = -1;
    const ImGuiAppComposerStyle* s = AppComposerGetStyle();
    if (seeded_version != (*AppComposerStyleVersion()))
    {
      const ImGuiAppColorModDesc combo[] =
      {   // dropdown fields (AppBlEnum, struct picker): field + popup + rows
        { ImGuiCol_FrameBg,        s->FieldBg },
        { ImGuiCol_FrameBgHovered, s->FieldBgHovered },
        { ImGuiCol_FrameBgActive,  s->FieldBgHovered },
        { ImGuiCol_Text,           s->FieldText },
        { ImGuiCol_PopupBg,        s->FieldBgEdit },
        { ImGuiCol_Header,         s->FieldBgHovered },
        { ImGuiCol_HeaderHovered,  s->FieldBgHovered },
        { ImGuiCol_Border,         s->FieldBorder },
      };
      const ImGuiAppColorModDesc edit[] =
      {   // in-place editors (InputText/InputInt): transparent frame over the self-drawn bg
        { ImGuiCol_FrameBg,        0 },
        { ImGuiCol_FrameBgHovered, 0 },
        { ImGuiCol_FrameBgActive,  0 },
        { ImGuiCol_Text,           s->FieldText },
      };
      memcpy(t.Combo, combo, sizeof(combo));
      memcpy(t.Edit, edit, sizeof(edit));
      seeded_version = (*AppComposerStyleVersion());
    }
    return &t;
  }

  namespace
  {

  }

  // Returns the pushed counts; pop with AppBlPopStyle. The PopupRounding desc's Active flag gates the
  // popup half of the scope.
  struct AppBlStyleScope { int Colors; int Vars; };
  static AppBlStyleScope AppBlPushStyle(const ImGuiAppColorModDesc* cols, int ncols, bool with_popup)
  {
    const float r = ImGui::GetFrameHeight() * kBlRounding;
    const ImGuiAppStyleModDesc vars[] =
    {
      { ImGuiStyleVar_FrameRounding, ImVec2(r, 0.0f) },
      { ImGuiStyleVar_PopupRounding, ImVec2(r, 0.0f), with_popup },
    };
    AppBlStyleScope s;
    s.Colors = ImGui::PushAppColorMods(cols, ncols);
    s.Vars = ImGui::PushAppStyleMods(vars, IM_ARRAYSIZE(vars));
    return s;
  }

  static void AppBlPopStyle(const AppBlStyleScope& s)
  {
    if (s.Vars > 0)
      ImGui::PopStyleVar(s.Vars);
    if (s.Colors > 0)
      ImGui::PopStyleColor(s.Colors);
  }

  static bool AppRowKebabButton(const char* str_id)
  {
    const float sz = ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemAllowOverlap();
    const bool clicked = ImGui::InvisibleButton(str_id, ImVec2(sz, sz));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(pos.x + sz * 0.5f, pos.y + sz * 0.5f);
    if (hovered || held)
      dl->AddCircleFilled(c, sz * 0.5f, ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered));
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text, (hovered || held) ? 1.0f : 0.55f);
    const float r = ImMax(1.0f, sz * 0.055f);
    const float step = sz * 0.20f;
    for (int i = -1; i <= 1; i++)
      dl->AddCircleFilled(ImVec2(c.x, c.y + step * (float)i), r, col);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return clicked;
  }

  // Inspector section header. Optional enable checkbox and kebab (pass null to omit); the caller answers
  // the kebab click with its own popup. Open state lives in the window's state storage (session-lived,
  // shared per panel). The first section submitted in a window each frame defaults open, the rest default
  // collapsed; a user toggle overrides the default either way. Returns true while open.
  bool AppInspectorSection(const char* str_id, const char* icon, const char* label, bool* enabled, bool* kebab_clicked)
  {
    const float h = ImGui::GetFrameHeight();
    const float em = ImGui::GetFontSize();
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID first_id = ImHashStr("AppInspectorSectionFirstFrame");   // window-wide: deliberately not seeded by the ID stack
    const int frame = ImGui::GetFrameCount();
    const bool is_first = st->GetInt(first_id, -1) != frame;
    st->SetInt(first_id, frame);
    const ImGuiID open_id = ImGui::GetID(str_id);
    bool open = st->GetInt(open_id, is_first ? 1 : 0) != 0;

    const float avail = ImGui::GetContentRegionAvail().x;
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx(mn.x + avail, mn.y + h);

    ImGui::PushID(str_id);
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InvisibleButton("##hdr", ImVec2(avail, h)))
    {
      open = !open;
      st->SetInt(open_id, open ? 1 : 0);
    }
    const bool hov = ImGui::IsItemHovered();
    const ImVec2 after = ImGui::GetCursorScreenPos();   // restored below: the right-cluster items move the cursor

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(mn, mx, hov ? AppBlFillHover() : AppBlFill(), h * 0.22f);
    const ImVec2 tc(mn.x + em * 0.55f, (mn.y + mx.y) * 0.5f);
    const float a = em * 0.24f;
    const ImU32 tri = ImGui::GetColorU32(ImGuiCol_Text, 0.7f);
    if (open)
      dl->AddTriangleFilled(ImVec2(tc.x - a, tc.y - a * 0.55f), ImVec2(tc.x + a, tc.y - a * 0.55f), ImVec2(tc.x, tc.y + a * 0.8f), tri);
    else
      dl->AddTriangleFilled(ImVec2(tc.x - a * 0.55f, tc.y - a), ImVec2(tc.x - a * 0.55f, tc.y + a), ImVec2(tc.x + a * 0.8f, tc.y), tri);
    char text[96];
    ImFormatString(text, IM_ARRAYSIZE(text), "%s%s%s", icon ? icon : "", icon ? "  " : "", label);
    dl->AddText(ImVec2(mn.x + em * 1.3f, mn.y + (h - ImGui::GetTextLineHeight()) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), text);

    // Right cluster, rightmost first; real items submitted after the header button, so they win its overlap.
    float rx = mx.x;
    if (kebab_clicked != nullptr)
    {
      rx -= h;
      ImGui::SetCursorScreenPos(ImVec2(rx, mn.y));
      *kebab_clicked = AppRowKebabButton("##kebab");
    }
    if (enabled != nullptr)
    {
      rx -= h + em * 0.15f;
      ImGui::SetCursorScreenPos(ImVec2(rx, mn.y));
      ImGui::Checkbox("##sec_on", enabled);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("enable/disable this whole section");
    }
    // Restores the flow position captured after the header item. Written directly (not SetCursorScreenPos)
    // so a collapsed section as the window's last item doesn't trip the extend-parent-boundaries error check.
    ImGui::GetCurrentWindow()->DC.CursorPos = after;
    ImGui::PopID();
    if (open)
      ImGui::Spacing();
    return open;
  }

  // Rounded fill + faint outline; returns the rounding so an in-place editor can match it.
  static float AppBlFieldBg(ImDrawList* dl, ImVec2 mn, ImVec2 mx, bool hovered, bool editing)
  {
    const float r = (mx.y - mn.y) * 0.28f;
    dl->AddRectFilled(mn, mx, editing ? AppBlFillEdit() : (hovered ? AppBlFillHover() : AppBlFill()), r);
    dl->AddRect(mn, mx, AppBlBorder(), r);
    return r;
  }

  static bool AppBlAddPill(const char* str_id, const char* label)
  {
    const float h = ImGui::GetFrameHeight();
    const float em = ImGui::GetFontSize();
    const float pad = em * 0.45f;
    const float plus_w = em * 0.85f;
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float w = pad + plus_w + ts.x + pad;
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx(mn.x + w, mn.y + h);

    ImGui::SetNextItemAllowOverlap();
    const bool clicked = ImGui::InvisibleButton(str_id, ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = h * 0.28f;
    dl->AddRectFilled(mn, mx, held ? AppBlFillEdit() : hovered ? AppBlFillHover() : AppBlFill(), r);
    dl->AddRect(mn, mx, AppBlBorder(), r);

    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text, hovered || held ? 1.0f : 0.8f);
    const ImVec2 pc(mn.x + pad + plus_w * 0.5f, (mn.y + mx.y) * 0.5f);
    const float a = em * 0.28f;
    const float th = ImMax(1.0f, IM_ROUND(em * 0.09f));
    dl->AddLine(ImVec2(pc.x - a, pc.y), ImVec2(pc.x + a, pc.y), col, th);
    dl->AddLine(ImVec2(pc.x, pc.y - a), ImVec2(pc.x, pc.y + a), col, th);
    dl->AddText(ImVec2(mn.x + pad + plus_w, mn.y + (h - ts.y) * 0.5f), col, label);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return clicked;
  }

  // True if the mouse is over [mn,mx] within the current window. Manual hit-test so the flat
  // buttons cannot be blocked by an overlapping ImGui item.
  static bool AppPtInRectHovered(ImVec2 mn, ImVec2 mx)
  {
    // AllowWhenBlockedByActiveItem: a press makes the item under the cursor active before this runs, and
    // plain IsWindowHovered() reports false while any item is active -- exactly the click frame. Stray
    // clicks can't leak in: IsMouseClicked is only true on the press frame itself.
    const ImVec2 m = ImGui::GetIO().MousePos;
    return ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && m.x >= mn.x && m.x < mx.x && m.y >= mn.y && m.y < mx.y;
  }

  static bool AppBlToggleButton(const char* str_id, const char* label, bool on, ImU32 accent)
  {
    IM_UNUSED(str_id);
    const float sz = ImGui::GetFrameHeight();
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx(mn.x + sz, mn.y + sz);
    ImGui::Dummy(ImVec2(sz, sz));   // reserve layout only -- interaction is a manual hit-test below
    const bool hovered = AppPtInRectHovered(mn, mx);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = sz * 0.22f;
    ImU32 fill = on ? accent : AppBlFill();
    if (hovered)
      fill = on ? accent : AppBlFillHover();
    dl->AddRectFilled(mn, mx, fill, r);
    dl->AddRect(mn, mx, on ? (accent & 0x00FFFFFF) | 0xFF000000 : AppBlBorder(), r);

    const ImU32 tc = on ? AppComposerGetStyle()->TextOnAccent : ImGui::GetColorU32(ImGuiCol_Text, hovered ? 0.9f : 0.55f);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(mn.x + (sz - ts.x) * 0.5f, mn.y + (sz - ts.y) * 0.5f), tc, label);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  }

  static bool AppBlFilterButton(const char* str_id, const char* icon, int count, bool on, ImU32 accent)
  {
    IM_UNUSED(str_id);
    const float em = ImGui::GetFontSize();
    const float h = ImGui::GetFrameHeight();
    char lbl[24];
    ImFormatString(lbl, IM_ARRAYSIZE(lbl), "%s %d", icon, count);
    const ImVec2 ts = ImGui::CalcTextSize(lbl);
    const float w = ts.x + em * 0.7f;
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx(mn.x + w, mn.y + h);
    ImGui::Dummy(ImVec2(w, h));   // reserve layout only -- interaction is a manual hit-test below
    const bool hov = AppPtInRectHovered(mn, mx);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = h * 0.24f;
    dl->AddRectFilled(mn, mx, on ? accent : (hov ? AppBlFillHover() : AppBlFill()), r);
    dl->AddRect(mn, mx, on ? (accent & 0x00FFFFFF) | 0xFF000000 : AppBlBorder(), r);
    const float dim = (count == 0 && !on) ? 0.4f : 1.0f;
    const ImU32 tc = on ? AppComposerGetStyle()->TextOnAccent : ImGui::GetColorU32(ImGuiCol_Text, dim * (hov ? 1.0f : 0.85f));
    dl->AddText(ImVec2(mn.x + em * 0.35f, mn.y + (h - ts.y) * 0.5f), tc, lbl);
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  }

  static void AppBlText(ImDrawList* dl, ImVec2 mn, ImVec2 mx, const char* text, bool centered)
  {
    const float pad = (mx.y - mn.y) * 0.30f;
    const ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 tp(centered ? mn.x + ((mx.x - mn.x) - ts.x) * 0.5f : mn.x + pad,
              mn.y + ((mx.y - mn.y) - ts.y) * 0.5f);
    if (tp.x < mn.x + pad) tp.x = mn.x + pad;
    dl->PushClipRect(mn, mx, true);
    dl->AddText(tp, AppBlText(), text);
    dl->PopClipRect();
  }

  static void AppBlStepArrows(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float arrow_w)
  {
    const float cy = (mn.y + mx.y) * 0.5f;
    const float a = (mx.y - mn.y) * 0.16f;
    const float lx = mn.x + arrow_w * 0.5f, rx = mx.x - arrow_w * 0.5f;
    dl->AddTriangleFilled(ImVec2(lx + a, cy - a), ImVec2(lx + a, cy + a), ImVec2(lx - a, cy), AppBlText());
    dl->AddTriangleFilled(ImVec2(rx - a, cy - a), ImVec2(rx - a, cy + a), ImVec2(rx + a, cy), AppBlText());
  }

  static bool AppBlDisclosure(const char* str_id, bool expanded)
  {
    const float sz = ImGui::GetFrameHeight() * 0.8f;
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton(str_id, ImVec2(sz, sz));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemActivated();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = hovered ? AppBlText() : AppComposerGetStyle()->TextMuted;
    const ImVec2 c(mn.x + sz * 0.5f, mn.y + sz * 0.5f);
    const float a = sz * 0.26f;
    if (expanded)
      dl->AddTriangleFilled(ImVec2(c.x - a, c.y - a * 0.55f), ImVec2(c.x + a, c.y - a * 0.55f), ImVec2(c.x, c.y + a * 0.8f), col);
    else
      dl->AddTriangleFilled(ImVec2(c.x - a * 0.55f, c.y - a), ImVec2(c.x - a * 0.55f, c.y + a), ImVec2(c.x + a * 0.8f, c.y), col);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return clicked;
  }

  static bool AppBlInputText(const char* str_id, char* buf, size_t buf_size, float width)
  {
    const float h = ImGui::GetFrameHeight();
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx(mn.x + width, mn.y + h);
    const ImGuiID id = ImGui::GetID(str_id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiStorage* st = ImGui::GetStateStorage();
    bool changed = false;

    if ((ImGuiID)st->GetInt(kAppKeyBlEditing, 0) == id)
    {
      AppBlFieldBg(dl, mn, mx, true, true);
      ImGui::SetCursorScreenPos(mn);
      ImGui::SetNextItemWidth(width);
      const AppBlStyleScope sc = AppBlPushStyle(AppGraphChromeTheme()->Edit, IM_ARRAYSIZE(AppGraphChromeTheme()->Edit), false);
      if (st->GetBool(kAppKeyBlFocus, false)) { ImGui::SetKeyboardFocusHere(); st->SetBool(kAppKeyBlFocus, false); }
      ImGui::PushID(id);
      changed = ImGui::InputText("##e", buf, buf_size, ImGuiInputTextFlags_AutoSelectAll);
      const bool done = ImGui::IsItemDeactivated();
      ImGui::PopID();
      AppBlPopStyle(sc);
      if (done) st->SetInt(kAppKeyBlEditing, 0);
    }
    else
    {
      ImGui::SetNextItemAllowOverlap();
      ImGui::InvisibleButton(str_id, ImVec2(width, h));
      const bool hovered = ImGui::IsItemHovered();
      AppBlFieldBg(dl, mn, mx, hovered, false);
      AppBlText(dl, mn, mx, buf, false);
      if (ImGui::IsItemActivated()) { st->SetInt(kAppKeyBlEditing, (int)id); st->SetBool(kAppKeyBlFocus, true); }
      if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    }
    return changed;
  }

  static bool AppBlEnum(const char* str_id, float width, int* v, const char* (*name_of)(int), int count)
  {
    // Built on ImGui::BeginCombo, not a raw OpenPopup: a popup opened inside a canvas node mis-anchors
    // and lets the click fall through to the node/window drag.
    bool changed = false;

    const AppBlStyleScope sc = AppBlPushStyle(AppGraphChromeTheme()->Combo, IM_ARRAYSIZE(AppGraphChromeTheme()->Combo), true);

    ImGui::SetNextItemWidth(width);
    ImGui::PushID(str_id);
    if (ImGui::BeginCombo("##bl", (*v >= 0 && *v < count) ? name_of(*v) : "", ImGuiComboFlags_NoArrowButton))
    {
      for (int i = 0; i < count; i++)
        if (ImGui::Selectable(name_of(i), i == *v)) { *v = i; changed = true; }
      ImGui::EndCombo();
    }
    // Wheel over the closed combo steps the value.
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f && count > 0 && ImGui::IsItemHovered())
    {
      *v = ((*v >= 0 ? *v : 0) + (wheel > 0.0f ? 1 : count - 1)) % count;
      changed = true;
    }
    ImGui::PopID();

    AppBlPopStyle(sc);
    return changed;
  }

  static bool AppBlDragInt(const char* str_id, float width, int* v, int vmin, int vmax)
  {
    const float h = ImGui::GetFrameHeight();
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx(mn.x + width, mn.y + h);
    const ImGuiID id = ImGui::GetID(str_id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiStorage* st = ImGui::GetStateStorage();
    bool changed = false;

    if ((ImGuiID)st->GetInt(kAppKeyBlEditing, 0) == id)
    {
      AppBlFieldBg(dl, mn, mx, true, true);
      ImGui::SetCursorScreenPos(mn);
      ImGui::SetNextItemWidth(width);
      const AppBlStyleScope sc = AppBlPushStyle(AppGraphChromeTheme()->Edit, IM_ARRAYSIZE(AppGraphChromeTheme()->Edit), false);
      if (st->GetBool(kAppKeyBlFocus, false)) { ImGui::SetKeyboardFocusHere(); st->SetBool(kAppKeyBlFocus, false); }
      ImGui::PushID(id);
      changed = ImGui::InputInt("##e", v, 0, 0, ImGuiInputTextFlags_AutoSelectAll);
      const bool done = ImGui::IsItemDeactivated();
      ImGui::PopID();
      AppBlPopStyle(sc);
      if (done) st->SetInt(kAppKeyBlEditing, 0);
      *v = ImClamp(*v, vmin, vmax);
      return changed;
    }

    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton(str_id, ImVec2(width, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    AppBlFieldBg(dl, mn, mx, hovered || active, false);
    const float arrow_w = h * 0.7f;

    if (ImGui::IsItemActivated())
    {
      st->SetInt(kAppKeyBlDragId, (int)id);
      st->SetFloat(kAppKeyBlAccum, 0.0f);
      st->SetBool(kAppKeyBlDragged, false);
      st->SetFloat(kAppKeyBlPressX, ImGui::GetIO().MousePos.x - mn.x);
    }
    if (active && (ImGuiID)st->GetInt(kAppKeyBlDragId, 0) == id)
    {
      const float dx = ImGui::GetIO().MouseDelta.x;
      const float press_x = st->GetFloat(kAppKeyBlPressX, 0.0f);
      if (!st->GetBool(kAppKeyBlDragged, false) && ImAbs(ImGui::GetIO().MousePos.x - mn.x - press_x) > ImGui::GetFontSize() * 0.1875f)
        st->SetBool(kAppKeyBlDragged, true);
      if (st->GetBool(kAppKeyBlDragged, false) && dx != 0.0f)
      {
        float accum = st->GetFloat(kAppKeyBlAccum, 0.0f) + dx * (ImGui::GetIO().KeyShift ? 0.05f : 0.25f);
        const int d = (int)accum;
        if (d != 0) { *v = ImClamp(*v + d, vmin, vmax); accum -= d; changed = true; }
        st->SetFloat(kAppKeyBlAccum, accum);
      }
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemDeactivated() && (ImGuiID)st->GetInt(kAppKeyBlDragId, 0) == id)
    {
      if (!st->GetBool(kAppKeyBlDragged, false))
      {
        const float press_x = st->GetFloat(kAppKeyBlPressX, 0.0f);
        if (press_x < arrow_w)              { *v = ImClamp(*v - 1, vmin, vmax); changed = true; }
        else if (press_x > width - arrow_w) { *v = ImClamp(*v + 1, vmin, vmax); changed = true; }
        else                                { st->SetInt(kAppKeyBlEditing, (int)id); st->SetBool(kAppKeyBlFocus, true); }
      }
      st->SetInt(kAppKeyBlDragId, 0);
    }

    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f && hovered && !active)
    {
      *v = ImClamp(*v + (int)wheel * (ImGui::GetIO().KeyShift ? 10 : 1), vmin, vmax);
      changed = true;
    }
    if (hovered && !active) { AppBlStepArrows(dl, mn, mx, arrow_w); ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); }
    char lbl[32]; ImFormatString(lbl, IM_ARRAYSIZE(lbl), "%d", *v);
    AppBlText(dl, mn, mx, lbl, true);
    return changed;
  }

  // Caller positions the cursor; name is edited elsewhere. `g` populates the struct picker.
  static void EditAppFieldTypeControls(ImGuiAppFieldDesc* f, float type_w, const ImGuiAppGraph* g)
  {
    int t = (int)f->Type;
    if (AppBlEnum("##type", type_w, &t, &AppFieldTypeName, ImGuiAppFieldType_COUNT))
    {
      f->Type = (ImGuiAppFieldType)t;
    }
    if (f->Type == ImGuiAppFieldType_String)
    {
      if (f->ArraySize <= 0)
      {
        f->ArraySize = 128;
      }
      ImGui::SameLine();
      AppBlDragInt("##size", ImGui::GetFontSize() * 4.0f, &f->ArraySize, 1, 65536);
    }
    else if (f->Type == ImGuiAppFieldType_Struct)
    {
      ImGui::SameLine();
      const AppBlStyleScope sc = AppBlPushStyle(AppGraphChromeTheme()->Combo, IM_ARRAYSIZE(AppGraphChromeTheme()->Combo), true);
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
      if (ImGui::BeginCombo("##stype", f->StructType[0] ? f->StructType : "<struct>", ImGuiComboFlags_NoArrowButton))
      {
        if (g != nullptr)
        {
          for (int si = 0; si < g->Nodes.Size; si++)
          {
            const ImGuiAppNode* sn = &g->Nodes.Data[si];
            if (sn->Kind != ImGuiAppNodeKind_Struct || sn->IsLive)
            {
              continue;
            }
            if (ImGui::Selectable(sn->Draft.Name, strcmp(sn->Draft.Name, f->StructType) == 0))
            {
              ImStrncpy(f->StructType, sn->Draft.Name, IM_ARRAYSIZE(f->StructType));
            }
          }
        }
        ImGui::EndCombo();
      }
      AppBlPopStyle(sc);
    }
  }

  // `g` (optional) populates the struct-type picker with the graph's Struct node names.
  static void EditAppFieldList(const char* list_label, ImVector<ImGuiAppFieldDesc>* fields, const ImGuiAppGraph* g = nullptr)
  {
    // TextDisabled (not SeparatorText) as the section label: a separator fills the content-region
    // width, which would blow up the node when this editor is hosted inside a canvas node.
    ImGui::PushID(list_label);
    ImGui::TextDisabled("%s", list_label);

    for (int i = 0; i < fields->Size; i++)
    {
      ImGuiAppFieldDesc* f = &fields->Data[i];
      ImGui::PushID(i);

      const float em = ImGui::GetFontSize();
      AppBlInputText("##name", f->Name, IM_ARRAYSIZE(f->Name), em * 8.0f);

      ImGui::SameLine();
      EditAppFieldTypeControls(f, em * 5.0f, g);

      // Member order == generated struct layout.
      ImGui::SameLine();
      if (AppRowReorderButton("##up", true, i > 0))
      {
        const ImGuiAppFieldDesc tmp = fields->Data[i - 1];
        fields->Data[i - 1] = fields->Data[i];
        fields->Data[i] = tmp;
      }
      ImGui::SameLine(0.0f, ImGui::GetFontSize() * 0.125f);
      if (AppRowReorderButton("##down", false, i < fields->Size - 1))
      {
        const ImGuiAppFieldDesc tmp = fields->Data[i + 1];
        fields->Data[i + 1] = fields->Data[i];
        fields->Data[i] = tmp;
      }

      ImGui::SameLine();
      if (AppRowDeleteButton("X"))   // id doubles as the test handle for the row-delete affordance
      {
        AppNodeDraftRemoveField(fields, i);
        ImGui::PopID();
        i--;
        continue;
      }

      ImGui::PopID();
    }

    if (AppBlAddPill("Add field", "Add field"))
      AppNodeDraftAddField(fields, "field", ImGuiAppFieldType_Float);

    ImGui::PopID();
  }

  void EditAppNodeDraftFields(ImGuiAppNodeDraft* draft)
  {
    IM_ASSERT(draft != nullptr);

    EditAppFieldList("Persist", &draft->PersistFields);
    EditAppFieldList("Temp", &draft->TempFields);
  }

  void EditAppNodeDraft(ImGuiAppNodeDraft* draft)
  {
    IM_ASSERT(draft != nullptr);

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::InputText("Name", draft->Name, IM_ARRAYSIZE(draft->Name));

    EditAppNodeDraftFields(draft);
  }

  void DrawAppNodeDraft(const ImGuiAppNodeDraft* draft)
  {
    IM_ASSERT(draft != nullptr);

    // The canvas positions the cursor at the node content origin after the title; the body must
    // submit at least one item or EndNode trips imgui's "SetCursorPos extends boundaries" assert.
    // A field-less draft therefore still emits a placeholder row.
    if (draft->PersistFields.Size == 0 && draft->TempFields.Size == 0)
    {
      ImGui::TextDisabled("(no fields)");
      return;
    }

    for (int i = 0; i < draft->PersistFields.Size; i++)
    {
      const ImGuiAppFieldDesc* f = &draft->PersistFields.Data[i];
      ImGui::Text("%s: %s", f->Name, AppFieldTypeName(f->Type));
    }
    for (int i = 0; i < draft->TempFields.Size; i++)
    {
      const ImGuiAppFieldDesc* f = &draft->TempFields.Data[i];
      ImGui::Text("%s: %s (temp)", f->Name, AppFieldTypeName(f->Type));
    }
  }

  // Optional-dependency wires keep the style hue at reduced alpha, so soft wiring reads as tentative.
  static ImU32 AppSoftWireColor(ImGuiCanvasState* c)
  {
    const ImU32 base = ImGui::CanvasGetStyle(c)->Wire;
    return (base & ~IM_COL32_A_MASK) | ((ImU32)(((base >> IM_COL32_A_SHIFT) & 0xFF) * 42 / 100) << IM_COL32_A_SHIFT);
  }

  void DrawAppNodeLinks(ImGuiCanvasState* c, const ImVector<ImGuiAppNodeLink>* links)
  {
    IM_ASSERT(c != nullptr && links != nullptr);

    for (int i = 0; i < links->Size; i++)
    {
      if (links->Data[i].Soft)
        ImGui::CanvasNextWireDashed(c);
      ImGui::CanvasWire(c, links->Data[i].Id, links->Data[i].StartAttr, links->Data[i].EndAttr,
                        links->Data[i].Soft ? AppSoftWireColor(c) : 0);
    }
  }

  bool CaptureAppNodeLinks(ImGuiCanvasState* c, ImVector<ImGuiAppNodeLink>* links, int* next_link_id)
  {
    IM_ASSERT(c != nullptr && links != nullptr && next_link_id != nullptr);

    bool changed = false;

    ImGuiAppNodeLink created;
    if (ImGui::CanvasWireCreated(c, &created.StartAttr, &created.EndAttr))
    {
      created.Id = (*next_link_id)++;
      links->push_back(created);
      changed = true;
    }

    // Endpoint dragged off a pin: the wire dies at grab time (releasing on a pin re-creates above).
    int detached_wire = 0;
    int detached_grab = 0;
    if (ImGui::CanvasWireDetached(c, &detached_wire, &detached_grab))
    {
      for (int i = 0; i < links->Size; i++)
        if (links->Data[i].Id == detached_wire)
        {
          links->erase(links->Data + i);
          changed = true;
          break;
        }
    }

    return changed;
  }

  // Parse one "name,typeint,arraysize" field record into fields.
  static void AppNodeParseField(ImVector<ImGuiAppFieldDesc>* fields, const char* line)
  {
    ImGuiAppFieldDesc f;
    int type = ImGuiAppFieldType_Float;
    int array_size = f.ArraySize;
    char struct_type[256] = "";
    const int got = sscanf(line, "%255[^,],%d,%d,%255[^\n]", f.Name, &type, &array_size, struct_type);
    if (got >= 1)
    {
      f.Type = (ImGuiAppFieldType)type;
      f.ArraySize = array_size;
      if (got >= 4)
        ImStrncpy(f.StructType, struct_type, IM_ARRAYSIZE(f.StructType));
      fields->push_back(f);
    }
  }

  bool SaveAppNodeGraph(const char* path, const ImGuiAppNodeDraft* draft, const ImVector<ImGuiAppNodeLink>* links)
  {
    IM_ASSERT(path != nullptr && draft != nullptr && links != nullptr);

    ImGuiTextBuffer buf;
    buf.appendf("[Draft]\n");
    buf.appendf("Name=%s\n", draft->Name);
    for (int i = 0; i < draft->PersistFields.Size; i++)
    {
      const ImGuiAppFieldDesc* f = &draft->PersistFields.Data[i];
      buf.appendf("Persist=%s,%d,%d,%s\n", f->Name, (int)f->Type, f->ArraySize, f->StructType);
    }
    for (int i = 0; i < draft->TempFields.Size; i++)
    {
      const ImGuiAppFieldDesc* f = &draft->TempFields.Data[i];
      buf.appendf("Temp=%s,%d,%d,%s\n", f->Name, (int)f->Type, f->ArraySize, f->StructType);
    }
    for (int i = 0; i < links->Size; i++)
      buf.appendf("Link=%d,%d,%d\n", links->Data[i].Id, links->Data[i].StartAttr, links->Data[i].EndAttr);

    ImFileHandle fh = ImFileOpen(path, "wt");
    if (fh == nullptr)
      return false;
    ImFileWrite(buf.c_str(), sizeof(char), (ImU64)buf.size(), fh);
    ImFileClose(fh);
    return true;
  }

  static void AppNodeWriteDraft(ImGuiTextBuffer* buf, const ImGuiAppNodeDraft* draft)
  {
    buf->appendf("[Draft]\n");
    buf->appendf("Name=%s\n", draft->Name);
    for (int i = 0; i < draft->PersistFields.Size; i++)
    {
      const ImGuiAppFieldDesc* f = &draft->PersistFields.Data[i];
      buf->appendf("Persist=%s,%d,%d,%s\n", f->Name, (int)f->Type, f->ArraySize, f->StructType);
    }
    for (int i = 0; i < draft->TempFields.Size; i++)
    {
      const ImGuiAppFieldDesc* f = &draft->TempFields.Data[i];
      buf->appendf("Temp=%s,%d,%d,%s\n", f->Name, (int)f->Type, f->ArraySize, f->StructType);
    }
  }

  bool SaveAppNodeGraphMulti(const char* path, const ImVector<ImGuiAppNodeDraft>* drafts, const ImVector<ImGuiAppNodeLink>* links)
  {
    IM_ASSERT(path != nullptr && drafts != nullptr && links != nullptr);

    ImGuiTextBuffer buf;
    for (int i = 0; i < drafts->Size; i++)
      AppNodeWriteDraft(&buf, &drafts->Data[i]);
    for (int i = 0; i < links->Size; i++)
      buf.appendf("Link=%d,%d,%d\n", links->Data[i].Id, links->Data[i].StartAttr, links->Data[i].EndAttr);

    ImFileHandle fh = ImFileOpen(path, "wt");
    if (fh == nullptr)
      return false;
    ImFileWrite(buf.c_str(), sizeof(char), (ImU64)buf.size(), fh);
    ImFileClose(fh);
    return true;
  }

  bool LoadAppNodeGraphMulti(const char* path, ImVector<ImGuiAppNodeDraft>* drafts, ImVector<ImGuiAppNodeLink>* links)
  {
    IM_ASSERT(path != nullptr && drafts != nullptr && links != nullptr);

    size_t data_size = 0;
    char* data = (char*)ImFileLoadToMemory(path, "rb", &data_size, 1); // +1 zero terminator
    if (data == nullptr)
      return false;

    drafts->clear();
    links->clear();

    // "[Draft]" opens a new draft; the Name/Persist/Temp lines that follow apply to it. A file with
    // no "[Draft]" header but Name/Persist lines (older single-draft format) still works: the first
    // such line lazily opens draft 0.
    ImGuiAppNodeDraft* cur = nullptr;
    char* p = data;
    while (*p)
    {
      char* eol = p;
      while (*eol != 0 && *eol != '\n')
        eol++;
      const char saved = *eol;
      *eol = 0;
      if (eol > p && eol[-1] == '\r')   // text-mode writes \r\n; binary read keeps the \r -> trim it
        eol[-1] = 0;

      if (strncmp(p, "[Draft]", 7) == 0)
      {
        drafts->push_back(ImGuiAppNodeDraft());
        cur = &drafts->Data[drafts->Size - 1];
        cur->PersistFields.clear();
        cur->TempFields.clear();
      }
      else if (strncmp(p, "Name=", 5) == 0)
      {
        if (cur == nullptr) { drafts->push_back(ImGuiAppNodeDraft()); cur = &drafts->Data[drafts->Size - 1]; cur->PersistFields.clear(); cur->TempFields.clear(); }
        ImStrncpy(cur->Name, p + 5, IM_ARRAYSIZE(cur->Name));
      }
      else if (cur != nullptr && strncmp(p, "Persist=", 8) == 0)
        AppNodeParseField(&cur->PersistFields, p + 8);
      else if (cur != nullptr && strncmp(p, "Temp=", 5) == 0)
        AppNodeParseField(&cur->TempFields, p + 5);
      else if (strncmp(p, "Link=", 5) == 0)
      {
        ImGuiAppNodeLink l;
        if (sscanf(p + 5, "%d,%d,%d", &l.Id, &l.StartAttr, &l.EndAttr) == 3)
          links->push_back(l);
      }

      if (saved == 0)
        break;
      p = eol + 1;
    }

    IM_FREE(data);
    return true;
  }

  bool LoadAppNodeGraph(const char* path, ImGuiAppNodeDraft* draft, ImVector<ImGuiAppNodeLink>* links)
  {
    IM_ASSERT(path != nullptr && draft != nullptr && links != nullptr);

    size_t data_size = 0;
    char* data = (char*)ImFileLoadToMemory(path, "rb", &data_size, 1); // +1 zero terminator
    if (data == nullptr)
      return false;

    draft->PersistFields.clear();
    draft->TempFields.clear();
    links->clear();

    char* p = data;
    while (*p)
    {
      char* eol = p;
      while (*eol != 0 && *eol != '\n')
        eol++;
      const char saved = *eol;
      *eol = 0;

      if (strncmp(p, "Name=", 5) == 0)
        ImStrncpy(draft->Name, p + 5, IM_ARRAYSIZE(draft->Name));
      else if (strncmp(p, "Persist=", 8) == 0)
        AppNodeParseField(&draft->PersistFields, p + 8);
      else if (strncmp(p, "Temp=", 5) == 0)
        AppNodeParseField(&draft->TempFields, p + 5);
      else if (strncmp(p, "Link=", 5) == 0)
      {
        ImGuiAppNodeLink l;
        if (sscanf(p + 5, "%d,%d,%d", &l.Id, &l.StartAttr, &l.EndAttr) == 3)
          links->push_back(l);
      }

      if (saved == 0)
        break;
      p = eol + 1;
    }

    IM_FREE(data);
    return true;
  }

  // Copy src into dst as a valid C++ identifier: alnum/underscore kept, anything else -> '_',
  // a leading digit prefixed with '_'. Empty input becomes "Control".
  static void AppSanitizeIdentifier(char* dst, size_t dst_size, const char* src)
  {
    IM_ASSERT(dst != nullptr && dst_size > 0);

    size_t n = 0;
    if (src == nullptr || src[0] == 0)
    {
      ImStrncpy(dst, "Control", dst_size);
      return;
    }

    if (src[0] >= '0' && src[0] <= '9' && n + 1 < dst_size)
      dst[n++] = '_';

    for (const char* s = src; *s != 0 && n + 1 < dst_size; s++)
    {
      const char c = *s;
      const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
      dst[n++] = keep ? c : '_';
    }
    dst[n] = 0;
  }

  // Type spelling of one drafted field (String is a char array; Struct falls back to a
  // placeholder until the referenced type is named).
  static const char* AppFieldDeclTypeName(const ImGuiAppFieldDesc* f)
  {
    if (f->Type == ImGuiAppFieldType_String)
      return "char";
    if (f->Type == ImGuiAppFieldType_Struct)
      return f->StructType[0] ? f->StructType : "void* /* set struct type */";
    return AppFieldTypeName(f->Type);
  }

  // Identifier column start = widest type spelling in the struct (+1 space), so member names
  // column-align in the emitted definition.
  static int AppFieldDeclTypeWidth(const ImGuiAppFieldDesc* fields, int count)
  {
    int w = 0;
    for (int i = 0; i < count; i++)
    {
      const int len = (int)strlen(AppFieldDeclTypeName(&fields[i]));
      if (len > w)
        w = len;
    }
    return w;
  }

  static void AppEmitFieldDecl(ImGuiTextBuffer* out, const ImGuiAppFieldDesc* f, int type_w)
  {
    char name[IM_LABEL_SIZE];
    AppSanitizeIdentifier(name, IM_ARRAYSIZE(name), f->Name);

    if (f->Type == ImGuiAppFieldType_String)
      out->appendf("  %-*s %s[%d];\n", type_w, "char", name, f->ArraySize > 0 ? f->ArraySize : 128);
    else
      out->appendf("  %-*s %s;\n", type_w, AppFieldDeclTypeName(f), name);
  }

  static void AppEmitControlWithDeps(const ImGuiAppGraph* g, const ImGuiAppNode* n, ImGuiTextBuffer* out);   // fwd (the one control emitter)

  // F16: a lone draft is the graph emitter's depCount==0 path -- a single Control node with no incoming
  // data edges, no events, no commands. Build that scratch node and emit through the ONE control emitter
  // (AppEmitControlWithDeps), so there is no parallel codegen path to drift. The historic single-control
  // output is byte-locked in the codegen proof.
  void GenerateAppControlCode(const ImGuiAppNodeDraft* draft, ImGuiTextBuffer* out)
  {
    IM_ASSERT(draft != nullptr && out != nullptr);
    ImGuiAppGraph g;
    ImGuiAppNode* n = AppGraphAddNode(&g, ImGuiAppNodeKind_Control, draft->Name);
    n->Draft = *draft;
    AppEmitControlWithDeps(&g, n, out);
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Typed node graph: allocation, factory, lookup
  //-----------------------------------------------------------------------------

  // Mirror of ImGuiStatic::_ConstantHash (imguiapp.h): a design port must carry the exact runtime
  // data-flow key ImGuiType<PersistData>::ID.
  static ImGuiID AppConstantHash(const char* s)
  {
    return *s ? (ImGuiID)(unsigned char)*s + 33u * AppConstantHash(s + 1) : 5381u;
  }

  ImGuiID AppNodeStructTypeId(const char* node_name)
  {
    char base[IM_LABEL_SIZE];
    AppSanitizeIdentifier(base, IM_ARRAYSIZE(base), node_name);
    char data[IM_LABEL_SIZE];
    ImFormatString(data, IM_ARRAYSIZE(data), "%sData", base);
    return AppConstantHash(data);
  }

  int AppGraphAllocId(ImGuiAppGraph* g)
  {
    IM_ASSERT(g != nullptr);
    return g->NextId++;
  }

  static void AppGraphPushPort(ImGuiAppGraph* g, ImGuiAppNode* n, ImGuiAppPortKind kind, const char* name, ImGuiID data_type)
  {
    ImGuiAppNodePort p;
    p.Id = AppGraphAllocId(g);
    p.Kind = kind;
    ImStrncpy(p.Name, name, IM_ARRAYSIZE(p.Name));
    p.DataTypeId = data_type;
    n->Ports.push_back(p);
  }

  // Stamp the mandatory ports for a node kind. For a Control, DataOut/DataIn carry the node's own data type id
  // (DataIn is a wildcard multi-link intake -> 0). Layers are fixed root composition slots, so they deliberately
  // have no containment ports.
  static void AppGraphStampPorts(ImGuiAppGraph* g, ImGuiAppNode* n)
  {
    const ImGuiID self_data = n->IsBuiltin && n->DataTypeName[0] ? AppConstantHash(n->DataTypeName)
                                                                 : AppNodeStructTypeId(n->Draft.Name);
    switch (n->Kind)
    {
    case ImGuiAppNodeKind_App:
      AppGraphPushPort(g, n, ImGuiAppPortKind_ChildIn, "children", 0);
      break;
    case ImGuiAppNodeKind_Layer:
      break;
    case ImGuiAppNodeKind_Window:
    case ImGuiAppNodeKind_Sidebar:
      AppGraphPushPort(g, n, ImGuiAppPortKind_ChildIn, "children", 0);
      AppGraphPushPort(g, n, ImGuiAppPortKind_ChildOut, "parent", 0);
      break;
    case ImGuiAppNodeKind_Struct:
      AppGraphPushPort(g, n, ImGuiAppPortKind_ChildIn, "fields", 0);         // receives exploded Field nodes
      AppGraphPushPort(g, n, ImGuiAppPortKind_DataOut, "type", self_data);   // a pure data type
      break;
    case ImGuiAppNodeKind_Field:
      AppGraphPushPort(g, n, ImGuiAppPortKind_DataOut,  "value", self_data); // wire the field into a consumer
      AppGraphPushPort(g, n, ImGuiAppPortKind_ChildOut, "parent", 0);        // owning struct
      break;
    case ImGuiAppNodeKind_Control:
    default:
      AppGraphPushPort(g, n, ImGuiAppPortKind_DataIn, "deps", 0);       // external dependencies
      AppGraphPushPort(g, n, ImGuiAppPortKind_DataIn, "persist", 0);    // exploded PersistData struct tie
      AppGraphPushPort(g, n, ImGuiAppPortKind_DataIn, "temp", 0);       // exploded TempData struct tie
      AppGraphPushPort(g, n, ImGuiAppPortKind_DataOut, "data", self_data);
      AppGraphPushPort(g, n, ImGuiAppPortKind_ChildOut, "parent", 0);
      break;
    }
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Layer column packing + default node placement
  //-----------------------------------------------------------------------------

  static int AppGraphPlacementColumn(const ImGuiAppNode* n)
  {
    switch (n->Kind)
    {
    case ImGuiAppNodeKind_App:
    case ImGuiAppNodeKind_Layer:   return 0;
    case ImGuiAppNodeKind_Window:
    case ImGuiAppNodeKind_Sidebar: return 1;
    case ImGuiAppNodeKind_Struct:  return 2;   // producers, left of the controls that consume them
    case ImGuiAppNodeKind_Field:   return 2;
    case ImGuiAppNodeKind_Control: return 3;
    default:                       return 3;
    }
  }

  // Origin of the layer master column: must leave room LEFT of the first node for the numbered rail
  // gutter + padding and ABOVE it for the group title bar, or the pipeline group box goes negative and clips.
  static const float kAppGraphX0 = 110.0f;
  static const float kAppGraphY0 = 96.0f;
  static const float kAppGraphLayerNodeWidth = 520.0f;
  static const float kAppGraphLayerRowH = 145.0f;

  // Uniform layer-column CONTENT width, model units: the widest layer's measured content raises it
  // for everyone; per-graph value (floor = kAppGraphLayerNodeWidth).
  static float AppLayerUniformW(const ImGuiAppGraph* g)
  {
    return g->_LayerUniformW > 0.0f ? g->_LayerUniformW : kAppGraphLayerNodeWidth;
  }

  static float AppCanvasZoom(const ImGuiAppGraph* g);   // fwd (defined with the view state)

  static int AppGraphPlacementRowHint(const ImGuiAppNode* n)
  {
    if (n->Kind == ImGuiAppNodeKind_App)
      return 0;
    if (n->Kind == ImGuiAppNodeKind_Layer)
      return 1 + (int)n->LayerType;
    return 0;
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Phase-coherent geometry cache
  //-----------------------------------------------------------------------------

  static bool AppEditorNodeWasSubmitted(const ImGuiAppGraph* g, int node_id);

  // The Composer's single canvas instance, created on first use. The engine stores node geometry in
  // MODEL units and measures it the same frame it renders (imguiapp_canvas.h).

  static ImGuiCanvasState* AppEditorCanvas(const ImGuiAppGraph* g)
  {
    if (AppGraphEditorState(g)->Canvas == nullptr)
      AppGraphEditorState(g)->Canvas = ImGui::CanvasCreate();
    return AppGraphEditorState(g)->Canvas;
  }

  ImGuiCanvasState* AppGraphEditorCanvas(const ImGuiAppGraph* g)
  {
    return AppEditorCanvas(g);
  }

  // A node's engine-measured size in MODEL units; false until it has been submitted once.
  // Last frame's MODEL measurement (the engine record), consumed this frame -- the framework's
  // T+1 law; invariant units. The engine's deadband keeps the value zoom-idempotent.
  static bool AppNodeModelSize(const ImGuiAppGraph* g, int node_id, ImVec2* out)
  {
    const ImVec2 s = ImGui::CanvasNodeSize(AppEditorCanvas(g), node_id);
    if (s.x <= 1.0f || s.y <= 1.0f)
      return false;
    *out = s;
    return true;
  }

  // Model-unit footprint: last measurement, else the per-kind estimate.
  static ImVec2 AppLayoutNodeSize(const ImGuiAppGraph* g, const ImGuiAppNode* n)
  {
    {
      ImVec2 m;
      if (AppNodeModelSize(g, n->Id, &m))
        return m;
    }
    switch (n->Kind)
    {
    case ImGuiAppNodeKind_Layer:   return ImVec2(kAppGraphLayerNodeWidth, kAppGraphLayerRowH);
    case ImGuiAppNodeKind_Window:
    case ImGuiAppNodeKind_Sidebar: return ImVec2(260.0f, 120.0f);
    case ImGuiAppNodeKind_Control: return ImVec2(400.0f, 320.0f);
    case ImGuiAppNodeKind_Struct:  return ImVec2(300.0f, 150.0f);
    case ImGuiAppNodeKind_Field:   return ImVec2(250.0f, 85.0f);
    default:                       return ImVec2(300.0f, 140.0f);
    }
  }

  // The x where free-standing (non-layer) content begins: right of the layer pipeline column + its frame gutter.
  static float AppLayoutContentX0(const ImGuiAppGraph* g)
  {
    return kAppGraphX0 + AppLayerUniformW(g) + 170.0f;
  }

  // The layer node anchoring a layer type's pipeline row. Design node preferred: the authored
  // foundation is canonical for a core phase when a live twin also exists (BuildAppLiveGraph rule).
  static const ImGuiAppNode* AppGraphLayerOfType(const ImGuiAppGraph* g, ImGuiAppLayerType type)
  {
    const ImGuiAppNode* live = nullptr;
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (n->Kind != ImGuiAppNodeKind_Layer || n->LayerType != type)
        continue;
      if (!n->IsLive)
        return n;
      live = n;
    }
    return live;
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Window section (windows + sidebars compose INTO the Display layer)
  //-----------------------------------------------------------------------------
  // Window and sidebar nodes are CONTAINED by the Display layer's pipeline section: the section
  // packer owns their positions (the same ownership the column packer has over layer Y), stacking
  // them VERTICALLY beneath the DisplayLayer node's header -- one node per row, top-to-bottom in
  // execution order (the same law the layer rail teaches). Sidebars stack above windows: they run
  // first, consuming viewport workrects, so the windows below them fit the remaining rect. The
  // column reserves the section's extent so the next row seats below it, and the section boundary
  // stretches down over the stack, so a contained node can never exist outside the section.

  static const float kAppGraphWindowSectionIndent = 24.0f;   // section inset from the layer column's left edge (model units)
  static const float kAppGraphWindowSectionGap = 16.0f;      // gap between stacked nodes and below the layer node (model units)

  static bool AppNodeHiddenByCollapse(const ImGuiAppGraph* g, int id);                     // fwd (defined with the scope helpers)
  static const ImGuiAppNode* AppGraphFindNodeConst(const ImGuiAppGraph* g, int node_id);   // fwd

  // The nodes the section contains, in seat order: sidebars first (they execute first, consuming
  // viewport workrects), then windows, each population in graph order (stable across frames).
  static void AppGraphCollectSectionMembers(const ImGuiAppGraph* g, bool show_live, const ImGuiAppNode* skip, ImVector<int>* out_node_ids)
  {
    out_node_ids->clear();
    for (int pass = 0; pass < 2; pass++)
    {
      const ImGuiAppNodeKind kind = pass == 0 ? ImGuiAppNodeKind_Sidebar : ImGuiAppNodeKind_Window;
      for (int i = 0; i < g->Nodes.Size; i++)
      {
        const ImGuiAppNode* n = &g->Nodes.Data[i];
        if (n->Kind != kind || n == skip)
          continue;
        if (!show_live && n->IsLive)
          continue;
        if (AppNodeHiddenByCollapse(g, n->Id))
          continue;
        out_node_ids->push_back(n->Id);
      }
    }
  }

  // Stack `ids` vertically from `origin`, one node per row -- the runtime executes these
  // sequentially, and vertical order IS execution order on this canvas. One slot per id into
  // `out_positions` (when non-null). Returns the stack's extent below origin.y (0 when empty).
  static float AppGraphWindowSectionStack(const ImGuiAppGraph* g, const ImVector<int>* ids, const ImVec2& origin, ImVector<ImVec2>* out_positions)
  {
    float y = origin.y;
    float extent = 0.0f;
    for (int i = 0; i < ids->Size; i++)
    {
      const ImGuiAppNode* n = AppGraphFindNodeConst(g, ids->Data[i]);
      if (n == nullptr)
        continue;
      const ImVec2 sz = AppLayoutNodeSize(g, n);
      if (out_positions != nullptr)
        out_positions->push_back(ImVec2(origin.x, y));
      extent = y + sz.y - origin.y;
      y += sz.y + kAppGraphWindowSectionGap;
    }
    return extent;
  }

  // The section's stack origin against a given Display layer height (callers off the editor
  // canvas pass the guarded AppLayoutNodeSize height; the editor passes the measured one).
  static ImVec2 AppGraphWindowSectionOrigin(const ImGuiAppNode* wl, float wl_h)
  {
    return ImVec2(wl->GridPos.x + kAppGraphWindowSectionIndent, wl->GridPos.y + wl_h + kAppGraphWindowSectionGap);
  }

  static bool AppGraphPlacementOccupied(const ImGuiAppGraph* g, const ImGuiAppNode* self, const ImVec2& pos)
  {
    const float margin = 28.0f;
    const ImVec2 ss = AppLayoutNodeSize(g, self);
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* other = &g->Nodes.Data[i];
      if (other == self || !other->HasGridPos)
        continue;
      const ImVec2 so = AppLayoutNodeSize(g, other);
      const bool x_over = pos.x < other->GridPos.x + so.x + margin && pos.x + ss.x + margin > other->GridPos.x;
      const bool y_over = pos.y < other->GridPos.y + so.y + margin && pos.y + ss.y + margin > other->GridPos.y;
      if (x_over && y_over)
        return true;
    }
    return false;
  }

  static ImVec2 AppGraphFindOpenPlacement(const ImGuiAppGraph* g, const ImGuiAppNode* n, const ImVec2& preferred, bool has_preferred)
  {
    ImVec2 start = preferred;
    if (!has_preferred)
    {
      // Default seats follow the tidy tree's reading order: the layer rail is the left column; the containment
      // tree runs top-down right of it -- windows/sidebars on top, then controls, then structs, then fields.
      if (n->Kind == ImGuiAppNodeKind_Layer || n->Kind == ImGuiAppNodeKind_App)
      {
        start = ImVec2(kAppGraphX0, kAppGraphY0 + AppGraphPlacementRowHint(n) * kAppGraphLayerRowH);
      }
      else
      {
        // Windows and sidebars are contained by the Display layer's section: the new node's seat
        // is its slot in the section stack (sidebars above windows, each population in graph
        // order). The editor's section packer re-stacks every frame; this seat only has to agree
        // with it. Falls through to the generic default only when no Display layer node exists.
        if (n->Kind == ImGuiAppNodeKind_Window || n->Kind == ImGuiAppNodeKind_Sidebar)
        {
          if (const ImGuiAppNode* wl = AppGraphLayerOfType(g, ImGuiAppLayerType_Display))
          {
            ImVector<int> ids;
            AppGraphCollectSectionMembers(g, true, nullptr, &ids);   // includes n (already in Nodes)
            ImVector<ImVec2> slots;
            const ImVec2 origin = AppGraphWindowSectionOrigin(wl, AppLayoutNodeSize(g, wl).y);
            AppGraphWindowSectionStack(g, &ids, origin, &slots);
            for (int i = 0; i < ids.Size; i++)
              if (ids.Data[i] == n->Id)
                return slots.Data[i];
          }
        }
        const int row = (n->Kind == ImGuiAppNodeKind_Window || n->Kind == ImGuiAppNodeKind_Sidebar) ? 0
                      : (n->Kind == ImGuiAppNodeKind_Control) ? 1
                      : (n->Kind == ImGuiAppNodeKind_Struct) ? 2 : 3;
        start = ImVec2(AppLayoutContentX0(g), kAppGraphY0 + (float)row * 220.0f);
      }
    }

    // March down in real-size steps until the measured footprint fits; the second-pass half-column
    // shift keeps a pathological pile from stacking forever.
    const float step = ImMax(60.0f, AppLayoutNodeSize(g, n).y * 0.5f);
    for (int pass = 0; pass < 2; pass++)
    {
      for (int row = 0; row < 96; row++)
      {
        const ImVec2 pos = ImVec2(start.x + (float)pass * 240.0f, start.y + (float)row * step);
        if (!AppGraphPlacementOccupied(g, n, pos))
          return pos;
      }
    }
    return start;
  }

  static void AppGraphCollectVisibleLayerStack(const ImGuiAppGraph* g, bool show_live, ImVector<int>* out_node_ids)
  {
    out_node_ids->clear();
    for (int t = 0; t < ImGuiAppLayerType_COUNT; t++)
    {
      for (int i = 0; i < g->Nodes.Size; i++)
      {
        const ImGuiAppNode* n = &g->Nodes.Data[i];
        if (n->Kind != ImGuiAppNodeKind_Layer || n->LayerType != t)
          continue;
        if (!show_live && n->IsLive)
          continue;
        out_node_ids->push_back(n->Id);
      }
    }
  }

  static ImGuiAppNode* AppGraphFindNodeById(ImGuiAppGraph* g, int node_id)
  {
    for (int i = 0; i < g->Nodes.Size; i++)
      if (g->Nodes.Data[i].Id == node_id)
        return &g->Nodes.Data[i];
    return nullptr;
  }

  static float  AppCanvasZoom(const ImGuiAppGraph* g);                          // fwd (defined with the view state)
  static ImVec2 AppCanvasNodePos(const ImGuiAppGraph* g, int node_id);          // fwd: engine passthrough (model units)
  static void   AppCanvasSetNodePos(const ImGuiAppGraph* g, int node_id, const ImVec2& model);  // fwd: engine passthrough (model units)

  // MODEL-unit node height (the engine measures in model units; fallback row pitch until measured).
  static float AppGraphLayerNodeHeight(const ImGuiAppGraph* g, int node_id)
  {
    const float h = ImGui::CanvasNodeSize(AppEditorCanvas(g), node_id).y;
    return h > 1.0f ? h : kAppGraphLayerRowH;
  }

  // A layer row's FOOTPRINT in the column: the node plus, for the canonical Display layer, the
  // section stack packed beneath it -- the column reserves the section's space so the next row
  // seats below the contained windows/sidebars, not through them.
  static float AppGraphLayerRowFootprint(const ImGuiAppGraph* g, bool show_live, int node_id)
  {
    float h = AppGraphLayerNodeHeight(g, node_id);
    const ImGuiAppNode* n = AppGraphFindNodeConst(g, node_id);
    if (n != nullptr && n == AppGraphLayerOfType(g, ImGuiAppLayerType_Display))
    {
      ImVector<int> ids;
      AppGraphCollectSectionMembers(g, show_live, nullptr, &ids);
      const float extent = AppGraphWindowSectionStack(g, &ids, ImVec2(0.0f, 0.0f), nullptr);
      if (extent > 0.0f)
        h += kAppGraphWindowSectionGap + extent;
    }
    return h;
  }

  // The section packer: assigns every contained window/sidebar its stack slot beneath the
  // (already packed) DisplayLayer row. Runs each root-scope editor frame right after the column
  // pack, so members track the row through provisional packs and anchor drags -- position is
  // OWNED here, the nodes are not user-draggable.
  static void AppGraphSeatWindowSection(ImGuiAppGraph* g, bool show_live)
  {
    const ImGuiAppNode* wl = AppGraphLayerOfType(g, ImGuiAppLayerType_Display);
    if (wl == nullptr)
      return;
    ImVector<int> ids;
    AppGraphCollectSectionMembers(g, show_live, nullptr, &ids);
    if (ids.Size == 0)
      return;
    ImVector<ImVec2> slots;
    const ImVec2 origin = AppGraphWindowSectionOrigin(wl, AppGraphLayerNodeHeight(g, wl->Id));
    AppGraphWindowSectionStack(g, &ids, origin, &slots);
    for (int i = 0; i < ids.Size; i++)
    {
      ImGuiAppNode* n = AppGraphFindNodeById(g, ids.Data[i]);
      if (n == nullptr)
        continue;
      n->GridPos = slots.Data[i];
      n->HasGridPos = true;
      n->_NeedsPlace = true;
      AppCanvasSetNodePos(g, n->Id, n->GridPos);
    }
  }

  static void AppGraphCollectSubtree(const ImGuiAppGraph* g, int root_id, ImVector<int>* out);   // fwd

  // Layer drags push occluded window groups (control clusters) ahead of the dragged edge and
  // keep them STUCK to it for the drag's whole life: positions derive from originals captured
  // at drag start, so a drag that returns home returns the clusters home. The bottom layer node
  // pushes clusters below it down; the top layer node pushes clusters above it up.
  static void AppGraphDragStickClusters(ImGuiAppGraph* g, bool show_live, int dragged_id)
  {
    if (dragged_id == 0)
    {
      g->_DragStickAnchor = 0;
      g->_DragStick.resize(0);
      return;
    }
    const ImGuiAppNode* dn = AppGraphFindNodeConst(g, dragged_id);
    if (dn == nullptr || dn->Kind != ImGuiAppNodeKind_Layer)
      return;
    // Only the column's TOP or BOTTOM row drives the stick.
    bool is_top = true;
    bool is_bot = true;
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (n->Kind != ImGuiAppNodeKind_Layer || n->Id == dragged_id || (!show_live && n->IsLive))
        continue;
      if (n->GridPos.y < dn->GridPos.y)
        is_top = false;
      if (n->GridPos.y > dn->GridPos.y)
        is_bot = false;
    }
    if (!is_top && !is_bot)
      return;

    // Originals captured once per drag.
    if (g->_DragStickAnchor != dragged_id)
    {
      g->_DragStickAnchor = dragged_id;
      g->_DragStick.resize(0);
      for (int i = 0; i < g->Nodes.Size; i++)
      {
        const ImGuiAppNode* n = &g->Nodes.Data[i];
        if ((n->Kind != ImGuiAppNodeKind_Window && n->Kind != ImGuiAppNodeKind_Sidebar) || (!show_live && n->IsLive))
          continue;
        ImVector<int> members;
        AppGraphCollectSubtree(g, n->Id, &members);
        for (int m = 0; m < members.Size; m++)
        {
          if (members.Data[m] == n->Id)
            continue;
          const ImGuiAppNode* mm = AppGraphFindNodeConst(g, members.Data[m]);
          if (mm == nullptr)
            continue;
          ImGuiAppDragStick st;
          st.NodeId = members.Data[m];
          st.OrigY = mm->GridPos.y;
          g->_DragStick.push_back(st);
        }
      }
    }
    auto orig_y = [&](int node_id, float fallback)
    {
      for (int si = 0; si < g->_DragStick.Size; si++)
        if (g->_DragStick.Data[si].NodeId == node_id)
          return g->_DragStick.Data[si].OrigY;
      return fallback;
    };

    // The dragged edge (model): bottom of the bottom row's footprint or top of the top row,
    // plus the column gap.
    const float sgn = is_bot ? 1.0f : -1.0f;
    const float gap = 12.0f;
    const float edge = is_bot ? dn->GridPos.y + AppGraphLayerRowFootprint(g, show_live, dragged_id) + gap
                              : dn->GridPos.y - gap;
    const float row_x0 = dn->GridPos.x;
    const float row_x1 = dn->GridPos.x + ImMax(AppLayerUniformW(g), AppLayoutNodeSize(g, dn).x);

    // Per cluster: x-overlap with the row, and the stick delta from ORIGINALS -- how far the
    // edge stands past the cluster's original near face (never negative: not yet touching, or
    // the drag has returned; either way the cluster sits at its original spot).
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if ((n->Kind != ImGuiAppNodeKind_Window && n->Kind != ImGuiAppNodeKind_Sidebar) || (!show_live && n->IsLive))
        continue;
      ImVector<int> members;
      AppGraphCollectSubtree(g, n->Id, &members);
      bool any = false;
      float bb_x0 = 0.0f;
      float bb_x1 = 0.0f;
      float near_face = 0.0f;
      for (int m = 0; m < members.Size; m++)
      {
        if (members.Data[m] == n->Id)
          continue;
        const ImGuiAppNode* mm = AppGraphFindNodeConst(g, members.Data[m]);
        if (mm == nullptr)
          continue;
        const float oy = orig_y(members.Data[m], mm->GridPos.y);
        const ImVec2 sz = AppLayoutNodeSize(g, mm);
        const float face = is_bot ? oy : oy + sz.y;
        if (!any)
        {
          bb_x0 = mm->GridPos.x;
          bb_x1 = mm->GridPos.x + sz.x;
          near_face = face;
          any = true;
        }
        else
        {
          bb_x0 = ImMin(bb_x0, mm->GridPos.x);
          bb_x1 = ImMax(bb_x1, mm->GridPos.x + sz.x);
          near_face = is_bot ? ImMin(near_face, face) : ImMax(near_face, face);
        }
      }
      if (!any || bb_x1 < row_x0 || bb_x0 > row_x1)
        continue;
      const float delta = sgn * (edge - near_face) > 0.0f ? edge - near_face : 0.0f;
      for (int m = 0; m < members.Size; m++)
      {
        if (members.Data[m] == n->Id)
          continue;
        ImGuiAppNode* mm = AppGraphFindNodeById(g, members.Data[m]);
        if (mm == nullptr)
          continue;
        const float ny = orig_y(members.Data[m], mm->GridPos.y) + delta;
        if (ImAbs(ny - mm->GridPos.y) > 0.01f)
        {
          mm->GridPos.y = ny;
          mm->HasGridPos = true;
          AppCanvasSetNodePos(g, mm->Id, mm->GridPos);
        }
      }
    }
  }


  static void AppGraphConstrainLayerColumn(ImGuiAppGraph* g, bool show_live, int anchor_node_id, const ImVec2* anchor_pos)
  {
    ImVector<int> ids;
    AppGraphCollectVisibleLayerStack(g, show_live, &ids);
    if (ids.Size == 0)
      return;

    const float x = kAppGraphX0;
    const float gap = 12.0f;

    // Pack the stack tight using actual node heights. The engine reports 0 height until a node has been
    // submitted once (AppGraphLayerNodeHeight falls back to kAppGraphLayerRowH), so only FINALIZE
    // (HasGridPos) once every fresh node's height is real; until then keep placement provisional and
    // re-pack next frame.
    bool all_heights_known = true;
    for (int i = 0; i < ids.Size; i++)
    {
      ImGuiAppNode* n = AppGraphFindNodeById(g, ids.Data[i]);
      if (n != nullptr && !n->HasGridPos && ImGui::CanvasNodeSize(AppEditorCanvas(g), n->Id).y <= 1.0f)
      {
        all_heights_known = false;
        break;
      }
    }
    float pack_y = kAppGraphY0;
    for (int i = 0; i < ids.Size; i++)
    {
      ImGuiAppNode* n = AppGraphFindNodeById(g, ids.Data[i]);
      if (n == nullptr)
        continue;
      if (!n->HasGridPos)
      {
        n->GridPos = ImVec2(x, pack_y);
        if (all_heights_known)
          n->HasGridPos = true;
      }
      pack_y = n->GridPos.y + AppGraphLayerRowFootprint(g, show_live, n->Id) + gap;
    }

    for (int i = 1; i < ids.Size; i++)
    {
      const int id = ids.Data[i];
      ImGuiAppNode* n = AppGraphFindNodeById(g, id);
      const float y = n ? n->GridPos.y : 0.0f;
      int j = i - 1;
      while (j >= 0)
      {
        ImGuiAppNode* prev = AppGraphFindNodeById(g, ids.Data[j]);
        if (prev == nullptr || prev->GridPos.y <= y)
          break;
        ids.Data[j + 1] = ids.Data[j];
        j--;
      }
      ids.Data[j + 1] = id;
    }

    // The dragged layer node slides ONLY within its own slot, clamped against its neighbors'
    // footprints (edge nodes against their single neighbor). A drag can never shove the rest of
    // the column: the stack holds still and the anchor stops at the gap.
    if (anchor_node_id != 0 && anchor_pos != nullptr)
    {
      int anchor_index = -1;
      for (int i = 0; i < ids.Size; i++)
        if (ids.Data[i] == anchor_node_id)
        {
          anchor_index = i;
          break;
        }
      ImGuiAppNode* anchor = anchor_index >= 0 ? AppGraphFindNodeById(g, anchor_node_id) : nullptr;
      if (anchor != nullptr)
      {
        const ImGuiAppNode* prev = anchor_index > 0 ? AppGraphFindNodeById(g, ids.Data[anchor_index - 1]) : nullptr;
        const ImGuiAppNode* next = anchor_index + 1 < ids.Size ? AppGraphFindNodeById(g, ids.Data[anchor_index + 1]) : nullptr;
        const float min_y = prev != nullptr ? prev->GridPos.y + AppGraphLayerRowFootprint(g, show_live, prev->Id) + gap : -FLT_MAX;
        const float max_y = next != nullptr ? next->GridPos.y - AppGraphLayerRowFootprint(g, show_live, anchor->Id) - gap : FLT_MAX;
        float y = anchor_pos->y;
        if (min_y <= max_y)
        {
          if (y < min_y) y = min_y;
          if (y > max_y) y = max_y;
          anchor->GridPos = ImVec2(x, y);
        }
      }
    }
    else
    {
      // No drag this frame: top-down separation. A row that grew (the Display layer's footprint
      // includes its section stack) pushes later rows down instead of overlapping them.
      for (int i = 1; i < ids.Size; i++)
      {
        ImGuiAppNode* n = AppGraphFindNodeById(g, ids.Data[i]);
        ImGuiAppNode* prev = AppGraphFindNodeById(g, ids.Data[i - 1]);
        if (n == nullptr || prev == nullptr)
          continue;
        const float min_y = prev->GridPos.y + AppGraphLayerRowFootprint(g, show_live, prev->Id) + gap;
        if (n->GridPos.y < min_y)
          n->GridPos.y = min_y;
      }
    }

    for (int i = 0; i < ids.Size; i++)
    {
      ImGuiAppNode* n = AppGraphFindNodeById(g, ids.Data[i]);
      if (n == nullptr)
        continue;
      const ImVec2 pos(x, n->GridPos.y);
      n->GridPos = pos;
      // Do NOT force HasGridPos here: a node placed provisionally this frame (heights not yet known)
      // must stay un-finalized so the default-placement pass re-packs it next frame with real heights.
      n->_NeedsPlace = true;
      AppCanvasSetNodePos(g, n->Id, pos);
    }
  }

  static void AppGraphPlaceNode(ImGuiAppGraph* g, ImGuiAppNode* n, const ImVec2* preferred)
  {
    n->GridPos = AppGraphFindOpenPlacement(g, n, preferred ? *preferred : ImVec2(0.0f, 0.0f), preferred != nullptr);
    // Layer nodes live in the tight-packed master column -- leave them UN-finalized so
    // AppGraphConstrainLayerColumn owns their Y.
    n->HasGridPos = (n->Kind != ImGuiAppNodeKind_Layer);
    n->_NeedsPlace = true;
  }

  static ImGuiAppNode* AppGraphInitNode(ImGuiAppGraph* g, ImGuiAppNodeKind kind, const char* name)
  {
    // Push an EMPTY node first (its ImVector<Port> is null), then populate in place: ImVector relocates by
    // memcpy on grow, which moves the inner vectors' heap pointers without double-freeing.
    g->Nodes.push_back(ImGuiAppNode());
    ImGuiAppNode* n = &g->Nodes.back();
    n->Id = AppGraphAllocId(g);
    n->Kind = kind;
    ImStrncpy(n->Draft.Name, (name && name[0]) ? name : "Node", IM_ARRAYSIZE(n->Draft.Name));
    if (kind == ImGuiAppNodeKind_Layer && name != nullptr)
    {
      if (strstr(name, "Command") != nullptr) n->LayerType = ImGuiAppLayerType_Command;
      else if (strstr(name, "Status") != nullptr) n->LayerType = ImGuiAppLayerType_Status;
      else if (strstr(name, "Layout") != nullptr) n->LayerType = ImGuiAppLayerType_Layout;
      else if (strstr(name, "Display") != nullptr) n->LayerType = ImGuiAppLayerType_Display;
      else n->LayerType = ImGuiAppLayerType_Task;
    }
    n->BodyAttrId = AppGraphAllocId(g);
    AppGraphPlaceNode(g, n, nullptr);
    AppGraphStampPorts(g, n);
    return n;
  }

  ImGuiAppNode* AppGraphAddNode(ImGuiAppGraph* g, ImGuiAppNodeKind kind, const char* name)
  {
    IM_ASSERT(g != nullptr);
    return AppGraphInitNode(g, kind, name);
  }

  ImGuiAppNode* AppGraphAddBuiltin(ImGuiAppGraph* g, ImGuiAppNodeKind kind, const char* type_name, const char* data_type_name)
  {
    IM_ASSERT(g != nullptr && type_name != nullptr);

    // Build the node manually so IsBuiltin/DataTypeName are set BEFORE ports are stamped (so the DataOut port
    // gets the builtin's real data type id).
    g->Nodes.push_back(ImGuiAppNode());
    ImGuiAppNode* n = &g->Nodes.back();
    n->Id = AppGraphAllocId(g);
    n->Kind = kind;
    n->IsBuiltin = true;
    ImStrncpy(n->TypeName, type_name, IM_ARRAYSIZE(n->TypeName));
    ImStrncpy(n->Draft.Name, type_name, IM_ARRAYSIZE(n->Draft.Name));
    if (data_type_name && data_type_name[0])
      ImStrncpy(n->DataTypeName, data_type_name, IM_ARRAYSIZE(n->DataTypeName));
    n->BodyAttrId = AppGraphAllocId(g);
    AppGraphPlaceNode(g, n, nullptr);
    AppGraphStampPorts(g, n);
    return n;
  }

  static int AppNodePortByName(const ImGuiAppNode* n, const char* name);   // fwd
  static bool AppLayerIsCore(ImGuiAppLayerType t);                         // fwd (defined by the editor section)

  // The five core layers are the guaranteed framework foundation every app builds on. Add any
  // that are missing; never duplicates (one per type). Permanent -- AppGraphRemoveNode refuses them.
  void AppGraphEnsureFoundation(ImGuiAppGraph* g)
  {
    IM_ASSERT(g != nullptr);
    // Insertion order matches the live mirror (and the type enum) and seeds the column packing.
    const struct { ImGuiAppLayerType T; const char* Name; } base[] =
    {
      { ImGuiAppLayerType_Task,    "TaskLayer"    },
      { ImGuiAppLayerType_Command, "CommandLayer" },
      { ImGuiAppLayerType_Status,  "StatusLayer"  },
      { ImGuiAppLayerType_Layout,  "LayoutLayer"  },
      { ImGuiAppLayerType_Display, "DisplayLayer" },
    };
    for (int i = 0; i < IM_ARRAYSIZE(base); i++)
      if (!AppGraphHasLayerType(g, base[i].T))
        AppGraphAddNode(g, ImGuiAppNodeKind_Layer, base[i].Name)->LayerType = base[i].T;
  }

  static void AppGraphLoadTemplate(ImGuiAppGraph* g, int which)
  {
    // Clear authored content but KEEP the foundation layers (permanent), then guarantee all four exist.
    ImVector<int> authored;
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      if (!g->Nodes.Data[i].IsLive && g->Nodes.Data[i].Kind != ImGuiAppNodeKind_Layer)
        authored.push_back(g->Nodes.Data[i].Id);
    }
    for (int i = 0; i < authored.Size; i++)
    {
      AppGraphRemoveNode(g, authored.Data[i]);
    }
    g->Bindings.clear();
    AppGraphEnsureFoundation(g);

    if (which == 0 || which == 1)
    {
      AppGraphAddNode(g, ImGuiAppNodeKind_Window, "MainWindow");
      if (which == 1)
        AppGraphAddNode(g, ImGuiAppNodeKind_Control, "MainControl");
    }
    else if (which == 2)
    {
      const int struct_id = AppGraphAddNode(g, ImGuiAppNodeKind_Struct, "SharedData")->Id;
      const int ctrl_id   = AppGraphAddNode(g, ImGuiAppNodeKind_Control, "Consumer")->Id;
      const ImGuiAppNode* s = AppGraphFindNode(g, struct_id);
      const ImGuiAppNode* c = AppGraphFindNode(g, ctrl_id);
      if (s != nullptr && c != nullptr)
      {
        const int sout = AppNodePortByName(s, "type");
        const int cin  = AppNodePortByName(c, "deps");
        if (sout != 0 && cin != 0)
        {
          ImGuiAppNodeLink l;
          l.Id = AppGraphAllocId(g);
          l.StartAttr = sout;
          l.EndAttr = cin;
          l.Kind = ImGuiAppEdgeKind_Data;
          g->Links.push_back(l);
        }
      }
    }
  }

  // Duplicate a design node (Control / Window / Sidebar / Custom layer): clone its authored props into a fresh
  // node, offset from the source. Core layers (the frame's phases, one each) and live mirrors are not duplicable.
  static int    AppScopeCurrent(const ImGuiAppGraph* g);   // fwd (scope section)
  static bool   AppNodeInScope(const ImGuiAppGraph* g, int id);   // fwd
  static int    AppGraphParentOf(const ImGuiAppGraph* g, int child_node_id);   // fwd
  static const ImGuiAppNode* AppGraphFindNodeConst(const ImGuiAppGraph* g, int node_id);   // fwd
  static ImVec2 AppNodeScopePos(const ImGuiAppGraph* g, const ImGuiAppNode* n);   // fwd
  static void   AppNodeScopePosStore(ImGuiAppGraph* g, int node_id, const ImVec2& pos);   // fwd
  static bool   AppGraphReparent(ImGuiAppGraph* g, int child_id, int parent_id);   // fwd

  static ImGuiAppNode* AppGraphDuplicateNode(ImGuiAppGraph* g, const ImGuiAppNode* src)
  {
    if (src == nullptr || src->IsLive || (src->Kind == ImGuiAppNodeKind_Layer && AppLayerIsCore(src->LayerType)))
      return nullptr;

    // Capture by value first: the add below grows g->Nodes and dangles `src`.
    const int src_id = src->Id;
    const ImGuiAppNodeKind kind = src->Kind;
    const bool is_builtin = src->IsBuiltin;
    char type_name[IM_LABEL_SIZE];
    char data_type[IM_LABEL_SIZE];
    char draft_name[IM_LABEL_SIZE];
    ImStrncpy(type_name, src->TypeName, IM_ARRAYSIZE(type_name));
    ImStrncpy(data_type, src->DataTypeName, IM_ARRAYSIZE(data_type));
    ImStrncpy(draft_name, src->Draft.Name, IM_ARRAYSIZE(draft_name));

    ImGuiAppNode* n = is_builtin
      ? AppGraphAddBuiltin(g, kind, type_name, data_type)
      : AppGraphAddNode(g, kind, draft_name);
    const int n_id = n->Id;
    const ImGuiAppNode* s = AppGraphFindNodeConst(g, src_id);   // re-find after the reallocation

    n->Draft = s->Draft;                 // name + PersistFields/TempFields (ImVector deep-copy)
    n->LayerType = s->LayerType;
    n->Flags = s->Flags;
    n->FieldList = s->FieldList;
    n->HasInitialPlacement = s->HasInitialPlacement;
    n->InitialPos = s->InitialPos;
    n->InitialSize = s->InitialSize;
    n->DockDir = s->DockDir;
    n->DockSize = s->DockSize;
    n->Commands = s->Commands;           // ImVector deep-copy
    n->Events = s->Events;               // ImVector deep-copy
    n->StyleMods = s->StyleMods;         // ImVector deep-copy
    n->ColorMods = s->ColorMods;         // ImVector deep-copy
    n->GridPos = s->GridPos + ImVec2(40.0f, 40.0f);
    n->HasGridPos = true;
    n->_NeedsPlace = true;

    // The copy keeps the source's containment home (a hosted control's clone stays hosted, a
    // field's clone stays in its struct) and, in a drilled interior, seats beside the source.
    const int parent = AppGraphParentOf(g, src_id);
    if (parent >= 0)
      AppGraphReparent(g, n_id, parent);
    if (AppScopeCurrent(g) >= 0 && AppNodeInScope(g, n_id))
      AppNodeScopePosStore(g, n_id, AppNodeScopePos(g, AppGraphFindNodeConst(g, src_id)) + ImVec2(40.0f, 40.0f));
    return AppGraphFindNode(g, n_id);
  }

  static int AppGraphParentOf(const ImGuiAppGraph* g, int child_node_id);   // fwd
  static const ImGuiAppNode* AppGraphFindNodeConst(const ImGuiAppGraph* g, int node_id);   // fwd
  static void AppNodeBaseName(const ImGuiAppNode* n, char* out, size_t out_size);   // fwd
  static void AppGraphCopySelection(const ImGuiAppGraph* g, const ImVector<int>& roots);   // fwd
  static int  AppGraphPasteClipboard(ImGuiAppGraph* g);   // fwd
  static bool AppGraphClipboardHasData(const ImGuiAppGraph* g);   // fwd
  static void AppGraphCollectSubtree(const ImGuiAppGraph* g, int root_id, ImVector<int>* ids);   // fwd
  static int  AppNodeFirstPortKind(const ImGuiAppNode* n, ImGuiAppPortKind kind);   // fwd
  static bool AppIdInSet(const ImVector<int>& s, int id);   // fwd

  // PersistData and TempData are both structs. A "field list" is one of them on any owner -- list 0 = PersistData
  // (Struct nodes + Control), list 1 = TempData (Control only). Everything below is generic over (owner, list).
  static ImVector<ImGuiAppFieldDesc>* AppNodeFieldList(ImGuiAppNode* n, int list)
  {
    return list == 1 ? &n->Draft.TempFields : &n->Draft.PersistFields;
  }

  // A node's port id matching a name (0 if none). Used for the control's named persist/temp tie pins.
  static int AppNodePortByName(const ImGuiAppNode* n, const char* name)
  {
    for (int p = 0; p < n->Ports.Size; p++)
    {
      if (strcmp(n->Ports.Data[p].Name, name) == 0)
      {
        return n->Ports.Data[p].Id;
      }
    }
    return 0;
  }

  // Count Field nodes of a given list parented to an owner -- i.e. that list is currently exploded.
  static int AppGraphFieldNodeCount(const ImGuiAppGraph* g, int owner_id, int list)
  {
    int c = 0;
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (n->Kind == ImGuiAppNodeKind_Field && n->FieldList == list && AppGraphParentOf(g, n->Id) == owner_id)
      {
        c++;
      }
    }
    return c;
  }

  // The effective fields of a (owner, list) struct: from its Field nodes when exploded, else its inline list.
  static void AppNodeEffectiveFields(const ImGuiAppGraph* g, const ImGuiAppNode* owner, int list, ImVector<ImGuiAppFieldDesc>* out)
  {
    out->clear();
    bool exploded = false;
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* fn = &g->Nodes.Data[i];
      if (fn->Kind != ImGuiAppNodeKind_Field || fn->FieldList != list || AppGraphParentOf(g, fn->Id) != owner->Id)
      {
        continue;
      }
      exploded = true;
      if (fn->Draft.PersistFields.Size == 0)
      {
        continue;
      }
      ImGuiAppFieldDesc fd = fn->Draft.PersistFields.Data[0];
      ImStrncpy(fd.Name, fn->Draft.Name, IM_ARRAYSIZE(fd.Name));
      out->push_back(fd);
    }
    if (!exploded)
    {
      *out = (list == 1) ? owner->Draft.TempFields : owner->Draft.PersistFields;
    }
  }

  // Create one Field node for member `fd` of an owner (Struct/Control) list, tied back to the owner via a
  // containment edge and placed at slot `idx` around the owner. Anchors per altitude: offspring cluster
  // around the owner's ROOT position in GridPos and around its interior position in the drilled scope's
  // placements. Does NOT touch the owner's inline list. Shared by explode (every member at once) and the
  // inspector's add-while-exploded road. Returns the new Field node id (0 if the owner vanished).
  static int AppGraphAddExplodedField(ImGuiAppGraph* g, int owner_id, int list, const ImGuiAppFieldDesc& fd, int idx)
  {
    ImGuiAppNode* owner = AppGraphFindNode(g, owner_id);
    if (owner == nullptr)
      return 0;
    // Snapshot off `owner` before AppGraphAddNode grows g->Nodes and dangles it.
    const ImVec2 opos = owner->GridPos;
    const int exp_scope = AppScopeCurrent(g);
    const ImVec2 spos = exp_scope >= 0 ? AppNodeScopePos(g, owner) : opos;
    int owner_in = 0;
    for (int p = 0; p < owner->Ports.Size; p++)
      if (owner->Ports.Data[p].Kind == ImGuiAppPortKind_ChildIn)
        owner_in = owner->Ports.Data[p].Id;

    ImGuiAppNode* fn = AppGraphAddNode(g, ImGuiAppNodeKind_Field, fd.Name);
    fn->Draft.PersistFields.clear();
    fn->Draft.PersistFields.push_back(fd);
    fn->FieldList = list;
    const ImVec2 field_off(-240.0f, (list == 1 ? 30.0f : -150.0f) + (float)idx * 70.0f);
    fn->GridPos = opos + field_off;
    fn->HasGridPos = true;
    fn->_NeedsPlace = true;
    if (exp_scope >= 0)
      AppNodeScopePosStore(g, fn->Id, spos + field_off);
    int field_childout = 0;
    for (int p = 0; p < fn->Ports.Size; p++)
      if (fn->Ports.Data[p].Kind == ImGuiAppPortKind_ChildOut)
        field_childout = fn->Ports.Data[p].Id;
    if (field_childout != 0 && owner_in != 0)
    {
      ImGuiAppNodeLink l;
      l.Id = AppGraphAllocId(g);
      l.StartAttr = field_childout;
      l.EndAttr = owner_in;
      l.Kind = ImGuiAppEdgeKind_Containment;
      g->Links.push_back(l);
    }
    return fn->Id;
  }

  // Explode an owner's (Struct or Control) Persist (0) / Temp (1) struct fields into individual Field nodes, each
  // linked back via containment and wireable. The owner's inline list is cleared (the Field nodes own it now).
  static void AppGraphExplodeFields(ImGuiAppGraph* g, ImGuiAppNode* owner, int list)
  {
    if (owner == nullptr || (owner->Kind != ImGuiAppNodeKind_Struct && owner->Kind != ImGuiAppNodeKind_Control))
    {
      return;
    }
    if (owner->IsLive || owner->IsBuiltin)
    {
      return;
    }
    ImVector<ImGuiAppFieldDesc>* src = AppNodeFieldList(owner, list);
    if (src->Size == 0 || AppGraphFieldNodeCount(g, owner->Id, list) > 0)
    {
      return;
    }

    const int oid = owner->Id;
    const ImVector<ImGuiAppFieldDesc> fields = *src;
    for (int i = 0; i < fields.Size; i++)
      AppGraphAddExplodedField(g, oid, list, fields.Data[i], i);
    if (ImGuiAppNode* oo = AppGraphFindNode(g, oid))   // re-find: owner may have moved
    {
      AppNodeFieldList(oo, list)->clear();
    }
  }

  // Collapse (un-explode) an owner's Persist/Temp Field nodes back into its inline list (capturing renames/edits),
  // then delete them. Inverse of AppGraphExplodeFields.
  static void AppGraphCollapseFields(ImGuiAppGraph* g, ImGuiAppNode* owner, int list)
  {
    if (owner == nullptr)
    {
      return;
    }
    const int oid = owner->Id;
    ImVector<int> field_ids;   // node order (stable)
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (n->Kind == ImGuiAppNodeKind_Field && n->FieldList == list && AppGraphParentOf(g, n->Id) == oid)
      {
        field_ids.push_back(n->Id);
      }
    }
    if (field_ids.Size == 0)
    {
      return;
    }

    ImVector<ImGuiAppFieldDesc> fields;
    for (int i = 0; i < field_ids.Size; i++)
    {
      const ImGuiAppNode* fn = AppGraphFindNodeConst(g, field_ids.Data[i]);
      if (fn == nullptr || fn->Draft.PersistFields.Size == 0)
      {
        continue;
      }
      ImGuiAppFieldDesc fd = fn->Draft.PersistFields.Data[0];
      ImStrncpy(fd.Name, fn->Draft.Name, IM_ARRAYSIZE(fd.Name));   // field node title is authoritative
      fields.push_back(fd);
    }
    for (int i = 0; i < field_ids.Size; i++)
    {
      AppGraphRemoveNode(g, field_ids.Data[i]);
    }

    if (ImGuiAppNode* oo = AppGraphFindNode(g, oid))   // re-find after removals
    {
      *AppNodeFieldList(oo, list) = fields;
    }
  }

  // Inspector field editor that stays honest whether the (owner, list) members live inline or exploded
  // into Field nodes. While exploded the owner's inline vector is empty -- the nodes own the members --
  // so editing that vector is a dead write the next collapse discards. Route every edit/add/delete to the
  // Field nodes instead; the node title is the authoritative member name on collapse.
  static void EditAppNodeFieldSection(ImGuiAppGraph* g, ImGuiAppNode* owner, int list, const char* label)
  {
    if (owner->IsLive)
      return;   // live mirror: fields are read-only (defense-in-depth; the inspector already gates this)
    if (AppGraphFieldNodeCount(g, owner->Id, list) == 0)
    {
      EditAppFieldList(label, AppNodeFieldList(owner, list), g);   // inline: edit the draft vector directly
      return;
    }

    const int owner_id = owner->Id;
    ImGui::PushID(label);
    ImGui::TextDisabled("%s", label);

    ImVector<int> ids;                                             // field node ids in node order (== collapse order)
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* fn = &g->Nodes.Data[i];
      if (fn->Kind == ImGuiAppNodeKind_Field && fn->FieldList == list && AppGraphParentOf(g, fn->Id) == owner_id)
        ids.push_back(fn->Id);
    }

    const float em = ImGui::GetFontSize();
    for (int i = 0; i < ids.Size; i++)
    {
      ImGuiAppNode* fn = AppGraphFindNode(g, ids.Data[i]);
      if (fn == nullptr)
        continue;
      if (fn->Draft.PersistFields.Size == 0)
        fn->Draft.PersistFields.push_back(ImGuiAppFieldDesc());
      ImGui::PushID(ids.Data[i]);

      // Title is authoritative on collapse; mirror it into the field desc so inline readers (codegen
      // preview, summary lines) agree before the collapse happens.
      if (AppBlInputText("##name", fn->Draft.Name, IM_ARRAYSIZE(fn->Draft.Name), em * 8.0f))
        ImStrncpy(fn->Draft.PersistFields.Data[0].Name, fn->Draft.Name, IM_ARRAYSIZE(fn->Draft.PersistFields.Data[0].Name));
      ImGui::SameLine();
      EditAppFieldTypeControls(&fn->Draft.PersistFields.Data[0], em * 5.0f, g);
      ImGui::SameLine();
      if (AppRowDeleteButton("X"))
      {
        AppGraphRemoveNode(g, ids.Data[i]);
        ImGui::PopID();
        continue;
      }
      ImGui::PopID();
    }

    if (AppBlAddPill("Add field", "Add field"))
    {
      ImGuiAppFieldDesc fd;
      ImStrncpy(fd.Name, "field", IM_ARRAYSIZE(fd.Name));
      fd.Type = ImGuiAppFieldType_Float;
      AppGraphAddExplodedField(g, owner_id, list, fd, AppGraphFieldNodeCount(g, owner_id, list));   // new member joins as a Field node
    }
    ImGui::PopID();
  }

  // Explode one of a Control's two structs OUT as a standalone Struct node (named <Control>Data /
  // <Control>TempData), tied via a data edge. The control's inline list is cleared (the struct owns it).
  static void AppGraphExplodeControlData(ImGuiAppGraph* g, ImGuiAppNode* c, bool temp)
  {
    if (c == nullptr || c->Kind != ImGuiAppNodeKind_Control || c->IsLive || c->IsBuiltin)
    {
      return;
    }
    if ((temp ? c->TempStructId : c->PersistStructId) >= 0)
    {
      return;
    }

    const int cid = c->Id;
    char cbase[IM_LABEL_SIZE];
    AppNodeBaseName(c, cbase, IM_ARRAYSIZE(cbase));
    char sname[IM_LABEL_SIZE];
    ImFormatString(sname, IM_ARRAYSIZE(sname), temp ? "%sTempData" : "%sData", cbase);
    const ImVector<ImGuiAppFieldDesc> fields = temp ? c->Draft.TempFields : c->Draft.PersistFields;
    const ImVec2 cpos = c->GridPos;
    // Anchor per altitude (root cluster in GridPos, interior cluster in the scope's placements).
    const int exp_scope = AppScopeCurrent(g);
    const ImVec2 cspos = exp_scope >= 0 ? AppNodeScopePos(g, c) : cpos;
    const int control_in = AppNodePortByName(c, temp ? "temp" : "persist");   // dedicated tie pin, not "deps"

    ImGuiAppNode* s = AppGraphAddNode(g, ImGuiAppNodeKind_Struct, sname);
    s->Draft.PersistFields = fields;
    const ImVec2 struct_off(-280.0f, temp ? 100.0f : -100.0f);
    s->GridPos = cpos + struct_off;
    s->HasGridPos = true;
    s->_NeedsPlace = true;
    if (exp_scope >= 0)
      AppNodeScopePosStore(g, s->Id, cspos + struct_off);
    const int sid = s->Id;
    int struct_out = 0;
    for (int p = 0; p < s->Ports.Size; p++)
    {
      if (s->Ports.Data[p].Kind == ImGuiAppPortKind_DataOut)
      {
        struct_out = s->Ports.Data[p].Id;
      }
    }

    ImGuiAppNode* cc = AppGraphFindNode(g, cid);   // re-find: `c` may have moved
    if (cc == nullptr)
    {
      return;
    }
    if (temp)
    {
      cc->TempStructId = sid;
      cc->Draft.TempFields.clear();
    }
    else
    {
      cc->PersistStructId = sid;
      cc->Draft.PersistFields.clear();
    }
    if (struct_out != 0 && control_in != 0)
    {
      ImGuiAppNodeLink l;
      l.Id = AppGraphAllocId(g);
      l.StartAttr = struct_out;
      l.EndAttr = control_in;
      l.Kind = ImGuiAppEdgeKind_Data;
      g->Links.push_back(l);
    }
  }

  // Collapse a control's exploded Persist/Temp struct back inline: pull the struct's (possibly field-exploded)
  // fields home, delete the struct (and its Field nodes); the tie edge is swept by AppGraphRemoveNode.
  static void AppGraphCollapseControlData(ImGuiAppGraph* g, ImGuiAppNode* c, bool temp)
  {
    if (c == nullptr)
    {
      return;
    }
    const int sid = temp ? c->TempStructId : c->PersistStructId;
    if (sid < 0)
    {
      return;
    }

    const int cid = c->Id;
    ImVector<ImGuiAppFieldDesc> fields;
    if (const ImGuiAppNode* s = AppGraphFindNodeConst(g, sid))
    {
      AppNodeEffectiveFields(g, s, 0, &fields);
    }

    ImVector<int> field_nodes;
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      if (g->Nodes.Data[i].Kind == ImGuiAppNodeKind_Field && AppGraphParentOf(g, g->Nodes.Data[i].Id) == sid)
      {
        field_nodes.push_back(g->Nodes.Data[i].Id);
      }
    }
    for (int i = 0; i < field_nodes.Size; i++)
    {
      AppGraphRemoveNode(g, field_nodes.Data[i]);
    }
    if (AppGraphFindNode(g, sid) != nullptr)
    {
      AppGraphRemoveNode(g, sid);
    }

    ImGuiAppNode* cc = AppGraphFindNode(g, cid);   // re-find after removals
    if (cc == nullptr)
    {
      return;
    }
    if (temp)
    {
      cc->Draft.TempFields = fields;
      cc->TempStructId = -1;
    }
    else
    {
      cc->Draft.PersistFields = fields;
      cc->PersistStructId = -1;
    }
  }

  // Effective struct id for a control's Persist/Temp (heals a stale ref if the struct was deleted directly).
  static int AppControlStructId(ImGuiAppGraph* g, ImGuiAppNode* c, bool temp)
  {
    int& id = temp ? c->TempStructId : c->PersistStructId;
    if (id >= 0 && AppGraphFindNode(g, id) == nullptr)
    {
      id = -1;
    }
    return id;
  }

  ImGuiAppNode* AppGraphFindNode(ImGuiAppGraph* g, int node_id)
  {
    IM_ASSERT(g != nullptr);
    for (int i = 0; i < g->Nodes.Size; i++)
      if (g->Nodes.Data[i].Id == node_id)
        return &g->Nodes.Data[i];
    return nullptr;
  }

  ImGuiAppNodePort* AppGraphFindPort(ImGuiAppGraph* g, int port_id, ImGuiAppNode** out_owner)
  {
    IM_ASSERT(g != nullptr);
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      ImGuiAppNode* n = &g->Nodes.Data[i];
      for (int p = 0; p < n->Ports.Size; p++)
        if (n->Ports.Data[p].Id == port_id)
        {
          if (out_owner) *out_owner = n;
          return &n->Ports.Data[p];
        }
    }
    if (out_owner) *out_owner = nullptr;
    return nullptr;
  }

  static const ImGuiAppNode* AppGraphFindLayerOfType(const ImGuiAppGraph* g, ImGuiAppLayerType type, int ignore_node_id = 0)
  {
    IM_ASSERT(g != nullptr);
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (n->Id != ignore_node_id && n->Kind == ImGuiAppNodeKind_Layer && n->LayerType == type)
        return n;
    }
    return nullptr;
  }

  bool AppGraphHasLayerType(const ImGuiAppGraph* g, ImGuiAppLayerType type)
  {
    return AppGraphFindLayerOfType(g, type) != nullptr;
  }

  static bool AppNodeIsCommandLayer(const ImGuiAppNode* n)
  {
    return n != nullptr && n->Kind == ImGuiAppNodeKind_Layer && n->LayerType == ImGuiAppLayerType_Command;
  }

  static int AppGraphCommandDefinitionCount(const ImGuiAppGraph* g)
  {
    int count = 0;
    for (int i = 0; i < g->Nodes.Size; i++)
      if (AppNodeIsCommandLayer(&g->Nodes.Data[i]))
        count += g->Nodes.Data[i].Commands.Size;
    return count;
  }

  static const ImGuiAppCommandDesc* AppGraphCommandDefinitionAt(const ImGuiAppGraph* g, int index)
  {
    int seen = 0;
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (!AppNodeIsCommandLayer(n))
        continue;
      for (int c = 0; c < n->Commands.Size; c++, seen++)
        if (seen == index)
          return &n->Commands.Data[c];
    }
    return nullptr;
  }

  static const ImGuiAppCommandDesc* AppGraphFindCommandDefinition(const ImGuiAppGraph* g, const char* name)
  {
    if (name == nullptr || name[0] == 0)
      return nullptr;
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (!AppNodeIsCommandLayer(n))
        continue;
      for (int c = 0; c < n->Commands.Size; c++)
        if (strcmp(n->Commands.Data[c].Name, name) == 0)
          return &n->Commands.Data[c];
    }
    return nullptr;
  }

  static bool AppNodeCommandNameUsed(const ImGuiAppNode* n, const char* name, int ignore_index = -1)
  {
    if (n == nullptr || name == nullptr || name[0] == 0)
      return false;
    for (int i = 0; i < n->Commands.Size; i++)
      if (i != ignore_index && strcmp(n->Commands.Data[i].Name, name) == 0)
        return true;
    return false;
  }

  void AppNodeAddCommand(ImGuiAppNode* n, const char* name)
  {
    IM_ASSERT(n != nullptr);
    ImGuiAppCommandDesc cmd;
    ImStrncpy(cmd.Name, (name && name[0]) ? name : "NewCommand", IM_ARRAYSIZE(cmd.Name));
    n->Commands.push_back(cmd);
  }

  static void AppNodeAddCommandUnique(ImGuiAppNode* n, const char* base_name)
  {
    char name[IM_LABEL_SIZE];
    ImStrncpy(name, (base_name && base_name[0]) ? base_name : "NewCommand", IM_ARRAYSIZE(name));
    if (AppNodeCommandNameUsed(n, name))
      for (int suffix = 2; suffix < 1000; suffix++)
      {
        ImFormatString(name, IM_ARRAYSIZE(name), "%s%d", (base_name && base_name[0]) ? base_name : "NewCommand", suffix);
        if (!AppNodeCommandNameUsed(n, name))
          break;
      }
    AppNodeAddCommand(n, name);
  }

  void AppNodeRemoveCommand(ImGuiAppNode* n, int index)
  {
    IM_ASSERT(n != nullptr);
    if (index >= 0 && index < n->Commands.Size)
      n->Commands.erase(n->Commands.Data + index);
  }

  static void EditAppNodeCommands(ImGuiAppNode* n, bool with_add_pill = true)
  {
    IM_ASSERT(n != nullptr);

    if (n->Commands.Size > 0)
      ImGui::TextDisabled("app commands");
    int remove = -1;
    for (int i = 0; i < n->Commands.Size; i++)
    {
      ImGui::PushID(i);
      AppBlInputText("##cmd", n->Commands.Data[i].Name, IM_ARRAYSIZE(n->Commands.Data[i].Name), ImGui::GetFontSize() * 16.0f);
      if (AppNodeCommandNameUsed(n, n->Commands.Data[i].Name, i))
      {
        ImGui::SameLine();
        ImGui::TextDisabled("duplicate");
      }
      ImGui::SameLine();
      if (AppRowDeleteButton("##del"))
        remove = i;
      ImGui::PopID();
    }
    if (remove >= 0)
      AppNodeRemoveCommand(n, remove);
    if (with_add_pill && AppBlAddPill("Add command", "Command"))
      AppNodeAddCommandUnique(n, "NewCommand");
  }

  static void EditAppControlCommandChoices(const ImGuiAppGraph* g, ImGuiAppNode* n)
  {
    IM_ASSERT(g != nullptr && n != nullptr);

    const int def_count = AppGraphCommandDefinitionCount(g);
    if (def_count == 0)
    {
      ImGui::TextDisabled("commands: define commands on CommandLayer");
      return;
    }

    ImGui::TextDisabled("emits commands");
    int remove = -1;
    for (int i = 0; i < n->Commands.Size; i++)
    {
      ImGui::PushID(i);
      const char* current = n->Commands.Data[i].Name;
      const bool missing = current[0] != 0 && AppGraphFindCommandDefinition(g, current) == nullptr;
      char preview[IM_LABEL_SIZE + 32];
      if (missing)
        ImFormatString(preview, IM_ARRAYSIZE(preview), "%s (missing)", current);
      else if (current[0] != 0)
        ImFormatString(preview, IM_ARRAYSIZE(preview), "%s", current);
      else
        ImFormatString(preview, IM_ARRAYSIZE(preview), "<command>");

      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
      if (ImGui::BeginCombo("##cmdref", preview))
      {
        for (int d = 0; d < def_count; d++)
        {
          const ImGuiAppCommandDesc* def = AppGraphCommandDefinitionAt(g, d);
          if (def == nullptr)
            continue;
          const bool selected = strcmp(current, def->Name) == 0;
          const bool taken = AppNodeCommandNameUsed(n, def->Name, i);
          if (taken) ImGui::BeginDisabled();
          if (ImGui::Selectable(def->Name, selected) && !taken)
            ImStrncpy(n->Commands.Data[i].Name, def->Name, IM_ARRAYSIZE(n->Commands.Data[i].Name));
          if (taken) ImGui::EndDisabled();
        }
        ImGui::EndCombo();
      }
      ImGui::SameLine();
      if (AppRowDeleteButton("##del"))
        remove = i;
      ImGui::PopID();
    }
    if (remove >= 0)
      AppNodeRemoveCommand(n, remove);

    const ImGuiAppCommandDesc* first_available = nullptr;
    for (int d = 0; d < def_count; d++)
    {
      const ImGuiAppCommandDesc* def = AppGraphCommandDefinitionAt(g, d);
      if (def != nullptr && !AppNodeCommandNameUsed(n, def->Name))
      {
        first_available = def;
        break;
      }
    }
    if (first_available == nullptr)
      ImGui::BeginDisabled();
    if (AppBlAddPill("Add command", "Command") && first_available != nullptr)
      AppNodeAddCommand(n, first_available->Name);
    if (first_available == nullptr)
    {
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::TextDisabled("all commands selected");
    }
  }

  //-----------------------------------------------------------------------------
  // Authored events (the temp ^ last_temp idiom, made first-class)
  //
  // Per-frame contract: OnRender records raw input into TempData (zeroed every frame); OnUpdate receives
  // that TempData AND last frame's, and user code identifies what happened by comparing them. An
  // ImGuiAppEventDesc authors one such comparison plus its reaction; codegen emits the guarded block verbatim.
  //-----------------------------------------------------------------------------

  static const char* AppEventEdgeName(int e)
  {
    switch (e)
    {
    case ImGuiAppEventEdge_Rising:  return "rising (t && !last)";
    case ImGuiAppEventEdge_Falling: return "falling (!t && last)";
    case ImGuiAppEventEdge_Changed: return "changed (t ^ last)";
    case ImGuiAppEventEdge_Active:  return "while active (t)";
    default:                        return "changed (t ^ last)";
    }
  }

  static const char* AppEventActionName(int a)
  {
    switch (a)
    {
    case ImGuiAppEventAction_SetField:    return "set field";
    case ImGuiAppEventAction_EmitCommand: return "emit command";
    default:                              return "set field";
    }
  }

  // Effective (inline or exploded) field type of `name` in the node's list (0 persist / 1 temp), or -1.
  static int AppNodeEffectiveFieldType(const ImGuiAppGraph* g, const ImGuiAppNode* n, int list, const char* name)
  {
    ImVector<ImGuiAppFieldDesc> fields;
    AppNodeEffectiveFields(g, n, list, &fields);
    for (int i = 0; i < fields.Size; i++)
      if (strcmp(fields.Data[i].Name, name) == 0)
        return (int)fields.Data[i].Type;
    return -1;
  }

  // One row per event: "when <TempField> <edge> -> <action>" plus the action's parameters. Events watch
  // TempData, so the add pill needs a Temp field to exist first.
  static void EditAppControlEvents(ImGuiAppGraph* g, ImGuiAppNode* n)
  {
    IM_ASSERT(g != nullptr && n != nullptr);
    if (n->IsLive)
      return;   // live mirror: events are read-only (defense-in-depth; the inspector already gates this)

    ImVector<ImGuiAppFieldDesc> temps;
    ImVector<ImGuiAppFieldDesc> persists;
    AppNodeEffectiveFields(g, n, 1, &temps);
    AppNodeEffectiveFields(g, n, 0, &persists);

    const float em = ImGui::GetFontSize();
    if (n->Events.Size > 0)
      ImGui::TextDisabled("events  (OnUpdate: temp vs last_temp)");
    int remove = -1;
    for (int i = 0; i < n->Events.Size; i++)
    {
      ImGuiAppEventDesc* ev = &n->Events.Data[i];
      ImGui::PushID(2000 + i);

      if (AppRowDeleteButton("##del"))
        remove = i;
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      ImGui::TextDisabled("when");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(em * 6.0f);
      if (ImGui::BeginCombo("##tf", ev->TempField[0] ? ev->TempField : "<temp field>"))
      {
        for (int t = 0; t < temps.Size; t++)
          if (ImGui::Selectable(temps.Data[t].Name, strcmp(ev->TempField, temps.Data[t].Name) == 0))
            ImStrncpy(ev->TempField, temps.Data[t].Name, IM_ARRAYSIZE(ev->TempField));
        ImGui::EndCombo();
      }
      ImGui::SameLine();
      AppBlEnum("##edge", em * 9.5f, &ev->Edge, AppEventEdgeName, ImGuiAppEventEdge_COUNT);
      ImGui::SameLine();
      AppBlEnum("##act", em * 7.0f, &ev->Action, AppEventActionName, ImGuiAppEventAction_COUNT);

      ImGui::Dummy(ImVec2(em * 1.6f, 0.0f));
      ImGui::SameLine();
      if (ev->Action == ImGuiAppEventAction_SetField)
      {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("data->");
        ImGui::SameLine(0.0f, em * 0.125f);
        ImGui::SetNextItemWidth(em * 6.0f);
        if (ImGui::BeginCombo("##dst", ev->DstField[0] ? ev->DstField : "<field>"))
        {
          for (int f = 0; f < persists.Size; f++)
            if (ImGui::Selectable(persists.Data[f].Name, strcmp(ev->DstField, persists.Data[f].Name) == 0))
              ImStrncpy(ev->DstField, persists.Data[f].Name, IM_ARRAYSIZE(ev->DstField));
          ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("=");
        ImGui::SameLine();
        AppBlInputText("##expr", ev->Expr, IM_ARRAYSIZE(ev->Expr), em * 10.0f);

        // Type-check live: emission is verbatim, so a bad expr otherwise surfaces at compile time.
        char eerr[192];
        if (!AppEventExprCheck(g, n, ev, eerr, IM_ARRAYSIZE(eerr)))
        {
          ImGui::Dummy(ImVec2(em * 1.6f, 0.0f));
          ImGui::SameLine();
          ImGui::PushStyleColor(ImGuiCol_Text, AppComposerGetStyle()->ErrorText);
          ImGui::TextWrapped("%s", eerr);
          ImGui::PopStyleColor();
        }
      }
      else
      {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("cmd:");
        ImGui::SameLine();
        if (n->Commands.Size == 0)
          ImGui::TextDisabled("(pick a command on this control first)");
        else
        {
          ImGui::SetNextItemWidth(em * 9.0f);
          if (ImGui::BeginCombo("##cmd", ev->Command[0] ? ev->Command : "<command>"))
          {
            for (int c = 0; c < n->Commands.Size; c++)
              if (ImGui::Selectable(n->Commands.Data[c].Name, strcmp(ev->Command, n->Commands.Data[c].Name) == 0))
                ImStrncpy(ev->Command, n->Commands.Data[c].Name, IM_ARRAYSIZE(ev->Command));
            ImGui::EndCombo();
          }
        }
      }
      ImGui::PopID();
    }
    if (remove >= 0)
      n->Events.erase(n->Events.Data + remove);

    if (temps.Size == 0)
    {
      if (n->Events.Size == 0)
        ImGui::TextDisabled("events watch TempData -- add a Temp field first");
    }
    else if (AppBlAddPill("##addevent", "Event"))
    {
      ImGuiAppEventDesc ev;
      ImStrncpy(ev.TempField, temps.Data[0].Name, IM_ARRAYSIZE(ev.TempField));
      char fld[IM_LABEL_SIZE];
      AppSanitizeIdentifier(fld, IM_ARRAYSIZE(fld), temps.Data[0].Name);
      ImFormatString(ev.Expr, IM_ARRAYSIZE(ev.Expr), "temp_data->%s", fld);
      n->Events.push_back(ev);
    }
  }

  // Owner node id for a port id (const-friendly; -1 if unknown). Used by topo/codegen which take const graphs.
  static int AppGraphPortOwnerId(const ImGuiAppGraph* g, int port_id)
  {
    for (int i = 0; i < g->Nodes.Size; i++)
      for (int p = 0; p < g->Nodes.Data[i].Ports.Size; p++)
        if (g->Nodes.Data[i].Ports.Data[p].Id == port_id)
          return g->Nodes.Data[i].Id;
    return -1;
  }

  void AppGraphRemoveNode(ImGuiAppGraph* g, int node_id)
  {
    IM_ASSERT(g != nullptr);

    ImGuiAppNode* n = AppGraphFindNode(g, node_id);
    if (n == nullptr)
      return;

    // The four CORE layers are the guaranteed framework foundation -- the frame's phases, permanent and
    // undeletable. Custom layers are user code and delete like any node.
    if (n->Kind == ImGuiAppNodeKind_Layer && !n->IsLive && AppLayerIsCore(n->LayerType))
      return;

    // Sweep every link incident on one of this node's ports, and any binding orphaned by those links.
    for (int li = g->Links.Size - 1; li >= 0; li--)
    {
      const int owner_a = AppGraphPortOwnerId(g, g->Links.Data[li].StartAttr);
      const int owner_b = AppGraphPortOwnerId(g, g->Links.Data[li].EndAttr);
      if (owner_a == node_id || owner_b == node_id)
      {
        const int link_id = g->Links.Data[li].Id;
        for (int bi = g->Bindings.Size - 1; bi >= 0; bi--)
          if (g->Bindings.Data[bi].LinkId == link_id)
            g->Bindings.erase(g->Bindings.Data + bi);
        g->Links.erase(g->Links.Data + li);
      }
    }
    for (int si = g->Selection.Size - 1; si >= 0; si--)
      if (g->Selection.Data[si] == node_id)
        g->Selection.erase(g->Selection.Data + si);
    for (int pi = g->ScopePlacements.Size - 1; pi >= 0; pi--)
      if (g->ScopePlacements.Data[pi].NodeId == node_id || g->ScopePlacements.Data[pi].ScopeId == node_id)
        g->ScopePlacements.erase(g->ScopePlacements.Data + pi);
    for (int ci = g->ScopeCams.Size - 1; ci >= 0; ci--)
      if (g->ScopeCams.Data[ci].ScopeId == node_id)
        g->ScopeCams.erase(g->ScopeCams.Data + ci);

    g->Nodes.erase(n);   // surviving nodes/ports/links keep their ids; ids are never reused
  }

  // Erase a single link by id, plus any field binding keyed to it.
  static void AppGraphEraseLink(ImGuiAppGraph* g, int link_id)
  {
    for (int li = g->Links.Size - 1; li >= 0; li--)
    {
      if (g->Links.Data[li].Id != link_id)
        continue;
      for (int bi = g->Bindings.Size - 1; bi >= 0; bi--)
        if (g->Bindings.Data[bi].LinkId == link_id)
          g->Bindings.erase(g->Bindings.Data + bi);
      g->Links.erase(g->Links.Data + li);
      break;
    }
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Typed links: resolve / validate / capture
  //-----------------------------------------------------------------------------

  static bool AppPortIsOutput(ImGuiAppPortKind k) { return k == ImGuiAppPortKind_DataOut || k == ImGuiAppPortKind_ChildOut; }
  static bool AppPortIsInput(ImGuiAppPortKind k)  { return k == ImGuiAppPortKind_DataIn  || k == ImGuiAppPortKind_ChildIn; }

  // Does 'from' reach 'to' along existing data edges (producer -> consumer)? Used to reject a new edge
  // that would close a cycle. Iteration capped in case the model is ever inconsistent.
  static bool AppGraphDataReaches(const ImGuiAppGraph* g, int from_node, int to_node)
  {
    if (from_node == to_node)
      return true;

    ImVector<int> frontier;
    frontier.push_back(from_node);
    ImVector<int> seen;
    seen.push_back(from_node);

    for (int guard = 0; guard < g->Links.Size + 1 && frontier.Size > 0; guard++)
    {
      ImVector<int> next;
      for (int f = 0; f < frontier.Size; f++)
      {
        const int cur = frontier.Data[f];
        for (int li = 0; li < g->Links.Size; li++)
        {
          if (g->Links.Data[li].Kind != ImGuiAppEdgeKind_Data)
            continue;
          if (AppGraphPortOwnerId(g, g->Links.Data[li].StartAttr) != cur)
            continue;
          const int consumer = AppGraphPortOwnerId(g, g->Links.Data[li].EndAttr);
          if (consumer == to_node)
            return true;
          bool known = false;
          for (int s = 0; s < seen.Size; s++) if (seen.Data[s] == consumer) { known = true; break; }
          if (!known) { seen.push_back(consumer); next.push_back(consumer); }
        }
      }
      frontier.swap(next);
    }
    return false;
  }

  static void AppSetErr(char* err, int err_size, const char* msg)
  {
    if (err && err_size > 0)
      ImStrncpy(err, msg, (size_t)err_size);
  }

  // Resolve an attempted (a -> b) wire (drag order arbitrary) into a normalized source->target edge.
  // Writes the output port id to out_src, input port id to out_dst, and the derived edge kind. err on reject.
  static bool AppGraphResolveLink(ImGuiAppGraph* g, int a, int b, int* out_src, int* out_dst, ImGuiAppEdgeKind* out_kind, char* err, int err_size)
  {
    ImGuiAppNode* na = nullptr; ImGuiAppNode* nb = nullptr;
    ImGuiAppNodePort* pa = AppGraphFindPort(g, a, &na);
    ImGuiAppNodePort* pb = AppGraphFindPort(g, b, &nb);
    if (pa == nullptr || pb == nullptr) { AppSetErr(err, err_size, "unknown port"); return false; }
    if (na == nb) { AppSetErr(err, err_size, "cannot link a node to itself"); return false; }

    // Normalize so src is the output side, dst the input side.
    ImGuiAppNodePort* src = nullptr; ImGuiAppNodePort* dst = nullptr;
    ImGuiAppNode* src_owner = nullptr; ImGuiAppNode* dst_owner = nullptr;
    if (AppPortIsOutput(pa->Kind) && AppPortIsInput(pb->Kind)) { src = pa; dst = pb; src_owner = na; dst_owner = nb; }
    else if (AppPortIsOutput(pb->Kind) && AppPortIsInput(pa->Kind)) { src = pb; dst = pa; src_owner = nb; dst_owner = na; }
    else { AppSetErr(err, err_size, "must connect an output port to an input port"); return false; }

    // Reject an exact duplicate edge (same two ports).
    for (int li = 0; li < g->Links.Size; li++)
      if (g->Links.Data[li].StartAttr == src->Id && g->Links.Data[li].EndAttr == dst->Id)
      { AppSetErr(err, err_size, "duplicate link"); return false; }

    if (src->Kind == ImGuiAppPortKind_DataOut && dst->Kind == ImGuiAppPortKind_DataIn)
    {
      // Data dependency: producer (src_owner) -> consumer (dst_owner).
      // No duplicate dependency TYPE: the runtime keys app->Data by PersistData type.
      for (int li = 0; li < g->Links.Size; li++)
      {
        if (g->Links.Data[li].Kind != ImGuiAppEdgeKind_Data) continue;
        if (AppGraphPortOwnerId(g, g->Links.Data[li].EndAttr) != dst_owner->Id) continue;
        ImGuiAppNode* existing_producer = nullptr;
        AppGraphFindPort(g, g->Links.Data[li].StartAttr, &existing_producer);
        if (existing_producer && existing_producer->Ports.Size > 0)
        {
          // compare the producers' data type ids (their DataOut port)
          ImGuiID existing_tid = 0;
          for (int pp = 0; pp < existing_producer->Ports.Size; pp++)
            if (existing_producer->Ports.Data[pp].Kind == ImGuiAppPortKind_DataOut) existing_tid = existing_producer->Ports.Data[pp].DataTypeId;
          if (existing_tid != 0 && existing_tid == src->DataTypeId)
          { AppSetErr(err, err_size, "already depends on this data type"); return false; }
        }
      }
      // Cycle guard: edge is producer->consumer. Illegal if consumer already reaches producer.
      if (AppGraphDataReaches(g, dst_owner->Id, src_owner->Id))
      { AppSetErr(err, err_size, "would create a dependency cycle"); return false; }

      *out_src = src->Id; *out_dst = dst->Id; *out_kind = ImGuiAppEdgeKind_Data;
      return true;
    }

    if (src->Kind == ImGuiAppPortKind_ChildOut && dst->Kind == ImGuiAppPortKind_ChildIn)
    {
      // Containment: a node has at most one parent.
      for (int li = 0; li < g->Links.Size; li++)
        if (g->Links.Data[li].Kind == ImGuiAppEdgeKind_Containment && g->Links.Data[li].StartAttr == src->Id)
        { AppSetErr(err, err_size, "node already has a parent"); return false; }
      *out_src = src->Id; *out_dst = dst->Id; *out_kind = ImGuiAppEdgeKind_Containment;
      return true;
    }

    AppSetErr(err, err_size, "incompatible port kinds");
    return false;
  }

  bool AppGraphCanLink(ImGuiAppGraph* g, int start_port, int end_port, char* err, int err_size)
  {
    IM_ASSERT(g != nullptr);
    int s = 0, d = 0; ImGuiAppEdgeKind k = ImGuiAppEdgeKind_Data;
    return AppGraphResolveLink(g, start_port, end_port, &s, &d, &k, err, err_size);
  }

  // Programmatic connect via the same resolver as interactive linking; returns true if a link was added.
  static bool AppGraphTryConnect(ImGuiAppGraph* g, int port_a, int port_b)
  {
    int s = 0, d = 0; ImGuiAppEdgeKind k = ImGuiAppEdgeKind_Data;
    char err[8];
    if (!AppGraphResolveLink(g, port_a, port_b, &s, &d, &k, err, IM_ARRAYSIZE(err)))
      return false;
    ImGuiAppNodeLink link;
    link.Id = AppGraphAllocId(g);
    link.StartAttr = s;
    link.EndAttr = d;
    link.Kind = k;
    g->Links.push_back(link);
    return true;
  }

  // Field-type inference: when a Struct producer's output (src) is wired into a consumer's input (dst), fill the
  // first struct-typed field on the consumer that still has no type with the producer struct's name. Only ever
  // completes an unset type -- never overwrites a choice the user already made.
  static void AppInferStructFieldType(ImGuiAppGraph* g, int src_port, int dst_port)
  {
    ImGuiAppNode* prod = AppGraphFindNode(g, AppGraphPortOwnerId(g, src_port));
    ImGuiAppNode* cons = AppGraphFindNode(g, AppGraphPortOwnerId(g, dst_port));
    if (prod == nullptr || cons == nullptr || prod->Kind != ImGuiAppNodeKind_Struct)
      return;

    char tn[IM_LABEL_SIZE];
    AppNodeBaseName(prod, tn, IM_ARRAYSIZE(tn));
    ImVector<ImGuiAppFieldDesc>* lists[2] = { &cons->Draft.PersistFields, &cons->Draft.TempFields };
    for (int l = 0; l < 2; l++)
      for (int f = 0; f < lists[l]->Size; f++)
        if (lists[l]->Data[f].Type == ImGuiAppFieldType_Struct && lists[l]->Data[f].StructType[0] == 0)
        {
          ImStrncpy(lists[l]->Data[f].StructType, tn, IM_ARRAYSIZE(lists[l]->Data[f].StructType));
          return;
        }
  }

  // A wire-detach re-drag dropped on empty canvas must not open the drop-create palette (the gesture
  // means "delete", not "create here"). Set by the detach event below, consumed by the editor's
  // CanvasWireDropped handler.

  bool CaptureAppGraphLinks(ImGuiAppGraph* g, char* err, int err_size)
  {
    IM_ASSERT(g != nullptr);
    ImGuiCanvasState* cv = AppEditorCanvas(g);
    bool changed = false;
    if (err && err_size > 0) err[0] = 0;

    int sa = 0, ea = 0;
    if (ImGui::CanvasWireCreated(cv, &sa, &ea))
    {
      AppGraphEditorState(g)->DragWasDetach = false;
      int s = 0, d = 0; ImGuiAppEdgeKind k = ImGuiAppEdgeKind_Data;
      if (AppGraphResolveLink(g, sa, ea, &s, &d, &k, err, err_size))
      {
        ImGuiAppNodeLink link;
        link.Id = AppGraphAllocId(g);
        link.StartAttr = s;
        link.EndAttr = d;
        link.Kind = k;
        g->Links.push_back(link);
        if (k == ImGuiAppEdgeKind_Data)
          AppInferStructFieldType(g, s, d);   // wiring a struct producer in fills an unset struct field type
        if (k == ImGuiAppEdgeKind_Containment)
        {
          ImGuiAppNode* child = AppGraphFindNode(g, AppGraphPortOwnerId(g, s));
          ImGuiAppNode* parent = AppGraphFindNode(g, AppGraphPortOwnerId(g, d));
          if (child != nullptr && parent != nullptr)
          {
            const ImVec2 preferred = parent->GridPos + ImVec2(280.0f, 0.0f);
            AppGraphPlaceNode(g, child, &preferred);
          }
        }
        changed = true;
        g->LastLinkErr[0] = 0;                                  // a successful create silences any standing toast
      }
      else
      {
        // Drive the toast off the rejection branch, NOT the `changed` return (also true on link destroy);
        // bump the seq so identical back-to-back rejections still re-fire it. The channel carries
        // full sentences (composition notices share it), so the link prefix is stamped here.
        ImFormatString(g->LastLinkErr, IM_ARRAYSIZE(g->LastLinkErr), "link refused: %s", err);
        g->LastLinkErrSeq++;
      }
    }

    // Endpoint dragged off a pin: the wire dies NOW (the drag continues from the surviving end as a
    // pending wire -- releasing on a pin re-creates via the event above, on empty it just stays dead).
    int detached_wire = 0;
    int detached_grab = 0;
    if (ImGui::CanvasWireDetached(cv, &detached_wire, &detached_grab))
    {
      AppGraphEditorState(g)->DragWasDetach = true;
      for (int i = 0; i < g->Links.Size; i++)
        if (g->Links.Data[i].Id == detached_wire)
        {
          for (int bi = g->Bindings.Size - 1; bi >= 0; bi--)
            if (g->Bindings.Data[bi].LinkId == detached_wire)
              g->Bindings.erase(g->Bindings.Data + bi);
          g->Links.erase(g->Links.Data + i);
          changed = true;
          break;
        }
    }
    return changed;
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Per-edge field bindings editor
  //-----------------------------------------------------------------------------

  void EditAppDataEdgeBindings(ImGuiAppGraph* g, int link_id)
  {
    IM_ASSERT(g != nullptr);

    // Only data edges carry field bindings.
    bool is_data = false;
    for (int i = 0; i < g->Links.Size; i++)
      if (g->Links.Data[i].Id == link_id && g->Links.Data[i].Kind == ImGuiAppEdgeKind_Data) { is_data = true; break; }
    if (!is_data)
      return;

    ImGui::PushID(link_id);
    ImGui::TextDisabled("bindings");
    int remove = -1;
    int row = 0;
    for (int i = 0; i < g->Bindings.Size; i++)
    {
      if (g->Bindings.Data[i].LinkId != link_id)
        continue;
      ImGui::PushID(row++);
      ImGui::BeginGroup();
      AppBlInputText("##dst", g->Bindings.Data[i].DstField, IM_ARRAYSIZE(g->Bindings.Data[i].DstField), ImGui::GetFontSize() * 6.0f);
      ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("="); ImGui::SameLine();
      AppBlInputText("##src", g->Bindings.Data[i].SrcField, IM_ARRAYSIZE(g->Bindings.Data[i].SrcField), ImGui::GetFontSize() * 6.0f);
      ImGui::SameLine();
      if (AppRowDeleteButton("##del"))
        remove = i;
      ImGui::EndGroup();
      // Brushing: pointing at a binding row lights the wire it belongs to on the canvas.
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        AppGraphHoverLink(g, link_id, ImGuiAppHoverSource_Inspector);
      ImGui::PopID();
    }
    if (remove >= 0)
      g->Bindings.erase(g->Bindings.Data + remove);
    if (AppBlAddPill("Add binding", "Add binding"))
    {
      ImGuiAppFieldBinding b;
      b.LinkId = link_id;
      g->Bindings.push_back(b);
    }
    ImGui::PopID();
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Hover sync (brushing across coordinated views) + cached validation
  //-----------------------------------------------------------------------------
  // Brushing: hover in one view highlights the same datum in all views. Writers report this frame;
  // readers see last frame's report (one-frame latency, same idiom as the node title hover).


  static void AppHoverRotate(const ImGuiAppGraph* g)
  {
    const int fc = ImGui::GetFrameCount();
    ImGuiAppEditorState* ed = AppGraphEditorState(g);
    if (fc == ed->HoverFrame)
      return;
    const bool fresh = fc == ed->HoverFrame + 1;   // a skipped frame drops stale hover
    ed->HoverPrevNode    = fresh ? ed->HoverNode    : -1;
    ed->HoverPrevLink    = fresh ? ed->HoverLink    : -1;
    ed->HoverPrevNodeSrc = fresh ? ed->HoverNodeSrc : ImGuiAppHoverSource_None;
    ed->HoverPrevLinkSrc = fresh ? ed->HoverLinkSrc : ImGuiAppHoverSource_None;
    ed->HoverNode = -1;
    ed->HoverLink = -1;
    ed->HoverNodeSrc = ImGuiAppHoverSource_None;
    ed->HoverLinkSrc = ImGuiAppHoverSource_None;
    ed->HoverFrame = fc;
  }

  ImGuiAppGraphViewState* AppGraphViewState(ImGuiAppGraph* g)
  {
    return &AppGraphEditorState(g)->View;
  }

  // Engine passthroughs: model units everywhere; the camera is the engine's one transform.
  // Zoom = logical camera (font pushes, persisted view state); Scale = model -> screen pixels.
  static float AppCanvasZoom(const ImGuiAppGraph* g)
  {
    return ImGui::CanvasGetZoom(AppEditorCanvas(g));
  }
  static float AppCanvasScale(const ImGuiAppGraph* g)
  {
    return ImGui::CanvasGetScale(AppEditorCanvas(g));
  }
  static ImVec2 AppCanvasNodePos(const ImGuiAppGraph* g, int node_id)
  {
    return ImGui::CanvasNodePos(AppEditorCanvas(g), node_id);
  }
  static void AppCanvasSetNodePos(const ImGuiAppGraph* g, int node_id, const ImVec2& model)
  {
    ImGui::CanvasSetNodePos(AppEditorCanvas(g), node_id, model);
  }

  void AppGraphSetHostCommands(const ImGuiAppGraph* g, const ImGuiAppGraphHostCmd* cmds, int count)
  {
    AppGraphEditorState(g)->HostCmds = cmds;
    AppGraphEditorState(g)->HostCmdCount = cmds != nullptr ? count : 0;
  }

  int AppGraphConsumeHostCommand(const ImGuiAppGraph* g)
  {
    const int picked = AppGraphEditorState(g)->HostCmdPicked;
    AppGraphEditorState(g)->HostCmdPicked = -1;
    return picked;
  }

  void AppGraphRequestAddPalette(const ImGuiAppGraph* g) { AppGraphEditorState(g)->AddPaletteRequest = true; }
  void AppGraphRequestCmdPalette(const ImGuiAppGraph* g) { AppGraphEditorState(g)->CmdPaletteRequest = true; }

  void AppGraphRequestFitAll(const ImGuiAppGraph* g) { AppGraphEditorState(g)->FitAllRequest = true; }

  void AppGraphHoverNode(const ImGuiAppGraph* g, int node_id, ImGuiAppHoverSource source)
  {
    AppHoverRotate(g);
    if (node_id >= 0) { AppGraphEditorState(g)->HoverNode = node_id; AppGraphEditorState(g)->HoverNodeSrc = source; }
  }

  void AppGraphHoverLink(const ImGuiAppGraph* g, int link_id, ImGuiAppHoverSource source)
  {
    AppHoverRotate(g);
    if (link_id >= 0) { AppGraphEditorState(g)->HoverLink = link_id; AppGraphEditorState(g)->HoverLinkSrc = source; }
  }

  int AppGraphHoveredNode(const ImGuiAppGraph* g, ImGuiAppHoverSource* out_source)
  {
    AppHoverRotate(g);
    if (out_source != nullptr) *out_source = AppGraphEditorState(g)->HoverPrevNodeSrc;
    return AppGraphEditorState(g)->HoverPrevNode;
  }

  int AppGraphHoveredLink(const ImGuiAppGraph* g, ImGuiAppHoverSource* out_source)
  {
    AppHoverRotate(g);
    if (out_source != nullptr) *out_source = AppGraphEditorState(g)->HoverPrevLinkSrc;
    return AppGraphEditorState(g)->HoverPrevLink;
  }

  // Validation cache. AppGraphSignature covers everything codegen-visible; bindings are folded in on top
  // (they can carry issues of their own without changing the signature). Recomputes only on model change.

  const ImVector<ImGuiAppGraphIssue>* AppGraphIssuesCached(const ImGuiAppGraph* g)
  {
    IM_ASSERT(g != nullptr);
    ImGuiID sig = AppGraphSignature(g);
    for (int i = 0; i < g->Bindings.Size; i++)
    {
      sig = ImHashData(&g->Bindings.Data[i].LinkId, sizeof(int), sig);
      sig = ImHashStr(g->Bindings.Data[i].DstField, 0, sig);
      sig = ImHashStr(g->Bindings.Data[i].SrcField, 0, sig);
    }
    if (!AppGraphEditorState(g)->IssuesValid || sig != AppGraphEditorState(g)->IssuesSig)
    {
      AppGraphEditorState(g)->IssuesSig = sig;
      AppGraphEditorState(g)->IssuesValid = true;
      AppGraphEditorState(g)->IssuesCache.resize(0);
      AppGraphValidate(g, &AppGraphEditorState(g)->IssuesCache);
      AppGraphEditorState(g)->IssuesSeverity.Clear();
      for (int i = 0; i < AppGraphEditorState(g)->IssuesCache.Size; i++)
      {
        if (AppGraphEditorState(g)->IssuesCache.Data[i].NodeId < 0)
          continue;
        const ImGuiID key = (ImGuiID)AppGraphEditorState(g)->IssuesCache.Data[i].NodeId;
        if (AppGraphEditorState(g)->IssuesCache.Data[i].Severity > AppGraphEditorState(g)->IssuesSeverity.GetInt(key, 0))
          AppGraphEditorState(g)->IssuesSeverity.SetInt(key, AppGraphEditorState(g)->IssuesCache.Data[i].Severity);
      }
    }
    return &AppGraphEditorState(g)->IssuesCache;
  }

  int AppGraphNodeSeverity(const ImGuiAppGraph* g, int node_id)
  {
    AppGraphIssuesCached(g);
    return AppGraphEditorState(g)->IssuesSeverity.GetInt((ImGuiID)node_id, 0);
  }

  // Shared severity accents: same hue in canvas, outliner, and inspector.
  static ImU32 AppSeverityColor(int severity)
  {
    return severity >= 2 ? AppComposerGetStyle()->SevError : AppComposerGetStyle()->SevWarn;
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Whole-graph editor render
  //-----------------------------------------------------------------------------

  static const char* AppLayerTypeName(ImGuiAppLayerType t)
  {
    switch (t)
    {
    case ImGuiAppLayerType_Task:    return "ImGuiAppTaskLayer";
    case ImGuiAppLayerType_Command: return "ImGuiAppCommandLayer";
    case ImGuiAppLayerType_Status:  return "ImGuiAppStatusLayer";
    case ImGuiAppLayerType_Layout:  return "ImGuiAppLayoutLayer";
    case ImGuiAppLayerType_Display: return "ImGuiAppDisplayLayer";
    default:                        return "ImGuiAppLayer";
    }
  }

  static const char* AppLayerNodeName(ImGuiAppLayerType t)
  {
    switch (t)
    {
    case ImGuiAppLayerType_Task:    return "TaskLayer";
    case ImGuiAppLayerType_Command: return "CommandLayer";
    case ImGuiAppLayerType_Status:  return "StatusLayer";
    case ImGuiAppLayerType_Layout:  return "LayoutLayer";
    case ImGuiAppLayerType_Display: return "DisplayLayer";
    case ImGuiAppLayerType_Custom:  return "CustomLayer";
    default:                        return "Layer";
    }
  }

  // True for the four permanent framework phases (one each, type immutable, undeletable). Custom layers are
  // user-authored ImGuiAppLayer subclasses and get none of those guarantees.
  static bool AppLayerIsCore(ImGuiAppLayerType t)
  {
    return t == ImGuiAppLayerType_Task || t == ImGuiAppLayerType_Command
        || t == ImGuiAppLayerType_Status || t == ImGuiAppLayerType_Layout
        || t == ImGuiAppLayerType_Display;
  }

  // One identity per layer kind (icon, role line, accent), shared by the canvas node header, body, and
  // the pipeline group box.
  static const char* AppLayerIcon(ImGuiAppLayerType t)
  {
    switch (t)
    {
    case ImGuiAppLayerType_Task:    return ICON_FA_GEARS;
    case ImGuiAppLayerType_Command: return ICON_FA_TERMINAL;
    case ImGuiAppLayerType_Status:  return ICON_FA_CIRCLE_INFO;
    case ImGuiAppLayerType_Layout:  return ICON_FA_BORDER_ALL;
    case ImGuiAppLayerType_Display: return ICON_FA_TABLE_COLUMNS;
    default:                        return ICON_FA_LAYER_GROUP;
    }
  }

  // Layer roles use the module-interop vocabulary: the app is one module among independent modules (threads,
  // async workers, external processes). Per frame it INGESTS their status + updates its state (Task), handles
  // received commands (Command), PUBLISHES its own status (Status), then renders -- never mutating (Window).
  static const char* AppLayerRole(ImGuiAppLayerType t)
  {
    switch (t)
    {
    case ImGuiAppLayerType_Task:    return "ingests module status & updates app state";
    case ImGuiAppLayerType_Command: return "receives & dispatches commands";
    case ImGuiAppLayerType_Status:  return "publishes the app's own status";
    case ImGuiAppLayerType_Layout:  return "lays out the workspace -- dockspaces before windows";
    case ImGuiAppLayerType_Display: return "renders the world -- presentation only";
    case ImGuiAppLayerType_Custom:  return "your ImGuiAppLayer subclass, at its stack position";
    default:                        return "orchestration layer";
    }
  }

  static ImU32 AppLayerAccent(ImGuiAppLayerType t)
  {
    switch (t)
    {
    case ImGuiAppLayerType_Task:    return AppComposerGetStyle()->LayerTask;
    case ImGuiAppLayerType_Command: return AppComposerGetStyle()->LayerCommand;
    case ImGuiAppLayerType_Status:  return AppComposerGetStyle()->LayerStatus;
    case ImGuiAppLayerType_Layout:  return AppComposerGetStyle()->LayerLayout;
    case ImGuiAppLayerType_Display: return AppComposerGetStyle()->LayerDisplay;
    default:                        return AppComposerGetStyle()->AccentNeutral;
    }
  }

  // Font Awesome glyph per node kind (kind-only; layers get a generic layer-group glyph here).
  static const char* AppKindIcon(ImGuiAppNodeKind k)
  {
    switch (k)
    {
    case ImGuiAppNodeKind_Layer:   return ICON_FA_LAYER_GROUP;
    case ImGuiAppNodeKind_Window:  return ICON_FA_WINDOW_MAXIMIZE;
    case ImGuiAppNodeKind_Sidebar: return ICON_FA_TABLE_COLUMNS;
    case ImGuiAppNodeKind_Control: return ICON_FA_SLIDERS;
    case ImGuiAppNodeKind_Struct:  return ICON_FA_CUBE;
    case ImGuiAppNodeKind_Field:   return ICON_FA_TAG;
    default:                       return ICON_FA_CIRCLE_NODES;
    }
  }

  // Font Awesome glyph for a specific node (layers use their per-type icon). Used by the outliner rows.
  static const char* AppNodeIcon(const ImGuiAppNode* n)
  {
    if (n->Kind == ImGuiAppNodeKind_Layer)
      return AppLayerIcon(n->LayerType);
    return AppKindIcon(n->Kind);
  }

  static const char* AppNodeKindName(ImGuiAppNodeKind k)
  {
    switch (k)
    {
    case ImGuiAppNodeKind_App:     return "App";
    case ImGuiAppNodeKind_Layer:   return "Layer";
    case ImGuiAppNodeKind_Window:  return "Window";
    case ImGuiAppNodeKind_Sidebar: return "Sidebar";
    case ImGuiAppNodeKind_Control: return "Control";
    case ImGuiAppNodeKind_Struct:  return "Struct";
    case ImGuiAppNodeKind_Field:   return "Field";
    default:                       return "Node";
    }
  }

  // The data link feeding a given consumer node (its DataIn), if any -- used to host its binding editor.
  static int AppGraphIncomingDataLink(const ImGuiAppGraph* g, int consumer_node_id)
  {
    for (int li = 0; li < g->Links.Size; li++)
      if (g->Links.Data[li].Kind == ImGuiAppEdgeKind_Data && AppGraphPortOwnerId(g, g->Links.Data[li].EndAttr) == consumer_node_id)
        return g->Links.Data[li].Id;
    return -1;
  }

  static void AppNodeDataTypeName(const ImGuiAppNode* n, char* out, size_t out_size);   // fwd (defined below)
  static const ImGuiAppNode* AppGraphFindNodeConst(const ImGuiAppGraph* g, int node_id);   // fwd
  static int AppGraphParentOf(const ImGuiAppGraph* g, int child_node_id);                   // fwd

  // One origin vocabulary shared by the canvas title-bar tint, the tree text tint, and the demo legend, so the
  // three surfaces cannot drift. design = default (no push).
  ImU32 AppGraphOriginColor(const ImGuiAppNode* n)   // exposed so the demo legend reads the same constants
  {
    if (n->IsLive)     return AppComposerGetStyle()->OriginLive;
    if (n->IsPromoted) return AppComposerGetStyle()->OriginPromoted;
    return 0;
  }

  static ImU32 AppKindColor(ImGuiAppNodeKind k);   // fwd

  // Scale an RGB color's brightness, keeping alpha (Blender-style muted node headers).
  static ImU32 AppScaleRGB(ImU32 c, float s)
  {
    const int r = (int)(((c >> IM_COL32_R_SHIFT) & 0xFF) * s);
    const int g = (int)(((c >> IM_COL32_G_SHIFT) & 0xFF) * s);
    const int b = (int)(((c >> IM_COL32_B_SHIFT) & 0xFF) * s);
    return IM_COL32(ImMin(r, 255), ImMin(g, 255), ImMin(b, 255), 255);
  }

  // Title-bar tint (rides the engine's CanvasNextNodeTitle). One meaning per channel: the header
  // hue is the node's KIND, for every node -- origin (live/promoted) rides the title dot instead.
  // Selection legibility stays engine-side (outline).
  static ImU32 AppNodeTitleColor(const ImGuiAppNode* n)
  {
    const ImU32 raw = (n->Kind == ImGuiAppNodeKind_Layer) ? AppLayerAccent(n->LayerType) : AppKindColor(n->Kind);
    return AppScaleRGB(raw, 0.52f);
  }

  // The kind word shown muted in the title bar.
  static const char* AppNodeKindTag(ImGuiAppNodeKind k)
  {
    switch (k)
    {
    case ImGuiAppNodeKind_Layer:   return "layer";
    case ImGuiAppNodeKind_Window:  return "window";
    case ImGuiAppNodeKind_Sidebar: return "sidebar";
    case ImGuiAppNodeKind_Control: return "control";
    case ImGuiAppNodeKind_Struct:  return "struct";
    case ImGuiAppNodeKind_Field:   return "field";
    default:                       return "";
    }
  }

  // Node ids the editor submitted on the last completed frame -- the set whose geometry queries are
  // meaningful (the engine keeps last-known geometry for evicted nodes, but decorations must not frame
  // nodes that are currently hidden). Single editor instance, same assumption as the other statics.

  static bool AppEditorNodeWasSubmitted(const ImGuiAppGraph* g, int node_id)
  {
    for (int i = 0; i < AppGraphEditorState(g)->PoolIds.Size; i++)
      if (AppGraphEditorState(g)->PoolIds.Data[i] == node_id)
        return true;
    return false;
  }

  static bool AppTreeRowIcon(const char* icon, ImVec2 center, float r, ImU32 col, ImDrawList* dl_override = nullptr);   // fwd (defined with the outliner)
  static bool AppNodeHiddenByCollapse(const ImGuiAppGraph* g, int id);                                                  // fwd (defined with the scope helpers)

  // Status hint written by ShowAppGraphEditor, rendered by the host's status bar (AppGraphStatusHint).

  const char* AppGraphStatusHint(const ImGuiAppGraph* g, int* out_severity)
  {
    if (out_severity != nullptr)
      *out_severity = AppGraphEditorState(g)->StatusSev;
    return AppGraphEditorState(g)->StatusHint;
  }

  // F38 read-backs: the animated overlay alpha this frame + the last-drawn overlay geometry (screen).
  float AppGraphEditorOverlayAlpha(const ImGuiAppGraph* g)
  {
    return AppGraphEditorState(g)->OverlayAlpha;
  }
  void AppGraphEditorGizmoRect(const ImGuiAppGraph* g, ImVec2* out_min, ImVec2* out_max)
  {
    if (out_min != nullptr) *out_min = AppGraphEditorState(g)->GizmoRectMin;
    if (out_max != nullptr) *out_max = AppGraphEditorState(g)->GizmoRectMax;
  }
  void AppGraphEditorCanvasRect(const ImGuiAppGraph* g, ImVec2* out_min, ImVec2* out_max)
  {
    if (out_min != nullptr) *out_min = AppGraphEditorState(g)->EditorRectMin;
    if (out_max != nullptr) *out_max = AppGraphEditorState(g)->EditorRectMax;
  }
  int AppGraphEditorGizmoCount(const ImGuiAppGraph* g)
  {
    return AppGraphEditorState(g)->GizmoCount;
  }
  ImVec2 AppGraphEditorGizmoCenter(const ImGuiAppGraph* g, int index)
  {
    const ImGuiAppEditorState* ed = AppGraphEditorState(g);
    if (index < 0 || index >= ed->GizmoCount || index >= IM_ARRAYSIZE(ed->GizmoCenters))
      return ImVec2(0.0f, 0.0f);
    return ed->GizmoCenters[index];
  }

  // Draw INSIDE the canvas, between CanvasBegin and the first node, on the engine's background channel:
  // Stroke the current path with consecutive duplicate points removed first. PathArcTo samples
  // its start point, which duplicates the path's previous point at every line->arc and arc->arc
  // joint; AddPolyline's join normals degenerate on zero-length segments and render a width
  // BULGE there. Dedup keeps the stroke width constant along the whole wire.
  static void AppWirePathStroke(ImDrawList* dl, ImU32 col, float th)
  {
    ImVector<ImVec2>& p = dl->_Path;
    int w = 1;
    for (int i = 1; i < p.Size; i++)
      if (ImLengthSqr(p.Data[i] - p.Data[w - 1]) > 0.25f)
        p.Data[w++] = p.Data[i];
    if (p.Size > 0)
      p.Size = w;
    dl->PathStroke(col, ImDrawFlags_None, th);
  }

  // Stroke an axis-aligned waypoint path as SEQUENTIAL TANGENT ARCS: every 90-degree corner
  // becomes a quarter arc sized to the room its two segments allow (a terminal segment gives its
  // corner the full length, a shared segment half). `r_caps` (optional, one per corner) bounds a
  // corner's radius from ABOVE: a fillet is inscribed, so it always cuts INSIDE its corner --
  // when the corner rounds an obstacle, the cap is what keeps the arc out of the obstacle.
  static void AppDrawWireArcPath(ImDrawList* dl, const ImVec2* pts, int count, const float* r_caps, ImU32 col, float th)
  {
    if (count < 2)
      return;
    dl->PathClear();
    dl->PathLineTo(pts[0]);
    for (int i = 1; i + 1 < count; i++)
    {
      const ImVec2 a = pts[i - 1];
      const ImVec2 b = pts[i];
      const ImVec2 c = pts[i + 1];
      ImVec2 din = b - a;
      ImVec2 dout = c - b;
      const float lin = ImSqrt(din.x * din.x + din.y * din.y);
      const float lout = ImSqrt(dout.x * dout.x + dout.y * dout.y);
      if (lin < 1.0f || lout < 1.0f)
        continue;
      din = din * (1.0f / lin);
      dout = dout * (1.0f / lout);
      const float avail_in = (i == 1) ? lin : lin * 0.5f;
      const float avail_out = (i + 2 == count) ? lout : lout * 0.5f;
      float r = ImMin(avail_in, avail_out);
      if (r_caps != nullptr && r_caps[i - 1] < r)
        r = r_caps[i - 1];
      if (r < 2.0f)
      {
        dl->PathLineTo(b);
        continue;
      }
      // Quarter arc inscribed at the corner: tangent to both segments, center perpendicular-in.
      const ImVec2 arc_in = b - din * r;
      const ImVec2 arc_out = b + dout * r;
      const ImVec2 center = arc_in + dout * r;
      float a0 = ImAtan2(arc_in.y - center.y, arc_in.x - center.x);
      float a1 = ImAtan2(arc_out.y - center.y, arc_out.x - center.x);
      if (a1 - a0 > IM_PI)
        a1 -= 2.0f * IM_PI;
      if (a0 - a1 > IM_PI)
        a1 += 2.0f * IM_PI;
      dl->PathArcTo(center, r, a0, a1, 0);
    }
    dl->PathLineTo(pts[count - 1]);
    AppWirePathStroke(dl, col, th);
  }

  // This frame's layer-column geometry, published by AppDrawLayerGroupBox for consumers in the
  // same pass (trunk routing): the LAYER NODE rects ARE the obstacle set -- one producer per
  // value, never re-derived (docs/phase-coherence.md rule 3). Screen space, this frame's camera.
  struct AppLayerColumnGeom
  {
    bool   Valid = false;
    float  NodeRight = 0.0f;    // right edge shared by the layer nodes
    float  SilRight = 0.0f;     // the SILHOUETTE's right edge: nodes, seated members, AND the
                                // section boundary -- the outermost thing a wire must clear
    ImVec2 BoxMin = ImVec2(0.0f, 0.0f);   // the layer GROUP's boundary (the App Layers box)
    ImVec2 BoxMax = ImVec2(0.0f, 0.0f);
    int    RowCount = 0;
    float  RowTop[16] = {};     // per layer NODE, sorted top to bottom
    float  RowBot[16] = {};     // the NODE's bottom edge
    float  RowSecBot[16] = {};  // the row's SECTION bottom (band incl. seated members)
  };

  // grid -> this box -> nodes, so the grid can never show through the box/bands and the bands sit under
  // the nodes. Caller wraps this in the zoomed font (decorations size against em like node content).
  // Always publishes `out_geom`; `draw` false computes geometry only (overlay hidden).
  static void AppDrawLayerGroupBox(const ImGuiAppGraph* g, bool show_live, bool draw, float em_base, float fh_base, AppLayerColumnGeom* out_geom)
  {
    // Per visible layer: screen y-span + accent for the flow rail and phase bands.
    ImGuiCanvasState* cv = AppEditorCanvas(g);
    const float  z = AppCanvasScale(g);

    // Top/Bot span the LAYER node (rail badge geometry); BandBot extends to the bottom of member
    // nodes folded into the section (phase-band geometry).
    struct LRow { float Top; float Bot; float BandBot; ImU32 Accent; ImGuiAppLayerType LT; };
    ImVector<LRow> rows;
    bool any = false;
    ImVec2 bb_min(FLT_MAX, FLT_MAX);
    ImVec2 bb_max(-FLT_MAX, -FLT_MAX);
    float node_left = FLT_MAX;
    float node_right = -FLT_MAX;
    bool have_wl = false;
    ImVec2 wl_min(0.0f, 0.0f);
    ImVec2 wl_max(0.0f, 0.0f);
    const ImGuiAppNode* wl_canonical = AppGraphLayerOfType(g, ImGuiAppLayerType_Display);
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (n->Kind != ImGuiAppNodeKind_Layer)
        continue;
      if (!show_live && n->IsLive)
        continue;
      if (!n->HasGridPos && !AppEditorNodeWasSubmitted(g, n->Id))
        continue;   // neither a settled model position nor a first measurement yet
      const ImVec2 pos = ImGui::CanvasToScreen(cv, n->GridPos);
      ImVec2 m;
      if (!AppNodeModelSize(g, n->Id, &m))
        m = ImVec2(AppLayerUniformW(g), kAppGraphLayerRowH);
      const ImVec2 size = m * z;
      bb_min.y = ImMin(bb_min.y, pos.y);
      bb_max.x = ImMax(bb_max.x, pos.x + size.x);
      bb_max.y = ImMax(bb_max.y, pos.y + size.y);
      node_left = ImMin(node_left, pos.x);
      node_right = ImMax(node_right, pos.x + size.x);
      if (n == wl_canonical)
      {
        have_wl = true;
        wl_min = pos;
        wl_max = pos + size;
      }
      LRow r; r.Top = pos.y; r.Bot = pos.y + size.y; r.BandBot = r.Bot; r.Accent = AppLayerAccent(n->LayerType); r.LT = n->LayerType;
      rows.push_back(r);
      any = true;
    }
    if (!any)
      return;

    // Sort rows top -> bottom (execution order).
    for (int a = 0; a < rows.Size; a++)
      for (int b = a + 1; b < rows.Size; b++)
        if (rows.Data[b].Top < rows.Data[a].Top)
        {
          const LRow t = rows.Data[a];
          rows.Data[a] = rows.Data[b];
          rows.Data[b] = t;
        }

    // Windows and sidebars compose into the Display layer: fold each section member into the
    // row's band and the box, so the band reads as the SECTION CONTAINING the member nodes, not
    // a strip beside them. The rail gutter keeps its layer-column x (node_left untouched).
    LRow* wr = nullptr;
    float sect_right = -FLT_MAX;
    for (int r = 0; r < rows.Size && wr == nullptr; r++)
      if (rows.Data[r].LT == ImGuiAppLayerType_Display)
        wr = &rows.Data[r];
    if (wr != nullptr)
    {
      for (int i = 0; i < g->Nodes.Size; i++)
      {
        const ImGuiAppNode* n = &g->Nodes.Data[i];
        if (n->Kind != ImGuiAppNodeKind_Window && n->Kind != ImGuiAppNodeKind_Sidebar)
          continue;
        if ((!show_live && n->IsLive) || AppNodeHiddenByCollapse(g, n->Id))
          continue;
        if (!n->HasGridPos && !AppEditorNodeWasSubmitted(g, n->Id))
          continue;
        const ImVec2 pos = ImGui::CanvasToScreen(cv, n->GridPos);
        ImVec2 m;
        if (!AppNodeModelSize(g, n->Id, &m))
          m = AppLayoutNodeSize(g, n);
        const ImVec2 size = m * z;
        wr->BandBot = ImMax(wr->BandBot, pos.y + size.y);
        sect_right = ImMax(sect_right, pos.x + size.x);
        bb_max.x = ImMax(bb_max.x, pos.x + size.x);
        bb_max.y = ImMax(bb_max.y, pos.y + size.y);
      }
    }

    const float em = ImGui::GetFontSize();   // already zoom-scaled: this draws under the canvas content font
    // Published bounds (BoxMin/Max, SilRight) get chrome from unzoomed metrics x zoom; the pushed
    // font rounds + clamps per zoom and must not size bounds (docs/phase-coherence.md rule 1).
    const float em_b = em_base * ImGui::CanvasGetZoom(cv);
    const float pad = em_b * 0.75f;
    const float rail_w = em_b * 2.0f;    // left gutter housing the numbered execution-order rail
    const float title_h = fh_base * ImGui::CanvasGetZoom(cv);
    bb_min.x = node_left - pad - rail_w;
    bb_min.y -= title_h + pad;
    bb_max.x += pad;
    bb_max.y += pad;

    if (out_geom != nullptr)
    {
      out_geom->Valid = true;
      out_geom->NodeRight = node_right;
      // Seated members can outgrow the layer nodes; the section boundary wraps them at +0.25em.
      // The silhouette edge is the outermost of all of it -- what a wire must actually clear.
      out_geom->SilRight = ImMax(node_right, sect_right > -FLT_MAX ? sect_right + em_b * 0.25f : node_right);
      out_geom->BoxMin = bb_min;
      out_geom->BoxMax = bb_max;
      out_geom->RowCount = ImMin(rows.Size, (int)IM_ARRAYSIZE(out_geom->RowTop));
      for (int i = 0; i < out_geom->RowCount; i++)
      {
        out_geom->RowTop[i] = rows.Data[i].Top;
        out_geom->RowBot[i] = rows.Data[i].Bot;
        out_geom->RowSecBot[i] = rows.Data[i].BandBot;
      }
    }
    if (!draw)
      return;

    ImDrawList* dl = ImGui::CanvasBackgroundDrawList(cv);
    const ImU32 fill = AppComposerGetStyle()->GroupFill;
    const ImU32 outline = AppComposerGetStyle()->GroupOutline;
    const ImU32 title_bg = AppComposerGetStyle()->GroupTitleBg;   // opaque: grid must not bleed through text
    const ImU32 title_fg = ImGui::GetColorU32(ImGuiCol_Text);
    const float rounding = em * 0.25f;

    dl->AddRectFilled(bb_min, bb_max, fill, rounding);
    dl->AddRect(bb_min, bb_max, outline, rounding, 0, ImMax(1.0f, em * 0.09375f));

    // Phase bands: a faint accent-tinted strip behind each layer, spanning the WHOLE row -- rail badge
    // through node edge -- so badge + node read as one phase section. Bands tile: each section runs
    // to the NEXT section's top (the last to its own bottom), so the column has no untinted holes
    // however far apart the nodes sit. Only the outer corners round; seams stay square.
    const float band_x0 = bb_min.x + pad * 0.4f;
    const float band_x1 = bb_max.x - pad;
    for (int i = 0; i < rows.Size; i++)
    {
      const ImU32 band = (rows.Data[i].Accent & 0x00FFFFFF) | (IM_COL32(0, 0, 0, 26) & 0xFF000000);
      const float y0 = rows.Data[i].Top - em * 0.125f;
      const float y1 = i + 1 < rows.Size ? rows.Data[i + 1].Top - em * 0.125f : rows.Data[i].BandBot + em * 0.125f;
      ImDrawFlags rf = ImDrawFlags_RoundCornersNone;
      if (i == 0)
        rf |= ImDrawFlags_RoundCornersTop;
      if (i == rows.Size - 1)
        rf |= ImDrawFlags_RoundCornersBottom;
      dl->AddRectFilled(ImVec2(band_x0, y0), ImVec2(band_x1, y1), band, em * 0.1875f, rf);
    }

    // One boundary around the Display layer node and its seated section stack: contained
    // windows/sidebars read as INSIDE the layer's section, never floating beneath its border.
    if (wr != nullptr && have_wl && wr->BandBot > wl_max.y + 0.5f)
    {
      const ImU32 accent = AppLayerAccent(ImGuiAppLayerType_Display);
      const ImU32 sect_fill = (accent & 0x00FFFFFF) | (IM_COL32(0, 0, 0, 22) & 0xFF000000);
      const ImU32 sect_line = (accent & 0x00FFFFFF) | (IM_COL32(0, 0, 0, 160) & 0xFF000000);
      const ImVec2 smn(wl_min.x - em * 0.25f, wl_min.y - em * 0.25f);
      const ImVec2 smx(ImMax(wl_max.x, sect_right) + em * 0.25f, wr->BandBot + em * 0.375f);
      dl->AddRectFilled(smn, smx, sect_fill, rounding);
      dl->AddRect(smn, smx, sect_line, rounding, 0, ImMax(1.0f, em * 0.09375f));
    }

    // Execution-order rail: a vertical flow spine through numbered accent-filled circles at each layer's center.
    const float rail_cx = bb_min.x + rail_w * 0.55f;
    const float r = em * 0.62f;
    if (rows.Size > 1)
    {
      const float y0 = (rows.Data[0].Top + rows.Data[0].Bot) * 0.5f;
      const float y1 = (rows.Data[rows.Size - 1].Top + rows.Data[rows.Size - 1].Bot) * 0.5f;
      dl->AddLine(ImVec2(rail_cx, y0), ImVec2(rail_cx, y1), AppComposerGetStyle()->RailLine, ImMax(1.0f, em * 0.125f));
    }
    for (int i = 0; i < rows.Size; i++)
    {
      const float cy = (rows.Data[i].Top + rows.Data[i].Bot) * 0.5f;
      // Downward arrowhead between this circle and the next (flow direction).
      if (i + 1 < rows.Size)
      {
        const float ay = (rows.Data[i].Bot + rows.Data[i + 1].Top) * 0.5f;
        const float s = em * 0.26f;
        const ImU32 arr = AppColWithAlpha(AppComposerGetStyle()->RailLine, 0.70f);
        dl->AddTriangleFilled(ImVec2(rail_cx - s, ay - s), ImVec2(rail_cx + s, ay - s), ImVec2(rail_cx, ay + s), arr);
      }
      dl->AddCircleFilled(ImVec2(rail_cx, cy), r, AppScaleRGB(rows.Data[i].Accent, 0.85f));
      dl->AddCircle(ImVec2(rail_cx, cy), r, AppComposerGetStyle()->DarkOutline, 0, ImMax(1.0f, em * 0.09375f));
      char num[8];
      ImFormatString(num, IM_ARRAYSIZE(num), "%d", i + 1);
      const ImVec2 ns = ImGui::CalcTextSize(num);
      dl->AddText(ImVec2(rail_cx - ns.x * 0.5f, cy - ns.y * 0.5f), AppComposerGetStyle()->TextOnAccent, num);
    }

    const char* title = ICON_FA_LAYER_GROUP "  App Layers";
    const ImVec2 text_size = ImGui::CalcTextSize(title);
    const ImVec2 title_min = ImVec2(bb_min.x + pad, bb_min.y);
    const ImVec2 title_max = ImVec2(title_min.x + text_size.x + pad * 1.0f, bb_min.y + title_h);
    dl->AddRectFilled(title_min, title_max, title_bg, em * 0.1875f);
    dl->AddText(ImVec2(title_min.x + pad * 0.5f, title_min.y + (title_h - text_size.y) * 0.5f), title_fg, title);
  }

  static bool AppHandleLayerVerticalDrag(ImGuiAppGraph* g, bool show_live, int* out_anchor_id, ImVec2* out_anchor_pos)
  {
    // Per-graph drag transients. Clamp edges captured at grab (neighbors move during the drag):
    // top layer -> top of the node below; bottom layer -> bottom of the node above.

    if (out_anchor_id != nullptr) *out_anchor_id = 0;
    if (out_anchor_pos != nullptr) *out_anchor_pos = ImVec2(0.0f, 0.0f);

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
      g->_LayerDragId = 0;

    const int hovered = ImGui::CanvasHoveredNode(AppEditorCanvas(g));   // last CanvasEnd's resolution
    if (g->_LayerDragId == 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered >= 0)
    {
      ImGuiAppNode* n = AppGraphFindNodeById(g, hovered);
      if (n != nullptr && n->Kind == ImGuiAppNodeKind_Layer && (show_live || !n->IsLive))
      {
        g->_LayerDragId = n->Id;
        g->_LayerDragMouseY0 = ImGui::GetIO().MousePos.y;
        g->_LayerDragNodeY0 = AppCanvasNodePos(g, n->Id).y;   // model units, like the clamps below

        // Capture the immediate neighbor edges by POSITION at grab time. The dragged node may approach
        // but not overlap either neighbor, so the constrain never shoves the stack.
        //   - nearest node ABOVE  -> bottom of dragged stays >  that node's bottom  (min_y)
        //   - nearest node BELOW  -> bottom of dragged stays <  that node's top     (max_y)
        // A missing neighbor leaves the bound at +/-inf.
        g->_LayerDragMaxY = FLT_MAX;
        g->_LayerDragMinY = -FLT_MAX;
        const float self_h = AppGraphLayerNodeHeight(g, n->Id);
        float above_y = -FLT_MAX, above_bottom = 0.0f;
        float below_y = FLT_MAX;
        for (int i = 0; i < g->Nodes.Size; i++)
        {
          const ImGuiAppNode* o = &g->Nodes.Data[i];
          if (o->Id == n->Id || o->Kind != ImGuiAppNodeKind_Layer || (!show_live && o->IsLive))
            continue;
          if (o->GridPos.y < g->_LayerDragNodeY0 && o->GridPos.y > above_y)
          {
            above_y = o->GridPos.y;
            above_bottom = o->GridPos.y + AppGraphLayerNodeHeight(g, o->Id);
          }
          if (o->GridPos.y > g->_LayerDragNodeY0 && o->GridPos.y < below_y)
            below_y = o->GridPos.y;
        }
        // Must equal the inter-node gap the master constrain enforces: a tighter clamp lets the dragged
        // node enter the constrain's gap zone, and the constrain shoves the whole stack to restore spacing.
        const float gap = 12.0f;
        if (above_y != -FLT_MAX) g->_LayerDragMinY = above_bottom + gap;
        if (below_y != FLT_MAX)  g->_LayerDragMaxY = below_y - self_h - gap;
      }
    }

    if (g->_LayerDragId != 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
    {
      ImGuiAppNode* n = AppGraphFindNodeById(g, g->_LayerDragId);
      if (n != nullptr)
      {
        float y = g->_LayerDragNodeY0 + (ImGui::GetIO().MousePos.y - g->_LayerDragMouseY0) / AppCanvasScale(g);
        if (y > g->_LayerDragMaxY) y = g->_LayerDragMaxY;
        if (y < g->_LayerDragMinY) y = g->_LayerDragMinY;
        const ImVec2 pos(kAppGraphX0, y);
        AppGraphConstrainLayerColumn(g, show_live, n->Id, &pos);
        // Same pre-submission pass as the constrain: members must submit at THIS frame's row
        // origin, not trail it by a frame (docs/phase-coherence.md rule 1).
        AppGraphSeatWindowSection(g, show_live);

        const ImVec2 accepted_pos = n->GridPos;
        if (out_anchor_id != nullptr) *out_anchor_id = n->Id;
        if (out_anchor_pos != nullptr) *out_anchor_pos = accepted_pos;
        return true;
      }
    }
    return false;
  }

  static const char*    kAppDockDirNames[] = { "Left", "Right", "Up", "Down" };
  static const ImGuiDir kAppDockDirs[]     = { ImGuiDir_Left, ImGuiDir_Right, ImGuiDir_Up, ImGuiDir_Down };

  // Codegen: the C++ enum spelling for a sidebar dock direction.
  static const char* AppDirEnumName(ImGuiDir dir)
  {
    switch (dir)
    {
    case ImGuiDir_Left:  return "ImGuiDir_Left";
    case ImGuiDir_Right: return "ImGuiDir_Right";
    case ImGuiDir_Up:    return "ImGuiDir_Up";
    case ImGuiDir_Down:  return "ImGuiDir_Down";
    default:             return "ImGuiDir_Down";
    }
  }

  // Codegen: the C++ enum spelling for a style var, indexed by ImGuiStyleVar value. This imgui has no
  // GetStyleVarName, so the table is ours; the IM_STATIC_ASSERT pins it to the enum so an imgui upgrade that
  // adds/reorders vars fails the build here instead of silently mislabeling.
  static const char* kAppStyleVarEnumNames[] =
  {
    "ImGuiStyleVar_Alpha",
    "ImGuiStyleVar_DisabledAlpha",
    "ImGuiStyleVar_WindowPadding",
    "ImGuiStyleVar_WindowRounding",
    "ImGuiStyleVar_WindowBorderSize",
    "ImGuiStyleVar_WindowMinSize",
    "ImGuiStyleVar_WindowTitleAlign",
    "ImGuiStyleVar_ChildRounding",
    "ImGuiStyleVar_ChildBorderSize",
    "ImGuiStyleVar_PopupRounding",
    "ImGuiStyleVar_PopupBorderSize",
    "ImGuiStyleVar_FramePadding",
    "ImGuiStyleVar_FrameRounding",
    "ImGuiStyleVar_FrameBorderSize",
    "ImGuiStyleVar_ItemSpacing",
    "ImGuiStyleVar_ItemInnerSpacing",
    "ImGuiStyleVar_IndentSpacing",
    "ImGuiStyleVar_CellPadding",
    "ImGuiStyleVar_ScrollbarSize",
    "ImGuiStyleVar_ScrollbarRounding",
    "ImGuiStyleVar_ScrollbarPadding",
    "ImGuiStyleVar_GrabMinSize",
    "ImGuiStyleVar_GrabRounding",
    "ImGuiStyleVar_ImageRounding",
    "ImGuiStyleVar_ImageBorderSize",
    "ImGuiStyleVar_TabRounding",
    "ImGuiStyleVar_TabBorderSize",
    "ImGuiStyleVar_TabMinWidthBase",
    "ImGuiStyleVar_TabMinWidthShrink",
    "ImGuiStyleVar_TabBarBorderSize",
    "ImGuiStyleVar_TabBarOverlineSize",
    "ImGuiStyleVar_TableAngledHeadersAngle",
    "ImGuiStyleVar_TableAngledHeadersTextAlign",
    "ImGuiStyleVar_TreeLinesSize",
    "ImGuiStyleVar_TreeLinesRounding",
    "ImGuiStyleVar_MenuItemRounding",
    "ImGuiStyleVar_SelectableRounding",
    "ImGuiStyleVar_DragDropTargetRounding",
    "ImGuiStyleVar_ButtonTextAlign",
    "ImGuiStyleVar_SelectableTextAlign",
    "ImGuiStyleVar_SeparatorSize",
    "ImGuiStyleVar_SeparatorTextBorderSize",
    "ImGuiStyleVar_SeparatorTextAlign",
    "ImGuiStyleVar_SeparatorTextPadding",
    "ImGuiStyleVar_DockingSeparatorSize",
  };
  IM_STATIC_ASSERT(IM_ARRAYSIZE(kAppStyleVarEnumNames) == ImGuiStyleVar_COUNT);

  static const char* AppStyleVarEnumName(int v)
  {
    return (v >= 0 && v < ImGuiStyleVar_COUNT) ? kAppStyleVarEnumNames[v] : kAppStyleVarEnumNames[0];
  }

  // Inspector display: the enum spelling minus its "ImGuiStyleVar_" prefix.
  static const char* AppStyleVarShortName(int v)
  {
    return AppStyleVarEnumName(v) + (sizeof("ImGuiStyleVar_") - 1);
  }

  // Seed a mod's value from the CURRENT style, so a freshly added/retargeted row starts at "no visible change"
  // instead of a jarring zero.
  static void AppStyleModSeedValue(ImGuiAppStyleModDesc* sm)
  {
    const ImGuiStyleVarInfo* info = ImGui::GetStyleVarInfo(sm->Var);
    const float* v = (const float*)info->GetVarPtr(&ImGui::GetStyle());
    sm->Value = (info->Count == 2) ? ImVec2(v[0], v[1]) : ImVec2(v[0], 0.0f);
  }

  // Editable style-var override rows for a Window/Sidebar/Control node. Rows land in the item's StyleMods at
  // bring-up (SetupApp), where the runtime pushes Active entries around the item's submission.
  static void EditAppNodeStyleMods(ImGuiAppNode* n)
  {
    const float em = ImGui::GetFontSize();
    int remove = -1;
    for (int i = 0; i < n->StyleMods.Size; i++)
    {
      ImGuiAppStyleModDesc* sm = &n->StyleMods.Data[i];
      ImGui::PushID(3000 + i);

      if (AppRowDeleteButton("##del"))
        remove = i;
      ImGui::SameLine();
      ImGui::Checkbox("##active", &sm->Active);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("active: pushed at runtime (a live app can toggle this without regenerating)");
      ImGui::SameLine();
      int var = sm->Var;
      if (AppBlEnum("##var", em * 11.0f, &var, AppStyleVarShortName, ImGuiStyleVar_COUNT) && var != sm->Var)
      {
        sm->Var = (ImGuiStyleVar)var;
        AppStyleModSeedValue(sm);
      }
      ImGui::SameLine();
      const ImGuiStyleVarInfo* info = ImGui::GetStyleVarInfo(sm->Var);
      ImGui::SetNextItemWidth(em * (info->Count == 2 ? 8.0f : 4.5f));
      if (info->Count == 2)
        ImGui::DragFloat2("##val", &sm->Value.x, 0.05f, 0.0f, 0.0f, "%.2f");
      else
        ImGui::DragFloat("##val", &sm->Value.x, 0.05f, 0.0f, 0.0f, "%.2f");
      // Right-click revert: back to the CURRENT style's value for this var.
      if (ImGui::BeginPopupContextItem("##rowreset"))
      {
        if (ImGui::MenuItem("Reset to current style value"))
          AppStyleModSeedValue(sm);
        ImGui::EndPopup();
      }

      ImGui::PopID();
    }
    if (remove >= 0)
      n->StyleMods.erase(n->StyleMods.Data + remove);

    int remove_col = -1;
    for (int i = 0; i < n->ColorMods.Size; i++)
    {
      ImGuiAppColorModDesc* cm = &n->ColorMods.Data[i];
      ImGui::PushID(3500 + i);

      if (AppRowDeleteButton("##del"))
        remove_col = i;
      ImGui::SameLine();
      ImGui::Checkbox("##active", &cm->Active);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("active: pushed at runtime (a live app can toggle this without regenerating)");
      ImGui::SameLine();
      int col = cm->Col;
      if (AppBlEnum("##col", em * 11.0f, &col, &ImGui::GetStyleColorName, ImGuiCol_COUNT) && col != cm->Col)
      {
        cm->Col = (ImGuiCol)col;
        cm->Value = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(cm->Col));   // seed: "no visible change"
      }
      ImGui::SameLine();
      ImVec4 c4 = ImGui::ColorConvertU32ToFloat4(cm->Value);
      if (ImGui::ColorEdit4("##val", &c4.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf))
        cm->Value = ImGui::ColorConvertFloat4ToU32(c4);
      if (ImGui::BeginPopupContextItem("##rowreset"))
      {
        if (ImGui::MenuItem("Reset to current style value"))
          cm->Value = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(cm->Col));
        ImGui::EndPopup();
      }

      ImGui::PopID();
    }
    if (remove_col >= 0)
      n->ColorMods.erase(n->ColorMods.Data + remove_col);

    if (AppBlAddPill("##addstyle", "Style"))
    {
      ImGuiAppStyleModDesc sm{ n->Kind == ImGuiAppNodeKind_Control ? ImGuiStyleVar_FrameRounding : ImGuiStyleVar_WindowRounding, ImVec2(0.0f, 0.0f), true };
      AppStyleModSeedValue(&sm);
      n->StyleMods.push_back(sm);
    }
    ImGui::SameLine();
    if (AppBlAddPill("##addcolor", "Color"))
    {
      const ImGuiCol col = n->Kind == ImGuiAppNodeKind_Control ? ImGuiCol_FrameBg : ImGuiCol_WindowBg;
      n->ColorMods.push_back(ImGuiAppColorModDesc{ col, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(col)), true });
    }
  }

  // Read-only echo of a live item's mirrored style/color mods.
  static void DrawAppNodeStyleMods(const ImGuiAppNode* n)
  {
    for (int i = 0; i < n->StyleMods.Size; i++)
    {
      const ImGuiAppStyleModDesc* sm = &n->StyleMods.Data[i];
      const ImGuiStyleVarInfo* info = ImGui::GetStyleVarInfo(sm->Var);
      if (info->Count == 2)
        ImGui::TextDisabled("%s %s = %.2f, %.2f", sm->Active ? "[on] " : "[off]", AppStyleVarShortName(sm->Var), sm->Value.x, sm->Value.y);
      else
        ImGui::TextDisabled("%s %s = %.2f", sm->Active ? "[on] " : "[off]", AppStyleVarShortName(sm->Var), sm->Value.x);
    }
    for (int i = 0; i < n->ColorMods.Size; i++)
    {
      const ImGuiAppColorModDesc* cm = &n->ColorMods.Data[i];
      ImGui::TextDisabled("%s %s = #%08X", cm->Active ? "[on] " : "[off]", ImGui::GetStyleColorName(cm->Col), cm->Value);
    }
  }

  // Editable node-body UI for a Sidebar's dock direction + size (ImGuiAppSidebarBase). Windows have no body props.
  static void EditAppWindowNodeProps(ImGuiAppNode* n)
  {
    if (n->Kind != ImGuiAppNodeKind_Sidebar)
      return;

    const float w = ImGui::GetFontSize() * 11.0f;
    int cur = 0;
    for (int i = 0; i < IM_ARRAYSIZE(kAppDockDirs); i++)
    {
      if (n->DockDir == kAppDockDirs[i])
      {
        cur = i;
        break;
      }
    }
    ImGui::SetNextItemWidth(w);
    if (ImGui::BeginCombo("dock", kAppDockDirNames[cur]))
    {
      for (int i = 0; i < IM_ARRAYSIZE(kAppDockDirs); i++)
      {
        if (ImGui::Selectable(kAppDockDirNames[i], cur == i))
        {
          n->DockDir = kAppDockDirs[i];
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(w);
    ImGui::DragFloat("size", &n->DockSize, 1.0f, 0.0f, 4096.0f, "%.0f");
  }

  // Read-only echo of a live Sidebar's mirrored dock props.
  static void DrawAppWindowNodeProps(const ImGuiAppNode* n)
  {
    if (n->Kind != ImGuiAppNodeKind_Sidebar)
      return;
    int cur = 0;
    for (int i = 0; i < IM_ARRAYSIZE(kAppDockDirs); i++)
    {
      if (n->DockDir == kAppDockDirs[i])
      {
        cur = i;
        break;
      }
    }
    ImGui::TextDisabled("dock: %s  size: %.0f", kAppDockDirNames[cur], n->DockSize);
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Inspector (component sections, style/color descs, project + multi-select)
  //-----------------------------------------------------------------------------

  // One reflected field's LIVE value as text (base = the running Persist/Temp struct).
  static void AppLiveFieldValueText(const ImGuiAppLiveFieldDesc* f, const void* base, char* out, int out_size)
  {
    if (base == nullptr)
    {
      ImStrncpy(out, "?", (size_t)out_size);
      return;
    }
    const char* p = (const char*)base + f->Offset;
    if (f->ElemTypeName != nullptr)
    {
      // ImVector member: Size is the leading int of the (Size, Capacity, Data) layout.
      ImFormatString(out, (size_t)out_size, "%d x %s", *(const int*)p, f->ElemTypeName);
      return;
    }
    switch (f->Kind)
    {
    case ImGuiAppLiveFieldKind_Bool:      ImFormatString(out, (size_t)out_size, "%s", *(const bool*)p ? "true" : "false"); break;
    case ImGuiAppLiveFieldKind_S32:       ImFormatString(out, (size_t)out_size, "%d", *(const int*)p); break;
    case ImGuiAppLiveFieldKind_U32:       ImFormatString(out, (size_t)out_size, "%u", *(const unsigned int*)p); break;
    case ImGuiAppLiveFieldKind_F32:       ImFormatString(out, (size_t)out_size, "%.4f", *(const float*)p); break;
    case ImGuiAppLiveFieldKind_F64:       ImFormatString(out, (size_t)out_size, "%.6f", *(const double*)p); break;
    case ImGuiAppLiveFieldKind_Vec2:      { const ImVec2* v = (const ImVec2*)p; ImFormatString(out, (size_t)out_size, "%.2f, %.2f", v->x, v->y); } break;
    case ImGuiAppLiveFieldKind_Vec4:      { const ImVec4* v = (const ImVec4*)p; ImFormatString(out, (size_t)out_size, "%.2f, %.2f, %.2f, %.2f", v->x, v->y, v->z, v->w); } break;
    case ImGuiAppLiveFieldKind_CharArray: { const int n = f->Size < out_size - 3 ? f->Size : out_size - 3; ImFormatString(out, (size_t)out_size, "\"%.*s\"", n, p); } break;
    default:
    {
      int len = ImFormatString(out, (size_t)out_size, "%d bytes:", f->Size);
      for (int i = 0; i < f->Size && i < 16 && len + 3 < out_size; i++)
        len += ImFormatString(out + len, (size_t)(out_size - len), " %02X", (unsigned char)p[i]);
      break;
    }
    }
  }

  // Read-only live field table for one struct (Persist or Temp) of a RUNNING control.
  // Values are read from the live instance every frame -- they change as the app runs.
  static void AppLiveFieldsTable(const char* str_id, const ImGuiAppControlBase* ctrl, bool temp_data, const void* base)
  {
    ImGuiAppLiveFieldDesc fields[64];
    const int n = ctrl->GetControlFields(fields, IM_ARRAYSIZE(fields), temp_data);
    if (n <= 0)
    {
      // Outside the field-enumeration contract (non-aggregate or tagged opaque).
      if (!ctrl->IsControlDataReflectable(temp_data))
        ImGui::TextDisabled("(opaque to reflection)");
      else
        ImGui::TextDisabled("(no members)");
      return;
    }
    if (!ctrl->IsControlDataReflectable(temp_data))
      ImGui::TextDisabled("(snapshot-opaque -- fields via build-time reflection)");
    if (ImGui::BeginTable(str_id, 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    {
      char val[256];
      for (int i = 0; i < n; i++)
      {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(fields[i].Name);
        ImGui::TableNextColumn();
        if (fields[i].Kind == ImGuiAppLiveFieldKind_CharArray)
          ImGui::TextDisabled("char[%d]", fields[i].Size);
        else
          ImGui::TextDisabled("%s", fields[i].TypeName);
        ImGui::TableNextColumn();
        AppLiveFieldValueText(&fields[i], base, val, IM_ARRAYSIZE(val));
        ImGui::TextUnformatted(val);
      }
      ImGui::EndTable();
    }
  }

  // Resolve a live mirror node back to the runtime object it reflects (the inverse of BuildAppLiveGraph's
  // keying: windows by label hash, sidebars by label hash + 1, controls by PersistData id).
  static ImGuiAppItemBase* AppGraphFindLiveItem(ImGuiApp* app, const ImGuiAppNode* n)
  {
    if (app == nullptr || n == nullptr || !n->IsLive)
      return nullptr;
    if (n->Kind == ImGuiAppNodeKind_Window)
    {
      for (int i = 0; i < app->Windows.Size; i++)
        if (AppConstantHash(app->Windows.Data[i]->Label[0] ? app->Windows.Data[i]->Label : "Window") == n->LiveKey)
          return app->Windows.Data[i];
    }
    else if (n->Kind == ImGuiAppNodeKind_Sidebar)
    {
      for (int i = 0; i < app->Sidebars.Size; i++)
        if (AppConstantHash(app->Sidebars.Data[i]->Label[0] ? app->Sidebars.Data[i]->Label : "Sidebar") + 1u == n->LiveKey)
          return app->Sidebars.Data[i];
    }
    else if (n->Kind == ImGuiAppNodeKind_Control)
    {
      ImGuiAppControlBase* found = nullptr;
      ImGui::ForEachAppControl(app, [&](ImGuiAppControlBase* ctrl, ImGuiAppWindowBase*)
      {
        if (found == nullptr && ctrl->GetControlDataID() == n->LiveKey)
          found = ctrl;
      });
      return found;
    }
    return nullptr;
  }

  // Style section: desc rows under a component header whose enable checkbox masters every row's Active
  // flag and whose kebab holds copy / paste / reset. The section clipboard is session-lived, value-typed.

  static void AppNodeStyleSection(ImGuiAppGraph* g, ImGuiAppNode* n)
  {
    const int total = n->StyleMods.Size + n->ColorMods.Size;
    bool any_active = false;
    for (int i = 0; i < n->StyleMods.Size && !any_active; i++) any_active = n->StyleMods.Data[i].Active;
    for (int i = 0; i < n->ColorMods.Size && !any_active; i++) any_active = n->ColorMods.Data[i].Active;

    bool enable = any_active;
    bool kebab = false;
    const bool open = AppInspectorSection("##sec_style", ICON_FA_PALETTE, "Style", total > 0 ? &enable : nullptr, &kebab);
    if (total > 0 && enable != any_active)
    {
      for (int i = 0; i < n->StyleMods.Size; i++) n->StyleMods.Data[i].Active = enable;
      for (int i = 0; i < n->ColorMods.Size; i++) n->ColorMods.Data[i].Active = enable;
    }
    if (kebab)
      ImGui::OpenPopup("##style_section_menu");
    if (ImGui::BeginPopup("##style_section_menu"))
    {
      if (ImGui::MenuItem("Copy section", nullptr, false, total > 0))
      {
        AppGraphEditorState(g)->StyleClipMods = n->StyleMods;
        AppGraphEditorState(g)->StyleClipCols = n->ColorMods;
        AppGraphEditorState(g)->StyleClipHas = true;
      }
      if (ImGui::MenuItem("Paste section", nullptr, false, AppGraphEditorState(g)->StyleClipHas))
      {
        n->StyleMods = AppGraphEditorState(g)->StyleClipMods;
        n->ColorMods = AppGraphEditorState(g)->StyleClipCols;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Reset (clear all)", nullptr, false, total > 0))
      {
        n->StyleMods.clear();
        n->ColorMods.clear();
      }
      ImGui::EndPopup();
    }
    if (open)
      EditAppNodeStyleMods(n);
  }

  // Multi-selection inspector: the Style section across every selected DESIGN node -- master enable over
  // all their descs, paste the section clipboard to all, clear all.
  void EditAppNodesInspectorMulti(ImGuiAppGraph* g)
  {
    IM_ASSERT(g != nullptr);
    int design = 0, styled = 0;
    bool any_active = false;
    for (int i = 0; i < g->Selection.Size; i++)
    {
      const ImGuiAppNode* n = AppGraphFindNodeConst(g, g->Selection.Data[i]);
      if (n == nullptr || n->IsLive)
        continue;
      design++;
      styled += n->StyleMods.Size + n->ColorMods.Size;
      for (int s = 0; s < n->StyleMods.Size && !any_active; s++) any_active = n->StyleMods.Data[s].Active;
      for (int s = 0; s < n->ColorMods.Size && !any_active; s++) any_active = n->ColorMods.Data[s].Active;
    }
    ImGui::TextDisabled("%d nodes selected  (%d design)", g->Selection.Size, design);
    ImGui::Separator();

    bool enable = any_active;
    bool kebab = false;
    const bool open = AppInspectorSection("##sec_style_multi", ICON_FA_PALETTE, "Style (all selected)", styled > 0 ? &enable : nullptr, &kebab);
    auto for_each_sel = [&](void (*fn)(ImGuiAppNode*, bool), bool arg)
    {
      for (int i = 0; i < g->Selection.Size; i++)
        if (ImGuiAppNode* n = AppGraphFindNode(g, g->Selection.Data[i]))
          if (!n->IsLive)
            fn(n, arg);
    };
    if (styled > 0 && enable != any_active)
      for_each_sel([](ImGuiAppNode* n, bool on)
      {
        for (int s = 0; s < n->StyleMods.Size; s++) n->StyleMods.Data[s].Active = on;
        for (int s = 0; s < n->ColorMods.Size; s++) n->ColorMods.Data[s].Active = on;
      }, enable);
    if (kebab)
      ImGui::OpenPopup("##multi_style_menu");
    if (ImGui::BeginPopup("##multi_style_menu"))
    {
      if (ImGui::MenuItem("Paste section to all", nullptr, false, AppGraphEditorState(g)->StyleClipHas))
        for (int i = 0; i < g->Selection.Size; i++)
          if (ImGuiAppNode* n = AppGraphFindNode(g, g->Selection.Data[i]))
            if (!n->IsLive)
            {
              n->StyleMods = AppGraphEditorState(g)->StyleClipMods;
              n->ColorMods = AppGraphEditorState(g)->StyleClipCols;
            }
      ImGui::Separator();
      if (ImGui::MenuItem("Clear style on all", nullptr, false, styled > 0))
        for_each_sel([](ImGuiAppNode* n, bool)
        {
          n->StyleMods.clear();
          n->ColorMods.clear();
        }, false);
      ImGui::EndPopup();
    }
    if (open)
      for (int i = 0; i < g->Selection.Size; i++)
        if (const ImGuiAppNode* n = AppGraphFindNodeConst(g, g->Selection.Data[i]))
          if (!n->IsLive)
            ImGui::TextDisabled("%s  --  %d style, %d color", n->Draft.Name[0] ? n->Draft.Name : "(unnamed)", n->StyleMods.Size, n->ColorMods.Size);
  }

  // Inspector for the selected node's authored data, as component sections; dispatches by kind. Live
  // mirror nodes are read-only except style Active flags, which write through to the running item when
  // the host passes its mirrored app (the one sanctioned live mutation -- it round-trips through the
  // mirror next frame, so model and runtime cannot desync).
  void EditAppNodeInspector(ImGuiAppGraph* g, int node_id)
  {
    EditAppNodeInspectorEx(g, node_id, nullptr);
  }

  void EditAppNodeInspectorEx(ImGuiAppGraph* g, int node_id, ImGuiApp* live_app)
  {
    IM_ASSERT(g != nullptr);

    ImGuiAppNode* n = node_id >= 0 ? AppGraphFindNode(g, node_id) : nullptr;
    if (n == nullptr)
    {
      ImGui::TextDisabled("Select a node to inspect.");
      return;
    }

    ImGui::PushID(n->Id);

    // Ambient problem marks: this node's validation issues, stated where the fix happens. The Problems
    // list stays the whole-graph index; here only the selected node's rows appear.
    if (AppGraphNodeSeverity(g, node_id) > 0)
    {
      const ImVector<ImGuiAppGraphIssue>* issues = AppGraphIssuesCached(g);
      for (int i = 0; i < issues->Size; i++)
      {
        if (issues->Data[i].NodeId != node_id)
          continue;
        ImGui::PushStyleColor(ImGuiCol_Text, AppSeverityColor(issues->Data[i].Severity));
        ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  %s", issues->Data[i].Text);
        ImGui::PopStyleColor();
      }
      ImGui::Separator();
    }

    if (n->IsLive)
    {
      ImGui::TextDisabled("%s  (live -- read-only mirror)", n->Draft.Name[0] ? n->Draft.Name : "(live)");
      ImGui::Separator();
      if (n->Kind == ImGuiAppNodeKind_Window || n->Kind == ImGuiAppNodeKind_Sidebar)
        DrawAppWindowNodeProps(n);
      else if (n->Kind == ImGuiAppNodeKind_Control && n->DataTypeName[0])
        ImGui::TextDisabled("data: %s", n->DataTypeName);

      // Live style: values are the mirror's echo, but the Active flags write THROUGH to the running item --
      // flip a checkbox, watch the app restyle, no regeneration. Falls back to the plain echo when the host
      // didn't pass its mirrored app (or the item vanished between frames).
      ImGuiAppItemBase* item = AppGraphFindLiveItem(live_app, n);

      // Live data: the RUNNING control's reflected members with their current values,
      // re-read every frame (this is the running binary's memory, not a draft).
      if (n->Kind == ImGuiAppNodeKind_Control && item != nullptr)
      {
        const ImGuiAppControlBase* ctrl = (const ImGuiAppControlBase*)item;
        const void* live_persist = nullptr;
        const void* live_temp = nullptr;
        ctrl->GetControlLiveData(&live_persist, &live_temp);
        if (AppInspectorSection("##sec_live_data", ICON_FA_DATABASE, "Data (live)", nullptr, nullptr))
          AppLiveFieldsTable("##live_persist", ctrl, false, live_persist);
        if (AppInspectorSection("##sec_live_temp", ICON_FA_BOLT, "Temp (live)", nullptr, nullptr))
          AppLiveFieldsTable("##live_temp", ctrl, true, live_temp);
      }
      if (item != nullptr && (item->StyleMods.Size > 0 || item->ColorMods.Size > 0))
      {
        if (AppInspectorSection("##sec_style_live", ICON_FA_PALETTE, "Style (live)", nullptr, nullptr))
        {
          for (int i = 0; i < item->StyleMods.Size; i++)
          {
            ImGuiAppStyleModDesc* sm = &item->StyleMods.Data[i];
            ImGui::PushID(4000 + i);
            ImGui::Checkbox("##on", &sm->Active);
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("live toggle: pushed (or not) starting next frame");
            ImGui::SameLine();
            const ImGuiStyleVarInfo* info = ImGui::GetStyleVarInfo(sm->Var);
            if (info->Count == 2)
              ImGui::TextDisabled("%s = %.2f, %.2f", AppStyleVarShortName(sm->Var), sm->Value.x, sm->Value.y);
            else
              ImGui::TextDisabled("%s = %.2f", AppStyleVarShortName(sm->Var), sm->Value.x);
            ImGui::PopID();
          }
          for (int i = 0; i < item->ColorMods.Size; i++)
          {
            ImGuiAppColorModDesc* cm = &item->ColorMods.Data[i];
            ImGui::PushID(4500 + i);
            ImGui::Checkbox("##on", &cm->Active);
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("live toggle: pushed (or not) starting next frame");
            ImGui::SameLine();
            ImGui::ColorButton("##swatch", ImGui::ColorConvertU32ToFloat4(cm->Value), ImGuiColorEditFlags_NoTooltip, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ImGui::GetStyleColorName(cm->Col));
            ImGui::PopID();
          }
        }
      }
      else if (n->StyleMods.Size > 0 || n->ColorMods.Size > 0)
      {
        ImGui::SeparatorText("style");
        DrawAppNodeStyleMods(n);
      }
      ImGui::PopID();
      return;
    }

    // Identity (F41): who this node is -- editable name (where a name is authorable) plus the kind and
    // stable id readout. Rides above the collapsible sections, always visible.
    ImGui::SeparatorText(ICON_FA_ID_CARD "  Identity");
    if (n->Kind != ImGuiAppNodeKind_Layer || n->LayerType == ImGuiAppLayerType_Custom)   // custom layer name = its class
    {
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
      ImGui::InputText("Name", n->Draft.Name, IM_ARRAYSIZE(n->Draft.Name));
    }
    else
      ImGui::TextDisabled("%s (core layer)", AppLayerNodeName(n->LayerType));
    ImGui::TextDisabled("%s  \xc2\xb7  id %d", AppNodeKindName(n->Kind), n->Id);   // kind + stable id
    ImGui::Separator();

    // Placement (F41): the node's authored root position -- fundamental identity, so it rides above the
    // collapsible sections (always visible). Layers are column-packed (position derived), no placement.
    if (n->Kind != ImGuiAppNodeKind_Layer)
    {
      ImGui::SeparatorText(ICON_FA_UP_DOWN_LEFT_RIGHT "  Placement");
      const float pw = ImGui::GetFontSize() * 5.0f;
      ImGui::SetNextItemWidth(pw);
      ImGui::DragFloat("X", &n->GridPos.x, 1.0f, 0.0f, 0.0f, "%.0f");
      ImGui::SetItemTooltip("Authored X on the root canvas (grid units).");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(pw);
      ImGui::DragFloat("Y", &n->GridPos.y, 1.0f, 0.0f, 0.0f, "%.0f");
      ImGui::SetItemTooltip("Authored Y on the root canvas (grid units).");
      ImGui::Separator();
    }

    switch (n->Kind)
    {
    case ImGuiAppNodeKind_Control:
      if (n->IsBuiltin)
        ImGui::TextDisabled("Builtin control: %s", n->DataTypeName[0] ? n->DataTypeName : n->TypeName);
      else
      {
        if (AppInspectorSection("##sec_fields", ICON_FA_TABLE_LIST, "Fields", nullptr, nullptr))
        {
          EditAppNodeFieldSection(g, n, 0, "Persist");   // routes to the Field nodes when the list is exploded
          EditAppNodeFieldSection(g, n, 1, "Temp");
        }
        if (AppInspectorSection("##sec_events", ICON_FA_BOLT, "Events", nullptr, nullptr))
          EditAppControlEvents(g, n);          // temp-vs-last-temp reactions (see ImGuiAppEventDesc)
      }
      AppNodeStyleSection(g, n);                  // style-var/color overrides applied around the control's render
      break;
    case ImGuiAppNodeKind_Window:
      AppNodeStyleSection(g, n);                  // style overrides applied around the window's Begin/End
      break;
    case ImGuiAppNodeKind_Sidebar:
      if (AppInspectorSection("##sec_dock", ICON_FA_THUMBTACK, "Dock", nullptr, nullptr))
        EditAppWindowNodeProps(n);             // dock direction / size
      AppNodeStyleSection(g, n);
      break;
    case ImGuiAppNodeKind_Struct:
      if (AppInspectorSection("##sec_fields", ICON_FA_TABLE_LIST, "Fields", nullptr, nullptr))
        EditAppNodeFieldSection(g, n, 0, "fields");   // routes to the Field nodes when the list is exploded
      break;
    case ImGuiAppNodeKind_Field:
    {
      const int sid = AppGraphParentOf(g, n->Id);
      const ImGuiAppNode* sn = sid >= 0 ? AppGraphFindNodeConst(g, sid) : nullptr;
      if (n->Draft.PersistFields.Size == 0)
        n->Draft.PersistFields.push_back(ImGuiAppFieldDesc());
      ImGui::TextDisabled("type");
      ImGui::SameLine();
      EditAppFieldTypeControls(&n->Draft.PersistFields.Data[0], ImGui::GetFontSize() * 5.0f, g);
      ImGui::TextDisabled("of struct: %s", sn != nullptr ? sn->Draft.Name : "(none)");
      break;
    }
    case ImGuiAppNodeKind_Layer:
      ImGui::TextDisabled("Layer: %s", AppLayerTypeName(n->LayerType));
      break;
    default:
      break;
    }
    ImGui::PopID();
  }

  static bool AppGraphHostsControl(const ImGuiAppGraph* g, int host_id);   // fwd

  // Node color by kind, shared by the outliner rows and the canvas group frames.
  static ImU32 AppKindColor(ImGuiAppNodeKind k)
  {
    switch (k)
    {
    case ImGuiAppNodeKind_Layer:   return AppComposerGetStyle()->KindLayer;
    case ImGuiAppNodeKind_Window:  return AppComposerGetStyle()->KindWindow;
    case ImGuiAppNodeKind_Sidebar: return AppComposerGetStyle()->KindSidebar;
    case ImGuiAppNodeKind_Control: return AppComposerGetStyle()->KindControl;
    case ImGuiAppNodeKind_Struct:  return AppComposerGetStyle()->KindStruct;
    case ImGuiAppNodeKind_Field:   return AppComposerGetStyle()->KindField;
    default:                       return AppComposerGetStyle()->KindDefault;
    }
  }

  // Immediate group-parent of a node (the node whose collapse would hide it): field -> its struct, owned struct
  // -> its control, hosted control -> its window/sidebar. -1 at a group root. Distinct from AppGraphParentOf for
  // the struct case (a struct is tied to its control by a data edge, not a containment edge).
  static int AppGroupOwnerOf(const ImGuiAppGraph* g, int id)
  {
    const ImGuiAppNode* n = AppGraphFindNodeConst(g, id);
    if (n == nullptr)
      return -1;
    if (n->Kind == ImGuiAppNodeKind_Field)
      return AppGraphParentOf(g, id);
    if (n->Kind == ImGuiAppNodeKind_Struct)
    {
      for (int i = 0; i < g->Nodes.Size; i++)
        if (g->Nodes.Data[i].PersistStructId == id || g->Nodes.Data[i].TempStructId == id)
          return g->Nodes.Data[i].Id;
      return -1;
    }
    if (n->Kind == ImGuiAppNodeKind_Control)
      return AppGraphParentOf(g, id);
    return -1;
  }

  //-----------------------------------------------------------------------------
  // Drill-down scopes (Blender node-group semantics for the composition hierarchy)
  //
  // The graph mirrors the layer architecture's composition tree: layers root the app, windows/sidebars compose
  // onto the Display layer, controls onto their host (or the Task layer at app level), data structs onto their
  // control, fields onto their struct. ViewScope is a stack of entered nodes: Tab (or double-click a layer)
  // drills into the selected node's composition, Esc goes back up, the breadcrumb bar jumps anywhere. Inside a
  // scope only that node's composition is submitted, and members carry execution-order badges -- the event
  // sequence in which the framework runs them each frame.
  //-----------------------------------------------------------------------------

  static bool AppGraphReparent(ImGuiAppGraph* g, int child_id, int parent_id);   // fwd (defined by the outliner)

  // Scope parent: which composition scope a node lives in (the tree the breadcrumb walks). -1 = root.
  static int AppScopeParentOf(const ImGuiAppGraph* g, int id)
  {
    const ImGuiAppNode* n = AppGraphFindNodeConst(g, id);
    if (n == nullptr)
      return -1;
    switch (n->Kind)
    {
    case ImGuiAppNodeKind_Field:
      return AppGraphParentOf(g, id);
    case ImGuiAppNodeKind_Struct:
    {
      for (int i = 0; i < g->Nodes.Size; i++)
        if (g->Nodes.Data[i].PersistStructId == id || g->Nodes.Data[i].TempStructId == id)
          return g->Nodes.Data[i].Id;
      // A standalone struct is app-level data: it lives (and updates) in the Task layer's domain.
      const ImGuiAppNode* task = AppGraphFindLayerOfType(g, ImGuiAppLayerType_Task);
      return task != nullptr ? task->Id : -1;
    }
    case ImGuiAppNodeKind_Control:
    {
      const int p = AppGraphParentOf(g, id);
      const ImGuiAppNode* pn = p >= 0 ? AppGraphFindNodeConst(g, p) : nullptr;
      if (pn != nullptr && (pn->Kind == ImGuiAppNodeKind_Window || pn->Kind == ImGuiAppNodeKind_Sidebar))
        return p;
      const ImGuiAppNode* task = AppGraphFindLayerOfType(g, ImGuiAppLayerType_Task);
      return task != nullptr ? task->Id : -1;
    }
    case ImGuiAppNodeKind_Window:
    case ImGuiAppNodeKind_Sidebar:
    {
      const ImGuiAppNode* wl = AppGraphFindLayerOfType(g, ImGuiAppLayerType_Display);
      return wl != nullptr ? wl->Id : -1;
    }
    default:
      return -1;   // layers are root composition slots
    }
  }

  static int AppScopeCurrent(const ImGuiAppGraph* g)
  {
    return g->ViewScope.Size > 0 ? g->ViewScope.back() : -1;
  }

  // Per-branch camera memory lives ON the graph (ImGuiAppGraph::ScopeCams, transient like
  // ViewScope): every editor over a graph shares its memory; two graphs never share state.
  static void AppScopeCameraSave(ImGuiAppGraph* g, ImGuiCanvasState* cv, int scope_id)
  {
    ImGuiAppScopeCamera* cam = nullptr;
    for (int i = 0; i < g->ScopeCams.Size && cam == nullptr; i++)
      if (g->ScopeCams.Data[i].ScopeId == scope_id)
        cam = &g->ScopeCams.Data[i];
    if (cam == nullptr)
    {
      g->ScopeCams.push_back(ImGuiAppScopeCamera());
      cam = &g->ScopeCams.back();
      cam->ScopeId = scope_id;
    }
    cam->Pan = ImGui::CanvasGetPan(cv);
    cam->Zoom = ImGui::CanvasGetZoom(cv);
  }

  static bool AppScopeCameraRestore(const ImGuiAppGraph* g, ImGuiCanvasState* cv, int scope_id)
  {
    for (int i = 0; i < g->ScopeCams.Size; i++)
    {
      const ImGuiAppScopeCamera* cam = &g->ScopeCams.Data[i];
      if (cam->ScopeId != scope_id)
        continue;
      ImGui::CanvasSetZoom(cv, cam->Zoom, ImGui::GetCursorScreenPos());
      ImGui::CanvasSetPan(cv, cam->Pan);   // after the zoom: SetZoom adjusts pan to hold its anchor
      return true;
    }
    return false;
  }

  static bool AppScopeCanEnter(const ImGuiAppNode* n)
  {
    return n != nullptr && (n->Kind == ImGuiAppNodeKind_Layer || n->Kind == ImGuiAppNodeKind_Window
        || n->Kind == ImGuiAppNodeKind_Sidebar || n->Kind == ImGuiAppNodeKind_Control || n->Kind == ImGuiAppNodeKind_Struct);
  }

  // True when the node belongs inside the current scope (strict descendant of the scope owner in the scope-parent
  // tree). At root everything is in scope; the owner itself is NOT a member (entering a group shows its contents,
  // not the group node). A layer scope matches by layer TYPE, so a live-mirror layer and an authored twin open
  // the same scope. The Command layer is a cross-cutting view: its members are the controls emitting commands.
  static bool AppNodeInScope(const ImGuiAppGraph* g, int id)
  {
    const int top = AppScopeCurrent(g);
    if (top < 0)
      return true;
    if (id == top)
      return false;
    const ImGuiAppNode* tn = AppGraphFindNodeConst(g, top);
    const ImGuiAppNode* n = AppGraphFindNodeConst(g, id);
    if (tn == nullptr || n == nullptr)
      return true;   // dangling scope entries are repaired by AppScopeValidate; fail open meanwhile

    if (tn->Kind == ImGuiAppNodeKind_Layer)
    {
      if (n->Kind == ImGuiAppNodeKind_Layer)
        return false;
      if (tn->LayerType == ImGuiAppLayerType_Command)
        return n->Kind == ImGuiAppNodeKind_Control && n->Commands.Size > 0;
      if (tn->LayerType == ImGuiAppLayerType_Status)
        return false;   // nothing composes into the status layer (it renders the app's status bar itself)
      if (tn->LayerType == ImGuiAppLayerType_Layout)
        return false;
      int cur = id;
      for (int guard = 0; guard < 64; guard++)
      {
        const int p = AppScopeParentOf(g, cur);
        if (p < 0)
          return false;
        const ImGuiAppNode* pn = AppGraphFindNodeConst(g, p);
        if (pn == nullptr)
          return false;
        if (pn->Kind == ImGuiAppNodeKind_Layer)
          return pn->LayerType == tn->LayerType;
        cur = p;
      }
      return false;
    }

    int cur = id;
    for (int guard = 0; guard < 64; guard++)
    {
      const int p = AppScopeParentOf(g, cur);
      if (p < 0)
        return false;
      if (p == top)
        return true;
      cur = p;
    }
    return false;
  }

  // Drop dangling / non-enterable entries (node deleted, undo snapshot restored) from the first broken one on.
  static void AppScopeValidate(ImGuiAppGraph* g)
  {
    for (int i = 0; i < g->ViewScope.Size; i++)
    {
      const ImGuiAppNode* n = AppGraphFindNodeConst(g, g->ViewScope.Data[i]);
      if (n == nullptr || !AppScopeCanEnter(n))
      {
        g->ViewScope.resize(i);
        return;
      }
    }
  }

  // Editor notice toast: shares the graph's LastLinkErr channel (status hint + canvas toast).
  static void AppGraphNotify(ImGuiAppGraph* g, const char* fmt, ...) IM_FMTARGS(2);
  static void AppGraphNotify(ImGuiAppGraph* g, const char* fmt, ...)
  {
    va_list args;
    va_start(args, fmt);
    ImFormatStringV(g->LastLinkErr, IM_ARRAYSIZE(g->LastLinkErr), fmt, args);
    va_end(args);
    g->LastLinkErrSeq++;
  }

  // Canonical refusal when a mutating verb is attempted on a live-mirror node. One phrasing across every
  // surface (canvas, outliner, commands, reparent) so the notice reads the same wherever it fires.
  static void AppNotifyLiveReadOnly(ImGuiAppGraph* g, const ImGuiAppNode* n)
  {
    AppGraphNotify(g, "'%s' is a live mirror -- read-only. Promote it to author it.", (n != nullptr && n->Draft.Name[0]) ? n->Draft.Name : "(live)");
  }

  // Kinds that compose into a drilled scope: what the interior palettes offer, and what a creation
  // road may adopt. Live non-layer scopes take nothing (the mirror is read-only; Promote authors).
  bool AppScopeKindComposable(const ImGuiAppGraph* g, int scope_id, ImGuiAppNodeKind kind)
  {
    const ImGuiAppNode* s = AppGraphFindNodeConst(g, scope_id);
    if (s == nullptr)
      return false;
    if (s->IsLive && s->Kind != ImGuiAppNodeKind_Layer)
      return false;
    switch (s->Kind)
    {
    case ImGuiAppNodeKind_Window:
    case ImGuiAppNodeKind_Sidebar:
      return kind == ImGuiAppNodeKind_Control;
    case ImGuiAppNodeKind_Struct:
      return kind == ImGuiAppNodeKind_Field && !s->IsBuiltin;
    case ImGuiAppNodeKind_Layer:
      // Layer domains are implicit (AppScopeParentOf falls back by kind); a bare add can only ever
      // land in the Task/Display domains. Command members need commands a new control lacks;
      // Status/Layout/Custom compose nothing.
      if (s->LayerType == ImGuiAppLayerType_Task)
        return kind == ImGuiAppNodeKind_Control || kind == ImGuiAppNodeKind_Struct;
      if (s->LayerType == ImGuiAppLayerType_Display)
        return kind == ImGuiAppNodeKind_Window || kind == ImGuiAppNodeKind_Sidebar;
      return false;
    default:
      return false;   // a control's members arrive via explode, never bare adds
    }
  }

  // True if this node should NOT be submitted to the canvas: outside the current drill-down scope,
  // or an ancestor group is collapsed. Eye-hidden nodes STAY submitted -- they render with the
  // disabled look (AppNodeCanvasOff) instead of vanishing.
  static bool AppNodeHiddenByCollapse(const ImGuiAppGraph* g, int id)
  {
    const ImGuiAppNode* self = AppGraphFindNodeConst(g, id);
    if (self == nullptr)
      return false;
    if (!AppNodeInScope(g, id))
      return true;
    for (int owner = AppGroupOwnerOf(g, id); owner >= 0; owner = AppGroupOwnerOf(g, owner))
    {
      const ImGuiAppNode* o = AppGraphFindNodeConst(g, owner);
      if (o != nullptr && o->GroupCollapsed)
        return true;
    }
    return false;
  }

  // Disabled look: the node (or a containment ancestor) is eye-hidden, or its enclosing live
  // window is closed (the display layer renders none of it).
  static bool AppNodeCanvasOff(const ImGuiAppGraph* g, int id)
  {
    for (int cur = id; cur >= 0; cur = AppGroupOwnerOf(g, cur))
    {
      const ImGuiAppNode* o = AppGraphFindNodeConst(g, cur);
      if (o == nullptr)
        return false;
      if (o->Hidden && o->Kind != ImGuiAppNodeKind_Layer)
        return true;
      if (o->IsLive && o->Kind == ImGuiAppNodeKind_Window && g->LiveApp != nullptr)
        if (const ImGuiAppWindowBase* wb = (const ImGuiAppWindowBase*)AppGraphFindLiveItem(g->LiveApp, o))
          if (!wb->Open)
            return true;
    }
    return false;
  }

  // Accumulate the screen-space bounding box of a containment group: the owner node + all its descendants
  // (window/sidebar -> hosted controls -> their data structs -> fields; control -> its structs -> fields;
  // struct -> fields). Skips hidden live nodes. Recursive. Bounds in MODEL units, no camera term.
  static void AppGroupAccumulate(const ImGuiAppGraph* g, int owner_id, bool show_live, ImVec2* mn, ImVec2* mx)
  {
    const ImGuiAppNode* n = AppGraphFindNodeConst(g, owner_id);
    if (n == nullptr || (!show_live && n->IsLive) || AppNodeHiddenByCollapse(g, owner_id))
      return;
    if (!n->HasGridPos && !AppEditorNodeWasSubmitted(g, owner_id))
      return;   // neither a settled model position nor a measurement yet (first-ever frame)
    // Engine position: the input FSM moves it before submission; GridPos is last frame's read-back.
    const ImVec2 p = AppEditorNodeWasSubmitted(g, owner_id) ? AppCanvasNodePos(g, owner_id) : n->GridPos;
    ImVec2 d;
    if (!AppNodeModelSize(g, owner_id, &d))
      d = AppLayoutNodeSize(g, n);
    mn->x = ImMin(mn->x, p.x); mn->y = ImMin(mn->y, p.y);
    mx->x = ImMax(mx->x, p.x + d.x); mx->y = ImMax(mx->y, p.y + d.y);

    if (n->Kind == ImGuiAppNodeKind_Window || n->Kind == ImGuiAppNodeKind_Sidebar)
    {
      for (int i = 0; i < g->Nodes.Size; i++)
        if (g->Nodes.Data[i].Kind == ImGuiAppNodeKind_Control && AppGraphParentOf(g, g->Nodes.Data[i].Id) == owner_id)
          AppGroupAccumulate(g, g->Nodes.Data[i].Id, show_live, mn, mx);
    }
    else if (n->Kind == ImGuiAppNodeKind_Control)
    {
      if (n->PersistStructId >= 0) AppGroupAccumulate(g, n->PersistStructId, show_live, mn, mx);
      if (n->TempStructId >= 0)    AppGroupAccumulate(g, n->TempStructId, show_live, mn, mx);
    }
    else if (n->Kind == ImGuiAppNodeKind_Struct)
    {
      for (int i = 0; i < g->Nodes.Size; i++)
        if (g->Nodes.Data[i].Kind == ImGuiAppNodeKind_Field && AppGraphParentOf(g, g->Nodes.Data[i].Id) == owner_id)
          AppGroupAccumulate(g, g->Nodes.Data[i].Id, show_live, mn, mx);
    }
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Tidy tree layout (measured-size layered DAG)
  //-----------------------------------------------------------------------------

  // Containment children in layout order (window -> hosted controls -> data structs -> fields).
  static void AppLayoutKids(const ImGuiAppGraph* g, const ImGuiAppNode* n, ImVector<int>* kids)
  {
    kids->clear();
    if (n->Kind == ImGuiAppNodeKind_Window || n->Kind == ImGuiAppNodeKind_Sidebar)
    {
      for (int i = 0; i < g->Nodes.Size; i++)
        if (g->Nodes.Data[i].Kind == ImGuiAppNodeKind_Control && AppGraphParentOf(g, g->Nodes.Data[i].Id) == n->Id)
          kids->push_back(g->Nodes.Data[i].Id);
    }
    else if (n->Kind == ImGuiAppNodeKind_Control)
    {
      if (n->PersistStructId >= 0) kids->push_back(n->PersistStructId);
      if (n->TempStructId >= 0)    kids->push_back(n->TempStructId);
    }
    else if (n->Kind == ImGuiAppNodeKind_Struct)
    {
      for (int i = 0; i < g->Nodes.Size; i++)
        if (g->Nodes.Data[i].Kind == ImGuiAppNodeKind_Field && AppGraphParentOf(g, g->Nodes.Data[i].Id) == n->Id)
          kids->push_back(g->Nodes.Data[i].Id);
    }
  }

  static const int kAppLayoutMaxDepth = 12;

  static ImVec2 AppLayoutPureSize(const ImGuiAppGraph* g, const ImGuiAppNode* n);

  // Depth-first vertical stack: every node sits UNDERNEATH the previous one (vertical order =
  // containment order), indented by depth so ownership still reads at a glance. *y advances past
  // each placed node's MEASURED height; *max_r (when given) tracks the stack's right edge.
  static void AppLayoutStack(ImGuiAppGraph* g, int id, int depth, float x0, float* y, float* max_r)
  {
    ImGuiAppNode* n = AppGraphFindNode(g, id);
    if (n == nullptr)
      return;
    const float kGapNodeY = 40.0f;
    const float kIndentX = 48.0f;
    const ImVec2 sz = AppLayoutPureSize(g, n);   // pure model size: tidy is zoom-idempotent
    // Altitude split: a scoped tidy arranges THIS interior (placement records); only the root
    // tidy owns GridPos (one producer per altitude, scope-interior-design.md par.7).
    const ImVec2 pos(x0 + (float)ImMin(depth, kAppLayoutMaxDepth - 1) * kIndentX, *y);
    if (AppScopeCurrent(g) >= 0)
      AppNodeScopePosStore(g, id, pos);
    else
    {
      n->GridPos = pos;
      n->HasGridPos = true;
    }
    n->_NeedsPlace = true;
    if (max_r != nullptr)
      *max_r = ImMax(*max_r, pos.x + sz.x);
    *y += sz.y + kGapNodeY;
    ImVector<int> kids;
    AppLayoutKids(g, n, &kids);
    for (int k = 0; k < kids.Size; k++)
      AppLayoutStack(g, kids.Data[k], depth + 1, x0, y, max_r);
  }

  // Tidy sizes are PURE MODEL functions (docs/phase-coherence.md: the camera is never an input
  // to a model-derived value): measured sizes vary with zoom because font metrics are not linear
  // in size, so a layout derived from them cannot be idempotent across zooms. Estimated from the
  // node's CONTENT instead -- identical model, identical layout, at any zoom, any time. Estimates
  // run generous so the vertical stack never visually overlaps the (tighter) measured nodes.
  static ImVec2 AppLayoutPureSize(const ImGuiAppGraph* g, const ImGuiAppNode* n)
  {
    IM_UNUSED(g);
    const float em = ImGui::GetStyle().FontSizeBase;   // model-unit em, zoom-free
    int rows = 3;
    int wchars = 24;
    switch (n->Kind)
    {
    case ImGuiAppNodeKind_Window:
    case ImGuiAppNodeKind_Sidebar:
      rows = 2;
      break;
    case ImGuiAppNodeKind_Control:
      // Live card: title, dependencies pin, origin note, data type, bindings, data pin.
      rows = 6 + n->Events.Size;
      wchars = 40;
      break;
    case ImGuiAppNodeKind_Struct:
      rows = 3 + n->Draft.PersistFields.Size;
      break;
    case ImGuiAppNodeKind_Field:
      rows = 2 + n->Draft.PersistFields.Size;
      break;
    default:
      break;
    }
    const int name_chars = (int)strlen(n->Draft.Name);
    if (name_chars + 6 > wchars)
      wchars = name_chars + 6;
    return ImVec2((float)wchars * em * 0.62f + em * 2.0f, (float)rows * em * 2.0f + em);
  }

  // Containment host of a layout root (window/sidebar id), -1 when it has none. Consecutive
  // roots sharing a host form one group unit in the tidy flow.
  static int AppLayoutHostOf(const ImGuiAppGraph* g, int id)
  {
    const ImGuiAppNode* n = AppGraphFindNodeConst(g, id);
    if (n == nullptr || n->Kind != ImGuiAppNodeKind_Control)
      return -1;
    const int parent = AppGraphParentOf(g, id);
    if (parent < 0)
      return -1;
    const ImGuiAppNode* p = AppGraphFindNodeConst(g, parent);
    return (p != nullptr && (p->Kind == ImGuiAppNodeKind_Window || p->Kind == ImGuiAppNodeKind_Sidebar)) ? parent : -1;
  }

  void AppGraphAutoLayout(ImGuiAppGraph* g, bool show_live)
  {
    IM_ASSERT(g != nullptr);

    // Window section is active only at root scope with a Display layer present: there the section
    // packer owns window positions, so tidy leaves windows out and their hosted controls tidy as
    // their own free trees. Without a section (drilled in, or no Display layer) containment tidies
    // as the classic vertical tree (window over its children).
    const bool section_active = AppScopeCurrent(g) < 0 && AppGraphLayerOfType(g, ImGuiAppLayerType_Display) != nullptr;

    // Layout roots (containment tree tops in the current scope).
    ImVector<int> roots;
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (n->Kind == ImGuiAppNodeKind_Layer)      // layers keep their own tight column packer
        continue;
      if (n->Kind == ImGuiAppNodeKind_Window && section_active)
        continue;                                 // windows are owned by the section packer
      if (!show_live && n->IsLive)
        continue;
      if (!AppNodeInScope(g, n->Id))              // scoped tidy arranges only what the scope shows
        continue;

      bool is_root = true;
      if (n->Kind == ImGuiAppNodeKind_Control || n->Kind == ImGuiAppNodeKind_Field)
      {
        const int parent = AppGraphParentOf(g, n->Id);
        // With the section active, a window-hosted control's parent (the window) is off the tidy
        // tree -- so the control is a root, laid out as its own free tree instead of hung under the
        // window. Without the section, it hangs under its window as before.
        const ImGuiAppNode* p = parent >= 0 ? AppGraphFindNodeConst(g, parent) : nullptr;
        const bool parent_is_sectioned_window = p != nullptr && p->Kind == ImGuiAppNodeKind_Window && section_active;
        is_root = parent < 0 || parent_is_sectioned_window;
      }
      else if (n->Kind == ImGuiAppNodeKind_Struct)
      {
        for (int j = 0; j < g->Nodes.Size; j++)
          if (g->Nodes.Data[j].PersistStructId == n->Id || g->Nodes.Data[j].TempStructId == n->Id)
          {
            is_root = false;
            break;
          }
      }
      // Drilled in, the scope owner is off-canvas: its direct children become the layout roots, otherwise a
      // scoped tidy would find nothing to walk.
      if (!is_root && AppScopeCurrent(g) >= 0 && AppScopeParentOf(g, n->Id) == AppScopeCurrent(g))
        is_root = true;
      if (is_root)
        roots.push_back(n->Id);
    }

    // Groups stack UNDERNEATH one another in composition order (roots discovered in node order),
    // all aligned at one left edge. INSIDE a group, a run of control roots hosted by the same
    // window flows in TWO columns -- producer first at the left, consumers to its right and then
    // wrapping -- so dependency wires read left-to-right within the frame. Everything else (a
    // lone control, a window tree off the section, struct chains) stacks vertically, indented by
    // containment depth. Vertical order = order, the same law as the section stacks.
    // (Window nodes are excluded above at root scope -- the section packer owns their positions.)
    const float kGapGroupY = 80.0f;
    const float kGapNodeY = 40.0f;
    const float kGapColX = 60.0f;
    const float kColStaggerY = 18.0f;
    const bool  scoped = AppScopeCurrent(g) >= 0;
    const float x0 = scoped ? 80.0f : AppLayoutContentX0(g);
    float y = scoped ? 60.0f : kAppGraphY0;
    int r = 0;
    while (r < roots.Size)
    {
      int unit_end = r + 1;
      const int host = AppLayoutHostOf(g, roots.Data[r]);
      if (host >= 0)
        while (unit_end < roots.Size && AppLayoutHostOf(g, roots.Data[unit_end]) == host)
          unit_end++;

      if (unit_end - r >= 2)
      {
        float row_y = y;
        float row_bottom = y;
        float col1_x = x0;
        for (int i = r; i < unit_end; i++)
        {
          if ((i - r) % 2 == 0)
          {
            float yy = row_y;
            float right = x0;
            AppLayoutStack(g, roots.Data[i], 0, x0, &yy, &right);
            row_bottom = ImMax(row_bottom, yy - kGapNodeY);
            col1_x = right + kGapColX;
          }
          else
          {
            float yy = row_y + kColStaggerY;
            AppLayoutStack(g, roots.Data[i], 0, col1_x, &yy, nullptr);
            row_bottom = ImMax(row_bottom, yy - kGapNodeY);
            row_y = row_bottom + kGapNodeY;
          }
        }
        y = row_bottom + kGapGroupY;
      }
      else
      {
        AppLayoutStack(g, roots.Data[r], 0, x0, &y, nullptr);
        y += kGapGroupY - kGapNodeY;
      }
      r = unit_end;
    }
  }

  // Direct members of the current scope in EXECUTION order -- the per-frame event sequence the framework runs
  // them in: windows/sidebars in push (render) order, controls in dependency (update) order, command emitters in
  // push order. Non-sequential scopes (control/struct/status) produce an empty list.
  static void AppScopeSequenceIds(const ImGuiAppGraph* g, ImVector<int>* out)
  {
    out->clear();
    const int top = AppScopeCurrent(g);
    const ImGuiAppNode* tn = top >= 0 ? AppGraphFindNodeConst(g, top) : nullptr;
    if (tn == nullptr)
      return;

    if (tn->Kind == ImGuiAppNodeKind_Layer && tn->LayerType == ImGuiAppLayerType_Display)
    {
      for (int pass = 0; pass < 2; pass++)   // windows render first, then sidebars
        for (int i = 0; i < g->Nodes.Size; i++)
        {
          const ImGuiAppNodeKind want = pass == 0 ? ImGuiAppNodeKind_Window : ImGuiAppNodeKind_Sidebar;
          if (g->Nodes.Data[i].Kind == want && AppNodeInScope(g, g->Nodes.Data[i].Id))
            out->push_back(g->Nodes.Data[i].Id);
        }
    }
    else if (tn->Kind == ImGuiAppNodeKind_Layer && tn->LayerType == ImGuiAppLayerType_Task)
    {
      // Authored controls in dependency (push/update) order, then live-mirror controls in mirror order.
      ImVector<int> order;
      char err[8];
      if (AppGraphTopoOrder(g, &order, err, IM_ARRAYSIZE(err)))
        for (int i = 0; i < order.Size; i++)
          if (AppNodeInScope(g, order.Data[i]))
            out->push_back(order.Data[i]);
      for (int i = 0; i < g->Nodes.Size; i++)
        if (g->Nodes.Data[i].IsLive && g->Nodes.Data[i].Kind == ImGuiAppNodeKind_Control && AppNodeInScope(g, g->Nodes.Data[i].Id))
          out->push_back(g->Nodes.Data[i].Id);
    }
    else if (tn->Kind == ImGuiAppNodeKind_Layer && tn->LayerType == ImGuiAppLayerType_Command)
    {
      for (int i = 0; i < g->Nodes.Size; i++)
        if (g->Nodes.Data[i].Kind == ImGuiAppNodeKind_Control && AppNodeInScope(g, g->Nodes.Data[i].Id))
          out->push_back(g->Nodes.Data[i].Id);
    }
    else if (tn->Kind == ImGuiAppNodeKind_Window || tn->Kind == ImGuiAppNodeKind_Sidebar)
    {
      for (int i = 0; i < g->Nodes.Size; i++)
        if (g->Nodes.Data[i].Kind == ImGuiAppNodeKind_Control && AppGraphParentOf(g, g->Nodes.Data[i].Id) == top)
          out->push_back(g->Nodes.Data[i].Id);
    }
  }

  // Accent color that identifies the current scope everywhere (badges, arrows, breadcrumb tail).
  static ImU32 AppScopeAccent(const ImGuiAppGraph* g)
  {
    const int top = AppScopeCurrent(g);
    const ImGuiAppNode* tn = top >= 0 ? AppGraphFindNodeConst(g, top) : nullptr;
    if (tn == nullptr)
      return AppComposerGetStyle()->AccentNeutral;
    return tn->Kind == ImGuiAppNodeKind_Layer ? AppLayerAccent(tn->LayerType) : AppKindColor(tn->Kind);
  }

  // One-line caption for the breadcrumb: WHAT executes in this scope and in which per-frame event sequence.
  // This is the layer architecture's contract, surfaced where the composition is edited.
  static const char* AppScopeCaption(const ImGuiAppGraph* g)
  {
    const int top = AppScopeCurrent(g);
    const ImGuiAppNode* tn = top >= 0 ? AppGraphFindNodeConst(g, top) : nullptr;
    if (tn == nullptr)
      return "composition root -- the phase layers run left to right every frame";
    if (tn->Kind == ImGuiAppNodeKind_Layer)
    {
      switch (tn->LayerType)
      {
      case ImGuiAppLayerType_Task:    return "state collection: module status ingest + control updates in dependency order --  OnUpdate(dt, temp, last_temp)";
      case ImGuiAppLayerType_Command: return "controls that emit commands:  OnGetCommand collects ->  app dispatches OnExecuteCommand";
      case ImGuiAppLayerType_Status:  return "publishes the app's own status for other modules (today: the status bar) -- nothing composes here yet";
      case ImGuiAppLayerType_Layout:  return "workspace layout: the app's OnLayout() submits dockspaces & dock bindings before any window Begins";
      case ImGuiAppLayerType_Display: return "presentation only -- windows & sidebars render the collected state, in push order, mutating nothing";
      case ImGuiAppLayerType_Custom:  return "your ImGuiAppLayer subclass:  OnAttach/OnDetach at push/pop, OnUpdate -> OnRender at its stack position";
      default: break;
      }
    }
    if (tn->Kind == ImGuiAppNodeKind_Window || tn->Kind == ImGuiAppNodeKind_Sidebar)
      return "hosted controls run in push order between the host's Begin/End:  OnGetCommand -> OnUpdate -> OnRender";
    if (tn->Kind == ImGuiAppNodeKind_Control)
      return "data domain:  OnRender records TempData  ->  OnUpdate derives events (temp ^ last_temp) and mutates PersistData";
    if (tn->Kind == ImGuiAppNodeKind_Struct)
      return "data fields -- wire values out to consumers";
    return "";
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Scope interior (walls, boundary portals, density altitude, scope-local placement)
  //
  // What nodes look like below the composition root (docs/scope-interior-design.md). Inside a
  // window/sidebar scope: the owner's card silhouette becomes the room (walls with title bar +
  // config readout), data edges crossing the boundary dock on the wall as portal chips, member
  // cards carry full authoring detail while everywhere else they fold to identity cards, and each
  // interior owns its own node arrangement (ScopePlacements; the root layout stays in GridPos).
  //
  // Geometry phase audit (docs/phase-coherence.md checklist, classified up front):
  //   * walls           -- bounds from engine positions / this scope's model placements with THIS
  //                        frame's camera; drawn pre-submission on the background list, same path
  //                        as group frames. Published to ImGuiAppEditorState::ScopeWallRect in
  //                        model units.
  //   * portal chips    -- derived every frame from Links + ViewScope (pure model); chip anchors
  //                        read pin geometry post-CanvasEnd (coherent per rule 5). No caches, no
  //                        measure->apply loop.
  //   * density flip    -- pure predicate on model state; the card resize it causes is the
  //                        framework's documented content-driven T+1 in invariant units.
  //-----------------------------------------------------------------------------

  // Scope-local placement: each drilled interior owns its own arrangement. The effective position
  // of a node in the CURRENT scope is its (scope, node) placement record when one exists, else
  // GridPos (first entry inherits the root layout); the root always reads GridPos. The interior
  // read-back writes placements, the root read-back writes GridPos -- moving a node in one
  // altitude never moves it in the other.
  static const ImGuiAppScopePlacement* AppScopePlacementFind(const ImGuiAppGraph* g, int scope_id, int node_id)
  {
    for (int i = 0; i < g->ScopePlacements.Size; i++)
      if (g->ScopePlacements.Data[i].ScopeId == scope_id && g->ScopePlacements.Data[i].NodeId == node_id)
        return &g->ScopePlacements.Data[i];
    return nullptr;
  }

  static ImVec2 AppNodeScopePos(const ImGuiAppGraph* g, const ImGuiAppNode* n)
  {
    const int top = AppScopeCurrent(g);
    if (top >= 0)
    {
      const ImGuiAppScopePlacement* pl = AppScopePlacementFind(g, top, n->Id);
      if (pl != nullptr)
        return pl->Pos;
    }
    return n->GridPos;
  }

  static void AppNodeScopePosStore(ImGuiAppGraph* g, int node_id, const ImVec2& pos)
  {
    const int top = AppScopeCurrent(g);
    IM_ASSERT(top >= 0 && "root read-back writes GridPos, never a placement record");
    for (int i = 0; i < g->ScopePlacements.Size; i++)
      if (g->ScopePlacements.Data[i].ScopeId == top && g->ScopePlacements.Data[i].NodeId == node_id)
      {
        g->ScopePlacements.Data[i].Pos = pos;
        return;
      }
    ImGuiAppScopePlacement pl;
    pl.ScopeId = top;
    pl.NodeId = node_id;
    pl.Pos = pos;
    g->ScopePlacements.push_back(pl);
  }

  // Interior seat for a creation road with no pointer position: just below the walls, or the
  // scoped tidy origin in an empty interior.
  static ImVec2 AppScopeInteriorDropPoint(const ImGuiAppGraph* g)
  {
    const ImGuiAppEditorState* ed = AppGraphEditorState(g);
    if (ed->ScopeWallValid)
      return ImVec2(ed->ScopeWallRect.x + 40.0f, ed->ScopeWallRect.w + 60.0f);
    return ImVec2(80.0f, 60.0f);
  }

  // Compose a just-created node into the drilled scope: containment link where the pair carries one
  // (Control->Window/Sidebar, Field->Struct; layer domains are implicit), the creation point into
  // THIS scope's placement records, and root GridPos re-derived near the owner's root cluster --
  // one producer per altitude, in both directions. A kind the scope cannot take keeps its default
  // root placement and states why (the silent-vanish failure this replaces).
  static void AppScopeComposeNewNode(ImGuiAppGraph* g, int added_id, const ImVec2* interior_pos)
  {
    const int top = AppScopeCurrent(g);
    ImGuiAppNode* added = AppGraphFindNode(g, added_id);
    const ImGuiAppNode* owner = top >= 0 ? AppGraphFindNodeConst(g, top) : nullptr;
    if (added == nullptr || owner == nullptr)
      return;
    if (!AppScopeKindComposable(g, top, added->Kind))
    {
      AppGraphNotify(g, "a %s cannot compose into %s -- created at the composition root",
                     AppNodeKindName(added->Kind), owner->Draft.Name[0] ? owner->Draft.Name : AppNodeKindName(owner->Kind));
      return;
    }
    if ((added->Kind == ImGuiAppNodeKind_Control && (owner->Kind == ImGuiAppNodeKind_Window || owner->Kind == ImGuiAppNodeKind_Sidebar))
     || (added->Kind == ImGuiAppNodeKind_Field && owner->Kind == ImGuiAppNodeKind_Struct))
      AppGraphReparent(g, added_id, top);
    const ImVec2 root_pref = owner->GridPos + ImVec2(280.0f, 0.0f);
    AppGraphPlaceNode(g, added, &root_pref);
    AppNodeScopePosStore(g, added_id, interior_pos != nullptr ? *interior_pos : AppScopeInteriorDropPoint(g));
  }

  // Compose freshly imported nodes (paste / prefab / struct import appended from first_index on)
  // into the drilled scope: adopt the subtree roots the scope can take, keep the cluster's shape in
  // this scope's placements anchored at the drop point, and shift the cluster's root layout next to
  // the owner's root cluster.
  static void AppScopeComposeImported(ImGuiAppGraph* g, int first_index, const ImVec2* interior_anchor)
  {
    const int top = AppScopeCurrent(g);
    const ImGuiAppNode* owner = top >= 0 ? AppGraphFindNodeConst(g, top) : nullptr;
    if (owner == nullptr || first_index < 0 || first_index >= g->Nodes.Size)
      return;

    ImVec2 mn(FLT_MAX, FLT_MAX);
    for (int i = first_index; i < g->Nodes.Size; i++)
    {
      mn.x = ImMin(mn.x, g->Nodes.Data[i].GridPos.x);
      mn.y = ImMin(mn.y, g->Nodes.Data[i].GridPos.y);
    }

    int refused = 0;
    for (int i = first_index; i < g->Nodes.Size; i++)
    {
      const int id = g->Nodes.Data[i].Id;
      const ImGuiAppNodeKind kind = g->Nodes.Data[i].Kind;
      if (AppGraphParentOf(g, id) >= 0)
        continue;   // interior of the imported subtree: containment came with it
      if (!AppScopeKindComposable(g, top, kind))
      {
        refused++;
        continue;
      }
      if ((kind == ImGuiAppNodeKind_Control && (owner->Kind == ImGuiAppNodeKind_Window || owner->Kind == ImGuiAppNodeKind_Sidebar))
       || (kind == ImGuiAppNodeKind_Field && owner->Kind == ImGuiAppNodeKind_Struct))
        AppGraphReparent(g, id, top);
    }

    const ImVec2 anchor = interior_anchor != nullptr ? *interior_anchor : AppScopeInteriorDropPoint(g);
    const ImVec2 root_shift = owner->GridPos + ImVec2(280.0f, 0.0f) - mn;
    for (int i = first_index; i < g->Nodes.Size; i++)
    {
      ImGuiAppNode* n = &g->Nodes.Data[i];
      if (AppNodeInScope(g, n->Id))
        AppNodeScopePosStore(g, n->Id, anchor + (n->GridPos - mn));
      n->GridPos += root_shift;
      n->_NeedsPlace = true;
    }
    if (refused > 0)
      AppGraphNotify(g, "%d node%s cannot compose into %s -- left at the composition root",
                     refused, refused == 1 ? "" : "s", owner->Draft.Name[0] ? owner->Draft.Name : AppNodeKindName(owner->Kind));
  }

  // Add a Field member to a struct. Inline draft fields explode into Field nodes first: field
  // nodes shadow the inline list wholesale, so adding one beside a populated inline list would
  // silently drop every inline field from the effective set.
  static int AppScopeAddFieldToStruct(ImGuiAppGraph* g, int struct_id)
  {
    ImGuiAppNode* sn = AppGraphFindNode(g, struct_id);
    if (sn == nullptr || sn->Kind != ImGuiAppNodeKind_Struct || sn->IsLive || sn->IsBuiltin)
      return -1;
    if (AppGraphFieldNodeCount(g, struct_id, 0) == 0 && sn->Draft.PersistFields.Size > 0)
      AppGraphExplodeFields(g, sn, 0);
    ImGuiAppNode* f = AppGraphAddNode(g, ImGuiAppNodeKind_Field, "field");
    f->FieldList = 0;
    AppNodeDraftAddField(&f->Draft.PersistFields, "field", ImGuiAppFieldType_Float);
    return f->Id;
  }

  // Detail altitude: a node shows its full authoring body only when the current scope is its
  // scope-parent; everywhere else it folds to an identity card (title, pins, one summary line).
  static bool AppScopeDetailAltitude(const ImGuiAppGraph* g, const ImGuiAppNode* n)
  {
    // Altitude-law guard (same as altitude_root): graphs without a composed Display foundation
    // (raw canvases, unit scaffolds) keep every node at full detail at every level.
    if (AppGraphLayerOfType(g, ImGuiAppLayerType_Display) == nullptr)
      return true;
    return AppScopeCurrent(g) == AppScopeParentOf(g, n->Id);
  }

  // Identity-card summary line: "2 fields \xc2\xb7 1 event \xc2\xb7 emits SetLevel". Empty when
  // there is nothing to fold (the deps/data pin rows still identify the node).
  static void AppNodeSummaryLine(const ImGuiAppGraph* g, const ImGuiAppNode* n, char* buf, int buf_size)
  {
    if (buf_size > 0)
      buf[0] = '\0';
    if (n->Kind != ImGuiAppNodeKind_Control || buf_size <= 0)
      return;
    ImVector<ImGuiAppFieldDesc> persists;
    ImVector<ImGuiAppFieldDesc> temps;
    AppNodeEffectiveFields(g, n, 0, &persists);
    AppNodeEffectiveFields(g, n, 1, &temps);
    const int fields = persists.Size + temps.Size;
    int len = 0;
    if (fields > 0)
      len += ImFormatString(buf + len, (size_t)(buf_size - len), "%d field%s", fields, fields == 1 ? "" : "s");
    if (n->Events.Size > 0 && len < buf_size - 1)
      len += ImFormatString(buf + len, (size_t)(buf_size - len), "%s%d event%s", len > 0 ? " \xc2\xb7 " : "", n->Events.Size, n->Events.Size == 1 ? "" : "s");
    if (n->Commands.Size > 0 && len < buf_size - 1)
    {
      if (n->Commands.Size == 1)
        ImFormatString(buf + len, (size_t)(buf_size - len), "%semits %s", len > 0 ? " \xc2\xb7 " : "", n->Commands.Data[0].Name);
      else
        ImFormatString(buf + len, (size_t)(buf_size - len), "%semits %d commands", len > 0 ? " \xc2\xb7 " : "", n->Commands.Size);
    }
  }

  void AppNodeConfigSummary(const ImGuiAppNode* n, char* buf, int buf_size)
  {
    if (buf_size > 0)
      buf[0] = '\0';
    if (n == nullptr || buf_size <= 0)
      return;
    int len = 0;
    if (n->Kind == ImGuiAppNodeKind_Window)
    {
      if (n->HasInitialPlacement)
        len += ImFormatString(buf, (size_t)buf_size, "%.0fx%.0f @ (%.0f,%.0f)",
                              n->InitialSize.x, n->InitialSize.y, n->InitialPos.x, n->InitialPos.y);
    }
    else if (n->Kind == ImGuiAppNodeKind_Sidebar)
    {
      int cur = 0;
      for (int i = 0; i < IM_ARRAYSIZE(kAppDockDirs); i++)
        if (n->DockDir == kAppDockDirs[i])
          cur = i;
      if (n->DockSize > 0.0f)
        len += ImFormatString(buf, (size_t)buf_size, "dock %s \xc2\xb7 %.0f px", kAppDockDirNames[cur], n->DockSize);
      else
        len += ImFormatString(buf, (size_t)buf_size, "dock %s \xc2\xb7 auto", kAppDockDirNames[cur]);
    }
    else
    {
      return;   // only placement/dock kinds carry a readout
    }
    if ((n->Flags & ImGuiWindowFlags_AlwaysAutoResize) && len < buf_size - 1)
      ImFormatString(buf + len, (size_t)(buf_size - len), "%sAlwaysAutoResize", len > 0 ? " \xc2\xb7 " : "");
  }

  // Walls render only for scopes whose owner has a card silhouette to become the room
  // (window/sidebar first slice; layer scopes keep their phase bands).
  static bool AppScopeWallsWanted(const ImGuiAppGraph* g)
  {
    const int top = AppScopeCurrent(g);
    const ImGuiAppNode* tn = top >= 0 ? AppGraphFindNodeConst(g, top) : nullptr;
    return tn != nullptr && (tn->Kind == ImGuiAppNodeKind_Window || tn->Kind == ImGuiAppNodeKind_Sidebar);
  }

  // Scope walls: the room drawn as the code block it generates. The face band (top wall) IS the
  // Begin("name") line with the runs order-strip row beneath it; the end band (bottom wall)
  // closes with End(); the side edges thicken into rails where portal chips dock; everything
  // OUTSIDE the walls dims (figure-ground: inside is stated by light, not by fill).
  // Pre-submission on the background channel, model bounds + THIS frame's camera (the group_box
  // transform discipline). Publishes ScopeWallRect + ScopeStripRow (model units) for the
  // post-CanvasEnd strip/portal passes. em_base/fh_base are the zoom-free font metrics captured
  // before CanvasBegin. An empty scope publishes nothing (the empty CTA owns that state).
  // Bounds grow instantly and shrink only past a deadband (docs/phase-coherence.md 1b -- the
  // fixed point: the rect is stable while every edge is within the deadband of its target).
  static void AppDrawScopeWalls(ImGuiAppGraph* g, ImGuiCanvasState* cv, bool show_live, float em_base, float fh_base)
  {
    IM_UNUSED(fh_base);
    ImGuiAppEditorState* ed = AppGraphEditorState(g);
    const bool was_valid = ed->ScopeWallValid;
    ed->ScopeWallValid = false;
    if (!AppScopeWallsWanted(g))
      return;
    const int top = AppScopeCurrent(g);
    const ImGuiAppNode* tn = AppGraphFindNodeConst(g, top);

    // Members' model bounds -- the same filters the submission loop applies. Positions follow the
    // group-frame discipline (engine pos when submitted, else this scope's model placement); a
    // still-unseated member (scope-transition frame) makes the bounds unknowable -- skip a frame.
    ImVec2 mn(FLT_MAX, FLT_MAX);
    ImVec2 mx(-FLT_MAX, -FLT_MAX);
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if ((!show_live && n->IsLive) || AppNodeHiddenByCollapse(g, n->Id))
        continue;
      if (n->_NeedsPlace)
        return;
      const ImVec2 p = AppEditorNodeWasSubmitted(g, n->Id) ? AppCanvasNodePos(g, n->Id) : AppNodeScopePos(g, n);
      ImVec2 m;
      if (!AppNodeModelSize(g, n->Id, &m))
        m = AppLayoutNodeSize(g, n);
      mn = ImMin(mn, p);
      mx = ImMax(mx, p + m);
    }
    if (mn.x > mx.x)
      return;

    // Chrome in MODEL units, zoom-free bases (the group_box idiom).
    const float em = ImGui::GetFontSize();   // zoomed content font: screen-space text metrics
    const float sc = AppCanvasScale(g);
    const float em_m = em_base * ImGui::CanvasGetZoom(cv) / sc;
    const float pad_m = em_m * 0.9f;
    // Every band metric is an em multiple: em scales with zoom, so the band's proportions are
    // identical at every zoom (FrameHeight carries additive style padding and is NOT proportional).
    const float tpad_m = em_m * 0.45f;              // clear air above the Begin line
    const float row1_m = em_m * 1.3f;               // the Begin line
    const float rgap_m = em_m * 0.8f;               // clear air between the Begin line and the strip
    const float row2_m = em_m * 1.3f;               // the runs strip row
    const float bpad_m = em_m * 0.45f;              // clear air between the strip and the band rule
    const float band_m = tpad_m + row1_m + rgap_m + row2_m + bpad_m;   // face band = both rows + their spacing, one plate
    const float end_m = em_m * 1.1f;                // the End() band
    const float rail_m = em_m * 1.0f;               // portal rails
    ImVec4 tgt(mn.x - (pad_m + rail_m), mn.y - (band_m + pad_m * 0.6f),
               mx.x + pad_m + rail_m, mx.y + pad_m + end_m);

    // The walls CONTAIN their face band: measure the Begin line and the full runs strip (label +
    // chips + separators, at the strip's own type scale) and widen the room to fit whichever is
    // longer -- the band never overflows its wall.
    {
      const char* mname = tn->Draft.Name[0] ? tn->Draft.Name : AppNodeKindName(tn->Kind);
      char mcfg[96];
      AppNodeConfigSummary(tn, mcfg, IM_ARRAYSIZE(mcfg));
      const float type_cap = em * AppComposerGetMotion()->TypeCaption;   // F39: strip text on the caption tier
      ImGui::PushFont(ed->CodeFont, type_cap);
      float row1_need = ImGui::CalcTextSize("Begin(\"").x + ImGui::CalcTextSize(mname).x + ImGui::CalcTextSize("\")").x;
      ImGui::PopFont();
      ImGui::PushFont(nullptr, type_cap);
      row1_need += em * 0.5f + ImGui::CalcTextSize(AppNodeKindTag(tn->Kind)).x + (mcfg[0] ? em * 1.0f + ImGui::CalcTextSize(mcfg).x : 0.0f);
      ImGui::PopFont();

      ImVector<int> mseq;
      AppScopeSequenceIds(g, &mseq);
      float row2_need = em * 1.5f;   // the strip's paragraph indent
      ImGui::PushFont(nullptr, type_cap);
      const float arrow_w = em * 0.45f;   // draw-list chevron (the arrow glyph is not in the atlas)
      for (int i = 0; i < mseq.Size; i++)
      {
        const ImGuiAppNode* n = AppGraphFindNodeConst(g, mseq.Data[i]);
        if (n == nullptr)
          continue;
        char mnum[8];
        ImFormatString(mnum, IM_ARRAYSIZE(mnum), "%d", i + 1);
        row2_need += ImGui::CalcTextSize(mnum).x + ImGui::CalcTextSize(n->Draft.Name[0] ? n->Draft.Name : "(unnamed)").x + em * 1.1f;
        if (i < mseq.Size - 1)
          row2_need += arrow_w + em * 1.0f;
      }
      ImGui::PopFont();
      const float need_m = (ImMax(row1_need, row2_need) + em * 1.5f) / sc + rail_m * 2.0f;
      if (tgt.z - tgt.x < need_m)
        tgt.z = tgt.x + need_m;
    }

    // Grow-fast / shrink-slow: expansion applies immediately, contraction only past the deadband.
    ImVec4 wall = ed->ScopeWallRect;
    if (!was_valid || ed->ScopeWallScope != top)
      wall = tgt;
    else
    {
      const float dead = em_m * 1.5f;
      wall.x = (tgt.x < wall.x || tgt.x > wall.x + dead) ? tgt.x : wall.x;
      wall.y = (tgt.y < wall.y || tgt.y > wall.y + dead) ? tgt.y : wall.y;
      wall.z = (tgt.z > wall.z || tgt.z < wall.z - dead) ? tgt.z : wall.z;
      wall.w = (tgt.w > wall.w || tgt.w < wall.w - dead) ? tgt.w : wall.w;
    }
    ed->ScopeWallRect = wall;
    ed->ScopeStripRow = ImVec4(wall.x, wall.y + tpad_m + row1_m + rgap_m, wall.z, wall.y + tpad_m + row1_m + rgap_m + row2_m);
    ed->ScopeWallScope = top;
    ed->ScopeWallValid = true;

    const ImVec2 smn = ImGui::CanvasToScreen(cv, ImVec2(wall.x, wall.y));
    const ImVec2 smx = ImGui::CanvasToScreen(cv, ImVec2(wall.z, wall.w));
    const float band_h = band_m * sc;
    const float row1_h = row1_m * sc;
    const float end_h = end_m * sc;
    const float rail_w = rail_m * sc;
    const float rounding = 2.0f * sc;   // the owner card's squared silhouette, at wall size
    const float line_w = ImMax(1.0f, em * 0.09375f);
    const ImU32 kind_col = AppKindColor(tn->Kind);
    const ImU32 band_bg = AppComposerGetStyle()->GroupTitleBg;
    const ImU32 muted = AppComposerGetStyle()->TextMuted;
    ImDrawList* dl = ImGui::CanvasBackgroundDrawList(cv);

    // The void: outside the block, the canvas dims. Clipped by the canvas child.
    const float big = 100000.0f;
    const ImU32 void_col = AppThemeDark(0.45f);
    dl->AddRectFilled(ImVec2(smn.x - big, smn.y - big), ImVec2(smx.x + big, smn.y), void_col);
    dl->AddRectFilled(ImVec2(smn.x - big, smx.y), ImVec2(smx.x + big, smx.y + big), void_col);
    dl->AddRectFilled(ImVec2(smn.x - big, smn.y), ImVec2(smn.x, smx.y), void_col);
    dl->AddRectFilled(ImVec2(smx.x, smn.y), ImVec2(smx.x + big, smx.y), void_col);

    // Face band (Begin line + strip row), end band, rails, outline, kind-hue rule.
    dl->AddRectFilled(smn, ImVec2(smx.x, smn.y + band_h), band_bg, rounding, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(ImVec2(smn.x, smx.y - end_h), smx, band_bg, rounding, ImDrawFlags_RoundCornersBottom);
    dl->AddRectFilled(ImVec2(smn.x, smn.y + band_h), ImVec2(smn.x + rail_w, smx.y - end_h), AppThemeNeutral(0.14f, 0.85f));
    dl->AddRectFilled(ImVec2(smx.x - rail_w, smn.y + band_h), ImVec2(smx.x, smx.y - end_h), AppThemeNeutral(0.14f, 0.85f));
    dl->AddRect(smn, smx, AppComposerGetStyle()->GroupOutline, rounding, 0, line_w);
    dl->AddLine(ImVec2(smn.x, smn.y + band_h), ImVec2(smx.x, smn.y + band_h), kind_col, ImMax(1.0f, em * 0.0625f));
    dl->AddLine(ImVec2(smn.x, smx.y - end_h), ImVec2(smx.x, smx.y - end_h), AppThemeNeutral(0.30f, 0.8f), 1.0f);

    // Begin("Name") -- the call is the wall: Begin( muted, the name in the kind hue, ) muted.
    // Kind word after; config readout right-aligned. The strip row beneath is drawn by the
    // post-CanvasEnd order-strip pass (its chips are interactive). Chrome text is quieter than
    // node content: node-title scale, clamped so it can never fill its band.
    const char* name = tn->Draft.Name[0] ? tn->Draft.Name : AppNodeKindName(tn->Kind);
    const float row1_top = smn.y + tpad_m * sc;
    const float call_sz = em * AppComposerGetMotion()->TypeCaption;   // F39 caption tier: same ratio to its row at every zoom
    ImGui::PushFont(ed->CodeFont, call_sz);
    char idb[IM_LABEL_SIZE + 2];
    ImFormatString(idb, IM_ARRAYSIZE(idb), "\"%s\"", name);
    const float ty = row1_top + (row1_h - ImGui::GetTextLineHeight()) * 0.5f;
    float tx = smn.x + em * 0.75f;
    dl->AddText(ImVec2(tx, ty), muted, "Begin(");
    tx += ImGui::CalcTextSize("Begin(").x;
    dl->AddText(ImVec2(tx, ty), kind_col, idb);
    tx += ImGui::CalcTextSize(idb).x;
    dl->AddText(ImVec2(tx, ty), muted, ")");
    tx += ImGui::CalcTextSize(")").x + em * 0.5f;
    const float ey = smx.y - end_h + (end_h - ImGui::GetTextLineHeight()) * 0.5f;
    dl->AddText(ImVec2(smn.x + em * 0.75f, ey), muted, "End()");
    ImGui::PopFont();
    ImGui::PushFont(nullptr, call_sz);   // F39: kind readout shares the caption tier (was an off-ladder 0.7)
    const char* kind_word = AppNodeKindTag(tn->Kind);
    const float ky = row1_top + (row1_h - ImGui::GetTextLineHeight()) * 0.5f;
    dl->AddText(ImVec2(tx, ky), muted, kind_word);
    tx += ImGui::CalcTextSize(kind_word).x;
    char cfg[96];
    AppNodeConfigSummary(tn, cfg, IM_ARRAYSIZE(cfg));
    if (cfg[0])
    {
      const ImVec2 cs = ImGui::CalcTextSize(cfg);
      const float cx = smx.x - em * 0.75f - cs.x;
      if (cx > tx + em)   // drop the readout before it collides with the identity (never truncate mid-fact)
        dl->AddText(ImVec2(cx, ky), muted, cfg);
    }
    ImGui::PopFont();
  }

  // The runs order strip: the face band's second row. One chip per member in execution order --
  // ordinal in the scope accent + name -- hover halos the member (brushing bus), click selects.
  // Post-CanvasEnd on the annotation list (chips are interactive; overlay hit-test rule).
  // Publishes chip rects (screen space, this frame); the coming sequence-reorder drag rides them.
  static void AppDrawScopeOrderStrip(ImGuiAppGraph* g, ImVec2 editor_min, ImVec2 editor_size, int* selected_node_id)
  {
    ImGuiAppEditorState* ed = AppGraphEditorState(g);
    ed->ScopeStripRects.resize(0);
    ed->ScopeStripNodes.resize(0);
    if (!ed->ScopeWallValid)
      return;
    ImVector<int> seq;
    AppScopeSequenceIds(g, &seq);
    if (seq.Size == 0)
      return;

    ImGuiCanvasState* cv = AppEditorCanvas(g);
    const float z = AppCanvasScale(g);
    const float em = ImGui::GetFontSize() * z;
    ImDrawList* dl = ImGui::CanvasAnnotationDrawList(cv);
    dl->PushClipRect(editor_min, editor_min + editor_size, true);
    const bool win_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const ImU32 accent = AppScopeAccent(g);

    const ImVec2 row_mn = ImGui::CanvasToScreen(cv, ImVec2(ed->ScopeStripRow.x, ed->ScopeStripRow.y));
    const ImVec2 row_mx = ImGui::CanvasToScreen(cv, ImVec2(ed->ScopeStripRow.z, ed->ScopeStripRow.w));
    const float cy = (row_mn.y + row_mx.y) * 0.5f;
    const float right_limit = row_mx.x - em * 0.75f;   // row content never touches the wall edge
    float x = row_mn.x + em * 2.25f;   // paragraph indent: the strip opens 1.5 em deeper than its Begin line

    // One type scale for the whole row (em multiple: zoom-proportional); chips keep air inside the row.
    int clicked = -1;
    ImGui::PushFont(nullptr, em * AppComposerGetMotion()->TypeCaption);   // F39 caption tier
    const float th = ImGui::GetTextLineHeight();
    const float ch = (row_mx.y - row_mn.y) * 0.92f;
    const float arrow_w = em * 0.45f;   // draw-list chevron (the arrow glyph is not in the atlas)
    for (int i = 0; i < seq.Size; i++)
    {
      const ImGuiAppNode* n = AppGraphFindNodeConst(g, seq.Data[i]);
      if (n == nullptr)
        continue;
      const char* nm = n->Draft.Name[0] ? n->Draft.Name : "(unnamed)";
      char num[8];
      ImFormatString(num, IM_ARRAYSIZE(num), "%d", i + 1);
      const float num_w = ImGui::CalcTextSize(num).x;
      const float nm_w = ImGui::CalcTextSize(nm).x;
      const float cw = em * 0.4f + num_w + em * 0.35f + nm_w + em * 0.4f;   // pad . num . gap . name . pad
      const float sep_w = i > 0 ? em * 0.5f + arrow_w + em * 0.5f : 0.0f;

      // Fold the tail the moment the next chip (plus its separator) cannot fit -- stated, never clipped.
      if (x + sep_w + cw > right_limit)
      {
        char more[16];
        ImFormatString(more, IM_ARRAYSIZE(more), "+%d", seq.Size - i);
        dl->AddText(ImVec2(x + em * 0.2f, cy - th * 0.5f), AppComposerGetStyle()->TextMuted, more);
        break;
      }
      if (i > 0)
      {
        const float ax = x + em * 0.5f;
        dl->AddTriangleFilled(ImVec2(ax, cy - arrow_w * 0.45f), ImVec2(ax + arrow_w * 0.7f, cy),
                              ImVec2(ax, cy + arrow_w * 0.45f), AppThemeNeutral(0.42f));
        x += sep_w;
      }

      const ImVec2 cmn(x, cy - ch * 0.5f);
      const ImVec2 cmx(x + cw, cy + ch * 0.5f);
      const bool hov = win_hovered && ImGui::IsMouseHoveringRect(cmn, cmx);
      dl->AddRectFilled(cmn, cmx, AppThemeNeutral(hov ? 0.20f : 0.13f, 0.95f), 3.0f * z);
      dl->AddRect(cmn, cmx, hov ? AppColWithAlpha(accent, 0.65f) : AppThemeNeutral(0.32f, 0.8f), 3.0f * z, 0, 1.0f);
      dl->AddText(ImVec2(x + em * 0.4f, cy - th * 0.5f), accent, num);
      dl->AddText(ImVec2(x + em * 0.4f + num_w + em * 0.35f, cy - th * 0.5f), ImGui::GetColorU32(ImGuiCol_Text, AppNodeHiddenByCollapse(g, n->Id) ? 0.5f : 0.9f), nm);
      ed->ScopeStripRects.push_back(ImVec4(cmn.x, cmn.y, cmx.x, cmx.y));
      ed->ScopeStripNodes.push_back(n->Id);
      if (hov)
      {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        AppGraphHoverNode(g, n->Id, ImGuiAppHoverSource_External);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
          clicked = n->Id;
      }
      x = cmx.x;
    }
    ImGui::PopFont();
    dl->PopClipRect();

    if (clicked >= 0)
    {
      g->Selection.clear();
      g->Selection.push_back(clicked);
      if (selected_node_id != nullptr)
        *selected_node_id = clicked;
    }
  }

  void AppScopeCollectPortals(const ImGuiAppGraph* g, ImVector<ImGuiAppScopePortal>* out)
  {
    out->resize(0);
    if (AppScopeCurrent(g) < 0)
      return;
    for (int li = 0; li < g->Links.Size; li++)
    {
      const ImGuiAppNodeLink* l = &g->Links.Data[li];
      if (l->Kind != ImGuiAppEdgeKind_Data)
        continue;
      const int producer = AppGraphPortOwnerId(g, l->StartAttr);
      const int consumer = AppGraphPortOwnerId(g, l->EndAttr);
      if (producer < 0 || consumer < 0)
        continue;
      const bool producer_in = AppNodeInScope(g, producer);
      const bool consumer_in = AppNodeInScope(g, consumer);
      if (producer_in == consumer_in)
        continue;   // fully inside (normal wire) or fully outside (not this scope's concern)
      ImGuiAppScopePortal p;
      p.LinkId = l->Id;
      p.Inbound = consumer_in;
      p.InsidePortId = consumer_in ? l->EndAttr : l->StartAttr;
      p.OutsideNodeId = consumer_in ? producer : consumer;
      out->push_back(p);
    }
  }

  // Jump navigation shared by portal chips (and future deep links): ViewScope becomes the target's
  // scope-parent chain, the target becomes the selection. The scope-change handler owns the camera
  // (per-branch memory, else deferred fit) -- no explicit centering here.
  static void AppScopeJumpToNode(ImGuiAppGraph* g, int node_id, int* selected_node_id)
  {
    int chain[16];
    int depth = 0;
    for (int cur = AppScopeParentOf(g, node_id); cur >= 0 && depth < IM_ARRAYSIZE(chain); cur = AppScopeParentOf(g, cur))
      chain[depth++] = cur;
    g->ViewScope.clear();
    for (int i = depth - 1; i >= 0; i--)
      if (AppScopeCanEnter(AppGraphFindNodeConst(g, chain[i])))
        g->ViewScope.push_back(chain[i]);
    g->Selection.clear();
    g->Selection.push_back(node_id);
    if (selected_node_id != nullptr)
      *selected_node_id = node_id;
  }

  // Portal chips: a wall-docked pill per boundary-crossing data edge, at the in-scope pin's row
  // height (inbound producers on the left wall, outbound consumers on the right), wired to the
  // real pin. Hover brushes the off-scope node; click jumps to its scope. Post-CanvasEnd on the
  // annotation list; hit-tests follow the overlay rule (AllowWhenBlockedByActiveItem).
  static void AppDrawScopePortals(ImGuiAppGraph* g, ImVec2 editor_min, ImVec2 editor_size, int* selected_node_id)
  {
    ImGuiAppEditorState* ed = AppGraphEditorState(g);
    if (!ed->ScopeWallValid)
      return;
    ImVector<ImGuiAppScopePortal> portals;
    AppScopeCollectPortals(g, &portals);
    if (portals.Size == 0)
      return;

    ImGuiCanvasState* cv = AppEditorCanvas(g);
    const float z = AppCanvasScale(g);
    const float em = ImGui::GetFontSize() * z;
    ImDrawList* dl = ImGui::CanvasAnnotationDrawList(cv);
    dl->PushClipRect(editor_min, editor_min + editor_size, true);
    const bool win_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    int jump_to = -1;
    ImGui::PushFont(nullptr, em);
    for (int i = 0; i < portals.Size; i++)
    {
      const ImGuiAppScopePortal* p = &portals.Data[i];
      ImGuiAppNode* inside_owner = nullptr;
      AppGraphFindPort(g, p->InsidePortId, &inside_owner);
      const ImGuiAppNode* remote = AppGraphFindNodeConst(g, p->OutsideNodeId);
      if (inside_owner == nullptr || remote == nullptr || !AppEditorNodeWasSubmitted(g, inside_owner->Id))
        continue;

      const ImVec2 pin_m = ImGui::CanvasPinPos(cv, p->InsidePortId);
      const ImVec2 pin_s = ImGui::CanvasToScreen(cv, pin_m);

      char label[IM_LABEL_SIZE + 8];
      const char* rname = remote->Draft.Name[0] ? remote->Draft.Name : "(unnamed)";
      if (p->Inbound)
        ImFormatString(label, IM_ARRAYSIZE(label), "\xe2\x96\xb8 %s", rname);
      else
        ImFormatString(label, IM_ARRAYSIZE(label), "%s \xe2\x96\xb8", rname);
      const ImVec2 ts = ImGui::CalcTextSize(label);
      const float ch = em * 1.5f;
      const float cw = ts.x + em * 1.2f;

      // Chip center rides the wall edge at the pin's row.
      const float wall_x_m = p->Inbound ? ed->ScopeWallRect.x : ed->ScopeWallRect.z;
      const ImVec2 dock = ImGui::CanvasToScreen(cv, ImVec2(wall_x_m, pin_m.y));
      const ImVec2 mn(dock.x - cw * 0.5f, dock.y - ch * 0.5f);
      const ImVec2 mx(mn.x + cw, mn.y + ch);

      // Wire first (under the chip): chip edge -> the real pin, data hue.
      const ImVec2 a = p->Inbound ? ImVec2(mx.x, dock.y) : ImVec2(mn.x, dock.y);
      const float bend = ImFabs(pin_s.x - a.x) * 0.5f + em * 0.5f;
      dl->AddBezierCubic(a, ImVec2(a.x + (p->Inbound ? bend : -bend), a.y),
                         ImVec2(pin_s.x + (p->Inbound ? -bend : bend), pin_s.y), pin_s,
                         AppComposerGetStyle()->PinData, ImMax(1.0f, em * 0.11f));

      const ImU32 kcol = AppKindColor(remote->Kind);
      const bool hov = win_hovered && ImGui::IsMouseHoveringRect(mn, mx);
      dl->AddRectFilled(mn, mx, AppThemeNeutral(hov ? 0.18f : 0.11f, 0.98f), ch * 0.5f);
      dl->AddRect(mn, mx, AppColWithAlpha(kcol, hov ? 0.9f : 0.45f), ch * 0.5f, 0, ImMax(1.0f, em * 0.07f));
      dl->AddText(ImVec2(mn.x + em * 0.6f, dock.y - ts.y * 0.5f), AppColWithAlpha(kcol, hov ? 1.0f : 0.75f), label);

      if (hov)
      {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip(p->Inbound ? "from %s -- click to open its scope" : "feeds %s -- click to open its scope", rname);
        AppGraphHoverNode(g, p->OutsideNodeId, ImGuiAppHoverSource_Canvas);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
          jump_to = p->OutsideNodeId;
      }
    }
    ImGui::PopFont();
    dl->PopClipRect();
    if (jump_to >= 0)
      AppScopeJumpToNode(g, jump_to, selected_node_id);
  }

  // Pin color by port kind: data pins blue, containment pins orange (tie pins use AppPinTieColor green).
  static ImU32 AppPinColor(ImGuiAppPortKind kind)
  {
    switch (kind)
    {
    case ImGuiAppPortKind_DataIn:
    case ImGuiAppPortKind_DataOut:  return AppComposerGetStyle()->PinData;
    case ImGuiAppPortKind_ChildIn:
    case ImGuiAppPortKind_ChildOut: return AppComposerGetStyle()->PinChild;
    default:                        return AppComposerGetStyle()->PinDefault;
    }
  }
  static ImU32 AppPinTieColor() { return AppComposerGetStyle()->PinTie; }

  // Breadcrumb at the canvas top-left: "App > DisplayLayer > Mixer". Always shown; drilled in,
  // clicking a segment jumps back to that depth. A CHILD window overlaying the canvas, not
  // draw-list buttons in the canvas window (the canvas item is submitted first and owns the
  // mouse) and not a top-level window (any click on the host window raises the host above it).
  static void AppDrawScopeBreadcrumb(ImGuiAppGraph* g, int* selected_node_id, ImVec2 editor_min)
  {
    const float em = ImGui::GetFontSize();
    const float h = ImGui::GetFrameHeight();
    int jump = -1;   // depth to pop back to (0 = root)

    // Callers treat the canvas as the editor's LAST ITEM (GetItemRect* after ShowAppGraphEditor);
    // the overlay child must not become it.
    ImGuiContext& ictx = *ImGui::GetCurrentContext();
    const ImGuiLastItemData last_item = ictx.LastItemData;

    ImGui::SetNextWindowPos(ImVec2(editor_min.x + em * 0.8f, editor_min.y + em * 0.6f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em * 0.1f, em * 0.1f));
    if (ImGui::BeginChild("##scope_breadcrumb", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings))
    {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      for (int i = 0; i <= g->ViewScope.Size; i++)
      {
        const bool is_tail = i == g->ViewScope.Size;
        char label[IM_LABEL_SIZE + 8];
        ImU32 accent = AppComposerGetStyle()->AccentNeutral;
        if (i == 0)
          ImStrncpy(label, ICON_FA_CIRCLE_NODES "  App", IM_ARRAYSIZE(label));
        else
        {
          const ImGuiAppNode* n = AppGraphFindNodeConst(g, g->ViewScope.Data[i - 1]);
          if (n == nullptr)
            break;
          const char* nm = n->Kind == ImGuiAppNodeKind_Layer ? AppLayerNodeName(n->LayerType) : (n->Draft.Name[0] ? n->Draft.Name : "(unnamed)");
          ImFormatString(label, IM_ARRAYSIZE(label), "%s  %s", AppNodeIcon(n), nm);
          accent = n->Kind == ImGuiAppNodeKind_Layer ? AppLayerAccent(n->LayerType) : AppKindColor(n->Kind);
        }
        const ImVec2 ts = ImGui::CalcTextSize(label);
        const ImVec2 seg(ts.x + em * 0.9f, h);

        if (i > 0)
          ImGui::SameLine(0.0f, em * 0.25f);
        ImGui::PushID(i);
        bool hov = false;
        if (is_tail)
        {
          ImGui::Dummy(seg);
        }
        else if (ImGui::InvisibleButton("##seg", seg))
        {
          jump = i;
        }
        if (!is_tail)
        {
          hov = ImGui::IsItemHovered();
          if (hov)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        ImGui::PopID();

        const ImVec2 mn = ImGui::GetItemRectMin();
        const ImVec2 mx = ImGui::GetItemRectMax();
        const ImU32 bg = is_tail ? AppScaleRGB(accent, 0.45f)
                                 : ImGui::GetColorU32(hov ? ImGuiCol_ButtonHovered : ImGuiCol_Button, 0.9f);
        dl->AddRectFilled(mn, mx, bg, em * 0.25f);
        if (is_tail)
          dl->AddRect(mn, mx, (accent & 0x00FFFFFF) | 0xC8000000, em * 0.25f);
        dl->AddText(ImVec2(mn.x + em * 0.45f, mn.y + (h - ts.y) * 0.5f),
                    is_tail ? AppThemeNeutral(0.94f) : ImGui::GetColorU32(ImGuiCol_Text, hov ? 1.0f : 0.75f), label);
        if (!is_tail)
        {
          ImGui::SameLine(0.0f, em * 0.25f);
          const ImVec2 ss = ImGui::CalcTextSize(ICON_FA_ANGLE_RIGHT);
          const ImVec2 sp = ImGui::GetCursorScreenPos();
          ImGui::Dummy(ImVec2(ss.x, h));
          dl->AddText(ImVec2(sp.x, sp.y + (h - ss.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), ICON_FA_ANGLE_RIGHT);
        }
      }

    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    const ImVec2 crumb_max = ImGui::GetItemRectMax();   // the child item's rect

    // Scope contract caption, passive text in the PARENT window (inside the child it would widen
    // the strip of canvas the overlay steals from panning/clicks).
    const char* cap = AppScopeCaption(g);
    if (cap[0])
      ImGui::GetWindowDrawList()->AddText(ImVec2(editor_min.x + em * 0.9f, crumb_max.y + em * 0.3f),
                                          ImGui::GetColorU32(ImGuiCol_TextDisabled), cap);

    // The overlay must not become the editor's last item (see above).
    ictx.LastItemData = last_item;

    if (jump >= 0)
    {
      const int exited = g->ViewScope.Data[jump];
      g->ViewScope.resize(jump);
      if (selected_node_id != nullptr)
        *selected_node_id = exited;   // land the eye on the scope just left (reveal runs next frame)
    }
  }

  // Centered call-to-action inside an empty scope: name what executes here and offer the contextual
  // first build step.
  static void AppDrawScopeEmptyCTA(ImGuiAppGraph* g, bool show_live, ImVec2 editor_min, ImVec2 editor_size)
  {
    const int top = AppScopeCurrent(g);
    ImGuiAppNode* tn = top >= 0 ? AppGraphFindNode(g, top) : nullptr;
    if (tn == nullptr)
      return;
    for (int i = 0; i < g->Nodes.Size; i++)   // any visible member -> no CTA
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if ((!show_live && n->IsLive) || n->Id == top)
        continue;
      if (!AppNodeHiddenByCollapse(g, n->Id) && AppNodeInScope(g, n->Id))
        return;
    }

    // Capture what we dispatch on BEFORE any add (AppGraphAddNode reallocates g->Nodes and invalidates tn).
    const ImGuiAppNodeKind kind = tn->Kind;
    const ImGuiAppLayerType lt = tn->LayerType;
    const bool live_owner = tn->IsLive && kind != ImGuiAppNodeKind_Layer;   // read-only mirror: no build step offered
    const float em = ImGui::GetFontSize();
    char head[IM_LABEL_SIZE + 48];
    ImFormatString(head, IM_ARRAYSIZE(head), "%s  %s -- nothing composed here yet", AppNodeIcon(tn),
                   kind == ImGuiAppNodeKind_Layer ? AppLayerNodeName(lt) : tn->Draft.Name);
    const char* sub = live_owner ? "live mirror (read-only) -- promote a member to author against it" : AppScopeCaption(g);

    const char* action = nullptr;
    if (live_owner)                                                             action = nullptr;
    else if (kind == ImGuiAppNodeKind_Layer && lt == ImGuiAppLayerType_Display) action = "+ Window";
    else if (kind == ImGuiAppNodeKind_Layer && lt == ImGuiAppLayerType_Task)    action = "+ Control";
    else if (kind == ImGuiAppNodeKind_Window || kind == ImGuiAppNodeKind_Sidebar) action = "+ Control";
    else if (kind == ImGuiAppNodeKind_Struct)                                   action = "+ Field";
    else if (kind == ImGuiAppNodeKind_Control)                                  action = "Explode PersistData";

    const ImVec2 hs = ImGui::CalcTextSize(head);
    const ImVec2 ss = ImGui::CalcTextSize(sub);
    const float bw = action != nullptr ? ImGui::CalcTextSize(action).x + em * 2.0f : 0.0f;
    const float pw = ImMax(ImMax(hs.x, ss.x), bw) + em * 2.5f;
    const float ph = em * (action != nullptr ? 8.0f : 6.0f);
    const ImVec2 cen(editor_min.x + editor_size.x * 0.5f, editor_min.y + editor_size.y * 0.5f);
    const ImVec2 mn(cen.x - pw * 0.5f, cen.y - ph * 0.5f);
    const ImVec2 mx(cen.x + pw * 0.5f, cen.y + ph * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 accent = AppScopeAccent(g);
    dl->AddRectFilled(mn, mx, AppThemeNeutral(0.06f, 0.94f), em * 0.5f);
    dl->AddRect(mn, mx, (accent & 0x00FFFFFF) | 0xC8000000, em * 0.5f, 0, ImMax(1.0f, em * 0.09375f));
    dl->AddText(ImVec2(cen.x - hs.x * 0.5f, mn.y + em * 1.0f), AppThemeNeutral(0.92f), head);
    dl->AddText(ImVec2(cen.x - ss.x * 0.5f, mn.y + em * 2.6f), ImGui::GetColorU32(ImGuiCol_TextDisabled), sub);

    if (action != nullptr)
    {
      ImGui::SetCursorScreenPos(ImVec2(cen.x - bw * 0.5f, mx.y - em * 2.6f));
      if (ImGui::Button(action, ImVec2(bw, em * 1.7f)))
      {
        int added_id = -1;
        if (kind == ImGuiAppNodeKind_Layer && lt == ImGuiAppLayerType_Display)
          added_id = AppGraphAddNode(g, ImGuiAppNodeKind_Window, "Window")->Id;
        else if ((kind == ImGuiAppNodeKind_Layer && lt == ImGuiAppLayerType_Task)
              || kind == ImGuiAppNodeKind_Window || kind == ImGuiAppNodeKind_Sidebar)
          added_id = AppGraphAddNode(g, ImGuiAppNodeKind_Control, "NewControl")->Id;
        else if (kind == ImGuiAppNodeKind_Struct)
          added_id = AppScopeAddFieldToStruct(g, top);
        else if (kind == ImGuiAppNodeKind_Control)
          AppGraphExplodeControlData(g, tn, false);   // no add ran in this branch, tn still valid
        if (added_id >= 0)
          AppScopeComposeNewNode(g, added_id, nullptr);   // host it in this scope so it appears where it was created
      }
    }
  }

  // Command registry (F34): one table is the single source for the editor's verbs -- id, icon, label,
  // shortcut, key, and which surfaces (palette / context menu / shortcut / gizmo) each declares. The Space
  // palette renders directly from it; the completeness test iterates it and checks each verb is reachable
  // from every surface it declares. run() stays the Id-keyed dispatch in the palette (below).
  static const ImGuiAppEditorCommand s_editor_commands[] =
  {
    // Id  Icon  Label                          Shortcut  Key                     Mods            Surfaces                                                            AddKind
    {  0, "",   "Add: Control",                "",       ImGuiKey_None,          0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Menu | ImGuiAppCmdSurface_Gizmo, ImGuiAppNodeKind_Control },
    {  1, "",   "Add: Struct",                 "",       ImGuiKey_None,          0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Menu | ImGuiAppCmdSurface_Gizmo, ImGuiAppNodeKind_Struct  },
    {  2, "",   "Add: Window",                 "",       ImGuiKey_None,          0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Menu | ImGuiAppCmdSurface_Gizmo, ImGuiAppNodeKind_Window  },
    {  3, "",   "Add: Sidebar",                "",       ImGuiKey_None,          0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Menu | ImGuiAppCmdSurface_Gizmo, ImGuiAppNodeKind_Sidebar },
    {  4, "",   "Add: Custom Layer",           "",       ImGuiKey_None,          0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Menu | ImGuiAppCmdSurface_Gizmo, ImGuiAppNodeKind_Layer   },
    {  5, "",   "Add: Field",                  "",       ImGuiKey_None,          0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Menu | ImGuiAppCmdSurface_Gizmo, ImGuiAppNodeKind_Field   },
    { 10, "",   "Layout: Tidy",                "L",      ImGuiKey_L,             0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut | ImGuiAppCmdSurface_Gizmo, ImGuiAppNodeKind_COUNT },
    { 11, "",   "View: Fit all",               "Home",   ImGuiKey_Home,          0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut | ImGuiAppCmdSurface_Gizmo, ImGuiAppNodeKind_COUNT },
    { 12, "",   "View: Frame selection",       "F",      ImGuiKey_F,             0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut | ImGuiAppCmdSurface_Gizmo, ImGuiAppNodeKind_COUNT },
    { 13, "",   "Toggle: Snap to grid",        "G",      ImGuiKey_G,             0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut | ImGuiAppCmdSurface_Gizmo, ImGuiAppNodeKind_COUNT },
    { 25, "",   "View: Hide selection",        "H",      ImGuiKey_H,             0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut, ImGuiAppNodeKind_COUNT },
    { 26, "",   "View: Show all hidden",       "Alt+H",  ImGuiKey_H,             ImGuiMod_Alt,   ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut, ImGuiAppNodeKind_COUNT },
    { 27, "",   "Overlays: Grid",              "",       ImGuiKey_None,          0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Menu | ImGuiAppCmdSurface_Gizmo, ImGuiAppNodeKind_COUNT },
    { 28, "",   "Overlays: Phase bands",       "",       ImGuiKey_None,          0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Menu | ImGuiAppCmdSurface_Gizmo, ImGuiAppNodeKind_COUNT },
    { 29, "",   "Overlays: Group frames",      "",       ImGuiKey_None,          0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Menu | ImGuiAppCmdSurface_Gizmo, ImGuiAppNodeKind_COUNT },
    { 30, "",   "Overlays: Minimap",           "",       ImGuiKey_None,          0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Menu | ImGuiAppCmdSurface_Gizmo, ImGuiAppNodeKind_COUNT },
    { 14, "",   "Edit: Undo",                  "Ctrl+Z", ImGuiKey_Z,             ImGuiMod_Ctrl,  ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut, ImGuiAppNodeKind_COUNT },
    { 15, "",   "Edit: Redo",                  "Ctrl+Y", ImGuiKey_Y,             ImGuiMod_Ctrl,  ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut, ImGuiAppNodeKind_COUNT },
    { 16, "",   "Edit: Copy",                  "Ctrl+C", ImGuiKey_C,             ImGuiMod_Ctrl,  ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut, ImGuiAppNodeKind_COUNT },
    { 17, "",   "Edit: Paste",                 "Ctrl+V", ImGuiKey_V,             ImGuiMod_Ctrl,  ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut, ImGuiAppNodeKind_COUNT },
    { 18, "",   "Edit: Duplicate",             "Ctrl+D", ImGuiKey_D,             ImGuiMod_Ctrl,  ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut | ImGuiAppCmdSurface_Menu, ImGuiAppNodeKind_COUNT },
    { 19, "",   "Edit: Delete selection",      "Del",    ImGuiKey_Delete,        0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut | ImGuiAppCmdSurface_Menu, ImGuiAppNodeKind_COUNT },
    { 31, "",   "Edit: Rename selection",      "F2",     ImGuiKey_F2,            0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut, ImGuiAppNodeKind_COUNT },
    { 32, "",   "Order: Send to back",         "[",      ImGuiKey_LeftBracket,   0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut, ImGuiAppNodeKind_COUNT },
    { 33, "",   "Order: Bring to front",       "]",      ImGuiKey_RightBracket,  0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut, ImGuiAppNodeKind_COUNT },
    { 20, "",   "Groups: Collapse all",        "",       ImGuiKey_None,          0,              ImGuiAppCmdSurface_Palette, ImGuiAppNodeKind_COUNT },
    { 21, "",   "Groups: Expand all",          "",       ImGuiKey_None,          0,              ImGuiAppCmdSurface_Palette, ImGuiAppNodeKind_COUNT },
    { 22, "",   "Import: Paste C++ struct(s)", "",       ImGuiKey_None,          0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Menu | ImGuiAppCmdSurface_Gizmo, ImGuiAppNodeKind_Struct },
    { 23, "",   "Scope: Enter selection",      "Tab",    ImGuiKey_Tab,           0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut | ImGuiAppCmdSurface_Menu | ImGuiAppCmdSurface_Gizmo, ImGuiAppNodeKind_COUNT },
    { 24, "",   "Scope: Up one level",         "Esc",    ImGuiKey_Escape,        0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut, ImGuiAppNodeKind_COUNT },
    { 35, "",   "Scope: Whole app",            "",       ImGuiKey_None,          0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Menu | ImGuiAppCmdSurface_Gizmo, ImGuiAppNodeKind_COUNT },
    { 36, "",   "View: Quick inspector",       "N",      ImGuiKey_N,             0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut, ImGuiAppNodeKind_COUNT },
    { 37, "",   "View: Outliner sidebar",      "",       ImGuiKey_None,          0,              ImGuiAppCmdSurface_Palette, ImGuiAppNodeKind_COUNT },
    { 38, "",   "View: Inspector sidebar",     "",       ImGuiKey_None,          0,              ImGuiAppCmdSurface_Palette, ImGuiAppNodeKind_COUNT },
    { 34, "",   "Help: Shortcut card",         "F1",     ImGuiKey_F1,            0,              ImGuiAppCmdSurface_Palette | ImGuiAppCmdSurface_Shortcut, ImGuiAppNodeKind_COUNT },
  };

  int AppGraphEditorCommandCount()
  {
    return IM_ARRAYSIZE(s_editor_commands);
  }

  const ImGuiAppEditorCommand* AppGraphEditorCommandAt(int index)
  {
    return (index >= 0 && index < IM_ARRAYSIZE(s_editor_commands)) ? &s_editor_commands[index] : nullptr;
  }

  // Availability predicate: add verbs gate on the drilled scope's composability; undo/redo gate on history;
  // everything else is always available.
  bool AppGraphEditorCommandAvailable(const ImGuiAppGraph* g, const ImGuiAppEditorCommand* c)
  {
    IM_ASSERT(g != nullptr && c != nullptr);
    if (c->AddKind != ImGuiAppNodeKind_COUNT)
    {
      const int scope = AppScopeCurrent(g);
      if (c->AddKind == ImGuiAppNodeKind_Field)
        return scope >= 0 && AppScopeKindComposable(g, scope, c->AddKind);
      return scope < 0 || AppScopeKindComposable(g, scope, c->AddKind);
    }
    if (c->Id == 14) return AppGraphCanUndo(g);   // Undo
    if (c->Id == 15) return AppGraphCanRedo(g);   // Redo
    return true;
  }

  void ShowAppGraphEditor(ImGuiApp* app, ImGuiAppGraph* g, int* selected_node_id, bool show_live)
  {
    IM_ASSERT(g != nullptr);
    IM_UNUSED(app);
    ImGuiCanvasState* cv = AppEditorCanvas(g);

    // Snap-to-grid: toggled by the G key (latched in the shared view state, applied to the canvas style).
    // When on, the engine snaps node origins to the grid as they're dragged. View state lives behind
    // AppGraphViewState(g) so the host can persist it across sessions.
    bool& snap_grid = AppGraphViewState(g)->SnapGrid;
    ImGui::CanvasGetStyle(cv)->GridSnap = snap_grid;

    // Canvas overlay toggles, driven from the gizmo cluster's popover: each is presentation-only and
    // never touches the model. Same persistable view state as snap.
    bool& ov_grid = AppGraphViewState(g)->OvGrid;
    bool& ov_bands = AppGraphViewState(g)->OvBands;
    bool& ov_frames = AppGraphViewState(g)->OvFrames;
    bool& ov_minimap = AppGraphViewState(g)->OvMinimap;
    ImGui::CanvasGetStyle(cv)->GridLines = ov_grid;

    // Zoom persistence: the engine owns the live camera; the view state is the SAVED value. An external
    // write (sidecar load) pushes into the engine here; the engine value is mirrored back after CanvasEnd.
    {
      float& vz = AppGraphViewState(g)->Zoom;
      if (!(vz > 0.0f))
        vz = 1.0f;   // a zeroed sidecar must never produce a degenerate camera
      if (ImFabs(vz - ImGui::CanvasGetZoom(cv)) > 0.001f)
        ImGui::CanvasSetZoom(cv, vz, ImGui::GetCursorScreenPos());
    }

    // While hidden, live nodes are not submitted. On the hidden->shown transition, re-arm _NeedsPlace so
    // each one is re-seated at its saved GridPos. Single editor instance in the demo, so a function-local
    // latch is enough.
    if (show_live && !AppGraphEditorState(g)->PrevShowLive)
    {
      for (int i = 0; i < g->Nodes.Size; i++)
        if (g->Nodes.Data[i].IsLive)
          g->Nodes.Data[i]._NeedsPlace = true;
      // The live population just joined the canvas: re-arm the launch tidy so the default view
      // of the mirrored composition is the tidied layout too.
      AppGraphEditorState(g)->AutoLayoutCountdown = 2;
    }
    AppGraphEditorState(g)->PrevShowLive = show_live;

    // Launch default is a TIDIED layout: fires once real sizes exist (the previous frame's
    // submission measured them).
    if (AppGraphEditorState(g)->AutoLayoutCountdown > 0 && --AppGraphEditorState(g)->AutoLayoutCountdown == 0)
      AppGraphAutoLayout(g, show_live);

    // One normalized card width: grows to the widest measured need, deadbanded against
    // zoom-tick re-measures.
    {
      float& uw = AppGraphEditorState(g)->UniformCardW;
      for (int i = 0; i < g->Nodes.Size; i++)
      {
        const ImGuiAppNode* n = &g->Nodes.Data[i];
        if (n->Kind == ImGuiAppNodeKind_Layer)
          continue;
        const float need = ImGui::CanvasNodeNeededWidth(cv, n->Id);
        if (need > uw + 2.0f)
          uw = need;
      }
    }

    // Drill-down scope upkeep: repair dangling entries; on any scope change re-seat every now-visible node at its
    // stored GridPos (it left the canvas while out of scope) and arm a deferred fit-all (dims valid post-submit).
    AppScopeValidate(g);
    const bool at_root = g->ViewScope.Size == 0;
    // Altitude law: with a composed foundation, the root shows relationships and flow only --
    // struct and field nodes (and binding detail) exist below the breadcrumb. Graphs without a
    // Display layer (raw canvases, unit scaffolds) keep every node at every level.
    const bool altitude_root = at_root && AppGraphLayerOfType(g, ImGuiAppLayerType_Display) != nullptr;
    const int scope_sig = g->ViewScope.Size * 100000 + (AppScopeCurrent(g) + 1);
    if (scope_sig != g->_ScopeSig)
    {
      if (g->_ScopeSig != -1)   // skip the very first frame (initial layout owns the camera)
      {
        for (int i = 0; i < g->Nodes.Size; i++)
          if (!(!show_live && g->Nodes.Data[i].IsLive) && !AppNodeHiddenByCollapse(g, g->Nodes.Data[i].Id))
            g->Nodes.Data[i]._NeedsPlace = true;
        // Camera memory is PER BRANCH (keyed by the scope NODE, not the depth): leaving a scope
        // saves the view the user left it with; re-entering one -- forward or back -- restores
        // it. Only a never-visited scope falls back to the deferred fit.
        AppScopeCameraSave(g, cv, g->_ScopeCamId);
        g->_PendingFit = AppScopeCameraRestore(g, cv, AppScopeCurrent(g)) ? 0 : 1;
        // Selection does not survive scope transitions by design: *selected_node_id is the survivor and
        // re-applies onto the new scope (the sync block after CanvasEnd).
        ImGui::CanvasClearSelection(cv);
      }
      g->_ScopeSig = scope_sig;
      g->_ScopeCamId = AppScopeCurrent(g);
    }

    // Drive the layer vertical drag BEFORE submission so the clamped position lands in THIS frame's
    // placement (after EndNodeEditor it lags a frame). Hover state is from last frame -- fine. Root scope
    // only: inside a drill-down scope the layer column is not on the canvas.
    int dragged_layer_id = 0;
    ImVec2 dragged_layer_pos(0.0f, 0.0f);
    if (at_root)
      AppHandleLayerVerticalDrag(g, show_live, &dragged_layer_id, &dragged_layer_pos);

    // Node-body buttons (explode/collapse) mutate g->Nodes, which is unsafe mid-submission -- record the request
    // here and apply it once after EndNodeEditor. 0 = none.
    enum { AppAct_None = 0, AppAct_Explode, AppAct_Collapse };
    int pending_act = AppAct_None;
    int pending_node = -1;
    int pending_list = 0;

    // "Build onto a layer" requests from layer-node pills (e.g. DisplayLayer -> + Window). Deferred for the same
    // reason: adding a node mid-submission reallocs g->Nodes. pending_build_owner is the layer requesting it.
    ImGuiAppNodeKind pending_build_kind = ImGuiAppNodeKind_COUNT;   // COUNT = none
    int              pending_build_owner = -1;

    // Camera bindings (LMB-drag pan, RMB pan + short-click menu, cursor-anchored wheel zoom) are the
    // engine's IO defaults.

    // The canvas: the engine owns the camera, the zoomed font + layout metrics for node content, the
    // grid, wire editing (detach + snap-create), and the interaction FSM.
    ImGui::CanvasBegin(cv, "##app_canvas", ImVec2(0.0f, 0.0f));

    // In-canvas decorations size against em like node content, so they render under the zoomed font
    // (the engine only pushes it per node). The UNZOOMED metrics are the zoom-invariant truth for
    // model-space margins -- the zoomed font clamps at extreme zooms and must never define bounds.
    const float em_base = ImGui::GetFontSize();
    const float fh_base = ImGui::GetFrameHeight();
    ImGui::PushFont(nullptr, ImGui::GetFontSize() * AppCanvasZoom(g));

    // Pipeline box, drawn on the engine's background channel between the grid and the nodes: grid under
    // box, box under nodes. Model geometry with this frame's camera -- valid from the very first frame
    // (unmeasured nodes fall back to per-kind estimates).
    // The column's box + rows, published once for this frame: the pipeline box overlay AND the
    // trunk router's obstacle set (geometry computed even when the overlay is hidden).
    AppLayerColumnGeom col_geom;
    if (at_root)
      AppDrawLayerGroupBox(g, show_live, ov_bands, em_base, fh_base, &col_geom);

    // The layer column is solid ground: window-group nodes can never be dragged into it
    // (engine solid-drag clamp; consumed by the next frame's FSM in model units).
    g->_LayerBoxValid = col_geom.Valid;
    if (col_geom.Valid)
    {
      ImGui::CanvasAddSolidRect(cv, ImGui::CanvasFromScreen(cv, col_geom.BoxMin), ImGui::CanvasFromScreen(cv, col_geom.BoxMax));
      g->_LayerBoxMin = ImGui::CanvasFromScreen(cv, col_geom.BoxMin);   // model, for the deferred group-drag clamp (update pass)
      g->_LayerBoxMax = ImGui::CanvasFromScreen(cv, col_geom.BoxMax);
    }

    // Scope walls (docs/scope-interior-design.md rule A): drilled into a window/sidebar, the
    // owner's silhouette becomes the room. Background channel like the pipeline box; publishes
    // the wall/bracket rects (model units) that the post-CanvasEnd brackets/rail/portal passes
    // consume this same frame. Self-gates (clears ScopeWallValid at root / non-wall scopes).
    AppDrawScopeWalls(g, cv, show_live, em_base, fh_base);

    // Owners whose group frame swallowed their containment fan this frame: one trunk connector
    // per owner replaces the per-control wires (rebuilt every frame; consumed by the link loop).
    ImVector<int> trunked_owners;

    // Semantic group frames: a translucent labeled box around each containment group, same background
    // channel as the pipeline box.
    // Passes are depth-ordered: windows/sidebars behind, control data clusters, then structs in front.
    // Sole producer of _GroupFrames (model units); consumers read _GroupFramesPrev.
    g->_GroupFramesPrev.swap(g->_GroupFrames);
    g->_GroupFrames.resize(0);
    g->_GroupDragPending = -1;   // a settled group-drag this frame records its owner here; applied post-CanvasEnd
    if (ov_frames)
    {
      auto group_box = [&](int owner_id, ImU32 kind_col, int depth, bool include_owner)
      {
        ImVec2 mn(FLT_MAX, FLT_MAX);
        ImVec2 mx(-FLT_MAX, -FLT_MAX);
        if (include_owner)
        {
          AppGroupAccumulate(g, owner_id, show_live, &mn, &mx);
        }
        else
        {
          // Owner excluded: frame only the hosted-control cluster. A section-seated window/sidebar
          // lives inside the Display layer's boundary; framing it too would drag this box across
          // every row between the section and its controls.
          for (int i = 0; i < g->Nodes.Size; i++)
            if (g->Nodes.Data[i].Kind == ImGuiAppNodeKind_Control && AppGraphParentOf(g, g->Nodes.Data[i].Id) == owner_id)
              AppGroupAccumulate(g, g->Nodes.Data[i].Id, show_live, &mn, &mx);
        }
        if (mn.x > mx.x)
          return;
        const ImGuiAppNode* owner = AppGraphFindNodeConst(g, owner_id);
        const float em = ImGui::GetFontSize();
        const float sc = AppCanvasScale(g);
        // Chrome in MODEL units: the zoom-free em (this scope runs under the zoomed canvas
        // font, so FontSizeBase here carries the zoom and cannot be used directly).
        const float em_m = em_base * ImGui::CanvasGetZoom(cv) / sc;
        const float pad_m = em_m * (0.5f + 0.22f * (float)depth);   // outer groups get more breathing room
        const float title_h_m = fh_base * em_m / em_base;
        ImVec2 fr_mn_m(mn.x - pad_m, mn.y - (title_h_m + pad_m * 0.4f));
        ImVec2 fr_mx_m(mx.x + pad_m, mx.y + pad_m);
        mn = ImGui::CanvasToScreen(cv, fr_mn_m);   // drawing reads the published rect through THIS frame's camera (rule 5)
        mx = ImGui::CanvasToScreen(cv, fr_mx_m);
        const float pad = pad_m * sc;
        const float title_h = title_h_m * sc;
        ImDrawList* dl = ImGui::CanvasBackgroundDrawList(cv);
        const ImU32 fill = (kind_col & 0x00FFFFFF) | (IM_COL32(0, 0, 0, 18) & 0xFF000000);
        const ImU32 line = (kind_col & 0x00FFFFFF) | (IM_COL32(0, 0, 0, 130) & 0xFF000000);
        if (owner == nullptr)
        {
          dl->AddRectFilled(mn, mx, fill, em * 0.3125f);
          dl->AddRect(mn, mx, line, em * 0.3125f, 0, ImMax(1.0f, em * 0.09375f));
        }
        if (owner != nullptr)
        {
          const char* title = owner->Draft.Name[0] ? owner->Draft.Name : AppNodeKindName(owner->Kind);
          ImVector<int> members;
          AppGraphCollectSubtree(g, owner_id, &members);
          const int member_count = members.Size - 1;   // exclude the owner itself

          char label[160];
          if (owner->GroupCollapsed)
            ImFormatString(label, IM_ARRAYSIZE(label), "%s  +%d", title, member_count);
          else
            ImStrncpy(label, title, IM_ARRAYSIZE(label));

          const float tri_w = em * 0.9f;
          const ImVec2 ts = ImGui::CalcTextSize(label);
          // Title bar spans the full group width.
          ImVec2 bar_mn(mn.x + pad, mn.y);
          ImVec2 bar_mx(mx.x - pad, mn.y + title_h * 1.15f);

          // Click (no drag) folds/unfolds; drag moves the group. A section-seated owner's
          // position is owned by the window-section packer (one producer per value,
          // docs/phase-coherence.md rule 3): expanded, the drag moves only the cluster;
          // collapsed, the drag is inert.
          const bool owner_seated = at_root
              && (owner->Kind == ImGuiAppNodeKind_Window || owner->Kind == ImGuiAppNodeKind_Sidebar)
              && AppGraphLayerOfType(g, ImGuiAppLayerType_Display) != nullptr;
          ImGui::SetCursorScreenPos(bar_mn);
          ImGui::PushID(owner_id);
          ImGui::InvisibleButton("##grouphandle", bar_mx - bar_mn);
          const bool hov = ImGui::IsItemHovered();
          const bool act = ImGui::IsItemActive();
          if (ImGui::IsItemActivated())
          {
            g->_GroupDragMoved = false;
            g->_GroupDragMouse0 = ImGui::CanvasFromScreen(cv, ImGui::GetIO().MousePos);   // MODEL-space origin: pan/zoom mid-drag cannot corrupt the displacement
            g->_GroupDragFrame0 = ImVec4(fr_mn_m.x, fr_mn_m.y, fr_mx_m.x, fr_mx_m.y);     // own frame at drag start, model
            g->_GroupDragApplied = ImVec2(0.0f, 0.0f);
            g->_GroupDragOrig.resize(0);
            for (int m = 0; m < members.Size; m++)
            {
              if (owner_seated && members.Data[m] == owner_id)
                continue;
              const ImGuiAppNode* mm = AppGraphFindNodeConst(g, members.Data[m]);
              if (mm == nullptr || (!show_live && mm->IsLive))
                continue;
              // Drag origin at the altitude being dragged: engine position while seated, else the
              // scope-effective model position (GridPos alone is stale inside a drilled interior).
              const ImVec2 op = AppEditorNodeWasSubmitted(g, members.Data[m]) ? AppCanvasNodePos(g, members.Data[m]) : AppNodeScopePos(g, mm);
              g->_GroupDragOrig.push_back(ImVec4((float)members.Data[m], op.x, op.y, 0.0f));
            }
          }
          if (act && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !(owner_seated && owner->GroupCollapsed) && g->_GroupDragOrig.Size > 0)
          {
            // Record the drag intent ONLY; the clamp + model write run in the post-CanvasEnd update pass
            // (AppGraphApplyGroupDrag, below). Writing here gated the clamp on last frame's obstacle set
            // (_GroupFramesPrev) because THIS frame's group frames are still mid-publication at this point
            // in the pass -- the phase break that let a group clip past a neighbour it should slide against.
            // Deferred, the clamp reads this frame's complete _GroupFrames + layer box; the group + its
            // nodes move on the next frame (deliberate T+1, model units, docs/phase-coherence.md rule 4).
            g->_GroupDragPending = owner_id;
          }
          if (ImGui::IsItemDeactivated() && !g->_GroupDragMoved)
          {
            if (ImGuiAppNode* o = AppGraphFindNode(g, owner_id))
            {
              o->GroupCollapsed = !o->GroupCollapsed;
              if (!o->GroupCollapsed)                  // expanding: re-seat descendants at their stored positions
                for (int m = 0; m < members.Size; m++)
                  if (ImGuiAppNode* mm = AppGraphFindNode(g, members.Data[m]))
                    mm->_NeedsPlace = true;
            }
          }
          if (hov || act)
            ImGui::SetMouseCursor(member_count > 0 ? ImGuiMouseCursor_Hand : ImGuiMouseCursor_ResizeAll);
          ImGui::PopID();

          // Frame rect publication, model units, title band included.
          {
            ImGuiAppGroupFrame gf;
            gf.OwnerId = owner_id;
            gf.MinM = fr_mn_m;
            gf.MaxM = fr_mx_m;
            g->_GroupFrames.push_back(gf);
          }

          dl->AddRectFilled(mn, mx, fill, em * 0.3125f);
          dl->AddRect(mn, mx, line, em * 0.3125f, 0, ImMax(1.0f, em * 0.09375f));

          const ImU32 bar_bg = (hov || act) ? ((line & 0x00FFFFFF) | 0xFF000000) : line;
          dl->AddRectFilled(bar_mn, bar_mx, AppThemeNeutral(0.05f), em * 0.1875f);   // opaque plate under the tint
          dl->AddRectFilled(bar_mn, bar_mx, bar_bg, em * 0.1875f);
          // Disclosure triangle: right-pointing when folded, down-pointing when open.
          const ImU32 glyph = AppThemeNeutral(0.92f);
          const ImVec2 tc(bar_mn.x + tri_w * 0.5f, (bar_mn.y + bar_mx.y) * 0.5f);
          const float a = em * 0.22f;
          if (owner->GroupCollapsed)
            dl->AddTriangleFilled(ImVec2(tc.x - a * 0.55f, tc.y - a), ImVec2(tc.x - a * 0.55f, tc.y + a), ImVec2(tc.x + a * 0.8f, tc.y), glyph);
          else
            dl->AddTriangleFilled(ImVec2(tc.x - a, tc.y - a * 0.55f), ImVec2(tc.x + a, tc.y - a * 0.55f), ImVec2(tc.x, tc.y + a * 0.8f), glyph);
          dl->AddText(ImVec2(bar_mn.x + tri_w, mn.y + (title_h - ts.y) * 0.5f), glyph, label);

          // Trunk connector: an owner excluded from its own frame gets ONE wire, owner right edge
          // -> this title bar, standing in for the per-control containment fan.
          if (!include_owner && (owner->HasGridPos || AppEditorNodeWasSubmitted(g, owner_id)))
          {
            const float z = AppCanvasScale(g);
            const ImVec2 opos = ImGui::CanvasToScreen(cv, owner->GridPos);
            ImVec2 om;
            if (!AppNodeModelSize(g, owner_id, &om))
              om = AppLayoutNodeSize(g, owner);
            const ImVec2 start(opos.x + om.x * z, opos.y + om.y * z * 0.5f);
            const ImVec2 end(bar_mn.x, (bar_mn.y + bar_mx.y) * 0.5f);
            const ImU32 wire_col = AppPinColor(ImGuiAppPortKind_ChildIn);
            const float th = ImMax(1.0f, em * 0.14f);
            // Trunk route constraints:
            //   * flow runs title bar -> window pin; horizontal tangents at both endpoints
            //   * inside the layer group the wire is DEAD FLAT at pin level -- the crossing of
            //     the group's right edge is exactly horizontal, no bend inside home ground
            //   * past the right edge the wire is CUBIC BEZIERS ONLY -- loose free curves, no
            //     straight verticals, no tight corner arcs
            //   * the column stays uncrossed by convex hull: while a curve's y-span overlaps the
            //     column, every control point sits at or right of the hug line; travel to the
            //     wire's far side happens beyond the group's far boundary
            //   * entry is always horizontal, through the side the pin faces
            //   * constant stroke width: one path, deduped joints, one stroke
            // Inputs in MODEL units. The route is computed ONCE from these and cached on the
            // graph; the camera only transforms the cached primitives -- zoom can never
            // re-route a link (phase-coherence rule 1). A route is re-derived only when its
            // model inputs move (endpoints, column geometry, destination frame).
            const float zc = AppCanvasScale(g);
            // Margins are MODEL quantities derived from the UNZOOMED font: exact at every zoom,
            // no clamp drift, no invisible geometry (scale = zoom * font-ratio).
            const float em_m = em_base * ImGui::CanvasGetZoom(cv) / zc;
            const float m_hug = em_m * 0.6f;
            const ImVec2 start_m = ImGui::CanvasFromScreen(cv, start);
            const ImVec2 end_m = ImGui::CanvasFromScreen(cv, end);
            const float mn_left_m = ImGui::CanvasFromScreen(cv, mn).x;
            const float dym = end_m.y - start_m.y;
            const float sgn = dym > 0.0f ? 1.0f : -1.0f;

            float box_l = start_m.x;
            float box_r = start_m.x;
            float far_y = start_m.y;
            float x_h = start_m.x + em_m;
            if (col_geom.Valid)
            {
              box_l = ImGui::CanvasFromScreen(cv, col_geom.BoxMin).x;
              box_r = ImGui::CanvasFromScreen(cv, col_geom.BoxMax).x;
              // The VERTICAL hugs the layer NODES' edge ("hug the layer node corners"); only the
              // far wrap -- the 2nd corner -- keys off the group box's boundary.
              x_h = ImGui::CanvasFromScreen(cv, ImVec2(col_geom.SilRight, 0.0f)).x + m_hug;
              far_y = ImGui::CanvasFromScreen(cv, sgn > 0.0f ? col_geom.BoxMax : col_geom.BoxMin).y;
            }
            const float m_far = m_hug;
            const bool dest_past_far = col_geom.Valid && sgn * (end_m.y - far_y) > 0.0f;
            const bool dest_left = end_m.x < start_m.x;
            // A right-side destination close to the column pulls the hug line inward (never
            // inside the base margin): the wire must enter from the destination's left, and a
            // hug line past it would fold the route into a hairpin.
            if (!dest_left && col_geom.Valid && end_m.x - em_m < x_h)
              x_h = ImMax(ImGui::CanvasFromScreen(cv, ImVec2(col_geom.SilRight, 0.0f)).x + m_hug, end_m.x - em_m);

            // The wire's own row: its section bottom is the DOWNWARD exit (through the gap) and
            // a routing input in its own right.
            float sec_bot_m = start_m.y;
            bool own_row = false;
            float node_r_m = x_h;
            if (col_geom.Valid)
            {
              node_r_m = ImGui::CanvasFromScreen(cv, ImVec2(col_geom.NodeRight, 0.0f)).x;
              for (int ri = 0; ri < col_geom.RowCount; ri++)
              {
                const float rt_m = ImGui::CanvasFromScreen(cv, ImVec2(0.0f, col_geom.RowTop[ri])).y;
                const float rb_m = ImGui::CanvasFromScreen(cv, ImVec2(0.0f, col_geom.RowSecBot[ri])).y;
                if (start_m.y >= rt_m && start_m.y <= rb_m)
                {
                  sec_bot_m = rb_m;
                  own_row = true;
                  break;
                }
              }
            }

            // Cache lookup + staleness (model-unit epsilon; the key carries every routing input).
            ImGuiAppTrunkRoute* rt = nullptr;
            for (int ti = 0; ti < g->_TrunkRoutes.Size; ti++)
              if (g->_TrunkRoutes.Data[ti].OwnerId == owner_id)
              {
                rt = &g->_TrunkRoutes.Data[ti];
                break;
              }
            if (rt == nullptr)
            {
              g->_TrunkRoutes.push_back(ImGuiAppTrunkRoute());
              rt = &g->_TrunkRoutes.back();
              rt->OwnerId = owner_id;
              rt->KeyA = ImVec4(FLT_MAX, 0.0f, 0.0f, 0.0f);
            }
            const ImVec4 keyA(x_h, 0.0f, far_y, box_l);
            const ImVec4 keyB(mn_left_m, sgn, sec_bot_m, em_m);
            const float eps = 0.5f;
            const bool stale = ImAbs(rt->StartM.x - start_m.x) > eps || ImAbs(rt->StartM.y - start_m.y) > eps
                            || ImAbs(rt->EndM.x - end_m.x) > eps || ImAbs(rt->EndM.y - end_m.y) > eps
                            || ImAbs(rt->KeyA.x - keyA.x) > eps || ImAbs(rt->KeyA.y - keyA.y) > eps
                            || ImAbs(rt->KeyA.z - keyA.z) > eps || ImAbs(rt->KeyA.w - keyA.w) > eps
                            || ImAbs(rt->KeyB.x - keyB.x) > eps || ImAbs(rt->KeyB.y - keyB.y) > eps
                            || ImAbs(rt->KeyB.z - keyB.z) > eps || ImAbs(rt->KeyB.w - keyB.w) > eps
                            || rt->Segs.Size == 0;
            if (stale)
            {
              rt->StartM = start_m;
              rt->EndM = end_m;
              rt->KeyA = keyA;
              rt->KeyB = keyB;
              rt->Segs.resize(0);
              auto seg_line = [&](ImVec2 pnt)
              {
                ImGuiAppTrunkSeg sg; sg.Kind = 0; sg.P0 = pnt; rt->Segs.push_back(sg);
              };
              auto seg_arc = [&](ImVec2 c, float r, float a0, float a1)
              {
                ImGuiAppTrunkSeg sg; sg.Kind = 1; sg.P0 = c; sg.R = r; sg.A0 = a0; sg.A1 = a1; rt->Segs.push_back(sg);
              };
              auto seg_cubic = [&](ImVec2 c1, ImVec2 c2, ImVec2 pnt)
              {
                ImGuiAppTrunkSeg sg; sg.Kind = 2; sg.P0 = c1; sg.P1 = c2; sg.P2 = pnt; rt->Segs.push_back(sg);
              };
              bool routed = false;
              // EVERY trunk leaves the section flat at pin level; the free-curve section starts
              // only at the hug line, past the group's right edge. There is no free-sweep
              // shortcut from the pin itself -- a sweep from an in-section pin inevitably
              // pierces the section's side wall mid-descent, the bug this router exists to
              // kill. The lead is emitted only when one of the endings below can actually
              // complete the route -- an orphaned lead would force the fallback to restart
              // from the pin, folding the wire into a hairpin.
              const bool can_beside = !dest_left && end_m.x >= x_h + em_m * 0.25f;
              if (x_h > start_m.x + em_m * 0.5f && (can_beside || dest_past_far || dest_left))
              {
                // The lead is a PIN-LEVEL exit and nothing more: DEAD FLAT to the hug line --
                // the crossing of the group's right edge is exactly horizontal, at pin level.
                // Everything after the exit point is cubics.
                seg_line(start_m);
                seg_line(ImVec2(x_h, start_m.y));
                if (can_beside)
                {
                  // Destination clear of the hug line on the right: one S-curve, horizontal tangents
                  // at both ends. The inbound control point is clamped to the hug line, so the
                  // hull -- and with it the curve -- never crosses back over the column.
                  const float t_s = ImMax(em_m, ImMin(ImMin(end_m.x - x_h, em_m * 4.0f), ImAbs(end_m.y - start_m.y) * 0.8f + em_m));
                  seg_cubic(ImVec2(x_h + t_s, start_m.y), ImVec2(ImMax(end_m.x - t_s, x_h), end_m.y), end_m);
                  routed = true;
                }
                else if (dest_past_far || dest_left)
                {
                  // Destination on the column's far side or its left: one curve down the OUTSIDE of
                  // the hug line -- bulge right, descend, land on the far-boundary line at the
                  // far corner heading left. Hull x stays in [x_h, x_h + b]: the column is
                  // never re-entered while crossing its y-span.
                  const float y_b = far_y + sgn * m_far;
                  const float b = ImMax(em_m, ImMin(em_m * 2.5f, ImAbs(y_b - start_m.y) * 0.5f));
                  seg_cubic(ImVec2(x_h + b, start_m.y), ImVec2(x_h + b, y_b), ImVec2(x_h, y_b));
                  if (dest_past_far)
                  {
                    // Past the far boundary: one curve out of the corner leftward, swinging
                    // beyond the far-boundary line, entering the pin horizontally through the
                    // side it faces.
                    const float t_in = ImMax(em_m, ImMin(em_m * 3.0f, ImAbs(end_m.y - y_b) * 0.8f + em_m));
                    seg_cubic(ImVec2(x_h - b, y_b), ImVec2(end_m.x - t_in, end_m.y), end_m);
                  }
                  else
                  {
                    // Level with the group on its LEFT: along the far boundary and around the
                    // far-left corner (the joint tangent is the 45-degree diagonal, so both
                    // curves meet it smoothly with their hulls outside the box), then up the
                    // left edge and horizontally in.
                    const float x_l = box_l - m_hug;
                    const float d = 0.7071f;
                    const float w = ImMax(em_m, ImMin(em_m * 2.0f, (x_h - x_l) * 0.25f));
                    seg_cubic(ImVec2(x_h - b, y_b), ImVec2(x_l + w * d, y_b + sgn * w * d), ImVec2(x_l, y_b));
                    const float h = ImMax(em_m, ImMin(em_m * 3.0f, ImAbs(y_b - end_m.y) * 0.5f));
                    seg_cubic(ImVec2(x_l - h * d, y_b - sgn * h * d), ImVec2(end_m.x - h, end_m.y), end_m);
                  }
                  routed = true;
                }
              }
              // Last resort stays LEGAL: flat exit, down the hug line to destination level, and
              // in -- from the left when there is room, from the right when the destination
              // overlaps the hug line. NEVER a diagonal through the column.
              if (!routed)
              {
                const float r_f = ImMax(0.5f, ImMin(em_m * 1.5f, ImAbs(end_m.y - start_m.y) * 0.4f));
                seg_line(start_m);
                if (x_h > start_m.x + 1.0f)
                {
                  seg_line(ImVec2(x_h - r_f, start_m.y));
                  seg_arc(ImVec2(x_h - r_f, start_m.y + sgn * r_f), r_f, sgn > 0.0f ? -IM_PI * 0.5f : IM_PI * 0.5f, 0.0f);
                }
                if (end_m.x > x_h + 0.5f)
                {
                  const float r_i = ImMax(0.5f, ImMin(end_m.x - x_h, em_m * 2.0f));
                  seg_line(ImVec2(x_h, end_m.y - sgn * r_i));
                  seg_arc(ImVec2(x_h + r_i, end_m.y - sgn * r_i), r_i, IM_PI, IM_PI - sgn * IM_PI * 0.5f);
                }
                else
                {
                  const float r_i = em_m;
                  seg_line(ImVec2(x_h, end_m.y - sgn * r_i));
                  seg_arc(ImVec2(x_h - r_i, end_m.y - sgn * r_i), r_i, 0.0f, sgn * IM_PI * 0.5f);
                }
                seg_line(end_m);
              }
            }

            // Render the cached model route with THIS frame's camera.
            dl->PathClear();
            if (rt->Segs.Size > 0 && rt->Segs.Data[0].Kind != 0)
              dl->PathLineTo(start);
            for (int si = 0; si < rt->Segs.Size; si++)
            {
              const ImGuiAppTrunkSeg* sg = &rt->Segs.Data[si];
              if (sg->Kind == 0)
                dl->PathLineTo(ImGui::CanvasToScreen(cv, sg->P0));
              else if (sg->Kind == 1)
                dl->PathArcTo(ImGui::CanvasToScreen(cv, sg->P0), sg->R * zc, sg->A0, sg->A1, 0);
              else
                dl->PathBezierCubicCurveTo(ImGui::CanvasToScreen(cv, sg->P0), ImGui::CanvasToScreen(cv, sg->P1), ImGui::CanvasToScreen(cv, sg->P2), 0);
            }
            AppWirePathStroke(dl, wire_col, th);
            // Square endpoints: the containment-pin idiom, so the trunk reads as a child wire.
            const float ps = em * 0.28f;
            dl->AddRectFilled(ImVec2(start.x - ps, start.y - ps), ImVec2(start.x + ps, start.y + ps), wire_col, ps * 0.4f);
            dl->AddRectFilled(ImVec2(end.x - ps, end.y - ps), ImVec2(end.x + ps, end.y + ps), wire_col, ps * 0.4f);
            trunked_owners.push_back(owner_id);
          }
        }
      };

      // Pass 1: windows/sidebars hosting controls (outermost). At root these owners are seated in
      // the Display layer's section, so their frames cover only the control cluster (the collapsed
      // state keeps the owner: the title bar must survive as the re-expand handle).
      for (int i = 0; i < g->Nodes.Size; i++)
      {
        const ImGuiAppNode* n = &g->Nodes.Data[i];
        if ((n->Kind == ImGuiAppNodeKind_Window || n->Kind == ImGuiAppNodeKind_Sidebar) && !(!show_live && n->IsLive) && AppGraphHostsControl(g, n->Id))
          group_box(n->Id, AppKindColor(n->Kind), 2, !at_root || n->GroupCollapsed);
      }
      // Pass 2: control data clusters (a control with an exploded Persist/Temp struct). Their
      // struct members exist below the breadcrumb, so these frames draw only there.
      if (!altitude_root)
        for (int i = 0; i < g->Nodes.Size; i++)
        {
          const ImGuiAppNode* n = &g->Nodes.Data[i];
          if (n->Kind == ImGuiAppNodeKind_Control && (n->PersistStructId >= 0 || n->TempStructId >= 0))
            group_box(n->Id, AppKindColor(ImGuiAppNodeKind_Control), 1, true);
        }
      // Pass 3: structs with exploded fields (innermost).
      if (!altitude_root)
        for (int i = 0; i < g->Nodes.Size; i++)
        {
          const ImGuiAppNode* n = &g->Nodes.Data[i];
          if (n->Kind == ImGuiAppNodeKind_Struct && !(!show_live && n->IsLive) && AppGraphFieldNodeCount(g, n->Id, 0) > 0)
            group_box(n->Id, AppKindColor(ImGuiAppNodeKind_Struct), 0, true);
        }
    }
    ImGui::PopFont();   // decoration font (node content gets its own from the engine)

    // Drilled scope: members carry their execution ordinal in the title bar (badge below).
    ImVector<int> scope_seq;
    if (!at_root)
      AppScopeSequenceIds(g, &scope_seq);

    // Last frame's submitted ids, kept for the re-entry check below; the current list is rebuilt from
    // scratch every frame: exactly the ids this submission puts on the canvas.
    AppGraphEditorState(g)->PrevPoolIds.swap(AppGraphEditorState(g)->PoolIds);
    AppGraphEditorState(g)->PoolIds.resize(0);
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      ImGuiAppNode* n = &g->Nodes.Data[i];

      // Hidden live mirror: skip the ENTIRE per-node submission (incl. placement). Merely skipping the body
      // would leave the grid-pos read-back below adopting a stale position for a node that never submitted.
      if (!show_live && n->IsLive)
        continue;
      // Folded behind a collapsed ancestor group: same deal -- never submit, so the read-back skips it too.
      if (AppNodeHiddenByCollapse(g, n->Id))
        continue;
      // Below-the-breadcrumb kinds never submit at the composition root (altitude law).
      if (altitude_root && (n->Kind == ImGuiAppNodeKind_Struct || n->Kind == ImGuiAppNodeKind_Field))
        continue;
      AppGraphEditorState(g)->PoolIds.push_back(n->Id);

      // Rejoining the canvas after a frame of eviction (unhidden via the outliner eye / show-all / H, a group
      // expand, or any other path with no explicit re-arm): the engine kept the last-known geometry, but the
      // stored GridPos is authoritative for a node the MODEL moved while it was off-canvas -- re-seat it.
      if (n->HasGridPos && !n->_NeedsPlace && !AppIdInSet(AppGraphEditorState(g)->PrevPoolIds, n->Id))
        n->_NeedsPlace = true;

      // Layer nodes (column packer) and window/sidebar nodes (section packer) are position-owned:
      // not draggable.
      const bool at_root_scope = g->ViewScope.Size == 0;
      const bool owned = n->Kind == ImGuiAppNodeKind_Layer
                      || ((n->Kind == ImGuiAppNodeKind_Window || n->Kind == ImGuiAppNodeKind_Sidebar) && at_root_scope);
      ImGui::CanvasSetNodeDraggable(cv, n->Id, !owned);
      // Control/window/sidebar nodes are mutually solid: a drag slides to contact, never overlaps.
      ImGui::CanvasSetNodeSolid(cv, n->Id, n->Kind == ImGuiAppNodeKind_Control
                                        || n->Kind == ImGuiAppNodeKind_Window
                                        || n->Kind == ImGuiAppNodeKind_Sidebar);

      if (n->_NeedsPlace)
      {
        AppCanvasSetNodePos(g, n->Id, AppNodeScopePos(g, n));   // drilled scopes seat from their own placements
        n->_NeedsPlace = false;
      }

      const ImU32 title_col = AppNodeTitleColor(n);
      ImGui::CanvasNextNodeTitleTag(cv, AppNodeKindTag(n->Kind));
      ImGui::CanvasNextNodeOriginDot(cv, AppGraphOriginColor(n), n->IsPromoted && !n->IsLive);

      // Execution ordinal in the title bar (drilled scopes): the layer nodes' n/N badge idiom at
      // member altitude -- order is part of the card, never an overlay.
      for (int si = 0; si < scope_seq.Size; si++)
        if (scope_seq.Data[si] == n->Id)
        {
          char ord[16];
          ImFormatString(ord, IM_ARRAYSIZE(ord), "%d/%d", si + 1, scope_seq.Size);
          ImGui::CanvasNextNodeTitleBadge(cv, ord);
          break;
        }

      // Kind silhouette (design page: rounded acts, squared hosts, rail is a phase, pill is an
      // atom). Model-unit constants; the engine scales. Every non-layer plate takes the ONE
      // normalized width.
      if (n->Kind != ImGuiAppNodeKind_Layer && AppGraphEditorState(g)->UniformCardW > 0.0f)
        ImGui::CanvasNextNodeWidth(cv, AppGraphEditorState(g)->UniformCardW);
      switch (n->Kind)
      {
      case ImGuiAppNodeKind_Control:
        ImGui::CanvasNextNodeRounding(cv, 7.0f);
        break;
      case ImGuiAppNodeKind_Window:
        ImGui::CanvasNextNodeRounding(cv, 2.0f);
        ImGui::CanvasNextNodeHeaderRule(cv, 1, AppKindColor(ImGuiAppNodeKind_Window));
        break;
      case ImGuiAppNodeKind_Sidebar:
      {
        ImGui::CanvasNextNodeRounding(cv, 2.0f);
        ImGui::CanvasNextNodeHeaderRule(cv, 1, AppKindColor(ImGuiAppNodeKind_Sidebar));
        int stripe_side = ImGui::ImGuiCanvasPinSide_Top;
        if (n->DockDir == ImGuiDir_Left)  stripe_side = ImGui::ImGuiCanvasPinSide_Left;
        if (n->DockDir == ImGuiDir_Right) stripe_side = ImGui::ImGuiCanvasPinSide_Right;
        if (n->DockDir == ImGuiDir_Down)  stripe_side = ImGui::ImGuiCanvasPinSide_Bottom;
        ImGui::CanvasNextNodeEdgeStripe(cv, stripe_side, AppKindColor(ImGuiAppNodeKind_Sidebar), 3.0f);
        break;
      }
      case ImGuiAppNodeKind_Layer:
      {
        ImGui::CanvasNextNodeRounding(cv, 0.0f);
        ImGui::CanvasNextNodeEdgeStripe(cv, ImGui::ImGuiCanvasPinSide_Left, AppLayerAccent(n->LayerType), 4.0f);
        int order = 0;
        int total = 0;
        for (int li = 0; li < g->Nodes.Size; li++)
          if (g->Nodes.Data[li].Kind == ImGuiAppNodeKind_Layer)
          {
            total++;
            if (g->Nodes.Data[li].Id == n->Id)
              order = total;
          }
        char badge[16];
        ImFormatString(badge, IM_ARRAYSIZE(badge), "%d/%d", order, total);
        ImGui::CanvasNextNodeTitleBadge(cv, badge);
        break;
      }
      case ImGuiAppNodeKind_Struct:
        ImGui::CanvasNextNodeRounding(cv, 0.0f);
        ImGui::CanvasNextNodeHeaderRule(cv, 2, AppKindColor(ImGuiAppNodeKind_Struct));
        break;
      case ImGuiAppNodeKind_Field:
        ImGui::CanvasNextNodeRounding(cv, 999.0f);
        break;
      default:
        break;
      }

      // Live nodes mirror the running app and are read-only: static (non-renamable) title. Core layers are the
      // frame's phases -- fixed titles; a CUSTOM layer's name IS its generated class name, so it renames.
      // Rename entry is the double-click event after CanvasEnd (host decides rename vs drill); the engine
      // renders the in-title field while the shared edit latch points at this node and hands it back on
      // deactivation.
      const bool was_editing = g->EditingNodeId == n->Id;
      if (n->IsLive)
        ImGui::CanvasNextNodeTitle(cv, n->Draft.Name[0] ? n->Draft.Name : "(live)", title_col);
      else if (n->Kind == ImGuiAppNodeKind_Layer && AppLayerIsCore(n->LayerType))
        ImGui::CanvasNextNodeTitle(cv, AppLayerNodeName(n->LayerType), title_col);
      else
      {
        AppGraphEditorState(g)->TitleEditing = was_editing;
        ImGui::CanvasNextNodeTitleEditable(cv, n->Draft.Name, IM_ARRAYSIZE(n->Draft.Name), &AppGraphEditorState(g)->TitleEditing, title_col);
      }
      // Eye-hidden nodes and members of a closed live window stay on the canvas with the
      // disabled look -- never removed.
      if (AppNodeCanvasOff(g, n->Id))
        ImGui::CanvasNextNodeAlpha(cv, 0.35f);
      ImGui::CanvasBeginNode(cv, n->Id);

      // Containment reads vertically, owner over child: the child's "parent" pin (ChildOut) sits on its TOP
      // edge and RECEIVES from above; the owner's "children" pin (ChildIn) sits on its BOTTOM edge and EMITS
      // downward. Row-less edge pins (at-most-one per edge) -- submit them before the body; order is free.
      for (int p = 0; p < n->Ports.Size; p++)
      {
        ImGuiAppNodePort* port = &n->Ports.Data[p];
        if (port->Kind == ImGuiAppPortKind_ChildOut)
        {
          ImGui::CanvasNextPinColor(cv, AppPinColor(port->Kind));
          ImGui::CanvasEdgePin(cv, port->Id, ImGui::ImGuiCanvasPin_In, ImGui::ImGuiCanvasPinShape_Square, ImGui::ImGuiCanvasPinSide_Top);
        }
        else if (port->Kind == ImGuiAppPortKind_ChildIn)
        {
          ImGui::CanvasNextPinColor(cv, AppPinColor(port->Kind));
          ImGui::CanvasEdgePin(cv, port->Id, ImGui::ImGuiCanvasPin_Out, ImGui::ImGuiCanvasPinShape_Square, ImGui::ImGuiCanvasPinSide_Bottom);
        }
      }

      // Input-side data ports (left). For a control, the "persist"/"temp" tie pins are NOT drawn here -- they
      // are drawn at their own body rows below (so an exploded PersistData/TempData wire enters lower than "deps").
      for (int p = 0; p < n->Ports.Size; p++)
      {
        ImGuiAppNodePort* port = &n->Ports.Data[p];
        const bool is_tie_pin = n->Kind == ImGuiAppNodeKind_Control && (strcmp(port->Name, "persist") == 0 || strcmp(port->Name, "temp") == 0);
        if (is_tie_pin)
          continue;
        if (port->Kind != ImGuiAppPortKind_DataIn)
          continue;
        // Data pins circular -- the engine draws pins. The "deps" identity stays stable for saved
        // graphs and port lookups; the ROW reads the wired producers' names (the relationship
        // itself), falling back to "dependencies" only as the unwired drop-target affordance.
        ImGui::CanvasNextPinColor(cv, AppPinColor(port->Kind));
        ImGui::CanvasBeginPin(cv, port->Id, ImGui::ImGuiCanvasPin_In, ImGui::ImGuiCanvasPinShape_Circle);
        if (strcmp(port->Name, "deps") == 0)
        {
          int named = 0;
          for (int li = 0; li < g->Links.Size; li++)
          {
            if (g->Links.Data[li].Kind != ImGuiAppEdgeKind_Data || g->Links.Data[li].EndAttr != port->Id)
              continue;
            const int pid = AppGraphPortOwnerId(g, g->Links.Data[li].StartAttr);
            const ImGuiAppNode* pn = pid >= 0 ? AppGraphFindNodeConst(g, pid) : nullptr;
            if (pn == nullptr)
              continue;
            ImGui::TextUnformatted(pn->Draft.Name[0] ? pn->Draft.Name : "(unnamed)");
            named++;
          }
          if (named == 0)
            ImGui::TextDisabled("dependencies");
        }
        else
        {
          ImGui::TextUnformatted(port->Name);
        }
        ImGui::CanvasEndPin(cv);
      }

      // Detail altitude (docs/scope-interior-design.md rule D): the full authoring body renders
      // only in the node's scope-parent's scope; everywhere else the card folds to identity
      // (title, pins, summary line). Deps pins stay at both altitudes -- wires must land.
      const bool detail = AppScopeDetailAltitude(g, n);

      // A control's exploded PersistData/TempData tie pins, each on its own row (so the wire enters at that line).
      if (n->Kind == ImGuiAppNodeKind_Control && !n->IsBuiltin && !n->IsLive && detail)
      {
        for (int list = 0; list < 2; list++)
        {
          const bool temp = list == 1;
          const int sid = AppControlStructId(g, n, temp);
          if (sid < 0)
            continue;
          const int pin = AppNodePortByName(n, temp ? "temp" : "persist");
          ImGui::CanvasNextPinColor(cv, AppPinTieColor());
          ImGui::CanvasBeginPin(cv, pin, ImGui::ImGuiCanvasPin_In, ImGui::ImGuiCanvasPinShape_Circle);
          ImGui::PushID(100 + list);
          if (AppBlDisclosure("##tie", true))
          {
            pending_node = n->Id;
            pending_act = AppAct_Collapse;
            pending_list = list;
          }
          ImGui::SameLine();
          const ImGuiAppNode* sn = AppGraphFindNodeConst(g, sid);
          ImGui::TextDisabled("%s -> %s", temp ? "TempData" : "PersistData", sn != nullptr ? sn->Draft.Name : "(struct)");
          ImGui::PopID();
          ImGui::CanvasEndPin(cv);
        }
      }

      // Body: plain widgets between CanvasBeginNode/EndNode (no attribute scaffolding needed).
      // Origin (live/promoted) is the title dot's job; the body never restates it.
      ImGui::PushID(n->Id);
      if (n->Kind == ImGuiAppNodeKind_Control)
      {
        if (n->IsBuiltin)
        {
          // The title already names the type; only show the data type when it adds something (differs from the
          // title), with a minimal fallback so the body always says something.
          if (n->DataTypeName[0] && strcmp(n->DataTypeName, n->Draft.Name) != 0)
            ImGui::TextDisabled("data: %s", n->DataTypeName);
          else if (!n->IsLive && !n->IsPromoted)
            ImGui::TextDisabled("builtin");
        }
        else if (detail)
        {
          // A control HAS two structs: PersistData (0) and TempData (1). A non-exploded one is edited inline with a
          // disclosure triangle to explode it out (the exploded ones render above as tie-pin rows).
          for (int list = 0; list < 2; list++)
          {
            const bool temp = list == 1;
            if (AppControlStructId(g, n, temp) >= 0)
              continue;   // exploded -> rendered as a tie-pin row above
            ImGui::PushID(list);
            if (AppBlDisclosure("##disc", false))
            {
              pending_node = n->Id;
              pending_act = AppAct_Explode;
              pending_list = list;
            }
            ImGui::SameLine();
            EditAppFieldList(temp ? "TempData" : "PersistData", AppNodeFieldList(n, list), g);
            ImGui::PopID();
          }
          if (!n->IsLive)
          {
            EditAppControlCommandChoices(g, n);
            EditAppControlEvents(g, n);   // "when <temp> <edge> -> <action>": the ^ idiom, authored in place
          }
        }
        else
        {
          // Identity card: one dim summary line folds the authoring detail this altitude hides.
          char sum[96];
          AppNodeSummaryLine(g, n, sum, IM_ARRAYSIZE(sum));
          if (sum[0])
            ImGui::TextDisabled("%s", sum);
        }

        // Every incoming data edge (struct/control/field producer) gets its own binding editor,
        // labelled by the producer. Binding detail lives BELOW the breadcrumb at detail altitude:
        // the composition root shows relationships only (the pin row already names each producer).
        if (detail && !altitude_root)
          for (int li = 0; li < g->Links.Size; li++)
          {
            if (g->Links.Data[li].Kind != ImGuiAppEdgeKind_Data) continue;
            if (AppGraphPortOwnerId(g, g->Links.Data[li].EndAttr) != n->Id) continue;
            const int pid = AppGraphPortOwnerId(g, g->Links.Data[li].StartAttr);
            if (pid == n->PersistStructId || pid == n->TempStructId) continue;   // own persist/temp tie -> no binding
            const ImGuiAppNode* pn = pid >= 0 ? AppGraphFindNodeConst(g, pid) : nullptr;
            if (pn != nullptr)
              ImGui::TextDisabled("from %s", pn->Draft.Name[0] ? pn->Draft.Name : "(unnamed)");
            EditAppDataEdgeBindings(g, g->Links.Data[li].Id);
          }
      }
      else if (n->Kind == ImGuiAppNodeKind_Layer)
      {
        const ImGuiAppLayerType lt = n->LayerType;
        const float em = ImGui::GetFontSize();

        // Identity row: accent icon + role, plus the layer's place in the execution pipeline (top -> bottom).
        int order = 1;
        int total = 0;
        for (int li = 0; li < g->Nodes.Size; li++)
        {
          const ImGuiAppNode* o = &g->Nodes.Data[li];
          if (o->Kind != ImGuiAppNodeKind_Layer || (!show_live && o->IsLive))
            continue;
          total++;
          if (o->Id != n->Id && o->GridPos.y < n->GridPos.y)
            order++;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, AppLayerAccent(lt));
        ImGui::Text(ICON_FA_LAYER_GROUP " %d/%d", order, total);   // pipeline order badge, accent-colored
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, em * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, AppLayerAccent(lt));
        ImGui::TextUnformatted(AppLayerIcon(lt));
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", AppLayerRole(lt));

        // Build-onto affordances: each foundation layer offers what you can build on top of it (the builder flow).
        // Shown even on a live foundation layer -- the pills AUTHOR new design nodes, they don't edit the layer.
        if (lt == ImGuiAppLayerType_Display)
        {
          if (AppBlAddPill("##bw", "Window"))  { pending_build_kind = ImGuiAppNodeKind_Window;  pending_build_owner = n->Id; }
          ImGui::SameLine();
          if (AppBlAddPill("##bs", "Sidebar")) { pending_build_kind = ImGuiAppNodeKind_Sidebar; pending_build_owner = n->Id; }
        }
        else if (lt == ImGuiAppLayerType_Command)
        {
          if (AppBlAddPill("##bc", "Command"))   // author a command on this layer (the command-dispatch foundation)
            AppNodeAddCommandUnique(n, "NewCommand");
        }
        else if (lt == ImGuiAppLayerType_Task)
        {
          ImGui::TextDisabled(ICON_FA_SLIDERS "  status ingest & control updates");
        }
        else if (lt == ImGuiAppLayerType_Status)
        {
          ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  publishes the app's status");
        }
        else if (lt == ImGuiAppLayerType_Layout)
        {
          ImGui::TextDisabled(ICON_FA_BORDER_ALL "  OnLayout(): dockspaces before windows");
          if (show_live)
          {
            // The running context's active root dockspaces: id + docked window count.
            ImGuiContext* ictx = ImGui::GetCurrentContext();
            int live_dockspaces = 0;
            for (int di = 0; di < ictx->DockContext.Nodes.Data.Size; di++)
            {
              ImGuiDockNode* dock = (ImGuiDockNode*)ictx->DockContext.Nodes.Data[di].val_p;
              if (dock == nullptr || !dock->IsDockSpace() || !dock->IsRootNode())
                continue;
              if (dock->LastFrameAlive < ImGui::GetFrameCount() - 1)
                continue;
              int docked = 0;
              ImGuiDockNode* walk[64];
              int depth = 0;
              walk[depth++] = dock;
              while (depth > 0)
              {
                ImGuiDockNode* nd = walk[--depth];
                docked += nd->Windows.Size;
                if (nd->ChildNodes[0] != nullptr && depth < 63)
                  walk[depth++] = nd->ChildNodes[0];
                if (nd->ChildNodes[1] != nullptr && depth < 63)
                  walk[depth++] = nd->ChildNodes[1];
              }
              ImGui::Bullet();
              ImGui::SameLine();
              ImGui::TextDisabled("dockspace %08X -- %d docked window%s", dock->ID, docked, docked == 1 ? "" : "s");
              live_dockspaces++;
            }
            if (live_dockspaces == 0)
              ImGui::TextDisabled("(no dockspaces this frame)");
          }
        }

        // Core phases have no type picker; a custom layer states the subclass it will generate.
        if (!n->IsLive && lt == ImGuiAppLayerType_Custom)
        {
          char base[IM_LABEL_SIZE];
          AppNodeBaseName(n, base, IM_ARRAYSIZE(base));
          ImGui::TextDisabled("struct %s : ImGuiAppLayer", base);
        }
        if (lt == ImGuiAppLayerType_Command)
          EditAppNodeCommands(n, false);   // list the commands (the "+ Command" adder lives in the build row above)
        // Uniform layer-column width: every layer stretches to the SAME shared width (widest content wins,
        // model units x zoom) -- flush column at any zoom, never per-node ragged, never fixed pixels.
        ImGui::Dummy(ImVec2(AppLayerUniformW(g) * AppCanvasScale(g), 1.0f));
      }
      else if (n->Kind == ImGuiAppNodeKind_Window || n->Kind == ImGuiAppNodeKind_Sidebar)
      {
        // No body rows at the composition root; props show only when drilled in.
        if (AppScopeCurrent(g) >= 0)
        {
          if (n->IsBuiltin && n->TypeName[0] && strcmp(n->TypeName, n->Draft.Name) != 0)
            ImGui::TextDisabled("type: %s", n->TypeName);
          if (n->IsLive)
            DrawAppWindowNodeProps(n);
          else
            EditAppWindowNodeProps(n);
        }
      }
      else if (n->Kind == ImGuiAppNodeKind_Struct)
      {
        // A pure data struct: its field list IS the struct body (list 0). The disclosure triangle expands
        // it into Field nodes / collapses them back (only shown when there's something to toggle).
        const int exploded = AppGraphFieldNodeCount(g, n->Id, 0);
        if (exploded > 0 || n->Draft.PersistFields.Size > 0)
        {
          if (AppBlDisclosure("##disc", exploded > 0))
          {
            pending_node = n->Id;
            pending_act = exploded > 0 ? AppAct_Collapse : AppAct_Explode;
            pending_list = 0;
          }
          ImGui::SameLine();
        }
        if (exploded > 0)
          ImGui::TextDisabled("fields (%d, expanded)", exploded);
        else
          EditAppFieldList("fields", &n->Draft.PersistFields, g);
      }
      else if (n->Kind == ImGuiAppNodeKind_Field)
      {
        // One field exploded out of a struct: the title IS the field name; edit its type here (a struct-typed
        // field picks another Struct -> recursive nesting). Wire its "value" out.
        if (n->Draft.PersistFields.Size == 0)
          n->Draft.PersistFields.push_back(ImGuiAppFieldDesc());   // keep body non-empty + give the editor a field
        EditAppFieldTypeControls(&n->Draft.PersistFields.Data[0], ImGui::GetFontSize() * 5.0f, g);
      }
      else
      {
        if (!n->IsLive)
          ImGui::TextDisabled("%s", AppNodeKindName(n->Kind));
      }
      ImGui::PopID();

      // Output-side data ports (right). The "children" containment pin is NOT here -- it renders on the
      // bottom edge as an edge pin above (vertical containment read).
      for (int p = 0; p < n->Ports.Size; p++)
      {
        ImGuiAppNodePort* port = &n->Ports.Data[p];
        if (port->Kind != ImGuiAppPortKind_DataOut)
          continue;
        ImGui::CanvasNextPinColor(cv, AppPinColor(port->Kind));
        ImGui::CanvasBeginPin(cv, port->Id, ImGui::ImGuiCanvasPin_Out, ImGui::ImGuiCanvasPinShape_Circle);
        ImGui::TextUnformatted(port->Name);
        ImGui::CanvasEndPin(cv);
      }

      ImGui::CanvasEndNode(cv);

      // The engine cleared the edit latch on deactivation (Enter / Escape / click-away): hand it back.
      if (was_editing && !AppGraphEditorState(g)->TitleEditing)
        g->EditingNodeId = -1;
    }

    // Draw links. When live nodes are hidden, skip any link incident on a hidden (live) owner: that attribute
    // was never submitted this frame, so the wire has no live anchor. (Inlined: DrawAppNodeLinks can't resolve
    // owners.)
    for (int li = 0; li < g->Links.Size; li++)
    {
      ImGuiAppNode* oa = nullptr; ImGuiAppNode* ob = nullptr;
      AppGraphFindPort(g, g->Links.Data[li].StartAttr, &oa);
      AppGraphFindPort(g, g->Links.Data[li].EndAttr, &ob);
      if (!show_live && ((oa && oa->IsLive) || (ob && ob->IsLive)))
        continue;
      // Skip links into a node folded behind a collapsed group: its attribute was not submitted this frame.
      if ((oa && AppNodeHiddenByCollapse(g, oa->Id)) || (ob && AppNodeHiddenByCollapse(g, ob->Id)))
        continue;
      // A trunked owner's containment fan is replaced by its group-frame trunk connector.
      if (g->Links.Data[li].Kind == ImGuiAppEdgeKind_Containment && trunked_owners.Size > 0)
      {
        const int fan_owner = AppGraphPortOwnerId(g, g->Links.Data[li].EndAttr);   // EndAttr = the owner's ChildIn
        bool trunked = false;
        for (int ti = 0; ti < trunked_owners.Size && !trunked; ti++)
          trunked = trunked_owners.Data[ti] == fan_owner;
        if (trunked)
          continue;
      }
      // Brushing echo on wires: the link another view (inspector binding rows) points at renders bright.
      ImGuiAppHoverSource lsrc = ImGuiAppHoverSource_None;
      const bool brushed = g->Links.Data[li].Id == AppGraphHoveredLink(g, &lsrc) && lsrc != ImGuiAppHoverSource_Canvas && lsrc != ImGuiAppHoverSource_None;
      ImU32 wire_col = 0;
      if (brushed)
        wire_col = AppThemeNeutral(0.98f, 0.86f);
      else if (g->Links.Data[li].Soft)
        wire_col = AppSoftWireColor(cv);
      if (g->Links.Data[li].Soft)
        ImGui::CanvasNextWireDashed(cv);
      ImGui::CanvasWire(cv, g->Links.Data[li].Id, g->Links.Data[li].StartAttr, g->Links.Data[li].EndAttr, wire_col);
    }

    // Overview minimap (engine inset, bottom-right; click/drag to jump). Lets the user see and reach
    // nodes past the visible canvas edge.
    if (ov_minimap)
      ImGui::CanvasMiniMap(cv, 0.2f);
    ImGui::CanvasEnd(cv);
    const ImVec2 editor_size = ImGui::GetItemRectSize();   // captured before later items, for fit-all centering
    const ImVec2 editor_min  = ImGui::GetItemRectMin();    // editor canvas top-left (screen), for overlay extents
    AppGraphEditorState(g)->EditorRectMin = editor_min;    // F38: publish the canvas rect for gesture detection + tests
    AppGraphEditorState(g)->EditorRectMax = editor_min + editor_size;

    // Deferred group-drag application (docs/phase-coherence.md: mutate the model in the update pass, never
    // mid-render). A group drag detected during the canvas pass recorded its owner in _GroupDragPending;
    // the slide-to-contact clamp + the member position writes run HERE, where THIS frame's group frames
    // (_GroupFrames) and layer box (_LayerBox) are fully published. The old inline write gated the clamp on
    // last frame's obstacle set (_GroupFramesPrev, the only complete set available mid-render) -- so a group
    // clipped past a neighbour it should slide against and could not reach contact with the layer column.
    // Applied here, the group + its nodes move on the next frame (deliberate T+1 in model units, rule 4).
    if (g->_GroupDragPending >= 0 && g->_GroupDragOrig.Size > 0)
    {
      const int drag_owner = g->_GroupDragPending;
      ImVec2 disp = ImGui::CanvasFromScreen(cv, ImGui::GetIO().MousePos) - g->_GroupDragMouse0 - g->_GroupDragApplied;
      const float mv_x0 = g->_GroupDragFrame0.x + g->_GroupDragApplied.x;
      const float mv_y0 = g->_GroupDragFrame0.y + g->_GroupDragApplied.y;
      const float mv_x1 = g->_GroupDragFrame0.z + g->_GroupDragApplied.x;
      const float mv_y1 = g->_GroupDragFrame0.w + g->_GroupDragApplied.y;
      const float kEps = 1.0f;   // T+1 measurement-variance deadband; penetration within it resolves to contact
      auto overlap = [](float a0, float a1, float b0, float b1) { return a0 < b1 && a1 > b0; };
      ImVector<ImVec4> ob;
      ob.reserve(g->_GroupFrames.Size + 1);
      if (g->_LayerBoxValid)
      {
        const ImVec2 bmn = g->_LayerBoxMin;
        const ImVec2 bmx = g->_LayerBoxMax;
        if (!(overlap(mv_x0, mv_x1, bmn.x + kEps, bmx.x - kEps) && overlap(mv_y0, mv_y1, bmn.y + kEps, bmx.y - kEps)))
          ob.push_back(ImVec4(bmn.x, bmn.y, bmx.x, bmx.y));
      }
      for (int i2 = 0; i2 < g->_GroupFrames.Size; i2++)
      {
        const ImGuiAppGroupFrame& gf = g->_GroupFrames.Data[i2];
        if (gf.OwnerId == drag_owner)
          continue;
        if (overlap(mv_x0, mv_x1, gf.MinM.x + kEps, gf.MaxM.x - kEps) && overlap(mv_y0, mv_y1, gf.MinM.y + kEps, gf.MaxM.y - kEps))
          continue;
        ob.push_back(ImVec4(gf.MinM.x, gf.MinM.y, gf.MaxM.x, gf.MaxM.y));
      }
      if (mv_x0 <= mv_x1)
      {
        auto slide_x = [&](float dx, float y0, float y1) -> float
        {
          for (int oi = 0; oi < ob.Size; oi++)
            if (overlap(y0, y1, ob.Data[oi].y, ob.Data[oi].w))
            {
              if (dx > 0.0f && mv_x1 <= ob.Data[oi].x + kEps && mv_x1 + dx > ob.Data[oi].x)
                dx = ob.Data[oi].x - mv_x1;
              if (dx < 0.0f && mv_x0 >= ob.Data[oi].z - kEps && mv_x0 + dx < ob.Data[oi].z)
                dx = ob.Data[oi].z - mv_x0;
            }
          return dx;
        };
        auto slide_y = [&](float dy, float x0, float x1) -> float
        {
          for (int oi = 0; oi < ob.Size; oi++)
            if (overlap(x0, x1, ob.Data[oi].x, ob.Data[oi].z))
            {
              if (dy > 0.0f && mv_y1 <= ob.Data[oi].y + kEps && mv_y1 + dy > ob.Data[oi].y)
                dy = ob.Data[oi].y - mv_y1;
              if (dy < 0.0f && mv_y0 >= ob.Data[oi].w - kEps && mv_y0 + dy < ob.Data[oi].w)
                dy = ob.Data[oi].w - mv_y0;
            }
          return dy;
        };
        ImVec2 a;
        a.x = slide_x(disp.x, mv_y0, mv_y1);
        a.y = slide_y(disp.y, mv_x0 + a.x, mv_x1 + a.x);
        ImVec2 b;
        b.y = slide_y(disp.y, mv_x0, mv_x1);
        b.x = slide_x(disp.x, mv_y0 + b.y, mv_y1 + b.y);
        const float ea = (a.x - disp.x) * (a.x - disp.x) + (a.y - disp.y) * (a.y - disp.y);
        const float eb = (b.x - disp.x) * (b.x - disp.x) + (b.y - disp.y) * (b.y - disp.y);
        disp = ea <= eb ? a : b;
      }
      g->_GroupDragApplied += disp;
      if (g->_GroupDragApplied.x != 0.0f || g->_GroupDragApplied.y != 0.0f)
        g->_GroupDragMoved = true;
      for (int m = 0; m < g->_GroupDragOrig.Size; m++)
      {
        const int mid = (int)g->_GroupDragOrig.Data[m].x;
        ImGuiAppNode* mm = AppGraphFindNode(g, mid);
        if (mm == nullptr)
          continue;
        const ImVec2 np(g->_GroupDragOrig.Data[m].y + g->_GroupDragApplied.x, g->_GroupDragOrig.Data[m].z + g->_GroupDragApplied.y);
        if (AppNodeHiddenByCollapse(g, mid) || !AppEditorNodeWasSubmitted(g, mid))
        {
          if (at_root) { mm->GridPos = np; mm->HasGridPos = true; }
          else         { AppNodeScopePosStore(g, mid, np); }
          mm->_NeedsPlace = true;   // re-seat it at this pos when it next submits
        }
        else
        {
          AppCanvasSetNodePos(g, mid, np);   // the canvas write; the read-back routes it to GridPos / placements
          if (at_root)
            mm->GridPos = np;
        }
      }
    }

    // Live-mirror diff dots: a small status dot at each node's top-right corner showing how it relates to the
    // running app -- blue = a live mirror node, green = a design node already running (promoted), amber = a
    // design node that is NOT in the running app yet (drift). Only meaningful once the app is initialized.
    {
      const bool app_running = app != nullptr && app->Layers.Size > 0;   // composed; Initialized is platform-only
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float r = ImGui::GetFontSize() * 0.28f;
      for (int i = 0; i < g->Nodes.Size; i++)
      {
        const ImGuiAppNode* n = &g->Nodes.Data[i];
        if ((!show_live && n->IsLive) || AppNodeHiddenByCollapse(g, n->Id) || !AppEditorNodeWasSubmitted(g, n->Id))
          continue;

        ImU32 dot = 0;
        const char* tip = nullptr;
        if (n->IsLive)
        {
          dot = AppComposerGetStyle()->DotLive;
          tip = "running (live mirror)";
        }
        else if (n->IsPromoted)
        {
          dot = AppComposerGetStyle()->DotPromoted;
          tip = "matches a running control";
        }
        else if (app_running && (n->Kind == ImGuiAppNodeKind_Control))
        {
          dot = AppComposerGetStyle()->DotDrift;
          tip = "design-only -- not in the running app yet";
        }
        if (dot == 0)
          continue;

        const ImVec2 p = ImGui::CanvasToScreen(cv, AppCanvasNodePos(g, n->Id));
        const ImVec2 d = ImGui::CanvasNodeSize(cv, n->Id) * AppCanvasScale(g);
        const ImVec2 c(p.x + d.x - r * 1.6f, p.y + r * 1.6f);
        dl->AddCircleFilled(c, r, dot);
        dl->AddCircle(c, r, AppColWithAlpha(AppComposerGetStyle()->DarkOutline, 0.63f));
        if (tip != nullptr)
        {
          const ImVec2 m = ImGui::GetIO().MousePos;
          const float dx = m.x - c.x;
          const float dy = m.y - c.y;
          const float reach = r + ImGui::GetFontSize() * 0.125f;
          if (dx * dx + dy * dy <= reach * reach)
            ImGui::SetTooltip("%s", tip);
        }
      }
    }

    // Port hover tooltip: name + (for data pins) the carried data type.
    {
      const int hov_pin = ImGui::CanvasHoveredPin(cv);
      if (hov_pin >= 0)
      {
        ImGuiAppNode* owner = nullptr;
        ImGuiAppNodePort* port = AppGraphFindPort(g, hov_pin, &owner);
        if (port != nullptr && owner != nullptr)
        {
          if (port->Kind == ImGuiAppPortKind_DataOut)
          {
            char dt[IM_LABEL_SIZE];
            AppNodeDataTypeName(owner, dt, IM_ARRAYSIZE(dt));
            ImGui::SetTooltip("%s : %s", port->Name, dt);
          }
          else
          {
            ImGui::SetTooltip("%s", port->Name);
          }
        }
      }
    }

    // Persist canvas layout for save/load. Skip hidden live nodes: they were not submitted, so a read-back
    // would assert; skipping also correctly retains each hidden node's last-shown GridPos.
    // Altitude split: the root read-back owns GridPos; a drilled read-back owns that scope's
    // placement records -- interior drags never leak into the root layout (or vice versa).
    int moved_layer_id = 0;
    ImVec2 moved_layer_pos(0.0f, 0.0f);
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      if (!show_live && g->Nodes.Data[i].IsLive) continue;
      if (AppNodeHiddenByCollapse(g, g->Nodes.Data[i].Id)) continue;
      ImGuiAppNode* n = &g->Nodes.Data[i];
      const ImVec2 pos = AppCanvasNodePos(g, n->Id);   // model units, like GridPos (drags land here)
      if (!at_root)
      {
        AppNodeScopePosStore(g, n->Id, pos);
        continue;
      }
      if (n->Kind == ImGuiAppNodeKind_Layer && n->HasGridPos
          && (ImAbs(pos.x - n->GridPos.x) > 0.5f || ImAbs(pos.y - n->GridPos.y) > 0.5f))
      {
        moved_layer_id = n->Id;
        moved_layer_pos = pos;
      }
      n->GridPos = pos;
      // Don't finalize layer nodes here: the tight-packer (AppGraphConstrainLayerColumn, below) sets
      // HasGridPos once their real heights are known.
      if (n->Kind != ImGuiAppNodeKind_Layer)
        n->HasGridPos = true;
    }
    if (dragged_layer_id != 0)
    {
      moved_layer_id = dragged_layer_id;
      moved_layer_pos = dragged_layer_pos;
    }
    if (at_root)
    {
      // Update stack, dependency order, consuming this frame's read-back: width -> pack -> seat -> stick.
      {
        const float pad2 = ImGui::CanvasGetStyle(cv)->NodePadding.x * 2.0f;   // model units, like the sizes
        float w = kAppGraphLayerNodeWidth;
        for (int i = 0; i < g->Nodes.Size; i++)
        {
          const ImGuiAppNode* n = &g->Nodes.Data[i];
          if (n->Kind != ImGuiAppNodeKind_Layer || (!show_live && n->IsLive))
            continue;
          ImVec2 m;
          if (AppNodeModelSize(g, n->Id, &m))
            w = ImMax(w, m.x - pad2);
        }
        if (w > AppLayerUniformW(g) + 2.0f)   // deadband: docs/phase-coherence.md 1b
          g->_LayerUniformW = w;
      }
      AppGraphConstrainLayerColumn(g, show_live, moved_layer_id, moved_layer_id != 0 ? &moved_layer_pos : nullptr);
      AppGraphSeatWindowSection(g, show_live);   // stack contained windows/sidebars beneath the packed Display layer row
      AppGraphDragStickClusters(g, show_live, moved_layer_id);   // edge drags push occluded clusters, stuck for the drag's life
    }
    else
    {
      // Drilled in: number the members in the order the framework runs them each frame, dock
      // portal chips for boundary-crossing data edges, and invite the first build step when the
      // scope is empty. The breadcrumb (below) names where we are and what executes here.
      AppDrawScopeOrderStrip(g, editor_min, editor_size, selected_node_id);
      AppDrawScopePortals(g, editor_min, editor_size, selected_node_id);
      AppDrawScopeEmptyCTA(g, show_live, editor_min, editor_size);
    }
    AppGraphViewState(g)->Zoom = ImGui::CanvasGetZoom(cv);   // mirror the live camera into the saved view state
    AppDrawScopeBreadcrumb(g, selected_node_id, editor_min);

    // In-widget node palette: right-click the canvas to add any node kind. Hover and menu requests come
    // latched from CanvasEnd (the engine's RMB short-click already disambiguated pan).
    const int  hovered_node = ImGui::CanvasHoveredNode(cv);
    const int  hovered_link = ImGui::CanvasHoveredWire(cv);
    const int  hovered_pin  = ImGui::CanvasHoveredPin(cv);
    const bool over_node = hovered_node >= 0;
    const bool over_link = hovered_link >= 0;
    const bool over_pin  = hovered_pin >= 0;
    // Brushing: report the canvas's hovered datum so the other views can echo it next frame.
    if (over_node)
      AppGraphHoverNode(g, hovered_node, ImGuiAppHoverSource_Canvas);
    if (over_link)
      AppGraphHoverLink(g, hovered_link, ImGuiAppHoverSource_Canvas);
    // Double-click: reported by the engine, interpreted here. Drill into a node's composition where the
    // title is not a rename target (layers, live mirrors); everything else renames in place -- the engine
    // shows the in-title field while g->EditingNodeId points at the node.
    {
      int dbl_id = -1;
      if (ImGui::CanvasNodeDoubleClicked(cv, &dbl_id))
      {
        const ImGuiAppNode* dn = AppGraphFindNodeConst(g, dbl_id);
        if (dn != nullptr && AppScopeCanEnter(dn) && (dn->Kind == ImGuiAppNodeKind_Layer || dn->IsLive))
          g->ViewScope.push_back(dn->Id);
        else if (dn != nullptr && !dn->IsLive && (dn->Kind != ImGuiAppNodeKind_Layer || dn->LayerType == ImGuiAppLayerType_Custom))
          g->EditingNodeId = dn->Id;
      }
    }

    // Host "Add" entry point (the toolbar's compose verb): open the same add palette the RMB / Space / +
    // gizmo roads reach, at the canvas center.
    if (AppGraphEditorState(g)->AddPaletteRequest)
    {
      AppGraphEditorState(g)->AddPaletteRequest = false;
      AppGraphEditorState(g)->AddPopupGrid = ImGui::CanvasFromScreen(cv, editor_min + editor_size * 0.5f);
      ImGui::OpenPopup("##AppGraphAdd");
    }
    if (AppGraphEditorState(g)->CmdPaletteRequest)   // same operator palette the Space key opens (F34: test/host road)
    {
      AppGraphEditorState(g)->CmdPaletteRequest = false;
      ImGui::OpenPopup("##cmdpalette");
    }

    // Context menus: the engine's RMB short-click events (a travelled RMB is a pan and never menus).
    {
      int    menu_id = -1;
      ImVec2 menu_model(0.0f, 0.0f);
      if (ImGui::CanvasMenuRequestNode(cv, &menu_id))
      {
        AppGraphEditorState(g)->CtxNodeId = menu_id;                      // right-click a node -> per-node context menu (Duplicate/Delete)
        ImGui::OpenPopup("##AppGraphNodeCtx");
      }
      else if (ImGui::CanvasMenuRequestWire(cv, &menu_id))
      {
        AppGraphEditorState(g)->CtxLinkId = menu_id;                      // right-click a link -> link context menu (Delete)
        ImGui::OpenPopup("##AppGraphLinkCtx");
      }
      else if (ImGui::CanvasMenuRequestEmpty(cv, &menu_model))
      {
        AppGraphEditorState(g)->AddPopupGrid = menu_model;                // right-click empty canvas -> add-node palette (model units)
        ImGui::OpenPopup("##AppGraphAdd");
      }
    }

    // Delete: the Delete key removes selected nodes + links. Live mirror nodes are read-only and survive. Skipped
    // while a text field is active (renaming a node) so the key still edits text. Valid post-EndNodeEditor.
    if (!ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
    {
      const int node_count = ImGui::CanvasNumSelectedNodes(cv);
      if (node_count > 0)
      {
        ImVector<int> picks;
        picks.resize(node_count);
        ImGui::CanvasGetSelectedNodes(cv, picks.Data, node_count);
        for (int i = 0; i < node_count; i++)
        {
          const ImGuiAppNode* n = AppGraphFindNode(g, picks.Data[i]);
          if (n != nullptr && !n->IsLive)
            AppGraphRemoveNode(g, picks.Data[i]);
          else if (n != nullptr && n->IsLive)
            AppNotifyLiveReadOnly(g, n);   // Delete on a live pick: refuse with the notice, not silence
        }
        ImGui::CanvasClearSelection(cv);
      }
      const int sel_wire = ImGui::CanvasSelectedWire(cv);
      if (sel_wire >= 0)
      {
        AppGraphEraseLink(g, sel_wire);
        ImGui::CanvasClearWireSelection(cv);
      }
    }

    // Zoom-to-fit a model-space bounding box, capped at 1:1 -- framing must never blow small content up
    // past its natural size (fit means "bring into view", not "magnify").
    auto fit_rect = [&](ImVec2 mn, ImVec2 mx)
    {
      ImGui::CanvasFitRect(cv, mn, mx, 60.0f);
      if (ImGui::CanvasGetZoom(cv) > 1.0f)
      {
        ImGui::CanvasSetZoom(cv, 1.0f, editor_min + editor_size * 0.5f);
        ImGui::CanvasCenterOn(cv, (mn + mx) * 0.5f);
      }
    };

    // Fit all visible nodes (model-space bounds; measured sizes with per-kind estimates before that).
    auto fit_all = [&]()
    {
      ImVec2 mn(FLT_MAX, FLT_MAX);
      ImVec2 mx(-FLT_MAX, -FLT_MAX);
      bool any = false;
      for (int i = 0; i < g->Nodes.Size; i++)
      {
        const ImGuiAppNode* n = &g->Nodes.Data[i];
        if (!show_live && n->IsLive)
          continue;
        if (AppNodeHiddenByCollapse(g, n->Id))
          continue;
        // Effective position: a drilled interior renders from its placements, not GridPos.
        const ImVec2 p = AppEditorNodeWasSubmitted(g, n->Id) ? AppCanvasNodePos(g, n->Id) : AppNodeScopePos(g, n);
        ImVec2 d;
        if (!AppNodeModelSize(g, n->Id, &d))
          d = AppLayoutNodeSize(g, n);
        mn.x = ImMin(mn.x, p.x); mn.y = ImMin(mn.y, p.y);
        mx.x = ImMax(mx.x, p.x + d.x); mx.y = ImMax(mx.y, p.y + d.y);
        any = true;
      }
      if (any)
        fit_rect(mn, mx);
      return any;
    };

    // Fit a specific set of node ids.
    auto fit_ids = [&](const ImVector<int>& ids)
    {
      ImVec2 mn(FLT_MAX, FLT_MAX);
      ImVec2 mx(-FLT_MAX, -FLT_MAX);
      bool any = false;
      for (int i = 0; i < ids.Size; i++)
      {
        const ImGuiAppNode* sn = AppGraphFindNode(g, ids.Data[i]);
        if (sn == nullptr || (!show_live && sn->IsLive) || AppNodeHiddenByCollapse(g, ids.Data[i]))
          continue;
        // Effective position: a drilled interior renders from its placements, not GridPos.
        const ImVec2 p = AppEditorNodeWasSubmitted(g, sn->Id) ? AppCanvasNodePos(g, sn->Id) : AppNodeScopePos(g, sn);
        ImVec2 d;
        if (!AppNodeModelSize(g, sn->Id, &d))
          d = AppLayoutNodeSize(g, sn);
        mn.x = ImMin(mn.x, p.x); mn.y = ImMin(mn.y, p.y);
        mx.x = ImMax(mx.x, p.x + d.x); mx.y = ImMax(mx.y, p.y + d.y);
        any = true;
      }
      if (any)
        fit_rect(mn, mx);
      return any;
    };

    // Deferred scope fit: frame the freshly revealed composition once its nodes have been submitted (scope
    // changes from Tab/breadcrumb/tree happen post-submission, so the fit lands on the following frame).
    if (g->_PendingFit > 0 && --g->_PendingFit == 0)
      fit_all();

    // Navigation: F frames the selection's bounding box (all selected nodes); Home fits all nodes.
    // Host fit request (seed/load): defer one frame so fresh nodes have measured -- centering on estimate
    // rects would frame the wrong extents.
    {
      if (AppGraphEditorState(g)->FitAllRequest)
      {
        AppGraphEditorState(g)->FitAllRequest = false;
        AppGraphEditorState(g)->FitAllCountdown = 2;
      }
      if (AppGraphEditorState(g)->FitAllCountdown > 0 && --AppGraphEditorState(g)->FitAllCountdown == 0)
        fit_all();
    }

    if (!ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
    {
      if (ImGui::IsKeyPressed(ImGuiKey_F, false))
      {
        if (!fit_ids(g->Selection) && ImGui::CanvasNumSelectedNodes(cv) > 0)
        {
          int picked = 0;
          ImGui::CanvasGetSelectedNodes(cv, &picked, 1);
          ImGui::CanvasCenterOn(cv, AppCanvasNodePos(g, picked) + ImGui::CanvasNodeSize(cv, picked) * 0.5f);
        }
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
        fit_all();
      if (ImGui::IsKeyPressed(ImGuiKey_L, false) && !ImGui::GetIO().KeyCtrl)
      {
        AppGraphAutoLayout(g, show_live);
        fit_all();
      }
      if (ImGui::IsKeyPressed(ImGuiKey_G, false))
        snap_grid = !snap_grid;   // toggle snap-to-grid
      if (ImGui::IsKeyPressed(ImGuiKey_F1, false))
        AppGraphEditorState(g)->HelpOverlay = !AppGraphEditorState(g)->HelpOverlay;             // toggle shortcut cheat-sheet
      if (ImGui::IsKeyPressed(ImGuiKey_N, false))
        AppGraphEditorState(g)->QuickInspector = !AppGraphEditorState(g)->QuickInspector; // Blender N-panel: floating inspector beside the selection
      if (ImGui::IsKeyPressed(ImGuiKey_Space, false) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P))
        ImGui::OpenPopup("##cmdpalette");   // Blender Space / VS Code Ctrl+P: same operator search

      // Host verbs may claim a keyboard road (Shortcut is display text; Key/Mods is the live chord). A match
      // records the pick exactly as a palette click would -- the editor never acts on the host's behalf.
      for (int i = 0; i < AppGraphEditorState(g)->HostCmdCount; i++)
      {
        const ImGuiAppGraphHostCmd* hc = &AppGraphEditorState(g)->HostCmds[i];
        if (hc->Key != ImGuiKey_None && ImGui::IsKeyChordPressed((ImGuiKeyChord)(hc->Key | hc->Mods)))
          AppGraphEditorState(g)->HostCmdPicked = hc->Id;
      }

      // F2 renames the primary SELECTION inline (the same editor the title double-click opens). Acts on
      // selection, never hover -- hover is for brushing. Live mirrors and core layers keep their names.
      if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && selected_node_id != nullptr && *selected_node_id >= 0)
      {
        const ImGuiAppNode* sel = AppGraphFindNodeConst(g, *selected_node_id);
        if (sel != nullptr && !sel->IsLive && (sel->Kind != ImGuiAppNodeKind_Layer || sel->LayerType == ImGuiAppLayerType_Custom))
          g->EditingNodeId = sel->Id;
        else if (sel != nullptr && sel->IsLive)
          AppNotifyLiveReadOnly(g, sel);   // F2 on a live mirror: refuse with the notice
      }

      // Drill-down (Blender node-group semantics): Tab enters the selected node's composition scope; Tab with
      // nothing enterable -- or Esc -- goes up one level, reselecting the scope just left. The reveal/fit runs
      // next frame once the newly scoped nodes have been submitted.
      if (ImGui::IsKeyPressed(ImGuiKey_Tab, false))
      {
        ImGuiAppNode* sel = (selected_node_id != nullptr && *selected_node_id >= 0) ? AppGraphFindNode(g, *selected_node_id) : nullptr;
        if (sel != nullptr && AppScopeCanEnter(sel) && !AppNodeHiddenByCollapse(g, sel->Id) && !(!show_live && sel->IsLive))
          g->ViewScope.push_back(sel->Id);
        else if (g->ViewScope.Size > 0)
        {
          const int exited = g->ViewScope.back();
          g->ViewScope.pop_back();
          if (selected_node_id != nullptr)
            *selected_node_id = exited;
        }
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && g->ViewScope.Size > 0)
      {
        const int exited = g->ViewScope.back();
        g->ViewScope.pop_back();
        if (selected_node_id != nullptr)
          *selected_node_id = exited;
      }

      // Blender selects: A selects everything visible; Alt+A (or A with a selection) clears.
      if (ImGui::IsKeyPressed(ImGuiKey_A, false) && !ImGui::GetIO().KeyCtrl)
      {
        if (ImGui::GetIO().KeyAlt || ImGui::CanvasNumSelectedNodes(cv) > 0)
        {
          ImGui::CanvasClearSelection(cv);
          g->Selection.clear();
        }
        else
        {
          for (int i = 0; i < g->Nodes.Size; i++)
          {
            const ImGuiAppNode* n = &g->Nodes.Data[i];
            if ((!show_live && n->IsLive) || AppNodeHiddenByCollapse(g, n->Id))
              continue;
            ImGui::CanvasSelectNode(cv, n->Id, true);
          }
        }
      }

      // Blender hide: H hides the selected design nodes (outliner eye); Alt+H shows everything again.
      if (ImGui::IsKeyPressed(ImGuiKey_H, false))
      {
        if (ImGui::GetIO().KeyAlt)
        {
          for (int i = 0; i < g->Nodes.Size; i++)
            g->Nodes.Data[i].Hidden = false;
        }
        else if (g->Selection.Size > 0)
        {
          for (int i = 0; i < g->Selection.Size; i++)
          {
            ImGuiAppNode* sn = AppGraphFindNode(g, g->Selection.Data[i]);
            if (sn != nullptr && !sn->IsLive && sn->Kind != ImGuiAppNodeKind_Layer)
              sn->Hidden = true;
          }
          ImGui::CanvasClearSelection(cv);
          g->Selection.clear();
        }
      }

      // Nudge: arrow keys move the selected nodes by 1 grid unit (Shift = 10). Held = repeat.
      {
        const float step = ImGui::GetIO().KeyShift ? 10.0f : 1.0f;
        ImVec2 d(0.0f, 0.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  d.x -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) d.x += step;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))    d.y -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))  d.y += step;
        if ((d.x != 0.0f || d.y != 0.0f) && g->Selection.Size > 0)
        {
          for (int i = 0; i < g->Selection.Size; i++)
          {
            ImGuiAppNode* sn = AppGraphFindNode(g, g->Selection.Data[i]);
            if (sn == nullptr || (!show_live && sn->IsLive) || AppNodeHiddenByCollapse(g, sn->Id))
              continue;
            // Nudge in MODEL units (a grid unit is a grid unit at any zoom). The canvas write is
            // enough while drilled: the interior read-back routes it to this scope's placements;
            // GridPos belongs to the root read-back alone.
            const ImVec2 np = AppCanvasNodePos(g, sn->Id) + d;
            AppCanvasSetNodePos(g, sn->Id, np);
            if (at_root)
              sn->GridPos = np;
          }
        }
      }

      // Z-order: '[' sends the selection to the back, ']' brings it to the front. The canvas draws in submission =
      // g->Nodes order, so we restack by rebuilding the vector with the selected nodes moved to one end. Element
      // moves are byte-copies (ImVector never frees element-owned memory), so inner buffers transfer cleanly.
      if ((ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false) || ImGui::IsKeyPressed(ImGuiKey_RightBracket, false)) && g->Selection.Size > 0)
      {
        const bool to_front = ImGui::IsKeyPressed(ImGuiKey_RightBracket, false);
        ImVector<ImGuiAppNode> reordered;
        reordered.reserve(g->Nodes.Size);
        if (to_front)
        {
          for (int i = 0; i < g->Nodes.Size; i++) if (!AppIdInSet(g->Selection, g->Nodes.Data[i].Id)) reordered.push_back(g->Nodes.Data[i]);
          for (int i = 0; i < g->Nodes.Size; i++) if (AppIdInSet(g->Selection, g->Nodes.Data[i].Id))  reordered.push_back(g->Nodes.Data[i]);
        }
        else
        {
          for (int i = 0; i < g->Nodes.Size; i++) if (AppIdInSet(g->Selection, g->Nodes.Data[i].Id))  reordered.push_back(g->Nodes.Data[i]);
          for (int i = 0; i < g->Nodes.Size; i++) if (!AppIdInSet(g->Selection, g->Nodes.Data[i].Id)) reordered.push_back(g->Nodes.Data[i]);
        }
        g->Nodes.Size = 0;            // abandon the old elements (their inner buffers now live in `reordered`)
        g->Nodes.swap(reordered);     // take the reordered buffer; `reordered` frees only the old outer array
      }
    }

    // Apply a deferred node-body button request (explode/collapse). Here -- after the canvas read-back -- it is
    // safe to add/remove nodes (the read-back already ran, so it never touches the un-submitted new nodes).
    if (pending_act != AppAct_None && pending_node >= 0)
    {
      if (ImGuiAppNode* pn = AppGraphFindNode(g, pending_node))
      {
        const bool is_control = pn->Kind == ImGuiAppNodeKind_Control;
        switch (pending_act)
        {
        case AppAct_Explode:
          if (is_control) AppGraphExplodeControlData(g, pn, pending_list == 1);
          else            AppGraphExplodeFields(g, pn, pending_list);
          break;
        case AppAct_Collapse:
          if (is_control) AppGraphCollapseControlData(g, pn, pending_list == 1);
          else            AppGraphCollapseFields(g, pn, pending_list);
          break;
        default: break;
        }
      }
    }

    // Apply a deferred "build onto layer" request: add the window/sidebar to the right of the layer column.
    if (pending_build_kind != ImGuiAppNodeKind_COUNT && pending_build_owner >= 0)
    {
      const ImGuiAppNode* owner = AppGraphFindNodeConst(g, pending_build_owner);
      const ImVec2 base = owner != nullptr ? owner->GridPos : ImVec2(640.0f, 60.0f);
      ImVec2 spot(base.x + 620.0f, base.y);
      ImGuiAppNode* added = AppGraphAddNode(g, pending_build_kind, pending_build_kind == ImGuiAppNodeKind_Sidebar ? "Sidebar" : "Window");
      AppGraphPlaceNode(g, added, &spot);
    }

    // Empty-canvas builder call-to-action: with no foundation yet, invite scaffolding the four guaranteed layers.
    // Root only -- an empty drill-down scope gets its own contextual CTA (below).
    {
      int design_count = 0;
      for (int i = 0; i < g->Nodes.Size; i++)
        if (!g->Nodes.Data[i].IsLive)
          design_count++;
      if (at_root && design_count == 0)
      {
        const float em = ImGui::GetFontSize();
        const char* head = ICON_FA_LAYER_GROUP "  Start with the app foundation";
        const char* sub  = "Every app builds on four layers: Window, Task, Command, Status.";
        const ImVec2 hs = ImGui::CalcTextSize(head);
        const ImVec2 ss = ImGui::CalcTextSize(sub);
        const float bw = ImGui::CalcTextSize("Scaffold foundation").x + em * 2.0f;
        const float pw = ImMax(ImMax(hs.x, ss.x), bw) + em * 2.5f;
        const float ph = em * 8.0f;
        const ImVec2 cen(editor_min.x + editor_size.x * 0.5f, editor_min.y + editor_size.y * 0.5f);
        const ImVec2 mn(cen.x - pw * 0.5f, cen.y - ph * 0.5f);
        const ImVec2 mx(cen.x + pw * 0.5f, cen.y + ph * 0.5f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(mn, mx, AppThemeNeutral(0.06f, 0.94f), em * 0.5f);
        dl->AddRect(mn, mx, AppColWithAlpha(AppComposerGetStyle()->Gold, 0.78f), em * 0.5f, 0, ImMax(1.0f, em * 0.09375f));
        dl->AddText(ImVec2(cen.x - hs.x * 0.5f, mn.y + em * 1.0f), AppThemeNeutral(0.92f), head);
        dl->AddText(ImVec2(cen.x - ss.x * 0.5f, mn.y + em * 2.6f), ImGui::GetColorU32(ImGuiCol_TextDisabled), sub);
        ImGui::SetCursorScreenPos(ImVec2(cen.x - bw * 0.5f, mx.y - em * 2.6f));
        if (ImGui::Button("Scaffold foundation", ImVec2(bw, em * 1.7f)))
          AppGraphEnsureFoundation(g);
      }
    }

    if (ImGui::BeginPopup("##AppGraphNodeCtx"))
    {
      ImGuiAppNode* cn = AppGraphEditorState(g)->CtxNodeId >= 0 ? AppGraphFindNode(g, AppGraphEditorState(g)->CtxNodeId) : nullptr;
      if (cn == nullptr)
        ImGui::CloseCurrentPopup();
      else if (cn->IsLive)
      {
        ImGui::TextDisabled("live node (read-only)");
        // Promote: spawn an editable design Control mirroring this live node's name + data type, so you can author
        // against a running control. The design node renders as "promoted" once its emitted type matches the live one.
        if (cn->Kind == ImGuiAppNodeKind_Control)
        {
          ImGui::Separator();
          if (ImGui::MenuItem("Promote to design"))
          {
            char nm[IM_LABEL_SIZE];
            char dt[IM_LABEL_SIZE];
            ImStrncpy(nm, cn->Draft.Name, IM_ARRAYSIZE(nm));
            ImStrncpy(dt, cn->DataTypeName, IM_ARRAYSIZE(dt));
            const ImVec2 near_pos = cn->GridPos + ImVec2(260.0f, 0.0f);   // root altitude: GridPos beside GridPos
            ImGuiAppNode* d = AppGraphAddNode(g, ImGuiAppNodeKind_Control, nm[0] ? nm : "NewControl");
            ImStrncpy(d->DataTypeName, dt, IM_ARRAYSIZE(d->DataTypeName));
            AppGraphPlaceNode(g, d, &near_pos);
            AppGraphEditorState(g)->CtxNodeId = d->Id;
            // The twin lives app-level, invisible from a drilled mirror interior -- jump to its
            // scope (the portal-chip idiom) instead of appearing to do nothing.
            if (AppScopeCurrent(g) >= 0 && !AppNodeInScope(g, d->Id))
              AppScopeJumpToNode(g, d->Id, selected_node_id);
          }
        }
      }
      else
      {
        ImGui::TextDisabled("%s", cn->Draft.Name[0] ? cn->Draft.Name : "(unnamed)");
        ImGui::Separator();
        // Struct node: explode/collapse its fields. Control node: explode/collapse its PersistData/TempData structs.
        if (cn->Kind == ImGuiAppNodeKind_Struct && !cn->IsLive && !cn->IsBuiltin)
        {
          if (AppGraphFieldNodeCount(g, cn->Id, 0) > 0)
          {
            if (ImGui::MenuItem("Collapse fields")) AppGraphCollapseFields(g, cn, 0);
          }
          else if (ImGui::MenuItem("Expand to fields", nullptr, false, cn->Draft.PersistFields.Size > 0))
            AppGraphExplodeFields(g, cn, 0);
        }
        else if (cn->Kind == ImGuiAppNodeKind_Control && !cn->IsLive && !cn->IsBuiltin)
        {
          for (int list = 0; list < 2; list++)
          {
            const bool temp = list == 1;
            const char* lbl = temp ? "TempData" : "PersistData";
            char item[64];
            if (AppControlStructId(g, cn, temp) >= 0)
            {
              ImFormatString(item, IM_ARRAYSIZE(item), "Collapse %s", lbl);
              if (ImGui::MenuItem(item)) AppGraphCollapseControlData(g, cn, temp);
            }
            else
            {
              ImFormatString(item, IM_ARRAYSIZE(item), "Explode %s", lbl);
              if (ImGui::MenuItem(item)) AppGraphExplodeControlData(g, cn, temp);
            }
          }
        }
        const bool can_duplicate = cn->Kind != ImGuiAppNodeKind_Layer;   // layers are one-per-type
        if (ImGui::MenuItem("Duplicate", nullptr, false, can_duplicate))
        {
          if (ImGuiAppNode* dup = AppGraphDuplicateNode(g, cn))
            AppGraphEditorState(g)->CtxNodeId = dup->Id;
        }
        // Save this node + its subtree as a reusable prefab (named after the node), instantiable from the palette.
        if (cn->Kind != ImGuiAppNodeKind_Layer && ImGui::MenuItem("Save as prefab"))
        {
          ImVector<int> roots;
          roots.push_back(cn->Id);
          AppGraphSavePrefab(g, roots, cn->Draft.Name[0] ? cn->Draft.Name : "prefab");
        }
        if (ImGui::MenuItem("Delete node"))
        {
          AppGraphRemoveNode(g, AppGraphEditorState(g)->CtxNodeId);
          AppGraphEditorState(g)->CtxNodeId = -1;
        }
      }
      ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##AppGraphLinkCtx"))
    {
      bool link_exists = false;
      for (int li = 0; li < g->Links.Size; li++)
        if (g->Links.Data[li].Id == AppGraphEditorState(g)->CtxLinkId) { link_exists = true; break; }
      if (!link_exists)
        ImGui::CloseCurrentPopup();
      else if (ImGui::MenuItem("Delete link"))
      {
        AppGraphEraseLink(g, AppGraphEditorState(g)->CtxLinkId);
        AppGraphEditorState(g)->CtxLinkId = -1;
      }
      ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##AppGraphAdd"))
    {
      const int add_scope = AppScopeCurrent(g);
      const ImGuiAppNode* add_owner = add_scope >= 0 ? AppGraphFindNodeConst(g, add_scope) : nullptr;
      if (add_owner != nullptr && add_owner->IsLive && add_owner->Kind != ImGuiAppNodeKind_Layer)
      {
        // Read-only mirror interior: nothing composes here; Promote (node context menu) authors.
        ImGui::TextDisabled("live %s (read-only)", AppNodeKindName(add_owner->Kind));
        ImGui::TextDisabled("promote a member to author against it");
        ImGui::EndPopup();
      }
      else
      {
      // Type-to-filter add palette: focus the search on open; an active filter flattens to a searchable list.
      if (ImGui::IsWindowAppearing())
      {
        AppGraphEditorState(g)->AddFilter.Clear();
        ImGui::SetKeyboardFocusHere();
      }
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
      AppGraphEditorState(g)->AddFilter.Draw("##addsearch");
      ImGui::Separator();

      // A drilled palette offers only what composes into this scope; the root offers every root kind.
      auto addable = [&](ImGuiAppNodeKind k)
      {
        if (k == ImGuiAppNodeKind_Field)
          return add_scope >= 0 && AppScopeKindComposable(g, add_scope, k);   // fields never live at root
        return add_scope < 0 || AppScopeKindComposable(g, add_scope, k);
      };

      int added_id = -1;
      if (AppGraphEditorState(g)->AddFilter.IsActive())
      {
        // The four core layers are guaranteed by the foundation and never added by hand; the only layer you
        // can author is a Custom one (your ImGuiAppLayer subclass).
        struct AddItem { const char* Label; ImGuiAppNodeKind Kind; ImGuiAppLayerType Layer; bool IsLayer; };
        static const AddItem items[] =
        {
          { "Control",       ImGuiAppNodeKind_Control, ImGuiAppLayerType_Task,    false },
          { "Struct",        ImGuiAppNodeKind_Struct,  ImGuiAppLayerType_Task,    false },
          { "Field",         ImGuiAppNodeKind_Field,   ImGuiAppLayerType_Task,    false },
          { "Window",        ImGuiAppNodeKind_Window,  ImGuiAppLayerType_Task,    false },
          { "Sidebar",       ImGuiAppNodeKind_Sidebar, ImGuiAppLayerType_Task,    false },
          { "Custom Layer",  ImGuiAppNodeKind_Layer,   ImGuiAppLayerType_Custom,  true  },
        };
        for (int i = 0; i < IM_ARRAYSIZE(items); i++)
        {
          if (!addable(items[i].Kind) || !AppGraphEditorState(g)->AddFilter.PassFilter(items[i].Label))
            continue;
          if (ImGui::Selectable(items[i].Label))
          {
            if (items[i].Kind == ImGuiAppNodeKind_Field)
              added_id = AppScopeAddFieldToStruct(g, add_scope);
            else
            {
              const char* nm = items[i].IsLayer ? AppLayerNodeName(items[i].Layer)
                             : items[i].Kind == ImGuiAppNodeKind_Control ? "NewControl"
                             : items[i].Kind == ImGuiAppNodeKind_Struct  ? "NewStruct"
                             : items[i].Kind == ImGuiAppNodeKind_Window   ? "Window" : "Sidebar";
              ImGuiAppNode* an = AppGraphAddNode(g, items[i].Kind, nm);
              if (items[i].IsLayer)
                an->LayerType = items[i].Layer;
              added_id = an->Id;
            }
          }
        }
      }
      else
      {
        if (addable(ImGuiAppNodeKind_Control) && ImGui::MenuItem("Control"))
          added_id = AppGraphAddNode(g, ImGuiAppNodeKind_Control, "NewControl")->Id;
        if (addable(ImGuiAppNodeKind_Struct) && ImGui::MenuItem("Struct"))
          added_id = AppGraphAddNode(g, ImGuiAppNodeKind_Struct,  "NewStruct")->Id;
        if (addable(ImGuiAppNodeKind_Field) && ImGui::MenuItem("Field"))
          added_id = AppScopeAddFieldToStruct(g, add_scope);
        // The core phases are guaranteed by the foundation -- the only authorable layer is a Custom subclass.
        if (addable(ImGuiAppNodeKind_Layer) && ImGui::MenuItem("Custom Layer"))
        {
          ImGuiAppNode* an = AppGraphAddNode(g, ImGuiAppNodeKind_Layer, "CustomLayer");
          an->LayerType = ImGuiAppLayerType_Custom;
          added_id = an->Id;
        }
        if (addable(ImGuiAppNodeKind_Window) && ImGui::MenuItem("Window"))
          added_id = AppGraphAddNode(g, ImGuiAppNodeKind_Window,  "Window")->Id;
        if (addable(ImGuiAppNodeKind_Sidebar) && ImGui::MenuItem("Sidebar"))
          added_id = AppGraphAddNode(g, ImGuiAppNodeKind_Sidebar, "Sidebar")->Id;
        // Templates rebuild the whole document -- a root verb (drilled, the wipe would take the scope owner).
        if (add_scope < 0)
        {
          ImGui::Separator();
          if (ImGui::BeginMenu("New from template"))
          {
            if (ImGui::MenuItem("Empty app (layers + window)")) AppGraphLoadTemplate(g, 0);
            if (ImGui::MenuItem("Window + control"))            AppGraphLoadTemplate(g, 1);
            if (ImGui::MenuItem("Struct producer + consumer"))  AppGraphLoadTemplate(g, 2);
            ImGui::EndMenu();
          }
        }
        // Round-trip: reconstruct Struct nodes from C++ source on the clipboard (inverse of the struct codegen).
        if (addable(ImGuiAppNodeKind_Struct))
        {
          if (ImGui::MenuItem("Paste C++ struct(s)"))
          {
            if (const char* clip = ImGui::GetClipboardText())
            {
              const int first = g->Nodes.Size;
              AppGraphImportStructsFromCode(g, clip, add_scope >= 0 ? ImVec2(0.0f, 0.0f) : AppGraphEditorState(g)->AddPopupGrid);
              if (add_scope >= 0)
                AppScopeComposeImported(g, first, &AppGraphEditorState(g)->AddPopupGrid);
            }
          }
          ImGui::SetItemTooltip("Parse 'struct Name { ... };' blocks from the clipboard into nodes");
        }
        // Prefabs: instantiate a previously saved subtree at the drop point.
        if (AppGraphPrefabCount(g) > 0 && ImGui::BeginMenu("Prefabs"))
        {
          for (int i = 0; i < AppGraphPrefabCount(g); i++)
            if (ImGui::MenuItem(AppGraphPrefabName(g, i)))
            {
              const int first = g->Nodes.Size;
              AppGraphInstantiatePrefab(g, i, add_scope >= 0 ? ImVec2(0.0f, 0.0f) : AppGraphEditorState(g)->AddPopupGrid);
              if (add_scope >= 0)
                AppScopeComposeImported(g, first, &AppGraphEditorState(g)->AddPopupGrid);
            }
          ImGui::EndMenu();
        }
      }
      if (added_id >= 0)
      {
        // A node born inside a scope is composed into it (stays visible); its root position derives
        // at root altitude, never from the interior click.
        if (add_scope >= 0)
          AppScopeComposeNewNode(g, added_id, &AppGraphEditorState(g)->AddPopupGrid);
        else if (ImGuiAppNode* an = AppGraphFindNode(g, added_id))
          AppGraphPlaceNode(g, an, &AppGraphEditorState(g)->AddPopupGrid);
      }
      ImGui::EndPopup();
      }
    }

    // Command palette (Space): editor verbs with their shortcuts, the host's document verbs (via
    // AppGraphSetHostCommands), and go-to-node over the graph itself.
    if (ImGui::BeginPopup("##cmdpalette"))
    {
      if (ImGui::IsWindowAppearing())
      {
        AppGraphEditorState(g)->CmdFilter.Clear();
        ImGui::SetKeyboardFocusHere();
      }
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 19.0f);
      AppGraphEditorState(g)->CmdFilter.Draw("##cmdsearch");
      ImGui::Separator();

      // One palette row: full-width selectable + the shortcut drawn dim at the right edge over it.
      auto cmd_row = [](const char* label, const char* shortcut) -> bool
      {
        const ImVec2 rp = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::Selectable(label);
        if (shortcut != nullptr && shortcut[0])
        {
          const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
          ImGui::GetWindowDrawList()->AddText(ImVec2(right - ImGui::CalcTextSize(shortcut).x, rp.y),
                                              ImGui::GetColorU32(ImGuiCol_TextDisabled), shortcut);
        }
        return clicked;
      };

      // Palette rows render straight from the command registry (F34): only verbs that declare the palette
      // surface AND pass their availability predicate (a drilled scope offers only what it takes).
      const int pal_scope = AppScopeCurrent(g);
      int run = -1;
      for (int i = 0; i < AppGraphEditorCommandCount(); i++)
      {
        const ImGuiAppEditorCommand* c = AppGraphEditorCommandAt(i);
        if (!(c->Surfaces & ImGuiAppCmdSurface_Palette) || !AppGraphEditorCommandAvailable(g, c))
          continue;
        if (!AppGraphEditorState(g)->CmdFilter.PassFilter(c->Label))
          continue;
        char lbl[IM_LABEL_SIZE + 32];
        if (c->Icon[0])
          ImFormatString(lbl, IM_ARRAYSIZE(lbl), "%s  %s", c->Icon, c->Label);
        else
          ImStrncpy(lbl, c->Label, IM_ARRAYSIZE(lbl));
        if (cmd_row(lbl, c->Shortcut))
          run = c->Id;
      }

      // Host document verbs (save/generate/panels...), same rows, host-owned meaning. Picking one is recorded
      // for AppGraphConsumeHostCommand -- the editor never acts on the host's behalf.
      if (AppGraphEditorState(g)->HostCmdCount > 0)
      {
        bool any_host = false;
        for (int i = 0; i < AppGraphEditorState(g)->HostCmdCount; i++)
          any_host = any_host || AppGraphEditorState(g)->CmdFilter.PassFilter(AppGraphEditorState(g)->HostCmds[i].Label);
        if (any_host)
          ImGui::Separator();
        for (int i = 0; i < AppGraphEditorState(g)->HostCmdCount; i++)
          if (AppGraphEditorState(g)->CmdFilter.PassFilter(AppGraphEditorState(g)->HostCmds[i].Label) && cmd_row(AppGraphEditorState(g)->HostCmds[i].Label, AppGraphEditorState(g)->HostCmds[i].Shortcut))
          {
            AppGraphEditorState(g)->HostCmdPicked = AppGraphEditorState(g)->HostCmds[i].Id;
            ImGui::CloseCurrentPopup();
          }
      }

      // Go to node: the palette searches the MODEL too. Only while filtering -- an unfiltered dump of
      // every node would bury the verbs.
      if (AppGraphEditorState(g)->CmdFilter.IsActive())
      {
        bool goto_header = false;
        int shown = 0;
        for (int i = 0; i < g->Nodes.Size && shown < 8; i++)
        {
          const ImGuiAppNode* n = &g->Nodes.Data[i];
          if ((!show_live && n->IsLive) || n->Hidden || !n->Draft.Name[0] || !AppGraphEditorState(g)->CmdFilter.PassFilter(n->Draft.Name))
            continue;
          if (!goto_header)
          {
            ImGui::Separator();
            goto_header = true;
          }
          char row[IM_LABEL_SIZE + 24];
          ImFormatString(row, IM_ARRAYSIZE(row), "Go to: %s###goto%d", n->Draft.Name, n->Id);
          if (cmd_row(row, n->IsLive ? "live" : ""))
          {
            if (selected_node_id != nullptr)
              *selected_node_id = n->Id;
            g->Selection.clear();
            g->Selection.push_back(n->Id);
            ImGui::CanvasSelectNode(cv, n->Id, false);
            fit_ids(g->Selection);
            ImGui::CloseCurrentPopup();
          }
          shown++;
        }
      }

      if (run >= 0)
      {
        // Palette verbs act at the view center (there is no pointer position to honor).
        const ImVec2 pal_center = ImGui::CanvasFromScreen(cv, editor_min + editor_size * 0.5f);
        ImGuiAppNode* added = nullptr;
        switch (run)
        {
        case 0: added = AppGraphAddNode(g, ImGuiAppNodeKind_Control, "NewControl"); break;
        case 1: added = AppGraphAddNode(g, ImGuiAppNodeKind_Struct,  "NewStruct");  break;
        case 2: added = AppGraphAddNode(g, ImGuiAppNodeKind_Window,  "Window");     break;
        case 3: added = AppGraphAddNode(g, ImGuiAppNodeKind_Sidebar, "Sidebar");    break;
        case 4:
          added = AppGraphAddNode(g, ImGuiAppNodeKind_Layer, "CustomLayer");
          added->LayerType = ImGuiAppLayerType_Custom;
          break;
        case 5:
        {
          const int fid = AppScopeAddFieldToStruct(g, pal_scope);
          added = fid >= 0 ? AppGraphFindNode(g, fid) : nullptr;
          break;
        }
        case 10: AppGraphAutoLayout(g, show_live); fit_all(); break;
        case 11: fit_all(); break;
        case 12: fit_ids(g->Selection); break;
        case 13: snap_grid = !snap_grid; break;
        case 14: if (AppGraphCanUndo(g)) { AppGraphUndo(g); ImGui::CanvasClearSelection(cv); } break;
        case 15: if (AppGraphCanRedo(g)) { AppGraphRedo(g); ImGui::CanvasClearSelection(cv); } break;
        case 16: AppGraphCopySelection(g, g->Selection); break;
        case 17:
        {
          const int first = g->Nodes.Size;
          AppGraphPasteClipboard(g);
          if (pal_scope >= 0)
            AppScopeComposeImported(g, first, &pal_center);
          break;
        }
        case 18:
        {
          ImVector<int> sel = g->Selection;
          for (int i = 0; i < sel.Size; i++)
            if (const ImGuiAppNode* sn = AppGraphFindNode(g, sel.Data[i]))
              AppGraphDuplicateNode(g, sn);
          break;
        }
        case 19:
        {
          ImVector<int> sel = g->Selection;
          for (int i = 0; i < sel.Size; i++)
          {
            const ImGuiAppNode* sn = AppGraphFindNode(g, sel.Data[i]);
            if (sn != nullptr && !sn->IsLive)
              AppGraphRemoveNode(g, sel.Data[i]);
            else if (sn != nullptr && sn->IsLive)
              AppNotifyLiveReadOnly(g, sn);   // Delete command on a live pick: refuse with the notice
          }
          break;
        }
        case 20:
          for (int i = 0; i < g->Nodes.Size; i++)
          {
            ImGuiAppNode* n = &g->Nodes.Data[i];
            const bool owner = (n->Kind == ImGuiAppNodeKind_Window || n->Kind == ImGuiAppNodeKind_Sidebar) ? AppGraphHostsControl(g, n->Id)
                             : n->Kind == ImGuiAppNodeKind_Control ? (n->PersistStructId >= 0 || n->TempStructId >= 0)
                             : n->Kind == ImGuiAppNodeKind_Struct ? (AppGraphFieldNodeCount(g, n->Id, 0) > 0) : false;
            if (owner)
              n->GroupCollapsed = true;
          }
          break;
        case 21:
          for (int i = 0; i < g->Nodes.Size; i++)
          {
            g->Nodes.Data[i].GroupCollapsed = false;
            g->Nodes.Data[i]._NeedsPlace = true;
          }
          break;
        case 22:
          if (const char* clip = ImGui::GetClipboardText())
          {
            const int first = g->Nodes.Size;
            AppGraphImportStructsFromCode(g, clip, pal_scope >= 0 ? ImVec2(0.0f, 0.0f) : pal_center);
            if (pal_scope >= 0)
              AppScopeComposeImported(g, first, &pal_center);
          }
          break;
        case 23:
        {
          ImGuiAppNode* sel = (selected_node_id != nullptr && *selected_node_id >= 0) ? AppGraphFindNode(g, *selected_node_id) : nullptr;
          if (sel != nullptr && AppScopeCanEnter(sel) && !AppNodeHiddenByCollapse(g, sel->Id) && !(!show_live && sel->IsLive))
            g->ViewScope.push_back(sel->Id);
          break;
        }
        case 24:
          if (g->ViewScope.Size > 0)
          {
            const int exited = g->ViewScope.back();
            g->ViewScope.pop_back();
            if (selected_node_id != nullptr)
              *selected_node_id = exited;
          }
          break;
        case 25:
          for (int i = 0; i < g->Selection.Size; i++)
          {
            ImGuiAppNode* sn = AppGraphFindNode(g, g->Selection.Data[i]);
            if (sn != nullptr && !sn->IsLive && sn->Kind != ImGuiAppNodeKind_Layer)
              sn->Hidden = true;
          }
          ImGui::CanvasClearSelection(cv);
          g->Selection.clear();
          break;
        case 26:
          for (int i = 0; i < g->Nodes.Size; i++)
            g->Nodes.Data[i].Hidden = false;
          break;
        case 27: AppGraphViewState(g)->OvGrid = !AppGraphViewState(g)->OvGrid; break;
        case 28: AppGraphViewState(g)->OvBands = !AppGraphViewState(g)->OvBands; break;
        case 29: AppGraphViewState(g)->OvFrames = !AppGraphViewState(g)->OvFrames; break;
        case 30: AppGraphViewState(g)->OvMinimap = !AppGraphViewState(g)->OvMinimap; break;
        case 31:
          if (selected_node_id != nullptr && *selected_node_id >= 0)
          {
            const ImGuiAppNode* sel = AppGraphFindNodeConst(g, *selected_node_id);
            if (sel != nullptr && !sel->IsLive && (sel->Kind != ImGuiAppNodeKind_Layer || sel->LayerType == ImGuiAppLayerType_Custom))
              g->EditingNodeId = sel->Id;
            else if (sel != nullptr && sel->IsLive)
              AppNotifyLiveReadOnly(g, sel);   // Rename command on a live mirror: refuse with the notice
          }
          break;
        case 32: case 33:
        {
          // Same restack the [ / ] keys perform (see the z-order shortcut block).
          const bool to_front = run == 33;
          if (g->Selection.Size > 0)
          {
            ImVector<ImGuiAppNode> reordered;
            reordered.reserve(g->Nodes.Size);
            for (int pass = 0; pass < 2; pass++)
              for (int i = 0; i < g->Nodes.Size; i++)
              {
                const bool in_sel = AppIdInSet(g->Selection, g->Nodes.Data[i].Id);
                if ((pass == 0) == (to_front ? !in_sel : in_sel))
                  reordered.push_back(g->Nodes.Data[i]);
              }
            g->Nodes.Size = 0;            // abandon the old elements (inner buffers now live in `reordered`)
            g->Nodes.swap(reordered);
          }
          break;
        }
        case 34: AppGraphEditorState(g)->HelpOverlay = !AppGraphEditorState(g)->HelpOverlay; break;
        case 35: g->ViewScope.clear(); break;
        case 36: AppGraphEditorState(g)->QuickInspector = !AppGraphEditorState(g)->QuickInspector; break;
        case 37: AppGraphViewState(g)->TreeOpen = !AppGraphViewState(g)->TreeOpen; break;
        case 38: AppGraphViewState(g)->InspOpen = !AppGraphViewState(g)->InspOpen; break;
        default: break;
        }
        if (added != nullptr)
        {
          // Compose the new node into the current scope, seated at the view center; at root the
          // default open placement from AppGraphAddNode stands.
          if (pal_scope >= 0)
            AppScopeComposeNewNode(g, added->Id, &pal_center);
        }
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    // Brushing echo: halo the node the user is pointing at in ANOTHER view (the canvas's own hovered node
    // is already outlined by the canvas), and halo both endpoints of a hovered wire -- wherever it was hovered.
    {
      const float em = ImGui::GetFontSize();
      auto halo = [&](int node_id)
      {
        if (node_id < 0 || !AppEditorNodeWasSubmitted(g, node_id))
          return;
        const ImGuiAppNode* hn = AppGraphFindNodeConst(g, node_id);
        if (hn == nullptr)
          return;
        const ImU32 tint = AppGraphOriginColor(hn);
        const ImU32 accent = tint ? tint : (hn->Kind == ImGuiAppNodeKind_Layer ? AppLayerAccent(hn->LayerType) : AppKindColor(hn->Kind));
        const ImVec2 p = ImGui::CanvasToScreen(cv, AppCanvasNodePos(g, node_id));
        const ImVec2 d = ImGui::CanvasNodeSize(cv, node_id) * AppCanvasScale(g);
        const float ex = em * 0.25f;
        ImGui::GetWindowDrawList()->AddRect(ImVec2(p.x - ex, p.y - ex), ImVec2(p.x + d.x + ex, p.y + d.y + ex),
                                            (accent & 0x00FFFFFF) | 0xE6000000, em * 0.4f, 0, 2.0f);
      };
      ImGuiAppHoverSource nsrc = ImGuiAppHoverSource_None;
      ImGuiAppHoverSource lsrc = ImGuiAppHoverSource_None;
      const int hnode = AppGraphHoveredNode(g, &nsrc);
      const int hlink = AppGraphHoveredLink(g, &lsrc);
      if (hnode >= 0 && nsrc != ImGuiAppHoverSource_Canvas)
        halo(hnode);
      if (hlink >= 0)
        for (int li = 0; li < g->Links.Size; li++)
          if (g->Links.Data[li].Id == hlink)
          {
            halo(AppGraphPortOwnerId(g, g->Links.Data[li].StartAttr));
            halo(AppGraphPortOwnerId(g, g->Links.Data[li].EndAttr));
            break;
          }
    }

    // Ambient problem marks: a severity dot on the node's title bar, where the problem lives.
    {
      const float em = ImGui::GetFontSize();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      for (int i = 0; i < AppGraphEditorState(g)->PoolIds.Size; i++)
      {
        const int sev = AppGraphNodeSeverity(g, AppGraphEditorState(g)->PoolIds.Data[i]);
        if (sev <= 0)
          continue;
        const ImVec2 p = ImGui::CanvasToScreen(cv, AppCanvasNodePos(g, AppGraphEditorState(g)->PoolIds.Data[i]));
        const ImVec2 d = ImGui::CanvasNodeSize(cv, AppGraphEditorState(g)->PoolIds.Data[i]) * AppCanvasScale(g);
        const float r = em * 0.22f;
        const ImVec2 c(p.x + d.x - r - em * 0.3f, p.y + em * 0.55f);
        dl->AddCircleFilled(c, r, AppSeverityColor(sev));
        dl->AddCircle(c, r, AppComposerGetStyle()->DarkOutline, 0, 1.0f);
      }
    }

    // Status hint: what the mouse does RIGHT NOW given the hover target. Composed here (only the editor
    // knows the hover target), rendered by the host's status bar via AppGraphStatusHint. This is the ONE
    // feedback slot (F14): every transient notice -- refused links, refused composes, AppGraphNotify --
    // rides the LastLinkErr channel and shows here for a single 2.5 s window. No floating canvas toast.
    {
      if (g->LastLinkErrSeq != AppGraphEditorState(g)->ErrSeqSeen)
      {
        AppGraphEditorState(g)->ErrSeqSeen = g->LastLinkErrSeq;
        AppGraphEditorState(g)->ErrTime = ImGui::GetTime();
      }
      const bool show_err = g->LastLinkErr[0] != 0 && (ImGui::GetTime() - AppGraphEditorState(g)->ErrTime) < 2.5;

      AppGraphEditorState(g)->StatusSev = show_err ? 2 : 0;
      if (show_err)
        ImFormatString(AppGraphEditorState(g)->StatusHint, IM_ARRAYSIZE(AppGraphEditorState(g)->StatusHint), "%s %s", ICON_FA_TRIANGLE_EXCLAMATION, g->LastLinkErr);
      else if (over_pin)
        ImStrncpy(AppGraphEditorState(g)->StatusHint, "drag  wire   (release on empty canvas: filtered add)", IM_ARRAYSIZE(AppGraphEditorState(g)->StatusHint));
      else if (over_link)
        ImStrncpy(AppGraphEditorState(g)->StatusHint, "drag end  rewire     click  select     Del  delete     RMB  menu", IM_ARRAYSIZE(AppGraphEditorState(g)->StatusHint));
      else if (over_node)
      {
        const ImGuiAppNode* hn = AppGraphFindNodeConst(g, hovered_node);
        if (hn != nullptr && hn->IsLive)
          ImStrncpy(AppGraphEditorState(g)->StatusHint, "live mirror (read-only)     dbl-click  enter     RMB  menu", IM_ARRAYSIZE(AppGraphEditorState(g)->StatusHint));
        else if (hn != nullptr && hn->Kind == ImGuiAppNodeKind_Layer)
          ImStrncpy(AppGraphEditorState(g)->StatusHint, "drag  reorder phase     dbl-click  enter     RMB  menu", IM_ARRAYSIZE(AppGraphEditorState(g)->StatusHint));
        else
          ImStrncpy(AppGraphEditorState(g)->StatusHint, "drag  move     click  select (Ctrl multi)     Tab  enter scope     dbl-click  rename     RMB  menu", IM_ARRAYSIZE(AppGraphEditorState(g)->StatusHint));
      }
      else
        ImStrncpy(AppGraphEditorState(g)->StatusHint, "drag  pan     wheel  zoom     RMB  add     Space  palette     F  frame     F1  help", IM_ARRAYSIZE(AppGraphEditorState(g)->StatusHint));
    }

    // Viewport gizmo cluster (top-right overlay column): VIEW verbs only; document verbs belong to the
    // host toolbar. Nothing here mutates the model except Tidy. Draw-list buttons: hit-tests follow the
    // overlay rule (AllowWhenBlockedByActiveItem, see AppTreeRowIcon).
    {
      const float em = ImGui::GetFontSize();
      const float r = em * 0.72f;
      const float step = r * 2.0f + em * 0.30f;
      // Above canvas content, inside the editor's z-order (never over other windows).
      ImDrawList* dl = ImGui::CanvasAnnotationDrawList(cv);
      dl->PushClipRect(editor_min, editor_min + editor_size, true);

      const int   count = 7;
      const ImVec2 col_c(editor_min.x + editor_size.x - em * 1.2f, editor_min.y + em * 1.2f);
      const ImVec2 giz_min(col_c.x - r - em * 0.25f, col_c.y - r - em * 0.25f);
      const ImVec2 giz_max(col_c.x + r + em * 0.25f, col_c.y + (count - 1) * step + r + em * 0.25f);

      // F38 motion ladder: the cluster is quiet at rest, brightens when the pointer is on it, and
      // recedes during a canvas gesture (any drag inside the editor). One linear fade (FadeMs) carries
      // it between states -- no overlay hard-codes its own alpha; they read the motion table.
      const ImGuiAppComposerMotion* mo = AppComposerGetMotion();
      const ImVec2 mp = ImGui::GetIO().MousePos;
      const bool ov_editor  = mp.x >= editor_min.x && mp.x < editor_min.x + editor_size.x && mp.y >= editor_min.y && mp.y < editor_min.y + editor_size.y;
      const bool ov_cluster = mp.x >= giz_min.x && mp.x < giz_max.x && mp.y >= giz_min.y && mp.y < giz_max.y;
      const bool gesturing  = ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ov_editor;
      const float ov_target = gesturing ? mo->OverlayGesture : (ov_cluster ? mo->OverlayHover : mo->OverlayRest);
      float& ov_a = AppGraphEditorState(g)->OverlayAlpha;
      const float ov_stepA = (mo->FadeMs > 0.0f) ? ImGui::GetIO().DeltaTime / (mo->FadeMs * 0.001f) : 1.0f;   // full 0..1 range per FadeMs
      if      (ov_a < ov_target) ov_a = ImMin(ov_target, ov_a + ov_stepA);
      else if (ov_a > ov_target) ov_a = ImMax(ov_target, ov_a - ov_stepA);
      AppGraphEditorState(g)->GizmoRectMin = giz_min;
      AppGraphEditorState(g)->GizmoRectMax = giz_max;

      dl->AddRectFilled(giz_min, giz_max, AppColWithAlpha(AppThemeNeutral(0.04f, 1.0f), 0.99f * ov_a), r + em * 0.25f);

      float gy = col_c.y;
      const ImU32 dim = ImGui::GetColorU32(ImGuiCol_Text, 0.55f * ov_a);
      const ImU32 lit = AppColWithAlpha(AppComposerGetStyle()->Gold, ov_a);
      AppGraphEditorState(g)->GizmoCount = 0;   // F40: republish gizmo centres in draw order for the click-path test
      auto gizmo = [&](const char* icon, const char* tip, bool on) -> bool
      {
        const ImVec2 c(col_c.x, gy);
        gy += step;
        if (AppGraphEditorState(g)->GizmoCount < IM_ARRAYSIZE(AppGraphEditorState(g)->GizmoCenters))
          AppGraphEditorState(g)->GizmoCenters[AppGraphEditorState(g)->GizmoCount++] = c;
        if (on)
          dl->AddCircleFilled(c, r, (lit & 0x00FFFFFF) | ((ImU32)(0x38 * ov_a) << 24));
        const bool clicked = AppTreeRowIcon(icon, c, r, on ? lit : dim, dl);
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float dx = m.x - c.x, dy = m.y - c.y;
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && dx * dx + dy * dy <= r * r)
          ImGui::SetTooltip("%s", tip);
        return clicked;
      };

      if (gizmo(ICON_FA_PLUS, "Add node (Space / RMB)", false))
      {
        AppGraphEditorState(g)->AddPopupGrid = ImGui::CanvasFromScreen(cv, editor_min + editor_size * 0.5f);
        ImGui::OpenPopup("##AppGraphAdd");
      }
      if (gizmo(ICON_FA_CROSSHAIRS, "Frame selection (F)", false))
        if (!fit_ids(g->Selection) && ImGui::CanvasNumSelectedNodes(cv) > 0)
        {
          int picked = 0;
          ImGui::CanvasGetSelectedNodes(cv, &picked, 1);
          ImGui::CanvasCenterOn(cv, AppCanvasNodePos(g, picked) + ImGui::CanvasNodeSize(cv, picked) * 0.5f);
        }
      if (gizmo(ICON_FA_EXPAND, "Fit all (Home)", false))
        fit_all();
      if (gizmo(ICON_FA_WAND_MAGIC_SPARKLES, "Tidy layout (L)", false))
        AppGraphAutoLayout(g, show_live);
      if (gizmo(ICON_FA_MAGNET, "Snap to grid (G)", snap_grid))
        snap_grid = !snap_grid;
      if (gizmo(ICON_FA_SLIDERS, "Overlays", !(ov_grid && ov_bands && ov_frames && ov_minimap)))
        ImGui::OpenPopup("##CanvasOverlays");
      if (ImGui::BeginPopup("##CanvasOverlays"))
      {
        ImGui::TextDisabled("Overlays");
        ImGui::Separator();
        ImGui::MenuItem("Grid", nullptr, &ov_grid);
        ImGui::MenuItem("Phase bands", nullptr, &ov_bands);
        ImGui::MenuItem("Group frames", nullptr, &ov_frames);
        ImGui::MenuItem("Minimap", nullptr, &ov_minimap);
        ImGui::EndPopup();
      }
      // View scope as an explicit mode: whole app, or drilled into one composition. Same state
      // Tab/Esc/breadcrumb navigate.
      if (gizmo(ICON_FA_LAYER_GROUP, g->ViewScope.Size > 0 ? "View scope (drilled in)" : "View scope: whole app", g->ViewScope.Size > 0))
        ImGui::OpenPopup("##ViewScopeMode");
      if (ImGui::BeginPopup("##ViewScopeMode"))
      {
        ImGui::TextDisabled("View scope");
        ImGui::Separator();
        if (ImGui::MenuItem("Whole app", "Esc", g->ViewScope.Size == 0))
          g->ViewScope.clear();
        for (int i = 0; i < g->Nodes.Size; i++)
        {
          const ImGuiAppNode* sn = &g->Nodes.Data[i];
          if (sn->Kind != ImGuiAppNodeKind_Layer || (!show_live && sn->IsLive) || !AppScopeCanEnter(const_cast<ImGuiAppNode*>(sn)))
            continue;
          const char* nm = sn->Kind == ImGuiAppNodeKind_Layer && AppLayerIsCore(sn->LayerType) ? AppLayerNodeName(sn->LayerType) : sn->Draft.Name;
          if (ImGui::MenuItem(nm, nullptr, AppScopeCurrent(g) == sn->Id))
          {
            g->ViewScope.clear();
            g->ViewScope.push_back(sn->Id);
          }
        }
        ImGui::EndPopup();
      }
      dl->PopClipRect();
    }

    // Quick inspector (N): a floating, self-sized inspector beside the primary selection. Follows the
    // selection; N (or its X) closes it.
    if (AppGraphEditorState(g)->QuickInspector && selected_node_id != nullptr && *selected_node_id >= 0 && AppEditorNodeWasSubmitted(g, *selected_node_id))
    {
      const float em_qi = ImGui::GetFontSize();
      const ImVec2 np = ImGui::CanvasToScreen(cv, AppCanvasNodePos(g, *selected_node_id));
      const ImVec2 nd = ImGui::CanvasNodeSize(cv, *selected_node_id) * AppCanvasScale(g);
      ImVec2 pos(np.x + nd.x + em_qi, np.y);
      pos.x = ImClamp(pos.x, editor_min.x, editor_min.x + editor_size.x - em_qi * 18.0f);
      pos.y = ImClamp(pos.y, editor_min.y, editor_min.y + editor_size.y - em_qi * 8.0f);
      ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
      ImGui::SetNextWindowSizeConstraints(ImVec2(em_qi * 17.0f, 0.0f), ImVec2(em_qi * 22.0f, em_qi * 26.0f));
      ImGui::SetNextWindowBgAlpha(0.97f);
      if (ImGui::Begin("Quick inspect###quick_insp", &AppGraphEditorState(g)->QuickInspector,
                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoCollapse))
      {
        EditAppNodeInspector(g, *selected_node_id);
      }
      ImGui::End();
    }

    if (AppGraphEditorState(g)->HelpOverlay)
    {
      static const char* lines[] =
      {
        "Space   command palette",
        "Tab      enter selection scope   Esc   up one scope",
        "dbl-click layer   drill into its composition",
        "F        frame selection      Home  fit all",
        "A        select all / none    H     hide sel (Alt+H show)",
        "L        tidy layout          G     snap-to-grid",
        "[ / ]   send back / front     arrows nudge (Shift x10)",
        "Del      delete                F2    rename selection",
        "Ctrl+Z / Ctrl+Y   undo / redo",
        "Ctrl+C / Ctrl+V   copy / paste subtree",
        "Ctrl+D   duplicate selection   Ctrl+S save graph",
        "drag pin -> empty   new node       drag link end   rewire",
        "wheel over field    scrub value",
        "click group title    collapse      drag group title    move group",
        "drag empty canvas    pan (also right-drag)",
        "wheel over canvas    zoom (Ctrl+wheel anywhere)",
        "right-click          context menu",
        "N        quick inspector at the selection",
        "F1       toggle this help",
      };
      const float em = ImGui::GetFontSize();
      ImVec2 mn(editor_min.x + em * 0.8f, editor_min.y + em * 0.8f);
      float w = 0.0f;
      for (int i = 0; i < IM_ARRAYSIZE(lines); i++)
        w = ImMax(w, ImGui::CalcTextSize(lines[i]).x);
      const ImVec2 mx(mn.x + w + em * 1.4f, mn.y + (float)IM_ARRAYSIZE(lines) * ImGui::GetTextLineHeightWithSpacing() + em * 1.4f);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(mn, mx, AppThemeNeutral(0.04f, 0.92f), em * 0.375f);
      dl->AddRect(mn, mx, AppColWithAlpha(AppComposerGetStyle()->Gold, 0.78f), em * 0.375f, 0, ImMax(1.0f, em * 0.09375f));
      ImVec2 tp(mn.x + em * 0.7f, mn.y + em * 0.7f);
      dl->AddText(tp, AppComposerGetStyle()->Gold, "Shortcuts");
      tp.y += ImGui::GetTextLineHeightWithSpacing();
      for (int i = 0; i < IM_ARRAYSIZE(lines); i++)
      {
        dl->AddText(tp, AppThemeNeutral(0.86f), lines[i]);
        tp.y += ImGui::GetTextLineHeightWithSpacing();
      }
    }

    char err[128];
    ImGui::CaptureAppGraphLinks(g, err, IM_ARRAYSIZE(err));

    // Pin-drag-to-create: dragging a wire from a pin and releasing on empty canvas opens a kind-filtered palette
    // (only node kinds that can legally connect to that pin) and, on pick, creates the node at the drop point and
    // auto-wires it. A detach re-drag dropped on empty means "delete" and must not palette (AppGraphEditorState(g)->DragWasDetach,
    // set by the capture above when the detach event fired at grab time).
    static ImVec2 drop_grid(0.0f, 0.0f);
    {
      int    da = -1;
      ImVec2 dpos(0.0f, 0.0f);
      if (ImGui::CanvasWireDropped(cv, &da, &dpos))
      {
        if (!AppGraphEditorState(g)->DragWasDetach && da > 0)
        {
          AppGraphEditorState(g)->DropSrcAttr = da;
          drop_grid = dpos;   // already model units
          ImGui::OpenPopup("##AppGraphDropCreate");
        }
        AppGraphEditorState(g)->DragWasDetach = false;
      }
    }
    if (ImGui::BeginPopup("##AppGraphDropCreate"))
    {
      ImGuiAppNode* sowner = nullptr;
      ImGuiAppNodePort* sp = (AppGraphEditorState(g)->DropSrcAttr > 0) ? AppGraphFindPort(g, AppGraphEditorState(g)->DropSrcAttr, &sowner) : nullptr;
      if (sp == nullptr || sowner == nullptr)
      {
        ImGui::CloseCurrentPopup();
      }
      else
      {
        ImGui::TextDisabled("from %s.%s", sowner->Draft.Name, sp->Name);
        ImGui::Separator();

        struct DropCand { const char* Label; ImGuiAppNodeKind Kind; ImGuiAppPortKind Comp; const char* Name; };
        DropCand cands[2];
        int      cand_count = 0;
        switch (sp->Kind)
        {
        case ImGuiAppPortKind_DataOut:
          cands[cand_count++] = { "Control (consumer)", ImGuiAppNodeKind_Control, ImGuiAppPortKind_DataIn, "NewControl" };
          break;
        case ImGuiAppPortKind_DataIn:
          cands[cand_count++] = { "Struct (producer)", ImGuiAppNodeKind_Struct,  ImGuiAppPortKind_DataOut, "NewStruct" };
          cands[cand_count++] = { "Control (producer)", ImGuiAppNodeKind_Control, ImGuiAppPortKind_DataOut, "NewControl" };
          break;
        case ImGuiAppPortKind_ChildOut:
          cands[cand_count++] = { "Window (host)",  ImGuiAppNodeKind_Window,  ImGuiAppPortKind_ChildIn, "Window" };
          cands[cand_count++] = { "Sidebar (host)", ImGuiAppNodeKind_Sidebar, ImGuiAppPortKind_ChildIn, "Sidebar" };
          break;
        case ImGuiAppPortKind_ChildIn:
          if (sowner->Kind == ImGuiAppNodeKind_Struct)
            cands[cand_count++] = { "Field", ImGuiAppNodeKind_Field, ImGuiAppPortKind_ChildOut, "field" };
          else
            cands[cand_count++] = { "Control (child)", ImGuiAppNodeKind_Control, ImGuiAppPortKind_ChildOut, "NewControl" };
          break;
        default:
          break;
        }

        // Drilled: only what stays visible here -- a ChildIn pick is composed by the wire itself;
        // anything else must be a kind this scope takes.
        const int drop_scope = AppScopeCurrent(g);
        const ImGuiAppPortKind sp_kind = sp->Kind;
        if (drop_scope >= 0)
        {
          int keep = 0;
          for (int c = 0; c < cand_count; c++)
            if (sp_kind == ImGuiAppPortKind_ChildIn || AppScopeKindComposable(g, drop_scope, cands[c].Kind))
              cands[keep++] = cands[c];
          cand_count = keep;
          if (cand_count == 0)
            ImGui::TextDisabled("nothing composes here");
        }

        const ImVec2 sowner_gp = sowner->GridPos;   // by value: the add below dangles sowner
        for (int c = 0; c < cand_count; c++)
        {
          if (!ImGui::Selectable(cands[c].Label))
            continue;
          ImGuiAppNode* nw = AppGraphAddNode(g, cands[c].Kind, cands[c].Name);
          const int nw_id = nw->Id;
          if (drop_scope < 0)
            AppGraphPlaceNode(g, nw, &drop_grid);
          const int comp = AppNodeFirstPortKind(nw, cands[c].Comp);
          if (comp > 0 && AppGraphTryConnect(g, AppGraphEditorState(g)->DropSrcAttr, comp))
          {
            AppInferStructFieldType(g, AppGraphEditorState(g)->DropSrcAttr, comp);   // drop direction is unknown -> try both orderings
            AppInferStructFieldType(g, comp, AppGraphEditorState(g)->DropSrcAttr);
          }
          if (drop_scope >= 0)
          {
            if (sp_kind == ImGuiAppPortKind_ChildIn)
            {
              // The wire carries containment under the drag SOURCE; split the altitudes by hand:
              // interior seat at the drop point, root seat beside the parent's root cluster.
              const ImVec2 pref = sowner_gp + ImVec2(280.0f, 0.0f);
              AppGraphPlaceNode(g, AppGraphFindNode(g, nw_id), &pref);
              AppNodeScopePosStore(g, nw_id, drop_grid);
            }
            else
            {
              AppScopeComposeNewNode(g, nw_id, &drop_grid);
            }
          }
          AppGraphEditorState(g)->DropSrcAttr = -1;
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::EndPopup();
    }

    // (F14 removed the floating canvas-corner rejection toast: transient feedback lives only in the
    // status-bar slot above, so the same notice never renders twice.)

    // A scope mutation THIS frame (Tab/Esc/double-click/breadcrumb/palette, all handled above): selection
    // does not survive scope transitions by design -- drop it before the sync below re-applies the survivor.
    // The top-of-frame latch covers changes made during the sync.
    if (g->ViewScope.Size * 100000 + (AppScopeCurrent(g) + 1) != g->_ScopeSig)
      ImGui::CanvasClearSelection(cv);

    // Cross-view selection sync. Order: dangle guard -> canvas read-back -> tree apply + reveal.
    if (selected_node_id != nullptr)
    {

      // 1) Dangle guard: a deleted / stripped / reloaded id must not highlight a ghost.
      if (*selected_node_id >= 0 && AppGraphFindNode(g, *selected_node_id) == nullptr)
        *selected_node_id = -1;

      // 2) Canvas -> tree read-back: a single canvas selection writes itself to *selected_node_id (closes the
      //    one-way gap). A multi-select leaves *selected_node_id unchanged (single-select model).
      bool canvas_originated = false;
      if (ImGui::CanvasNumSelectedNodes(cv) == 1)
      {
        int picked = -1;
        ImGui::CanvasGetSelectedNodes(cv, &picked, 1);
        if (picked != *selected_node_id) { *selected_node_id = picked; canvas_originated = true; }
      }

      // 3) Tree -> canvas apply + reveal: an external (tree) change outlines AND pans the node into view. Never
      //    pan on a canvas-originated change (don't yank the viewport on a click).
      if (*selected_node_id != AppGraphEditorState(g)->AppliedSel)
      {
        // Apply only to a node actually submitted this frame: a hidden live node (tree still lists it)
        // was evicted from the pool, so SelectNode/MoveToNode on it would assert.
        ImGuiAppNode* tn = (*selected_node_id >= 0) ? AppGraphFindNode(g, *selected_node_id) : nullptr;
        bool scope_revealing = false;

        // Tree-originated pick outside the current drill-down scope: the outliner is global, the canvas is
        // scoped -- so navigate the scope chain to the node's home composition. Leave AppGraphEditorState(g)->AppliedSel unlatched;
        // the select + reveal re-runs next frame once the node submits inside its scope.
        if (tn != nullptr && !canvas_originated && g->ViewScope.Size > 0 && !AppNodeInScope(g, *selected_node_id))
        {
          int chain[64];
          int cn = 0;
          for (int p = AppScopeParentOf(g, *selected_node_id); p >= 0 && cn < 64; p = AppScopeParentOf(g, p))
            chain[cn++] = p;
          g->ViewScope.clear();
          for (int i = cn - 1; i >= 0; i--)
            g->ViewScope.push_back(chain[i]);
          scope_revealing = true;   // suppress this frame's apply AND the AppGraphEditorState(g)->AppliedSel latch below
          tn = nullptr;
        }

        const bool submitted = tn != nullptr && !(!show_live && tn->IsLive) && !AppNodeHiddenByCollapse(g, *selected_node_id);
        if (submitted && !canvas_originated)
        {
          ImGui::CanvasSelectNode(cv, *selected_node_id, false);
          // Reveal with the MINIMAL pan: nudge the view just enough to bring the node inside a margin.
          // A node already in view must not move the camera at all.
          const ImVec2 p = ImGui::CanvasToScreen(cv, AppCanvasNodePos(g, *selected_node_id));
          const ImVec2 d = ImGui::CanvasNodeSize(cv, *selected_node_id) * AppCanvasScale(g);
          const float  margin = ImGui::GetFontSize() * 2.0f;
          const ImVec2 vmin(editor_min.x + margin, editor_min.y + margin);
          const ImVec2 vmax(editor_min.x + editor_size.x - margin, editor_min.y + editor_size.y - margin);
          ImVec2 delta(0.0f, 0.0f);
          if      (p.x < vmin.x)       delta.x = vmin.x - p.x;
          else if (p.x + d.x > vmax.x) delta.x = vmax.x - (p.x + d.x);
          if      (p.y < vmin.y)       delta.y = vmin.y - p.y;
          else if (p.y + d.y > vmax.y) delta.y = vmax.y - (p.y + d.y);
          if (delta.x != 0.0f || delta.y != 0.0f)
            ImGui::CanvasSetPan(cv, ImGui::CanvasGetPan(cv) + delta);
        }
        if (!scope_revealing)
          AppGraphEditorState(g)->AppliedSel = *selected_node_id;
      }

      // 4) Mirror the canvas multi-selection (Ctrl-click / A) into g->Selection, so the outliner highlight,
      //    F-fit, and Ctrl+C all see the whole set -- not just the single primary node. Only when the canvas
      //    actually has a selection, so a tree-driven multi-select survives an empty canvas.
      const int multi = ImGui::CanvasNumSelectedNodes(cv);
      if (multi > 0)
      {
        g->Selection.resize(multi);
        ImGui::CanvasGetSelectedNodes(cv, g->Selection.Data, multi);
      }
    }

    // Clipboard + undo/redo keys (canvas focused, not typing). Ctrl+C copies the selected nodes' subtrees,
    // Ctrl+V pastes them with fresh ids and a cascading offset. Ctrl+Z / Ctrl+(Shift+)Z / Ctrl+Y restore
    // snapshots -- and clear the canvas selection so it can't reference an id the snapshot dropped.
    if (!ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::GetIO().KeyCtrl)
    {
      if (ImGui::IsKeyPressed(ImGuiKey_C, false))
      {
        ImVector<int> roots;
        const int sel_count = ImGui::CanvasNumSelectedNodes(cv);
        if (sel_count > 0)
        {
          roots.resize(sel_count);
          ImGui::CanvasGetSelectedNodes(cv, roots.Data, sel_count);
        }
        else if (selected_node_id != nullptr && *selected_node_id >= 0)
        {
          roots.push_back(*selected_node_id);
        }
        if (roots.Size > 0)
          AppGraphCopySelection(g, roots);
      }
      if (ImGui::IsKeyPressed(ImGuiKey_V, false) && AppGraphClipboardHasData(g))
      {
        const int first = g->Nodes.Size;
        AppGraphPasteClipboard(g);
        if (!at_root)
          AppScopeComposeImported(g, first, nullptr);   // a paste while drilled composes into the scope
      }
      // Ctrl+D duplicates the selection in place (the palette's Edit: Duplicate, on the keyboard).
      if (ImGui::IsKeyPressed(ImGuiKey_D, false) && g->Selection.Size > 0)
      {
        ImVector<int> sel = g->Selection;   // copy: duplication mutates g->Nodes (and may grow Selection)
        for (int i = 0; i < sel.Size; i++)
          if (const ImGuiAppNode* sn = AppGraphFindNode(g, sel.Data[i]))
            AppGraphDuplicateNode(g, sn);
      }
    }

    // Undo/redo: capture this frame's settled mutations, then honor Ctrl+Z / Ctrl+(Shift+)Z / Ctrl+Y.
    AppGraphCheckpoint(g);
    if (!ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::GetIO().KeyCtrl)
    {
      const bool redo = ImGui::IsKeyPressed(ImGuiKey_Y, false) || (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false));
      const bool undo = !ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false);
      if (redo && AppGraphCanRedo(g))
      {
        AppGraphRedo(g);
        ImGui::CanvasClearSelection(cv);
      }
      else if (undo && AppGraphCanUndo(g))
      {
        AppGraphUndo(g);
        ImGui::CanvasClearSelection(cv);
      }
    }
  }

  void AppGraphSelectionBreadcrumb(const ImGuiAppGraph* g, int node_id, char* buf, int buf_size)
  {
    IM_ASSERT(g != nullptr && buf != nullptr && buf_size > 0);

    const ImGuiAppNode* n = node_id >= 0 ? AppGraphFindNodeConst(g, node_id) : nullptr;
    if (n == nullptr) { ImStrncpy(buf, "sel: -", (size_t)buf_size); return; }

    const char* tag = n->IsLive ? "live" : n->IsPromoted ? "promoted" : "design";
    const int parent = AppGraphParentOf(g, n->Id);
    const ImGuiAppNode* pn = parent >= 0 ? AppGraphFindNodeConst(g, parent) : nullptr;
    if (pn != nullptr)
      ImFormatString(buf, buf_size, "sel: %s > %s [%s]", pn->Draft.Name, n->Draft.Name, tag);
    else
      ImFormatString(buf, buf_size, "sel: %s [%s]", n->Draft.Name, tag);
  }

  static void AppValidatePushIssue(ImVector<ImGuiAppGraphIssue>* out, int node_id, int severity, const char* fmt, ...)
  {
    ImGuiAppGraphIssue issue;
    issue.NodeId = node_id;
    issue.Severity = severity;
    va_list args;
    va_start(args, fmt);
    ImFormatStringV(issue.Text, IM_ARRAYSIZE(issue.Text), fmt, args);
    va_end(args);
    out->push_back(issue);
  }

  // Check one field's type completeness, reporting against its owning node.
  static void AppValidateField(ImVector<ImGuiAppGraphIssue>* out, const ImGuiAppNode* owner, const ImGuiAppFieldDesc* f, const char* list_name)
  {
    if (f->Type == ImGuiAppFieldType_Struct && f->StructType[0] == 0)
      AppValidatePushIssue(out, owner->Id, 2, "%s.%s: struct field has no type set", owner->Draft.Name, f->Name);
    if (f->Type == ImGuiAppFieldType_String && f->ArraySize <= 0)
      AppValidatePushIssue(out, owner->Id, 1, "%s.%s: string field has no size", owner->Draft.Name, f->Name);
    IM_UNUSED(list_name);
  }

  void AppGraphValidate(const ImGuiAppGraph* g, ImVector<ImGuiAppGraphIssue>* out)
  {
    IM_ASSERT(g != nullptr && out != nullptr);

    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (n->IsLive)
        continue;

      // Core layers have fixed identities; a CUSTOM layer's name is its generated class name and must exist.
      if (n->Draft.Name[0] == 0 && (n->Kind != ImGuiAppNodeKind_Layer || n->LayerType == ImGuiAppLayerType_Custom))
        AppValidatePushIssue(out, n->Id, 1, "unnamed %s node", AppNodeKindName(n->Kind));

      // Duplicate codegen type name across Control / Struct nodes (would collide in the emitted C++).
      if ((n->Kind == ImGuiAppNodeKind_Control || n->Kind == ImGuiAppNodeKind_Struct) && n->Draft.Name[0])
      {
        char base[IM_LABEL_SIZE];
        AppNodeBaseName(n, base, IM_ARRAYSIZE(base));
        for (int j = 0; j < i; j++)
        {
          const ImGuiAppNode* m = &g->Nodes.Data[j];
          if (m->IsLive || (m->Kind != ImGuiAppNodeKind_Control && m->Kind != ImGuiAppNodeKind_Struct))
            continue;
          char mbase[IM_LABEL_SIZE];
          AppNodeBaseName(m, mbase, IM_ARRAYSIZE(mbase));
          if (strcmp(base, mbase) == 0)
          {
            AppValidatePushIssue(out, n->Id, 2, "duplicate type name '%s'", base);
            break;
          }
        }
      }

      // Every control is hosted: composed into a window or sidebar (no app-level controls).
      if (n->Kind == ImGuiAppNodeKind_Control && AppGraphParentOf(g, n->Id) < 0)
        AppValidatePushIssue(out, n->Id, 1, "control '%s' has no host window or sidebar", n->Draft.Name);

      if (n->Kind == ImGuiAppNodeKind_Struct)
      {
        if (AppGraphFieldNodeCount(g, n->Id, 0) == 0 && n->Draft.PersistFields.Size == 0)
          AppValidatePushIssue(out, n->Id, 1, "struct '%s' has no fields", n->Draft.Name);
        for (int f = 0; f < n->Draft.PersistFields.Size; f++)
          AppValidateField(out, n, &n->Draft.PersistFields.Data[f], "field");
      }
      else if (n->Kind == ImGuiAppNodeKind_Field)
      {
        for (int f = 0; f < n->Draft.PersistFields.Size; f++)
          AppValidateField(out, n, &n->Draft.PersistFields.Data[f], "field");
      }
      else if (n->Kind == ImGuiAppNodeKind_Control)
      {
        if (n->PersistStructId >= 0 && AppGraphFindNodeConst(g, n->PersistStructId) == nullptr)
          AppValidatePushIssue(out, n->Id, 2, "control '%s': exploded PersistData struct is missing", n->Draft.Name);
        if (n->TempStructId >= 0 && AppGraphFindNodeConst(g, n->TempStructId) == nullptr)
          AppValidatePushIssue(out, n->Id, 2, "control '%s': exploded TempData struct is missing", n->Draft.Name);
        for (int f = 0; f < n->Draft.PersistFields.Size; f++)
          AppValidateField(out, n, &n->Draft.PersistFields.Data[f], "persist");
        for (int f = 0; f < n->Draft.TempFields.Size; f++)
          AppValidateField(out, n, &n->Draft.TempFields.Data[f], "temp");

        // Authored events must resolve against the control's effective field lists and selected commands.
        for (int e = 0; e < n->Events.Size; e++)
        {
          const ImGuiAppEventDesc* ev = &n->Events.Data[e];
          if (ev->TempField[0] == 0)
            AppValidatePushIssue(out, n->Id, 2, "%s: event %d watches no TempData field", n->Draft.Name, e + 1);
          else if (AppNodeEffectiveFieldType(g, n, 1, ev->TempField) < 0)
            AppValidatePushIssue(out, n->Id, 2, "%s: event watches missing temp field '%s'", n->Draft.Name, ev->TempField);
          if (ev->Action == ImGuiAppEventAction_SetField)
          {
            if (ev->DstField[0] == 0)
              AppValidatePushIssue(out, n->Id, 1, "%s: event on '%s' has no destination field", n->Draft.Name, ev->TempField);
            else if (AppNodeEffectiveFieldType(g, n, 0, ev->DstField) < 0)
              AppValidatePushIssue(out, n->Id, 2, "%s: event writes missing persist field '%s'", n->Draft.Name, ev->DstField);
            char eerr[192];
            if (!AppEventExprCheck(g, n, ev, eerr, IM_ARRAYSIZE(eerr)))
              AppValidatePushIssue(out, n->Id, 2, "%s: event expr '%s': %s", n->Draft.Name, ev->Expr, eerr);
          }
          else if (ev->Action == ImGuiAppEventAction_EmitCommand)
          {
            if (ev->Command[0] == 0)
              AppValidatePushIssue(out, n->Id, 1, "%s: event on '%s' emits no command", n->Draft.Name, ev->TempField);
            else if (AppGraphFindCommandDefinition(g, ev->Command) == nullptr)
              AppValidatePushIssue(out, n->Id, 2, "%s: event command '%s' is not defined on CommandLayer", n->Draft.Name, ev->Command);
          }
        }
      }
      else if (n->Kind == ImGuiAppNodeKind_Window || n->Kind == ImGuiAppNodeKind_Sidebar)
      {
        if (!AppGraphHostsControl(g, n->Id))
          AppValidatePushIssue(out, n->Id, 1, "%s '%s' hosts no controls", AppNodeKindName(n->Kind), n->Draft.Name);
      }
    }

    // Whole-graph: a data-dependency cycle blocks codegen entirely.
    ImVector<int> order;
    char err[160] = "";
    if (!AppGraphTopoOrder(g, &order, err, IM_ARRAYSIZE(err)))
      AppValidatePushIssue(out, -1, 2, "dependency cycle: %s", err[0] ? err : "controls form a loop");

    //-------------------------------------------------------------------------
    // Structural invariants. CaptureAppGraphLinks/CanLink gate interactive edits, but load, paste, import and
    // undo construct links wholesale -- so the MODEL's well-formedness relation is re-checked here in full:
    //   (a) every edge is kind-well-formed (Data: DataOut->DataIn; Containment: ChildOut->ChildIn),
    //   (b) containment is a forest (<=1 parent per node, no ancestor cycles),
    //   (c) emitted data types form a functional dependency (one producer per type: app->Data is keyed by it),
    //   (d) the event system is confluent (no two writers of the same persist field, whose order would matter).
    //-------------------------------------------------------------------------

    // (a) Edge well-formedness.
    for (int li = 0; li < g->Links.Size; li++)
    {
      const ImGuiAppNodeLink* l = &g->Links.Data[li];
      ImGuiAppNode* oa = nullptr;
      ImGuiAppNode* ob = nullptr;
      const ImGuiAppNodePort* pa = AppGraphFindPort(const_cast<ImGuiAppGraph*>(g), l->StartAttr, &oa);
      const ImGuiAppNodePort* pb = AppGraphFindPort(const_cast<ImGuiAppGraph*>(g), l->EndAttr, &ob);
      if (pa == nullptr || pb == nullptr)
      {
        AppValidatePushIssue(out, -1, 2, "link %d references a missing port (dangling edge)", l->Id);
        continue;
      }
      if (oa != nullptr && ob != nullptr && (oa->IsLive || ob->IsLive))
        continue;   // live-mirror edges are rebuilt every frame by the mirror itself
      const bool ok = (l->Kind == ImGuiAppEdgeKind_Data        && pa->Kind == ImGuiAppPortKind_DataOut  && pb->Kind == ImGuiAppPortKind_DataIn)
                   || (l->Kind == ImGuiAppEdgeKind_Containment && pa->Kind == ImGuiAppPortKind_ChildOut && pb->Kind == ImGuiAppPortKind_ChildIn);
      if (!ok)
        AppValidatePushIssue(out, oa != nullptr ? oa->Id : -1, 2, "link %d violates port-kind pairing (%s edge)", l->Id,
                             l->Kind == ImGuiAppEdgeKind_Data ? "data" : "containment");
    }

    // (b) Containment forest: at most one parent, and the parent walk terminates (acyclic).
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (n->IsLive)
        continue;
      int parents = 0;
      for (int li = 0; li < g->Links.Size; li++)
        if (g->Links.Data[li].Kind == ImGuiAppEdgeKind_Containment && AppGraphPortOwnerId(g, g->Links.Data[li].StartAttr) == n->Id)
          parents++;
      if (parents > 1)
        AppValidatePushIssue(out, n->Id, 2, "'%s' has %d containment parents (composition must be a tree)", n->Draft.Name, parents);
      int walker = n->Id;
      for (int guard = 0; ; guard++)
      {
        walker = AppGraphParentOf(g, walker);
        if (walker < 0)
          break;
        if (walker == n->Id || guard >= g->Nodes.Size)
        {
          AppValidatePushIssue(out, n->Id, 2, "'%s' is inside a containment cycle", n->Draft.Name);
          break;
        }
      }
    }

    // (c) Functional dependency of emitted data types: app->Data keys one instance per type, so two authored
    // producers resolving to the SAME type name (drafted "<Name>Data", renamed struct, or builtin type) collide
    // at runtime even when their node names differ. (The name-based duplicate check above misses cross-kind
    // and builtin collisions.)
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* a = &g->Nodes.Data[i];
      if (a->IsLive || (a->Kind != ImGuiAppNodeKind_Control && a->Kind != ImGuiAppNodeKind_Struct))
        continue;
      char ta[IM_LABEL_SIZE];
      AppNodeDataTypeName(a, ta, IM_ARRAYSIZE(ta));
      if (ta[0] == 0)
        continue;
      for (int j = 0; j < i; j++)
      {
        const ImGuiAppNode* b = &g->Nodes.Data[j];
        if (b->IsLive || (b->Kind != ImGuiAppNodeKind_Control && b->Kind != ImGuiAppNodeKind_Struct))
          continue;
        char tb[IM_LABEL_SIZE];
        AppNodeDataTypeName(b, tb, IM_ARRAYSIZE(tb));
        if (strcmp(ta, tb) == 0 && strcmp(a->Draft.Name, b->Draft.Name) != 0)
        {
          AppValidatePushIssue(out, a->Id, 2, "'%s' and '%s' both emit data type '%s' (one producer per type)", a->Draft.Name, b->Draft.Name, ta);
          break;
        }
      }
    }

    // (d) Event confluence: OnUpdate emits writers in authored order, so two events (or an event and a data-edge
    // binding) targeting the same persist field make the result order-dependent -- flag the non-confluence.
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (n->IsLive || n->Kind != ImGuiAppNodeKind_Control)
        continue;
      for (int e = 0; e < n->Events.Size; e++)
      {
        const ImGuiAppEventDesc* ev = &n->Events.Data[e];
        if (ev->Action != ImGuiAppEventAction_SetField || ev->DstField[0] == 0)
          continue;
        for (int f = 0; f < e; f++)
        {
          const ImGuiAppEventDesc* prior = &n->Events.Data[f];
          if (prior->Action == ImGuiAppEventAction_SetField && strcmp(prior->DstField, ev->DstField) == 0)
          {
            AppValidatePushIssue(out, n->Id, 1, "%s: events %d and %d both write '%s' -- result depends on their order", n->Draft.Name, f + 1, e + 1, ev->DstField);
            break;
          }
        }
        for (int bi = 0; bi < g->Bindings.Size; bi++)
        {
          if (strcmp(g->Bindings.Data[bi].DstField, ev->DstField) != 0)
            continue;
          // The binding must belong to one of THIS control's incoming data edges to conflict.
          for (int li = 0; li < g->Links.Size; li++)
            if (g->Links.Data[li].Id == g->Bindings.Data[bi].LinkId && g->Links.Data[li].Kind == ImGuiAppEdgeKind_Data
                && AppGraphPortOwnerId(g, g->Links.Data[li].EndAttr) == n->Id)
            {
              AppValidatePushIssue(out, n->Id, 1, "%s: '%s' is written by both a data binding and an event -- the event wins within the frame", n->Draft.Name, ev->DstField);
              break;
            }
        }
      }
    }

    // (e) Data-dependency adherence: a binding maps a producer field (Src) to a consumer field (Dst); both
    // must be DECLARED. An undeclared field is a dead reference codegen drops with a // WARNING, so surface
    // it here. Builtin/live producers and consumers carry their fields in compiled C++, not the draft -- skip.
    for (int bi = 0; bi < g->Bindings.Size; bi++)
    {
      const ImGuiAppFieldBinding* b = &g->Bindings.Data[bi];
      const ImGuiAppNodeLink* link = nullptr;
      for (int li = 0; li < g->Links.Size; li++)
        if (g->Links.Data[li].Id == b->LinkId) { link = &g->Links.Data[li]; break; }
      if (link == nullptr || link->Kind != ImGuiAppEdgeKind_Data)
        continue;   // dangling bindings are swept on load; only data edges carry a field mapping
      const int cons_id = AppGraphPortOwnerId(g, link->EndAttr);
      const int prod_id = AppGraphPortOwnerId(g, link->StartAttr);
      const ImGuiAppNode* cons = AppGraphFindNodeConst(g, cons_id);
      const ImGuiAppNode* prod = AppGraphFindNodeConst(g, prod_id);
      if (cons != nullptr && !cons->IsLive && !cons->IsBuiltin && b->DstField[0]
          && AppNodeEffectiveFieldType(g, cons, 0, b->DstField) < 0)
        AppValidatePushIssue(out, cons_id, 2, "%s: binding writes undeclared field '%s'", cons->Draft.Name, b->DstField);
      if (prod != nullptr && !prod->IsLive && !prod->IsBuiltin && b->SrcField[0]
          && (prod->Kind == ImGuiAppNodeKind_Control || prod->Kind == ImGuiAppNodeKind_Struct)
          && AppNodeEffectiveFieldType(g, prod, 0, b->SrcField) < 0)
        AppValidatePushIssue(out, prod_id, 2, "%s: binding reads undeclared field '%s'", prod->Draft.Name, b->SrcField);
    }
  }

  // Render one field list as live mock widgets. Numeric state is kept in the window's ImGuiStorage keyed by the
  // field id, so scrubbed values persist across frames; strings show a read-only box (mock layout, not wired).
  static void AppMockRenderFields(const ImVector<ImGuiAppFieldDesc>& fields)
  {
    ImGuiStorage* st = ImGui::GetStateStorage();
    for (int i = 0; i < fields.Size; i++)
    {
      const ImGuiAppFieldDesc* f = &fields.Data[i];
      ImGui::PushID(i);
      const ImGuiID id = ImGui::GetID(f->Name);
      switch (f->Type)
      {
      case ImGuiAppFieldType_Bool:
      {
        bool v = st->GetBool(id, false);
        if (ImGui::Checkbox(f->Name, &v)) st->SetBool(id, v);
        break;
      }
      case ImGuiAppFieldType_Int:
      {
        int* p = st->GetIntRef(id, 0);
        ImGui::DragInt(f->Name, p);
        break;
      }
      case ImGuiAppFieldType_Float:
      case ImGuiAppFieldType_Double:
      {
        float* p = st->GetFloatRef(id, 0.0f);
        ImGui::DragFloat(f->Name, p, 0.01f);
        break;
      }
      case ImGuiAppFieldType_Vec2:
      {
        float v[2] = { st->GetFloat(id, 0.0f), st->GetFloat(id + 1, 0.0f) };
        if (ImGui::DragFloat2(f->Name, v, 0.01f)) { st->SetFloat(id, v[0]); st->SetFloat(id + 1, v[1]); }
        break;
      }
      case ImGuiAppFieldType_Vec4:
      {
        float v[4] = { st->GetFloat(id, 0.0f), st->GetFloat(id + 1, 0.0f), st->GetFloat(id + 2, 0.0f), st->GetFloat(id + 3, 0.0f) };
        if (ImGui::DragFloat4(f->Name, v, 0.01f)) { for (int k = 0; k < 4; k++) st->SetFloat(id + k, v[k]); }
        break;
      }
      case ImGuiAppFieldType_String:
      {
        char dummy[1] = { 0 };
        ImGui::InputTextWithHint(f->Name, "(text)", dummy, 1, ImGuiInputTextFlags_ReadOnly);
        break;
      }
      default:
        ImGui::LabelText(f->Name, "%s", f->StructType[0] ? f->StructType : "struct");
        break;
      }
      ImGui::PopID();
    }
  }

  void AppGraphRenderMockPanel(ImGuiAppGraph* g, int node_id, ImGuiApp* live_app)
  {
    IM_ASSERT(g != nullptr);

    const ImGuiAppNode* n = node_id >= 0 ? AppGraphFindNodeConst(g, node_id) : nullptr;
    if (n == nullptr)
    {
      ImGui::TextDisabled("Select a Control to preview its UI.");
      return;
    }
    if (n->Kind != ImGuiAppNodeKind_Control)
    {
      ImGui::TextDisabled("Preview is for Control nodes (%s selected).", AppNodeKindName(n->Kind));
      return;
    }

    // Live node: no mock and no repeat -- the control is RUNNING, and its members with current
    // values are already the Data (live) / Temp (live) sections of the node inspector.
    if (n->IsLive)
    {
      IM_UNUSED(live_app);
      ImGui::TextDisabled("live: %s is running -- current values are in Data (live) / Temp (live)",
                          n->DataTypeName[0] ? n->DataTypeName : n->Draft.Name);
      return;
    }

    ImGui::TextDisabled("mock: %s", n->Draft.Name[0] ? n->Draft.Name : "(unnamed)");
    ImGui::Separator();

    ImVector<ImGuiAppFieldDesc> persist;
    ImVector<ImGuiAppFieldDesc> temp;
    AppNodeEffectiveFields(g, n, 0, &persist);
    AppNodeEffectiveFields(g, n, 1, &temp);

    if (persist.Size == 0 && temp.Size == 0)
    {
      ImGui::TextDisabled("(no fields to preview -- add Persist/Temp fields)");
      return;
    }
    if (persist.Size > 0)
      AppMockRenderFields(persist);
    if (temp.Size > 0)
    {
      ImGui::SeparatorText("temp");
      AppMockRenderFields(temp);
    }
    if (n->Events.Size > 0)
    {
      ImGui::SeparatorText("events");
      for (int e = 0; e < n->Events.Size; e++)
      {
        const ImGuiAppEventDesc* ev = &n->Events.Data[e];
        if (ev->Action == ImGuiAppEventAction_EmitCommand)
          ImGui::TextDisabled("when %s %s  ->  emit %s", ev->TempField[0] ? ev->TempField : "?",
                              AppEventEdgeName(ev->Edge), ev->Command[0] ? ev->Command : "?");
        else
          ImGui::TextDisabled("when %s %s  ->  data->%s = %s", ev->TempField[0] ? ev->TempField : "?",
                              AppEventEdgeName(ev->Edge), ev->DstField[0] ? ev->DstField : "?",
                              ev->Expr[0] ? ev->Expr : "temp value");
      }
    }
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Topological order + whole-graph codegen
  //-----------------------------------------------------------------------------

  static void AppNodeBaseName(const ImGuiAppNode* n, char* out, size_t out_size)
  {
    if (n->IsBuiltin && n->TypeName[0])
      ImStrncpy(out, n->TypeName, out_size);
    else
      AppSanitizeIdentifier(out, out_size, n->Draft.Name);
  }

  // Persist-field type of a named field in a draft, or -1 if absent. Gates binding emission: a binding
  // whose endpoints disagree on type is dropped.
  static int AppDraftFieldType(const ImGuiAppNodeDraft* d, const char* name)
  {
    for (int i = 0; i < d->PersistFields.Size; i++)
      if (strcmp(d->PersistFields.Data[i].Name, name) == 0)
        return (int)d->PersistFields.Data[i].Type;
    return -1;
  }

  // Emit the OnUpdate guard for one authored event.
  static void AppEmitEventGuard(ImGuiTextBuffer* out, const ImGuiAppEventDesc* ev, bool is_bool, const char* fld)
  {
    switch (ev->Edge)
    {
    case ImGuiAppEventEdge_Rising:
      out->appendf("    if (temp_data->%s && !last_temp_data->%s)\n", fld, fld);
      break;
    case ImGuiAppEventEdge_Falling:
      out->appendf("    if (!temp_data->%s && last_temp_data->%s)\n", fld, fld);
      break;
    case ImGuiAppEventEdge_Changed:
      if (is_bool)
        out->appendf("    if (temp_data->%s ^ last_temp_data->%s)\n", fld, fld);
      else
        out->appendf("    if (temp_data->%s != last_temp_data->%s)\n", fld, fld);
      break;
    case ImGuiAppEventEdge_Active:
    default:
      out->appendf("    if (temp_data->%s)\n", fld);
      break;
    }
  }

  static void AppNodeDataTypeName(const ImGuiAppNode* n, char* out, size_t out_size)
  {
    if (n->Kind == ImGuiAppNodeKind_Struct)
    {
      AppNodeBaseName(n, out, (int)out_size);   // a struct's type IS its name (no "Data" suffix)
      return;
    }
    if (n->DataTypeName[0])
      ImStrncpy(out, n->DataTypeName, out_size);
    else
    {
      char base[IM_LABEL_SIZE];
      AppNodeBaseName(n, base, IM_ARRAYSIZE(base));
      ImFormatString(out, (int)out_size, "%sData", base);
    }
  }

  // "RandomTime" -> "random_time" (a readable dependency parameter name).
  static void AppToSnake(char* dst, size_t dst_size, const char* src)
  {
    char id[IM_LABEL_SIZE];
    AppSanitizeIdentifier(id, IM_ARRAYSIZE(id), src);
    size_t n = 0;
    for (const char* s = id; *s != 0 && n + 1 < dst_size; s++)
    {
      const char c = *s;
      if (c >= 'A' && c <= 'Z')
      {
        if (s != id && n + 2 < dst_size) dst[n++] = '_';
        dst[n++] = (char)(c - 'A' + 'a');
      }
      else
        dst[n++] = c;
    }
    dst[n] = 0;
  }

  static void AppCommandEnumValue(const ImGuiAppCommandDesc* cmd, char* out, size_t out_size)
  {
    char base[IM_LABEL_SIZE];
    AppSanitizeIdentifier(base, IM_ARRAYSIZE(base), cmd->Name);
    ImFormatString(out, (int)out_size, "AppCommand_%s", base);
  }

  // Just the AppCommand enum folded from every CommandLayer's command list (no app shell).
  static void AppEmitCommandEnum(const ImGuiAppGraph* g, ImGuiTextBuffer* out)
  {
    if (AppGraphCommandDefinitionCount(g) == 0)
      return;

    // Align the '=' column: width = the longest enumerator name carrying an initializer.
    int w = (int)strlen("AppCommand_None");
    if ((int)strlen("AppCommand_Shutdown") > w) w = (int)strlen("AppCommand_Shutdown");
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (!AppNodeIsCommandLayer(n)) continue;
      for (int c = 0; c < n->Commands.Size; c++)
      {
        char enum_value[IM_LABEL_SIZE];
        AppCommandEnumValue(&n->Commands.Data[c], enum_value, IM_ARRAYSIZE(enum_value));
        if ((int)strlen(enum_value) > w) w = (int)strlen(enum_value);
      }
    }

    out->appendf("enum AppCommand\n{\n");
    out->appendf("  %-*s = ImGuiAppCommand_None,\n", w, "AppCommand_None");
    out->appendf("  %-*s = ImGuiAppCommand_Shutdown,\n", w, "AppCommand_Shutdown");
    int offset = 0;
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (!AppNodeIsCommandLayer(n)) continue;
      for (int c = 0; c < n->Commands.Size; c++)
      {
        char enum_value[IM_LABEL_SIZE];
        AppCommandEnumValue(&n->Commands.Data[c], enum_value, IM_ARRAYSIZE(enum_value));
        out->appendf("  %-*s = ImGuiAppCommand_COUNT + %d,\n", w, enum_value, offset++);
      }
    }
    out->appendf("  AppCommand_COUNT\n};\n\n");
  }

  static void AppEmitCommandEnumAndApp(const ImGuiAppGraph* g, ImGuiTextBuffer* out)
  {
    if (AppGraphCommandDefinitionCount(g) == 0)
      return;

    AppEmitCommandEnum(g, out);

    out->appendf("struct ClientApp : ImGuiApp\n{\n");
    out->appendf("  virtual void OnExecuteCommand(ImGuiAppCommand cmd) override\n  {\n");
    out->appendf("    switch ((AppCommand)cmd)\n    {\n");
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (!AppNodeIsCommandLayer(n)) continue;
      for (int c = 0; c < n->Commands.Size; c++)
      {
        char enum_value[IM_LABEL_SIZE];
        AppCommandEnumValue(&n->Commands.Data[c], enum_value, IM_ARRAYSIZE(enum_value));
        out->appendf("    case %s:\n      // TODO: handle %s\n      break;\n", enum_value, n->Commands.Data[c].Name);
      }
    }
    out->appendf("    default:\n      ImGuiApp::OnExecuteCommand(cmd);\n      break;\n");
    out->appendf("    }\n  }\n};\n\n");
  }

  static const ImGuiAppNode* AppGraphFindNodeConst(const ImGuiAppGraph* g, int node_id)
  {
    for (int i = 0; i < g->Nodes.Size; i++)
      if (g->Nodes.Data[i].Id == node_id) return &g->Nodes.Data[i];
    return nullptr;
  }

  bool AppGraphTopoOrder(const ImGuiAppGraph* g, ImVector<int>* out_control_ids, char* err, int err_size, bool include_live, ImVector<int>* out_cycle)
  {
    IM_ASSERT(g != nullptr && out_control_ids != nullptr);
    out_control_ids->clear();
    if (out_cycle != nullptr) out_cycle->clear();
    if (err && err_size > 0) err[0] = 0;

    // Collect control node ids (stable order = node order, for deterministic output). Validation/health
    // pass include_live false (authored domain); codegen passes true (the full mirrored composition --
    // live nodes append after authored ones, so authored controls keep their relative order).
    ImVector<int> ctrl;
    for (int i = 0; i < g->Nodes.Size; i++)
      if (g->Nodes.Data[i].Kind == ImGuiAppNodeKind_Control && (include_live || !g->Nodes.Data[i].IsLive))
        ctrl.push_back(g->Nodes.Data[i].Id);

    // In-degree = number of incoming data edges (producer -> this consumer).
    ImVector<int> indeg;
    indeg.resize(ctrl.Size);
    for (int i = 0; i < ctrl.Size; i++) indeg.Data[i] = 0;

    auto ctrl_index = [&](int node_id) -> int
    {
      for (int i = 0; i < ctrl.Size; i++) if (ctrl.Data[i] == node_id) return i;
      return -1;
    };

    for (int li = 0; li < g->Links.Size; li++)
    {
      if (g->Links.Data[li].Kind != ImGuiAppEdgeKind_Data) continue;
      // Only control -> control edges order controls. Struct / Field / persist-temp-tie producers are emitted
      // before controls (separately), so they must NOT add in-degree (else the control never reaches zero).
      const ImGuiAppNode* prod = AppGraphFindNodeConst(g, AppGraphPortOwnerId(g, g->Links.Data[li].StartAttr));
      if (prod == nullptr || prod->Kind != ImGuiAppNodeKind_Control || (!include_live && prod->IsLive)) continue;
      const int consumer = AppGraphPortOwnerId(g, g->Links.Data[li].EndAttr);
      const int ci = ctrl_index(consumer);
      if (ci >= 0) indeg.Data[ci]++;
    }

    // Kahn: repeatedly take a zero-in-degree control in stable order.
    ImVector<bool> done;
    done.resize(ctrl.Size);
    for (int i = 0; i < ctrl.Size; i++) done.Data[i] = false;

    for (int produced = 0; produced < ctrl.Size; produced++)
    {
      int pick = -1;
      for (int i = 0; i < ctrl.Size; i++)
        if (!done.Data[i] && indeg.Data[i] == 0) { pick = i; break; }

      if (pick < 0)
      {
        // Remaining nodes form a cycle (plus anything it blocks); name one for the message and, when
        // asked, hand the caller the whole unscheduled set for the Select verb.
        const char* who = "control";
        bool named = false;
        for (int i = 0; i < ctrl.Size; i++)
          if (!done.Data[i])
          {
            if (!named) { const ImGuiAppNode* nn = AppGraphFindNodeConst(g, ctrl.Data[i]); if (nn) who = nn->Draft.Name; named = true; }
            if (out_cycle != nullptr) out_cycle->push_back(ctrl.Data[i]);
          }
        char msg[160];
        ImFormatString(msg, IM_ARRAYSIZE(msg), "dependency cycle at %s", who);
        AppSetErr(err, err_size, msg);
        out_control_ids->clear();
        return false;
      }

      done.Data[pick] = true;
      out_control_ids->push_back(ctrl.Data[pick]);

      // Decrement consumers fed by the picked producer.
      for (int li = 0; li < g->Links.Size; li++)
      {
        if (g->Links.Data[li].Kind != ImGuiAppEdgeKind_Data) continue;
        if (AppGraphPortOwnerId(g, g->Links.Data[li].StartAttr) != ctrl.Data[pick]) continue;
        const int consumer = AppGraphPortOwnerId(g, g->Links.Data[li].EndAttr);
        const int ci = ctrl_index(consumer);
        if (ci >= 0 && !done.Data[ci] && indeg.Data[ci] > 0) indeg.Data[ci]--;
      }
    }
    return true;
  }

  int AppGraphDependencyCycle(const ImGuiAppGraph* g, ImVector<int>* out_nodes, char* out_name, int name_size)
  {
    IM_ASSERT(g != nullptr);
    if (out_nodes != nullptr) out_nodes->clear();
    if (out_name != nullptr && name_size > 0) out_name[0] = 0;

    ImVector<int> order;
    ImVector<int> cycle;
    char err[160] = "";
    if (AppGraphTopoOrder(g, &order, err, IM_ARRAYSIZE(err), false, &cycle))
      return 0;   // acyclic

    if (out_name != nullptr && name_size > 0)
    {
      const ImGuiAppNode* nn = cycle.Size > 0 ? AppGraphFindNodeConst(g, cycle.Data[0]) : nullptr;
      ImStrncpy(out_name, nn != nullptr ? nn->Draft.Name : "control", name_size);
    }
    if (out_nodes != nullptr)
      *out_nodes = cycle;
    return cycle.Size;
  }

  // Distinct producer node ids feeding a consumer control via data edges, in node order (deterministic).
  static void AppGraphConsumerDeps(const ImGuiAppGraph* g, int consumer_node_id, ImVector<int>* out_producers)
  {
    out_producers->clear();
    const ImGuiAppNode* consumer = AppGraphFindNodeConst(g, consumer_node_id);
    const int own_persist = consumer != nullptr ? consumer->PersistStructId : -1;   // the control's OWN exploded
    const int own_temp    = consumer != nullptr ? consumer->TempStructId    : -1;   // data structs are not deps
    for (int i = 0; i < g->Nodes.Size; i++)   // iterate nodes for stable order
    {
      const int producer_id = g->Nodes.Data[i].Id;
      for (int li = 0; li < g->Links.Size; li++)
      {
        if (g->Links.Data[li].Kind != ImGuiAppEdgeKind_Data) continue;
        if (AppGraphPortOwnerId(g, g->Links.Data[li].EndAttr) != consumer_node_id) continue;
        if (AppGraphPortOwnerId(g, g->Links.Data[li].StartAttr) != producer_id) continue;
        // A Field producer contributes its PARENT owner (Struct/Control) as the dependency (many fields -> one dep).
        int dep_id = producer_id;
        const ImGuiAppNode* pn = AppGraphFindNodeConst(g, producer_id);
        if (pn != nullptr && pn->Kind == ImGuiAppNodeKind_Field)
        {
          const int sid = AppGraphParentOf(g, producer_id);
          if (sid >= 0) dep_id = sid;
        }
        if (dep_id == consumer_node_id || dep_id == own_persist || dep_id == own_temp)
          continue;   // self, or the control's own PersistData/TempData struct -- not a dependency
        bool dup = false;
        for (int d = 0; d < out_producers->Size; d++) if (out_producers->Data[d] == dep_id) { dup = true; break; }
        if (!dup) out_producers->push_back(dep_id);
      }
    }
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Event expression checking (AppEventExprCheck)
  //-----------------------------------------------------------------------------
  // Expr is emitted verbatim into the generated OnUpdate, so anything rejected here might still be legal
  // C++ -- but then the event is no longer analyzable data. The grammar is deliberately tiny; growing it
  // is fine as long as every construct stays type-checkable against the authored field lists.

  enum { AppExprType_Unknown = -2 };   // a builtin dep's field: lives in compiled C++, compatible with everything

  struct AppExprCtx
  {
    const ImGuiAppGraph* Graph;
    const ImGuiAppNode*  Node;
    const char*          Cur;
    bool                 Failed;
    char                 Err[192];
    char                 StructType[IM_LABEL_SIZE]; // set when the last primary was a whole struct-field ref
  };

  static int AppExprFail(AppExprCtx* c, const char* fmt, ...)
  {
    if (!c->Failed)
    {
      va_list args;
      va_start(args, fmt);
      ImFormatStringV(c->Err, IM_ARRAYSIZE(c->Err), fmt, args);
      va_end(args);
      c->Failed = true;
    }
    return AppExprType_Unknown;
  }

  static const char* AppExprTypeName(int t)
  {
    return (t >= 0 && t < ImGuiAppFieldType_COUNT) ? AppFieldTypeName((ImGuiAppFieldType)t) : "?";
  }

  static void AppExprSkipBlanks(AppExprCtx* c) { while (*c->Cur == ' ' || *c->Cur == '\t') c->Cur++; }

  // Consume `op` if it is next. Longer operators must be tried first at each level; '-' refuses '->' and
  // '!' refuses '!=' so unary parsing cannot eat half of a two-char token.
  static bool AppExprAccept(AppExprCtx* c, const char* op)
  {
    AppExprSkipBlanks(c);
    const size_t len = strlen(op);
    if (strncmp(c->Cur, op, len) != 0) return false;
    if (len == 1 && op[0] == '-' && c->Cur[1] == '>') return false;
    if (len == 1 && op[0] == '!' && c->Cur[1] == '=') return false;
    if (len == 1 && (op[0] == '<' || op[0] == '>') && c->Cur[1] == '=') return false;
    c->Cur += len;
    return true;
  }

  static bool AppExprIdent(AppExprCtx* c, char* out, int out_size)
  {
    AppExprSkipBlanks(c);
    const char* s = c->Cur;
    if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || *s == '_')) return false;
    int n = 0;
    while ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_')
    {
      if (n < out_size - 1) out[n++] = *s;
      s++;
    }
    out[n] = 0;
    c->Cur = s;
    return true;
  }

  static bool AppExprIsNumeric(int t) { return t == ImGuiAppFieldType_Float || t == ImGuiAppFieldType_Int || t == ImGuiAppFieldType_Double || t == AppExprType_Unknown; }
  static bool AppExprIsBool(int t)    { return t == ImGuiAppFieldType_Bool || t == AppExprType_Unknown; }
  static bool AppExprIsInt(int t)     { return t == ImGuiAppFieldType_Int || t == AppExprType_Unknown; }

  static int AppExprPromote(int a, int b)
  {
    if (a == AppExprType_Unknown || b == AppExprType_Unknown) return AppExprType_Unknown;
    if (a == ImGuiAppFieldType_Double || b == ImGuiAppFieldType_Double) return ImGuiAppFieldType_Double;
    if (a == ImGuiAppFieldType_Float || b == ImGuiAppFieldType_Float) return ImGuiAppFieldType_Float;
    return ImGuiAppFieldType_Int;
  }

  // `ident` in owner's effective (list) fields, matched by the sanitized name codegen emits. Returns the
  // field type (Struct fields also copy out their StructType) or -1.
  static int AppExprLookupField(const ImGuiAppGraph* g, const ImGuiAppNode* owner, int list, const char* ident, char* struct_type, int struct_type_size)
  {
    ImVector<ImGuiAppFieldDesc> fields;
    AppNodeEffectiveFields(g, owner, list, &fields);
    for (int i = 0; i < fields.Size; i++)
    {
      char id[IM_LABEL_SIZE];
      AppSanitizeIdentifier(id, IM_ARRAYSIZE(id), fields.Data[i].Name);
      if (strcmp(id, ident) == 0)
      {
        if (struct_type != nullptr)
          ImStrncpy(struct_type, fields.Data[i].StructType, struct_type_size);
        return (int)fields.Data[i].Type;
      }
    }
    return -1;
  }

  // Design Struct node whose (sanitized) name is `type_name`, or null.
  static const ImGuiAppNode* AppExprFindStructNode(const ImGuiAppGraph* g, const char* type_name)
  {
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* sn = &g->Nodes.Data[i];
      if (sn->IsLive || sn->Kind != ImGuiAppNodeKind_Struct) continue;
      char id[IM_LABEL_SIZE];
      AppSanitizeIdentifier(id, IM_ARRAYSIZE(id), sn->Draft.Name);
      if (strcmp(sn->Draft.Name, type_name) == 0 || strcmp(id, type_name) == 0)
        return sn;
    }
    return nullptr;
  }

  static int AppExprOr(AppExprCtx* c);   // fwd (parens recurse to the top)

  static int AppExprPrimary(AppExprCtx* c)
  {
    AppExprSkipBlanks(c);
    c->StructType[0] = 0;

    if (AppExprAccept(c, "("))
    {
      const int t = AppExprOr(c);
      if (c->Failed) return t;
      if (!AppExprAccept(c, ")")) return AppExprFail(c, "missing ')'");
      c->StructType[0] = 0;
      return t;
    }

    // Number literal: digits [. digits] [f]. No suffix and no dot -> int.
    if ((c->Cur[0] >= '0' && c->Cur[0] <= '9') || (c->Cur[0] == '.' && c->Cur[1] >= '0' && c->Cur[1] <= '9'))
    {
      bool is_float = false;
      while (*c->Cur >= '0' && *c->Cur <= '9') c->Cur++;
      if (*c->Cur == '.') { is_float = true; c->Cur++; while (*c->Cur >= '0' && *c->Cur <= '9') c->Cur++; }
      if (*c->Cur == 'f' || *c->Cur == 'F') { is_float = true; c->Cur++; }
      return is_float ? ImGuiAppFieldType_Float : ImGuiAppFieldType_Int;
    }

    char ident[IM_LABEL_SIZE];
    if (!AppExprIdent(c, ident, IM_ARRAYSIZE(ident)))
      return AppExprFail(c, "expected a value at '%.12s'", c->Cur[0] ? c->Cur : "<end>");
    if (strcmp(ident, "true") == 0 || strcmp(ident, "false") == 0)
      return ImGuiAppFieldType_Bool;

    // Root -> field. Roots are the exact parameter names the generated OnUpdate receives.
    const ImGuiAppNode* owner = nullptr;
    int list = 0;
    bool unverifiable = false;
    if (strcmp(ident, "temp_data") == 0 || strcmp(ident, "last_temp_data") == 0) { owner = c->Node; list = 1; }
    else if (strcmp(ident, "data") == 0) { owner = c->Node; list = 0; }
    else
    {
      ImVector<int> deps;
      AppGraphConsumerDeps(c->Graph, c->Node->Id, &deps);
      for (int d = 0; d < deps.Size && owner == nullptr; d++)
      {
        const ImGuiAppNode* dn = AppGraphFindNodeConst(c->Graph, deps.Data[d]);
        if (dn == nullptr) continue;
        char param[IM_LABEL_SIZE];
        AppToSnake(param, IM_ARRAYSIZE(param), dn->Draft.Name);
        if (strcmp(param, ident) == 0) { owner = dn; list = 0; unverifiable = dn->IsBuiltin; }
      }
      if (owner == nullptr)
        return AppExprFail(c, "unknown name '%s' (temp_data / last_temp_data / data / a dep param)", ident);
    }
    if (!AppExprAccept(c, "->"))
      return AppExprFail(c, "'%s' needs '-><field>'", ident);
    char field[IM_LABEL_SIZE];
    if (!AppExprIdent(c, field, IM_ARRAYSIZE(field)))
      return AppExprFail(c, "expected a field name after '%s->'", ident);

    char stype[IM_LABEL_SIZE];
    stype[0] = 0;
    int t = AppExprLookupField(c->Graph, owner, list, field, stype, IM_ARRAYSIZE(stype));
    if (t < 0)
    {
      if (!unverifiable)
        return AppExprFail(c, "'%s' has no field '%s'", ident, field);
      t = AppExprType_Unknown;
    }

    // Struct member chain: one '.' hop per nested level, each checked against that struct's field list.
    // Unknown (builtin) chains are consumed unchecked.
    for (;;)
    {
      AppExprSkipBlanks(c);
      if (*c->Cur != '.' || (t != ImGuiAppFieldType_Struct && t != AppExprType_Unknown))
        break;
      c->Cur++;
      char member[IM_LABEL_SIZE];
      if (!AppExprIdent(c, member, IM_ARRAYSIZE(member)))
        return AppExprFail(c, "expected a member after '.'");
      if (t == AppExprType_Unknown)
        continue;
      const ImGuiAppNode* sn = AppExprFindStructNode(c->Graph, stype);
      if (sn == nullptr) { t = AppExprType_Unknown; continue; }   // missing struct type is its own validate issue
      char inner[IM_LABEL_SIZE];
      inner[0] = 0;
      t = AppExprLookupField(c->Graph, sn, 0, member, inner, IM_ARRAYSIZE(inner));
      if (t < 0)
        return AppExprFail(c, "struct '%s' has no field '%s'", stype, member);
      ImStrncpy(stype, inner, IM_ARRAYSIZE(stype));
    }
    if (t == ImGuiAppFieldType_Struct)
      ImStrncpy(c->StructType, stype, IM_ARRAYSIZE(c->StructType));
    return t;
  }

  static int AppExprUnary(AppExprCtx* c)
  {
    if (AppExprAccept(c, "!"))
    {
      const int t = AppExprUnary(c);
      if (c->Failed) return t;
      c->StructType[0] = 0;
      if (!AppExprIsBool(t)) return AppExprFail(c, "'!' needs a bool (got %s)", AppExprTypeName(t));
      return ImGuiAppFieldType_Bool;
    }
    if (AppExprAccept(c, "-") || AppExprAccept(c, "+"))
    {
      const int t = AppExprUnary(c);
      if (c->Failed) return t;
      c->StructType[0] = 0;
      if (!AppExprIsNumeric(t)) return AppExprFail(c, "unary '-'/'+' needs a number (got %s)", AppExprTypeName(t));
      return t;
    }
    return AppExprPrimary(c);
  }

  static int AppExprMul(AppExprCtx* c)
  {
    int t = AppExprUnary(c);
    while (!c->Failed)
    {
      char op;
      if      (AppExprAccept(c, "*")) op = '*';
      else if (AppExprAccept(c, "/")) op = '/';
      else if (AppExprAccept(c, "%")) op = '%';
      else break;
      const int r = AppExprUnary(c);
      if (c->Failed) return r;
      c->StructType[0] = 0;
      if (op == '%')
      {
        if (!AppExprIsInt(t) || !AppExprIsInt(r))
          return AppExprFail(c, "'%%' needs two ints (got %s and %s)", AppExprTypeName(t), AppExprTypeName(r));
        t = (t == AppExprType_Unknown && r == AppExprType_Unknown) ? AppExprType_Unknown : ImGuiAppFieldType_Int;
      }
      else
      {
        if (!AppExprIsNumeric(t) || !AppExprIsNumeric(r))
          return AppExprFail(c, "'%c' needs numbers (got %s and %s)", op, AppExprTypeName(t), AppExprTypeName(r));
        t = AppExprPromote(t, r);
      }
    }
    return t;
  }

  static int AppExprAdd(AppExprCtx* c)
  {
    int t = AppExprMul(c);
    while (!c->Failed)
    {
      char op;
      if      (AppExprAccept(c, "+")) op = '+';
      else if (AppExprAccept(c, "-")) op = '-';
      else break;
      const int r = AppExprMul(c);
      if (c->Failed) return r;
      c->StructType[0] = 0;
      if (!AppExprIsNumeric(t) || !AppExprIsNumeric(r))
        return AppExprFail(c, "'%c' needs numbers (got %s and %s)", op, AppExprTypeName(t), AppExprTypeName(r));
      t = AppExprPromote(t, r);
    }
    return t;
  }

  static int AppExprRel(AppExprCtx* c)
  {
    int t = AppExprAdd(c);
    while (!c->Failed && (AppExprAccept(c, "<=") || AppExprAccept(c, ">=") || AppExprAccept(c, "<") || AppExprAccept(c, ">")))
    {
      const int r = AppExprAdd(c);
      if (c->Failed) return r;
      c->StructType[0] = 0;
      if (!AppExprIsNumeric(t) || !AppExprIsNumeric(r))
        return AppExprFail(c, "comparison needs numbers (got %s and %s)", AppExprTypeName(t), AppExprTypeName(r));
      t = ImGuiAppFieldType_Bool;
    }
    return t;
  }

  static int AppExprEq(AppExprCtx* c)
  {
    int t = AppExprRel(c);
    while (!c->Failed && (AppExprAccept(c, "==") || AppExprAccept(c, "!=")))
    {
      const int r = AppExprRel(c);
      if (c->Failed) return r;
      c->StructType[0] = 0;
      const bool ok = (AppExprIsNumeric(t) && AppExprIsNumeric(r)) || (AppExprIsBool(t) && AppExprIsBool(r));
      if (!ok)
        return AppExprFail(c, "'==' / '!=' needs two numbers or two bools (got %s and %s)", AppExprTypeName(t), AppExprTypeName(r));
      t = ImGuiAppFieldType_Bool;
    }
    return t;
  }

  static int AppExprXor(AppExprCtx* c)
  {
    int t = AppExprEq(c);
    while (!c->Failed && AppExprAccept(c, "^"))
    {
      const int r = AppExprEq(c);
      if (c->Failed) return r;
      c->StructType[0] = 0;
      if (AppExprIsBool(t) && AppExprIsBool(r))
        t = ImGuiAppFieldType_Bool;         // the change idiom: temp ^ last
      else if (AppExprIsInt(t) && AppExprIsInt(r))
        t = ImGuiAppFieldType_Int;
      else
        return AppExprFail(c, "'^' pairs two bools (the change idiom) or two ints (got %s and %s)", AppExprTypeName(t), AppExprTypeName(r));
    }
    return t;
  }

  static int AppExprAnd(AppExprCtx* c)
  {
    int t = AppExprXor(c);
    while (!c->Failed && AppExprAccept(c, "&&"))
    {
      const int r = AppExprXor(c);
      if (c->Failed) return r;
      c->StructType[0] = 0;
      if (!AppExprIsBool(t) || !AppExprIsBool(r))
        return AppExprFail(c, "'&&' needs two bools (got %s and %s)", AppExprTypeName(t), AppExprTypeName(r));
      t = ImGuiAppFieldType_Bool;
    }
    return t;
  }

  static int AppExprOr(AppExprCtx* c)
  {
    int t = AppExprAnd(c);
    while (!c->Failed && AppExprAccept(c, "||"))
    {
      const int r = AppExprAnd(c);
      if (c->Failed) return r;
      c->StructType[0] = 0;
      if (!AppExprIsBool(t) || !AppExprIsBool(r))
        return AppExprFail(c, "'||' needs two bools (got %s and %s)", AppExprTypeName(t), AppExprTypeName(r));
      t = ImGuiAppFieldType_Bool;
    }
    return t;
  }

  bool AppEventExprCheck(const ImGuiAppGraph* g, const ImGuiAppNode* n, const ImGuiAppEventDesc* ev, char* err, int err_size)
  {
    IM_ASSERT(g != nullptr && n != nullptr && ev != nullptr);
    if (err != nullptr && err_size > 0)
      err[0] = 0;
    if (ev->Expr[0] == 0)
      return true;   // empty: codegen copies the watched temp field

    AppExprCtx c;
    c.Graph = g;
    c.Node = n;
    c.Cur = ev->Expr;
    c.Failed = false;
    c.Err[0] = 0;
    c.StructType[0] = 0;

    const int t = AppExprOr(&c);
    if (!c.Failed)
    {
      AppExprSkipBlanks(&c);
      if (*c.Cur != 0)
        AppExprFail(&c, "unexpected '%.12s'", c.Cur);
    }

    // The expression lands in `data-><DstField> = <Expr>;` -- its type must fit the destination.
    if (!c.Failed && ev->Action == ImGuiAppEventAction_SetField && ev->DstField[0] && t != AppExprType_Unknown)
    {
      char dst_id[IM_LABEL_SIZE];
      AppSanitizeIdentifier(dst_id, IM_ARRAYSIZE(dst_id), ev->DstField);
      char dst_struct[IM_LABEL_SIZE];
      dst_struct[0] = 0;
      const int dt = AppExprLookupField(g, n, 0, dst_id, dst_struct, IM_ARRAYSIZE(dst_struct));
      if (dt == ImGuiAppFieldType_Bool && t != ImGuiAppFieldType_Bool)
        AppExprFail(&c, "expr is %s but data->%s is bool -- compare explicitly", AppExprTypeName(t), dst_id);
      else if ((dt == ImGuiAppFieldType_Float || dt == ImGuiAppFieldType_Int || dt == ImGuiAppFieldType_Double) && !AppExprIsNumeric(t))
        AppExprFail(&c, "expr is %s but data->%s is %s", AppExprTypeName(t), dst_id, AppExprTypeName(dt));
      else if ((dt == ImGuiAppFieldType_Vec2 || dt == ImGuiAppFieldType_Vec4) && t != dt)
        AppExprFail(&c, "expr is %s but data->%s is %s", AppExprTypeName(t), dst_id, AppExprTypeName(dt));
      else if (dt == ImGuiAppFieldType_String)
        AppExprFail(&c, "data->%s is char[] -- string fields cannot be event targets", dst_id);
      else if (dt == ImGuiAppFieldType_Struct && (t != ImGuiAppFieldType_Struct || strcmp(c.StructType, dst_struct) != 0))
        AppExprFail(&c, "data->%s is struct %s -- assign a whole %s field", dst_id, dst_struct, dst_struct);
      // dt < 0 (missing destination) is reported by AppGraphValidate separately.
    }

    if (c.Failed && err != nullptr)
      ImStrncpy(err, c.Err, err_size);
    return !c.Failed;
  }

  // Emit a control struct (and its data structs) with derived dependencies + binding assignment lines.
  static void AppEmitControlWithDeps(const ImGuiAppGraph* g, const ImGuiAppNode* n, ImGuiTextBuffer* out)
  {
    char base[IM_LABEL_SIZE];
    AppNodeBaseName(n, base, IM_ARRAYSIZE(base));

    // Data structs. When PersistData/TempData is exploded into a Struct node (named <base>Data / <base>TempData),
    // that node emits the type -- skip the inline def here to avoid a duplicate. Else emit from inline fields.
    const bool persist_exploded = n->PersistStructId >= 0 && AppGraphFindNodeConst(g, n->PersistStructId) != nullptr;
    const bool temp_exploded    = n->TempStructId    >= 0 && AppGraphFindNodeConst(g, n->TempStructId)    != nullptr;

    // Edge-triggered EmitCommand events need a persist latch: OnUpdate (which sees last_temp_data) sets it on
    // the edge, OnGetCommand (which does not) emits it the same frame -- the Task layer updates before the
    // Command layer collects. Dedup by command so two events sharing a command share the latch.
    ImVector<int> latch_events;
    for (int e = 0; e < n->Events.Size; e++)
    {
      const ImGuiAppEventDesc* ev = &n->Events.Data[e];
      if (ev->Action != ImGuiAppEventAction_EmitCommand || ev->Edge == ImGuiAppEventEdge_Active || ev->Command[0] == 0)
        continue;
      bool dup = false;
      for (int l = 0; l < latch_events.Size && !dup; l++)
        dup = strcmp(n->Events.Data[latch_events.Data[l]].Command, ev->Command) == 0;
      if (!dup)
        latch_events.push_back(e);
    }
    auto latch_name = [&](const ImGuiAppEventDesc* ev, char* buf, int buf_size)
    {
      char cmd[IM_LABEL_SIZE];
      AppSanitizeIdentifier(cmd, IM_ARRAYSIZE(cmd), ev->Command);
      ImFormatString(buf, buf_size, "%sPending", cmd);
    };

    if (!persist_exploded)
    {
      ImVector<ImGuiAppFieldDesc> persist_fields;
      AppNodeEffectiveFields(g, n, 0, &persist_fields);
      out->appendf("struct %sData\n{\n", base);
      int type_w = AppFieldDeclTypeWidth(persist_fields.Data, persist_fields.Size);
      if (latch_events.Size > 0 && type_w < 4)
        type_w = 4;   // the latch rows below are "bool"
      for (int i = 0; i < persist_fields.Size; i++)
        AppEmitFieldDecl(out, &persist_fields.Data[i], type_w);
      for (int l = 0; l < latch_events.Size; l++)
      {
        char latch[IM_LABEL_SIZE + 16];
        latch_name(&n->Events.Data[latch_events.Data[l]], latch, IM_ARRAYSIZE(latch));
        out->appendf("  %-*s %s;   // event latch: set by OnUpdate on the edge, emitted by OnGetCommand\n", type_w, "bool", latch);
      }
      out->appendf("};\n\n");
    }
    else if (latch_events.Size > 0)
    {
      out->appendf("// NOTE: add the event latch field(s) below to the exploded PersistData struct:\n");
      for (int l = 0; l < latch_events.Size; l++)
      {
        char latch[IM_LABEL_SIZE + 16];
        latch_name(&n->Events.Data[latch_events.Data[l]], latch, IM_ARRAYSIZE(latch));
        out->appendf("//   bool %s;\n", latch);
      }
      out->appendf("\n");
    }
    if (!temp_exploded)
    {
      ImVector<ImGuiAppFieldDesc> temp_fields;
      AppNodeEffectiveFields(g, n, 1, &temp_fields);
      out->appendf("struct %sTempData\n{\n", base);
      const int type_w = AppFieldDeclTypeWidth(temp_fields.Data, temp_fields.Size);
      for (int i = 0; i < temp_fields.Size; i++)
        AppEmitFieldDecl(out, &temp_fields.Data[i], type_w);
      out->appendf("};\n\n");
    }

    // PersistData/TempData type names. When exploded, follow the Struct node's ACTUAL name (so renaming it stays
    // valid); else the inline <base>Data / <base>TempData. Used everywhere below instead of base + literal suffix.
    char persist_type[IM_LABEL_SIZE];
    char temp_type[IM_LABEL_SIZE];
    if (persist_exploded)
    {
      AppNodeBaseName(AppGraphFindNodeConst(g, n->PersistStructId), persist_type, IM_ARRAYSIZE(persist_type));
    }
    else
    {
      ImFormatString(persist_type, IM_ARRAYSIZE(persist_type), "%sData", base);
    }
    if (temp_exploded)
    {
      AppNodeBaseName(AppGraphFindNodeConst(g, n->TempStructId), temp_type, IM_ARRAYSIZE(temp_type));
    }
    else
    {
      ImFormatString(temp_type, IM_ARRAYSIZE(temp_type), "%sTempData", base);
    }

    // Dependency producers.
    ImVector<int> deps;
    AppGraphConsumerDeps(g, n->Id, &deps);

    // Control struct header with template dependency args.
    out->appendf("struct %s : ImGuiAppControl<%s, %s", base, persist_type, temp_type);
    for (int d = 0; d < deps.Size; d++)
    {
      const ImGuiAppNode* dn = AppGraphFindNodeConst(g, deps.Data[d]);
      char dtype[IM_LABEL_SIZE]; AppNodeDataTypeName(dn, dtype, IM_ARRAYSIZE(dtype));
      out->appendf(", %s", dtype);
    }
    out->appendf(">\n{\n");

    // Build dep (type, param) pairs once.
    auto emit_dep_params = [&](ImGuiTextBuffer* o)
    {
      for (int d = 0; d < deps.Size; d++)
      {
        const ImGuiAppNode* dn = AppGraphFindNodeConst(g, deps.Data[d]);
        char dtype[IM_LABEL_SIZE]; AppNodeDataTypeName(dn, dtype, IM_ARRAYSIZE(dtype));
        char dparam[IM_LABEL_SIZE]; AppToSnake(dparam, IM_ARRAYSIZE(dparam), dn->Draft.Name);
        o->appendf(", const %s* %s", dtype, dparam);
      }
    };

    out->appendf("  virtual void OnInitialize(ImGuiApp* app, %s* data", persist_type);
    emit_dep_params(out);
    out->appendf(") const override final\n  {\n    IM_UNUSED(app); IM_UNUSED(data);\n    // TODO: initialize persistent data\n  }\n\n");

    out->appendf("  virtual void OnGetCommand(const ImGuiApp* app, ImGuiAppCommand* cmd, const %s* data, const %s* temp_data", persist_type, temp_type);
    emit_dep_params(out);
    out->appendf(") const override final\n  {\n    IM_UNUSED(app); IM_UNUSED(cmd); IM_UNUSED(data); IM_UNUSED(temp_data);\n");
    // Event-driven command emissions: level events read temp_data directly; edge events read the persist latch
    // OnUpdate set this frame (the Task layer updates before the Command layer collects).
    bool any_cmd_event = false;
    for (int e = 0; e < n->Events.Size; e++)
    {
      const ImGuiAppEventDesc* ev = &n->Events.Data[e];
      if (ev->Action != ImGuiAppEventAction_EmitCommand || ev->Command[0] == 0)
        continue;
      ImGuiAppCommandDesc cd;
      ImStrncpy(cd.Name, ev->Command, IM_ARRAYSIZE(cd.Name));
      char enum_value[IM_LABEL_SIZE];
      AppCommandEnumValue(&cd, enum_value, IM_ARRAYSIZE(enum_value));
      if (AppGraphFindCommandDefinition(g, ev->Command) == nullptr)
      {
        out->appendf("    // WARNING: event command '%s' is not defined on CommandLayer\n", ev->Command);
        continue;
      }
      if (ev->Edge == ImGuiAppEventEdge_Active)
      {
        char fld[IM_LABEL_SIZE];
        AppSanitizeIdentifier(fld, IM_ARRAYSIZE(fld), ev->TempField);
        out->appendf("    if (temp_data->%s)\n      *cmd = (ImGuiAppCommand)%s;\n", fld, enum_value);
      }
      else
      {
        char latch[IM_LABEL_SIZE + 16];
        latch_name(ev, latch, IM_ARRAYSIZE(latch));
        out->appendf("    if (data->%s)\n      *cmd = (ImGuiAppCommand)%s;\n", latch, enum_value);
      }
      any_cmd_event = true;
    }
    if (!any_cmd_event)
    {
      if (n->Commands.Size > 0)
      {
        out->appendf("    // TODO: emit one of this control's commands from UI/control state, for example:\n");
        for (int c = 0; c < n->Commands.Size; c++)
        {
          if (AppGraphFindCommandDefinition(g, n->Commands.Data[c].Name) == nullptr)
          {
            out->appendf("    // TODO: command '%s' is not defined on CommandLayer\n", n->Commands.Data[c].Name);
            continue;
          }
          char enum_value[IM_LABEL_SIZE];
          AppCommandEnumValue(&n->Commands.Data[c], enum_value, IM_ARRAYSIZE(enum_value));
          out->appendf("    // *cmd = (ImGuiAppCommand)%s;\n", enum_value);
        }
      }
      else
        out->appendf("    // TODO: set *cmd when this control should request an app command\n");
    }
    out->appendf("  }\n\n");

    out->appendf("  virtual void OnUpdate(float dt, %s* data, const %s* temp_data, const %s* last_temp_data", persist_type, temp_type, temp_type);
    emit_dep_params(out);
    out->appendf(") const override final\n  {\n    IM_UNUSED(dt); IM_UNUSED(data); IM_UNUSED(temp_data); IM_UNUSED(last_temp_data);\n");
    // Field bindings -> assignment lines.
    for (int d = 0; d < deps.Size; d++)
    {
      const ImGuiAppNode* dn = AppGraphFindNodeConst(g, deps.Data[d]);
      char dparam[IM_LABEL_SIZE]; AppToSnake(dparam, IM_ARRAYSIZE(dparam), dn->Draft.Name);
      // Find the data link producer==dn -> consumer==n, then its bindings.
      for (int li = 0; li < g->Links.Size; li++)
      {
        if (g->Links.Data[li].Kind != ImGuiAppEdgeKind_Data) continue;
        if (AppGraphPortOwnerId(g, g->Links.Data[li].EndAttr) != n->Id) continue;
        if (AppGraphPortOwnerId(g, g->Links.Data[li].StartAttr) != dn->Id) continue;
        const int link_id = g->Links.Data[li].Id;
        for (int bi = 0; bi < g->Bindings.Size; bi++)
        {
          if (g->Bindings.Data[bi].LinkId != link_id) continue;
          char dst_id[IM_LABEL_SIZE]; AppSanitizeIdentifier(dst_id, IM_ARRAYSIZE(dst_id), g->Bindings.Data[bi].DstField);
          char src_id[IM_LABEL_SIZE]; AppSanitizeIdentifier(src_id, IM_ARRAYSIZE(src_id), g->Bindings.Data[bi].SrcField);
          // Type gate: a builtin producer's fields are unknown here (its struct already exists) so trust it;
          // for a drafted producer require both fields to resolve and share a type.
          const int dst_t = AppDraftFieldType(&n->Draft, g->Bindings.Data[bi].DstField);
          const int src_t = AppDraftFieldType(&dn->Draft, g->Bindings.Data[bi].SrcField);
          const bool types_ok = dn->IsBuiltin || (dst_t >= 0 && src_t >= 0 && dst_t == src_t);
          if (dst_id[0] && src_id[0] && types_ok)
            out->appendf("    data->%s = %s->%s;\n", dst_id, dparam, src_id);
          else if (dst_id[0] && src_id[0])
            out->appendf("    // WARNING: dropped binding %s = %s (type mismatch)\n", dst_id, src_id);
        }

        // Inferred bindings: a same-named, same-typed field present on BOTH a drafted producer and this consumer
        // gets an auto-copy line -- no manual binding row needed. Skipped when an explicit binding already covers
        // the destination (the explicit one wins) or the producer is builtin (its fields aren't known here).
        if (!dn->IsBuiltin)
        {
          for (int cf = 0; cf < n->Draft.PersistFields.Size; cf++)
          {
            const ImGuiAppFieldDesc* cdesc = &n->Draft.PersistFields.Data[cf];
            bool already = false;
            for (int bi = 0; bi < g->Bindings.Size && !already; bi++)
              if (g->Bindings.Data[bi].LinkId == link_id && strcmp(g->Bindings.Data[bi].DstField, cdesc->Name) == 0)
                already = true;
            if (already)
              continue;
            for (int pf = 0; pf < dn->Draft.PersistFields.Size; pf++)
            {
              const ImGuiAppFieldDesc* pdesc = &dn->Draft.PersistFields.Data[pf];
              if (strcmp(pdesc->Name, cdesc->Name) != 0 || pdesc->Type != cdesc->Type)
                continue;
              char id[IM_LABEL_SIZE];
              AppSanitizeIdentifier(id, IM_ARRAYSIZE(id), cdesc->Name);
              out->appendf("    data->%s = %s->%s;   // inferred (name+type match)\n", id, dparam, id);
              break;
            }
          }
        }
      }
    }
    // Exploded Field producers: each wired field assigns from its parent struct (SrcField IS the field's name;
    // the consumer's destination field comes from the edge binding).
    for (int li = 0; li < g->Links.Size; li++)
    {
      if (g->Links.Data[li].Kind != ImGuiAppEdgeKind_Data) continue;
      if (AppGraphPortOwnerId(g, g->Links.Data[li].EndAttr) != n->Id) continue;
      const int pid = AppGraphPortOwnerId(g, g->Links.Data[li].StartAttr);
      const ImGuiAppNode* fn = pid >= 0 ? AppGraphFindNodeConst(g, pid) : nullptr;
      if (fn == nullptr || fn->Kind != ImGuiAppNodeKind_Field) continue;
      const int sid = AppGraphParentOf(g, fn->Id);
      const ImGuiAppNode* sn = sid >= 0 ? AppGraphFindNodeConst(g, sid) : nullptr;
      if (sn == nullptr)
      {
        out->appendf("    // WARNING: field '%s' has no parent struct -- cannot bind\n", fn->Draft.Name);
        continue;
      }
      char sparam[IM_LABEL_SIZE]; AppToSnake(sparam, IM_ARRAYSIZE(sparam), sn->Draft.Name);
      char fld[IM_LABEL_SIZE]; AppSanitizeIdentifier(fld, IM_ARRAYSIZE(fld), fn->Draft.Name);
      const char* dst = nullptr;
      for (int bi = 0; bi < g->Bindings.Size; bi++)
        if (g->Bindings.Data[bi].LinkId == g->Links.Data[li].Id && g->Bindings.Data[bi].DstField[0]) { dst = g->Bindings.Data[bi].DstField; break; }
      if (dst != nullptr)
      {
        char dst_id[IM_LABEL_SIZE]; AppSanitizeIdentifier(dst_id, IM_ARRAYSIZE(dst_id), dst);
        out->appendf("    data->%s = %s->%s;\n", dst_id, sparam, fld);
      }
      else
        out->appendf("    // TODO: choose a destination field for %s->%s\n", sparam, fld);
    }

    // Authored events. OnRender recorded temp_data; compare against last frame's to identify what
    // happened, then mutate persistent state -- OnUpdate is the sole mutator.
    if (n->Events.Size > 0)
    {
      out->appendf("\n    // events: identify what happened by comparing this frame's TempData with last frame's\n");
      for (int l = 0; l < latch_events.Size; l++)
      {
        char latch[IM_LABEL_SIZE + 16];
        latch_name(&n->Events.Data[latch_events.Data[l]], latch, IM_ARRAYSIZE(latch));
        out->appendf("    data->%s = false;   // re-arm: OnGetCommand consumed last frame's edge\n", latch);
      }
      for (int e = 0; e < n->Events.Size; e++)
      {
        const ImGuiAppEventDesc* ev = &n->Events.Data[e];
        if (ev->TempField[0] == 0)
        {
          out->appendf("    // TODO: event %d has no TempData field to watch\n", e);
          continue;
        }
        char fld[IM_LABEL_SIZE];
        AppSanitizeIdentifier(fld, IM_ARRAYSIZE(fld), ev->TempField);
        const bool is_bool = AppNodeEffectiveFieldType(g, n, 1, ev->TempField) == ImGuiAppFieldType_Bool;

        if (ev->Action == ImGuiAppEventAction_SetField)
        {
          if (ev->DstField[0] == 0)
          {
            out->appendf("    // TODO: event on temp_data->%s has no destination field\n", fld);
            continue;
          }
          char dst_id[IM_LABEL_SIZE];
          AppSanitizeIdentifier(dst_id, IM_ARRAYSIZE(dst_id), ev->DstField);
          AppEmitEventGuard(out, ev, is_bool, fld);
          if (ev->Expr[0])
            out->appendf("      data->%s = %s;\n", dst_id, ev->Expr);
          else
            out->appendf("      data->%s = temp_data->%s;\n", dst_id, fld);
        }
        else if (ev->Action == ImGuiAppEventAction_EmitCommand && ev->Edge != ImGuiAppEventEdge_Active && ev->Command[0])
        {
          char latch[IM_LABEL_SIZE + 16];
          latch_name(ev, latch, IM_ARRAYSIZE(latch));
          AppEmitEventGuard(out, ev, is_bool, fld);
          out->appendf("      data->%s = true;   // OnGetCommand emits AppCommand this frame (Task updates before Command collects)\n", latch);
        }
        // EmitCommand + Active is handled entirely in OnGetCommand (it reads temp_data directly).
      }
    }
    out->appendf("  }\n\n");

    out->appendf("  virtual void OnRender(const %s* data, %s* temp_data", persist_type, temp_type);
    emit_dep_params(out);
    out->appendf(") const override final\n  {\n    IM_UNUSED(data); IM_UNUSED(temp_data);\n    // TODO: render widgets from const data\n");
    {
      // Capture stubs: OnRender's half of the contract is recording raw input into temp_data (zeroed each
      // frame) for the next OnUpdate to compare -- list the authored temp fields so the hookup is obvious.
      ImVector<ImGuiAppFieldDesc> temp_fields;
      AppNodeEffectiveFields(g, n, 1, &temp_fields);
      for (int i = 0; i < temp_fields.Size; i++)
      {
        char fld[IM_LABEL_SIZE];
        AppSanitizeIdentifier(fld, IM_ARRAYSIZE(fld), temp_fields.Data[i].Name);
        if (temp_fields.Data[i].Type == ImGuiAppFieldType_Bool)
          out->appendf("    // temp_data->%s = ImGui::IsItemHovered();   // or Button()/IsItemClicked() etc.\n", fld);
        else
          out->appendf("    // temp_data->%s = ...;   // record this frame's raw input\n", fld);
      }
    }
    out->appendf("  }\n};\n\n");
  }

  // Containment parent node id for a child node (its ChildOut -> a ChildIn), or -1.
  static int AppGraphParentOf(const ImGuiAppGraph* g, int child_node_id)
  {
    for (int li = 0; li < g->Links.Size; li++)
    {
      if (g->Links.Data[li].Kind != ImGuiAppEdgeKind_Containment) continue;
      if (AppGraphPortOwnerId(g, g->Links.Data[li].StartAttr) != child_node_id) continue;
      return AppGraphPortOwnerId(g, g->Links.Data[li].EndAttr);
    }
    return -1;
  }

  // Does any control node (authored or live) name this host (Window/Sidebar) as its containment parent? Used by
  // codegen to decide whether to capture a named local for the host (so it isn't emitted unused).
  static bool AppGraphHostsControl(const ImGuiAppGraph* g, int host_id)
  {
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (n->Kind != ImGuiAppNodeKind_Control) continue;
      if (AppGraphParentOf(g, n->Id) == host_id) return true;
    }
    return false;
  }

  ImGuiID AppGraphSignature(const ImGuiAppGraph* g)
  {
    IM_ASSERT(g != nullptr);

    // Fold the authored (!IsLive) population plus command definitions attached to the CommandLayer. Live mirror
    // node churn still stays out of the hash. char[] hashed as NUL-terminated ImHashStr (ctors zero only byte 0,
    // so ImHashData over the fixed buffer would fold trailing garbage). GridPos/ids/BodyAttrId excluded.
    ImGuiID h = 0;
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (n->IsLive)
      {
        if (AppNodeIsCommandLayer(n))
          for (int c = 0; c < n->Commands.Size; c++)
            h = ImHashStr(n->Commands.Data[c].Name, 0, h);
        continue;
      }
      h = ImHashData(&n->Kind, sizeof(n->Kind), h);
      h = ImHashStr(n->Draft.Name, 0, h);
      h = ImHashStr(n->TypeName, 0, h);
      h = ImHashStr(n->DataTypeName, 0, h);
      h = ImHashData(&n->IsBuiltin, sizeof(n->IsBuiltin), h);
      h = ImHashData(&n->LayerType, sizeof(n->LayerType), h);
      for (int f = 0; f < n->Draft.PersistFields.Size; f++)
      {
        const ImGuiAppFieldDesc* fd = &n->Draft.PersistFields.Data[f];
        h = ImHashStr(fd->Name, 0, h);
        h = ImHashData(&fd->Type, sizeof(fd->Type), h);
        h = ImHashData(&fd->ArraySize, sizeof(fd->ArraySize), h);
        h = ImHashStr(fd->StructType, 0, h);
      }
      for (int f = 0; f < n->Draft.TempFields.Size; f++)
      {
        const ImGuiAppFieldDesc* fd = &n->Draft.TempFields.Data[f];
        h = ImHashStr(fd->Name, 0, h);
        h = ImHashData(&fd->Type, sizeof(fd->Type), h);
        h = ImHashData(&fd->ArraySize, sizeof(fd->ArraySize), h);
        h = ImHashStr(fd->StructType, 0, h);
      }
      for (int c = 0; c < n->Commands.Size; c++)
        h = ImHashStr(n->Commands.Data[c].Name, 0, h);
      for (int e = 0; e < n->Events.Size; e++)
      {
        const ImGuiAppEventDesc* ev = &n->Events.Data[e];
        h = ImHashData(&ev->Edge, sizeof(ev->Edge), h);
        h = ImHashData(&ev->Action, sizeof(ev->Action), h);
        h = ImHashStr(ev->TempField, 0, h);
        h = ImHashStr(ev->DstField, 0, h);
        h = ImHashStr(ev->Command, 0, h);
        h = ImHashStr(ev->Expr, 0, h);
      }
      for (int s = 0; s < n->StyleMods.Size; s++)
      {
        const ImGuiAppStyleModDesc* sm = &n->StyleMods.Data[s];
        h = ImHashData(&sm->Var, sizeof(sm->Var), h);
        h = ImHashData(&sm->Value, sizeof(sm->Value), h);
        h = ImHashData(&sm->Active, sizeof(sm->Active), h);
      }
      for (int s = 0; s < n->ColorMods.Size; s++)
      {
        const ImGuiAppColorModDesc* cm = &n->ColorMods.Data[s];
        h = ImHashData(&cm->Col, sizeof(cm->Col), h);
        h = ImHashData(&cm->Value, sizeof(cm->Value), h);
        h = ImHashData(&cm->Active, sizeof(cm->Active), h);
      }
    }
    for (int li = 0; li < g->Links.Size; li++)
    {
      const ImGuiAppNodeLink* l = &g->Links.Data[li];
      const int oa = AppGraphPortOwnerId(g, l->StartAttr);
      const int ob = AppGraphPortOwnerId(g, l->EndAttr);
      const ImGuiAppNode* na = oa >= 0 ? AppGraphFindNodeConst(g, oa) : nullptr;
      const ImGuiAppNode* nb = ob >= 0 ? AppGraphFindNodeConst(g, ob) : nullptr;
      if (na == nullptr || nb == nullptr || na->IsLive || nb->IsLive) continue;   // authored links only
      h = ImHashData(&l->StartAttr, sizeof(l->StartAttr), h);   // stable port ids: capture connectivity changes
      h = ImHashData(&l->EndAttr, sizeof(l->EndAttr), h);
      h = ImHashData(&l->Kind, sizeof(l->Kind), h);
      for (int bi = 0; bi < g->Bindings.Size; bi++)
      {
        if (g->Bindings.Data[bi].LinkId != l->Id) continue;
        h = ImHashStr(g->Bindings.Data[bi].DstField, 0, h);
        h = ImHashStr(g->Bindings.Data[bi].SrcField, 0, h);
      }
    }
    return h;
  }

  // F17. The signature is content-derived (recomputed, never stored in the model), so it survives a
  // save/load round-trip by construction. Revision is the cheap monotonic pulse the editor reads to
  // know "did the authored graph change since last frame" without diffing hashes itself.
  int AppGraphSyncRevision(ImGuiAppGraph* g)
  {
    IM_ASSERT(g != nullptr);
    const ImGuiID sig = AppGraphSignature(g);
    if (sig != g->_SigCache)
    {
      g->_SigCache = sig;
      g->Revision++;
    }
    return g->Revision;
  }

  void AppGraphMarkGenerated(ImGuiAppGraph* g)
  {
    IM_ASSERT(g != nullptr);
    g->GenSignature = AppGraphSignature(g);
  }

  bool AppGraphCodeStale(const ImGuiAppGraph* g)
  {
    IM_ASSERT(g != nullptr);
    return AppGraphSignature(g) != g->GenSignature;
  }

  bool AppGraphCodeFresh(const ImGuiAppGraph* g)
  {
    IM_ASSERT(g != nullptr);
    return g->GenSignature != 0 && AppGraphSignature(g) == g->GenSignature;
  }

  int AppScanCodegenWarnings(const char* code, ImGuiTextBuffer* out_list)
  {
    IM_ASSERT(code != nullptr);
    static const char* markers[] = { "// WARNING", "// codegen aborted" };
    int count = 0;
    const char* line = code;
    while (*line)
    {
      const char* eol = line;
      while (*eol != 0 && *eol != '\n') eol++;
      bool hit = false;
      for (int m = 0; m < IM_ARRAYSIZE(markers) && !hit; m++)
        hit = ImStristr(line, eol, markers[m], nullptr) != nullptr;
      if (hit)
      {
        count++;
        if (out_list != nullptr)
        {
          const char* s = line;
          while (s < eol && (*s == ' ' || *s == '\t')) s++;   // trim leading indent
          out_list->append(s, eol);
          out_list->append("\n");
        }
      }
      line = (*eol == '\n') ? eol + 1 : eol;
    }
    return count;
  }

  // Emit the ImGuiAppLayer subclass a Custom layer node names: the phase hooks, stubbed at their positions in
  // the loop. Core layers ship with the framework and emit nothing but their bring-up line.
  static void AppEmitCustomLayerCode(const ImGuiAppNode* n, ImGuiTextBuffer* out)
  {
    char base[IM_LABEL_SIZE];
    AppNodeBaseName(n, base, IM_ARRAYSIZE(base));
    out->appendf("struct %s : ImGuiAppLayer\n{\n", base);
    out->appendf("  virtual void OnAttach(ImGuiApp* app) const override\n  {\n    IM_UNUSED(app);\n  }\n\n");
    out->appendf("  virtual void OnDetach(ImGuiApp* app) const override\n  {\n    IM_UNUSED(app);\n  }\n\n");
    out->appendf("  virtual void OnUpdate(ImGuiApp* app, float dt) const override\n  {\n    IM_UNUSED(app); IM_UNUSED(dt);\n    // TODO: this layer's per-frame work, at its position in the stack\n  }\n\n");
    out->appendf("  virtual void OnRender(const ImGuiApp* app) const override\n  {\n    IM_UNUSED(app);\n  }\n};\n\n");
  }

  // Emit a standalone struct type from a Struct node: its PersistFields are the members.
  static void AppEmitStructCode(const ImGuiAppGraph* g, const ImGuiAppNode* n, ImGuiTextBuffer* out)
  {
    char base[IM_LABEL_SIZE]; AppNodeBaseName(n, base, IM_ARRAYSIZE(base));
    ImVector<ImGuiAppFieldDesc> fields; AppNodeEffectiveFields(g, n, 0, &fields);   // exploded Field nodes or inline
    out->appendf("struct %s\n{\n", base);
    const int type_w = AppFieldDeclTypeWidth(fields.Data, fields.Size);
    for (int i = 0; i < fields.Size; i++)
      AppEmitFieldDecl(out, &fields.Data[i], type_w);
    out->appendf("};\n\n");
  }

  void GenerateAppGraphCode(const ImGuiAppGraph* g, ImGuiTextBuffer* out)
  {
    GenerateAppGraphCodeEx(g, out, nullptr);
  }

  // Format a float as a C++ literal: %g alone drops the decimal point from whole values, and "8f" won't compile.
  static void AppFloatLit(char* out, size_t out_size, float v)
  {
    int len = ImFormatString(out, out_size, "%g", v);
    if (strchr(out, '.') == nullptr && strchr(out, 'e') == nullptr)
      len += ImFormatString(out + len, out_size - (size_t)len, ".0");
    ImFormatString(out + len, out_size - (size_t)len, "f");
  }

  // Emit the StyleMods/ColorMods fill lines for one just-pushed item; `expr` names the item (e.g.
  // "app->Windows.back()"). Emitted into SetupApp right after the push -- windows/sidebars/controls have no
  // generated classes, so bring-up is the one codegen site that can reach the runtime object.
  static void AppEmitStyleModLines(const ImGuiAppNode* n, const char* expr, const char* indent, ImGuiTextBuffer* out)
  {
    for (int i = 0; i < n->StyleMods.Size; i++)
    {
      const ImGuiAppStyleModDesc* sm = &n->StyleMods.Data[i];
      const ImGuiStyleVarInfo* info = ImGui::GetStyleVarInfo(sm->Var);
      char x[32], y[32];
      AppFloatLit(x, IM_ARRAYSIZE(x), sm->Value.x);
      AppFloatLit(y, IM_ARRAYSIZE(y), sm->Value.y);
      out->appendf("%s%s->StyleMods.push_back(ImGuiAppStyleModDesc{ %s, ImVec2(%s, %s), %s });\n",
                   indent, expr, AppStyleVarEnumName(sm->Var), x, info->Count == 2 ? y : "0.0f", sm->Active ? "true" : "false");
    }
    for (int i = 0; i < n->ColorMods.Size; i++)
    {
      const ImGuiAppColorModDesc* cm = &n->ColorMods.Data[i];
      out->appendf("%s%s->ColorMods.push_back(ImGuiAppColorModDesc{ ImGuiCol_%s, IM_COL32(%u, %u, %u, %u), %s });\n",
                   indent, expr, ImGui::GetStyleColorName(cm->Col),
                   (cm->Value >> IM_COL32_R_SHIFT) & 0xFF, (cm->Value >> IM_COL32_G_SHIFT) & 0xFF,
                   (cm->Value >> IM_COL32_B_SHIFT) & 0xFF, (cm->Value >> IM_COL32_A_SHIFT) & 0xFF,
                   cm->Active ? "true" : "false");
    }
  }

  //-----------------------------------------------------------------------------
  // Live-node emission: a live node's code is REFLECTION of the running composition, never a draft.
  // Types come from the runtime objects (reflected data shapes, mirrored placement); member functions are
  // declaration-only -- their definitions are the ones compiled into the running binary.
  //-----------------------------------------------------------------------------

  // Type spelling of one reflected field as its declaration emits it.
  static const char* AppLiveFieldDeclTypeName(const ImGuiAppLiveFieldDesc* f)
  {
    if (f->Kind == ImGuiAppLiveFieldKind_CharArray)
      return "char";
    if (f->Kind == ImGuiAppLiveFieldKind_Opaque && !f->Exact)
      return "unsigned char";
    return f->TypeName;
  }

  // Identifier column start = widest type spelling in the struct (+1 space), so member names
  // column-align in the emitted definition.
  static int AppLiveFieldDeclTypeWidth(const ImGuiAppLiveFieldDesc* fields, int count)
  {
    int w = 0;
    for (int i = 0; i < count; i++)
    {
      const int len = (int)strlen(AppLiveFieldDeclTypeName(&fields[i]));
      if (len > w)
        w = len;
    }
    return w;
  }

  static void AppEmitLiveFieldDecl(ImGuiTextBuffer* out, const ImGuiAppLiveFieldDesc* f, int type_w)
  {
    if (f->Kind == ImGuiAppLiveFieldKind_CharArray)
      out->appendf("  %-*s %s[%d];\n", type_w, "char", f->Name, f->Size);
    else if (f->Kind == ImGuiAppLiveFieldKind_Opaque && !f->Exact)
      out->appendf("  %-*s %s[%d];   // %s\n", type_w, "unsigned char", f->Name, f->Size, f->TypeName);
    else
      out->appendf("  %-*s %s;\n", type_w, f->TypeName, f->Name);
  }

  // Depth-first mirror emission of a registered schema type: every schema'd type a struct
  // reaches (member type or ImVector element) is written once per document, before first use.
  static void AppEmitSchemaTypeMirror(const char* type_name, ImGuiStorage* emitted, ImGuiTextBuffer* out)
  {
    const ImGuiAppTypeSchema* s = ImGuiAppFindTypeSchema(type_name);
    if (s == nullptr)
      return;
    const ImGuiID key = ImHashStr(type_name);
    if (emitted->GetBool(key))
      return;
    emitted->SetBool(key, true);
    for (int i = 0; i < s->Count; i++)
    {
      const ImGuiAppLiveFieldDesc* f = &s->Fields[i];
      if (const char* nested = f->ElemTypeName ? f->ElemTypeName : (f->Kind == ImGuiAppLiveFieldKind_Opaque ? f->TypeName : nullptr))
        AppEmitSchemaTypeMirror(nested, emitted, out);
    }
    out->appendf("// mirror of %s -- the real definition is compiled into the running binary\nstruct %s\n{\n", type_name, type_name);
    const int type_w = AppLiveFieldDeclTypeWidth(s->Fields, s->Count);
    for (int i = 0; i < s->Count; i++)
      AppEmitLiveFieldDecl(out, &s->Fields[i], type_w);
    out->appendf("};\n\n");
  }

  // Mirror every schema'd type the given field list reaches (used before emitting the struct
  // that declares those fields).
  static void AppEmitSchemaFieldDeps(const ImGuiAppLiveFieldDesc* fields, int count, ImGuiStorage* emitted, ImGuiTextBuffer* out)
  {
    for (int i = 0; i < count; i++)
    {
      const ImGuiAppLiveFieldDesc* f = &fields[i];
      if (const char* nested = f->ElemTypeName ? f->ElemTypeName : (f->Kind == ImGuiAppLiveFieldKind_Opaque ? f->TypeName : nullptr))
        AppEmitSchemaTypeMirror(nested, emitted, out);
    }
  }

  // The RECORDED class name when the mirror stamped one (node named by the control's Label,
  // distinct from its data type); graphs recorded before the label existed carry the data type
  // as the node name -- derive "GraphDocData" -> "GraphDocControl" for those.
  static void AppLiveControlClassName(const ImGuiAppNode* n, char* out, size_t out_size)
  {
    char dtype[IM_LABEL_SIZE];
    AppNodeDataTypeName(n, dtype, IM_ARRAYSIZE(dtype));
    if (n->Draft.Name[0] != 0 && strcmp(n->Draft.Name, dtype) != 0)
    {
      AppSanitizeIdentifier(out, (int)out_size, n->Draft.Name);
      return;
    }
    char base[IM_LABEL_SIZE];
    AppSanitizeIdentifier(base, IM_ARRAYSIZE(base), dtype);
    const size_t len = strlen(base);
    if (len > 4 && strcmp(base + len - 4, "Data") == 0)
      base[len - 4] = 0;
    ImFormatString(out, (int)out_size, "%sControl", base);
  }

  static void AppEmitLiveLayerCode(const ImGuiAppNode* n, ImGuiTextBuffer* out)
  {
    char base[IM_LABEL_SIZE];
    AppNodeBaseName(n, base, IM_ARRAYSIZE(base));
    out->appendf("// live layer '%s' -- definitions live in the running binary\n", n->Draft.Name);
    out->appendf("struct %s : ImGuiAppLayer\n{\n  virtual void OnRender(const ImGuiApp* app) const override;\n};\n\n", base);
  }

  static void AppEmitLiveHostCode(const ImGuiAppNode* n, ImGuiTextBuffer* out)
  {
    char base[IM_LABEL_SIZE];
    AppNodeBaseName(n, base, IM_ARRAYSIZE(base));
    const char* kind = n->Kind == ImGuiAppNodeKind_Sidebar ? "Sidebar" : "Window";
    out->appendf("// live %s '%s' -- definitions live in the running binary\n", n->Kind == ImGuiAppNodeKind_Sidebar ? "sidebar" : "window", n->Draft.Name);
    out->appendf("struct %s : ImGuiApp%s<%s>\n{\n  virtual void OnRender(const ImGuiApp* app) const override;\n};\n\n", base, kind, base);
  }

  // Mirrored initial placement the running host carries (Flags ride the push site).
  static void AppEmitLivePlacementLines(const ImGuiAppNode* n, const char* expr, ImGuiTextBuffer* out)
  {
    if (!n->HasInitialPlacement)
      return;
    out->appendf("  %s->HasInitialPlacement = true;\n", expr);
    out->appendf("  %s->InitialPos = ImVec2(%.1ff, %.1ff);\n", expr, n->InitialPos.x, n->InitialPos.y);
    out->appendf("  %s->InitialSize = ImVec2(%.1ff, %.1ff);\n", expr, n->InitialSize.x, n->InitialSize.y);
  }

  // Reflected data shapes + the control's interface shell with its dependency pack (from the live
  // data edges GetControlDependencyIDs produced). Returns false when the runtime object is gone.
  static bool AppEmitLiveControlCode(const ImGuiAppGraph* g, ImGuiApp* live_app, const ImGuiAppNode* n, ImGuiTextBuffer* out, ImGuiStorage* emitted)
  {
    ImGuiAppItemBase* item = AppGraphFindLiveItem(live_app, n);
    if (item == nullptr)
    {
      out->appendf("// live control '%s': runtime object unavailable\n\n", n->Draft.Name);
      return false;
    }
    const ImGuiAppControlBase* ctrl = (const ImGuiAppControlBase*)item;

    char pname[IM_LABEL_SIZE];
    char tname[IM_LABEL_SIZE];
    ctrl->GetControlDataTypeName(pname, IM_ARRAYSIZE(pname));
    ctrl->GetControlTempDataTypeName(tname, IM_ARRAYSIZE(tname));
    if (pname[0] == 0)
    {
      out->appendf("// live control '%s': no data type name\n\n", n->Draft.Name);
      return false;
    }
    const char* temp_type = tname[0] ? tname : "TempData";

    // The data structs themselves are emitted below with the control; mark them up front so
    // schema recursion never mirrors them a second time.
    emitted->SetBool(ImHashStr(pname), true);
    emitted->SetBool(ImHashStr(temp_type), true);

    ImGuiAppLiveFieldDesc fields[64];
    ImGuiAppLiveFieldDesc temp_fields[64];
    const int nf = ctrl->GetControlFields(fields, IM_ARRAYSIZE(fields), false);
    const int nt = ctrl->GetControlFields(temp_fields, IM_ARRAYSIZE(temp_fields), true);

    // Nested schema'd types first (ImGuiAppGraph and friends): the reflected structs below
    // declare members of these types.
    AppEmitSchemaFieldDeps(fields, nf, emitted, out);
    AppEmitSchemaFieldDeps(temp_fields, nt, emitted, out);

    out->appendf("// reflected from the running control\nstruct %s\n{\n", pname);
    if (nf <= 0)
      out->appendf(ctrl->IsControlDataReflectable(false) ? "  // (no members)\n" : "  // opaque: not reflectable\n");
    int type_w = AppLiveFieldDeclTypeWidth(fields, nf);
    for (int i = 0; i < nf; i++)
      AppEmitLiveFieldDecl(out, &fields[i], type_w);
    out->appendf("};\n\nstruct %s\n{\n", temp_type);
    if (nt <= 0)
      out->appendf(ctrl->IsControlDataReflectable(true) ? "  // (no members)\n" : "  // opaque: not reflectable\n");
    type_w = AppLiveFieldDeclTypeWidth(temp_fields, nt);
    for (int i = 0; i < nt; i++)
      AppEmitLiveFieldDecl(out, &temp_fields[i], type_w);
    out->appendf("};\n\n");

    // Dependency producers, from the live data edges (rebuilt each frame from GetControlDependencyIDs).
    ImVector<int> deps;
    AppGraphConsumerDeps(g, n->Id, &deps);

    char cls[IM_LABEL_SIZE];
    AppLiveControlClassName(n, cls, IM_ARRAYSIZE(cls));
    out->appendf("// definitions live in the running binary\nstruct %s : ImGuiAppControl<%s, %s", cls, pname, temp_type);
    for (int d = 0; d < deps.Size; d++)
    {
      const ImGuiAppNode* dn = AppGraphFindNodeConst(g, deps.Data[d]);
      char dtype[IM_LABEL_SIZE];
      AppNodeDataTypeName(dn, dtype, IM_ARRAYSIZE(dtype));
      out->appendf(", %s", dtype);
    }
    out->appendf(">\n{\n");

    auto emit_dep_params = [&](ImGuiTextBuffer* o)
    {
      for (int d = 0; d < deps.Size; d++)
      {
        const ImGuiAppNode* dn = AppGraphFindNodeConst(g, deps.Data[d]);
        char dtype[IM_LABEL_SIZE];
        AppNodeDataTypeName(dn, dtype, IM_ARRAYSIZE(dtype));
        char dparam[IM_LABEL_SIZE];
        AppToSnake(dparam, IM_ARRAYSIZE(dparam), dn->Draft.Name);
        o->appendf(", const %s* %s", dtype, dparam);
      }
    };
    out->appendf("  virtual void OnInitialize(ImGuiApp* app, %s* data", pname);
    emit_dep_params(out);
    out->appendf(") const override;\n");
    out->appendf("  virtual void OnGetCommand(const ImGuiApp* app, ImGuiAppCommand* cmd, const %s* data, const %s* temp_data", pname, temp_type);
    emit_dep_params(out);
    out->appendf(") const override;\n");
    out->appendf("  virtual void OnUpdate(float dt, %s* data, const %s* temp_data, const %s* last_temp_data", pname, temp_type, temp_type);
    emit_dep_params(out);
    out->appendf(") const override;\n");
    out->appendf("  virtual void OnRender(const %s* data, %s* temp_data", pname, temp_type);
    emit_dep_params(out);
    out->appendf(") const override;\n};\n\n");
    return true;
  }

  void GenerateAppGraphCodeEx(const ImGuiAppGraph* g, ImGuiTextBuffer* out, ImVector<ImGuiAppCodeSpan>* out_spans)
  {
    IM_ASSERT(g != nullptr && out != nullptr);
    if (out_spans != nullptr)
      out_spans->resize(0);

    // Source-map cursor: counts newlines incrementally so each chunk's [begin,end) line range is O(new bytes).
    int scanned = 0;
    int line = 0;
    auto line_now = [&]() -> int
    {
      const char* s = out->Buf.Data;
      const int n = out->size();
      for (; scanned < n; scanned++)
        if (s[scanned] == '\n')
          line++;
      return line;
    };
    auto span = [&](int node_id, int begin)
    {
      if (out_spans == nullptr || node_id < 0)
        return;
      const int end = line_now();
      if (end > begin)
      {
        ImGuiAppCodeSpan sp;
        sp.NodeId = node_id;
        sp.LineBegin = begin;
        sp.LineEnd = end;
        out_spans->push_back(sp);
      }
    };

    // 1) Topo order of ALL controls, authored and live (producers before consumers): the generated
    // program reproduces the whole running composition, not just the authored subset.
    ImVector<int> order;
    char err[160];
    if (!AppGraphTopoOrder(g, &order, err, IM_ARRAYSIZE(err), true))
    {
      out->appendf("// codegen aborted: %s\n", err[0] ? err : "dependency cycle");
      return;
    }

    // 2) Emit client command enum/app shell, then standalone struct types, then data structs + control structs
    // for DRAFTED controls in topo order (builtin types already exist).
    {
      int cmd_layer_id = -1;
      for (int i = 0; i < g->Nodes.Size && cmd_layer_id < 0; i++)
        if (g->Nodes.Data[i].Kind == ImGuiAppNodeKind_Layer && g->Nodes.Data[i].LayerType == ImGuiAppLayerType_Command)
          cmd_layer_id = g->Nodes.Data[i].Id;
      const int begin = line_now();
      AppEmitCommandEnumAndApp(g, out);
      span(cmd_layer_id, begin);
    }

    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (n->Kind == ImGuiAppNodeKind_Layer && n->LayerType == ImGuiAppLayerType_Custom)
      {
        const int begin = line_now();
        if (n->IsLive)
          AppEmitLiveLayerCode(n, out);
        else
          AppEmitCustomLayerCode(n, out);
        span(n->Id, begin);
      }
    }

    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (n->Kind == ImGuiAppNodeKind_Struct && !n->IsBuiltin && !n->IsLive)
      {
        const int begin = line_now();
        AppEmitStructCode(g, n, out);
        span(n->Id, begin);
      }
    }

    // Live host type shells (window/sidebar subclasses compiled into the running binary).
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if ((n->Kind == ImGuiAppNodeKind_Window || n->Kind == ImGuiAppNodeKind_Sidebar) && n->IsLive)
      {
        const int begin = line_now();
        AppEmitLiveHostCode(n, out);
        span(n->Id, begin);
      }
    }

    ImGuiStorage mirrored_types;   // schema'd types already mirrored into this document
    for (int i = 0; i < order.Size; i++)
    {
      const ImGuiAppNode* n = AppGraphFindNodeConst(g, order.Data[i]);
      if (n == nullptr)
        continue;
      const int begin = line_now();
      if (n->IsLive)
        AppEmitLiveControlCode(g, g->LiveApp, n, out, &mirrored_types);
      else if (!n->IsBuiltin)
        AppEmitControlWithDeps(g, n, out);
      span(n->Id, begin);
    }

    // 3) Bring-up function: layers, then windows/sidebars, then controls in topo order. Each node's push
    // line(s) become a second span for that node, so selecting a node also lights its composition site.
    out->appendf("void SetupApp(ImGuiApp* app, ImGuiViewport* vp)\n{\n  IM_UNUSED(vp);\n");

    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (n->Kind != ImGuiAppNodeKind_Layer)
        continue;
      const int begin = line_now();
      if (n->LayerType == ImGuiAppLayerType_Custom)
      {
        char base[IM_LABEL_SIZE];
        AppNodeBaseName(n, base, IM_ARRAYSIZE(base));
        out->appendf("  ImGui::PushAppLayer<%s>(app);\n", base);
      }
      else
        out->appendf("  ImGui::PushAppLayer<%s>(app);\n", AppLayerTypeName(n->LayerType));
      span(n->Id, begin);
    }
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (n->Kind == ImGuiAppNodeKind_Window)
      {
        const int begin = line_now();
        char base[IM_LABEL_SIZE]; AppNodeBaseName(n, base, IM_ARRAYSIZE(base));
        out->appendf("  ImGui::PushAppWindow<%s>(app);\n", base);
        if (n->IsLive && n->Flags != 0)
          out->appendf("  app->Windows.back()->Flags = (ImGuiWindowFlags)0x%X;\n", (unsigned)n->Flags);
        if (n->IsLive)
          AppEmitLivePlacementLines(n, "app->Windows.back()", out);
        AppEmitStyleModLines(n, "app->Windows.back()", "  ", out);
        if (AppGraphHostsControl(g, n->Id))
          out->appendf("  ImGuiAppWindowBase* win_%s = app->Windows.back();\n", base);   // name-based local: stable across import
        span(n->Id, begin);
      }
    }
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* n = &g->Nodes.Data[i];
      if (n->Kind == ImGuiAppNodeKind_Sidebar)
      {
        const int begin = line_now();
        char base[IM_LABEL_SIZE]; AppNodeBaseName(n, base, IM_ARRAYSIZE(base));
        char flags[32];
        if (n->IsLive && n->Flags != 0)
          ImFormatString(flags, IM_ARRAYSIZE(flags), "(ImGuiWindowFlags)0x%X", (unsigned)n->Flags);
        else
          ImStrncpy(flags, "ImGuiWindowFlags_None", IM_ARRAYSIZE(flags));
        out->appendf("  ImGui::PushAppSidebar<%s>(app, vp, %s, %.1ff, %s);\n", base, AppDirEnumName(n->DockDir), n->DockSize, flags);
        if (n->IsLive)
          AppEmitLivePlacementLines(n, "app->Sidebars.back()", out);
        AppEmitStyleModLines(n, "app->Sidebars.back()", "  ", out);
        if (AppGraphHostsControl(g, n->Id))
          out->appendf("  ImGuiAppSidebarBase* sb_%s = app->Sidebars.back();\n", base);   // name-based local: stable across import
        span(n->Id, begin);
      }
    }
    for (int i = 0; i < order.Size; i++)
    {
      const ImGuiAppNode* n = AppGraphFindNodeConst(g, order.Data[i]);
      if (n == nullptr) continue;
      const int begin = line_now();
      char base[IM_LABEL_SIZE];
      if (n->IsLive)
        AppLiveControlClassName(n, base, IM_ARRAYSIZE(base));
      else
        AppNodeBaseName(n, base, IM_ARRAYSIZE(base));
      const int parent = AppGraphParentOf(g, n->Id);
      const ImGuiAppNode* pn = parent >= 0 ? AppGraphFindNodeConst(g, parent) : nullptr;
      if (pn && pn->Kind == ImGuiAppNodeKind_Sidebar)
      {
        char pbase[IM_LABEL_SIZE]; AppNodeBaseName(pn, pbase, IM_ARRAYSIZE(pbase));
        out->appendf("  ImGui::PushSidebarControl<%s>(app, sb_%s); // hosted by %s\n", base, pbase, pn->Draft.Name);
        char expr[IM_LABEL_SIZE + 32]; ImFormatString(expr, IM_ARRAYSIZE(expr), "sb_%s->Controls.back()", pbase);
        AppEmitStyleModLines(n, expr, "  ", out);
      }
      else if (pn && pn->Kind == ImGuiAppNodeKind_Window)
      {
        char pbase[IM_LABEL_SIZE]; AppNodeBaseName(pn, pbase, IM_ARRAYSIZE(pbase));
        out->appendf("  ImGui::PushWindowControl<%s>(app, win_%s); // hosted by %s\n", base, pbase, pn->Draft.Name);
        char expr[IM_LABEL_SIZE + 32]; ImFormatString(expr, IM_ARRAYSIZE(expr), "win_%s->Controls.back()", pbase);
        AppEmitStyleModLines(n, expr, "  ", out);
      }
      else
      {
        out->appendf("  ImGui::PushAppControl<%s>(app);\n", base);
        AppEmitStyleModLines(n, "app->Controls.back()", "  ", out);
      }
      span(n->Id, begin);
    }
    out->appendf("}\n");
  }

  void GenerateAppNodeCode(const ImGuiAppGraph* g, const ImGuiAppNode* n, ImGuiTextBuffer* out, ImGuiApp* live_app)
  {
    IM_ASSERT(g != nullptr && n != nullptr && out != nullptr);

    switch (n->Kind)
    {
    case ImGuiAppNodeKind_Control:
    {
      ImGuiStorage mirrored_types;
      if (n->IsLive && AppEmitLiveControlCode(g, live_app, n, out, &mirrored_types))
        break;                                      // live: the real compiled shape, never a TODO template
      AppEmitControlWithDeps(g, n, out);            // data structs + control struct with derived deps/bindings
      break;
    }
    case ImGuiAppNodeKind_Layer:
      if (AppNodeIsCommandLayer(n))
        AppEmitCommandEnum(g, out);                 // just the AppCommand enum (the user's example)
      else if (n->LayerType == ImGuiAppLayerType_Custom)
      {
        char base[IM_LABEL_SIZE]; AppNodeBaseName(n, base, IM_ARRAYSIZE(base));
        if (n->IsLive)
          AppEmitLiveLayerCode(n, out);             // the running subclass's shell
        else
          AppEmitCustomLayerCode(n, out);           // the subclass skeleton this node names
        out->appendf("// bring-up (at this position in the layer stack):\nImGui::PushAppLayer<%s>(app);\n", base);
      }
      else
        out->appendf("// '%s' is a framework %s layer -- no generated type, just bring-up:\n"
                     "ImGui::PushAppLayer<%s>(app);\n",
                     n->Draft.Name, AppLayerTypeName(n->LayerType), AppLayerTypeName(n->LayerType));
      break;
    case ImGuiAppNodeKind_Window:
    {
      char base[IM_LABEL_SIZE]; AppNodeBaseName(n, base, IM_ARRAYSIZE(base));
      if (n->IsLive)
        AppEmitLiveHostCode(n, out);
      out->appendf("// Window '%s' bring-up:\nImGui::PushAppWindow<%s>(app);\n", n->Draft.Name, base);
      if (n->IsLive && n->Flags != 0)
        out->appendf("app->Windows.back()->Flags = (ImGuiWindowFlags)0x%X;\n", (unsigned)n->Flags);
      if (n->IsLive && n->HasInitialPlacement)
      {
        out->appendf("app->Windows.back()->HasInitialPlacement = true;\n");
        out->appendf("app->Windows.back()->InitialPos = ImVec2(%.1ff, %.1ff);\n", n->InitialPos.x, n->InitialPos.y);
        out->appendf("app->Windows.back()->InitialSize = ImVec2(%.1ff, %.1ff);\n", n->InitialSize.x, n->InitialSize.y);
      }
      AppEmitStyleModLines(n, "app->Windows.back()", "", out);
      break;
    }
    case ImGuiAppNodeKind_Sidebar:
    {
      char base[IM_LABEL_SIZE]; AppNodeBaseName(n, base, IM_ARRAYSIZE(base));
      if (n->IsLive)
        AppEmitLiveHostCode(n, out);
      char flags[32];
      if (n->IsLive && n->Flags != 0)
        ImFormatString(flags, IM_ARRAYSIZE(flags), "(ImGuiWindowFlags)0x%X", (unsigned)n->Flags);
      else
        ImStrncpy(flags, "ImGuiWindowFlags_None", IM_ARRAYSIZE(flags));
      out->appendf("// Sidebar '%s' bring-up:\nImGui::PushAppSidebar<%s>(app, vp, %s, %.1ff, %s);\n", n->Draft.Name, base, AppDirEnumName(n->DockDir), n->DockSize, flags);
      if (n->IsLive && n->HasInitialPlacement)
      {
        out->appendf("app->Sidebars.back()->HasInitialPlacement = true;\n");
        out->appendf("app->Sidebars.back()->InitialPos = ImVec2(%.1ff, %.1ff);\n", n->InitialPos.x, n->InitialPos.y);
        out->appendf("app->Sidebars.back()->InitialSize = ImVec2(%.1ff, %.1ff);\n", n->InitialSize.x, n->InitialSize.y);
      }
      AppEmitStyleModLines(n, "app->Sidebars.back()", "", out);
      break;
    }
    case ImGuiAppNodeKind_Struct:
      AppEmitStructCode(g, n, out);                  // standalone data struct
      break;
    case ImGuiAppNodeKind_App:
    default:
      GenerateAppGraphCode(g, out);                 // App node == the whole composition
      break;
    }

    if (out->size() == 0)
    {
      if (n->Kind == ImGuiAppNodeKind_Layer && AppNodeIsCommandLayer(n))
        out->appendf("// CommandLayer has no commands yet -- add commands to generate AppCommand.\n");
      else
        out->appendf("// no code generated for this node.\n");
    }
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Whole-graph persistence (SaveAppGraph / LoadAppGraph, legacy [Draft] ingest)
  //-----------------------------------------------------------------------------

  // Emit one [Node] record (shared by whole-graph and subset serialization).
  static void AppEmitNodeRecord(ImGuiTextBuffer* buf, const ImGuiAppNode* n)
  {
    buf->appendf("[Node]\n");
    buf->appendf("Id=%d\n", n->Id);
    buf->appendf("Kind=%d\n", (int)n->Kind);
    buf->appendf("Name=%s\n", n->Draft.Name);
    buf->appendf("Builtin=%d\n", n->IsBuiltin ? 1 : 0);
    buf->appendf("Type=%s\n", n->TypeName);
    buf->appendf("DataType=%s\n", n->DataTypeName);
    buf->appendf("LayerType=%d\n", (int)n->LayerType);
    buf->appendf("FieldList=%d\n", n->FieldList);
    buf->appendf("PersistStruct=%d\n", n->PersistStructId);
    buf->appendf("TempStruct=%d\n", n->TempStructId);
    buf->appendf("Body=%d\n", n->BodyAttrId);
    buf->appendf("Pos=%.1f,%.1f\n", n->GridPos.x, n->GridPos.y);
    if (n->Flags != 0)
      buf->appendf("Flags=%u\n", (unsigned)n->Flags);
    if (n->HasInitialPlacement)
      buf->appendf("Init=%.1f,%.1f,%.1f,%.1f\n", n->InitialPos.x, n->InitialPos.y, n->InitialSize.x, n->InitialSize.y);
    if (n->DockDir != ImGuiDir_Down || n->DockSize != 0.0f)
      buf->appendf("Dock=%d,%.1f\n", (int)n->DockDir, n->DockSize);
    for (int p = 0; p < n->Ports.Size; p++)
      buf->appendf("Port=%d,%d,%s,%u\n", n->Ports.Data[p].Id, (int)n->Ports.Data[p].Kind, n->Ports.Data[p].Name, (unsigned)n->Ports.Data[p].DataTypeId);
    for (int f = 0; f < n->Draft.PersistFields.Size; f++)
      buf->appendf("Persist=%s,%d,%d,%s\n", n->Draft.PersistFields.Data[f].Name, (int)n->Draft.PersistFields.Data[f].Type, n->Draft.PersistFields.Data[f].ArraySize, n->Draft.PersistFields.Data[f].StructType);
    for (int f = 0; f < n->Draft.TempFields.Size; f++)
      buf->appendf("Temp=%s,%d,%d,%s\n", n->Draft.TempFields.Data[f].Name, (int)n->Draft.TempFields.Data[f].Type, n->Draft.TempFields.Data[f].ArraySize, n->Draft.TempFields.Data[f].StructType);
    for (int c = 0; c < n->Commands.Size; c++)
      buf->appendf("Command=%s\n", n->Commands.Data[c].Name);
    for (int s = 0; s < n->StyleMods.Size; s++)
      buf->appendf("Style=%d,%g,%g,%d\n", (int)n->StyleMods.Data[s].Var, n->StyleMods.Data[s].Value.x, n->StyleMods.Data[s].Value.y, n->StyleMods.Data[s].Active ? 1 : 0);
    for (int s = 0; s < n->ColorMods.Size; s++)
      buf->appendf("Color=%d,%08X,%d\n", (int)n->ColorMods.Data[s].Col, n->ColorMods.Data[s].Value, n->ColorMods.Data[s].Active ? 1 : 0);
    // Expr is last: it is emitted verbatim into generated C++ and may itself contain commas.
    for (int e = 0; e < n->Events.Size; e++)
      buf->appendf("Event=%d,%d,%s,%s,%s,%s\n", (int)n->Events.Data[e].Edge, (int)n->Events.Data[e].Action,
                   n->Events.Data[e].TempField, n->Events.Data[e].DstField, n->Events.Data[e].Command, n->Events.Data[e].Expr);
  }

  // Parse "edge,action,temp,dst,cmd,expr" (text fields may be empty -- sscanf's %[^,] cannot match an empty
  // field, so split by hand; expr takes the remainder of the line, commas included).
  static void AppNodeParseEvent(ImGuiAppNode* n, const char* line)
  {
    ImGuiAppEventDesc ev;
    const char* s = line;
    ev.Edge = (ImGuiAppEventEdge)atoi(s);
    s = strchr(s, ',');
    if (s == nullptr) return;
    s++;
    ev.Action = (ImGuiAppEventAction)atoi(s);
    s = strchr(s, ',');
    if (s == nullptr) return;
    s++;
    char* bufs[3] = { ev.TempField, ev.DstField, ev.Command };
    for (int i = 0; i < 3; i++)
    {
      const char* e = strchr(s, ',');
      if (e == nullptr) return;
      int len = (int)(e - s);
      if (len > IM_LABEL_SIZE - 1) len = IM_LABEL_SIZE - 1;
      memcpy(bufs[i], s, (size_t)len);
      bufs[i][len] = 0;
      s = e + 1;
    }
    ImStrncpy(ev.Expr, s, IM_ARRAYSIZE(ev.Expr));
    n->Events.push_back(ev);
  }

  // Serialize the whole graph to the imgui-style text format (shared by SaveAppGraph for files and by the
  // in-memory undo snapshots). Positions are included so undo restores layout, not just topology.
  static void AppGraphSerialize(const ImGuiAppGraph* g, ImGuiTextBuffer* buf)
  {
    buf->appendf("[Graph]\n");
    buf->appendf("NextId=%d\n", g->NextId);
    for (int i = 0; i < g->Nodes.Size; i++)
      AppEmitNodeRecord(buf, &g->Nodes.Data[i]);
    for (int i = 0; i < g->Links.Size; i++)
      buf->appendf("Link=%d,%d,%d,%d\n", g->Links.Data[i].Id, g->Links.Data[i].StartAttr, g->Links.Data[i].EndAttr, (int)g->Links.Data[i].Kind);
    for (int i = 0; i < g->Bindings.Size; i++)
      buf->appendf("Bind=%d,%s,%s\n", g->Bindings.Data[i].LinkId, g->Bindings.Data[i].DstField, g->Bindings.Data[i].SrcField);
    for (int i = 0; i < g->ScopePlacements.Size; i++)
      buf->appendf("Place=%d,%d,%g,%g\n", g->ScopePlacements.Data[i].ScopeId, g->ScopePlacements.Data[i].NodeId,
                   g->ScopePlacements.Data[i].Pos.x, g->ScopePlacements.Data[i].Pos.y);
  }

  // The prefab registry (F04) rides in a sidecar file beside the graph ("<graph>.prefabs"). Each entry
  // is a name plus a serialized subtree -- itself multi-line -- fenced by <<< / >>> so the subtree's own
  // newlines survive the line-based format.
  static void AppGraphPrefabSidecarPath(const char* graph_path, char* out, int out_size)
  {
    ImFormatString(out, (size_t)out_size, "%s.prefabs", graph_path);
  }

  static void AppGraphSerializePrefabs(const ImGuiAppGraph* g, ImGuiTextBuffer* buf)
  {
    const ImVector<ImGuiAppPrefab>& prefabs = AppGraphEditorState(g)->Prefabs;
    for (int i = 0; i < prefabs.Size; i++)
    {
      buf->appendf("[Prefab]\nName=%s\n<<<\n", prefabs.Data[i].Name);
      const char* d = prefabs.Data[i].Data;
      const size_t dl = d ? strlen(d) : 0;
      if (dl > 0)
        buf->append(d);
      if (dl == 0 || d[dl - 1] != '\n')   // guarantee >>> sits on its own line
        buf->appendf("\n");
      buf->appendf(">>>\n");
    }
  }

  static void AppGraphDeserializePrefabs(ImGuiAppGraph* g, char* data)
  {
    ImVector<ImGuiAppPrefab>* prefabs = &AppGraphEditorState(g)->Prefabs;
    for (int i = 0; i < prefabs->Size; i++)
      IM_FREE(prefabs->Data[i].Data);
    prefabs->clear();

    char name[64] = "";
    ImGuiTextBuffer body;
    bool in_body = false;
    char* p = data;
    while (*p)
    {
      char* eol = p;
      while (*eol != 0 && *eol != '\n') eol++;
      const char saved = *eol;
      *eol = 0;
      if (eol > p && eol[-1] == '\r') eol[-1] = 0;

      if (in_body && strcmp(p, ">>>") == 0)
      {
        in_body = false;
        ImGuiAppPrefab pf;
        ImStrncpy(pf.Name, name[0] ? name : "prefab", IM_ARRAYSIZE(pf.Name));
        const char* bs = body.c_str();
        const size_t bl = strlen(bs);
        char* dup = (char*)IM_ALLOC(bl + 1);
        memcpy(dup, bs, bl);
        dup[bl] = 0;
        pf.Data = dup;
        prefabs->push_back(pf);
        body.clear();
      }
      else if (in_body)
      {
        body.append(p);
        body.appendf("\n");
      }
      else if (strncmp(p, "Name=", 5) == 0)
      {
        ImStrncpy(name, p + 5, IM_ARRAYSIZE(name));
      }
      else if (strcmp(p, "<<<") == 0)
      {
        in_body = true;
        body.clear();
      }
      // "[Prefab]" itself needs no handling: Name= carries the name, <<< opens the body.

      if (saved == 0) break;
      p = eol + 1;
    }
  }

  bool SaveAppGraph(const char* path, const ImGuiAppGraph* g)
  {
    IM_ASSERT(path != nullptr && g != nullptr);

    ImGuiTextBuffer buf;
    AppGraphSerialize(g, &buf);

    ImFileHandle fh = ImFileOpen(path, "wt");
    if (fh == nullptr)
      return false;
    ImFileWrite(buf.c_str(), sizeof(char), (ImU64)buf.size(), fh);
    ImFileClose(fh);

    // Prefab registry sidecar (F04): written only when non-empty.
    if (AppGraphPrefabCount(g) > 0)
    {
      char sidecar[1024];
      AppGraphPrefabSidecarPath(path, sidecar, IM_ARRAYSIZE(sidecar));
      ImGuiTextBuffer pbuf;
      AppGraphSerializePrefabs(g, &pbuf);
      if (ImFileHandle pfh = ImFileOpen(sidecar, "wt"))
      {
        ImFileWrite(pbuf.c_str(), sizeof(char), (ImU64)pbuf.size(), pfh);
        ImFileClose(pfh);
      }
    }
    return true;
  }

  // Parse "id,kind,name,datatype" into a port record.
  static void AppGraphParsePort(ImGuiAppNode* n, const char* line)
  {
    ImGuiAppNodePort p;
    int id = 0, kind = 0; unsigned tid = 0;
    if (sscanf(line, "%d,%d,%255[^,],%u", &id, &kind, p.Name, &tid) >= 3)
    {
      p.Id = id;
      p.Kind = (ImGuiAppPortKind)kind;
      p.DataTypeId = (ImGuiID)tid;
      n->Ports.push_back(p);
    }
  }

  // Synthesize the standard ports for a legacy [Draft]-migrated Control node (no [Port] lines on disk).
  static void AppGraphSynthesizeControlPorts(ImGuiAppGraph* g, ImGuiAppNode* n)
  {
    AppGraphStampPorts(g, n);
  }

  static bool AppNodeKeepsPortKind(const ImGuiAppNode* n, ImGuiAppPortKind kind)
  {
    switch (n->Kind)
    {
    case ImGuiAppNodeKind_App:
      return kind == ImGuiAppPortKind_ChildIn;
    case ImGuiAppNodeKind_Layer:
      return false;
    case ImGuiAppNodeKind_Window:
    case ImGuiAppNodeKind_Sidebar:
      return kind == ImGuiAppPortKind_ChildIn || kind == ImGuiAppPortKind_ChildOut;
    case ImGuiAppNodeKind_Struct:
      return kind == ImGuiAppPortKind_ChildIn || kind == ImGuiAppPortKind_DataOut;
    case ImGuiAppNodeKind_Field:
      return kind == ImGuiAppPortKind_DataOut || kind == ImGuiAppPortKind_ChildOut;
    case ImGuiAppNodeKind_Control:
    default:
      return kind == ImGuiAppPortKind_DataIn || kind == ImGuiAppPortKind_DataOut || kind == ImGuiAppPortKind_ChildOut;
    }
  }

  static void AppGraphNormalizeLoadedPorts(ImGuiAppGraph* g)
  {
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      ImGuiAppNode* n = &g->Nodes.Data[i];
      for (int p = n->Ports.Size - 1; p >= 0; p--)
        if (!AppNodeKeepsPortKind(n, n->Ports.Data[p].Kind))
          n->Ports.erase(n->Ports.Data + p);
    }
  }

  // Parse a serialized graph (mutable in-memory text, NUL-terminated) into g, replacing its contents. Shared by
  // LoadAppGraph (file) and the in-memory undo snapshots. Does not free `data`.
  static void AppGraphDeserialize(ImGuiAppGraph* g, char* data)
  {
    g->Nodes.clear();
    g->Links.clear();
    g->Bindings.clear();
    g->ScopePlacements.clear();
    g->Selection.clear();
    g->NextId = 1;
    g->EditingNodeId = -1;

    bool is_legacy = true;          // until we see a [Graph]/[Node] header
    bool legacy_needs_ports = false;
    ImGuiAppNode* cur = nullptr;
    int max_id = 0;

    char* p = data;
    while (*p)
    {
      char* eol = p;
      while (*eol != 0 && *eol != '\n') eol++;
      const char saved = *eol;
      *eol = 0;
      if (eol > p && eol[-1] == '\r') eol[-1] = 0;

      if (strncmp(p, "[Graph]", 7) == 0)
      {
        is_legacy = false;
      }
      else if (strncmp(p, "NextId=", 7) == 0)
      {
        g->NextId = atoi(p + 7);
        is_legacy = false;
      }
      else if (strncmp(p, "[Node]", 6) == 0)
      {
        is_legacy = false;
        if (cur && legacy_needs_ports) { AppGraphSynthesizeControlPorts(g, cur); legacy_needs_ports = false; }
        g->Nodes.push_back(ImGuiAppNode());
        cur = &g->Nodes.back();
        cur->Kind = ImGuiAppNodeKind_Control;
      }
      else if (strncmp(p, "[Draft]", 7) == 0)
      {
        // Legacy single/multi draft -> a Control node; ports are synthesized when the node closes.
        if (cur && legacy_needs_ports) { AppGraphSynthesizeControlPorts(g, cur); legacy_needs_ports = false; }
        g->Nodes.push_back(ImGuiAppNode());
        cur = &g->Nodes.back();
        cur->Kind = ImGuiAppNodeKind_Control;
        cur->Id = g->NextId++;
        cur->BodyAttrId = g->NextId++;
        const int idx = g->Nodes.Size - 1;
        cur->GridPos = ImVec2(40.0f + (idx % 5) * 36.0f, 200.0f + (idx % 5) * 36.0f);
        cur->HasGridPos = true; cur->_NeedsPlace = true;
        legacy_needs_ports = true;
      }
      else if (strncmp(p, "Id=", 3) == 0)        { if (cur) { cur->Id = atoi(p + 3); if (cur->Id > max_id) max_id = cur->Id; } }
      else if (strncmp(p, "Kind=", 5) == 0)      { if (cur) cur->Kind = (ImGuiAppNodeKind)atoi(p + 5); }
      else if (strncmp(p, "Name=", 5) == 0)      { if (cur) ImStrncpy(cur->Draft.Name, p + 5, IM_ARRAYSIZE(cur->Draft.Name)); }
      else if (strncmp(p, "Builtin=", 8) == 0)   { if (cur) cur->IsBuiltin = atoi(p + 8) != 0; }
      else if (strncmp(p, "Type=", 5) == 0)      { if (cur) ImStrncpy(cur->TypeName, p + 5, IM_ARRAYSIZE(cur->TypeName)); }
      else if (strncmp(p, "DataType=", 9) == 0)  { if (cur) ImStrncpy(cur->DataTypeName, p + 9, IM_ARRAYSIZE(cur->DataTypeName)); }
      else if (strncmp(p, "LayerType=", 10) == 0){ if (cur) cur->LayerType = (ImGuiAppLayerType)atoi(p + 10); }
      else if (strncmp(p, "FieldList=", 10) == 0)    { if (cur) cur->FieldList = atoi(p + 10); }
      else if (strncmp(p, "PersistStruct=", 14) == 0){ if (cur) cur->PersistStructId = atoi(p + 14); }
      else if (strncmp(p, "TempStruct=", 11) == 0)   { if (cur) cur->TempStructId = atoi(p + 11); }
      else if (strncmp(p, "Body=", 5) == 0)      { if (cur) { cur->BodyAttrId = atoi(p + 5); if (cur->BodyAttrId > max_id) max_id = cur->BodyAttrId; } }
      else if (strncmp(p, "Pos=", 4) == 0)       { if (cur) { float x = 0, y = 0; if (sscanf(p + 4, "%f,%f", &x, &y) == 2) { cur->GridPos = ImVec2(x, y); cur->HasGridPos = true; cur->_NeedsPlace = true; } } }
      else if (strncmp(p, "Flags=", 6) == 0)     { if (cur) { unsigned fl = 0; if (sscanf(p + 6, "%u", &fl) == 1) cur->Flags = (ImGuiWindowFlags)fl; } }
      else if (strncmp(p, "Init=", 5) == 0)      { if (cur) { float px = 0, py = 0, sx = 0, sy = 0; if (sscanf(p + 5, "%f,%f,%f,%f", &px, &py, &sx, &sy) == 4) { cur->HasInitialPlacement = true; cur->InitialPos = ImVec2(px, py); cur->InitialSize = ImVec2(sx, sy); } } }
      else if (strncmp(p, "Dock=", 5) == 0)      { if (cur) { int d = 0; float sz = 0; if (sscanf(p + 5, "%d,%f", &d, &sz) >= 1) { cur->DockDir = (ImGuiDir)d; cur->DockSize = sz; } } }
      else if (strncmp(p, "Port=", 5) == 0)      { if (cur) { AppGraphParsePort(cur, p + 5); int last = cur->Ports.Size ? cur->Ports.Data[cur->Ports.Size - 1].Id : 0; if (last > max_id) max_id = last; } }
      else if (strncmp(p, "Persist=", 8) == 0)   { if (cur) AppNodeParseField(&cur->Draft.PersistFields, p + 8); }
      else if (strncmp(p, "Temp=", 5) == 0)      { if (cur) AppNodeParseField(&cur->Draft.TempFields, p + 5); }
      else if (strncmp(p, "Command=", 8) == 0)   { if (cur) AppNodeAddCommand(cur, p + 8); }
      else if (strncmp(p, "Event=", 6) == 0)     { if (cur) AppNodeParseEvent(cur, p + 6); }
      else if (strncmp(p, "Style=", 6) == 0)
      {
        int var = 0, active = 1; float x = 0.0f, y = 0.0f;
        if (cur && sscanf(p + 6, "%d,%f,%f,%d", &var, &x, &y, &active) >= 3 && var >= 0 && var < ImGuiStyleVar_COUNT)
          cur->StyleMods.push_back(ImGuiAppStyleModDesc{ (ImGuiStyleVar)var, ImVec2(x, y), active != 0 });
      }
      else if (strncmp(p, "Color=", 6) == 0)
      {
        int col = 0, active = 1; unsigned val = 0;
        if (cur && sscanf(p + 6, "%d,%X,%d", &col, &val, &active) >= 2 && col >= 0 && col < ImGuiCol_COUNT)
          cur->ColorMods.push_back(ImGuiAppColorModDesc{ (ImGuiCol)col, (ImU32)val, active != 0 });
      }
      else if (strncmp(p, "Link=", 5) == 0)
      {
        ImGuiAppNodeLink l; int kind = ImGuiAppEdgeKind_Data;
        const int got = sscanf(p + 5, "%d,%d,%d,%d", &l.Id, &l.StartAttr, &l.EndAttr, &kind);
        if (got >= 3) { l.Kind = (ImGuiAppEdgeKind)kind; g->Links.push_back(l); if (l.Id > max_id) max_id = l.Id; }
      }
      else if (strncmp(p, "Place=", 6) == 0)
      {
        ImGuiAppScopePlacement pl;
        if (sscanf(p + 6, "%d,%d,%f,%f", &pl.ScopeId, &pl.NodeId, &pl.Pos.x, &pl.Pos.y) == 4)
          g->ScopePlacements.push_back(pl);
      }
      else if (strncmp(p, "Bind=", 5) == 0)
      {
        ImGuiAppFieldBinding b;
        if (sscanf(p + 5, "%d,%255[^,],%255[^\n]", &b.LinkId, b.DstField, b.SrcField) >= 1)
          g->Bindings.push_back(b);
      }

      if (saved == 0) break;
      p = eol + 1;
    }
    if (cur && legacy_needs_ports)
      AppGraphSynthesizeControlPorts(g, cur);

    AppGraphNormalizeLoadedPorts(g);

    // Drop links whose endpoints don't resolve to any port (e.g. legacy inert attr ids that no synthesized
    // port matches), so the loaded model is always self-consistent.
    for (int li = g->Links.Size - 1; li >= 0; li--)
    {
      ImGuiAppNode* dummy = nullptr;
      const bool ok = AppGraphFindPort(g, g->Links.Data[li].StartAttr, &dummy) != nullptr
                   && AppGraphFindPort(g, g->Links.Data[li].EndAttr, &dummy) != nullptr;
      if (!ok)
      {
        const int link_id = g->Links.Data[li].Id;
        for (int bi = g->Bindings.Size - 1; bi >= 0; bi--)
          if (g->Bindings.Data[bi].LinkId == link_id) g->Bindings.erase(g->Bindings.Data + bi);
        g->Links.erase(g->Links.Data + li);
      }
    }

    // Ensure NextId stays ahead of every id present (legacy files carried none).
    if (g->NextId <= max_id)
      g->NextId = max_id + 1;
    IM_UNUSED(is_legacy);
  }

  bool LoadAppGraph(const char* path, ImGuiAppGraph* g)
  {
    IM_ASSERT(path != nullptr && g != nullptr);

    size_t data_size = 0;
    char* data = (char*)ImFileLoadToMemory(path, "rb", &data_size, 1);
    if (data == nullptr)
      return false;

    AppGraphDeserialize(g, data);
    IM_FREE(data);

    // Load the prefab registry sidecar if present (F04): the prefabs authored beside this graph.
    char sidecar[1024];
    AppGraphPrefabSidecarPath(path, sidecar, IM_ARRAYSIZE(sidecar));
    size_t psize = 0;
    if (char* pdata = (char*)ImFileLoadToMemory(sidecar, "rb", &psize, 1))
    {
      AppGraphDeserializePrefabs(g, pdata);
      IM_FREE(pdata);
    }
    return true;
  }

  bool AppGraphGenerateToFiles(const char* graph_path, const char* out_header_path)
  {
    IM_ASSERT(graph_path != nullptr && out_header_path != nullptr);

    ImGuiAppGraph g;
    if (!LoadAppGraph(graph_path, &g))
      return false;

    ImGuiTextBuffer buf;
    GenerateAppGraphCode(&g, &buf);

    ImFileHandle fh = ImFileOpen(out_header_path, "wt");
    if (fh == nullptr)
      return false;
    ImFileWrite(buf.c_str(), sizeof(char), (ImU64)buf.size(), fh);
    ImFileClose(fh);
    return true;
  }

  // Split a buffer into NUL-terminated lines in place (replaces '\n', strips '\r'); fills `lines` with pointers.
  static void AppDiffSplitLines(char* text, ImVector<const char*>* lines)
  {
    char* p = text;
    lines->push_back(p);
    while (*p != 0)
    {
      if (*p == '\n')
      {
        *p = 0;
        if (p > text && p[-1] == '\r') p[-1] = 0;
        lines->push_back(p + 1);
      }
      p++;
    }
  }

  void AppGraphDiffCode(const ImGuiAppGraph* a, const ImGuiAppGraph* b, ImGuiTextBuffer* out)
  {
    IM_ASSERT(a != nullptr && b != nullptr && out != nullptr);

    ImGuiTextBuffer ca, cb;
    GenerateAppGraphCode(a, &ca);
    GenerateAppGraphCode(b, &cb);

    // Mutable copies for in-place line splitting.
    const int la = ca.size();
    const int lb = cb.size();
    char* ta = (char*)IM_ALLOC((size_t)la + 1);
    char* tb = (char*)IM_ALLOC((size_t)lb + 1);
    memcpy(ta, ca.c_str(), (size_t)la + 1);
    memcpy(tb, cb.c_str(), (size_t)lb + 1);

    ImVector<const char*> A, B;
    AppDiffSplitLines(ta, &A);
    AppDiffSplitLines(tb, &B);

    // LCS DP over lines (sizes are small -- generated headers, not whole programs).
    const int n = A.Size;
    const int m = B.Size;
    ImVector<int> dp;
    dp.resize((n + 1) * (m + 1));
    for (int i = 0; i <= n; i++) dp[i * (m + 1) + m] = 0;
    for (int j = 0; j <= m; j++) dp[n * (m + 1) + j] = 0;
    for (int i = n - 1; i >= 0; i--)
      for (int j = m - 1; j >= 0; j--)
      {
        if (strcmp(A[i], B[j]) == 0)
          dp[i * (m + 1) + j] = dp[(i + 1) * (m + 1) + (j + 1)] + 1;
        else
          dp[i * (m + 1) + j] = ImMax(dp[(i + 1) * (m + 1) + j], dp[i * (m + 1) + (j + 1)]);
      }

    int i = 0, j = 0;
    int added = 0, removed = 0;
    while (i < n && j < m)
    {
      if (strcmp(A[i], B[j]) == 0)
      {
        out->appendf("  %s\n", A[i]);
        i++; j++;
      }
      else if (dp[(i + 1) * (m + 1) + j] >= dp[i * (m + 1) + (j + 1)])
      {
        out->appendf("- %s\n", A[i]);
        removed++; i++;
      }
      else
      {
        out->appendf("+ %s\n", B[j]);
        added++; j++;
      }
    }
    while (i < n) { out->appendf("- %s\n", A[i]); removed++; i++; }
    while (j < m) { out->appendf("+ %s\n", B[j]); added++; j++; }

    if (added == 0 && removed == 0)
      out->appendf("\n(no differences)\n");
    else
      out->appendf("\n%d added, %d removed\n", added, removed);

    IM_FREE(ta);
    IM_FREE(tb);
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Round-trip: parse C++ struct blocks back into Struct nodes
  //-----------------------------------------------------------------------------

  static bool AppCImportIsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
  static bool AppCImportIsIdent(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

  // Map a C++ type spelling back to a field type. Unknown spellings become a Struct reference carrying the name.
  static ImGuiAppFieldType AppFieldTypeFromCpp(const char* type, bool is_array, char* struct_type_out, int out_sz)
  {
    struct_type_out[0] = 0;
    if (strcmp(type, "float") == 0)  return ImGuiAppFieldType_Float;
    if (strcmp(type, "int") == 0)    return ImGuiAppFieldType_Int;
    if (strcmp(type, "unsigned") == 0 || strcmp(type, "unsigned int") == 0 || strcmp(type, "ImU32") == 0 || strcmp(type, "ImGuiID") == 0) return ImGuiAppFieldType_Int;
    if (strcmp(type, "bool") == 0)   return ImGuiAppFieldType_Bool;
    if (strcmp(type, "double") == 0) return ImGuiAppFieldType_Double;
    if (strcmp(type, "ImVec2") == 0) return ImGuiAppFieldType_Vec2;
    if (strcmp(type, "ImVec4") == 0) return ImGuiAppFieldType_Vec4;
    if (strcmp(type, "char") == 0 && is_array) return ImGuiAppFieldType_String;
    ImStrncpy(struct_type_out, type, out_sz);
    return ImGuiAppFieldType_Struct;
  }

  // Parse one "<type...> <name>[<N>]" member (text in [m, end)) and append it to the struct draft. Skips methods
  // (parentheses), access specifiers, and anything without a clear "type name" shape.
  static void AppImportParseMember(const char* m, const char* end, ImVector<ImGuiAppFieldDesc>* fields)
  {
    char buf[256];
    int  bi = 0;
    for (const char* c = m; c < end && bi < (int)sizeof(buf) - 1; c++)
      buf[bi++] = *c;
    buf[bi] = 0;

    // Drop any initializer, and reject methods / labels / using-decls outright.
    if (char* eq = strchr(buf, '='))
      *eq = 0;
    if (strchr(buf, '(') != nullptr || strchr(buf, ':') != nullptr)
      return;

    // Pull out an array size and strip the [..] so the trailing token is a clean name.
    int  array_size = 0;
    bool is_array = false;
    if (char* lb = strchr(buf, '['))
    {
      is_array = true;
      array_size = atoi(lb + 1);
      *lb = 0;
    }

    // Tokenize by whitespace / '*' / '&'. Last token = field name, the rest joined = the type spelling.
    char* words[16];
    int   word_count = 0;
    char* w = buf;
    while (*w != 0 && word_count < 16)
    {
      while (*w != 0 && (AppCImportIsSpace(*w) || *w == '*' || *w == '&'))
      {
        *w = 0;
        w++;
      }
      if (*w == 0)
        break;
      words[word_count++] = w;
      while (*w != 0 && !AppCImportIsSpace(*w) && *w != '*' && *w != '&') w++;
    }
    if (word_count < 2)
      return;   // need at least a type and a name

    const char* name = words[word_count - 1];
    if (!AppCImportIsIdent(name[0]) || (name[0] >= '0' && name[0] <= '9'))
      return;
    if (strcmp(name, "struct") == 0 || strcmp(name, "class") == 0)
      return;

    char type[IM_LABEL_SIZE];
    int  ti = 0;
    type[0] = 0;
    for (int i = 0; i < word_count - 1; i++)
    {
      if (strcmp(words[i], "const") == 0 || strcmp(words[i], "struct") == 0)
        continue;
      if (ti > 0 && ti < IM_LABEL_SIZE - 1)
        type[ti++] = ' ';
      for (const char* c = words[i]; *c != 0 && ti < IM_LABEL_SIZE - 1; c++)
        type[ti++] = *c;
    }
    type[ti] = 0;
    if (type[0] == 0)
      return;

    ImGuiAppFieldDesc fd;
    ImStrncpy(fd.Name, name, IM_ARRAYSIZE(fd.Name));
    fd.Type = AppFieldTypeFromCpp(type, is_array, fd.StructType, IM_ARRAYSIZE(fd.StructType));
    fd.ArraySize = (fd.Type == ImGuiAppFieldType_String) ? (array_size > 0 ? array_size : 32) : 0;
    fields->push_back(fd);
  }

  int AppGraphImportStructsFromCode(ImGuiAppGraph* g, const char* code, ImVec2 origin)
  {
    IM_ASSERT(g != nullptr);
    if (code == nullptr)
      return 0;

    int added = 0;
    const char* p = code;
    while ((p = strstr(p, "struct")) != nullptr)
    {
      // Require word boundaries around the keyword.
      if (p != code && AppCImportIsIdent(p[-1]))     { p += 6; continue; }
      const char* q = p + 6;
      if (!AppCImportIsSpace(*q))                    { p = q; continue; }
      while (AppCImportIsSpace(*q)) q++;

      char name[IM_LABEL_SIZE];
      int  ni = 0;
      while (AppCImportIsIdent(*q) && ni < IM_LABEL_SIZE - 1) name[ni++] = *q++;
      name[ni] = 0;
      while (AppCImportIsSpace(*q)) q++;
      if (ni == 0 || *q != '{')                      { p = q; continue; }   // fwd-decl / inheritance -> skip

      const char* body = q + 1;
      const char* e = body;
      int depth = 1;
      while (*e != 0 && depth > 0)
      {
        if (*e == '{') depth++;
        else if (*e == '}') { depth--; if (depth == 0) break; }
        e++;
      }

      ImGuiAppNode* s = AppGraphAddNode(g, ImGuiAppNodeKind_Struct, name);
      ImVec2 pos = origin + ImVec2((float)(added % 4) * 300.0f, (float)(added / 4) * 220.0f);
      AppGraphPlaceNode(g, s, &pos);

      const char* m = body;
      while (m < e)
      {
        const char* c = m;
        while (c < e && *c != ';' && *c != '{')
          c++;
        if (c < e && *c == '{')
        {
          // Nested block (method/nested type): skip to its matching brace, then past a trailing ';'.
          int d2 = 1;
          c++;
          while (c < e && d2 > 0) { if (*c == '{') d2++; else if (*c == '}') d2--; c++; }
          while (c < e && *c != ';') c++;
          m = (c < e) ? c + 1 : e;
          continue;
        }
        AppImportParseMember(m, c, &s->Draft.PersistFields);
        m = c + 1;
      }

      added++;
      p = (*e != 0) ? e + 1 : e;
    }
    return added;
  }

  // Trim leading/trailing import whitespace in place.
  static void AppImportTrim(char* s)
  {
    char* b = s;
    while (*b != 0 && AppCImportIsSpace(*b)) b++;
    if (b != s) memmove(s, b, strlen(b) + 1);
    int n = (int)strlen(s);
    while (n > 0 && AppCImportIsSpace(s[n - 1])) s[--n] = 0;
  }

  // Locate `struct <name> { ... }` (a plain struct, no base) and return the body span (inside the braces).
  // Skips inheriting structs (control shells), so the Data/TempData block is found, not the control itself.
  static bool AppImportFindNamedStruct(const char* code, const char* name, const char** out_body, const char** out_end)
  {
    const char* p = code;
    while ((p = strstr(p, "struct")) != nullptr)
    {
      if (p != code && AppCImportIsIdent(p[-1]))     { p += 6; continue; }
      const char* q = p + 6;
      if (!AppCImportIsSpace(*q))                    { p = q; continue; }
      while (AppCImportIsSpace(*q)) q++;
      char nm[IM_LABEL_SIZE];
      int  ni = 0;
      while (AppCImportIsIdent(*q) && ni < IM_LABEL_SIZE - 1) nm[ni++] = *q++;
      nm[ni] = 0;
      while (AppCImportIsSpace(*q)) q++;
      if (*q != '{' || strcmp(nm, name) != 0)        { p = q; continue; }   // fwd-decl / inheriting / wrong name
      const char* body = q + 1;
      const char* e = body;
      int depth = 1;
      while (*e != 0 && depth > 0) { if (*e == '{') depth++; else if (*e == '}') { depth--; if (depth == 0) break; } e++; }
      *out_body = body;
      *out_end = e;
      return true;
    }
    return false;
  }

  // Parse the `type name;` members of a struct body span into fields (shared by struct + control import).
  static void AppImportParseStructMembers(const char* body, const char* e, ImVector<ImGuiAppFieldDesc>* fields)
  {
    const char* m = body;
    while (m < e)
    {
      const char* c = m;
      while (c < e && *c != ';' && *c != '{') c++;
      if (c < e && *c == '{')
      {
        int d2 = 1;
        c++;
        while (c < e && d2 > 0) { if (*c == '{') d2++; else if (*c == '}') d2--; c++; }
        while (c < e && *c != ';') c++;
        m = (c < e) ? c + 1 : e;
        continue;
      }
      AppImportParseMember(m, c, fields);
      m = c + 1;
    }
  }

  // strstr bounded to [b, e): the leftmost occurrence of needle that starts before e, or null.
  static const char* AppImportFind(const char* b, const char* e, const char* needle)
  {
    const char* s = strstr(b, needle);
    return (s != nullptr && s < e) ? s : nullptr;
  }

  // Find `<method>(...) ... {` inside [b, e) and return its brace body span. Skips the arg-list parens so
  // the body brace is the method's, not something inside the signature.
  static bool AppImportMethodBody(const char* b, const char* e, const char* method, const char** out_body, const char** out_end)
  {
    const char* s = b;
    while ((s = AppImportFind(s, e, method)) != nullptr)
    {
      const char* a = s + strlen(method);
      while (a < e && AppCImportIsSpace(*a)) a++;
      if (a >= e || *a != '(')                       { s += strlen(method); continue; }   // not a call/def
      int pd = 0;
      const char* c = a;
      while (c < e) { if (*c == '(') pd++; else if (*c == ')') { pd--; if (pd == 0) { c++; break; } } c++; }
      while (c < e && *c != '{' && *c != ';') c++;
      if (c >= e || *c != '{')                       { s += strlen(method); continue; }
      const char* body = c + 1;
      const char* be = body;
      int depth = 1;
      while (be < e && depth > 0) { if (*be == '{') depth++; else if (*be == '}') { depth--; if (depth == 0) break; } be++; }
      *out_body = body;
      *out_end = be;
      return true;
    }
    return false;
  }

  // Parse an event guard condition in [cond, cond_end) to (edge, temp field). The emitter's guard shapes:
  // rising `temp && !last`, falling `!temp && last`, changed `temp ^ last` / `temp != last`, active `temp`.
  static bool AppImportParseGuard(const char* cond, const char* cond_end, ImGuiAppEventEdge* out_edge, char* out_temp, int temp_size)
  {
    const char* t = AppImportFind(cond, cond_end, "temp_data->");   // leftmost is always the real temp field
    if (t == nullptr) return false;
    const bool temp_negated = (t > cond && t[-1] == '!');
    t += 11;
    int i = 0;
    while (t < cond_end && AppCImportIsIdent(*t) && i < temp_size - 1) out_temp[i++] = *t++;
    out_temp[i] = 0;
    if (out_temp[0] == 0) return false;

    const bool has_last = AppImportFind(cond, cond_end, "last_temp_data->") != nullptr;
    if (!has_last)                                             { *out_edge = ImGuiAppEventEdge_Active; return true; }
    if (AppImportFind(cond, cond_end, "^") || AppImportFind(cond, cond_end, "!="))
      *out_edge = ImGuiAppEventEdge_Changed;
    else if (temp_negated)
      *out_edge = ImGuiAppEventEdge_Falling;
    else
      *out_edge = ImGuiAppEventEdge_Rising;
    return true;
  }

  // Merge policy (F24): imports find an existing node of the same kind + name and update it in place;
  // otherwise they create. So importing the same source twice equals importing it once.
  static ImGuiAppNode* AppImportFindNode(ImGuiAppGraph* g, ImGuiAppNodeKind kind, const char* name)
  {
    for (int i = 0; i < g->Nodes.Size; i++)
      if (g->Nodes.Data[i].Kind == kind && !g->Nodes.Data[i].IsLive && strcmp(g->Nodes.Data[i].Draft.Name, name) == 0)
        return &g->Nodes.Data[i];
    return nullptr;
  }

  // Add a link only if an identical one (endpoints + kind) is absent, so re-import does not double edges.
  static bool AppImportAddLinkUnique(ImGuiAppGraph* g, int start, int end, ImGuiAppEdgeKind kind)
  {
    for (int i = 0; i < g->Links.Size; i++)
      if (g->Links.Data[i].StartAttr == start && g->Links.Data[i].EndAttr == end && g->Links.Data[i].Kind == kind)
        return false;
    ImGuiAppNodeLink l;
    l.Id = AppGraphAllocId(g);
    l.StartAttr = start;
    l.EndAttr = end;
    l.Kind = kind;
    g->Links.push_back(l);
    return true;
  }

  int AppGraphImportControlsFromCode(ImGuiAppGraph* g, const char* code, ImVec2 origin)
  {
    IM_ASSERT(g != nullptr);
    if (code == nullptr)
      return 0;

    // Command selections referenced in a control body appear as `(ImGuiAppCommand)AppCommand_<name>` (real
    // for event emissions, commented for a bare selection). Collect the distinct names onto the control.
    auto import_commands = [](ImGuiAppNode* ctrl, const char* b, const char* en)
    {
      static const char* needle = "(ImGuiAppCommand)AppCommand_";
      const int nl = (int)strlen(needle);
      const char* s = b;
      while (s < en && (s = strstr(s, needle)) != nullptr && s < en)
      {
        const char* id = s + nl;
        char cmd[IM_LABEL_SIZE];
        int  ci = 0;
        while (id < en && AppCImportIsIdent(*id) && ci < IM_LABEL_SIZE - 1) cmd[ci++] = *id++;
        cmd[ci] = 0;
        bool dup = cmd[0] == 0;
        for (int k = 0; k < ctrl->Commands.Size && !dup; k++)
          dup = strcmp(ctrl->Commands.Data[k].Name, cmd) == 0;
        if (!dup)
          AppNodeAddCommand(ctrl, cmd);
        s = id;
      }
    };

    // Reconstruct events from the two method bodies. OnGetCommand yields Active EmitCommand events (their
    // `if (temp_data->X)` guard) plus a latch->command map (`if (data-><Cmd>Pending)`); OnUpdate's events
    // block yields SetField events (`data->dst = <expr>`) and edge EmitCommand events (`data-><latch> = true`
    // under an edge guard, command resolved via that map). The `= false` re-arm lines carry no guard, so the
    // if-scan skips them.
    auto import_events = [](ImGuiAppNode* ctrl, const char* cb, const char* ce)
    {
      struct LatchCmd { char Latch[IM_LABEL_SIZE + 16]; char Cmd[IM_LABEL_SIZE]; };
      ImVector<LatchCmd> latches;

      const char* gb = nullptr;
      const char* ge = nullptr;
      if (AppImportMethodBody(cb, ce, "OnGetCommand", &gb, &ge))
      {
        const char* s = gb;
        while ((s = AppImportFind(s, ge, "if (")) != nullptr)
        {
          const char* cond = s + 4;
          const char* cend = cond;
          int pd = 1;
          while (cend < ge && pd > 0) { if (*cend == '(') pd++; else if (*cend == ')') { pd--; if (pd == 0) break; } cend++; }
          const char* emit = AppImportFind(cend, ge, "(ImGuiAppCommand)AppCommand_");
          const char* next_if = AppImportFind(cend, ge, "if (");
          s = cend + 1;
          if (emit == nullptr) continue;
          if (next_if != nullptr && emit > next_if) continue;   // guard has no emission of its own
          const char* id = emit + strlen("(ImGuiAppCommand)AppCommand_");
          char cmd[IM_LABEL_SIZE];
          int  ci = 0;
          while (id < ge && AppCImportIsIdent(*id) && ci < IM_LABEL_SIZE - 1) cmd[ci++] = *id++;
          cmd[ci] = 0;
          if (cmd[0] == 0) continue;

          if (AppImportFind(cond, cend, "temp_data->") != nullptr)
          {
            char tf[IM_LABEL_SIZE];
            ImGuiAppEventEdge edge;
            if (AppImportParseGuard(cond, cend, &edge, tf, IM_ARRAYSIZE(tf)))
            {
              ImGuiAppEventDesc ev;
              ev.Edge = ImGuiAppEventEdge_Active;
              ev.Action = ImGuiAppEventAction_EmitCommand;
              ImStrncpy(ev.TempField, tf, IM_ARRAYSIZE(ev.TempField));
              ImStrncpy(ev.Command, cmd, IM_ARRAYSIZE(ev.Command));
              ctrl->Events.push_back(ev);
            }
          }
          else if (const char* d = AppImportFind(cond, cend, "data->"))
          {
            d += 6;
            LatchCmd lc;
            int li = 0;
            while (d < cend && AppCImportIsIdent(*d) && li < (int)sizeof(lc.Latch) - 1) lc.Latch[li++] = *d++;
            lc.Latch[li] = 0;
            ImStrncpy(lc.Cmd, cmd, IM_ARRAYSIZE(lc.Cmd));
            if (lc.Latch[0])
              latches.push_back(lc);
          }
        }
      }

      const char* ub = nullptr;
      const char* ue = nullptr;
      if (AppImportMethodBody(cb, ce, "OnUpdate", &ub, &ue))
      {
        const char* ev_start = AppImportFind(ub, ue, "// events:");
        const char* s = ev_start;
        while (s != nullptr && (s = AppImportFind(s, ue, "if (")) != nullptr)
        {
          const char* cond = s + 4;
          const char* cend = cond;
          int pd = 1;
          while (cend < ue && pd > 0) { if (*cend == '(') pd++; else if (*cend == ')') { pd--; if (pd == 0) break; } cend++; }
          const char* asg = AppImportFind(cend, ue, "data->");
          const char* semi = asg != nullptr ? AppImportFind(asg, ue, ";") : nullptr;
          const char* next_if = AppImportFind(cend, ue, "if (");
          s = cend + 1;
          if (asg == nullptr || semi == nullptr) continue;
          if (next_if != nullptr && asg > next_if) continue;   // guard body is not a data-> assignment
          const char* eq = AppImportFind(asg, semi, "=");
          if (eq == nullptr) continue;

          const char* dp = asg + 6;
          char dst[IM_LABEL_SIZE];
          int  di = 0;
          while (dp < eq && AppCImportIsIdent(*dp) && di < IM_LABEL_SIZE - 1) dst[di++] = *dp++;
          dst[di] = 0;
          char rest[IM_LABEL_SIZE];
          int  ri = 0;
          for (const char* r = eq + 1; r < semi && ri < IM_LABEL_SIZE - 1; r++) rest[ri++] = *r;
          rest[ri] = 0;
          AppImportTrim(rest);

          char tf[IM_LABEL_SIZE];
          ImGuiAppEventEdge edge;
          if (!AppImportParseGuard(cond, cend, &edge, tf, IM_ARRAYSIZE(tf))) continue;

          if (strcmp(rest, "true") == 0)
          {
            const char* cmd = nullptr;
            for (int k = 0; k < latches.Size && cmd == nullptr; k++)
              if (strcmp(latches.Data[k].Latch, dst) == 0) cmd = latches.Data[k].Cmd;
            if (cmd != nullptr)
            {
              ImGuiAppEventDesc ev;
              ev.Edge = edge;
              ev.Action = ImGuiAppEventAction_EmitCommand;
              ImStrncpy(ev.TempField, tf, IM_ARRAYSIZE(ev.TempField));
              ImStrncpy(ev.Command, cmd, IM_ARRAYSIZE(ev.Command));
              ctrl->Events.push_back(ev);
              // `dst` is the synthesized `<Cmd>Pending` latch: it is derived, not authored, and the
              // emitter re-adds it. Drop it from the imported persist fields so re-emission doesn't double it.
              for (int fi = ctrl->Draft.PersistFields.Size - 1; fi >= 0; fi--)
                if (strcmp(ctrl->Draft.PersistFields.Data[fi].Name, dst) == 0)
                  ctrl->Draft.PersistFields.erase(&ctrl->Draft.PersistFields.Data[fi]);
            }
          }
          else
          {
            ImGuiAppEventDesc ev;
            ev.Edge = edge;
            ev.Action = ImGuiAppEventAction_SetField;
            ImStrncpy(ev.TempField, tf, IM_ARRAYSIZE(ev.TempField));
            ImStrncpy(ev.DstField, dst, IM_ARRAYSIZE(ev.DstField));
            char noexpr[IM_LABEL_SIZE + 16];
            ImFormatString(noexpr, IM_ARRAYSIZE(noexpr), "temp_data->%s", tf);
            if (strcmp(rest, noexpr) != 0)
              ImStrncpy(ev.Expr, rest, IM_ARRAYSIZE(ev.Expr));
            ctrl->Events.push_back(ev);
          }
        }
      }
    };

    // Per imported control, remember its persist type name and the dep Data types so a second pass can
    // wire producer->consumer data edges once every control node exists.
    struct ImportedCtrl
    {
      int  NodeId;
      char PersistType[IM_LABEL_SIZE];
      int  DepCount;
      char Deps[8][IM_LABEL_SIZE];
    };
    ImVector<ImportedCtrl> ctrls;

    int added = 0;
    const char* p = code;
    while ((p = strstr(p, "struct")) != nullptr)
    {
      if (p != code && AppCImportIsIdent(p[-1]))     { p += 6; continue; }
      const char* q = p + 6;
      if (!AppCImportIsSpace(*q))                    { p = q; continue; }
      while (AppCImportIsSpace(*q)) q++;

      char name[IM_LABEL_SIZE];
      int  ni = 0;
      while (AppCImportIsIdent(*q) && ni < IM_LABEL_SIZE - 1) name[ni++] = *q++;
      name[ni] = 0;
      while (AppCImportIsSpace(*q)) q++;
      if (ni == 0 || *q != ':')                      { p = q; continue; }   // a control shell inherits a base
      q++;
      while (AppCImportIsSpace(*q)) q++;
      if (strncmp(q, "ImGuiAppControl<", 16) != 0)   { p = q; continue; }   // ... specifically ImGuiAppControl<>
      q += 16;

      // Split the template arg list at depth-0 commas: [0]=PersistData, [1]=TempData, [2..]=dep Data types.
      char args[8][IM_LABEL_SIZE];
      int  arg_count = 0;
      {
        const char* a = q;
        int  depth = 1;
        char cur[IM_LABEL_SIZE];
        int  ci = 0;
        while (*a != 0 && depth > 0 && arg_count < 8)
        {
          if (*a == '<') { depth++; if (ci < IM_LABEL_SIZE - 1) cur[ci++] = *a; }
          else if (*a == '>')
          {
            depth--;
            if (depth == 0) { cur[ci] = 0; AppImportTrim(cur); if (cur[0]) ImStrncpy(args[arg_count++], cur, IM_LABEL_SIZE); break; }
            if (ci < IM_LABEL_SIZE - 1) cur[ci++] = *a;
          }
          else if (*a == ',' && depth == 1) { cur[ci] = 0; AppImportTrim(cur); if (cur[0]) ImStrncpy(args[arg_count++], cur, IM_LABEL_SIZE); ci = 0; }
          else if (ci < IM_LABEL_SIZE - 1) cur[ci++] = *a;
          a++;
        }
      }

      ImGuiAppNode* c = AppImportFindNode(g, ImGuiAppNodeKind_Control, name);
      if (c != nullptr)
      {
        // Update in place: clear the reflected content before re-populating (merge, not duplicate).
        c->Draft.PersistFields.clear();
        c->Draft.TempFields.clear();
        c->Events.clear();
        c->Commands.clear();
      }
      else
      {
        c = AppGraphAddNode(g, ImGuiAppNodeKind_Control, name);
        ImVec2 pos = origin + ImVec2((float)(added % 4) * 300.0f, (float)(added / 4) * 220.0f);
        AppGraphPlaceNode(g, c, &pos);
      }

      // Persist + temp fields from the referenced Data / TempData struct blocks.
      const char* body = nullptr;
      const char* end  = nullptr;
      if (arg_count >= 1 && AppImportFindNamedStruct(code, args[0], &body, &end))
        AppImportParseStructMembers(body, end, &c->Draft.PersistFields);
      if (arg_count >= 2 && AppImportFindNamedStruct(code, args[1], &body, &end))
        AppImportParseStructMembers(body, end, &c->Draft.TempFields);

      // Record persist type + dep types for the linking pass.
      ImportedCtrl rec;
      rec.NodeId = c->Id;
      ImStrncpy(rec.PersistType, arg_count >= 1 ? args[0] : "", IM_ARRAYSIZE(rec.PersistType));
      rec.DepCount = 0;
      for (int d = 2; d < arg_count && rec.DepCount < 8; d++)
        ImStrncpy(rec.Deps[rec.DepCount++], args[d], IM_LABEL_SIZE);

      // Skip past this control's body so the scan resumes after it; scan the body for command selections.
      const char* open = strchr(q, '{');
      if (open != nullptr)
      {
        const char* e2 = open + 1;
        int depth = 1;
        while (*e2 != 0 && depth > 0) { if (*e2 == '{') depth++; else if (*e2 == '}') depth--; e2++; }
        import_commands(c, open, e2);
        import_events(c, open, e2);
        p = e2;
      }
      else
        p = q;

      ctrls.push_back(rec);
      added++;
    }

    // Pass 2: resolve dep Data types to producer controls (by persist type name) and add data edges.
    auto port_of = [](const ImGuiAppNode* nd, ImGuiAppPortKind k) -> int
    {
      for (int i = 0; i < nd->Ports.Size; i++)
        if (nd->Ports.Data[i].Kind == k) return nd->Ports.Data[i].Id;
      return -1;
    };
    for (int i = 0; i < ctrls.Size; i++)
    {
      const ImGuiAppNode* consumer = AppGraphFindNodeConst(g, ctrls.Data[i].NodeId);
      if (consumer == nullptr) continue;
      for (int d = 0; d < ctrls.Data[i].DepCount; d++)
      {
        int producer_id = -1;
        for (int j = 0; j < ctrls.Size && producer_id < 0; j++)
          if (j != i && strcmp(ctrls.Data[j].PersistType, ctrls.Data[i].Deps[d]) == 0)
            producer_id = ctrls.Data[j].NodeId;
        const ImGuiAppNode* producer = producer_id >= 0 ? AppGraphFindNodeConst(g, producer_id) : nullptr;
        if (producer == nullptr) continue;   // unresolved dep (external/builtin) -- skip, no dangling edge
        const int out_port = port_of(producer, ImGuiAppPortKind_DataOut);
        const int in_port  = port_of(consumer, ImGuiAppPortKind_DataIn);
        if (out_port < 0 || in_port < 0) continue;
        AppImportAddLinkUnique(g, out_port, in_port, ImGuiAppEdgeKind_Data);
      }
    }
    return added;
  }

  // Import the CommandLayer's command DEFINITIONS from the emitted `enum AppCommand { ... }`. Every entry
  // but the None/Shutdown builtins and the COUNT terminator is a user command; add each to the layer.
  static void AppImportCommandEnum(ImGuiAppGraph* g, const char* code)
  {
    const char* en = strstr(code, "enum AppCommand");
    if (en == nullptr) return;
    const char* body = strchr(en, '{');
    const char* e = body != nullptr ? strchr(body, '}') : nullptr;
    if (body == nullptr || e == nullptr) return;

    ImGuiAppNode* cmdlayer = nullptr;
    for (int i = 0; i < g->Nodes.Size; i++)
      if (g->Nodes.Data[i].Kind == ImGuiAppNodeKind_Layer && g->Nodes.Data[i].LayerType == ImGuiAppLayerType_Command)
      {
        cmdlayer = &g->Nodes.Data[i];
        break;
      }
    if (cmdlayer == nullptr) return;

    const char* s = body;
    while ((s = AppImportFind(s, e, "AppCommand_")) != nullptr)
    {
      const char* id = s + strlen("AppCommand_");
      char name[IM_LABEL_SIZE];
      int  ni = 0;
      while (id < e && AppCImportIsIdent(*id) && ni < IM_LABEL_SIZE - 1) name[ni++] = *id++;
      name[ni] = 0;
      s = id;
      if (name[0] == 0 || strcmp(name, "None") == 0 || strcmp(name, "Shutdown") == 0 || strcmp(name, "COUNT") == 0)
        continue;
      bool dup = false;
      for (int k = 0; k < cmdlayer->Commands.Size && !dup; k++)
        dup = strcmp(cmdlayer->Commands.Data[k].Name, name) == 0;
      if (!dup)
        AppNodeAddCommand(cmdlayer, name);
    }
  }

  // Import authored Custom layers: each `struct <Name> : ImGuiAppLayer` becomes a Custom Layer node.
  // Added after the foundation so the layer push order (foundation, then customs in source order) matches.
  static void AppImportCustomLayers(ImGuiAppGraph* g, const char* code)
  {
    const char* p = code;
    while ((p = strstr(p, "struct")) != nullptr)
    {
      if (p != code && AppCImportIsIdent(p[-1]))     { p += 6; continue; }
      const char* q = p + 6;
      if (!AppCImportIsSpace(*q))                    { p = q; continue; }
      while (AppCImportIsSpace(*q)) q++;
      char name[IM_LABEL_SIZE];
      int  ni = 0;
      while (AppCImportIsIdent(*q) && ni < IM_LABEL_SIZE - 1) name[ni++] = *q++;
      name[ni] = 0;
      while (AppCImportIsSpace(*q)) q++;
      if (ni == 0 || *q != ':')                      { p = q; continue; }
      q++;
      while (AppCImportIsSpace(*q)) q++;
      if (strncmp(q, "ImGuiAppLayer", 13) != 0 || AppCImportIsIdent(q[13])) { p = q; continue; }   // not exactly ImGuiAppLayer
      if (AppImportFindNode(g, ImGuiAppNodeKind_Layer, name) == nullptr)
        AppGraphAddNode(g, ImGuiAppNodeKind_Layer, name)->LayerType = ImGuiAppLayerType_Custom;
      p = q + 13;
    }
  }

  // Import Window / Sidebar host nodes from their `ImGui::PushAppWindow<Name>` / `PushAppSidebar<Name>`
  // lines, in source order (= the emitter's node order among hosts).
  static void AppImportHosts(ImGuiAppGraph* g, const char* code)
  {
    struct Spec { const char* Push; ImGuiAppNodeKind Kind; };
    const Spec specs[] = { { "PushAppWindow<", ImGuiAppNodeKind_Window }, { "PushAppSidebar<", ImGuiAppNodeKind_Sidebar } };
    for (int si = 0; si < IM_ARRAYSIZE(specs); si++)
    {
      const char* p = code;
      while ((p = strstr(p, specs[si].Push)) != nullptr)
      {
        const char* a = p + strlen(specs[si].Push);
        char name[IM_LABEL_SIZE];
        int  ni = 0;
        while (AppCImportIsIdent(*a) && ni < IM_LABEL_SIZE - 1) name[ni++] = *a++;
        name[ni] = 0;
        if (name[0] && AppImportFindNode(g, specs[si].Kind, name) == nullptr)
          AppGraphAddNode(g, specs[si].Kind, name);
        p = a;
      }
    }
  }

  // Import hosting: each `PushWindowControl<Ctrl>(app, win_<Host>)` / `PushSidebarControl<Ctrl>(app,
  // sb_<Host>)` re-forms a containment edge Ctrl.ChildOut -> Host.ChildIn. PushAppControl is unhosted.
  static void AppImportHosting(ImGuiAppGraph* g, const char* code)
  {
    auto port_of = [](const ImGuiAppNode* nd, ImGuiAppPortKind k) -> int
    {
      for (int i = 0; i < nd->Ports.Size; i++)
        if (nd->Ports.Data[i].Kind == k) return nd->Ports.Data[i].Id;
      return -1;
    };
    auto find_named = [&](ImGuiAppNodeKind kind, const char* name) -> ImGuiAppNode*
    {
      for (int i = 0; i < g->Nodes.Size; i++)
        if (g->Nodes.Data[i].Kind == kind && strcmp(g->Nodes.Data[i].Draft.Name, name) == 0)
          return &g->Nodes.Data[i];
      return nullptr;
    };
    struct Spec { const char* Push; const char* Local; ImGuiAppNodeKind Kind; };
    const Spec specs[] = { { "PushWindowControl<", "win_", ImGuiAppNodeKind_Window }, { "PushSidebarControl<", "sb_", ImGuiAppNodeKind_Sidebar } };
    for (int si = 0; si < IM_ARRAYSIZE(specs); si++)
    {
      const char* p = code;
      while ((p = strstr(p, specs[si].Push)) != nullptr)
      {
        const char* a = p + strlen(specs[si].Push);
        char ctrl[IM_LABEL_SIZE];
        int  ci = 0;
        while (AppCImportIsIdent(*a) && ci < IM_LABEL_SIZE - 1) ctrl[ci++] = *a++;
        ctrl[ci] = 0;
        const char* eol = a;
        while (*eol != 0 && *eol != '\n') eol++;
        char host[IM_LABEL_SIZE];
        host[0] = 0;
        if (const char* loc = AppImportFind(a, eol, specs[si].Local))
        {
          loc += strlen(specs[si].Local);
          int hi = 0;
          while (loc < eol && AppCImportIsIdent(*loc) && hi < IM_LABEL_SIZE - 1) host[hi++] = *loc++;
          host[hi] = 0;
        }
        p = a;
        ImGuiAppNode* c = find_named(ImGuiAppNodeKind_Control, ctrl);
        ImGuiAppNode* h = host[0] ? find_named(specs[si].Kind, host) : nullptr;
        if (c == nullptr || h == nullptr) continue;
        const int co = port_of(c, ImGuiAppPortKind_ChildOut);
        const int hin = port_of(h, ImGuiAppPortKind_ChildIn);
        if (co < 0 || hin < 0) continue;
        AppImportAddLinkUnique(g, co, hin, ImGuiAppEdgeKind_Containment);
      }
    }
  }

  // Import true standalone structs (plain `struct <Name> { ... }`), excluding each control's inline
  // `<X>Data`/`<X>TempData` (carried as inline control fields). Find-or-update by name (merge policy).
  static void AppImportStandaloneStructs(ImGuiAppGraph* g, const char* code)
  {
    const char* p = code;
    while ((p = strstr(p, "struct")) != nullptr)
    {
      if (p != code && AppCImportIsIdent(p[-1]))     { p += 6; continue; }
      const char* q = p + 6;
      if (!AppCImportIsSpace(*q))                    { p = q; continue; }
      while (AppCImportIsSpace(*q)) q++;
      char name[IM_LABEL_SIZE];
      int  ni = 0;
      while (AppCImportIsIdent(*q) && ni < IM_LABEL_SIZE - 1) name[ni++] = *q++;
      name[ni] = 0;
      while (AppCImportIsSpace(*q)) q++;
      if (ni == 0 || *q != '{')                      { p = q; continue; }   // plain struct only (no base)
      const char* body = q + 1;
      const char* e = body;
      int depth = 1;
      while (*e != 0 && depth > 0) { if (*e == '{') depth++; else if (*e == '}') { depth--; if (depth == 0) break; } e++; }
      p = (*e != 0) ? e + 1 : e;

      bool is_ctrl_data = false;
      for (int j = 0; j < g->Nodes.Size && !is_ctrl_data; j++)
      {
        const ImGuiAppNode* n = &g->Nodes.Data[j];
        if (n->Kind != ImGuiAppNodeKind_Control)
          continue;
        char base[IM_LABEL_SIZE];
        AppNodeBaseName(n, base, IM_ARRAYSIZE(base));
        char dn[IM_LABEL_SIZE + 12];
        char tn[IM_LABEL_SIZE + 12];
        ImFormatString(dn, IM_ARRAYSIZE(dn), "%sData", base);
        ImFormatString(tn, IM_ARRAYSIZE(tn), "%sTempData", base);
        is_ctrl_data = strcmp(name, dn) == 0 || strcmp(name, tn) == 0;
      }
      if (is_ctrl_data)
        continue;

      ImGuiAppNode* s = AppImportFindNode(g, ImGuiAppNodeKind_Struct, name);
      if (s != nullptr)
        s->Draft.PersistFields.clear();
      else
      {
        s = AppGraphAddNode(g, ImGuiAppNodeKind_Struct, name);
        ImVec2 pos = ImVec2(0.0f, 0.0f);
        AppGraphPlaceNode(g, s, &pos);
      }
      AppImportParseStructMembers(body, e, &s->Draft.PersistFields);
    }
  }

  int AppGraphImportProgram(ImGuiAppGraph* g, const char* code)
  {
    IM_ASSERT(g != nullptr);
    if (code == nullptr)
      return 0;

    // Every emitted program brings up the foundation layers, so seed them first (idempotent). Their
    // node order matches EnsureFoundation, which matches the emitter's layer push order.
    AppGraphEnsureFoundation(g);
    AppImportCustomLayers(g, code);

    // CommandLayer definitions (the AppCommand enum), then controls (with their inline Data/TempData
    // fields, deps, commands and events).
    AppImportCommandEnum(g, code);
    const int controls = AppGraphImportControlsFromCode(g, code, ImVec2(0.0f, 0.0f));

    // True standalone structs (control Data/TempData excluded), find-or-update by name.
    AppImportStandaloneStructs(g, code);

    // Windows / sidebars, then the hosting containment edges (needs both endpoints present).
    AppImportHosts(g, code);
    AppImportHosting(g, code);

    return controls;
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Undo / redo (in-memory serialized snapshots)
  //-----------------------------------------------------------------------------
  // Each snapshot is a serialized-graph string (positions included). Linear history with a cursor at the live
  // state; pushing after an undo truncates the redo tail. One shared history -- the demo edits a single graph --
  // guarded by graph identity so switching graphs resets it. Snapshots are heap strings owned here.

  namespace
  {

    const int      kAppUndoCap = 128;
  }

  static char* AppUndoSnapshot(const ImGuiAppGraph* g)
  {
    ImGuiTextBuffer buf;
    AppGraphSerialize(g, &buf);
    const int n = buf.size();
    char* s = (char*)IM_ALLOC((size_t)n + 1);
    memcpy(s, buf.c_str(), (size_t)n);
    s[n] = 0;
    return s;
  }

  static void AppUndoClear(const ImGuiAppGraph* g)
  {
    for (int i = 0; i < AppGraphEditorState(g)->Undo.Snaps.Size; i++)
      IM_FREE(AppGraphEditorState(g)->Undo.Snaps.Data[i]);
    for (int i = 0; i < AppGraphEditorState(g)->Undo.Labels.Size; i++)
      IM_FREE(AppGraphEditorState(g)->Undo.Labels.Data[i]);
    AppGraphEditorState(g)->Undo.Snaps.clear();
    AppGraphEditorState(g)->Undo.Labels.clear();
    AppGraphEditorState(g)->Undo.Cursor = -1;
  }

  static char* AppUndoStrdup(const char* s)
  {
    const size_t n = strlen(s);
    char* d = (char*)IM_ALLOC(n + 1);
    memcpy(d, s, n + 1);
    return d;
  }

  // Name the operation that turned `prev` into `now`. Derived by structural diff -- the checkpoint model
  // detects settled changes rather than instrumenting call sites, so the name must come from the states
  // themselves. Precedence: structure > wiring > content > movement.
  static void AppUndoDeriveLabel(const ImGuiAppGraph* prev, const ImGuiAppGraph* now, char* out, size_t out_size)
  {
    // Node-set diff by id.
    const ImGuiAppNode* added = nullptr;   int n_added = 0;
    const ImGuiAppNode* removed = nullptr; int n_removed = 0;
    for (int i = 0; i < now->Nodes.Size; i++)
      if (AppGraphFindNodeConst(prev, now->Nodes.Data[i].Id) == nullptr) { added = &now->Nodes.Data[i]; n_added++; }
    for (int i = 0; i < prev->Nodes.Size; i++)
      if (AppGraphFindNodeConst(now, prev->Nodes.Data[i].Id) == nullptr) { removed = &prev->Nodes.Data[i]; n_removed++; }

    if (n_added > 0 && n_removed == 0)
    {
      if (n_added == 1) ImFormatString(out, out_size, "Add %s", added->Draft.Name[0] ? added->Draft.Name : "node");
      else              ImFormatString(out, out_size, "Add %d nodes", n_added);
      return;
    }
    if (n_removed > 0 && n_added == 0)
    {
      if (n_removed == 1) ImFormatString(out, out_size, "Delete %s", removed->Draft.Name[0] ? removed->Draft.Name : "node");
      else                ImFormatString(out, out_size, "Delete %d nodes", n_removed);
      return;
    }
    if (n_added > 0 && n_removed > 0)
    {
      ImFormatString(out, out_size, "Edit %d nodes", n_added + n_removed);
      return;
    }

    // Wiring: link-set diff by id.
    int links_added = 0, links_removed = 0;
    for (int i = 0; i < now->Links.Size; i++)
    {
      bool found = false;
      for (int j = 0; j < prev->Links.Size && !found; j++) found = prev->Links.Data[j].Id == now->Links.Data[i].Id;
      if (!found) links_added++;
    }
    for (int i = 0; i < prev->Links.Size; i++)
    {
      bool found = false;
      for (int j = 0; j < now->Links.Size && !found; j++) found = now->Links.Data[j].Id == prev->Links.Data[i].Id;
      if (!found) links_removed++;
    }
    if (links_added + links_removed > 0)
    {
      ImStrncpy(out, links_added > 0 && links_removed == 0 ? "Connect" : links_removed > 0 && links_added == 0 ? "Disconnect" : "Rewire", out_size);
      return;
    }

    // Content: per-node record compare (position excluded); movement tracked separately.
    const ImGuiAppNode* changed = nullptr; int n_changed = 0;
    const ImGuiAppNode* moved = nullptr;   int n_moved = 0;
    const ImGuiAppNode* renamed_now = nullptr; const ImGuiAppNode* renamed_prev = nullptr;
    ImGuiTextBuffer rec_a, rec_b;
    for (int i = 0; i < now->Nodes.Size; i++)
    {
      const ImGuiAppNode* a = AppGraphFindNodeConst(prev, now->Nodes.Data[i].Id);
      const ImGuiAppNode* b = &now->Nodes.Data[i];
      if (a == nullptr)
        continue;
      if (a->GridPos.x != b->GridPos.x || a->GridPos.y != b->GridPos.y) { moved = b; n_moved++; }
      rec_a.clear(); rec_b.clear();
      ImGuiAppNode ap = *a, bp = *b;                       // shallow copy is fine: emit reads only
      ap.GridPos = bp.GridPos = ImVec2(0.0f, 0.0f);        // neutralize the Pos= line
      AppEmitNodeRecord(&rec_a, &ap);
      AppEmitNodeRecord(&rec_b, &bp);
      if (strcmp(rec_a.c_str(), rec_b.c_str()) != 0)
      {
        changed = b; n_changed++;
        if (strcmp(a->Draft.Name, b->Draft.Name) != 0) { renamed_prev = a; renamed_now = b; }
      }
    }
    if (n_changed == 1)
    {
      const char* name = changed->Draft.Name[0] ? changed->Draft.Name : "node";
      if (renamed_now != nullptr)
        ImFormatString(out, out_size, "Rename %s \xE2\x86\x92 %s", renamed_prev->Draft.Name, renamed_now->Draft.Name);
      else if (changed->StyleMods.Size != AppGraphFindNodeConst(prev, changed->Id)->StyleMods.Size
            || changed->ColorMods.Size != AppGraphFindNodeConst(prev, changed->Id)->ColorMods.Size
            || memcmp(changed->StyleMods.Data, AppGraphFindNodeConst(prev, changed->Id)->StyleMods.Data, (size_t)changed->StyleMods.Size * sizeof(ImGuiAppStyleModDesc)) != 0
            || memcmp(changed->ColorMods.Data, AppGraphFindNodeConst(prev, changed->Id)->ColorMods.Data, (size_t)changed->ColorMods.Size * sizeof(ImGuiAppColorModDesc)) != 0)
        ImFormatString(out, out_size, "Style %s", name);
      else if (changed->Events.Size != AppGraphFindNodeConst(prev, changed->Id)->Events.Size)
        ImFormatString(out, out_size, "Events %s", name);
      else
        ImFormatString(out, out_size, "Edit %s", name);
      return;
    }
    if (n_changed > 1) { ImFormatString(out, out_size, "Edit %d nodes", n_changed); return; }

    if (n_moved == 1) { ImFormatString(out, out_size, "Move %s", moved->Draft.Name[0] ? moved->Draft.Name : "node"); return; }
    if (n_moved > 1)  { ImFormatString(out, out_size, "Move %d nodes", n_moved); return; }

    if (now->Bindings.Size != prev->Bindings.Size) { ImStrncpy(out, "Bind fields", out_size); return; }
    ImStrncpy(out, "Edit", out_size);
  }

  // Drop the redo tail, append a new snapshot + its label, and cap total depth from the front.
  static void AppUndoPush(const ImGuiAppGraph* g, char* snap, const char* label)
  {
    while (AppGraphEditorState(g)->Undo.Snaps.Size - 1 > AppGraphEditorState(g)->Undo.Cursor)
    {
      IM_FREE(AppGraphEditorState(g)->Undo.Snaps.Data[AppGraphEditorState(g)->Undo.Snaps.Size - 1]);
      IM_FREE(AppGraphEditorState(g)->Undo.Labels.Data[AppGraphEditorState(g)->Undo.Labels.Size - 1]);
      AppGraphEditorState(g)->Undo.Snaps.pop_back();
      AppGraphEditorState(g)->Undo.Labels.pop_back();
    }
    AppGraphEditorState(g)->Undo.Snaps.push_back(snap);
    AppGraphEditorState(g)->Undo.Labels.push_back(AppUndoStrdup(label));
    if (AppGraphEditorState(g)->Undo.Snaps.Size > kAppUndoCap)
    {
      IM_FREE(AppGraphEditorState(g)->Undo.Snaps.Data[0]);
      IM_FREE(AppGraphEditorState(g)->Undo.Labels.Data[0]);
      AppGraphEditorState(g)->Undo.Snaps.erase(AppGraphEditorState(g)->Undo.Snaps.Data);
      AppGraphEditorState(g)->Undo.Labels.erase(AppGraphEditorState(g)->Undo.Labels.Data);
    }
    AppGraphEditorState(g)->Undo.Cursor = AppGraphEditorState(g)->Undo.Snaps.Size - 1;
    AppGraphEditorState(g)->Undo.LiveHash = ImHashStr(snap);
  }

  static void AppUndoRestore(ImGuiAppGraph* g, int cursor)
  {
    const char* src = AppGraphEditorState(g)->Undo.Snaps.Data[cursor];
    const int n = (int)strlen(src);
    char* tmp = (char*)IM_ALLOC((size_t)n + 1);     // deserialize mutates the buffer in place
    memcpy(tmp, src, (size_t)n + 1);
    AppGraphDeserialize(g, tmp);
    IM_FREE(tmp);
    AppGraphEditorState(g)->Undo.Cursor = cursor;
    AppGraphEditorState(g)->Undo.LiveHash = ImHashStr(src);
  }

  void AppGraphCheckpoint(ImGuiAppGraph* g)
  {
    IM_ASSERT(g != nullptr);

    if (AppGraphEditorState(g)->Undo.Owner != g)
    {
      AppUndoClear(g);
      AppGraphEditorState(g)->Undo.Owner = g;
      AppUndoPush(g, AppUndoSnapshot(g), "Open");
      return;
    }
    // Coalesce: don't snapshot mid-drag or mid-edit -- wait for the gesture to settle so one action == one step.
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsAnyItemActive())
      return;
    char* snap = AppUndoSnapshot(g);
    if (ImHashStr(snap) == AppGraphEditorState(g)->Undo.LiveHash)
    {
      IM_FREE(snap);
      return;
    }

    // Name the step: deserialize the snapshot the live graph just diverged from, diff against the live graph.
    char label[96];
    ImStrncpy(label, "Edit", IM_ARRAYSIZE(label));
    if (AppGraphEditorState(g)->Undo.Cursor >= 0)
    {
      char* prev_text = AppUndoStrdup(AppGraphEditorState(g)->Undo.Snaps.Data[AppGraphEditorState(g)->Undo.Cursor]);   // deserialize mutates in place
      ImGuiAppGraph prev;
      AppGraphDeserialize(&prev, prev_text);
      IM_FREE(prev_text);
      AppUndoDeriveLabel(&prev, g, label, IM_ARRAYSIZE(label));
      prev.Nodes.clear_destruct();   // scratch graph owns its nodes' inner vectors; ImVector never destructs elements
    }
    AppUndoPush(g, snap, label);
  }

  bool AppGraphCanUndo(const ImGuiAppGraph* g) { return AppGraphEditorState(g)->Undo.Owner == g && AppGraphEditorState(g)->Undo.Cursor > 0; }
  bool AppGraphCanRedo(const ImGuiAppGraph* g) { return AppGraphEditorState(g)->Undo.Owner == g && AppGraphEditorState(g)->Undo.Cursor >= 0 && AppGraphEditorState(g)->Undo.Cursor < AppGraphEditorState(g)->Undo.Snaps.Size - 1; }

  void AppGraphUndo(ImGuiAppGraph* g)
  {
    if (AppGraphCanUndo(g))
      AppUndoRestore(g, AppGraphEditorState(g)->Undo.Cursor - 1);
  }

  void AppGraphRedo(ImGuiAppGraph* g)
  {
    if (AppGraphCanRedo(g))
      AppUndoRestore(g, AppGraphEditorState(g)->Undo.Cursor + 1);
  }

  int AppGraphHistoryCount(const ImGuiAppGraph* g) { return AppGraphEditorState(g)->Undo.Owner == g ? AppGraphEditorState(g)->Undo.Snaps.Size : 0; }
  int AppGraphHistoryCursor(const ImGuiAppGraph* g) { return AppGraphEditorState(g)->Undo.Owner == g ? AppGraphEditorState(g)->Undo.Cursor : -1; }

  const char* AppGraphHistoryLabel(const ImGuiAppGraph* g, int index)
  {
    if (AppGraphEditorState(g)->Undo.Owner != g || index < 0 || index >= AppGraphEditorState(g)->Undo.Labels.Size)
      return "";
    return AppGraphEditorState(g)->Undo.Labels.Data[index];
  }

  void AppGraphHistoryGoto(ImGuiAppGraph* g, int index)
  {
    if (AppGraphEditorState(g)->Undo.Owner == g && index >= 0 && index < AppGraphEditorState(g)->Undo.Snaps.Size && index != AppGraphEditorState(g)->Undo.Cursor)
      AppUndoRestore(g, index);
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Copy / paste (subtree clipboard with id remap)
  //-----------------------------------------------------------------------------
  // Copy serializes the selected roots + their containment subtrees (and the links/bindings internal to that
  // set) into a clipboard string. Paste deserializes into a scratch graph, allocates fresh ids for every node /
  // port / body-attr / link, offsets positions, and moves the records into the live graph. Works across graphs.


  static bool AppIdInSet(const ImVector<int>& s, int id)
  {
    for (int i = 0; i < s.Size; i++)
      if (s.Data[i] == id)
        return true;
    return false;
  }

  static int AppMapLookup(const ImVector<int>& olds, const ImVector<int>& news, int old_id)
  {
    for (int i = 0; i < olds.Size; i++)
      if (olds.Data[i] == old_id)
        return news.Data[i];
    return -1;
  }

  // Add root + its containment descendants to `ids` (no duplicates). Mirrors AppGroupAccumulate's hierarchy.
  static void AppGraphCollectSubtree(const ImGuiAppGraph* g, int root_id, ImVector<int>* ids)
  {
    if (AppIdInSet(*ids, root_id))
      return;
    const ImGuiAppNode* n = AppGraphFindNodeConst(g, root_id);
    if (n == nullptr)
      return;
    ids->push_back(root_id);
    if (n->Kind == ImGuiAppNodeKind_Window || n->Kind == ImGuiAppNodeKind_Sidebar)
    {
      for (int i = 0; i < g->Nodes.Size; i++)
        if (g->Nodes.Data[i].Kind == ImGuiAppNodeKind_Control && AppGraphParentOf(g, g->Nodes.Data[i].Id) == root_id)
          AppGraphCollectSubtree(g, g->Nodes.Data[i].Id, ids);
    }
    else if (n->Kind == ImGuiAppNodeKind_Control)
    {
      if (n->PersistStructId >= 0) AppGraphCollectSubtree(g, n->PersistStructId, ids);
      if (n->TempStructId >= 0)    AppGraphCollectSubtree(g, n->TempStructId, ids);
    }
    else if (n->Kind == ImGuiAppNodeKind_Struct)
    {
      for (int i = 0; i < g->Nodes.Size; i++)
        if (g->Nodes.Data[i].Kind == ImGuiAppNodeKind_Field && AppGraphParentOf(g, g->Nodes.Data[i].Id) == root_id)
          AppGraphCollectSubtree(g, g->Nodes.Data[i].Id, ids);
    }
  }

  static bool AppGraphClipboardHasData(const ImGuiAppGraph* g)
  {
    return AppGraphEditorState(g)->ClipText != nullptr;
  }

  // Serialize the roots' containment subtrees + their internal links/bindings (skipping layers/live
  // nodes) to a heap string (caller frees), or null if nothing serializable.
  static char* AppGraphSerializeSubsetString(const ImGuiAppGraph* g, const ImVector<int>& roots)
  {
    ImVector<int> ids;
    for (int i = 0; i < roots.Size; i++)
    {
      const ImGuiAppNode* r = AppGraphFindNodeConst(g, roots.Data[i]);
      if (r == nullptr || r->IsLive || r->Kind == ImGuiAppNodeKind_Layer)
        continue;
      AppGraphCollectSubtree(g, roots.Data[i], &ids);
    }
    if (ids.Size == 0)
      return nullptr;

    ImGuiTextBuffer buf;
    buf.appendf("[Graph]\n");
    for (int i = 0; i < g->Nodes.Size; i++)
      if (AppIdInSet(ids, g->Nodes.Data[i].Id))
        AppEmitNodeRecord(&buf, &g->Nodes.Data[i]);
    for (int i = 0; i < g->Links.Size; i++)
    {
      const int a = AppGraphPortOwnerId(g, g->Links.Data[i].StartAttr);
      const int b = AppGraphPortOwnerId(g, g->Links.Data[i].EndAttr);
      if (AppIdInSet(ids, a) && AppIdInSet(ids, b))
        buf.appendf("Link=%d,%d,%d,%d\n", g->Links.Data[i].Id, g->Links.Data[i].StartAttr, g->Links.Data[i].EndAttr, (int)g->Links.Data[i].Kind);
    }
    for (int i = 0; i < g->Bindings.Size; i++)
    {
      for (int li = 0; li < g->Links.Size; li++)
      {
        if (g->Links.Data[li].Id != g->Bindings.Data[i].LinkId)
          continue;
        const int a = AppGraphPortOwnerId(g, g->Links.Data[li].StartAttr);
        const int b = AppGraphPortOwnerId(g, g->Links.Data[li].EndAttr);
        if (AppIdInSet(ids, a) && AppIdInSet(ids, b))
          buf.appendf("Bind=%d,%s,%s\n", g->Bindings.Data[i].LinkId, g->Bindings.Data[i].DstField, g->Bindings.Data[i].SrcField);
        break;
      }
    }

    const int n = buf.size();
    char* s = (char*)IM_ALLOC((size_t)n + 1);
    memcpy(s, buf.c_str(), (size_t)n);
    s[n] = 0;
    return s;
  }

  static void AppGraphCopySelection(const ImGuiAppGraph* g, const ImVector<int>& roots)
  {
    char* s = AppGraphSerializeSubsetString(g, roots);
    if (s == nullptr)
      return;
    if (AppGraphEditorState(g)->ClipText != nullptr)
      IM_FREE(AppGraphEditorState(g)->ClipText);
    AppGraphEditorState(g)->ClipText = s;
    AppGraphEditorState(g)->ClipPaste = 0;
  }

  // Import a serialized partial graph into g with fresh ids for every node/port/body/link, offset positions, and
  // a remapped selection. Shared by paste and prefab instantiation. Returns the number of nodes added.
  static int AppGraphImportSerialized(ImGuiAppGraph* g, const char* serialized, ImVec2 offset)
  {
    if (serialized == nullptr)
      return 0;

    const int len = (int)strlen(serialized);
    char* data = (char*)IM_ALLOC((size_t)len + 1);
    memcpy(data, serialized, (size_t)len + 1);
    ImGuiAppGraph tmp;
    AppGraphDeserialize(&tmp, data);
    IM_FREE(data);
    if (tmp.Nodes.Size == 0)
      return 0;

    // Allocate fresh ids for every node, body-attr, and port; remember the old->new maps.
    ImVector<int> old_node, new_node;
    ImVector<int> old_port, new_port;
    for (int i = 0; i < tmp.Nodes.Size; i++)
    {
      ImGuiAppNode* tn = &tmp.Nodes.Data[i];
      old_node.push_back(tn->Id);
      const int nid = g->NextId++;
      new_node.push_back(nid);
      tn->Id = nid;
      tn->BodyAttrId = g->NextId++;
      for (int p = 0; p < tn->Ports.Size; p++)
      {
        old_port.push_back(tn->Ports.Data[p].Id);
        const int pid = g->NextId++;
        new_port.push_back(pid);
        tn->Ports.Data[p].Id = pid;
      }
    }

    g->Selection.clear();
    for (int i = 0; i < tmp.Nodes.Size; i++)
    {
      ImGuiAppNode* tn = &tmp.Nodes.Data[i];
      if (tn->PersistStructId >= 0) tn->PersistStructId = AppMapLookup(old_node, new_node, tn->PersistStructId);
      if (tn->TempStructId >= 0)    tn->TempStructId = AppMapLookup(old_node, new_node, tn->TempStructId);
      tn->GridPos += offset;
      tn->HasGridPos = true;
      tn->_NeedsPlace = true;
      tn->IsLive = false;
      tn->LiveKey = 0;
      g->Selection.push_back(tn->Id);
      g->Nodes.push_back(*tn);     // MOVE: ImVector copies the bytes; tmp keeps no separate ownership of inner buffers
    }

    for (int i = 0; i < tmp.Links.Size; i++)
    {
      const int s = AppMapLookup(old_port, new_port, tmp.Links.Data[i].StartAttr);
      const int e = AppMapLookup(old_port, new_port, tmp.Links.Data[i].EndAttr);
      if (s < 0 || e < 0)
        continue;
      ImGuiAppNodeLink l = tmp.Links.Data[i];
      const int old_link = l.Id;
      l.Id = g->NextId++;
      l.StartAttr = s;
      l.EndAttr = e;
      g->Links.push_back(l);
      for (int bi = 0; bi < tmp.Bindings.Size; bi++)
      {
        if (tmp.Bindings.Data[bi].LinkId != old_link)
          continue;
        ImGuiAppFieldBinding b = tmp.Bindings.Data[bi];
        b.LinkId = l.Id;
        g->Bindings.push_back(b);
      }
    }

    // tmp's nodes were moved into g (their inner ImVector buffers now belong to g). ImVector's destructor frees
    // only the outer arrays, never element-owned memory, so clearing tmp here would not double-free -- but to be
    // explicit that ownership has transferred, drop tmp's node count without touching the moved inner buffers.
    tmp.Nodes.Size = 0;
    return new_node.Size;
  }

  // Paste the clipboard into g with fresh ids and a cascading offset; selects the pasted roots. Returns count.
  static int AppGraphPasteClipboard(ImGuiAppGraph* g)
  {
    if (AppGraphEditorState(g)->ClipText == nullptr)
      return 0;
    const float step = 40.0f * (float)(++AppGraphEditorState(g)->ClipPaste);
    return AppGraphImportSerialized(g, AppGraphEditorState(g)->ClipText, ImVec2(step, step));
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Prefabs (named reusable subtrees)
  //-----------------------------------------------------------------------------

  void AppGraphSavePrefab(const ImGuiAppGraph* g, const ImVector<int>& roots, const char* name)
  {
    IM_ASSERT(g != nullptr && name != nullptr);
    char* s = AppGraphSerializeSubsetString(g, roots);
    if (s == nullptr)
      return;

    // Replace an existing prefab of the same name in place.
    ImVector<ImGuiAppPrefab>* prefabs = &AppGraphEditorState(g)->Prefabs;
    for (int i = 0; i < prefabs->Size; i++)
      if (strcmp(prefabs->Data[i].Name, name) == 0)
      {
        IM_FREE(prefabs->Data[i].Data);
        prefabs->Data[i].Data = s;
        return;
      }

    ImGuiAppPrefab pf;
    ImStrncpy(pf.Name, name[0] ? name : "prefab", IM_ARRAYSIZE(pf.Name));
    pf.Data = s;
    prefabs->push_back(pf);
  }

  int         AppGraphPrefabCount(const ImGuiAppGraph* g) { return AppGraphEditorState(g)->Prefabs.Size; }
  const char* AppGraphPrefabName(const ImGuiAppGraph* g, int index) { ImVector<ImGuiAppPrefab>* p = &AppGraphEditorState(g)->Prefabs; return (index >= 0 && index < p->Size) ? p->Data[index].Name : ""; }

  int AppGraphInstantiatePrefab(ImGuiAppGraph* g, int index, ImVec2 origin)
  {
    IM_ASSERT(g != nullptr);
    ImVector<ImGuiAppPrefab>* prefabs = &AppGraphEditorState(g)->Prefabs;
    if (index < 0 || index >= prefabs->Size)
      return 0;
    return AppGraphImportSerialized(g, prefabs->Data[index].Data, origin);
  }

  // Push `scratch` (all of it) into g's prefab registry under `name`, replacing a same-named entry. The
  // scratch graph is a disposable staging area authored only to be serialized.
  static void AppSeedPrefabFromGraph(ImGuiAppGraph* g, const ImGuiAppGraph* scratch, const char* name)
  {
    ImVector<int> roots;
    for (int i = 0; i < scratch->Nodes.Size; i++)
      roots.push_back(scratch->Nodes.Data[i].Id);
    char* data = AppGraphSerializeSubsetString(scratch, roots);
    if (data == nullptr)
      return;
    ImVector<ImGuiAppPrefab>* prefabs = &AppGraphEditorState(g)->Prefabs;
    for (int i = 0; i < prefabs->Size; i++)
      if (strcmp(prefabs->Data[i].Name, name) == 0)
      {
        IM_FREE(prefabs->Data[i].Data);
        prefabs->Data[i].Data = data;
        return;
      }
    ImGuiAppPrefab pf;
    ImStrncpy(pf.Name, name, IM_ARRAYSIZE(pf.Name));
    pf.Data = data;
    prefabs->push_back(pf);
  }

  static int AppNodeFirstPortOfKind(const ImGuiAppNode* n, ImGuiAppPortKind kind)
  {
    for (int p = 0; p < n->Ports.Size; p++)
      if (n->Ports.Data[p].Kind == kind)
        return n->Ports.Data[p].Id;
    return 0;
  }

  // Seed the starter prefab library (F04) if the registry is empty: a producer/consumer pair wired by a
  // data edge + binding, and an event->command control. Never clobbers a loaded or authored registry.
  void AppGraphSeedStarterPrefabs(ImGuiAppGraph* g)
  {
    IM_ASSERT(g != nullptr);
    if (AppGraphPrefabCount(g) > 0)
      return;

    {
      ImGuiAppGraph s;
      ImGuiAppNode* prod = AppGraphAddNode(&s, ImGuiAppNodeKind_Control, "Producer");
      AppNodeDraftAddField(&prod->Draft.PersistFields, "value", ImGuiAppFieldType_Float);
      const int prod_out = AppNodeFirstPortOfKind(prod, ImGuiAppPortKind_DataOut);   // capture before the next add dangles prod
      ImGuiAppNode* cons = AppGraphAddNode(&s, ImGuiAppNodeKind_Control, "Consumer");
      AppNodeDraftAddField(&cons->Draft.PersistFields, "input", ImGuiAppFieldType_Float);
      const int cons_in = AppNodeFirstPortOfKind(cons, ImGuiAppPortKind_DataIn);
      ImGuiAppNodeLink l;
      l.Id = AppGraphAllocId(&s);
      l.StartAttr = prod_out;
      l.EndAttr = cons_in;
      l.Kind = ImGuiAppEdgeKind_Data;
      s.Links.push_back(l);
      ImGuiAppFieldBinding b;
      b.LinkId = l.Id;
      ImStrncpy(b.DstField, "input", IM_ARRAYSIZE(b.DstField));
      ImStrncpy(b.SrcField, "value", IM_ARRAYSIZE(b.SrcField));
      s.Bindings.push_back(b);
      AppSeedPrefabFromGraph(g, &s, "Producer/Consumer");
    }
    {
      ImGuiAppGraph s;
      ImGuiAppNode* c = AppGraphAddNode(&s, ImGuiAppNodeKind_Control, "Trigger");
      AppNodeDraftAddField(&c->Draft.TempFields, "hit", ImGuiAppFieldType_Bool);
      c->Commands.push_back(ImGuiAppCommandDesc());
      ImStrncpy(c->Commands.back().Name, "Fire", IM_ARRAYSIZE(c->Commands.back().Name));
      ImGuiAppEventDesc ev;
      ev.Edge = ImGuiAppEventEdge_Changed;
      ev.Action = ImGuiAppEventAction_EmitCommand;
      ImStrncpy(ev.TempField, "hit", IM_ARRAYSIZE(ev.TempField));
      ImStrncpy(ev.Command, "Fire", IM_ARRAYSIZE(ev.Command));
      c->Events.push_back(ev);
      AppSeedPrefabFromGraph(g, &s, "Event -> Command");
    }
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Live mirror: reflect the running app's controls into the model
  //-----------------------------------------------------------------------------
  // Memory discipline: snapshot the running controls into plain value records BEFORE mutating the graph;
  // capture every node/port id by VALUE right after a node is created; never dereference a node pointer
  // across another AppGraph* call that can reallocate g->Nodes.

  // One desired live entry, snapshotted from the running app BEFORE any graph mutation.
  struct AppLiveWant
  {
    ImGuiID                 Key;       // stable upsert key (so the node keeps its canvas position across frames)
    ImGuiAppNodeKind        Kind;
    char                    Name[IM_LABEL_SIZE];     // display name (controls: the CLASS name)
    char                    DataType[IM_LABEL_SIZE]; // controls only: PersistData type name (structs/codegen/promotion)
    ImGuiAppLayerType       LayerType;
    ImGuiID                 DataId;    // control PersistData id (0 for non-controls)
    ImGuiID                 Deps[16];
    bool                    DepSoft[16];   // per-slot Optional flag (soft wires draw dimmed)
    int                     DepCount;
    ImGuiID                 ParentKey; // host window/sidebar Key for a hosted control (0 == app-level / no parent)
    ImGuiWindowFlags        Flags;     // Window/Sidebar: live window flags
    bool                    HasPlacement;
    ImVec2                  InitialPos;
    ImVec2                  InitialSize;
    ImGuiDir                DockDir;   // Sidebar only
    float                   DockSize;  // Sidebar only
    const ImGuiAppItemBase* Item;      // live object (windows/sidebars/controls; null for layers) -- read within
                                  // BuildAppLiveGraph only, to mirror StyleMods (kept OFF this struct: a nested
                                  // ImVector member inside ImVector<AppLiveWant> would break ImVector's memcpy grow)
  };

  static const char* AppLayerTypeName(ImGuiAppLayerType t);   // fwd (defined above)
  static const char* AppLayerNodeName(ImGuiAppLayerType t);   // fwd (defined above)

  void BuildAppLiveGraph(const ImGuiApp* app, ImGuiAppGraph* g)
  {
    IM_ASSERT(g != nullptr);
    // Codegen reads the mirrored composition through this pointer (read-only by contract).
    g->LiveApp = const_cast<ImGuiApp*>(app);
    if (app == nullptr)
      return;

    // 1) Snapshot the WHOLE app composition (layers, windows, sidebars, controls). No graph access here.
    ImVector<AppLiveWant> want;

    // Snapshot one control (app-level or hosted). Keyed by its runtime PersistData id (app->Data is keyed by
    // type id, so the id is unique per app -> no collision between app-level and hosted controls). parent_key is
    // the host window/sidebar Key for a hosted control, 0 for an app-level control.
    auto PushControlWant = [&](const ImGuiAppControlBase* ctrl, ImGuiID parent_key)
    {
      if (ctrl == nullptr)
        return;
      ImGuiID id = ctrl->GetControlDataID();
      if (id == 0)
        return;
      AppLiveWant w; w.Kind = ImGuiAppNodeKind_Control; w.Key = id; w.DataId = id; w.LayerType = ImGuiAppLayerType_Task;
      w.ParentKey = parent_key;
      w.Flags = 0; w.HasPlacement = false; w.InitialPos = ImVec2(0.0f, 0.0f); w.InitialSize = ImVec2(0.0f, 0.0f); w.DockDir = ImGuiDir_None; w.DockSize = 0.0f;
      w.Item = ctrl;
      // Name: the control class (Push*Control stamps the Label). DataType: the PersistData type.
      ctrl->GetControlDataTypeName(w.DataType, IM_ARRAYSIZE(w.DataType));
      if (ctrl->Label[0])
        ImStrncpy(w.Name, ctrl->Label, IM_ARRAYSIZE(w.Name));
      else if (w.DataType[0])
        ImStrncpy(w.Name, w.DataType, IM_ARRAYSIZE(w.Name));
      else
        ImStrncpy(w.Name, "Control", IM_ARRAYSIZE(w.Name));
      int n = ctrl->GetControlDependencyIDs(w.Deps, IM_ARRAYSIZE(w.Deps));
      if (n < 0) n = 0; if (n > IM_ARRAYSIZE(w.Deps)) n = IM_ARRAYSIZE(w.Deps);
      w.DepCount = n;
      for (int d = 0; d < IM_ARRAYSIZE(w.DepSoft); d++)
        w.DepSoft[d] = false;
      ctrl->GetControlDependencyOptional(w.DepSoft, n);
      want.push_back(w);
    };

    // Layers: stable order from InitializeApp (Task, Command, Status, Window; anything after is a custom
    // subclass). Keyed by index. The AUTHORED foundation is canonical for the CORE phases -- when a design
    // layer of a core type exists, the live layer is represented BY it (the pipeline is one stack, never
    // design/live phase twins). Custom live layers always mirror, named by their stamped type label.
    for (int i = 0; i < app->Layers.Size; i++)
    {
      const ImGuiAppLayerType lt = (i <= (int)ImGuiAppLayerType_Display) ? (ImGuiAppLayerType)i : ImGuiAppLayerType_Custom;
      if (AppLayerIsCore(lt))
      {
        bool has_design_twin = false;
        for (int n = 0; n < g->Nodes.Size && !has_design_twin; n++)
          has_design_twin = !g->Nodes.Data[n].IsLive && g->Nodes.Data[n].Kind == ImGuiAppNodeKind_Layer && g->Nodes.Data[n].LayerType == lt;
        if (has_design_twin)
          continue;
      }
      AppLiveWant w; w.Kind = ImGuiAppNodeKind_Layer; w.Key = 0x4C000000u + (ImGuiID)i;
      w.DataId = 0; w.DepCount = 0; w.ParentKey = 0;
      w.Flags = 0; w.HasPlacement = false; w.InitialPos = ImVec2(0.0f, 0.0f); w.InitialSize = ImVec2(0.0f, 0.0f); w.DockDir = ImGuiDir_None; w.DockSize = 0.0f;
      w.Item = nullptr;
      w.LayerType = lt;
      if (app->Layers.Data[i]->Label[0])   // PushAppLayer stamps the class name
        ImStrncpy(w.Name, app->Layers.Data[i]->Label, IM_ARRAYSIZE(w.Name));
      else
        ImFormatString(w.Name, IM_ARRAYSIZE(w.Name), "%s", AppLayerNodeName(lt));
      want.push_back(w);
    }
    // Windows / Sidebars: keyed by their unique Label. Each hosts its own controls (window->Controls), mirrored
    // right after the host so a live containment edge (control -> host) can be built below.
    for (int i = 0; i < app->Windows.Size; i++)
    {
      const ImGuiAppWindowBase* win = app->Windows.Data[i];
      const char* lbl = win->Label;
      ImGuiID key = AppConstantHash(lbl[0] ? lbl : "Window");
      AppLiveWant w; w.Kind = ImGuiAppNodeKind_Window; w.Key = key;
      w.DataId = 0; w.DepCount = 0; w.LayerType = ImGuiAppLayerType_Task; w.ParentKey = 0;
      w.Flags = win->Flags; w.HasPlacement = win->HasInitialPlacement; w.InitialPos = win->InitialPos; w.InitialSize = win->InitialSize;
      w.DockDir = ImGuiDir_None; w.DockSize = 0.0f;
      w.Item = win;
      ImStrncpy(w.Name, lbl[0] ? lbl : "Window", IM_ARRAYSIZE(w.Name));
      want.push_back(w);
      for (int c = 0; c < win->Controls.Size; c++)
        PushControlWant(win->Controls.Data[c], key);
    }
    for (int i = 0; i < app->Sidebars.Size; i++)
    {
      const ImGuiAppSidebarBase* sb = app->Sidebars.Data[i];
      const char* lbl = sb->Label;
      ImGuiID key = AppConstantHash(lbl[0] ? lbl : "Sidebar") + 1u;
      AppLiveWant w; w.Kind = ImGuiAppNodeKind_Sidebar; w.Key = key;
      w.DataId = 0; w.DepCount = 0; w.LayerType = ImGuiAppLayerType_Task; w.ParentKey = 0;
      w.Flags = sb->Flags; w.HasPlacement = sb->HasInitialPlacement; w.InitialPos = sb->InitialPos; w.InitialSize = sb->InitialSize;
      w.DockDir = sb->DockDir; w.DockSize = sb->Size;
      w.Item = sb;
      ImStrncpy(w.Name, lbl[0] ? lbl : "Sidebar", IM_ARRAYSIZE(w.Name));
      want.push_back(w);
      for (int c = 0; c < sb->Controls.Size; c++)
        PushControlWant(sb->Controls.Data[c], key);
    }
    // App-level controls: keyed by runtime PersistData id; carry their dependency ids for edge discovery.
    for (int c = 0; c < app->Controls.Size; c++)
      PushControlWant(app->Controls.Data[c], 0);

    // 2) Remove live nodes whose key is no longer wanted (collect ids first; removal mutates g->Nodes).
    ImVector<int> stale;
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      if (!g->Nodes.Data[i].IsLive) continue;
      bool wanted = false;
      for (int w = 0; w < want.Size; w++) if (want.Data[w].Key == g->Nodes.Data[i].LiveKey) { wanted = true; break; }
      if (!wanted) stale.push_back(g->Nodes.Data[i].Id);
    }
    for (int i = 0; i < stale.Size; i++)
      AppGraphRemoveNode(g, stale.Data[i]);

    // 3) Upsert: keep existing live nodes (DO NOT touch their position); add only the missing ones, placed
    //    once via the staggered default. Capture per-key port ids by value for edge building.
    struct AppLiveMade { ImGuiID Key; int OutPort; int InPort; int ChildOutPort; int ChildInPort; };
    ImVector<AppLiveMade> made;
    for (int w = 0; w < want.Size; w++)
    {
      // already present?
      bool present = false;
      for (int i = 0; i < g->Nodes.Size; i++)
        if (g->Nodes.Data[i].IsLive && g->Nodes.Data[i].LiveKey == want.Data[w].Key) { present = true; break; }

      if (!present)
      {
        ImGuiAppNode* node = (want.Data[w].Kind == ImGuiAppNodeKind_Control)
          ? AppGraphAddBuiltin(g, ImGuiAppNodeKind_Control, want.Data[w].Name, want.Data[w].DataType)
          : AppGraphAddNode(g, want.Data[w].Kind, want.Data[w].Name);
        node->IsLive = true;
        node->LiveKey = want.Data[w].Key;
        node->LayerType = want.Data[w].LayerType;
        for (int p = 0; p < node->Ports.Size; p++)
          if (node->Ports.Data[p].Kind == ImGuiAppPortKind_DataOut)
            node->Ports.Data[p].DataTypeId = want.Data[w].DataId;

        // New live nodes share the same composition-first placement as authored nodes:
        // layers/app | windows/sidebars | controls. Existing live nodes keep user-dragged
        // positions. A hosted control clusters beside its host window/sidebar node.
        const ImGuiAppNode* host = nullptr;
        if (want.Data[w].Kind == ImGuiAppNodeKind_Control && want.Data[w].ParentKey != 0)
          for (int i = 0; i < g->Nodes.Size && host == nullptr; i++)
            if (g->Nodes.Data[i].IsLive && g->Nodes.Data[i].LiveKey == want.Data[w].ParentKey)
              host = &g->Nodes.Data[i];
        if (host != nullptr)
        {
          // Siblings chain below the previous member's REAL footprint (a fixed pitch under-shoots
          // measured cluster nodes, cascading the open-placement march far from the window).
          ImVec2 preferred(host->GridPos.x + 520.0f, host->GridPos.y);
          const ImGuiAppNode* prev = nullptr;
          for (int w2 = 0; w2 < w; w2++)
          {
            if (want.Data[w2].Kind != ImGuiAppNodeKind_Control || want.Data[w2].ParentKey != want.Data[w].ParentKey)
              continue;
            for (int i = 0; i < g->Nodes.Size; i++)
              if (g->Nodes.Data[i].IsLive && g->Nodes.Data[i].LiveKey == want.Data[w2].Key)
              {
                prev = &g->Nodes.Data[i];
                break;
              }
          }
          if (prev != nullptr)
            preferred.y = prev->GridPos.y + AppLayoutNodeSize(g, prev).y + 60.0f;
          AppGraphPlaceNode(g, node, &preferred);
        }
        else
        {
          AppGraphPlaceNode(g, node, nullptr);
        }
      }
    }
    // Build the per-key port map (after all adds, pointers are stable until next mutation; we only read here).
    for (int w = 0; w < want.Size; w++)
    {
      AppLiveMade lm; lm.Key = want.Data[w].Key; lm.OutPort = 0; lm.InPort = 0; lm.ChildOutPort = 0; lm.ChildInPort = 0;
      for (int i = 0; i < g->Nodes.Size; i++)
      {
        ImGuiAppNode* node = &g->Nodes.Data[i];
        if (!node->IsLive || node->LiveKey != want.Data[w].Key) continue;
        for (int p = 0; p < node->Ports.Size; p++)
        {
          const ImGuiAppNodePort* port = &node->Ports.Data[p];
          // InPort must be the "deps" pin: a control also carries persist/temp DataIn tie ports,
          // and live nodes never submit those pins -- a wire anchored there is dropped unseen.
          if      (port->Kind == ImGuiAppPortKind_DataOut)  lm.OutPort = port->Id;
          else if (port->Kind == ImGuiAppPortKind_DataIn && (lm.InPort == 0 || strcmp(port->Name, "deps") == 0))
            lm.InPort = port->Id;
          else if (port->Kind == ImGuiAppPortKind_ChildOut) lm.ChildOutPort = port->Id;
          else if (port->Kind == ImGuiAppPortKind_ChildIn)  lm.ChildInPort = port->Id;
        }
        // Refresh mirrored composition props each frame (read-only mirror; never touch GridPos/position).
        node->Flags = want.Data[w].Flags;
        node->HasInitialPlacement = want.Data[w].HasPlacement;
        node->InitialPos = want.Data[w].InitialPos;
        node->InitialSize = want.Data[w].InitialSize;
        node->DockDir = want.Data[w].DockDir;
        node->DockSize = want.Data[w].DockSize;
        if (want.Data[w].Item != nullptr)
        {
          node->StyleMods = want.Data[w].Item->StyleMods;
          node->ColorMods = want.Data[w].Item->ColorMods;
        }
        break;
      }
      made.push_back(lm);
    }

    // 4) Rebuild live data edges (between two live nodes). Tear down old, re-derive from control deps.
    for (int li = g->Links.Size - 1; li >= 0; li--)
    {
      ImGuiAppNode* a = nullptr; ImGuiAppNode* b = nullptr;
      AppGraphFindPort(g, g->Links.Data[li].StartAttr, &a);
      AppGraphFindPort(g, g->Links.Data[li].EndAttr, &b);
      if (a && b && a->IsLive && b->IsLive)
        g->Links.erase(g->Links.Data + li);
    }
    for (int w = 0; w < want.Size; w++)
    {
      if (want.Data[w].Kind != ImGuiAppNodeKind_Control || want.Data[w].DepCount == 0) continue;
      int consumer_in = 0;
      for (int m = 0; m < made.Size; m++) if (made.Data[m].Key == want.Data[w].Key) { consumer_in = made.Data[m].InPort; break; }
      if (consumer_in == 0) continue;
      for (int d = 0; d < want.Data[w].DepCount; d++)
      {
        int producer_out = 0;
        for (int m = 0; m < made.Size; m++) if (made.Data[m].Key == want.Data[w].Deps[d]) { producer_out = made.Data[m].OutPort; break; }
        if (producer_out == 0) continue;
        ImGuiAppNodeLink l; l.Id = AppGraphAllocId(g); l.StartAttr = producer_out; l.EndAttr = consumer_in; l.Kind = ImGuiAppEdgeKind_Data;
        l.Soft = want.Data[w].DepSoft[d];
        g->Links.push_back(l);
      }
    }

    // 4b) Rebuild live containment edges: a hosted control's ChildOut -> its host window/sidebar ChildIn.
    for (int w = 0; w < want.Size; w++)
    {
      if (want.Data[w].Kind != ImGuiAppNodeKind_Control || want.Data[w].ParentKey == 0) continue;
      int child_out = 0;
      int parent_in = 0;
      for (int m = 0; m < made.Size; m++)
      {
        if (made.Data[m].Key == want.Data[w].Key)       child_out = made.Data[m].ChildOutPort;
        if (made.Data[m].Key == want.Data[w].ParentKey) parent_in = made.Data[m].ChildInPort;
      }
      if (child_out == 0 || parent_in == 0) continue;
      ImGuiAppNodeLink l; l.Id = AppGraphAllocId(g); l.StartAttr = child_out; l.EndAttr = parent_in; l.Kind = ImGuiAppEdgeKind_Containment;
      g->Links.push_back(l);
    }

    // 5) Promotion preview: a design control node whose emitted data type name matches a live node.
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      ImGuiAppNode* n = &g->Nodes.Data[i];
      n->IsPromoted = false;
      if (n->IsLive || n->Kind != ImGuiAppNodeKind_Control) continue;
      char wantname[IM_LABEL_SIZE];
      AppNodeDataTypeName(n, wantname, IM_ARRAYSIZE(wantname));
      for (int j = 0; j < g->Nodes.Size; j++)
        if (g->Nodes.Data[j].IsLive && g->Nodes.Data[j].DataTypeName[0] && strcmp(g->Nodes.Data[j].DataTypeName, wantname) == 0)
        { n->IsPromoted = true; break; }
    }
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Scene-hierarchy tree (ECS-style outliner)
  //-----------------------------------------------------------------------------

  static int AppTreeDisplayLayerId(const ImGuiAppGraph* g)
  {
    for (int i = 0; i < g->Nodes.Size; i++)
      if (g->Nodes.Data[i].Kind == ImGuiAppNodeKind_Layer && g->Nodes.Data[i].LayerType == ImGuiAppLayerType_Display)
        return g->Nodes.Data[i].Id;
    return -1;
  }

  static int AppTreeOwningControl(const ImGuiAppGraph* g, int struct_id)
  {
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* c = &g->Nodes.Data[i];
      if (c->Kind == ImGuiAppNodeKind_Control && (c->PersistStructId == struct_id || c->TempStructId == struct_id))
        return c->Id;
    }
    return -1;
  }

  // Outliner parent of a node (-1 = a root): windows nest under the Display layer; hosted controls under their
  // window/sidebar; a control's exploded Persist/Temp structs under the control; fields under their struct.
  static int AppNodeTreeParent(const ImGuiAppGraph* g, const ImGuiAppNode* n)
  {
    switch (n->Kind)
    {
    case ImGuiAppNodeKind_Window:
      return AppTreeDisplayLayerId(g);
    case ImGuiAppNodeKind_Control:
    {
      const int p = AppGraphParentOf(g, n->Id);
      const ImGuiAppNode* pn = p >= 0 ? AppGraphFindNodeConst(g, p) : nullptr;
      return (pn != nullptr && (pn->Kind == ImGuiAppNodeKind_Window || pn->Kind == ImGuiAppNodeKind_Sidebar)) ? p : -1;
    }
    case ImGuiAppNodeKind_Struct:
      return AppTreeOwningControl(g, n->Id);
    case ImGuiAppNodeKind_Field:
      return AppGraphParentOf(g, n->Id);
    default:
      return -1;
    }
  }

  static int AppNodeFirstPortKind(const ImGuiAppNode* n, ImGuiAppPortKind kind)
  {
    for (int p = 0; p < n->Ports.Size; p++)
      if (n->Ports.Data[p].Kind == kind)
        return n->Ports.Data[p].Id;
    return 0;
  }

  // Re-parent a node by moving its containment edge: a Control onto a Window/Sidebar (host it), or a Field onto a
  // Struct. Removes the child's old containment edge, adds a new one (child ChildOut -> parent ChildIn).
  static bool AppGraphReparent(ImGuiAppGraph* g, int child_id, int parent_id)
  {
    ImGuiAppNode* child  = AppGraphFindNode(g, child_id);
    ImGuiAppNode* parent = AppGraphFindNode(g, parent_id);
    if (child == nullptr || parent == nullptr || child == parent)
      return false;
    if (child->IsLive || parent->IsLive)   // a live mirror can be neither reparented nor host a reparent
    {
      AppNotifyLiveReadOnly(g, child->IsLive ? child : parent);
      return false;
    }
    const bool ok = (child->Kind == ImGuiAppNodeKind_Control && (parent->Kind == ImGuiAppNodeKind_Window || parent->Kind == ImGuiAppNodeKind_Sidebar))
                 || (child->Kind == ImGuiAppNodeKind_Field   && parent->Kind == ImGuiAppNodeKind_Struct);
    if (!ok)
      return false;
    const int cout = AppNodeFirstPortKind(child, ImGuiAppPortKind_ChildOut);
    const int pin  = AppNodeFirstPortKind(parent, ImGuiAppPortKind_ChildIn);
    if (cout == 0 || pin == 0)
      return false;

    for (int li = g->Links.Size - 1; li >= 0; li--)
      if (g->Links.Data[li].Kind == ImGuiAppEdgeKind_Containment && g->Links.Data[li].StartAttr == cout)
        g->Links.erase(g->Links.Data + li);

    ImGuiAppNodeLink l;
    l.Id = AppGraphAllocId(g);
    l.StartAttr = cout;
    l.EndAttr = pin;
    l.Kind = ImGuiAppEdgeKind_Containment;
    g->Links.push_back(l);
    return true;
  }

  struct AppTreeCtx
  {
    int*  Sel;
    bool  KindVisible[ImGuiAppNodeKind_COUNT];
    int*  RenameNode;
    bool* RenameFocus;
    int   Act;       // 0 none / 1 delete / 2 duplicate / 3 explode / 4 collapse / 5 reparent
    int   ActNode;
    int   ActList;
    int   ActTarget; // reparent destination
    int   SetOpen;   // -1 none, 0 collapse-all, 1 expand-all (applied via SetNextItemOpen for one frame)
    bool  ShowLive;  // false: live-mirror rows are not listed (same toggle as the canvas)
    ImGuiWindow* HostRoot; // window hosting this outliner (captured before popups, whose root is the popup itself)
  };

  // Live Display-layer window behind a tree row (null for design rows and non-windows). Its eye/Hide
  // toggles the RUNNING window's Open -- the display layer then skips the window entirely. The window
  // hosting this composer is exempt: hiding the editor from inside itself is a softlock.
  static ImGuiAppWindowBase* AppTreeLiveWindow(ImGuiAppGraph* g, const ImGuiAppNode* n, const ImGuiWindow* host_root, bool* hosts_composer)
  {
    *hosts_composer = false;
    if (!n->IsLive || n->Kind != ImGuiAppNodeKind_Window)
      return nullptr;
    ImGuiAppWindowBase* wb = (ImGuiAppWindowBase*)AppGraphFindLiveItem(g->LiveApp, n);
    if (wb != nullptr && wb->Window != nullptr && host_root != nullptr)
      *hosts_composer = wb->Window == host_root;
    return wb;
  }

  // The closed live window an enclosed live row inherits its visibility from (a closed window
  // renders NONE of its members), or null when every enclosing live window is open.
  static ImGuiAppWindowBase* AppTreeClosedLiveHost(ImGuiAppGraph* g, const ImGuiAppNode* n, const ImGuiWindow* host_root)
  {
    if (!n->IsLive)
      return nullptr;
    bool hosts_composer = false;
    for (const ImGuiAppNode* p = AppGraphFindNodeConst(g, AppNodeTreeParent(g, n)); p != nullptr; p = AppGraphFindNodeConst(g, AppNodeTreeParent(g, p)))
    {
      ImGuiAppWindowBase* pw = AppTreeLiveWindow(g, p, host_root, &hosts_composer);
      if (pw != nullptr)
        return pw->Open ? nullptr : pw;
    }
    return nullptr;
  }

  // Right-aligned metadata for a tree row: a count of what the node holds (fields, hosted controls, commands).
  static void AppTreeRowMeta(const ImGuiAppGraph* g, const ImGuiAppNode* n, char* out, int out_size)
  {
    out[0] = 0;
    if (n->Kind == ImGuiAppNodeKind_Control)
    {
      ImVector<ImGuiAppFieldDesc> p, t;
      AppNodeEffectiveFields(g, n, 0, &p);
      AppNodeEffectiveFields(g, n, 1, &t);
      const int total = p.Size + t.Size;
      if (total > 0)
        ImFormatString(out, out_size, "%d %s", total, total == 1 ? "field" : "fields");
    }
    else if (n->Kind == ImGuiAppNodeKind_Struct)
    {
      const int cnt = AppGraphFieldNodeCount(g, n->Id, 0) > 0 ? AppGraphFieldNodeCount(g, n->Id, 0) : n->Draft.PersistFields.Size;
      if (cnt > 0)
        ImFormatString(out, out_size, "%d %s", cnt, cnt == 1 ? "field" : "fields");
    }
    else if (n->Kind == ImGuiAppNodeKind_Window || n->Kind == ImGuiAppNodeKind_Sidebar)
    {
      int hosted = 0;
      for (int i = 0; i < g->Nodes.Size; i++)
        if (g->Nodes.Data[i].Kind == ImGuiAppNodeKind_Control && AppGraphParentOf(g, g->Nodes.Data[i].Id) == n->Id)
          hosted++;
      if (hosted > 0)
        ImFormatString(out, out_size, "%d %s", hosted, hosted == 1 ? "control" : "controls");
    }
    else if (n->Kind == ImGuiAppNodeKind_Layer && n->LayerType == ImGuiAppLayerType_Command)
    {
      if (n->Commands.Size > 0)
        ImFormatString(out, out_size, "%d cmd", n->Commands.Size);
    }
  }

  static bool AppSelContains(const ImGuiAppGraph* g, int id)
  {
    for (int i = 0; i < g->Selection.Size; i++)
      if (g->Selection.Data[i] == id)
        return true;
    return false;
  }
  static void AppSelToggle(ImGuiAppGraph* g, int id)
  {
    for (int i = 0; i < g->Selection.Size; i++)
      if (g->Selection.Data[i] == id)
      {
        g->Selection.erase(g->Selection.Data + i);
        return;
      }
    g->Selection.push_back(id);
  }
  static void AppSelSet(ImGuiAppGraph* g, int id)
  {
    g->Selection.clear();
    if (id >= 0)
      g->Selection.push_back(id);
  }

  // Click a tree row: Ctrl toggles it in the multi-selection; plain click selects only it. Primary (*Sel) follows.
  static void AppTreeClick(ImGuiAppGraph* g, ImGuiAppNode* n, AppTreeCtx* c)
  {
    if (c->Sel == nullptr)
      return;
    if (ImGui::GetIO().KeyCtrl)
      AppSelToggle(g, n->Id);
    else
      AppSelSet(g, n->Id);
    *c->Sel = n->Id;
  }

  // A small flat icon "button" for the outliner row's eye + hover actions, drawn ENTIRELY on the draw list with a
  // manual hit-test (no ImGui item / no SetCursorScreenPos -- those would extend the tree window's boundaries and
  // trip ImGui's cursor-boundary assert). Returns true on a left click within the icon's circle.
  static bool AppTreeRowIcon(const char* icon, ImVec2 center, float r, ImU32 col, ImDrawList* dl_override)
  {
    // AllowWhenBlockedByActiveItem: the icon overlays the row's TreeNode item, and the mouse press makes
    // that item active BEFORE this hit-test runs -- plain IsWindowHovered() is false on exactly the click
    // frame (hover highlight worked, clicks never landed). See AppPtInRectHovered.
    // ChildWindows: the canvas gizmo column overlays the canvas's INNER child window -- without the flag the
    // hover test asks about the outer window, is always false there, and every gizmo is dead chrome.
    const ImVec2 m = ImGui::GetIO().MousePos;
    const float dx = m.x - center.x;
    const float dy = m.y - center.y;
    const bool hov = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && (dx * dx + dy * dy) <= r * r;
    ImDrawList* dl = dl_override != nullptr ? dl_override : ImGui::GetWindowDrawList();
    if (hov)
    {
      dl->AddCircleFilled(center, r, ImGui::GetColorU32(ImGuiCol_ButtonHovered));
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const ImVec2 ts = ImGui::CalcTextSize(icon);
    dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), hov ? AppThemeNeutral(0.96f) : col, icon);
    return hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  }

  static void AppTreeRenderNode(ImGuiAppGraph* g, int node_id, AppTreeCtx* c);   // fwd (recursive)

  // The right-click menu for a tree row -- records a deferred action (mutating g while iterating it is unsafe).
  static void AppTreeContextMenu(ImGuiAppGraph* g, ImGuiAppNode* n, AppTreeCtx* c)
  {
    if (!ImGui::BeginPopupContextItem("##ctx"))
      return;
    if (n->IsLive)
    {
      // Live rows offer the same Promote the canvas menu offers.
      ImGui::TextDisabled("live (read-only)");
      if (n->Kind == ImGuiAppNodeKind_Control)
      {
        ImGui::Separator();
        if (ImGui::MenuItem("Promote to design")) { c->Act = 6; c->ActNode = n->Id; }
      }
      ImGui::EndPopup();
      return;
    }
    if (n->Kind != ImGuiAppNodeKind_Layer && ImGui::MenuItem("Rename"))   { *c->RenameNode = n->Id; *c->RenameFocus = true; }
    if (n->Kind != ImGuiAppNodeKind_Layer && ImGui::MenuItem("Duplicate")) { c->Act = 2; c->ActNode = n->Id; }
    if (ImGui::MenuItem("Delete"))                                         { c->Act = 1; c->ActNode = n->Id; }
    // Prefab save serializes without mutating the graph -- safe inline mid-iteration (canvas menu parity).
    if (n->Kind != ImGuiAppNodeKind_Layer && ImGui::MenuItem("Save as prefab"))
    {
      ImVector<int> roots;
      roots.push_back(n->Id);
      AppGraphSavePrefab(g, roots, n->Draft.Name[0] ? n->Draft.Name : "prefab");
    }
    if (n->Kind == ImGuiAppNodeKind_Struct && !n->IsBuiltin)
    {
      ImGui::Separator();
      if (AppGraphFieldNodeCount(g, n->Id, 0) > 0)
      {
        if (ImGui::MenuItem("Collapse fields")) { c->Act = 4; c->ActNode = n->Id; c->ActList = 0; }
      }
      else if (ImGui::MenuItem("Explode fields", nullptr, false, n->Draft.PersistFields.Size > 0)) { c->Act = 3; c->ActNode = n->Id; c->ActList = 0; }
    }
    if (n->Kind == ImGuiAppNodeKind_Control && !n->IsBuiltin)
    {
      ImGui::Separator();
      for (int list = 0; list < 2; list++)
      {
        const char* lbl = list == 0 ? "PersistData" : "TempData";
        char item[64];
        if (AppControlStructId(g, n, list == 1) >= 0)
        {
          ImFormatString(item, IM_ARRAYSIZE(item), "Collapse %s", lbl);
          if (ImGui::MenuItem(item)) { c->Act = 4; c->ActNode = n->Id; c->ActList = list; }
        }
        else
        {
          ImFormatString(item, IM_ARRAYSIZE(item), "Explode %s", lbl);
          if (ImGui::MenuItem(item)) { c->Act = 3; c->ActNode = n->Id; c->ActList = list; }
        }
      }
    }

    // Visibility: hide/show this node (and its subtree) on the canvas, or isolate it (hide all other
    // design nodes). A live window row's Hide closes the RUNNING window instead; none for the
    // composer's own host window.
    ImGui::Separator();
    bool hosts_composer = false;
    ImGuiAppWindowBase* live_win = AppTreeLiveWindow(g, n, c->HostRoot, &hosts_composer);
    if (n->Kind != ImGuiAppNodeKind_Layer && !hosts_composer)
    {
      const bool visible = live_win != nullptr ? live_win->Open : !n->Hidden;
      if (ImGui::MenuItem(visible ? ICON_FA_EYE_SLASH "  Hide" : ICON_FA_EYE "  Show"))
      {
        if (live_win != nullptr)
          live_win->Open = !live_win->Open;
        else
          n->Hidden = !n->Hidden;
      }
    }
    if (ImGui::MenuItem(ICON_FA_EYE "  Isolate"))
    {
      ImVector<int> keep;
      AppGraphCollectSubtree(g, n->Id, &keep);
      for (int i = 0; i < g->Nodes.Size; i++)
        if (!g->Nodes.Data[i].IsLive && g->Nodes.Data[i].Kind != ImGuiAppNodeKind_Layer)   // foundation layers stay visible
          g->Nodes.Data[i].Hidden = !AppIdInSet(keep, g->Nodes.Data[i].Id);
    }
    if (ImGui::MenuItem(ICON_FA_EYE "  Show all"))
      for (int i = 0; i < g->Nodes.Size; i++)
        g->Nodes.Data[i].Hidden = false;

    ImGui::EndPopup();
  }

  static void AppTreeRenderNode(ImGuiAppGraph* g, int node_id, AppTreeCtx* c)
  {
    ImGuiAppNode* n = AppGraphFindNode(g, node_id);
    if (n == nullptr || !c->KindVisible[n->Kind])
      return;
    if (!c->ShowLive && n->IsLive)
      return;   // live mirror hidden: the outliner obeys the same toggle as the canvas

    // Structural children = nodes whose tree-parent is this node.
    ImVector<int> kids;
    for (int i = 0; i < g->Nodes.Size; i++)
      if (c->KindVisible[g->Nodes.Data[i].Kind] && AppNodeTreeParent(g, &g->Nodes.Data[i]) == n->Id)
        kids.push_back(g->Nodes.Data[i].Id);

    // Controls add component sub-rows: inline Persist/Temp (when not exploded), deps, commands.
    const bool is_control = n->Kind == ImGuiAppNodeKind_Control && !n->IsBuiltin && !n->IsLive;
    ImVector<int> deps;
    if (is_control)
      AppGraphConsumerDeps(g, n->Id, &deps);
    const bool inline_persist = is_control && AppControlStructId(g, n, false) < 0;
    const bool inline_temp    = is_control && AppControlStructId(g, n, true)  < 0;
    const bool has_children = kids.Size > 0 || (is_control && (deps.Size > 0 || n->Commands.Size > 0 || inline_persist || inline_temp));

    ImGui::PushID(n->Id);

    if (c->RenameNode != nullptr && *c->RenameNode == n->Id && !n->IsLive)
    {
      ImGui::AlignTextToFramePadding();
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (*c->RenameFocus)
      {
        ImGui::SetKeyboardFocusHere();
        *c->RenameFocus = false;
      }
      if (ImGui::InputText("##rn", n->Draft.Name, IM_ARRAYSIZE(n->Draft.Name), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
        *c->RenameNode = -1;
      if (ImGui::IsItemDeactivated())
        *c->RenameNode = -1;
      ImGui::PopID();
      return;
    }

    ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (!has_children)
      f |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet;
    if (AppSelContains(g, n->Id) || (c->Sel != nullptr && *c->Sel == n->Id))
      f |= ImGuiTreeNodeFlags_Selected;

    if (c->SetOpen >= 0 && has_children)
      ImGui::SetNextItemOpen(c->SetOpen == 1);

    // Row visibility, one truth for tint + eye: a live window row reads its RUNNING window's Open,
    // a live member row inherits an enclosing closed window, a design row reads canvas Hidden.
    bool hosts_composer = false;
    ImGuiAppWindowBase* live_win = AppTreeLiveWindow(g, n, c->HostRoot, &hosts_composer);
    ImGuiAppWindowBase* closed_host = live_win == nullptr ? AppTreeClosedLiveHost(g, n, c->HostRoot) : nullptr;
    const bool row_off = live_win != nullptr ? !live_win->Open : closed_host != nullptr ? true : n->Hidden;

    const ImU32 tint = AppGraphOriginColor(n);
    ImU32 row_col = tint ? tint : (n->Kind == ImGuiAppNodeKind_Layer ? AppLayerAccent(n->LayerType) : AppKindColor(n->Kind));
    if (row_off)
      row_col = (row_col & 0x00FFFFFF) | (IM_COL32(0, 0, 0, 110) & 0xFF000000);   // faded when not rendering (hidden / closed window)
    ImGui::PushStyleColor(ImGuiCol_Text, row_col);
    // Origin rides the row tint (and the canvas title dot); no text suffix.
    const bool open = ImGui::TreeNodeEx("##row", f, "%s  %s", AppNodeIcon(n), n->Draft.Name[0] ? n->Draft.Name : "(unnamed)");
    ImGui::PopStyleColor();

    // Capture row interaction now, before the overlay icon buttons below change the "current item".
    const bool row_clicked  = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();
    const bool row_dblclick = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    const bool row_hovered  = ImGui::IsItemHovered();
    const ImVec2 rmn = ImGui::GetItemRectMin();
    const ImVec2 rmx = ImGui::GetItemRectMax();

    // Brushing: report this row's node; echo a hover that originated in another view as a faint accent
    // fill + a solid left edge (never selection -- hover previews, it does not commit).
    if (row_hovered)
      AppGraphHoverNode(g, n->Id, ImGuiAppHoverSource_Tree);
    ImGuiAppHoverSource brush_src = ImGuiAppHoverSource_None;
    if (AppGraphHoveredNode(g, &brush_src) == n->Id && brush_src != ImGuiAppHoverSource_Tree && brush_src != ImGuiAppHoverSource_None && !row_hovered)
    {
      ImDrawList* bdl = ImGui::GetWindowDrawList();
      bdl->AddRectFilled(rmn, rmx, (row_col & 0x00FFFFFF) | 0x24000000);
      bdl->AddRectFilled(rmn, ImVec2(rmn.x + ImGui::GetFontSize() * 0.1875f, rmx.y), row_col);
    }
    // Ambient problem mark: severity underline along the row bottom (same hue as the canvas dot).
    if (const int row_sev = AppGraphNodeSeverity(g, n->Id))
    {
      const float sev_th = ImMax(1.0f, ImGui::GetFontSize() * 0.0625f);
      ImGui::GetWindowDrawList()->AddLine(ImVec2(rmn.x, rmx.y - sev_th), ImVec2(rmx.x, rmx.y - sev_th),
                                          (AppSeverityColor(row_sev) & 0x00FFFFFF) | 0xB4000000, sev_th);
    }

    // Right-edge overlay (pure draw-list, manual hit-test -- no ImGui items, so the layout cursor is untouched):
    // an always-on eye (hide/show this subtree), hover-revealed rename / duplicate / delete, else the meta count.
    // Handled BEFORE the row's own click so an icon click consumes the press instead of also selecting the row.
    bool icon_clicked = false;
    {
      const float rem = ImGui::GetFontSize();
      const float r = rem * 0.62f;
      const float cy = (rmn.y + rmx.y) * 0.5f;
      float       x = rmx.x - r - rem * 0.2f;

      // Foundation layers are always visible (permanent base) -- no eye toggle for them. A live
      // window row's eye drives the RUNNING window's Open (the display layer skips a closed window);
      // no eye at all for the window hosting this composer. A member of a closed live window
      // inherits the closed state; its eye reads slashed and a click reopens the host window.
      if (n->Kind != ImGuiAppNodeKind_Layer && !hosts_composer)
      {
        const bool visible = !row_off;
        const char* eye_icon = visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH;
        const ImU32 eye_col = !visible ? AppComposerGetStyle()->LayerCommand : ImGui::GetColorU32(ImGuiCol_Text, row_hovered ? 0.85f : 0.3f);
        if (AppTreeRowIcon(eye_icon, ImVec2(x, cy), r, eye_col))
        {
          if (live_win != nullptr)
            live_win->Open = !live_win->Open;
          else if (closed_host != nullptr)
            closed_host->Open = true;
          else
            n->Hidden = !n->Hidden;
          icon_clicked = true;
        }
        x -= r * 2.0f + rem * 0.05f;
      }

      if (row_hovered && !n->IsLive)
      {
        if (AppTreeRowIcon(ICON_FA_TRASH, ImVec2(x, cy), r, AppComposerGetStyle()->Danger))
        {
          c->Act = 1;
          c->ActNode = n->Id;
          icon_clicked = true;
        }
        x -= r * 2.0f + rem * 0.05f;
        if (n->Kind != ImGuiAppNodeKind_Layer)
        {
          const ImU32 ac = ImGui::GetColorU32(ImGuiCol_Text, 0.7f);
          if (AppTreeRowIcon(ICON_FA_CLONE, ImVec2(x, cy), r, ac))
          {
            c->Act = 2;
            c->ActNode = n->Id;
            icon_clicked = true;
          }
          x -= r * 2.0f + rem * 0.05f;
          if (AppTreeRowIcon(ICON_FA_PEN, ImVec2(x, cy), r, ac))
          {
            *c->RenameNode = n->Id;
            *c->RenameFocus = true;
            icon_clicked = true;
          }
        }
      }
      else
      {
        char meta[32];
        AppTreeRowMeta(g, n, meta, IM_ARRAYSIZE(meta));
        if (meta[0])
        {
          const ImVec2 ts = ImGui::CalcTextSize(meta);
          ImGui::GetWindowDrawList()->AddText(ImVec2(x - ts.x, cy - ts.y * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), meta);
        }
      }
    }

    if (row_clicked && !icon_clicked)
      AppTreeClick(g, n, c);
    if (row_dblclick && !icon_clicked && !n->IsLive && c->RenameNode != nullptr)
    {
      *c->RenameNode = n->Id;
      *c->RenameFocus = true;
    }
    AppTreeContextMenu(g, n, c);

    // Drag-reparent: drag a Control onto a Window/Sidebar (host it), or a Field onto a Struct.
    if (!n->IsLive && (n->Kind == ImGuiAppNodeKind_Control || n->Kind == ImGuiAppNodeKind_Field) && ImGui::BeginDragDropSource())
    {
      ImGui::SetDragDropPayload("APPNODE", &n->Id, sizeof(int));
      ImGui::Text("%s", n->Draft.Name[0] ? n->Draft.Name : "(unnamed)");
      ImGui::EndDragDropSource();
    }
    if (!n->IsLive && (n->Kind == ImGuiAppNodeKind_Window || n->Kind == ImGuiAppNodeKind_Sidebar || n->Kind == ImGuiAppNodeKind_Struct) && ImGui::BeginDragDropTarget())
    {
      if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("APPNODE"))
      {
        c->Act = 5;
        c->ActNode = *(const int*)pl->Data;
        c->ActTarget = n->Id;
      }
      ImGui::EndDragDropTarget();
    }

    if (open)
    {
      // Indent guide: a faint vertical line down the children's left edge.
      const float guide_x = ImGui::GetCursorScreenPos().x + ImGui::GetStyle().IndentSpacing * 0.5f;
      const float guide_y0 = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y * 0.5f;

      for (int i = 0; i < kids.Size; i++)
        AppTreeRenderNode(g, kids.Data[i], c);

      if (is_control)
      {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        if (inline_persist)
          ImGui::BulletText("PersistData (%d fields)", n->Draft.PersistFields.Size);
        if (inline_temp)
          ImGui::BulletText("TempData (%d fields)", n->Draft.TempFields.Size);
        ImGui::PopStyleColor();
        if (deps.Size > 0 && ImGui::TreeNodeEx("##deps", ImGuiTreeNodeFlags_SpanAvailWidth, "deps (%d)", deps.Size))
        {
          for (int d = 0; d < deps.Size; d++)
          {
            const ImGuiAppNode* dn = AppGraphFindNodeConst(g, deps.Data[d]);
            ImGui::BulletText("%s", dn != nullptr ? dn->Draft.Name : "(dep)");
          }
          ImGui::TreePop();
        }
        if (n->Commands.Size > 0 && ImGui::TreeNodeEx("##cmds", ImGuiTreeNodeFlags_SpanAvailWidth, "commands (%d)", n->Commands.Size))
        {
          for (int cm = 0; cm < n->Commands.Size; cm++)
            ImGui::BulletText("%s", n->Commands.Data[cm].Name);
          ImGui::TreePop();
        }
      }
      const float guide_y1 = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y * 0.5f;
      ImGui::GetWindowDrawList()->AddLine(ImVec2(guide_x, guide_y0), ImVec2(guide_x, guide_y1), ImGui::GetColorU32(ImGuiCol_Separator, 0.5f), 1.0f);
      ImGui::TreePop();
    }
    ImGui::PopID();
  }

  void ShowAppGraphTree(const ImGuiApp* app, ImGuiAppGraph* g, int* selected_node_id, bool show_live)
  {
    IM_ASSERT(g != nullptr);
    IM_UNUSED(app);   // the graph already mirrors the live app, so it is the single outliner source


    AppTreeCtx ctx;
    ctx.Sel = selected_node_id;
    ctx.RenameNode = &AppGraphEditorState(g)->OutlinerRename;
    ctx.RenameFocus = &AppGraphEditorState(g)->OutlinerRenameFocus;
    ctx.Act = 0;
    ctx.ActNode = -1;
    ctx.ActList = 0;
    ctx.ActTarget = -1;
    ctx.SetOpen = -1;

    ctx.ShowLive = show_live;
    ctx.HostRoot = ImGui::GetCurrentWindowRead() != nullptr ? ImGui::GetCurrentWindowRead()->RootWindow : nullptr;

    // Per-kind node counts (drive the filter buttons' badges); also tally hidden nodes for a
    // "show all" affordance. Hidden live rows are not listed, so they are not counted either.
    int kind_count[ImGuiAppNodeKind_COUNT] = { 0 };
    int total_nodes = 0;
    int hidden_count = 0;
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      if (!show_live && g->Nodes.Data[i].IsLive)
        continue;
      kind_count[g->Nodes.Data[i].Kind]++;
      total_nodes++;
      if (g->Nodes.Data[i].Hidden)
        hidden_count++;
    }

    // Header bar: title + total count, with show-hidden + collapse/expand-all on the right.
    const float em = ImGui::GetFontSize();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(ICON_FA_LAYER_GROUP "  Outliner");
    ImGui::SameLine();
    ImGui::TextDisabled("%d", total_nodes);
    if (hidden_count > 0)
    {
      ImGui::SameLine();
      if (AppBlFilterButton("##showhidden", ICON_FA_EYE_SLASH, hidden_count, true, AppComposerGetStyle()->LayerCommand))
        for (int i = 0; i < g->Nodes.Size; i++)
          g->Nodes.Data[i].Hidden = false;
      ImGui::SetItemTooltip("%d hidden -- click to show all", hidden_count);
    }
    {
      // Right-align via a spacer relative to the REMAINING line width (never an absolute coord -- that could land
      // past the clip rect and ImGui would clip the button dead). Falls back to a small gap when the panel is narrow.
      const float bw = ImGui::GetFrameHeight() * 2.0f + em * 0.3f;
      const float avail = ImGui::GetContentRegionAvail().x;
      ImGui::SameLine(0.0f, avail > bw + em * 0.5f ? avail - bw : em * 0.5f);
      if (AppBlToggleButton("##expall", ICON_FA_ANGLE_DOWN, false, AppKindColor(ImGuiAppNodeKind_Control)))
        ctx.SetOpen = 1;
      ImGui::SetItemTooltip("Expand all");
      ImGui::SameLine(0.0f, em * 0.3f);
      if (AppBlToggleButton("##colall", ICON_FA_ANGLE_RIGHT, false, AppKindColor(ImGuiAppNodeKind_Control)))
        ctx.SetOpen = 0;
      ImGui::SetItemTooltip("Collapse all");
    }

    // Kind filter buttons: icon + count; click toggles visibility of that kind.
    static const ImGuiAppNodeKind filter_kinds[] =
    {
      ImGuiAppNodeKind_Layer, ImGuiAppNodeKind_Window, ImGuiAppNodeKind_Sidebar,
      ImGuiAppNodeKind_Control, ImGuiAppNodeKind_Struct, ImGuiAppNodeKind_Field,
    };
    for (int i = 0; i < IM_ARRAYSIZE(filter_kinds); i++)
    {
      const ImGuiAppNodeKind k = filter_kinds[i];
      if (i > 0)
      {
        // Flow-wrap: at narrow panel widths the buttons wrap to a second row instead of clipping dead.
        ImGui::SameLine(0.0f, 3.0f);
        if (ImGui::GetContentRegionAvail().x < em * 3.2f)
          ImGui::NewLine();
      }
      ImGui::PushID(i);
      if (AppBlFilterButton("##kindfilter", AppKindIcon(k), kind_count[k], AppGraphEditorState(g)->OutlinerKindVis[k], AppKindColor(k)))
        AppGraphEditorState(g)->OutlinerKindVis[k] = !AppGraphEditorState(g)->OutlinerKindVis[k];
      ImGui::PopID();
      ImGui::SetItemTooltip("%s (%d)", AppNodeKindName(k), kind_count[k]);
    }

    // Search box: name filter. When active, results are shown flat (vs the browse hierarchy).
    ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 2.0f);
    ImGui::SetNextItemAllowOverlap();
    AppGraphEditorState(g)->OutlinerFilter.Draw(ICON_FA_MAGNIFYING_GLASS "##search");
    ImGui::SameLine();
    if (AppRowDeleteButton("##clearsearch"))
      AppGraphEditorState(g)->OutlinerFilter.Clear();

    for (int k = 0; k < ImGuiAppNodeKind_COUNT; k++)
      ctx.KindVisible[k] = AppGraphEditorState(g)->OutlinerKindVis[k];

    ImGui::Separator();

    if (total_nodes == 0)
    {
      ImGui::Spacing();
      ImGui::TextDisabled("No nodes yet.");
      ImGui::TextDisabled("Right-click the canvas or press Space to add.");
      return;
    }
    if (AppGraphEditorState(g)->OutlinerFilter.IsActive())
    {
      // Flat filtered results (kind-visible + name match).
      for (int i = 0; i < g->Nodes.Size; i++)
      {
        ImGuiAppNode* n = &g->Nodes.Data[i];
        if (!ctx.KindVisible[n->Kind] || !AppGraphEditorState(g)->OutlinerFilter.PassFilter(n->Draft.Name))
          continue;
        if (!show_live && n->IsLive)
          continue;
        ImGui::PushID(n->Id);
        bool hosts_composer = false;
        ImGuiAppWindowBase* live_win = AppTreeLiveWindow(g, n, ctx.HostRoot, &hosts_composer);
        const bool row_off = live_win != nullptr ? !live_win->Open
                           : AppTreeClosedLiveHost(g, n, ctx.HostRoot) != nullptr ? true : n->Hidden;
        const ImU32 tint = AppGraphOriginColor(n);
        ImU32 row_col = tint ? tint : (n->Kind == ImGuiAppNodeKind_Layer ? AppLayerAccent(n->LayerType) : AppKindColor(n->Kind));
        if (row_off)
          row_col = (row_col & 0x00FFFFFF) | (IM_COL32(0, 0, 0, 110) & 0xFF000000);   // faded when not rendering
        ImGui::PushStyleColor(ImGuiCol_Text, row_col);
        char label[IM_LABEL_SIZE + 32];
        ImFormatString(label, IM_ARRAYSIZE(label), "%s  %s  (%s)", AppNodeIcon(n), n->Draft.Name[0] ? n->Draft.Name : "(unnamed)", AppNodeKindName(n->Kind));
        if (ImGui::Selectable(label, AppSelContains(g, n->Id) || (selected_node_id && *selected_node_id == n->Id)))
          AppTreeClick(g, n, &ctx);
        ImGui::PopStyleColor();
        AppTreeContextMenu(g, n, &ctx);
        ImGui::PopID();
      }
    }
    else
    {
      // Browse hierarchy: DESIGN roots first, then a dim band, then LIVE mirror roots -- two populations
      // must not read as one list.
      bool live_band_drawn = false;
      for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < g->Nodes.Size; i++)
        {
          const ImGuiAppNode* rn = &g->Nodes.Data[i];
          if (rn->IsLive != (pass == 1) || AppNodeTreeParent(g, rn) != -1)
            continue;
          if (pass == 1 && !live_band_drawn && ctx.ShowLive)
          {
            live_band_drawn = true;
            ImGui::Spacing();
            ImGui::TextDisabled(ICON_FA_EYE "  live mirror");
            ImGui::Separator();
          }
          AppTreeRenderNode(g, g->Nodes.Data[i].Id, &ctx);
        }
    }

    // Apply the deferred row action (now safe to mutate g->Nodes).
    if (ctx.Act != 0 && ctx.ActNode >= 0)
    {
      ImGuiAppNode* an = AppGraphFindNode(g, ctx.ActNode);
      if (an != nullptr)
      {
        if (ctx.Act == 1)
        {
          // Delete the whole multi-selection if the acted node is part of it; else just that node.
          ImVector<int> victims;
          if (AppSelContains(g, ctx.ActNode) && g->Selection.Size > 1)
            victims = g->Selection;
          else
            victims.push_back(ctx.ActNode);
          for (int v = 0; v < victims.Size; v++)
          {
            const ImGuiAppNode* vn = AppGraphFindNode(g, victims.Data[v]);
            if (vn != nullptr && !vn->IsLive)
              AppGraphRemoveNode(g, victims.Data[v]);
            else if (vn != nullptr && vn->IsLive)
              AppNotifyLiveReadOnly(g, vn);   // outliner delete reaching a live victim: refuse with the notice
          }
          g->Selection.clear();
          if (selected_node_id) *selected_node_id = -1;
        }
        else if (ctx.Act == 2)
        {
          AppGraphDuplicateNode(g, an);
        }
        else if (ctx.Act == 3)
        {
          if (an->Kind == ImGuiAppNodeKind_Control) AppGraphExplodeControlData(g, an, ctx.ActList == 1);
          else                                      AppGraphExplodeFields(g, an, ctx.ActList);
        }
        else if (ctx.Act == 4)
        {
          if (an->Kind == ImGuiAppNodeKind_Control) AppGraphCollapseControlData(g, an, ctx.ActList == 1);
          else                                      AppGraphCollapseFields(g, an, ctx.ActList);
        }
        else if (ctx.Act == 5)
        {
          AppGraphReparent(g, ctx.ActNode, ctx.ActTarget);
        }
        else if (ctx.Act == 6)
        {
          // Promote a live control to an editable design twin (canvas menu parity; see ##AppGraphNodeCtx).
          const ImVec2 near_pos = an->GridPos + ImVec2(260.0f, 0.0f);
          ImGuiAppNode* d = AppGraphAddNode(g, ImGuiAppNodeKind_Control, an->Draft.Name[0] ? an->Draft.Name : "NewControl");
          ImStrncpy(d->DataTypeName, an->DataTypeName, IM_ARRAYSIZE(d->DataTypeName));
          AppGraphPlaceNode(g, d, &near_pos);
          if (selected_node_id) *selected_node_id = d->Id;
        }
      }
    }
  }
}
