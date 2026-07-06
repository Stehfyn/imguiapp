// Previewer interpreter core (F67). Freezes to docs/previewer-design.md (F66). The interpreter is a
// SECOND backend beside codegen: it builds a real ImGuiApp from the authored graph and evaluates the same
// model every frame, emitting nothing. It reuses shipped rails -- RegisterAppStorage, the Task/Command/
// Window passes, the temp^last skew, ImGuiAppStateHistory -- so "what the preview does" equals "what the
// generated code does".
//
// This module re-implements a handful of tiny graph readers (name sanitize, effective fields, consumer
// deps, port owner/parent) locally rather than exporting them, to keep imguiapp_nodes.cpp untouched under
// parallel edits. The public rails AppGraphTopoOrder / AppNodeStructTypeId / AppEventExprCheck are reused.
//
// SCOPE (F67): App / Layer / Window / Sidebar / Control(design-draft) / Struct / Field / events / commands.
// Op-fold evaluation (F55) and animation-builtin dt update (F56) are named STUBS -- grep "F55" / "F56".

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"

#include "imguiapp_preview.h"

#include <string.h>   // memcpy, memset, strcmp, strlen
#include <stdlib.h>   // strtod

//-----------------------------------------------------------------------------
// [SECTION] Store manifest + session structures
//-----------------------------------------------------------------------------

namespace
{
  enum { AppPvUnknown = -2 };   // a builtin dep field: opaque to the checker, compatible with everything

  // One slot in a control's Persist or Temp sub-buffer. Offsets are relative to the sub-buffer base.
  struct AppPvSlot
  {
    char              Name[IM_LABEL_SIZE];   // sanitized identifier (codegen spelling)
    ImGuiAppFieldType Type;
    int               ArraySize;
    int               Offset;
    int               Size;
  };

  // A Persist bool the interpreter allocates per (instance, command): set on the event edge in OnUpdate,
  // emitted by OnGetCommand -- the interpreter form of codegen's <Cmd>Pending latch.
  struct AppPvLatch
  {
    int Offset;         // within the Persist sub-buffer
    int CommandValue;   // ImGuiAppCommand assigned to this command name
  };

  // One resolved data binding: persist[Dst] = producer_persist[Src] every Task frame (design 4.1 step 1).
  struct AppPvBinding
  {
    ImGuiID   ProducerDataTypeId;
    AppPvSlot Src;   // slot in the producer's Persist manifest
    AppPvSlot Dst;   // slot in this control's Persist manifest
  };

  // A dependency producer, for expression dep-param roots (design 4.5).
  struct AppPvDep
  {
    int     ProducerNodeId;
    ImGuiID ProducerDataTypeId;
  };

  // Scripted input: the headless equivalent of a widget recording into TempData during OnRender. F68
  // replaces this with real widget input on the composed window surface.
  struct AppPvScript
  {
    int    NodeId;
    char   Field[IM_LABEL_SIZE];
    double Value;
  };

  struct AppPvDispatch
  {
    int Tick;
    int Command;
  };

  struct AppPvCommandName
  {
    char Name[IM_LABEL_SIZE];
    int  Value;
  };

  // The on-camera surface + selection-brushing channel (design 8), bundled off the session. All transient;
  // never snapshotted. Composer -> preview: Brush* name the node whose widget group haloes. Preview ->
  // composer: HoverNode/ClickNode report the node the mouse is over / clicked, latched until taken.
  struct AppPvSurface
  {
    bool Enabled;        // OnRender submits real widgets into the current window (else headless CORE)
    int  BrushSelected;  // composer -> preview halo target (primary selection node id, -1 none)
    int  BrushHover;     // composer -> preview halo target (hovered node id, -1 none)
    int  HoverNode;      // preview -> composer: node under the mouse this frame (-1 none)
    int  ClickNode;      // preview -> composer: node whose panel was clicked (latched, -1 none)

    AppPvSurface() { Enabled = false; BrushSelected = -1; BrushHover = -1; HoverNode = -1; ClickNode = -1; }
  };

  // Per interpreter control instance. The value store is a flat byte buffer laid out Persist|LastTemp|Temp
  // (InstanceData order) and registered through RegisterAppStorage, so snapshot/restore/replay/state-hash
  // apply verbatim. Records are heap-allocated and held by pointer so their addresses stay stable.
  struct AppPvInstance
  {
    int                      NodeId;
    ImGuiID                  DataTypeId;
    ImVector<AppPvSlot>      Persist;       // effective persist fields
    ImVector<AppPvSlot>      Temp;          // effective temp fields
    int                      PersistBytes;  // Persist sub-buffer size (fields + latches, 8-rounded)
    int                      TempBytes;     // Temp sub-buffer size
    ImVector<AppPvLatch>     Latches;
    ImVector<AppPvBinding>   Bindings;
    ImVector<AppPvDep>       Deps;
    ImVector<ImGuiAppEventDesc> Events;     // copied off the node (a CORE run does not edit the graph)
    bool                     IsBuiltin;     // builtin control with no interpreter rule -> reflected only (design 9)
  };
}

// One heap session per previewed document (design 2). Owns the interpreter ImGuiApp, the instances, the
// scripted-input list, the dispatch log, and the command-name table. Not serialized, not snapshotted.
struct ImGuiAppPreview
{
  const ImGuiAppGraph*        Graph;        // borrowed, read-only; never mutated by running it
  ImGuiApp*                   App;          // a real framework app (ImGuiAppPreviewApp)
  ImVector<AppPvInstance*>    Instances;
  ImVector<AppPvScript>       Scripts;
  ImVector<AppPvDispatch>     Dispatches;
  ImVector<AppPvCommandName>  Commands;
  int                         Tick;
  AppPvSurface                Surface;      // F68 surface + brushing channel (design 8)

  ImGuiAppPreview() { Graph = nullptr; App = nullptr; Tick = 0; }
};

namespace
{
  //---------------------------------------------------------------------------
  // [SECTION] Local graph readers (see module header note)
  //---------------------------------------------------------------------------

  void AppPvSanitize(char* dst, size_t dst_size, const char* src)
  {
    size_t n = 0;
    if (src == nullptr || src[0] == 0) { ImStrncpy(dst, "Control", dst_size); return; }
    if (src[0] >= '0' && src[0] <= '9' && n + 1 < dst_size) dst[n++] = '_';
    for (const char* s = src; *s != 0 && n + 1 < dst_size; s++)
    {
      const char c = *s;
      const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
      dst[n++] = keep ? c : '_';
    }
    dst[n] = 0;
  }

  void AppPvSnake(char* dst, size_t dst_size, const char* src)
  {
    char id[IM_LABEL_SIZE];
    AppPvSanitize(id, IM_ARRAYSIZE(id), src);
    size_t n = 0;
    for (const char* s = id; *s != 0 && n + 1 < dst_size; s++)
    {
      const char c = *s;
      if (c >= 'A' && c <= 'Z') { if (s != id && n + 2 < dst_size) dst[n++] = '_'; dst[n++] = (char)(c - 'A' + 'a'); }
      else dst[n++] = c;
    }
    dst[n] = 0;
  }

  const ImGuiAppNode* AppPvFindNode(const ImGuiAppGraph* g, int node_id)
  {
    for (int i = 0; i < g->Nodes.Size; i++)
      if (g->Nodes.Data[i].Id == node_id) return &g->Nodes.Data[i];
    return nullptr;
  }

  int AppPvPortOwner(const ImGuiAppGraph* g, int port_id)
  {
    for (int i = 0; i < g->Nodes.Size; i++)
      for (int p = 0; p < g->Nodes.Data[i].Ports.Size; p++)
        if (g->Nodes.Data[i].Ports.Data[p].Id == port_id) return g->Nodes.Data[i].Id;
    return -1;
  }

  int AppPvParentOf(const ImGuiAppGraph* g, int child_node_id)
  {
    for (int li = 0; li < g->Links.Size; li++)
    {
      if (g->Links.Data[li].Kind != ImGuiAppEdgeKind_Containment) continue;
      if (AppPvPortOwner(g, g->Links.Data[li].StartAttr) != child_node_id) continue;
      return AppPvPortOwner(g, g->Links.Data[li].EndAttr);
    }
    return -1;
  }

  // Effective field list (design 6.1): exploded Field nodes when present, else the inline draft list.
  // Mirror of AppNodeEffectiveFields (imguiapp_nodes.cpp) so this TU stays independent.
  void AppPvEffectiveFields(const ImGuiAppGraph* g, const ImGuiAppNode* owner, int list, ImVector<ImGuiAppFieldDesc>* out)
  {
    out->clear();
    bool exploded = false;
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* fn = &g->Nodes.Data[i];
      if (fn->Kind != ImGuiAppNodeKind_Field || fn->FieldList != list || AppPvParentOf(g, fn->Id) != owner->Id) continue;
      exploded = true;
      if (fn->Draft.PersistFields.Size == 0) continue;
      ImGuiAppFieldDesc fd = fn->Draft.PersistFields.Data[0];
      ImStrncpy(fd.Name, fn->Draft.Name, IM_ARRAYSIZE(fd.Name));
      out->push_back(fd);
    }
    if (!exploded)
      *out = (list == 1) ? owner->Draft.TempFields : owner->Draft.PersistFields;
  }

  // Distinct producer node ids feeding a consumer control via data edges (mirror of AppGraphConsumerDeps).
  void AppPvConsumerDeps(const ImGuiAppGraph* g, int consumer_node_id, ImVector<int>* out_producers)
  {
    out_producers->clear();
    const ImGuiAppNode* consumer = AppPvFindNode(g, consumer_node_id);
    const int own_persist = consumer != nullptr ? consumer->PersistStructId : -1;
    const int own_temp    = consumer != nullptr ? consumer->TempStructId    : -1;
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const int producer_id = g->Nodes.Data[i].Id;
      for (int li = 0; li < g->Links.Size; li++)
      {
        if (g->Links.Data[li].Kind != ImGuiAppEdgeKind_Data) continue;
        if (AppPvPortOwner(g, g->Links.Data[li].EndAttr) != consumer_node_id) continue;
        if (AppPvPortOwner(g, g->Links.Data[li].StartAttr) != producer_id) continue;
        int dep_id = producer_id;
        const ImGuiAppNode* pn = AppPvFindNode(g, producer_id);
        if (pn != nullptr && pn->Kind == ImGuiAppNodeKind_Field)
        {
          const int sid = AppPvParentOf(g, producer_id);
          if (sid >= 0) dep_id = sid;
        }
        if (dep_id == consumer_node_id || dep_id == own_persist || dep_id == own_temp) continue;
        bool dup = false;
        for (int d = 0; d < out_producers->Size; d++) if (out_producers->Data[d] == dep_id) { dup = true; break; }
        if (!dup) out_producers->push_back(dep_id);
      }
    }
  }

  // The runtime data-flow key of a control's DataOut (design 3): the id its storage registers under and
  // dependents resolve by. Reads the stamped port, falling back to the "<Name>Data" hash.
  ImGuiID AppPvControlDataTypeId(const ImGuiAppNode* n)
  {
    for (int p = 0; p < n->Ports.Size; p++)
      if (n->Ports.Data[p].Kind == ImGuiAppPortKind_DataOut && n->Ports.Data[p].DataTypeId != 0)
        return n->Ports.Data[p].DataTypeId;
    return ImGui::AppNodeStructTypeId(n->Draft.Name);
  }

  //---------------------------------------------------------------------------
  // [SECTION] Field sizing + manifest layout (design 6.1)
  //---------------------------------------------------------------------------

  const ImGuiAppNode* AppPvFindStructNode(const ImGuiAppGraph* g, const char* type_name)
  {
    for (int i = 0; i < g->Nodes.Size; i++)
    {
      const ImGuiAppNode* sn = &g->Nodes.Data[i];
      if (sn->Kind != ImGuiAppNodeKind_Struct) continue;
      char id[IM_LABEL_SIZE];
      AppPvSanitize(id, IM_ARRAYSIZE(id), sn->Draft.Name);
      if (strcmp(sn->Draft.Name, type_name) == 0 || strcmp(id, type_name) == 0) return sn;
    }
    return nullptr;
  }

  int AppPvBuildManifestList(const ImGuiAppGraph* g, const ImGuiAppNode* owner, int list, ImVector<AppPvSlot>* out, int depth);

  // Byte size of one field at natural alignment. Struct fields recurse into the referenced Struct node.
  int AppPvFieldSize(const ImGuiAppGraph* g, const ImGuiAppFieldDesc* f, int depth)
  {
    switch (f->Type)
    {
    case ImGuiAppFieldType_Bool:   return 1;
    case ImGuiAppFieldType_Int:    return 4;
    case ImGuiAppFieldType_Float:  return 4;
    case ImGuiAppFieldType_Double: return 8;
    case ImGuiAppFieldType_Vec2:   return 8;
    case ImGuiAppFieldType_Vec4:   return 16;
    case ImGuiAppFieldType_String: return f->ArraySize > 0 ? f->ArraySize : 128;
    case ImGuiAppFieldType_Struct:
    {
      if (depth > 8) return 0;   // cycle guard (dangling/cyclic struct types are a validate issue)
      const ImGuiAppNode* sn = AppPvFindStructNode(g, f->StructType);
      if (sn == nullptr) return 0;
      ImVector<AppPvSlot> nested;
      return AppPvBuildManifestList(g, sn, 0, &nested, depth + 1);
    }
    default: return 0;
    }
  }

  int AppPvFieldAlign(const ImGuiAppGraph* g, const ImGuiAppFieldDesc* f, int depth)
  {
    switch (f->Type)
    {
    case ImGuiAppFieldType_Bool:   return 1;
    case ImGuiAppFieldType_String: return 1;
    case ImGuiAppFieldType_Double: return 8;
    case ImGuiAppFieldType_Struct:
    {
      // Aligned to its widest member; 4 is a safe floor for the scalar leaf set.
      IM_UNUSED(g); IM_UNUSED(depth);
      return 8;
    }
    default: return 4;   // Int / Float / Vec2 / Vec4 (float-based)
    }
  }

  int AppPvAlignUp(int off, int align) { return (off + align - 1) / align * align; }

  // Pack one field list into slots at natural alignment; returns the sub-buffer byte size (8-rounded).
  int AppPvBuildManifestList(const ImGuiAppGraph* g, const ImGuiAppNode* owner, int list, ImVector<AppPvSlot>* out, int depth)
  {
    out->clear();
    ImVector<ImGuiAppFieldDesc> fields;
    AppPvEffectiveFields(g, owner, list, &fields);
    int off = 0;
    for (int i = 0; i < fields.Size; i++)
    {
      const ImGuiAppFieldDesc* f = &fields.Data[i];
      AppPvSlot s;
      AppPvSanitize(s.Name, IM_ARRAYSIZE(s.Name), f->Name);
      s.Type = f->Type;
      s.ArraySize = f->ArraySize;
      s.Size = AppPvFieldSize(g, f, depth);
      const int align = AppPvFieldAlign(g, f, depth);
      off = AppPvAlignUp(off, align);
      s.Offset = off;
      off += s.Size;
      out->push_back(s);
    }
    return AppPvAlignUp(off, 8);
  }

  const AppPvSlot* AppPvFindSlot(const ImVector<AppPvSlot>& m, const char* sanitized_name)
  {
    for (int i = 0; i < m.Size; i++)
      if (strcmp(m.Data[i].Name, sanitized_name) == 0) return &m.Data[i];
    return nullptr;
  }

  //---------------------------------------------------------------------------
  // [SECTION] Slot read / write (memcpy: buffers are packed, sub-buffer aligned)
  //---------------------------------------------------------------------------

  double AppPvReadSlot(const char* base, const AppPvSlot* s)
  {
    const char* p = base + s->Offset;
    switch (s->Type)
    {
    case ImGuiAppFieldType_Bool:   { bool v; memcpy(&v, p, 1); return v ? 1.0 : 0.0; }
    case ImGuiAppFieldType_Int:    { int v; memcpy(&v, p, 4); return (double)v; }
    case ImGuiAppFieldType_Float:  { float v; memcpy(&v, p, 4); return (double)v; }
    case ImGuiAppFieldType_Double: { double v; memcpy(&v, p, 8); return v; }
    default: return 0.0;   // Vec2/Vec4/String/Struct: not a scalar expression value (CORE)
    }
  }

  void AppPvWriteSlot(char* base, const AppPvSlot* s, double value)
  {
    char* p = base + s->Offset;
    switch (s->Type)
    {
    case ImGuiAppFieldType_Bool:   { bool v = value != 0.0; memcpy(p, &v, 1); break; }
    case ImGuiAppFieldType_Int:    { int v = (int)value; memcpy(p, &v, 4); break; }
    case ImGuiAppFieldType_Float:  { float v = (float)value; memcpy(p, &v, 4); break; }
    case ImGuiAppFieldType_Double: { double v = value; memcpy(p, &v, 8); break; }
    default: break;
    }
  }

  //---------------------------------------------------------------------------
  // [SECTION] The expression evaluator (design 4.5) -- value-returning parallel walk of AppEventExprCheck's
  // grammar (imguiapp_nodes.cpp AppExprOr..AppExprPrimary). The checker already proved every construct
  // re-parseable and type-fitting, so this is the second visitor of one grammar (single-authority rule).
  //---------------------------------------------------------------------------

  struct AppPvValue
  {
    int    Type;   // ImGuiAppFieldType_* or AppPvUnknown
    double Num;    // bool as 0/1, else numeric
  };

  struct AppPvEval
  {
    ImGuiAppPreview*     Session;
    const AppPvInstance* Inst;
    char*                Buffer;   // this control's instance buffer
    const char*          Cur;
    bool                 Failed;
  };

  char* AppPvInstanceBuffer(ImGuiAppPreview* s, ImGuiID data_type_id);
  const AppPvInstance* AppPvFindInstance(const ImGuiAppPreview* s, int node_id);

  bool AppPvIsBoolish(int t) { return t == ImGuiAppFieldType_Bool || t == AppPvUnknown; }

  int AppPvPromote(int a, int b)
  {
    if (a == AppPvUnknown || b == AppPvUnknown) return AppPvUnknown;
    if (a == ImGuiAppFieldType_Double || b == ImGuiAppFieldType_Double) return ImGuiAppFieldType_Double;
    if (a == ImGuiAppFieldType_Float || b == ImGuiAppFieldType_Float) return ImGuiAppFieldType_Float;
    return ImGuiAppFieldType_Int;
  }

  void AppPvSkip(AppPvEval* c) { while (*c->Cur == ' ' || *c->Cur == '\t') c->Cur++; }

  bool AppPvAccept(AppPvEval* c, const char* op)
  {
    AppPvSkip(c);
    const size_t len = strlen(op);
    if (strncmp(c->Cur, op, len) != 0) return false;
    if (len == 1 && op[0] == '-' && c->Cur[1] == '>') return false;
    if (len == 1 && op[0] == '!' && c->Cur[1] == '=') return false;
    if (len == 1 && (op[0] == '<' || op[0] == '>') && c->Cur[1] == '=') return false;
    c->Cur += len;
    return true;
  }

  bool AppPvIdent(AppPvEval* c, char* out, int out_size)
  {
    AppPvSkip(c);
    const char* s = c->Cur;
    if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || *s == '_')) return false;
    int n = 0;
    while ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_')
    { if (n < out_size - 1) out[n++] = *s; s++; }
    out[n] = 0;
    c->Cur = s;
    return true;
  }

  AppPvValue AppPvOr(AppPvEval* c);   // fwd (parens recurse to the top)

  // Resolve a `root->field[.member...]` primary to a scalar value from live storage.
  AppPvValue AppPvResolveRef(AppPvEval* c, const char* root)
  {
    AppPvValue v;
    v.Type = AppPvUnknown;
    v.Num = 0.0;

    const AppPvInstance* owner = c->Inst;
    char* base = nullptr;
    const ImVector<AppPvSlot>* manifest = nullptr;
    bool builtin_dep = false;

    if (strcmp(root, "temp_data") == 0)      { base = c->Buffer + owner->PersistBytes + owner->TempBytes; manifest = &owner->Temp; }
    else if (strcmp(root, "last_temp_data") == 0) { base = c->Buffer + owner->PersistBytes; manifest = &owner->Temp; }
    else if (strcmp(root, "data") == 0)      { base = c->Buffer; manifest = &owner->Persist; }
    else
    {
      // A dependency param name = snake(producer node name). Read the producer's Persist (list 0).
      for (int d = 0; d < owner->Deps.Size && base == nullptr; d++)
      {
        const ImGuiAppNode* dn = AppPvFindNode(c->Session->Graph, owner->Deps.Data[d].ProducerNodeId);
        if (dn == nullptr) continue;
        char param[IM_LABEL_SIZE];
        AppPvSnake(param, IM_ARRAYSIZE(param), dn->Draft.Name);
        if (strcmp(param, root) != 0) continue;
        const AppPvInstance* pi = AppPvFindInstance(c->Session, dn->Id);
        if (pi == nullptr) continue;
        base = AppPvInstanceBuffer(c->Session, owner->Deps.Data[d].ProducerDataTypeId);
        manifest = &pi->Persist;
        builtin_dep = dn->IsBuiltin;   // builtin dep fields (F56 animators) are opaque to the manifest
      }
    }

    if (!AppPvAccept(c, "->")) { c->Failed = true; return v; }
    char field[IM_LABEL_SIZE];
    if (!AppPvIdent(c, field, IM_ARRAYSIZE(field))) { c->Failed = true; return v; }

    const AppPvSlot* slot = (manifest != nullptr) ? AppPvFindSlot(*manifest, field) : nullptr;

    // Struct member chains (design 4.5): consumed here but resolved as opaque. The manifest packs nested
    // structs, but the flat slot does not retain its StructType, so a scalar-leaf descent is a follow-on
    // (F67 CORE reads top-level scalars, which the producer/consumer acceptance uses).
    while (true)
    {
      AppPvSkip(c);
      if (*c->Cur != '.') break;
      c->Cur++;
      char member[IM_LABEL_SIZE];
      if (!AppPvIdent(c, member, IM_ARRAYSIZE(member))) { c->Failed = true; return v; }
      slot = nullptr;   // opaque past the first hop
    }

    if (slot != nullptr && base != nullptr)
    {
      v.Type = slot->Type;
      v.Num = AppPvReadSlot(base, slot);
    }
    else if (builtin_dep)
    {
      v.Type = AppPvUnknown;   // F56: value comes from the animator's interpreted store once F56 lands
    }
    return v;
  }

  AppPvValue AppPvPrimary(AppPvEval* c)
  {
    AppPvValue v;
    v.Type = AppPvUnknown;
    v.Num = 0.0;
    AppPvSkip(c);

    if (AppPvAccept(c, "("))
    {
      v = AppPvOr(c);
      if (!AppPvAccept(c, ")")) c->Failed = true;
      return v;
    }

    if ((c->Cur[0] >= '0' && c->Cur[0] <= '9') || (c->Cur[0] == '.' && c->Cur[1] >= '0' && c->Cur[1] <= '9'))
    {
      const char* start = c->Cur;
      bool is_float = false;
      while (*c->Cur >= '0' && *c->Cur <= '9') c->Cur++;
      if (*c->Cur == '.') { is_float = true; c->Cur++; while (*c->Cur >= '0' && *c->Cur <= '9') c->Cur++; }
      char buf[64];
      int len = (int)(c->Cur - start);
      if (len > 63) len = 63;
      memcpy(buf, start, len);
      buf[len] = 0;
      if (*c->Cur == 'f' || *c->Cur == 'F') { is_float = true; c->Cur++; }
      v.Type = is_float ? ImGuiAppFieldType_Float : ImGuiAppFieldType_Int;
      v.Num = strtod(buf, nullptr);
      return v;
    }

    char ident[IM_LABEL_SIZE];
    if (!AppPvIdent(c, ident, IM_ARRAYSIZE(ident))) { c->Failed = true; return v; }
    if (strcmp(ident, "true") == 0)  { v.Type = ImGuiAppFieldType_Bool; v.Num = 1.0; return v; }
    if (strcmp(ident, "false") == 0) { v.Type = ImGuiAppFieldType_Bool; v.Num = 0.0; return v; }
    return AppPvResolveRef(c, ident);
  }

  AppPvValue AppPvUnary(AppPvEval* c)
  {
    if (AppPvAccept(c, "!"))
    {
      AppPvValue t = AppPvUnary(c);
      AppPvValue r; r.Type = ImGuiAppFieldType_Bool; r.Num = (t.Num != 0.0) ? 0.0 : 1.0;
      return r;
    }
    if (AppPvAccept(c, "-")) { AppPvValue t = AppPvUnary(c); t.Num = -t.Num; return t; }
    if (AppPvAccept(c, "+")) { return AppPvUnary(c); }
    return AppPvPrimary(c);
  }

  AppPvValue AppPvMul(AppPvEval* c)
  {
    AppPvValue t = AppPvUnary(c);
    while (!c->Failed)
    {
      char op;
      if      (AppPvAccept(c, "*")) op = '*';
      else if (AppPvAccept(c, "/")) op = '/';
      else if (AppPvAccept(c, "%")) op = '%';
      else break;
      AppPvValue r = AppPvUnary(c);
      if (op == '%')      { long long a = (long long)t.Num, b = (long long)r.Num; t.Num = (b != 0) ? (double)(a % b) : 0.0; t.Type = ImGuiAppFieldType_Int; }
      else if (op == '*') { t.Num = t.Num * r.Num; t.Type = AppPvPromote(t.Type, r.Type); }
      else                { t.Num = (r.Num != 0.0) ? (t.Num / r.Num) : 0.0; t.Type = AppPvPromote(t.Type, r.Type); }
    }
    return t;
  }

  AppPvValue AppPvAdd(AppPvEval* c)
  {
    AppPvValue t = AppPvMul(c);
    while (!c->Failed)
    {
      char op;
      if      (AppPvAccept(c, "+")) op = '+';
      else if (AppPvAccept(c, "-")) op = '-';
      else break;
      AppPvValue r = AppPvMul(c);
      t.Num = (op == '+') ? (t.Num + r.Num) : (t.Num - r.Num);
      t.Type = AppPvPromote(t.Type, r.Type);
    }
    return t;
  }

  AppPvValue AppPvRel(AppPvEval* c)
  {
    AppPvValue t = AppPvAdd(c);
    while (!c->Failed)
    {
      int op;
      if      (AppPvAccept(c, "<=")) op = 0;
      else if (AppPvAccept(c, ">=")) op = 1;
      else if (AppPvAccept(c, "<"))  op = 2;
      else if (AppPvAccept(c, ">"))  op = 3;
      else break;
      AppPvValue r = AppPvAdd(c);
      bool b = false;
      switch (op) { case 0: b = t.Num <= r.Num; break; case 1: b = t.Num >= r.Num; break; case 2: b = t.Num < r.Num; break; default: b = t.Num > r.Num; break; }
      t.Type = ImGuiAppFieldType_Bool; t.Num = b ? 1.0 : 0.0;
    }
    return t;
  }

  AppPvValue AppPvEq(AppPvEval* c)
  {
    AppPvValue t = AppPvRel(c);
    while (!c->Failed)
    {
      bool eq;
      if      (AppPvAccept(c, "==")) eq = true;
      else if (AppPvAccept(c, "!=")) eq = false;
      else break;
      AppPvValue r = AppPvRel(c);
      const bool same = (t.Num == r.Num);
      t.Type = ImGuiAppFieldType_Bool; t.Num = (same == eq) ? 1.0 : 0.0;
    }
    return t;
  }

  AppPvValue AppPvXor(AppPvEval* c)
  {
    AppPvValue t = AppPvEq(c);
    while (!c->Failed && AppPvAccept(c, "^"))
    {
      AppPvValue r = AppPvEq(c);
      if (AppPvIsBoolish(t.Type) && AppPvIsBoolish(r.Type)) { t.Num = ((t.Num != 0.0) != (r.Num != 0.0)) ? 1.0 : 0.0; t.Type = ImGuiAppFieldType_Bool; }
      else { long long a = (long long)t.Num, b = (long long)r.Num; t.Num = (double)(a ^ b); t.Type = ImGuiAppFieldType_Int; }
    }
    return t;
  }

  AppPvValue AppPvAnd(AppPvEval* c)
  {
    AppPvValue t = AppPvXor(c);
    while (!c->Failed && AppPvAccept(c, "&&"))
    {
      AppPvValue r = AppPvXor(c);
      t.Num = ((t.Num != 0.0) && (r.Num != 0.0)) ? 1.0 : 0.0; t.Type = ImGuiAppFieldType_Bool;
    }
    return t;
  }

  AppPvValue AppPvOr(AppPvEval* c)
  {
    AppPvValue t = AppPvAnd(c);
    while (!c->Failed && AppPvAccept(c, "||"))
    {
      AppPvValue r = AppPvAnd(c);
      t.Num = ((t.Num != 0.0) || (r.Num != 0.0)) ? 1.0 : 0.0; t.Type = ImGuiAppFieldType_Bool;
    }
    return t;
  }

  // Evaluate an event Expr against live storage. On any parse failure returns 0 (the checker already blessed
  // it at build; a runtime failure is a defensive default, not a crash).
  double AppPvEvalExpr(ImGuiAppPreview* s, const AppPvInstance* inst, char* buffer, const char* expr)
  {
    AppPvEval c;
    c.Session = s;
    c.Inst = inst;
    c.Buffer = buffer;
    c.Cur = expr;
    c.Failed = false;
    AppPvValue v = AppPvOr(&c);
    return c.Failed ? 0.0 : v.Num;
  }
}

//-----------------------------------------------------------------------------
// [SECTION] Interpreter control + app (design 3, 4)
//-----------------------------------------------------------------------------

// A real framework ImGuiApp instance. The interpreter builds ONE and pushes interpreter controls into it,
// so UpdateApp/RenderApp drive the four phases with no special-casing (design 2). Its OnExecuteCommand
// records dispatches into the session's log (design 4.3) -- there is no user handler to run.
struct ImGuiAppPreviewApp : ImGuiApp
{
  ImGuiAppPreview* Session = nullptr;

  virtual void OnExecuteCommand(ImGuiAppCommand cmd) override
  {
    if (Session != nullptr)
    {
      AppPvDispatch d;
      d.Tick = Session->Tick;
      d.Command = (int)cmd;
      Session->Dispatches.push_back(d);
    }
    ImGuiApp::OnExecuteCommand(cmd);
  }
};

// One compiled control stands in for every interpreted control (design 3): its "type" is the manifest data
// it carries. Const like every framework control; all mutation lands in the registered instance buffer.
struct ImGuiAppPreviewControl : ImGuiAppControlBase
{
  ImGuiAppPreview* Session = nullptr;
  AppPvInstance*   Inst = nullptr;
  bool             Hosted = false;   // window/sidebar-hosted -> render the field panel (design 4.6 / 8.1)

  virtual ImGuiID GetControlDataID() const override final { return Inst != nullptr ? Inst->DataTypeId : 0; }

  virtual void OnInitialize(ImGuiApp* app) const override final
  {
    // Default-initialise Persist from field defaults: the buffer is allocated zeroed, which IS the default
    // for the plain-scalar field vocabulary codegen emits. (Op/animation defaults land with F55/F56.)
    IM_UNUSED(app);
  }

  virtual void OnShutdown(ImGuiApp* app) const override final { IM_UNUSED(app); }

  virtual void OnUpdate(const ImGuiApp* app, float dt) const override final
  {
    IM_UNUSED(dt);
    char* buffer = (char*)app->Data.GetVoidPtr(Inst->DataTypeId);
    if (buffer == nullptr) return;

    char* persist = buffer;
    char* last_temp = buffer + Inst->PersistBytes;
    char* temp = buffer + Inst->PersistBytes + Inst->TempBytes;

    // Latches reset every frame; only an edge this frame re-sets them (dispatch-once per edge, design 4.2).
    for (int i = 0; i < Inst->Latches.Size; i++) { bool f = false; memcpy(persist + Inst->Latches.Data[i].Offset, &f, 1); }

    // 1. Dependency binding lines: persist[Dst] = producer_persist[Src] (design 4.1 step 1).
    for (int i = 0; i < Inst->Bindings.Size; i++)
    {
      const AppPvBinding* b = &Inst->Bindings.Data[i];
      char* prod = (char*)app->Data.GetVoidPtr(b->ProducerDataTypeId);
      if (prod == nullptr) continue;
      const double v = AppPvReadSlot(prod, &b->Src);
      AppPvWriteSlot(persist, &b->Dst, v);
    }

    // F55 STUB: Op subtrees folded into an event Expr are evaluated by AppPvEvalExpr (id-0 result pins fan
    // out freely). A future Op node kind (ImGuiAppNodeKind_Op) folds to the same expression string here --
    // no separate runtime object. Nothing to do until the Op kind exists in the base (F54).

    // F56 STUB: animation-builtin dt update (Tween/Timer/Spring/Pulse) would run its closed-form rule over
    // this control's Persist accumulator HERE, before events, when Inst->IsBuiltin names an animator. See
    // vocabulary-nodes-design.md 2.1. Left unimplemented: the builtins do not exist in this base (F56).
    IM_UNUSED(Inst->IsBuiltin);

    // 2. Events, in authored order (design 4.2).
    for (int e = 0; e < Inst->Events.Size; e++)
    {
      const ImGuiAppEventDesc* ev = &Inst->Events.Data[e];
      char wid[IM_LABEL_SIZE];
      AppPvSanitize(wid, IM_ARRAYSIZE(wid), ev->TempField);
      const AppPvSlot* watched = AppPvFindSlot(Inst->Temp, wid);
      if (watched == nullptr) continue;

      const double tv = AppPvReadSlot(temp, watched);
      const double lv = AppPvReadSlot(last_temp, watched);
      bool fired = false;
      switch (ev->Edge)
      {
      case ImGuiAppEventEdge_Rising:  fired = (tv != 0.0) && (lv == 0.0); break;
      case ImGuiAppEventEdge_Falling: fired = (tv == 0.0) && (lv != 0.0); break;
      case ImGuiAppEventEdge_Changed: fired = (tv != lv); break;   // temp ^ last
      case ImGuiAppEventEdge_Active:  fired = (tv != 0.0); break;
      default: fired = false; break;
      }
      if (!fired) continue;

      if (ev->Action == ImGuiAppEventAction_SetField)
      {
        char dst[IM_LABEL_SIZE];
        AppPvSanitize(dst, IM_ARRAYSIZE(dst), ev->DstField);
        const AppPvSlot* dslot = AppPvFindSlot(Inst->Persist, dst);
        if (dslot != nullptr)
        {
          const double val = (ev->Expr[0] == 0) ? tv : AppPvEvalExpr(Session, Inst, buffer, ev->Expr);
          AppPvWriteSlot(persist, dslot, val);
        }
      }
      else if (ev->Action == ImGuiAppEventAction_EmitCommand)
      {
        const int target = AppPvCommandValueForEvent(Session, ev->Command);
        for (int li = 0; li < Inst->Latches.Size; li++)
          if (Inst->Latches.Data[li].CommandValue == target)
          {
            bool t = true;
            memcpy(persist + Inst->Latches.Data[li].Offset, &t, 1);
            break;
          }
      }
    }

    // The temp^last swap: last_temp <- temp (mirror of ImGuiInterfaceAdapter::OnUpdate). Next frame's edge
    // test compares the next recorded input against this frame's.
    if (Inst->TempBytes > 0) memcpy(last_temp, temp, Inst->TempBytes);
  }

  virtual void OnGetCommand(const ImGuiApp* app, ImGuiAppCommand* cmd) const override final
  {
    char* buffer = (char*)app->Data.GetVoidPtr(Inst->DataTypeId);
    if (buffer == nullptr) return;
    // At most one command per control (framework contract). Emit the first set latch.
    for (int i = 0; i < Inst->Latches.Size; i++)
    {
      bool set = false;
      memcpy(&set, buffer + Inst->Latches.Data[i].Offset, 1);
      if (set) { *cmd = (ImGuiAppCommand)Inst->Latches.Data[i].CommandValue; return; }
    }
  }

  virtual void OnRender(const ImGuiApp* app) const override final;

private:
  // Command value for an event's command name -- defined out-of-line (needs the session table).
  static int AppPvCommandValueForEvent(ImGuiAppPreview* s, const char* name);
};

//-----------------------------------------------------------------------------
// [SECTION] Session helpers (instance/buffer lookup, command table)
//-----------------------------------------------------------------------------

namespace
{
  const AppPvInstance* AppPvFindInstance(const ImGuiAppPreview* s, int node_id)
  {
    for (int i = 0; i < s->Instances.Size; i++)
      if (s->Instances.Data[i]->NodeId == node_id) return s->Instances.Data[i];
    return nullptr;
  }

  char* AppPvInstanceBuffer(ImGuiAppPreview* s, ImGuiID data_type_id)
  {
    return (char*)s->App->Data.GetVoidPtr(data_type_id);
  }

  // Assign (or find) a stable command value for a name, past the framework's reserved range.
  int AppPvCommandValue(ImGuiAppPreview* s, const char* name)
  {
    char base[IM_LABEL_SIZE];
    AppPvSanitize(base, IM_ARRAYSIZE(base), name);
    for (int i = 0; i < s->Commands.Size; i++)
      if (strcmp(s->Commands.Data[i].Name, base) == 0) return s->Commands.Data[i].Value;
    AppPvCommandName cn;
    ImStrncpy(cn.Name, base, IM_ARRAYSIZE(cn.Name));
    cn.Value = (int)ImGuiAppCommand_COUNT + s->Commands.Size;
    s->Commands.push_back(cn);
    return cn.Value;
  }

  void AppPvDestroyBuffer(void* p) { IM_FREE(p); }

#ifndef IMGUIX_DISABLE_TOOLS   // TOOL: F68 preview-surface widgets + brushing (Phase A3)
  void AppPvFieldWidget(const AppPvSlot* sl, char* p)
  {
    switch (sl->Type)
    {
    case ImGuiAppFieldType_Bool:   { bool v; memcpy(&v, p, 1); if (ImGui::Checkbox(sl->Name, &v)) memcpy(p, &v, 1); break; }
    case ImGuiAppFieldType_Int:    { ImGui::DragInt(sl->Name, (int*)p); break; }
    case ImGuiAppFieldType_Float:  { ImGui::DragFloat(sl->Name, (float*)p, 0.01f); break; }
    case ImGuiAppFieldType_Double: { float v = (float)(*(double*)p); if (ImGui::DragFloat(sl->Name, &v, 0.01f)) *(double*)p = v; break; }
    default: ImGui::TextDisabled("%s", sl->Name); break;
    }
  }

  // Manifest-bound widget panel (design 8.1) with selection brushing (design 8.2): the field switch of
  // AppMockRenderFields, rewritten to read/write manifest offsets in live storage, wrapped in one titled
  // group so the panel is a single hit-target. The surface reports the hovered/clicked node back to the
  // composer, and haloes the group when the composer's selection/hover names this node. Runs for a hosted or
  // surface control inside the current window; app-level headless controls (the CORE path) issue no ImGui.
  void AppPvRenderFields(ImGuiAppPreview* s, const AppPvInstance* inst, char* buffer, const char* label)
  {
    ImGui::PushID(inst->NodeId);
    ImGui::BeginGroup();

    ImGui::TextUnformatted((label != nullptr && label[0] != 0) ? label : "Control");
    ImGui::Separator();

    // Temp fields are the input surface (write the Temp sub-buffer -> next frame's edge test consumes it).
    char* temp = buffer + inst->PersistBytes + inst->TempBytes;
    for (int i = 0; i < inst->Temp.Size; i++)
    {
      ImGui::PushID(i);
      AppPvFieldWidget(&inst->Temp.Data[i], temp + inst->Temp.Data[i].Offset);
      ImGui::PopID();
    }
    // Persist fields shown bound to live storage (state; poke-able, design 5).
    char* persist = buffer;
    for (int i = 0; i < inst->Persist.Size; i++)
    {
      ImGui::PushID(1000 + i);
      AppPvFieldWidget(&inst->Persist.Data[i], persist + inst->Persist.Data[i].Offset);
      ImGui::PopID();
    }
    ImGui::EndGroup();

    // Brushing (design 8.2). Preview -> composer: the group's hover/click publish this node. Composer ->
    // preview: halo the group when the selection (primary) or hover names it -- theme-derived, em-padded.
    const ImVec2 gmin = ImGui::GetItemRectMin();
    const ImVec2 gmax = ImGui::GetItemRectMax();
    const float  em   = ImGui::GetFontSize();
    const float  pad  = em * 0.25f;
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseHoveringRect(gmin, gmax))
    {
      s->Surface.HoverNode = inst->NodeId;
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) s->Surface.ClickNode = inst->NodeId;
    }
    if (inst->NodeId == s->Surface.BrushSelected || inst->NodeId == s->Surface.BrushHover)
    {
      const bool  primary = (inst->NodeId == s->Surface.BrushSelected);
      const ImU32 col = ImGui::GetColorU32(primary ? ImGuiCol_NavHighlight : ImGuiCol_Border);
      ImGui::GetWindowDrawList()->AddRect(gmin - ImVec2(pad, pad), gmax + ImVec2(pad, pad), col, em * 0.35f, 0, primary ? 2.0f : 1.0f);
    }

    ImGui::Spacing();
    ImGui::PopID();
  }
#endif // IMGUIX_DISABLE_TOOLS
}

int ImGuiAppPreviewControl::AppPvCommandValueForEvent(ImGuiAppPreview* s, const char* name)
{
  return AppPvCommandValue(s, name);
}

void ImGuiAppPreviewControl::OnRender(const ImGuiApp* app) const
{
  char* buffer = (char*)app->Data.GetVoidPtr(Inst->DataTypeId);
  if (buffer == nullptr) return;

  // OnRender records TempData: zero the Temp sub-buffer first (framework contract), then record input.
  char* temp = buffer + Inst->PersistBytes + Inst->TempBytes;
  if (Inst->TempBytes > 0) memset(temp, 0, Inst->TempBytes);

  // Headless input seam: apply the session's scripted inputs for this node (the widget-record equivalent).
  for (int i = 0; i < Session->Scripts.Size; i++)
  {
    if (Session->Scripts.Data[i].NodeId != Inst->NodeId) continue;
    const AppPvSlot* slot = AppPvFindSlot(Inst->Temp, Session->Scripts.Data[i].Field);
    if (slot != nullptr) AppPvWriteSlot(temp, slot, Session->Scripts.Data[i].Value);
  }

  // Real widget input on the composed window surface (design 8.1). Guarded to hosted/surface controls so the
  // headless CORE (app-level controls, no window/NewFrame) makes no ImGui calls.
#ifndef IMGUIX_DISABLE_TOOLS   // TOOL: surface widget input (Phase A3) -- core->UI call site
  if (Hosted || Session->Surface.Enabled)
    AppPvRenderFields(Session, Inst, buffer, Label);
#endif // IMGUIX_DISABLE_TOOLS
}

//-----------------------------------------------------------------------------
// [SECTION] Session build + teardown (design 2, 6, 7)
//-----------------------------------------------------------------------------

namespace
{
  // Build one control instance (manifest + latches + bindings + deps + events) and register its store.
  void AppPvPushControl(ImGuiAppPreview* s, const ImGuiAppNode* n, ImGuiAppWindowBase* host)
  {
    ImGuiApp* app = s->App;
    AppPvInstance* inst = IM_NEW(AppPvInstance)();
    inst->NodeId = n->Id;
    inst->DataTypeId = AppPvControlDataTypeId(n);
    inst->IsBuiltin = n->IsBuiltin;

    // Manifests (design 6.1): effective persist/temp fields packed at natural alignment.
    int persist_fields_bytes = AppPvBuildManifestList(s->Graph, n, 0, &inst->Persist, 0);
    inst->TempBytes = AppPvBuildManifestList(s->Graph, n, 1, &inst->Temp, 0);

    // Command latches: one Persist bool per distinct EmitCommand command, appended after the persist fields.
    int latch_off = persist_fields_bytes;
    for (int e = 0; e < n->Events.Size; e++)
    {
      const ImGuiAppEventDesc* ev = &n->Events.Data[e];
      if (ev->Action != ImGuiAppEventAction_EmitCommand || ev->Command[0] == 0) continue;
      const int cmd_value = AppPvCommandValue(s, ev->Command);
      bool have = false;
      for (int li = 0; li < inst->Latches.Size; li++) if (inst->Latches.Data[li].CommandValue == cmd_value) { have = true; break; }
      if (have) continue;
      AppPvLatch latch;
      latch.Offset = latch_off;
      latch.CommandValue = cmd_value;
      inst->Latches.push_back(latch);
      latch_off += 1;
    }
    inst->PersistBytes = AppPvAlignUp(latch_off, 8);

    // Events: only keep those that type-check (single authority: AppEventExprCheck). A malformed event
    // would not compile either, so it is dropped rather than run.
    for (int e = 0; e < n->Events.Size; e++)
    {
      char eerr[192];
      if (ImGui::AppEventExprCheck(s->Graph, n, &n->Events.Data[e], eerr, IM_ARRAYSIZE(eerr)))
        inst->Events.push_back(n->Events.Data[e]);
    }

    // Deps (for expression dep-param roots).
    ImVector<int> deps;
    AppPvConsumerDeps(s->Graph, n->Id, &deps);
    for (int d = 0; d < deps.Size; d++)
    {
      const ImGuiAppNode* dn = AppPvFindNode(s->Graph, deps.Data[d]);
      if (dn == nullptr || dn->Kind != ImGuiAppNodeKind_Control) continue;   // Struct/Field deps: no store of their own here
      AppPvDep dep;
      dep.ProducerNodeId = dn->Id;
      dep.ProducerDataTypeId = AppPvControlDataTypeId(dn);
      inst->Deps.push_back(dep);
    }

    // Bindings: resolve each data link into this consumer that carries a field binding.
    for (int li = 0; li < s->Graph->Links.Size; li++)
    {
      if (s->Graph->Links.Data[li].Kind != ImGuiAppEdgeKind_Data) continue;
      if (AppPvPortOwner(s->Graph, s->Graph->Links.Data[li].EndAttr) != n->Id) continue;
      const int link_id = s->Graph->Links.Data[li].Id;
      const int prod_id = AppPvPortOwner(s->Graph, s->Graph->Links.Data[li].StartAttr);
      const ImGuiAppNode* prod = AppPvFindNode(s->Graph, prod_id);
      if (prod == nullptr || prod->Kind != ImGuiAppNodeKind_Control) continue;
      const AppPvInstance* pi = AppPvFindInstance(s, prod_id);
      if (pi == nullptr) continue;   // producer must be built first (topo order guarantees it)
      for (int bi = 0; bi < s->Graph->Bindings.Size; bi++)
      {
        if (s->Graph->Bindings.Data[bi].LinkId != link_id) continue;
        char dst[IM_LABEL_SIZE]; AppPvSanitize(dst, IM_ARRAYSIZE(dst), s->Graph->Bindings.Data[bi].DstField);
        char src[IM_LABEL_SIZE]; AppPvSanitize(src, IM_ARRAYSIZE(src), s->Graph->Bindings.Data[bi].SrcField);
        const AppPvSlot* dslot = AppPvFindSlot(inst->Persist, dst);
        const AppPvSlot* sslot = AppPvFindSlot(pi->Persist, src);
        if (dslot == nullptr || sslot == nullptr) continue;
        AppPvBinding b;
        b.ProducerDataTypeId = AppPvControlDataTypeId(prod);
        b.Src = *sslot;
        b.Dst = *dslot;
        inst->Bindings.push_back(b);
      }
    }

    // Allocate the value store zeroed and register it (design 6.2): Persist | LastTemp | Temp, with the
    // input (TempData) byte range at the tail so the [0, temp_offset) prefix is the snapshottable state.
    const int temp_offset = inst->PersistBytes + inst->TempBytes;
    const int total = inst->PersistBytes + 2 * inst->TempBytes;
    void* buffer = IM_ALLOC(total > 0 ? total : 1);
    memset(buffer, 0, total > 0 ? total : 1);
    app->Data.SetVoidPtr(inst->DataTypeId, buffer);
    ImGui::RegisterAppStorage(app, inst->DataTypeId, buffer, total, temp_offset, inst->TempBytes, AppPvDestroyBuffer);

    ImGuiAppPreviewControl* control = IM_NEW(ImGuiAppPreviewControl)();
    control->Session = s;
    control->Inst = inst;
    control->Hosted = (host != nullptr);
    ImStrncpy(control->Label, n->Draft.Name, IM_ARRAYSIZE(control->Label));

    if (host != nullptr) host->Controls.push_back(control);
    else                 app->Controls.push_back(control);
    control->OnInitialize(app);

    s->Instances.push_back(inst);
  }

  // Build the interpreter app for a session: the framework core layers plus one interpreter control per
  // Control node in the given topo order (design 2). Shared by AppPreviewCreate and AppPreviewReconcile so
  // both stand up ONE identical population; the caller owns topo (reconcile must test it before teardown).
  void AppPvBuildPopulation(ImGuiAppPreview* s, const ImVector<int>& order)
  {
    ImGuiAppPreviewApp* app = IM_NEW(ImGuiAppPreviewApp)();
    app->Session = s;
    s->App = app;

    // Task (mutate), Command (dispatch), Display (render app-level + hosted controls' OnRender). Status is
    // omitted -- it submits a real ImGui window; the surface hosts controls in the composer's own window.
    ImGui::PushAppLayer<ImGuiAppTaskLayer>(app);
    ImGui::PushAppLayer<ImGuiAppCommandLayer>(app);
    ImGui::PushAppLayer<ImGuiAppDisplayLayer>(app);

    for (int i = 0; i < order.Size; i++)
    {
      const ImGuiAppNode* n = AppPvFindNode(s->Graph, order.Data[i]);
      if (n == nullptr) continue;
      AppPvPushControl(s, n, nullptr);
    }
  }

  // A captured control's snapshottable bytes (design 7): the Persist + LastTemp regions with their manifests,
  // kept across a reconcile so surviving (sanitized name, type) slots carry their values into the rebuild.
  // Held by HEAP POINTER (like AppPvInstance): its nested ImVectors are not bitwise-copyable, so it must never
  // live as a value element of an ImVector (ImVector copies bitwise and would dangle the inner buffers).
  struct AppPvCapture
  {
    int                 NodeId;
    ImVector<AppPvSlot> Persist;
    ImVector<AppPvSlot> Temp;
    ImVector<char>      PersistData;    // copy of the Persist sub-buffer ([0, PersistBytes))
    ImVector<char>      LastTempData;   // copy of the LastTemp sub-buffer ([PersistBytes, +TempBytes))
  };

  void AppPvCaptureInstances(ImGuiAppPreview* s, ImVector<AppPvCapture*>* out)
  {
    out->clear();
    for (int i = 0; i < s->Instances.Size; i++)
    {
      const AppPvInstance* inst = s->Instances.Data[i];
      const char* buffer = (const char*)s->App->Data.GetVoidPtr(inst->DataTypeId);
      if (buffer == nullptr) continue;
      AppPvCapture* cap = IM_NEW(AppPvCapture)();
      cap->NodeId = inst->NodeId;
      cap->Persist = inst->Persist;   // POD element copy (ImVector operator= memcpy is correct for AppPvSlot)
      cap->Temp = inst->Temp;
      cap->PersistData.resize(inst->PersistBytes);
      if (inst->PersistBytes > 0) memcpy(cap->PersistData.Data, buffer, (size_t)inst->PersistBytes);
      cap->LastTempData.resize(inst->TempBytes);
      if (inst->TempBytes > 0) memcpy(cap->LastTempData.Data, buffer + inst->PersistBytes, (size_t)inst->TempBytes);
      out->push_back(cap);
    }
  }

  // Copy every surviving (sanitized name, type) slot from the capture into a freshly-built instance; new and
  // retyped slots keep their zero default (design 7). Preserves Persist + LastTemp -- Temp is re-recorded input.
  void AppPvRestoreInstance(ImGuiAppPreview* s, const AppPvInstance* inst, const ImVector<AppPvCapture*>& caps)
  {
    const AppPvCapture* cap = nullptr;
    for (int i = 0; i < caps.Size; i++) if (caps.Data[i]->NodeId == inst->NodeId) { cap = caps.Data[i]; break; }
    if (cap == nullptr) return;   // a new node -> default-initialised

    char* buffer = (char*)s->App->Data.GetVoidPtr(inst->DataTypeId);
    if (buffer == nullptr) return;

    for (int i = 0; i < inst->Persist.Size; i++)
    {
      const AppPvSlot* ns = &inst->Persist.Data[i];
      for (int j = 0; j < cap->Persist.Size; j++)
      {
        const AppPvSlot* os = &cap->Persist.Data[j];
        if (os->Type == ns->Type && os->Size == ns->Size && strcmp(os->Name, ns->Name) == 0)
        { memcpy(buffer + ns->Offset, cap->PersistData.Data + os->Offset, (size_t)ns->Size); break; }
      }
    }
    char* last_temp = buffer + inst->PersistBytes;
    for (int i = 0; i < inst->Temp.Size; i++)
    {
      const AppPvSlot* ns = &inst->Temp.Data[i];
      for (int j = 0; j < cap->Temp.Size; j++)
      {
        const AppPvSlot* os = &cap->Temp.Data[j];
        if (os->Type == ns->Type && os->Size == ns->Size && strcmp(os->Name, ns->Name) == 0)
        { memcpy(last_temp + ns->Offset, cap->LastTempData.Data + os->Offset, (size_t)ns->Size); break; }
      }
    }
  }
}

namespace ImGui
{
  ImGuiAppPreview* AppPreviewCreate(const ImGuiAppGraph* graph, char* err, int err_size)
  {
    IM_ASSERT(graph != nullptr);
    if (err != nullptr && err_size > 0) err[0] = 0;

    // Producers before consumers, or refuse (design 4.1 uses AppRebuildUpdateOrder's topo; this base's
    // equivalent is AppGraphTopoOrder). The interpreter pushes controls in this order, so the shipped
    // push-order-is-update-order enumeration (ForEachAppControl) runs the DAG correctly.
    ImVector<int> order;
    char toperr[160];
    if (!AppGraphTopoOrder(graph, &order, toperr, IM_ARRAYSIZE(toperr)))
    {
      if (err != nullptr && err_size > 0) ImStrncpy(err, toperr, (size_t)err_size);
      return nullptr;
    }

    ImGuiAppPreview* s = IM_NEW(ImGuiAppPreview)();
    s->Graph = graph;
    AppPvBuildPopulation(s, order);
    return s;
  }

  bool AppPreviewReconcile(ImGuiAppPreview* session, char* err, int err_size)
  {
    if (err != nullptr && err_size > 0) err[0] = 0;
    if (session == nullptr || session->Graph == nullptr) return false;

    // Refuse on a dependency cycle BEFORE tearing anything down: the running population survives an invalid
    // intermediate edit (design 7 -- a rewire applies next frame; a cycle keeps the last-good run intact).
    ImVector<int> order;
    char toperr[160];
    if (!AppGraphTopoOrder(session->Graph, &order, toperr, IM_ARRAYSIZE(toperr)))
    {
      if (err != nullptr && err_size > 0) ImStrncpy(err, toperr, (size_t)err_size);
      return false;
    }

    // Capture surviving state, rebuild the population, restore by (name, type). Scripts / dispatch log /
    // command table are session-scoped and carry across unchanged.
    ImVector<AppPvCapture*> caps;
    AppPvCaptureInstances(session, &caps);

    if (session->App != nullptr)
    {
      ShutdownApp(session->App);   // unregisters storage -> frees the old value buffers
      IM_DELETE(session->App);
      session->App = nullptr;
    }
    for (int i = 0; i < session->Instances.Size; i++)
      IM_DELETE(session->Instances.Data[i]);
    session->Instances.clear();

    AppPvBuildPopulation(session, order);

    for (int i = 0; i < session->Instances.Size; i++)
      AppPvRestoreInstance(session, session->Instances.Data[i], caps);

    for (int i = 0; i < caps.Size; i++)
      IM_DELETE(caps.Data[i]);

    return true;
  }

  void AppPreviewDestroy(ImGuiAppPreview* session)
  {
    if (session == nullptr) return;
    if (session->App != nullptr)
    {
      ShutdownApp(session->App);   // deletes controls + unregisters storage (frees the value buffers)
      IM_DELETE(session->App);
    }
    for (int i = 0; i < session->Instances.Size; i++)
      IM_DELETE(session->Instances.Data[i]);
    IM_DELETE(session);
  }

  ImGuiApp* AppPreviewApp(ImGuiAppPreview* session)
  {
    return session != nullptr ? session->App : nullptr;
  }

  void AppPreviewFrame(ImGuiAppPreview* session, float dt)
  {
    if (session == nullptr || session->App == nullptr) return;
    session->Surface.HoverNode = -1;   // recomputed this frame while the controls render (design 8.2)
    session->Tick++;
    UpdateApp(session->App, dt);
    RenderApp(session->App);
  }

#ifndef IMGUIX_DISABLE_TOOLS   // TOOL: F68 preview-surface + brushing public API (Phase A3)
  void AppPreviewRender(ImGuiAppPreview* session)
  {
    if (session == nullptr || session->App == nullptr) return;
    session->Surface.HoverNode = -1;   // paused: render (and brush) the frozen state without a Task pass
    RenderApp(session->App);
  }

  void AppPreviewSetSurface(ImGuiAppPreview* session, bool on)
  {
    if (session != nullptr) session->Surface.Enabled = on;
  }

  void AppPreviewSetBrush(ImGuiAppPreview* session, int selected_node_id, int hover_node_id)
  {
    if (session == nullptr) return;
    session->Surface.BrushSelected = selected_node_id;
    session->Surface.BrushHover = hover_node_id;
  }

  int AppPreviewHoveredNode(const ImGuiAppPreview* session)
  {
    return session != nullptr ? session->Surface.HoverNode : -1;
  }

  int AppPreviewTakeClickedNode(ImGuiAppPreview* session)
  {
    if (session == nullptr) return -1;
    const int n = session->Surface.ClickNode;
    session->Surface.ClickNode = -1;   // consumed
    return n;
  }
#endif // IMGUIX_DISABLE_TOOLS

  bool AppPreviewSetInput(ImGuiAppPreview* session, int node_id, const char* temp_field, double value)
  {
    if (session == nullptr || temp_field == nullptr) return false;
    const AppPvInstance* inst = AppPvFindInstance(session, node_id);
    if (inst == nullptr) return false;
    char fid[IM_LABEL_SIZE];
    AppPvSanitize(fid, IM_ARRAYSIZE(fid), temp_field);
    if (AppPvFindSlot(inst->Temp, fid) == nullptr) return false;
    for (int i = 0; i < session->Scripts.Size; i++)
      if (session->Scripts.Data[i].NodeId == node_id && strcmp(session->Scripts.Data[i].Field, fid) == 0)
      { session->Scripts.Data[i].Value = value; return true; }
    AppPvScript sc;
    sc.NodeId = node_id;
    ImStrncpy(sc.Field, fid, IM_ARRAYSIZE(sc.Field));
    sc.Value = value;
    session->Scripts.push_back(sc);
    return true;
  }

  bool AppPreviewGetPersist(ImGuiAppPreview* session, int node_id, const char* persist_field, double* out_value)
  {
    if (session == nullptr || persist_field == nullptr) return false;
    const AppPvInstance* inst = AppPvFindInstance(session, node_id);
    if (inst == nullptr) return false;
    char fid[IM_LABEL_SIZE];
    AppPvSanitize(fid, IM_ARRAYSIZE(fid), persist_field);
    const AppPvSlot* slot = AppPvFindSlot(inst->Persist, fid);
    if (slot == nullptr) return false;
    char* buffer = (char*)session->App->Data.GetVoidPtr(inst->DataTypeId);
    if (buffer == nullptr) return false;
    if (out_value != nullptr) *out_value = AppPvReadSlot(buffer, slot);
    return true;
  }

  int AppPreviewDispatchCount(const ImGuiAppPreview* session)
  {
    return session != nullptr ? session->Dispatches.Size : 0;
  }

  int AppPreviewDispatchCommandAt(const ImGuiAppPreview* session, int index)
  {
    if (session == nullptr || index < 0 || index >= session->Dispatches.Size) return -1;
    return session->Dispatches.Data[index].Command;
  }

  const char* AppPreviewCommandName(const ImGuiAppPreview* session, int command_value)
  {
    if (session == nullptr) return "";
    for (int i = 0; i < session->Commands.Size; i++)
      if (session->Commands.Data[i].Value == command_value) return session->Commands.Data[i].Name;
    return "";
  }
}
