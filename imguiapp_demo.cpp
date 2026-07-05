// ImGuiAppLayer demo.
//
// Index of this file (search for "[SECTION]"):
// [SECTION] Sample controls (RandomTime, Breathing) -- the framework idioms, showcased
// [SECTION] Composer document (GraphDocData) + workspace layout persistence
// [SECTION] Composer toolbar (flow-ordered: compose -> iterate -> persist -> produce | observe)
// [SECTION] Composer status strip (keymap hints + document counts)
// [SECTION] Generated-code view (source-mapped, shared by every code surface)
// [SECTION] Composer editor body (outliner | canvas + bottom console | inspector; project inspector)
// [SECTION] Composer host window + demo menu
// [SECTION] Demo bring-up (ShowAppLayerDemo: sample app + editor app composition)

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imguiapp.h"
#include "imguiapp_nodes.h"
#include "imguiapp_canvas.h"
#include "imgui_internal.h"
#include "IconsFontAwesome6.h"

#include <ctime>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>                          // std::filesystem path -> utf8 at the ImGui boundary

namespace
{

  // Theme-derived colors (same vocabulary as CanvasStyleFromTheme, imguiapp_canvas.cpp):
  // neutrals blend WindowBg toward Text; semantic hues pull toward Text for light-theme legibility.
  static ImU32 DemoThemeMix(ImVec4 a, ImVec4 b, float t, float alpha)
  {
    ImVec4 c = ImLerp(a, b, t);
    c.w = alpha;
    return ImGui::ColorConvertFloat4ToU32(c);
  }

  static ImU32 DemoThemeCol(ImVec4 c, float alpha)
  {
    c.w = alpha;
    return ImGui::ColorConvertFloat4ToU32(c);
  }

  static const ImVec4 kDemoGold   = ImVec4(0.88f, 0.72f, 0.40f, 1.0f);   // selection / focus accent
  static const ImVec4 kDemoRed    = ImVec4(0.90f, 0.45f, 0.42f, 1.0f);   // errors
  static const ImVec4 kDemoYellow = ImVec4(0.90f, 0.75f, 0.35f, 1.0f);   // warnings
  static const ImVec4 kDemoGreen  = ImVec4(0.50f, 0.75f, 0.50f, 1.0f);   // healthy

  static void RenderTextT(const char* text, ImVec2 text_size, ImVec2 pos, ImVec2 avail, float t_value)
  {
    // Draws into the window draw list; must not move the layout cursor (SetCursorScreenPos past
    // the content max trips imgui's extend-boundaries assert).
    float offset = avail.x * 0.5f + (t_value * 0.5f * (avail.x - text_size.x));
    ImVec2 text_pos = pos + ImVec2(offset, 0.0f) + (text_size * -0.5f);
    ImGui::GetWindowDrawList()->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), text);
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Sample controls (RandomTime, Breathing) -- the framework idioms, showcased
  //-----------------------------------------------------------------------------

  struct RandomTimeData
  {
    char  label[128];
    char  type[128];
    float max_timer_secs;
    ImU64 rng; // seeded once in OnInitialize, stepped only by OnUpdate: snapshots capture it,
                               // record/replay reproduces every roll (see ImAppRandom)
  };

  struct RandomTimeTempData
  {
    bool generate;
  };

  // The one host for the sample controls: every control is composed into a window or sidebar.
  struct SampleWindow : ImGuiAppWindow<SampleWindow>
  {
  };

  struct RandomTimeControlDemo : ImGuiAppControl <RandomTimeData, RandomTimeTempData>
  {
    static float GenerateTime(ImU64* rng) { return (float)ImAppRandomInt(rng, 1, 30); }

    virtual void OnInitialize(ImGuiApp*, RandomTimeData* data) const override final
    {
      std::string_view sv;

      sv = ImGuiType<decltype(this)>::Name;

      ImStrncpy(data->type, sv.data(), sv.length() + 1); // +1: ImStrncpy copies count-1 chars
      ImFormatString(data->label, sizeof(data->label), "%s", data->type);

      data->rng = (ImU64)time(nullptr);
      data->max_timer_secs = GenerateTime(&data->rng);
    }

    virtual void OnUpdate(float dt, RandomTimeData* data, const RandomTimeTempData* temp_data, const RandomTimeTempData* last_temp_data) const override final
    {
      IM_UNUSED(dt);
      IM_UNUSED(last_temp_data);

      if (temp_data->generate)
        data->max_timer_secs = GenerateTime(&data->rng);
    }

    virtual void OnRender(const RandomTimeData* data, RandomTimeTempData* temp_data) const override final
    {
      // Renders between the host window's Begin/End.
      ImGui::Text("%s", "Max Timer Seconds");

      temp_data->generate = ImGui::Button("Generate");
      ImGui::SameLine();

      ImGui::Text("%.1f", data->max_timer_secs);
    }
  };

  struct BreathingControlData
  {
    char   label[128];
    char   type[128];
    char   text[128];
    char   timer_text[128];
    float  timer_secs;
    float  t_value;
    float  t_direction;
    ImVec4 col;
  };

  struct BreathingControlTempData
  {
    bool hovered;
  };

  // RandomTimeData is an Optional dependency: null while the Random Time example is disabled,
  // rebound live when it is pushed/popped (the push site passes the Optional binding).
  struct BreathingControlDemo : ImGuiAppControl<BreathingControlData, BreathingControlTempData, RandomTimeData>
  {
    static constexpr float DefaultMaxTimerSecs = 5.0f;

    static float SourceMaxTimerSecs(const RandomTimeData* src)
    {
      if (src != nullptr)
        return src->max_timer_secs;
      return DefaultMaxTimerSecs;
    }

    virtual void OnInitialize(ImGuiApp* app, BreathingControlData* data, const RandomTimeData* src) const override final
    {
      IM_UNUSED(app);
      IM_UNUSED(src);

      std::string_view sv;

      sv = ImGuiType<decltype(this)>::Name;

      ImStrncpy(data->type, sv.data(), sv.length() + 1); // +1: ImStrncpy copies count-1 chars
      ImFormatString(data->label, sizeof(data->label), "%s", data->type);
    }

    virtual void OnUpdate(float dt, BreathingControlData* data, const BreathingControlTempData* temp_data, const BreathingControlTempData* last_temp_data, const RandomTimeData* src) const override final
    {
      data->timer_secs = ImMax(0.0f, data->timer_secs - dt);

      if (temp_data->hovered ^ last_temp_data->hovered)
      {
        data->timer_secs = temp_data->hovered * SourceMaxTimerSecs(src);
        data->t_value = 0.0f;
        data->t_direction = 1.0f;
      }

      if (0.0f < data->timer_secs)
      {
        ImFormatString(data->timer_text, sizeof(data->timer_text), "%.1f Seconds Left!", data->timer_secs);
      }
      else
      {
        ImStrncpy(data->timer_text, "Timer Expired!", sizeof(data->timer_text));
      }

      if (temp_data->hovered)
      {
        data->t_value = ImLinearSweep(data->t_value, data->t_direction, dt);
        data->t_direction = (data->t_value == data->t_direction) ? -data->t_direction : data->t_direction;
        data->col = ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_WindowBg), 0.0f <= data->t_value ? data->t_value : -data->t_value);
      }
    }

    virtual void OnRender(const BreathingControlData* data, BreathingControlTempData* temp_data, const RandomTimeData* src) const override final
    {
      IM_UNUSED(src);

      // Renders between the host window's Begin/End.
      ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, 2.0f * ImGui::GetFrameHeight());

      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, data->col);
      ImGui::Button("Hover Me!", size);
      temp_data->hovered = ImGui::IsItemHovered();
      ImGui::PopStyleColor();

      ImVec2 pos = ImGui::GetCursorScreenPos();
      size = ImGui::GetContentRegionAvail();
      ImRect r = { pos, pos + size };

      ImGui::PushClipRect(r.Min, r.Max, true);
      ImGui::NewLine();
      RenderTextT(data->timer_text, ImGui::CalcTextSize(data->timer_text), ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail(), data->t_value);
      ImGui::PopClipRect();
    }
  };

  struct BaseWindow : ImGuiAppWindow<BaseWindow>
  {
    virtual void OnRender(const ImGuiApp*) const override final
    {
      ImGui::TextWrapped("%s", "This is a base window managed by ImGuiAppDisplayLayer.");
    }
  };

  // Window-hosted control: renders inside its host window (no Begin/End).
  struct BaseInfoData
  {
    int Frames;
  };
  struct BaseInfoTempData {};
  struct BaseInfoControl : ImGuiAppControl<BaseInfoData, BaseInfoTempData>
  {
    virtual void OnInitialize(ImGuiApp*, BaseInfoData* data) const override final
    {
      data->Frames = 0;
    }

    virtual void OnUpdate(float dt, BaseInfoData* data, const BaseInfoTempData* temp_data, const BaseInfoTempData* last_temp_data) const override final
    {
      IM_UNUSED(dt);
      IM_UNUSED(temp_data);
      IM_UNUSED(last_temp_data);
      data->Frames++;
    }

    virtual void OnRender(const BaseInfoData* data, BaseInfoTempData* temp_data) const override final
    {
      IM_UNUSED(temp_data);
      ImGui::Separator();
      ImGui::Text("Hosted control: %d frames alive", data->Frames);
    }
  };

  struct StatusBar : ImGuiAppSidebar<StatusBar>
  {
    virtual void OnRender(const ImGuiApp*) const override final
    {
      ImGui::TextWrapped("%s", "This is a status bar managed by ImGuiAppSidebar.");
    }
  };

  void SeedAppGraph(ImGuiAppGraph* graph)
  {
    // BuildAppLiveGraph upserts onto the foundation layers, so design/live phases never duplicate.
    ImGui::AppGraphEnsureFoundation(graph);
    // Starter prefab library on start (no-op once a saved registry loaded from the sidecar).
    ImGui::AppGraphSeedStarterPrefabs(graph);
  }

  void ShowNodePalette(ImGuiAppGraph* graph)
  {
    if (ImGui::BeginMenu("Control"))
    {
      if (ImGui::MenuItem("New control (draft)"))
        ImGui::AppGraphAddNode(graph, ImGuiAppNodeKind_Control, "NewControl");
      ImGui::Separator();
      if (ImGui::MenuItem("Random Time (builtin)"))
        ImGui::AppGraphAddBuiltin(graph, ImGuiAppNodeKind_Control, "RandomTime", "RandomTimeData");
      if (ImGui::MenuItem("Breathing (builtin)"))
        ImGui::AppGraphAddBuiltin(graph, ImGuiAppNodeKind_Control, "Breathing", "BreathingData");
      ImGui::EndMenu();
    }
    // Core phases come from the foundation; the only authorable layer is Custom.
    if (ImGui::MenuItem("Custom Layer"))
    {
      ImGuiAppNode* n = ImGui::AppGraphAddNode(graph, ImGuiAppNodeKind_Layer, "CustomLayer");
      n->LayerType = ImGuiAppLayerType_Custom;
    }
    if (ImGui::MenuItem("Struct"))  ImGui::AppGraphAddNode(graph, ImGuiAppNodeKind_Struct,  "NewStruct");
    if (ImGui::MenuItem("Window"))  ImGui::AppGraphAddNode(graph, ImGuiAppNodeKind_Window,  "Window");
    if (ImGui::MenuItem("Sidebar")) ImGui::AppGraphAddNode(graph, ImGuiAppNodeKind_Sidebar, "Sidebar");
  }

  // Dogfooded Composer node editor: GraphDocControl owns the graph as PersistData and is the single
  // writer; panels read it as a typed dependency and mutate through a cached pointer (deliberate escape
  // from the read-only dependency rule until an edit-intent bus lands).
  //-----------------------------------------------------------------------------
  // [SECTION] Composer document (GraphDocData) + workspace layout persistence
  //-----------------------------------------------------------------------------

  // Bottom-bar panel identities for the one-shot reveal intent.
  enum ComposerPanel_
  {
    ComposerPanel_None = 0,
    ComposerPanel_Code,
    ComposerPanel_Project,
    ComposerPanel_Preview,
    ComposerPanel_Output,
  };

  // Document verbs registered into the canvas command palette; the toolbar consumes the pick.
  enum ComposerHostCmd_
  {
    ComposerHostCmd_Save = 1,
    ComposerHostCmd_Load,
    ComposerHostCmd_Generate,
    ComposerHostCmd_CopyCode,
    ComposerHostCmd_Diff,
    ComposerHostCmd_PanelCode,
    ComposerHostCmd_PanelProject,
    ComposerHostCmd_PanelPreview,
    ComposerHostCmd_PanelOutput,
    ComposerHostCmd_ToggleLive,
  };

  // App-time transport (F29): a per-frame snapshot ring of the mirror's snapshottable (trivially-copyable)
  // controls plus the scrub position. Lives OFF GraphDocData's snapshotted bytes -- the doc itself is
  // opaque (non-POD), so AppStateSnapshot skips it and never captures this history. Heap, process lifetime.
  struct ComposerTransport
  {
    ImGuiAppStateHistory History;
    bool                 Frozen = false;   // engaged: hold + scrub instead of record
    int                  Frame  = 0;       // scrub position (0..Count-1)
  };

  struct GraphDocData
  {
    ImGuiAppGraph Graph;
    int           Selection;    // node selection shared by tree + canvas
    bool          ShowLive;     // show vs hide (never delete) live-mirror nodes
    float         TreeW;        // outliner width (0 -> default on first use)
    float         CodeH;        // bottom panel height (code/preview/problems/project; 0 == collapsed)
    float         InspW;        // right-side Inspector column width (0 -> default on first use)
    char          WriteMsg[64]; // transient "wrote header" confirmation

    // Frame-published values: derived once per frame in GraphDocControl::OnUpdate, read
    // everywhere; never re-derived in a render path.
    ImGuiID FrameSig;  // this frame's AppGraphSignature
    int     NumErrors; // cached-validation counts
    int     NumWarnings;
    int     CodegenWarnCount;   // "// WARNING"/"// codegen aborted" markers in the last-generated C++ (F19); refreshed on signature change
    ImGuiID CodegenWarnSig;     // signature the codegen-warning count was scanned at (gate: rescan only on change)
    char    GraphPath[256];
    char    HeaderPath[256];
    struct DocLogLine { int Severity; char Text[184]; };
    ImVector<DocLogLine> Log;             // newest last; capped in DocLog()
    int                  RevealPanel;     // one-shot ComposerPanel_*: reveal + select that bottom tab (0 = none)
    int                  LinkErrSeqSeen;  // last Graph.LastLinkErrSeq folded into the log
    ImGuiID              LayoutSavedHash; // hash of the last-persisted layout fields (change detection)
    float                LayoutSaveT;     // debounce: seconds until the next layout-save check
    ImGuiApp*            Mirror;          // THE running app: the one hosting this control (set in OnInitialize)
    ComposerTransport*   Transport;       // App-time scrubber state (heap; opaque, never snapshotted) -- F29
    int                  NumUnbuilt;      // per-frame count: authored nodes with no live counterpart in the running
                                      // binary -- nonzero == stale until Generate + recompile + relaunch
  };
  struct GraphDocTempData {};

  // PersistData aliases the start of InstanceData.
  static GraphDocData* GetGraphDoc(ImGuiApp* app)
  {
    return static_cast<GraphDocData*>(app->Data.GetVoidPtr(ImGuiType<GraphDocData>::ID));
  }

  //-----------------------------------------------------------------------------
  // Workspace layout persistence. A sidecar file, not an imgui settings handler: the Composer
  // initializes after imgui loads its ini, and handlers registered late never see their sections.
  //-----------------------------------------------------------------------------

  static const char* kComposerLayoutPath = "imguix_composer_layout.ini";

  struct ComposerLayoutFields
  {
    float TreeW, InspW, CodeH;
    bool                          ShowLive;
    ImGuiAppGraphViewState View;
  };
  static ComposerLayoutFields ComposerLayoutCapture(const GraphDocData* doc)
  {
    ComposerLayoutFields f;
    memset(&f, 0, sizeof(f));   // padding participates in the hash -- keep it deterministic
    f.TreeW = doc->TreeW; f.InspW = doc->InspW; f.CodeH = doc->CodeH;
    f.ShowLive = doc->ShowLive;
    f.View = ImGui::AppGraphEditorState(&doc->Graph)->View;
    return f;
  }

  static void ComposerLayoutLoad(GraphDocData* doc)
  {
    size_t size = 0;
    char* text = (char*)ImFileLoadToMemory(kComposerLayoutPath, "rb", &size, 1);
    if (text == nullptr)
      return;
    ImGuiAppGraphViewState* view = &ImGui::AppGraphEditorState(&doc->Graph)->View;
    for (char* p = text; *p; )
    {
      char* eol = p;
      while (*eol != 0 && *eol != '\n') eol++;
      const char saved = *eol;
      *eol = 0;
      float fv = 0.0f; int iv = 0;
      if      (sscanf(p, "TreeW=%f", &fv) == 1)     doc->TreeW = fv;
      else if (sscanf(p, "InspW=%f", &fv) == 1)     doc->InspW = fv;
      else if (sscanf(p, "CodeH=%f", &fv) == 1)     doc->CodeH = fv;
      else if (sscanf(p, "ShowLive=%d", &iv) == 1)  doc->ShowLive = iv != 0;
      else if (sscanf(p, "TreeOpen=%d", &iv) == 1)  view->TreeOpen = iv != 0;
      else if (sscanf(p, "InspOpen=%d", &iv) == 1)  view->InspOpen = iv != 0;
      else if (sscanf(p, "Snap=%d", &iv) == 1)      view->SnapGrid = iv != 0;
      else if (sscanf(p, "OvGrid=%d", &iv) == 1)    view->OvGrid = iv != 0;
      else if (sscanf(p, "OvBands=%d", &iv) == 1)   view->OvBands = iv != 0;
      else if (sscanf(p, "OvFrames=%d", &iv) == 1)  view->OvFrames = iv != 0;
      else if (sscanf(p, "OvMinimap=%d", &iv) == 1) view->OvMinimap = iv != 0;
      else if (sscanf(p, "Zoom=%f", &fv) == 1)      view->Zoom = (fv > 0.0f) ? fv : 1.0f;
      if (saved == 0) break;
      p = eol + 1;
    }
    IM_FREE(text);
    const ComposerLayoutFields f = ComposerLayoutCapture(doc);
    doc->LayoutSavedHash = ImHashData(&f, sizeof(f));
  }

  // At most one disk check per second, skipped mid-gesture so a drag lands once.
  static void ComposerLayoutSaveIfChanged(GraphDocData* doc, float dt)
  {
    doc->LayoutSaveT -= dt;
    if (doc->LayoutSaveT > 0.0f || ImGui::IsMouseDown(ImGuiMouseButton_Left))
      return;
    doc->LayoutSaveT = 1.0f;
    const ComposerLayoutFields f = ComposerLayoutCapture(doc);
    const ImGuiID h = ImHashData(&f, sizeof(f));
    if (h == doc->LayoutSavedHash)
      return;
    if (ImFileHandle fh = ImFileOpen(kComposerLayoutPath, "wt"))
    {
      ImGuiTextBuffer buf;
      buf.appendf("TreeW=%g\nInspW=%g\nCodeH=%g\nShowLive=%d\n", f.TreeW, f.InspW, f.CodeH, f.ShowLive ? 1 : 0);
      buf.appendf("TreeOpen=%d\nInspOpen=%d\n", f.View.TreeOpen ? 1 : 0, f.View.InspOpen ? 1 : 0);
      buf.appendf("Snap=%d\nOvGrid=%d\nOvBands=%d\nOvFrames=%d\nOvMinimap=%d\nZoom=%g\n",
                  f.View.SnapGrid ? 1 : 0, f.View.OvGrid ? 1 : 0, f.View.OvBands ? 1 : 0, f.View.OvFrames ? 1 : 0, f.View.OvMinimap ? 1 : 0, f.View.Zoom);
      ImFileWrite(buf.c_str(), sizeof(char), (ImU64)buf.size(), fh);
      ImFileClose(fh);
      doc->LayoutSavedHash = h;
    }
  }

  // Severity: 0 info, 1 warning, 2 error. OnUpdate paths only -- the log is document state.
  static void DocLog(GraphDocData* doc, int severity, const char* fmt, ...)
  {
    GraphDocData::DocLogLine line;
    line.Severity = severity;
    va_list args;
    va_start(args, fmt);
    ImFormatStringV(line.Text, IM_ARRAYSIZE(line.Text), fmt, args);
    va_end(args);
    doc->Log.push_back(line);
    if (doc->Log.Size > 256)
      doc->Log.erase(doc->Log.Data, doc->Log.Data + 64);
  }

  // Generate the whole-graph C++ to the header path + stamp the fresh baseline. Shared by the toolbar
  // Generate button and the status-bar freshness zone (F32) so both take the identical road.
  static void ComposerGenerateHeader(GraphDocData* doc)
  {
    ImGuiTextBuffer full;
    ImGui::GenerateAppGraphCode(&doc->Graph, &full);
    if (ImFileHandle fh = ImFileOpen(doc->HeaderPath, "wt"))
    {
      ImFileWrite(full.c_str(), sizeof(char), (ImU64)full.size(), fh);
      ImFileClose(fh);
      ImFormatString(doc->WriteMsg, IM_ARRAYSIZE(doc->WriteMsg), "wrote %s", doc->HeaderPath);
      ImGui::AppGraphMarkGenerated(&doc->Graph);
      DocLog(doc, 0, "generated C++ -> %s", doc->HeaderPath);
    }
    else
      DocLog(doc, 2, "could not open %s for writing", doc->HeaderPath);
  }

  struct GraphDocControl : ImGuiAppControl<GraphDocData, GraphDocTempData>
  {
    virtual void OnInitialize(ImGuiApp* app, GraphDocData* data) const override final
    {
      data->Selection   = -1;
      data->ShowLive    = false;   // live mirror is opt-in (the toolbar eye)
      data->TreeW       = 0.0f;          // 0 -> EditorBody picks a default on first layout
      data->CodeH       = 0.0f;          // collapsed
      data->InspW       = 0.0f;          // 0 -> EditorBody picks a default on first layout
      data->WriteMsg[0] = 0;
      data->FrameSig    = 0;
      data->NumErrors   = 0;
      data->NumWarnings = 0;
      data->CodegenWarnCount = 0;
      data->CodegenWarnSig   = 0;
      data->RevealPanel = ComposerPanel_None;
      data->LinkErrSeqSeen = 0;
      data->LayoutSavedHash = 0;
      data->LayoutSaveT = 0.0f;
      data->Mirror      = app;     // ONE application: the mirror IS the app hosting this control
      data->Transport   = IM_NEW(ComposerTransport)();   // App-time scrubber (F29); heap, process lifetime
      data->NumUnbuilt  = 0;
      ImStrncpy(data->GraphPath,  "imguix_node_graph.txt",      sizeof(data->GraphPath));
      ImStrncpy(data->HeaderPath, "imguix_generated_control.h", sizeof(data->HeaderPath));
      ComposerLayoutLoad(data);
      if (data->Graph.Nodes.empty())
      {
        SeedAppGraph(&data->Graph);
        ImGui::AppGraphAutoLayout(&data->Graph, false);
      }
      ImGui::AppGraphRequestFitAll(&data->Graph);
    }
    virtual void OnUpdate(float dt, GraphDocData* data, const GraphDocTempData*, const GraphDocTempData*) const override final
    {
      ComposerLayoutSaveIfChanged(data, dt);

      // Self-mirroring from inside our own update is safe: BuildAppLiveGraph only reads the object model.
      // Build the live mirror first so every panel reads the reconciled graph this frame.
      if (data->Mirror != nullptr)
      {
        ImGui::BuildAppLiveGraph(data->Mirror, &data->Graph);
      }

      // App-time transport (F29). Gated on ShowLive + a mirror. While running, snapshot the mirror's
      // snapshottable (POD) controls each frame and track the newest frame; while frozen, hold + restore
      // the scrubbed frame's bytes back into the app (time travel). Opaque controls (this chrome) are
      // skipped by AppStateSnapshot, so only the user app's state rewinds.
      if (data->Mirror != nullptr && data->Transport != nullptr && data->ShowLive)
      {
        ComposerTransport* tr = data->Transport;
        if (!tr->Frozen)
        {
          ImGui::AppStateSnapshot(data->Mirror, &tr->History);
          tr->Frame = tr->History.Count - 1;   // live: follow the newest frame
        }
        else if (tr->History.Count > 0)
        {
          tr->Frame = ImClamp(tr->Frame, 0, tr->History.Count - 1);
          ImGui::AppStateRestore(data->Mirror, &tr->History, tr->Frame);
        }
      }

      // Publish the frame's derived values once; GraphDoc updates first (push order), so all consumers see the same values.
      ImGui::AppGraphSyncRevision(&data->Graph);   // one signature fold per frame -> Revision pulse + _SigCache
      data->FrameSig = data->Graph._SigCache;
      if (data->WriteMsg[0] && ImGui::AppGraphCodeStale(&data->Graph))
        data->WriteMsg[0] = 0;   // the "wrote/copied" confirmation stops being true the frame the graph diverges from disk
      {
        const ImVector<ImGui::ImGuiAppGraphIssue>* issues = ImGui::AppGraphIssuesCached(&data->Graph);
        data->NumErrors = 0;
        data->NumWarnings = 0;
        for (int i = 0; i < issues->Size; i++)
          (issues->Data[i].Severity >= 2 ? data->NumErrors : data->NumWarnings)++;
      }

      // Codegen self-diagnostics (F19): count the "// WARNING"/"// codegen aborted" markers the emitter
      // embeds in the generated C++. Scan the emitted text (single source), gated on the signature so a
      // static graph costs nothing. The listing popup regenerates on open.
      if (data->CodegenWarnSig != data->FrameSig)
      {
        data->CodegenWarnSig = data->FrameSig;
        ImGuiTextBuffer code;
        ImGui::GenerateAppGraphCode(&data->Graph, &code);
        data->CodegenWarnCount = ImGui::AppScanCodegenWarnings(code.c_str(), nullptr);
      }

      // Fold editor notices (refused links, composition refusals) into the document log; the
      // channel carries full sentences.
      if (data->Graph.LastLinkErrSeq != data->LinkErrSeqSeen)
      {
        data->LinkErrSeqSeen = data->Graph.LastLinkErrSeq;
        if (data->Graph.LastLinkErr[0])
          DocLog(data, 1, "%s", data->Graph.LastLinkErr);
      }

      // Bootstrap staleness: an authored node with no live counterpart exists only in the design,
      // not in the running binary -- the document is stale until Generate + recompile + relaunch.
      // Core layers are the foundation representing the live stack; Struct/Field nodes are data
      // shapes carried by their control.
      int unbuilt = 0;
      for (int i = 0; i < data->Graph.Nodes.Size; i++)
      {
        const ImGuiAppNode* n = &data->Graph.Nodes.Data[i];
        if (n->IsLive)
          continue;
        if (n->Kind == ImGuiAppNodeKind_Control)
        {
          if (!n->IsPromoted)
            unbuilt++;
          continue;
        }
        const bool needs_twin = n->Kind == ImGuiAppNodeKind_Window || n->Kind == ImGuiAppNodeKind_Sidebar
                             || (n->Kind == ImGuiAppNodeKind_Layer && n->LayerType == ImGuiAppLayerType_Custom);
        if (!needs_twin)
          continue;
        bool built = false;
        for (int j = 0; j < data->Graph.Nodes.Size && !built; j++)
        {
          const ImGuiAppNode* m = &data->Graph.Nodes.Data[j];
          built = m->IsLive && m->Kind == n->Kind && strcmp(m->Draft.Name, n->Draft.Name) == 0;
        }
        if (!built)
          unbuilt++;
      }
      data->NumUnbuilt = unbuilt;
    }
  };

  static void EditorToolSep(float em)
  {
    ImGui::SameLine(0.0f, em * 0.6f);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0.0f, em * 0.6f);
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Composer toolbar (flow-ordered: compose -> iterate -> persist -> produce | observe)
  //-----------------------------------------------------------------------------

  // Declared GraphDocData dependency orders updates producer-first; the Doc pointer is the write half only.
  struct ToolbarData
  {
    GraphDocData* Doc;
  };
  // Actions captured in OnRender, applied in OnUpdate -- TempData is the edit-intent bus.
  struct ToolbarTempData
  {
    bool Save;
    bool Load;
    bool WriteHeader;
    bool ToggleCode;
    bool ToggleLive;     // Live-eye toggle clicked this frame (OnUpdate derives the new state)
    bool ToggleTree;     // outliner sidebar toggle clicked this frame
    bool ToggleInsp;     // Inspector sidebar toggle clicked this frame
    bool Undo;           // undo / redo edit-intents (applied in OnUpdate)
    bool Redo;
    bool Diff;           // diff current graph's codegen vs the saved-on-disk graph -> clipboard
    bool HistoryGotoSet; // history dropdown picked a step
    int  HistoryGotoIdx;
    bool CopyCode;       // Generate menu: copy the generated C++ to the clipboard
    int  RevealPanel;    // ComposerPanel_* intent from a palette pick (0 = none)
    bool AddNode;        // toolbar "+ Add" -> open the canvas add palette (the loop's entry point)
  };
  struct ToolbarControl : ImGuiAppControl<ToolbarData, ToolbarTempData, GraphDocData>
  {
    virtual void OnInitialize(ImGuiApp* app, ToolbarData* data, const GraphDocData*) const override final
    {
      data->Doc = GetGraphDoc(app);   // write half only; reads flow through the declared dep
    }

    virtual void OnUpdate(float dt, ToolbarData* data, const ToolbarTempData* temp_data, const ToolbarTempData* last_temp_data, const GraphDocData*) const override final
    {
      IM_UNUSED(dt);
      GraphDocData* doc = data->Doc;
      if (temp_data->Save)
      {
        ImGui::SaveAppGraph(doc->GraphPath, &doc->Graph);
        DocLog(doc, 0, "saved graph -> %s", doc->GraphPath);
      }
      if (temp_data->Load)
      {
        ImGui::LoadAppGraph(doc->GraphPath, &doc->Graph);
        ImGui::AppGraphEnsureFoundation(&doc->Graph);
        ImGui::AppGraphRequestFitAll(&doc->Graph);
        DocLog(doc, 0, "loaded graph <- %s", doc->GraphPath);
      }
      if (temp_data->WriteHeader)
        ComposerGenerateHeader(doc);
      if (temp_data->ToggleCode)
      {
        doc->CodeH = (doc->CodeH > 0.0f) ? 0.0f : ImGui::GetFontSize() * 12.0f;
      }
      if (temp_data->ToggleLive)
      {
        doc->ShowLive = !doc->ShowLive;
      }
      if (temp_data->ToggleTree)
      {
        ImGui::AppGraphViewState(&doc->Graph)->TreeOpen = !ImGui::AppGraphViewState(&doc->Graph)->TreeOpen;
      }
      if (temp_data->ToggleInsp)
      {
        ImGui::AppGraphViewState(&doc->Graph)->InspOpen = !ImGui::AppGraphViewState(&doc->Graph)->InspOpen;
      }
      if (temp_data->Undo)
      {
        ImGui::AppGraphUndo(&doc->Graph);
      }
      if (temp_data->Redo)
      {
        ImGui::AppGraphRedo(&doc->Graph);
      }
      if (temp_data->HistoryGotoSet)
      {
        ImGui::AppGraphHistoryGoto(&doc->Graph, temp_data->HistoryGotoIdx);
      }
      if (temp_data->CopyCode)
      {
        ImGuiTextBuffer full;
        ImGui::GenerateAppGraphCode(&doc->Graph, &full);
        ImGui::SetClipboardText(full.c_str());
        ImFormatString(doc->WriteMsg, IM_ARRAYSIZE(doc->WriteMsg), "generated C++ -> clipboard");
        DocLog(doc, 0, "copied generated C++ -> clipboard");
      }
      if (temp_data->RevealPanel != ComposerPanel_None)
      {
        doc->RevealPanel = temp_data->RevealPanel;
      }
      if (temp_data->AddNode)
      {
        ImGui::AppGraphRequestAddPalette(&doc->Graph);
      }
      if (temp_data->Diff)
      {
        ImGuiAppGraph saved;
        if (ImGui::LoadAppGraph(doc->GraphPath, &saved))
        {
          ImGuiTextBuffer d;
          ImGui::AppGraphDiffCode(&saved, &doc->Graph, &d);
          ImGui::SetClipboardText(d.c_str());
          ImFormatString(doc->WriteMsg, IM_ARRAYSIZE(doc->WriteMsg), "diff vs saved -> clipboard");
          DocLog(doc, 0, "diff vs saved graph -> clipboard");
        }
        else
        {
          ImFormatString(doc->WriteMsg, IM_ARRAYSIZE(doc->WriteMsg), "no saved graph to diff (Save first)");
          DocLog(doc, 1, "diff skipped: no saved graph (Save first)");
        }
      }
    }

    virtual void OnRender(const ToolbarData* data, ToolbarTempData* temp_data, const GraphDocData*) const override final
    {
      GraphDocData*     doc       = data->Doc;
      const float       em        = ImGui::GetFontSize();
      const ImGuiStyle& style     = ImGui::GetStyle();
      const bool        code_open = doc->CodeH > 0.0f;
      bool              show_live = doc->ShowLive;
      if (ImGui::BeginChild("##Toolbar", ImVec2(0.0f, 0.0f), ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
      {
        // Flow-ordered: compose -> iterate -> persist -> produce; panel/observe toggles right-aligned.
        float cap_x[4] = { 0, 0, 0, 0 };

        // -- compose
        cap_x[0] = ImGui::GetCursorPosX();
        temp_data->AddNode = ImGui::Button(ICON_FA_PLUS "  Add");
        ImGui::SetItemTooltip("Add a node (Space / right-click canvas)");

        EditorToolSep(em);
        // -- iterate: undo/redo carry the name of the step; render only records the pick.
        cap_x[1] = ImGui::GetCursorPosX();
        const int hist_count  = ImGui::AppGraphHistoryCount(&doc->Graph);
        const int hist_cursor = ImGui::AppGraphHistoryCursor(&doc->Graph);
        ImGui::BeginDisabled(!ImGui::AppGraphCanUndo(&doc->Graph));
        temp_data->Undo = ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT "##undo");
        ImGui::EndDisabled();
        if (hist_cursor > 0)
          ImGui::SetItemTooltip("Undo %s (Ctrl+Z)", ImGui::AppGraphHistoryLabel(&doc->Graph, hist_cursor));
        else
          ImGui::SetItemTooltip("Undo (Ctrl+Z)");
        ImGui::SameLine();
        ImGui::BeginDisabled(!ImGui::AppGraphCanRedo(&doc->Graph));
        temp_data->Redo = ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##redo");
        ImGui::EndDisabled();
        if (hist_cursor >= 0 && hist_cursor + 1 < hist_count)
          ImGui::SetItemTooltip("Redo %s (Ctrl+Y)", ImGui::AppGraphHistoryLabel(&doc->Graph, hist_cursor + 1));
        else
          ImGui::SetItemTooltip("Redo (Ctrl+Y)");
        ImGui::SameLine();
        temp_data->HistoryGotoSet = false;
        temp_data->HistoryGotoIdx = -1;
        ImGui::BeginDisabled(hist_count <= 1);
        if (ImGui::Button(ICON_FA_CLOCK_ROTATE_LEFT "##history"))
          ImGui::OpenPopup("##edit_history");
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Edit history (%d steps) -- click a step to jump", hist_count);
        if (ImGui::BeginPopup("##edit_history"))
        {
          for (int i = hist_count - 1; i >= 0; i--)   // newest first
          {
            char row[128];
            ImFormatString(row, IM_ARRAYSIZE(row), "%s %s###h%d", i == hist_cursor ? ICON_FA_CARET_RIGHT : " ",
                           ImGui::AppGraphHistoryLabel(&doc->Graph, i), i);
            if (ImGui::Selectable(row, i == hist_cursor) && i != hist_cursor)
            {
              temp_data->HistoryGotoSet = true;
              temp_data->HistoryGotoIdx = i;
            }
          }
          ImGui::EndPopup();
        }

        EditorToolSep(em);
        // -- persist. Ctrl+S captured here because the toolbar renders every frame.
        cap_x[2] = ImGui::GetCursorPosX();
        temp_data->Save = ImGui::Button(ICON_FA_FLOPPY_DISK "  Save")
                       || (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false));
        ImGui::SetItemTooltip("Save graph -> %s (Ctrl+S)", doc->GraphPath);
        ImGui::SameLine(0.0f, ImMax(1.0f, em * 0.0625f));
        if (ImGui::Button(ICON_FA_CHEVRON_DOWN "##savemenu"))
          ImGui::OpenPopup("##save_family");
        ImGui::SetItemTooltip("More file actions");
        if (ImGui::BeginPopup("##save_family"))
        {
          if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Load graph"))
            temp_data->Load = true;
          ImGui::EndPopup();
        }

        EditorToolSep(em);
        // -- produce: green = header matches graph; amber = graph changed; red = errors (writing stays allowed).
        cap_x[3] = ImGui::GetCursorPosX();
        const int   nerr  = doc->NumErrors;
        const bool  fresh = ImGui::AppGraphCodeFresh(&doc->Graph);
        // Stable ### id: the visible label swings Generate/Generated with health, but the widget keeps one
        // identity (no focus/press churn across states; test-addressable).
        const char* gen_label = nerr > 0 ? ICON_FA_TRIANGLE_EXCLAMATION "  Generate###generate" : fresh ? ICON_FA_CHECK "  Generated###generate" : ICON_FA_FILE_EXPORT "  Generate###generate";
        ImGui::PushStyleColor(ImGuiCol_Button, nerr > 0 ? ImVec4(0.55f, 0.21f, 0.18f, 1.0f)
                                             : fresh    ? ImVec4(0.16f, 0.38f, 0.22f, 1.0f)
                                                        : ImVec4(0.52f, 0.39f, 0.14f, 1.0f));
        temp_data->WriteHeader = ImGui::Button(gen_label);
        ImGui::PopStyleColor();
        if (nerr > 0)
          ImGui::SetItemTooltip("%d error(s) in the graph -- writes anyway; see Output", nerr);
        else if (fresh)
          ImGui::SetItemTooltip("%s matches the graph -- click to rewrite", doc->HeaderPath);
        else
          ImGui::SetItemTooltip("Graph changed -- write whole-graph C++ -> %s", doc->HeaderPath);
        ImGui::SameLine(0.0f, ImMax(1.0f, em * 0.0625f));
        if (ImGui::Button(ICON_FA_CHEVRON_DOWN "##genmenu"))
          ImGui::OpenPopup("##generate_family");
        ImGui::SetItemTooltip("More generate actions");
        if (ImGui::BeginPopup("##generate_family"))
        {
          if (ImGui::MenuItem(ICON_FA_COPY "  Copy generated C++ to clipboard"))
            temp_data->CopyCode = true;
          if (ImGui::MenuItem(ICON_FA_CODE_COMPARE "  Diff vs saved graph -> clipboard"))
            temp_data->Diff = true;
          ImGui::EndPopup();
        }

        // Codegen self-diagnostics chip (F19): count of "// WARNING"/"// codegen aborted" markers in the
        // emitted C++, beside Generate. Amber, click to list; absent when the emission is clean.
        if (doc->CodegenWarnCount > 0)
        {
          ImGui::SameLine(0.0f, ImMax(1.0f, em * 0.25f));
          ImGui::PushStyleColor(ImGuiCol_Text, ImLerp(kDemoGold, style.Colors[ImGuiCol_Text], 0.15f));
          char warn_lbl[48];
          ImFormatString(warn_lbl, IM_ARRAYSIZE(warn_lbl), ICON_FA_TRIANGLE_EXCLAMATION "  %d###codegenwarn", doc->CodegenWarnCount);
          const bool open_warn = ImGui::Button(warn_lbl);
          ImGui::PopStyleColor();
          ImGui::SetItemTooltip("%d codegen warning(s) embedded in the generated C++ -- click to list", doc->CodegenWarnCount);
          if (open_warn)
            ImGui::OpenPopup("##codegen_warn_list");
          if (ImGui::BeginPopup("##codegen_warn_list"))
          {
            ImGui::TextDisabled("Codegen warnings (%d)", doc->CodegenWarnCount);
            ImGui::Separator();
            ImGuiTextBuffer code;
            ImGui::GenerateAppGraphCode(&doc->Graph, &code);   // pure read; the list regenerates while the popup is open
            ImGuiTextBuffer list;
            ImGui::AppScanCodegenWarnings(code.c_str(), &list);
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
            ImGui::TextUnformatted(list.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndPopup();
          }
        }

        // F31: problems-count badge. Worst severity wins the colour (red = errors, amber = warnings only);
        // absent when the graph validates clean. Clicking opens the Output panel filtered to problems.
        {
          const int nwarn_v = doc->NumWarnings;
          const int nprob   = nerr + nwarn_v;   // nerr captured with the Generate cluster above
          if (nprob > 0)
          {
            ImGui::SameLine(0.0f, ImMax(1.0f, em * 0.25f));
            ImGui::PushStyleColor(ImGuiCol_Button, nerr > 0 ? ImVec4(0.55f, 0.21f, 0.18f, 1.0f) : ImVec4(0.52f, 0.39f, 0.14f, 1.0f));
            char prob_lbl[48];
            ImFormatString(prob_lbl, IM_ARRAYSIZE(prob_lbl), ICON_FA_TRIANGLE_EXCLAMATION "  %d###problems", nprob);
            if (ImGui::Button(prob_lbl))
            {
              temp_data->RevealPanel = ComposerPanel_Output;
              ImGuiAppEditorState* ed = ImGui::AppGraphEditorState(&doc->Graph);
              ed->OutputShowErr  = true;
              ed->OutputShowWarn = true;
              ed->OutputShowInfo = false;   // filter to problems: hide the info/log stream
            }
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("%d problem(s): %d error(s), %d warning(s) -- click to open Output", nprob, nerr, nwarn_v);
          }
        }

        // App-time transport (F29): freeze the running app + scrub its state history. Flow-placed (left of
        // the right-aligned observe cluster) so it stays on the toolbar; only offered with the live mirror.
        if (show_live && doc->Transport != nullptr)
        {
          ComposerTransport* tr = doc->Transport;
          const int frames = tr->History.Count;
          const bool was_frozen = tr->Frozen;   // capture: the button click below flips Frozen mid-Push/Pop
          EditorToolSep(em);
          if (was_frozen)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.52f, 0.39f, 0.14f, 1.0f));   // amber: app time engaged
          if (ImGui::Button(was_frozen ? ICON_FA_PLAY "###apptime" : ICON_FA_PAUSE "###apptime"))
            tr->Frozen = !tr->Frozen;
          if (was_frozen)
            ImGui::PopStyleColor();
          ImGui::SetItemTooltip(tr->Frozen ? "Resume the running app (App time)" : "Freeze the app to scrub its state history (App time)");
          if (tr->Frozen)
          {
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_BACKWARD_STEP "###apptimeback"))
              tr->Frame = ImMax(0, tr->Frame - 1);
            ImGui::SetItemTooltip("Step back one frame");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(em * 8.0f);
            int f = frames > 0 ? tr->Frame : 0;
            if (ImGui::SliderInt("###apptimescrub", &f, 0, frames > 0 ? frames - 1 : 0, "f %d"))
              tr->Frame = f;
            ImGui::SetItemTooltip("Scrub App-time frame (0 = oldest, %d = newest)", frames > 0 ? frames - 1 : 0);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_FORWARD_STEP "###apptimefwd"))
              tr->Frame = frames > 0 ? ImMin(frames - 1, tr->Frame + 1) : 0;
            ImGui::SetItemTooltip("Step forward one frame");
          }
        }

        // -- observe (right-aligned): bootstrap-state readout + panel toggles. The Live eye
        // always reflects THE running app -- there is exactly one.
        const bool  unwritten = doc->Graph.GenSignature != 0 && ImGui::AppGraphCodeStale(&doc->Graph);
        const bool  stale     = doc->NumUnbuilt > 0 || unwritten;
        char        sync_lbl[64];
        if (stale)
          ImFormatString(sync_lbl, IM_ARRAYSIZE(sync_lbl), ICON_FA_TRIANGLE_EXCLAMATION "  Stale %d###sync", doc->NumUnbuilt > 0 ? doc->NumUnbuilt : 1);
        else
          ImFormatString(sync_lbl, IM_ARRAYSIZE(sync_lbl), "%s", ICON_FA_CIRCLE_CHECK "  Built###sync");
        const char* code_lbl = ICON_FA_CODE "  Code";
        const char* live_lbl = show_live ? ICON_FA_EYE "  Live###live" : ICON_FA_EYE_SLASH "  Live###live";
        const char* tree_lbl = ICON_FA_LAYER_GROUP "###treetoggle";
        const char* insp_lbl = ICON_FA_CIRCLE_INFO "###insptoggle";
        const float pad2 = style.FramePadding.x * 2.0f;
        const float cluster_w = ImGui::CalcTextSize(code_lbl).x + pad2
                              + ImGui::CalcTextSize(live_lbl, ImGui::FindRenderedTextEnd(live_lbl)).x + pad2 + style.ItemSpacing.x
                              + ImGui::CalcTextSize(sync_lbl, ImGui::FindRenderedTextEnd(sync_lbl)).x + pad2 + style.ItemSpacing.x
                              + ImGui::CalcTextSize(tree_lbl, ImGui::FindRenderedTextEnd(tree_lbl)).x + pad2 + style.ItemSpacing.x
                              + ImGui::CalcTextSize(insp_lbl, ImGui::FindRenderedTextEnd(insp_lbl)).x + pad2 + style.ItemSpacing.x;
        ImGui::SameLine(ImMax(ImGui::GetCursorPosX() + em, ImGui::GetContentRegionMax().x - cluster_w - em * 0.2f));

        // Amber ink when authored work is not compiled into the running app; quiet when in sync.
        const ImVec4 sync_ink = stale ? ImLerp(kDemoGold, style.Colors[ImGuiCol_Text], 0.25f) : style.Colors[ImGuiCol_TextDisabled];
        ImGui::PushStyleColor(ImGuiCol_Text, sync_ink);
        if (ImGui::Button(sync_lbl))
          temp_data->RevealPanel = ComposerPanel_Code;
        ImGui::PopStyleColor();
        if (stale)
          ImGui::SetItemTooltip("Authored changes are NOT in the running app yet.\n"
                                "Bootstrap: Generate (write the header), recompile, relaunch.\n"
                                "Live nodes always show what the running binary actually is.");
        else
          ImGui::SetItemTooltip("Everything authored is compiled into the running app.");
        ImGui::SameLine();

        const ImGuiAppGraphViewState* view = ImGui::AppGraphViewState(&doc->Graph);
        if (view->TreeOpen)
          ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
        temp_data->ToggleTree = ImGui::Button(tree_lbl);
        if (view->TreeOpen)
          ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Show / hide the Outliner sidebar");
        ImGui::SameLine();

        if (view->InspOpen)
          ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
        temp_data->ToggleInsp = ImGui::Button(insp_lbl);
        if (view->InspOpen)
          ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Show / hide the Inspector sidebar");
        ImGui::SameLine();

        if (code_open)
          ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
        temp_data->ToggleCode = ImGui::Button(code_lbl);
        if (code_open)
          ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Show / hide the generated-code + problems panel");
        ImGui::SameLine();

        // Hiding the live mirror never deletes; render records the click, OnUpdate derives the state.
        if (show_live)
          ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
        temp_data->ToggleLive = ImGui::Button(live_lbl);
        if (show_live)
          ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Show / hide read-only nodes mirrored from the running app");

        // Palette pick from last frame's canvas folds into the same temp flags the buttons set.
        switch (ImGui::AppGraphConsumeHostCommand(&doc->Graph))
        {
        case ComposerHostCmd_Save:         temp_data->Save = true; break;
        case ComposerHostCmd_Load:         temp_data->Load = true; break;
        case ComposerHostCmd_Generate:     temp_data->WriteHeader = true; break;
        case ComposerHostCmd_CopyCode:     temp_data->CopyCode = true; break;
        case ComposerHostCmd_Diff:         temp_data->Diff = true; break;
        case ComposerHostCmd_PanelCode:    temp_data->RevealPanel = ComposerPanel_Code; break;
        case ComposerHostCmd_PanelProject: temp_data->RevealPanel = ComposerPanel_Project; break;
        case ComposerHostCmd_PanelOutput:  temp_data->RevealPanel = ComposerPanel_Output; break;
        case ComposerHostCmd_ToggleLive:   temp_data->ToggleLive = true; break;
        default: break;
        }

        // Phase captions: dim small-type row naming the flow under each cluster.
        {
          static const char* caps[4] = { "compose", "iterate", "persist", "produce" };
          ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.78f);
          ImGui::SetCursorPosX(cap_x[0]);
          ImGui::TextDisabled("%s", caps[0]);
          for (int i = 1; i < 4; i++)
          {
            ImGui::SameLine(cap_x[i]);
            ImGui::TextDisabled("%s", caps[i]);
          }
          ImGui::PopFont();
        }
      }
      ImGui::EndChild();
    }
  };

  //-----------------------------------------------------------------------------
  // [SECTION] Composer status strip (keymap hints + document counts)
  //-----------------------------------------------------------------------------

  // Status-pill grammar (F33): one shared primitive with one colour source, so no call site repeats a
  // status colour triple. ok = green, warn = amber, err = red, neutral = disabled ink.
  enum ComposerPillState { ComposerPill_Neutral, ComposerPill_Ok, ComposerPill_Warn, ComposerPill_Err };

  static ImVec4 ComposerPillColor(ComposerPillState s)
  {
    switch (s)
    {
    case ComposerPill_Ok:   return ImVec4(0.42f, 0.74f, 0.47f, 1.0f);
    case ComposerPill_Warn: return ImVec4(0.85f, 0.68f, 0.35f, 1.0f);
    case ComposerPill_Err:  return ImVec4(0.90f, 0.42f, 0.38f, 1.0f);
    default:                return ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
    }
  }

  // A small rounded pill: tinted background + state-coloured label. `id` keeps a stable widget identity
  // while the label swings. Returns true on click.
  static bool ComposerStatusPill(const char* id, ComposerPillState s, const char* label)
  {
    const ImVec4 col = ComposerPillColor(s);
    const float  em  = ImGui::GetFontSize();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, em * 0.9f);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(col.x, col.y, col.z, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x, col.y, col.z, 0.28f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(col.x, col.y, col.z, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    char buf[144];
    ImFormatString(buf, IM_ARRAYSIZE(buf), "%s###%s", label, id);
    const bool clicked = ImGui::SmallButton(buf);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    return clicked;
  }

  // Rendered last -> window bottom. Keymap hints left, document counts right; all right-side text is
  // derived in OnUpdate, OnRender only lays out text.
  struct StatusStripData
  {
    GraphDocData* Doc;
    char          CountMsg[96];                  // "design N  live N  promoted N"
    bool          HasMirror;                     // doc->Mirror != nullptr
    bool          MirrorInit;                    // mirror app initialized
    char          MirrorCounts[64];              // "L# W# S# C#"
    char          Breadcrumb[IM_LABEL_SIZE * 2]; // selection breadcrumb
    char          Msg[64];                       // transient write/diff confirmation (doc->WriteMsg snapshot)
    char          CycleMsg[96];                  // "cycle: <name> (+N)" when a data-dependency cycle blocks codegen (F21)
    int           CycleCount;                    // nodes the topo sort could not schedule; 0 = acyclic
    ImVector<int> CycleNodes;                    // those node ids -- the Select verb's target
    ImGuiID       CycleSig;                      // FrameSig the cycle was last computed at (recompute gate)
  };
  struct StatusStripTempData
  {
    bool SelectCycle;   // the Select verb was clicked this frame -> jump selection to the cycle nodes (applied next update)
    bool SelectScope;   // F32 breadcrumb zone -> select the current scope owner
    bool RevealTree;    // F32 counts zone     -> reveal the outliner
    bool ToggleLive;    // F32 mirror zone     -> toggle the live mirror
    bool Generate;      // F32 freshness zone  -> generate the header
  };
  struct StatusStripControl : ImGuiAppControl<StatusStripData, StatusStripTempData, GraphDocData>
  {
    virtual void OnInitialize(ImGuiApp* app, StatusStripData* data, const GraphDocData*) const override final
    {
      data->Doc = GetGraphDoc(app);
      data->CycleMsg[0] = 0;
      data->CycleCount  = 0;
      data->CycleSig    = 0;
    }

    virtual void OnUpdate(float dt, StatusStripData* data, const StatusStripTempData* temp_data, const StatusStripTempData*, const GraphDocData*) const override final
    {
      IM_UNUSED(dt);
      GraphDocData* doc = data->Doc;

      int nd = 0;
      int nl = 0;
      int np = 0;

      for (int i = 0; i < doc->Graph.Nodes.Size; i++)
      {
        const ImGuiAppNode* n = &doc->Graph.Nodes.Data[i];
        if (n->IsLive)
        {
          nl++;
        }
        else
        {
          nd++;
        }
        if (n->IsPromoted)
        {
          np++;
        }
      }
      if (doc->ShowLive)
        ImFormatString(data->CountMsg, IM_ARRAYSIZE(data->CountMsg), "d%d l%d p%d", nd, nl, np);   // design / live / promoted
      else
        ImFormatString(data->CountMsg, IM_ARRAYSIZE(data->CountMsg), "d%d", nd);

      data->HasMirror = (doc->Mirror != nullptr);
      if (data->HasMirror)
      {
        const ImGuiApp* a = doc->Mirror;
        ImFormatString(data->MirrorCounts, IM_ARRAYSIZE(data->MirrorCounts), "L%d W%d S%d C%d", a->Layers.Size, a->Windows.Size, a->Sidebars.Size, a->Controls.Size);
        data->MirrorInit = a->Layers.Size > 0;   // "composed"; Initialized is the platform flag
      }

      ImGui::AppGraphSelectionBreadcrumb(&doc->Graph, doc->Selection, data->Breadcrumb, IM_ARRAYSIZE(data->Breadcrumb));
      ImStrncpy(data->Msg, doc->WriteMsg, IM_ARRAYSIZE(data->Msg));

      // Data-dependency cycle (F21): recompute on signature change; surface a name + the node set the
      // Select verb jumps to. Applying the recorded Select click is a model write, so it lands here in
      // the update pass, not in OnRender.
      if (data->CycleSig != doc->FrameSig)
      {
        data->CycleSig = doc->FrameSig;
        char name[IM_LABEL_SIZE];
        data->CycleCount = ImGui::AppGraphDependencyCycle(&doc->Graph, &data->CycleNodes, name, IM_ARRAYSIZE(name));
        if (data->CycleCount > 0)
        {
          if (data->CycleCount > 1)
            ImFormatString(data->CycleMsg, IM_ARRAYSIZE(data->CycleMsg), "cycle: %s +%d", name, data->CycleCount - 1);
          else
            ImFormatString(data->CycleMsg, IM_ARRAYSIZE(data->CycleMsg), "cycle: %s", name);
        }
        else
          data->CycleMsg[0] = 0;
      }
      if (temp_data->SelectCycle && data->CycleNodes.Size > 0)
      {
        doc->Graph.Selection = data->CycleNodes;   // multi-select the whole cycle
        doc->Selection = data->CycleNodes.Data[0]; // primary = first member (drives the breadcrumb + inspector)
      }

      // F32 status-bar zone actions (model writes -> update pass, not render).
      if (temp_data->SelectScope)
      {
        int owner = -1;
        if (doc->Graph.ViewScope.Size > 0)
          owner = doc->Graph.ViewScope.back();   // innermost drilled scope
        else
        {
          for (int i = 0; i < doc->Graph.Nodes.Size; i++)   // root: the App node names the whole app
            if (doc->Graph.Nodes.Data[i].Kind == ImGuiAppNodeKind_App) { owner = doc->Graph.Nodes.Data[i].Id; break; }
          if (owner < 0 && doc->Graph.Nodes.Size > 0)
            owner = doc->Graph.Nodes.Data[0].Id;   // fallback: first node
        }
        if (owner >= 0)
        {
          doc->Selection = owner;
          doc->Graph.Selection.resize(0);
          doc->Graph.Selection.push_back(owner);
        }
      }
      if (temp_data->RevealTree)
        doc->TreeW = doc->TreeW > 0.0f ? 0.0f : 220.0f;   // show / hide the outliner column
      if (temp_data->ToggleLive)
        doc->ShowLive = !doc->ShowLive;
      if (temp_data->Generate)
        ComposerGenerateHeader(doc);
    }

    virtual void OnRender(const StatusStripData* data, StatusStripTempData* temp_data, const GraphDocData* doc_dep) const override final
    {
      const float       em    = ImGui::GetFontSize();
      const ImGuiStyle& style = ImGui::GetStyle();
      temp_data->SelectCycle = false;
      temp_data->SelectScope = false;
      temp_data->RevealTree  = false;
      temp_data->ToggleLive  = false;
      temp_data->Generate    = false;
      const ImVec4 dim = style.Colors[ImGuiCol_TextDisabled];

      if (ImGui::BeginChild("##Strip", ImVec2(0.0f, 0.0f), ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
      {
        // Fixed x anchors (em from content-left): the zones never shift as their own text changes width,
        // so each topic stays where the eye learned it. keymap | breadcrumb | counts | mirror | freshness.
        const float A_bread  = em * 15.0f;   // left zone (HEALTH + PERF pills) occupies 0..15em
        const float A_count  = em * 23.0f;
        const float A_mirror = em * 30.0f;
        const float A_fresh  = em * 37.0f;

        // A clickable zone at a fixed anchor: an invisible button (### id -> test-addressable) with the
        // label drawn over it; brightens on hover. The button is clamped to its slot (up to the next
        // anchor) so adjacent zones never overlap -- each anchor owns exactly one hit region.
        auto zone = [&](float anchor, float slot, const char* id, const char* txt, const ImVec4& col) -> bool
        {
          ImGui::SameLine(anchor);
          const ImVec2 p  = ImGui::GetCursorScreenPos();
          const ImVec2 sz = ImGui::CalcTextSize(txt);
          const bool clicked = ImGui::InvisibleButton(id, ImVec2(ImClamp(sz.x, em, slot), ImGui::GetFrameHeight()));
          const bool hov = ImGui::IsItemHovered();
          if (hov)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
          ImGui::GetWindowDrawList()->AddText(ImVec2(p.x, p.y + style.FramePadding.y),
                                              ImGui::GetColorU32(hov ? style.Colors[ImGuiCol_Text] : col), txt);
          return clicked;
        };
        const float slot = em * 6.5f;   // each right zone owns ~6.5em before the next anchor (7em apart)

        // HEALTH pill (F33): the whole graph's state in the shared pill grammar. Cycle -> err + name and a
        // click selects the cycle (folds in F21's Select verb); errors -> err "codegen blocked"; warnings
        // -> warn; else ok "graph ok". The transient keymap notice rides the feedback slot, not here.
        ComposerPillState hstate = ComposerPill_Ok;
        char hlabel[96] = "graph ok";
        if (data->CycleCount > 0)
        {
          hstate = ComposerPill_Err;
          ImFormatString(hlabel, IM_ARRAYSIZE(hlabel), ICON_FA_TRIANGLE_EXCLAMATION " %s", data->CycleMsg);
        }
        else if (doc_dep->NumErrors > 0)
        {
          hstate = ComposerPill_Err;
          ImFormatString(hlabel, IM_ARRAYSIZE(hlabel), ICON_FA_TRIANGLE_EXCLAMATION " codegen blocked");
        }
        else if (doc_dep->NumWarnings > 0)
        {
          hstate = ComposerPill_Warn;
          ImFormatString(hlabel, IM_ARRAYSIZE(hlabel), "%d warning%s", doc_dep->NumWarnings, doc_dep->NumWarnings == 1 ? "" : "s");
        }
        ImGui::AlignTextToFramePadding();
        // Id encodes the state so the state is externally observable (test) and the pill re-identifies when
        // it changes class.
        const char* hid = hstate == ComposerPill_Err ? "health-err" : hstate == ComposerPill_Warn ? "health-warn" : "health-ok";
        if (ComposerStatusPill(hid, hstate, hlabel) && data->CycleCount > 0)
          temp_data->SelectCycle = true;
        if (data->CycleCount > 0)
          ImGui::SetItemTooltip("Dependency cycle blocks codegen -- click to select the %d node(s)", data->CycleCount);
        else if (hstate == ComposerPill_Err)
          ImGui::SetItemTooltip("%d error(s) in the graph -- see Output", doc_dep->NumErrors);
        else if (hstate == ComposerPill_Warn)
          ImGui::SetItemTooltip("%d warning(s) in the graph", doc_dep->NumWarnings);
        else
          ImGui::SetItemTooltip("Graph validates clean");

        // PERF pill (F33): FPS + frame ms; tooltip carries the backend + draw counts.
        ImGui::SameLine(0.0f, em * 0.4f);
        const ImGuiIO& io = ImGui::GetIO();
        char plabel[48];
        ImFormatString(plabel, IM_ARRAYSIZE(plabel), "%.0f fps  %.1f ms", io.Framerate, io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
        ComposerStatusPill("perf", ComposerPill_Neutral, plabel);
        if (ImGui::IsItemHovered())
        {
          const ImDrawData* dd = ImGui::GetDrawData();
          ImGui::SetTooltip("backend: %s\nvtx %d  idx %d", io.BackendRendererName ? io.BackendRendererName : "?",
                            dd != nullptr ? dd->TotalVtxCount : 0, dd != nullptr ? dd->TotalIdxCount : 0);
        }

        // breadcrumb -> select the scope owner.
        if (zone(A_bread, slot, "###zbread", data->Breadcrumb[0] ? data->Breadcrumb : "(root)", dim))
          temp_data->SelectScope = true;
        ImGui::SetItemTooltip("Select the current scope owner");

        // counts -> show / hide the outliner.
        if (zone(A_count, slot, "###zcount", data->CountMsg, dim))
          temp_data->RevealTree = true;
        ImGui::SetItemTooltip("Show / hide the outliner");

        // mirror facts -> toggle the live mirror.
        const char* mir = data->HasMirror ? data->MirrorCounts : "no mirror";
        if (zone(A_mirror, slot, "###zmirror", mir, dim))
          temp_data->ToggleLive = true;
        ImGui::SetItemTooltip("Toggle the live mirror (currently %s; %s)", doc_dep->ShowLive ? "on" : "off",
                              data->HasMirror ? (data->MirrorInit ? "composed" : "uncomposed") : "no mirror");

        // freshness -> generate.
        const bool fresh = ImGui::AppGraphCodeFresh(&doc_dep->Graph);
        char frz[48];
        ImFormatString(frz, IM_ARRAYSIZE(frz), "%s  %s", fresh ? ICON_FA_CIRCLE_CHECK : ICON_FA_FILE_EXPORT, fresh ? "fresh" : "generate");
        if (zone(A_fresh, slot, "###zfresh", frz, ComposerPillColor(fresh ? ComposerPill_Ok : ComposerPill_Warn)))
          temp_data->Generate = true;
        ImGui::SetItemTooltip(fresh ? "Header matches the graph" : "Generate the whole-graph header");
      }
      ImGui::EndChild();
    }
  };

  //-----------------------------------------------------------------------------
  // [SECTION] Generated-code view (source-mapped, shared by every code surface)
  //-----------------------------------------------------------------------------

  // Shared by every code tab. With a source map it carries the coordinated-view interactions
  // (selection highlight + scroll, hover brushing, click-to-select). Pure view: reads const data,
  // records the click into *selection.
  static void ShowGeneratedCodeView(const ImGuiAppGraph* graph, const char* str_id, const ImGuiTextBuffer& text, const ImVector<int>& lines,
                                    const ImVector<ImGui::ImGuiAppCodeSpan>* spans, int* selection)
  {
    ImFont* code_font = graph != nullptr ? ImGui::AppGraphEditorState(graph)->CodeFont : nullptr;
    if (code_font)
      ImGui::PushFont(code_font, 0.0f);
    if (ImGui::BeginChild(str_id, ImVec2(-FLT_MIN, -FLT_MIN), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
    {
      const char* buf        = text.Buf.Data;
      const int   text_len   = text.size();
      const int   line_count = lines.Size;
      const float line_h     = ImGui::GetTextLineHeight();

      // Min 3 digits so short files don't jitter the layout.
      int digits = 3;
      for (int n = line_count; n >= 1000; n /= 10)
        digits++;
      const float gutter_w = ImGui::CalcTextSize("0").x * (float)digits;
      const ImU32 gutter_col = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.75f);
      const ImVec4 row_ink  = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const ImVec4 row_gold = ImLerp(kDemoGold, row_ink, 0.15f);

      ImGui::ImGuiAppHoverSource hsrc = ImGui::ImGuiAppHoverSource_None;
      const int brushed_node = spans != nullptr && graph != nullptr ? ImGui::AppGraphHoveredNode(graph, &hsrc) : -1;
      const int sel = selection != nullptr ? *selection : -1;
      auto span_owner = [&](int ln) -> int
      {
        if (spans == nullptr)
          return -1;
        for (int si = 0; si < spans->Size; si++)
          if (ln >= spans->Data[si].LineBegin && ln < spans->Data[si].LineEnd)
            return spans->Data[si].NodeId;
        return -1;
      };

      // On selection change, scroll its first span into the top quarter; the latch is window view state.
      if (spans != nullptr && selection != nullptr)
      {
        const ImGuiID focus_key = ImGui::GetID("##codefocus");
        ImGuiStorage* st = ImGui::GetStateStorage();
        if (st->GetInt(focus_key, -123456) != sel)
        {
          st->SetInt(focus_key, sel);
          for (int si = 0; si < spans->Size; si++)
            if (spans->Data[si].NodeId == sel)
            {
              ImGui::SetScrollY(ImMax(0.0f, (float)spans->Data[si].LineBegin * line_h - ImGui::GetWindowHeight() * 0.25f));
              break;
            }
        }
      }

      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
      ImGuiListClipper clipper;
      clipper.Begin(line_count, line_h);
      while (clipper.Step())
      {
        for (int ln = clipper.DisplayStart; ln < clipper.DisplayEnd; ln++)
        {
          const int   owner    = span_owner(ln);
          const bool  hl_sel   = owner >= 0 && owner == sel;
          const bool  hl_brush = owner >= 0 && owner == brushed_node;
          const ImVec2 row_min = ImGui::GetCursorScreenPos();
          ImDrawList*  dl      = ImGui::GetWindowDrawList();

          if (hl_sel || hl_brush)
          {
            const ImVec2 row_max(row_min.x + ImGui::GetContentRegionAvail().x, row_min.y + line_h);
            dl->AddRectFilled(row_min, row_max, hl_sel ? DemoThemeCol(row_gold, 0.13f) : DemoThemeCol(row_ink, 0.06f));
            dl->AddRectFilled(row_min, ImVec2(row_min.x + ImGui::GetFontSize() * 0.1875f, row_max.y), hl_sel ? DemoThemeCol(row_gold, 0.86f) : DemoThemeCol(row_ink, 0.35f));
          }

          char num[16];
          const int num_len = ImFormatString(num, IM_ARRAYSIZE(num), "%d", ln + 1);
          dl->AddText(ImVec2(row_min.x + gutter_w - ImGui::CalcTextSize(num, num + num_len).x, row_min.y), gutter_col, num, num + num_len);

          const char* b = buf + lines.Data[ln];
          const char* e = (ln + 1 < line_count) ? buf + lines.Data[ln + 1] - 1 : buf + text_len;
          while (e > b && (e[-1] == '\n' || e[-1] == '\r'))
            e--;
          ImGui::SetCursorScreenPos(ImVec2(row_min.x + gutter_w + ImGui::CalcTextSize("0").x, row_min.y));
          ImGui::TextUnformatted(b, e);

          if (owner >= 0 && ImGui::IsItemHovered())
          {
            ImGui::AppGraphHoverNode(graph, owner, ImGui::ImGuiAppHoverSource_External);
            if (selection != nullptr && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
              *selection = owner;
          }
        }
      }
      ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    if (code_font)
      ImGui::PopFont();
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Composer editor body (outliner | canvas + bottom console | inspector; project inspector)
  //-----------------------------------------------------------------------------

  struct EditorBodyData
  {
    GraphDocData*                     Doc;                     // shared doc, cached non-const in OnInitialize
    bool                              TreeDragging;            // tree splitter drag FSM (advanced only in OnUpdate)
    float                             TreeDragDX;              // grab offset within the grip, captured at drag start
    bool                              InspDragging;            // inspector splitter drag FSM (advanced only in OnUpdate)
    ImGuiTextBuffer                   CodeText;                // whole app's generated C++ (kept current while the panel is open)
    ImVector<ImGui::ImGuiAppCodeSpan> CodeSpans;               // source map: node id -> line ranges in CodeText
    ImVector<int>                     CodeLines;               // byte offset of each line start in CodeText (render index)
    ImGuiTextBuffer                   CodeNodeText;            // the selected node's code (the focused "Node" tab)
    ImVector<int>                     NodeLines;               // line starts in CodeNodeText
    ImGuiID                           CodeSig;                 // graph signature the buffers were generated from (regen gate)
    int                               CodeSel;                 // selection they were generated for (regen gate)
    bool                              HasCode;                 // CodeText is non-empty
    bool                              HasNodeCode;             // a node is selected and its code was generated
    char                              CodeName[IM_LABEL_SIZE]; // selected node's draft name (the Node tab label)

    // Diff mode, regenerated behind the same signature gate as the code buffers.
    bool                                DiffMode; // Code tab shows diff-vs-saved instead of the whole program
    ImGuiTextBuffer                     DiffText;
    ImVector<int>                       DiffLines;
    bool                                HasDiff;  // a saved graph existed to diff against
    ImVector<ImGui::ImGuiAppGraphIssue> Issues;   // validation problems, recomputed while the panel is open

    // Project tab: the document's files on disk, rescanned on a slow cadence in OnUpdate.
    struct ProjFile { char Name[160]; unsigned long long Size; bool IsGraph; bool IsHeader; };
    ImVector<ProjFile> ProjFiles;
    float              ProjRescan; // seconds until the next directory scan
  };
  // Raw input recorded by OnRender (the only place ImGui item geometry exists), consumed by OnUpdate.
  struct EditorBodyTempData
  {
    bool  TreeGripActivated; // drag started on the tree grip this frame
    bool  MouseLeftDown;     // left button held
    float MouseX;            // mouse x (screen)
    float TreeGripMinX;      // tree grip left edge
    float TreeOriginX;       // body row left edge
    bool  CodeGripActive;    // code grip held this frame
    float CodeResolved;      // this frame's clamped code height (drag base)
    float CodeMax;           // this frame's max code height
    float MouseDY;           // mouse y delta while dragging the code grip
    bool  CodeSnapClosed;    // resolved code height collapsed below threshold while idle
    bool  SelectionChanged;  // tree/canvas changed the selection this frame
    int   Selection;         // the new selection
    bool  InspGripActivated; // inspector splitter drag started this frame
    float BodyMaxX;          // body row right edge (screen) -- the inspector width derives from it
    bool  ProjLoadGraph;     // Project tab: load the graph file
    bool  OpenOutput;        // viewport status strip clicked -> reveal + select the Output tab
    bool  AckReveal;         // the bottom tab bar consumed the one-shot RevealPanel intent this frame
    bool  ClearLog;          // Output tab: clear the document log
    bool  ToggleDiffMode;    // Code tab header: flip whole-program <-> diff-vs-saved
    int   StampPrefab;       // prefab index to instantiate (-1 = none)
  };

  // Empty selection shows the document, in the same section grammar as the node inspector.
  static void ShowComposerProjectInspector(GraphDocData* doc, ImGuiAppGraph* graph, EditorBodyTempData* temp_data)
  {
    const float em = ImGui::GetFontSize();
    const float label_w = em * 5.5f;

    if (ImGui::AppInspectorSection("##psec_doc", ICON_FA_FILE_LINES, "Document", nullptr, nullptr))
    {
      const bool fresh = ImGui::AppGraphCodeFresh(&doc->Graph);
      ImGui::TextDisabled("graph");
      ImGui::SameLine(label_w);
      ImGui::TextUnformatted(doc->GraphPath);
      ImGui::TextDisabled("header");
      ImGui::SameLine(label_w);
      ImGui::TextUnformatted(doc->HeaderPath);
      ImGui::SameLine();
      ImGui::TextColored(fresh ? ImVec4(0.45f, 0.85f, 0.45f, 1.0f) : ImVec4(0.90f, 0.75f, 0.35f, 1.0f),
                         fresh ? ICON_FA_CHECK : ICON_FA_TRIANGLE_EXCLAMATION);
      ImGui::SetItemTooltip(fresh ? "header matches the graph" : "graph changed since the last Generate");
      int design = 0, live = 0;
      for (int i = 0; i < graph->Nodes.Size; i++)
        (graph->Nodes.Data[i].IsLive ? live : design)++;
      ImGui::TextDisabled("nodes");
      ImGui::SameLine(label_w);
      if (live > 0)
        ImGui::Text("%d design   %d live", design, live);
      else
        ImGui::Text("%d design", design);
      ImGui::TextDisabled("wiring");
      ImGui::SameLine(label_w);
      ImGui::Text("%d links   %d bindings", graph->Links.Size, graph->Bindings.Size);
      ImGui::TextDisabled("signature");
      ImGui::SameLine(label_w);
      ImGui::Text("%08X", doc->FrameSig);
      ImGui::Spacing();
    }

    if (ImGui::AppInspectorSection("##psec_val", ICON_FA_TRIANGLE_EXCLAMATION, "Validation", nullptr, nullptr))
    {
      const int nerr = doc->NumErrors, nwarn = doc->NumWarnings;
      if (nerr + nwarn == 0)
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), ICON_FA_CHECK "  No configuration problems.");
      else
      {
        ImGui::TextColored(nerr > 0 ? ImVec4(0.92f, 0.45f, 0.45f, 1.0f) : ImVec4(0.92f, 0.80f, 0.40f, 1.0f),
                           "%d error(s), %d warning(s)", nerr, nwarn);
        if (ImGui::SmallButton("Open Output"))
          temp_data->OpenOutput = true;
      }
      ImGui::Spacing();
    }

    if (ImGui::AppInspectorSection("##psec_theme", ICON_FA_PALETTE, "Composer theme", nullptr, nullptr))
    {
      ImGui::ImGuiAppChromeTheme* theme = ImGui::AppGraphChromeTheme();
      auto theme_rows = [](const char* caption, ImGuiAppColorModDesc* descs, int count)
      {
        ImGui::TextDisabled("%s", caption);
        for (int i = 0; i < count; i++)
        {
          ImGui::PushID(caption);
          ImGui::PushID(i);
          ImGui::Checkbox("##on", &descs[i].Active);
          ImGui::SameLine();
          ImVec4 c4 = ImGui::ColorConvertU32ToFloat4(descs[i].Value);
          if (ImGui::ColorEdit4("##v", &c4.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf))
            descs[i].Value = ImGui::ColorConvertFloat4ToU32(c4);
          ImGui::SameLine();
          ImGui::TextDisabled("%s", ImGui::GetStyleColorName(descs[i].Col));
          ImGui::PopID();
          ImGui::PopID();
        }
      };
      theme_rows("dropdown fields", theme->Combo, IM_ARRAYSIZE(theme->Combo));
      theme_rows("in-place editors", theme->Edit, IM_ARRAYSIZE(theme->Edit));
      ImGui::Spacing();
    }

    if (ImGui::AppInspectorSection("##psec_prefabs", ICON_FA_CUBES, "Prefabs", nullptr, nullptr))
    {
      if (ImGui::AppGraphPrefabCount(&doc->Graph) == 0)
        ImGui::TextDisabled("Save a selection as a prefab from the canvas context menu.");
      for (int i = 0; i < ImGui::AppGraphPrefabCount(&doc->Graph); i++)
      {
        ImGui::PushID(i);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(ImGui::AppGraphPrefabName(&doc->Graph, i));
        ImGui::SameLine(ImGui::GetContentRegionMax().x - em * 4.0f);
        if (ImGui::SmallButton("Stamp"))
          temp_data->StampPrefab = i;
        ImGui::SetItemTooltip("Instantiate this prefab on the canvas (fresh ids, selected)");
        ImGui::PopID();
      }
    }
  }
  struct EditorBodyControl : ImGuiAppControl<EditorBodyData, EditorBodyTempData, GraphDocData>
  {
    virtual void OnInitialize(ImGuiApp* app, EditorBodyData* data, const GraphDocData*) const override final
    {
      data->Doc = GetGraphDoc(app);
      data->CodeSig = 0;
      data->CodeSel = -2;   // "never generated" (a real empty selection is -1)
      data->InspDragging = false;
      data->ProjRescan = 0.0f;
      data->DiffMode = false;
      data->HasDiff = false;
    }

    virtual void OnUpdate(float dt, EditorBodyData* data, const EditorBodyTempData* temp_data, const EditorBodyTempData* last_temp_data, const GraphDocData*) const override final
    {
      GraphDocData* doc = data->Doc;

      // Tree splitter FSM, driven by input captured last render.
      if (temp_data->TreeGripActivated)
      {
        data->TreeDragging = true;
        data->TreeDragDX = temp_data->MouseX - temp_data->TreeGripMinX;
      }
      if (data->TreeDragging)
      {
        if (!temp_data->MouseLeftDown)
        {
          data->TreeDragging = false;
        }
        else
        {
          doc->TreeW = temp_data->MouseX - data->TreeDragDX - temp_data->TreeOriginX;
        }
      }

      // Code splitter: drag up grows the panel; release tiny snaps it closed.
      if (temp_data->CodeGripActive)
      {
        doc->CodeH = ImClamp(temp_data->CodeResolved - temp_data->MouseDY, 0.0f, temp_data->CodeMax);
      }
      else if (temp_data->CodeSnapClosed)
      {
        doc->CodeH = 0.0f;
      }

      // Inspector splitter: same FSM as the tree's, mirrored to the right edge.
      if (temp_data->InspGripActivated)
      {
        data->InspDragging = true;
      }
      if (data->InspDragging)
      {
        if (!temp_data->MouseLeftDown)
          data->InspDragging = false;
        else
          doc->InspW = temp_data->BodyMaxX - temp_data->MouseX;
      }

      // Revealing a bottom tab opens the bar if collapsed -- an intent must never land on a closed panel.
      if (temp_data->OpenOutput)
        doc->RevealPanel = ComposerPanel_Output;
      if (doc->RevealPanel != ComposerPanel_None && doc->CodeH <= 0.0f)
        doc->CodeH = ImGui::GetFontSize() * 12.0f;
      if (temp_data->AckReveal)
        doc->RevealPanel = ComposerPanel_None;
      if (temp_data->ClearLog)
      {
        doc->Log.resize(0);
      }

      if (temp_data->ProjLoadGraph)
      {
        ImGui::LoadAppGraph(doc->GraphPath, &doc->Graph);
        ImGui::AppGraphEnsureFoundation(&doc->Graph);
        ImGui::AppGraphRequestFitAll(&doc->Graph);
        DocLog(doc, 0, "loaded graph <- %s (Project)", doc->GraphPath);
      }
      if (temp_data->StampPrefab >= 0)
      {
        ImGui::AppGraphInstantiatePrefab(&doc->Graph, temp_data->StampPrefab, ImVec2(140.0f, 140.0f));
        DocLog(doc, 0, "stamped prefab '%s'", ImGui::AppGraphPrefabName(&doc->Graph, temp_data->StampPrefab));
      }
      data->ProjRescan -= dt;
      if (data->ProjRescan <= 0.0f)
      {
        data->ProjRescan = 2.0f;
        data->ProjFiles.resize(0);
        std::error_code fs_ec;
        for (const auto& entry : std::filesystem::directory_iterator(".", fs_ec))
        {
          if (data->ProjFiles.Size >= 64 || !entry.is_regular_file(fs_ec))
            continue;
          const std::filesystem::path& p = entry.path();
          const std::string ext = p.extension().string();
          if (ext != ".txt" && ext != ".h" && ext != ".wal" && ext != ".ini")
            continue;
          EditorBodyData::ProjFile f;
          ImFormatString(f.Name, IM_ARRAYSIZE(f.Name), "%s", p.filename().string().c_str());
          f.Size = (unsigned long long)entry.file_size(fs_ec);
          f.IsGraph  = strcmp(f.Name, doc->GraphPath) == 0;
          f.IsHeader = strcmp(f.Name, doc->HeaderPath) == 0;
          data->ProjFiles.push_back(f);
        }
      }

      if (temp_data->SelectionChanged)
      {
        doc->Selection = temp_data->Selection;
      }

      // Regenerate the code buffers only when graph signature or selection changed. A closed panel keeps
      // stale buffers; the gate makes them correct when next needed.
      if (temp_data->ToggleDiffMode)
      {
        data->DiffMode = !data->DiffMode;
        data->CodeSig = 0;   // force the gated regen below
      }
      const ImGuiID gsig = doc->FrameSig;
      if (gsig != data->CodeSig || doc->Selection != data->CodeSel)
      {
        data->CodeSig = gsig;
        data->CodeSel = doc->Selection;

        auto index_lines = [](const ImGuiTextBuffer& text, ImVector<int>* lines)
        {
          lines->resize(0);
          if (text.size() > 0)
            lines->push_back(0);
          const char* s = text.Buf.Data;
          for (int i = 0; i < text.size(); i++)
            if (s[i] == '\n' && i + 1 < text.size())
              lines->push_back(i + 1);
        };

        data->CodeText.clear();
        data->CodeSpans.resize(0);
        ImGui::GenerateAppGraphCodeEx(&doc->Graph, &data->CodeText, &data->CodeSpans);
        data->HasCode = data->CodeText.size() > 0;
        index_lines(data->CodeText, &data->CodeLines);

        data->CodeNodeText.clear();
        data->HasNodeCode = false;
        data->CodeName[0] = 0;
        if (ImGuiAppNode* seln = doc->Selection >= 0 ? ImGui::AppGraphFindNode(&doc->Graph, doc->Selection) : nullptr)
        {
          ImGui::GenerateAppNodeCode(&doc->Graph, seln, &data->CodeNodeText, doc->Mirror);
          ImStrncpy(data->CodeName, seln->Draft.Name, sizeof(data->CodeName));
          data->HasNodeCode = data->CodeNodeText.size() > 0;
        }
        index_lines(data->CodeNodeText, &data->NodeLines);

        data->DiffText.clear();
        data->HasDiff = false;
        if (data->DiffMode)
        {
          ImGuiAppGraph saved;
          if (ImGui::LoadAppGraph(doc->GraphPath, &saved))
          {
            ImGui::AppGraphDiffCode(&saved, &doc->Graph, &data->DiffText);
            data->HasDiff = true;
          }
          saved.Nodes.clear_destruct();   // scratch graph owns its nodes' inner vectors
        }
        index_lines(data->DiffText, &data->DiffLines);
      }

      // Only while the panel is open -- validation scans the whole graph.
      data->Issues.clear();
      if (doc->CodeH > 0.0f)
      {
        ImGui::AppGraphValidate(&doc->Graph, &data->Issues);
      }
    }

    virtual void OnRender(const EditorBodyData* data, EditorBodyTempData* temp_data, const GraphDocData*) const override final
    {
      GraphDocData* doc = data->Doc;
      temp_data->StampPrefab = -1;   // zero-init would read as prefab INDEX 0 -- "none" must be explicit
      if (doc->Mirror == nullptr)
      {
        return;
      }
      ImGuiApp*      app   = doc->Mirror;                    // non-const: the viewer/canvas APIs edit through it
      ImGuiAppGraph* graph = &doc->Graph;

      // Canvas theme rides the graph's canvas; GridSpacing doubles as the applied sentinel.
      {
        ImGuiCanvasStyle* cs = ImGui::CanvasGetStyle(ImGui::AppGraphEditorCanvas(graph));
        if (cs->GridSpacing != 26.0f)
        {
          const ImVec4 wire_ink = ImGui::GetStyleColorVec4(ImGuiCol_Text);
          cs->WireHovered    = DemoThemeCol(ImLerp(kDemoGold, wire_ink, 0.10f), 1.0f);
          cs->WireSelected   = DemoThemeCol(ImLerp(kDemoGold, wire_ink, 0.18f), 1.0f);
          cs->NodeRounding   = 5.0f;
          cs->NodePadding    = ImVec2(9.0f, 7.0f);
          cs->NodeBorder     = 1.0f;
          cs->WireThickness  = 2.6f;
          cs->PinRadius      = 4.2f;
          cs->PinHoverRadius = 10.0f;
          cs->GridSpacing    = 26.0f;
        }
      }

      // Layout is local + display-only; nothing is written to the doc from OnRender.
      const float    em            = ImGui::GetFontSize();
      const ImGuiIO& io            = ImGui::GetIO();
      ImVec2         body          = ImGui::GetContentRegionAvail();
      // Status strip's real height: frame height + FramePadding.y both sides + ItemSpacing before it.
      const ImGuiStyle& lay_style = ImGui::GetStyle();
      body.y = ImMax(em * 4.0f, body.y - (ImGui::GetFrameHeight() + lay_style.FramePadding.y * 2.0f + lay_style.ItemSpacing.y));
      const float    tree_origin_x = ImGui::GetCursorScreenPos().x;
      const float    tree_grip     = em * 0.5f;
      const float    min_canvas_w  = em * 16.0f;
      const float    min_canvas_h  = em * 8.0f;
      const float    vspacing      = lay_style.ItemSpacing.y;   // canvas | grip | code stack in ##Right
      // ##Right stacks canvas | grip | code with ItemSpacing between: heights must sum to body.y
      // INCLUDING that spacing, or the column grows a scrollbar.
      const float    code_grip     = (doc->CodeH > 0.0f) ? em * 0.5f : 0.0f;
      const float    code_chrome   = (doc->CodeH > 0.0f) ? code_grip + vspacing * 2.0f : 0.0f;
      const float    code_max      = ImMax(0.0f, body.y - min_canvas_h - code_chrome);
      const float    code_h        = ImClamp(doc->CodeH, 0.0f, code_max);
      const float    canvas_h      = ImMax(0.0f, body.y - ((code_h > 0.0f) ? code_h + code_chrome : 0.0f));
      // A collapsed sidebar contributes zero width; its saved width survives for the next expand.
      // Open flags are Composer view state (toolbar buttons + "View:" palette commands toggle them).
      const bool     tree_open     = ImGui::AppGraphViewState(graph)->TreeOpen;
      const bool     insp_open     = ImGui::AppGraphViewState(graph)->InspOpen;
      const float    insp_grip     = insp_open ? em * 0.5f : 0.0f;
      const float    tree_grip_w   = tree_open ? tree_grip : 0.0f;
      const float    tree_w        = tree_open ? ImClamp((doc->TreeW > 0.0f) ? doc->TreeW : em * 16.0f, em * 9.0f, ImMax(em * 9.0f, body.x - tree_grip_w - min_canvas_w)) : 0.0f;
      const float    insp_w        = insp_open ? ImClamp((doc->InspW > 0.0f) ? doc->InspW : em * 22.0f, em * 14.0f, ImMax(em * 14.0f, body.x - tree_w - tree_grip_w - insp_grip - min_canvas_w)) : 0.0f;
      const float    right_w       = ImMax(0.0f, body.x - tree_w - tree_grip_w - insp_grip - insp_w);
      int            selection     = doc->Selection;
      float          col_w         = 0.0f;     // assigned inside ##Right (needs that child's content region)

      temp_data->TreeGripActivated = false;
      temp_data->MouseLeftDown     = ImGui::IsMouseDown(ImGuiMouseButton_Left);
      temp_data->MouseX            = io.MousePos.x;
      temp_data->TreeGripMinX      = 0.0f;
      temp_data->TreeOriginX       = tree_origin_x;
      if (tree_open)
      {
        if (ImGui::BeginChild("##Tree", ImVec2(tree_w, body.y), ImGuiChildFlags_Borders))
        {
          ImGui::ShowAppGraphTree(app, graph, &selection, doc->ShowLive);
        }
        ImGui::EndChild();

        // Tree splitter: record raw input; the drag resolves in OnUpdate.
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::InvisibleButton("##tsplit", ImVec2(tree_grip, body.y));
        temp_data->TreeGripActivated = ImGui::IsItemActivated();
        temp_data->TreeGripMinX      = ImGui::GetItemRectMin().x;
        if (data->TreeDragging || ImGui::IsItemHovered())
        {
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        ImGui::SameLine(0.0f, 0.0f);
      }

      // NoScrollbar is a hard rule: children fill this column exactly; a scrollbar means the layout
      // math regressed and must fail visibly (clipped content).
      if (ImGui::BeginChild("##Right", ImVec2(right_w, body.y), ImGuiChildFlags_None,
                            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
      {
        col_w = ImGui::GetContentRegionAvail().x;

        if (ImGui::BeginChild("##NodeGraph", ImVec2(col_w, canvas_h), ImGuiChildFlags_Borders))
        {
          // Document verbs for the canvas palette; the pick comes back through AppGraphConsumeHostCommand.
          static const ImGui::ImGuiAppGraphHostCmd host_cmds[] =
          {
            { "File: Save graph", "Ctrl+S", ComposerHostCmd_Save },
            { "File: Load graph", "", ComposerHostCmd_Load },
            { "File: Generate C++ header", "", ComposerHostCmd_Generate },
            { "File: Copy generated C++", "", ComposerHostCmd_CopyCode },
            { "File: Diff vs saved -> clipboard", "", ComposerHostCmd_Diff },
            { "Panel: Code", "", ComposerHostCmd_PanelCode },
            { "Panel: Project", "", ComposerHostCmd_PanelProject },
            { "Panel: Output", "", ComposerHostCmd_PanelOutput },
            { "View: Toggle live mirror", "", ComposerHostCmd_ToggleLive },
          };
          ImGui::AppGraphSetHostCommands(graph, host_cmds, IM_ARRAYSIZE(host_cmds));

          ImGui::ShowAppGraphEditor(app, graph, &selection, doc->ShowLive);

          // F30: while App-time is frozen/rewound, wash the viewport amber so the canvas reads as paused
          // (not live) -- matches the engaged freeze button. A thin inner border makes the state obvious
          // at a glance. Low alpha so nodes stay readable.
          if (doc->Transport != nullptr && doc->Transport->Frozen)
          {
            ImDrawList*  dl   = ImGui::GetWindowDrawList();
            const ImVec2 vmin = ImGui::GetWindowPos();
            const ImVec2 vmax = ImVec2(vmin.x + ImGui::GetWindowSize().x, vmin.y + ImGui::GetWindowSize().y);
            dl->AddRectFilled(vmin, vmax, IM_COL32(210, 150, 40, 30));                       // amber wash
            dl->AddRect(ImVec2(vmin.x + 1.0f, vmin.y + 1.0f), ImVec2(vmax.x - 1.0f, vmax.y - 1.0f),
                        IM_COL32(210, 150, 40, 150), 0.0f, 0, ImMax(2.0f, em * 0.15f));      // engaged border
          }

          // Viewport overlays (health readout bottom-left, transport bottom-center): real ImGui
          // items submitted after the editor, so they win hover over the canvas.
          {
            const int nerr = doc->NumErrors, nwarn = doc->NumWarnings;

            char health[48];
            if (nerr + nwarn > 0)
              ImFormatString(health, IM_ARRAYSIZE(health), ICON_FA_TRIANGLE_EXCLAMATION " %d   ! %d", nerr, nwarn);
            else
              ImFormatString(health, IM_ARRAYSIZE(health), ICON_FA_CHECK " 0");
            const char* last_log = doc->Log.Size > 0 ? doc->Log.back().Text : "";

            const ImVec2 c_min  = ImGui::GetWindowPos();
            const ImVec2 c_size = ImGui::GetWindowSize();
            const float  h      = ImGui::GetTextLineHeight() + em * 0.5f;
            const ImVec2 s_min(c_min.x + em * 0.6f, c_min.y + c_size.y - h - em * 0.55f);

            // Real overlay WINDOW, not draw-list-over-canvas: must render above canvas content AND keep
            // its hit-test when a node scrolls beneath it.
            ImGui::SetNextWindowPos(s_min);
            ImGui::SetNextWindowBgAlpha(0.99f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, em * 0.35f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg,
                DemoThemeMix(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg), ImGui::GetStyleColorVec4(ImGuiCol_Text), 0.04f, 0.99f));
            temp_data->OpenOutput = false;
            if (ImGui::Begin("##canvas_health", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar))
            {
              const ImVec4 health_ink = ImGui::GetStyleColorVec4(ImGuiCol_Text);
              const ImU32 health_col = DemoThemeCol(ImLerp(nerr > 0 ? kDemoRed : nwarn > 0 ? kDemoYellow : kDemoGreen, health_ink, 0.15f), 1.0f);
              ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(health_col), "%s", health);
              if (last_log[0])
              {
                ImGui::SameLine();
                char clipped[64];
                ImStrncpy(clipped, last_log, IM_ARRAYSIZE(clipped));
                ImGui::TextDisabled("%s", clipped);
              }
              temp_data->OpenOutput = ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
              if (ImGui::IsWindowHovered())
              {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip("%d error(s), %d warning(s) -- click to open Output", nerr, nwarn);
              }
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
          }

        }
        ImGui::EndChild();

        if (code_h > 0.0f)
        {
          // Code splitter: record raw input only.
          ImGui::InvisibleButton("##hsplit", ImVec2(col_w, code_grip));
          temp_data->CodeGripActive = ImGui::IsItemActive();
          temp_data->CodeResolved   = code_h;
          temp_data->CodeMax        = code_max;
          temp_data->MouseDY        = io.MouseDelta.y;
          temp_data->CodeSnapClosed = !temp_data->CodeGripActive && code_h < em * 4.0f;
          if (temp_data->CodeGripActive || ImGui::IsItemHovered())
          {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
          }

          if (ImGui::BeginChild("##CodePanel", ImVec2(col_w, code_h), ImGuiChildFlags_Borders))
          {
            if (ImGui::BeginTabBar("##bottomtabs"))
            {
              temp_data->ToggleDiffMode = false;
              if (ImGui::BeginTabItem("Code", nullptr, doc->RevealPanel == ComposerPanel_Code ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
              {
                // Shared tab header grammar: context label left, actions right.
                ImGui::AlignTextToFramePadding();
                // Header context label. Amber when the shown code is ahead of the file on disk (never
                // written, or diverged since); neutral when fresh, empty, or diffing.
                const bool code_fresh = ImGui::AppGraphCodeFresh(&doc->Graph);
                const bool code_ahead = !code_fresh && data->HasCode && !data->DiffMode;
                if (data->DiffMode)
                  ImGui::TextDisabled("Diff vs saved graph");
                else if (!code_ahead)
                  ImGui::TextDisabled("Whole program");
                else
                  ImGui::TextColored(ImLerp(kDemoGold, ImGui::GetStyleColorVec4(ImGuiCol_Text), 0.25f),
                                     doc->Graph.GenSignature != 0 ? ICON_FA_TRIANGLE_EXCLAMATION "  Whole program -- ahead of file"
                                                                  : ICON_FA_TRIANGLE_EXCLAMATION "  Whole program -- unwritten");
                if (doc->WriteMsg[0])
                {
                  ImGui::SameLine();
                  ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", doc->WriteMsg);
                }
                {
                  const float right = ImGui::GetContentRegionMax().x;
                  ImGui::SameLine(right - em * 9.0f);
                  if (data->DiffMode)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                  temp_data->ToggleDiffMode = ImGui::SmallButton("Diff");
                  if (data->DiffMode)
                    ImGui::PopStyleColor();
                  ImGui::SetItemTooltip("Show the generated C++ as a diff against the SAVED graph");
                  ImGui::SameLine();
                  if (code_ahead)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImLerp(kDemoGold, ImGui::GetStyleColorVec4(ImGuiCol_Text), 0.2f));
                  if (ImGui::SmallButton("Copy"))
                    ImGui::SetClipboardText(data->DiffMode ? data->DiffText.c_str() : data->CodeText.c_str());
                  if (code_ahead)
                  {
                    ImGui::PopStyleColor();
                    ImGui::SetItemTooltip("This C++ is ahead of %s -- Generate to write it", doc->HeaderPath);
                  }
                }
                if (data->DiffMode)
                {
                  if (!data->HasDiff)
                    ImGui::TextDisabled("No saved graph to diff against -- Save first.");
                  else if (data->DiffText.size() == 0)
                    ImGui::TextDisabled("No differences: the graph matches the save.");
                  else
                    ShowGeneratedCodeView(&doc->Graph, "##codediff", data->DiffText, data->DiffLines, nullptr, nullptr);
                }
                else if (!data->HasCode)
                {
                  ImGui::TextDisabled("The graph generates no code yet -- add a window or a control.");
                }
                else
                {
                  ShowGeneratedCodeView(&doc->Graph, "##codeall", data->CodeText, data->CodeLines, &data->CodeSpans, &selection);
                }
                ImGui::EndTabItem();
              }
              if (ImGui::BeginTabItem("Project", nullptr, doc->RevealPanel == ComposerPanel_Project ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
              {
                temp_data->ProjLoadGraph = false;
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("Document files on disk");
                if (data->ProjFiles.Size == 0)
                {
                  ImGui::TextDisabled("No document files yet -- Save the graph or Generate the header.");
                }
                else if (ImGui::BeginTable("##proj", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                {
                  ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch, 0.6f);
                  ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.15f);
                  ImGui::TableSetupColumn("##act", ImGuiTableColumnFlags_WidthStretch, 0.25f);
                  ImGui::TableHeadersRow();
                  const bool fresh = ImGui::AppGraphCodeFresh(&doc->Graph);
                  for (int i = 0; i < data->ProjFiles.Size; i++)
                  {
                    const EditorBodyData::ProjFile& f = data->ProjFiles.Data[i];
                    ImGui::PushID(i);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const char* icon = f.IsGraph ? ICON_FA_CIRCLE_NODES : f.IsHeader ? ICON_FA_CODE : ICON_FA_FILE_LINES;
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%s  %s", icon, f.Name);
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    if (f.Size >= 1024)
                      ImGui::TextDisabled("%.1f KB", (double)f.Size / 1024.0);
                    else
                      ImGui::TextDisabled("%llu B", f.Size);
                    ImGui::TableNextColumn();
                    if (f.IsGraph)
                    {
                      if (ImGui::SmallButton("Load"))
                        temp_data->ProjLoadGraph = true;
                      ImGui::SetItemTooltip("Load this graph into the Composer");
                    }
                    else if (f.IsHeader)
                    {
                      ImGui::TextColored(fresh ? ImVec4(0.45f, 0.85f, 0.45f, 1.0f) : ImVec4(0.90f, 0.75f, 0.35f, 1.0f),
                                         fresh ? ICON_FA_CHECK "  matches graph" : ICON_FA_TRIANGLE_EXCLAMATION "  stale");
                    }
                    else
                    {
                      ImGui::TextDisabled("write-ahead log");
                    }
                    ImGui::PopID();
                  }
                  ImGui::EndTable();
                }
                ImGui::EndTabItem();
              }
              char output_label[32];
              if (data->Issues.Size > 0)
                ImFormatString(output_label, IM_ARRAYSIZE(output_label), "Output (%d)###output", data->Issues.Size);
              else
                ImStrncpy(output_label, "Output###output", IM_ARRAYSIZE(output_label));
              temp_data->AckReveal = doc->RevealPanel != ComposerPanel_None;   // consume the one-shot select
              if (ImGui::BeginTabItem(output_label, nullptr, doc->RevealPanel == ComposerPanel_Output ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
              {
                int nerr2 = 0, nwarn2 = 0;
                for (int i = 0; i < data->Issues.Size; i++)
                  (data->Issues.Data[i].Severity >= 2 ? nerr2 : nwarn2)++;

                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%d error(s)  %d warning(s)  %d log line(s)", nerr2, nwarn2, doc->Log.Size);
                {
                  const float right = ImGui::GetContentRegionMax().x;
                  ImGui::SameLine(right - em * 22.0f);
                  auto sev_toggle = [](const char* lbl, bool* on, const ImVec4& col) {
                    ImGui::PushStyleColor(ImGuiCol_Text, *on ? col : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    if (ImGui::SmallButton(lbl))
                      *on = !*on;
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                  };
                  ImGuiAppEditorState* ed = ImGui::AppGraphEditorState(&doc->Graph);
                  sev_toggle("err",  &ed->OutputShowErr,  ImVec4(0.92f, 0.45f, 0.45f, 1.0f));
                  sev_toggle("warn", &ed->OutputShowWarn, ImVec4(0.92f, 0.80f, 0.40f, 1.0f));
                  sev_toggle("info", &ed->OutputShowInfo, ImGui::GetStyle().Colors[ImGuiCol_Text]);
                  ImGui::SetNextItemWidth(em * 9.0f);
                  ed->OutputFilter.Draw("##outfilter");
                  ImGui::SetItemTooltip("Filter the stream");
                  ImGui::SameLine();
                  temp_data->ClearLog = ImGui::SmallButton("Clear");
                  ImGui::SetItemTooltip("Clear the document log (issues re-derive from the graph)");
                }
                ImGui::Separator();

                if (ImGui::BeginChild("##outstream", ImVec2(-FLT_MIN, -FLT_MIN)))
                {
                  ImGuiAppEditorState* ed = ImGui::AppGraphEditorState(&doc->Graph);
                  if (data->Issues.Size == 0 && ed->OutputShowErr && ed->OutputShowWarn)
                    ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), ICON_FA_CHECK "  No configuration problems.");
                  for (int i = 0; i < data->Issues.Size; i++)
                  {
                    const ImGui::ImGuiAppGraphIssue& it = data->Issues.Data[i];
                    if ((it.Severity >= 2 && !ed->OutputShowErr) || (it.Severity < 2 && !ed->OutputShowWarn) || !ed->OutputFilter.PassFilter(it.Text))
                      continue;
                    const ImVec4 col = (it.Severity >= 2) ? ImVec4(0.92f, 0.45f, 0.45f, 1.0f) : ImVec4(0.92f, 0.80f, 0.40f, 1.0f);
                    ImGui::PushID(i);
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    char row[288];
                    ImFormatString(row, IM_ARRAYSIZE(row), "%s%s", (it.Severity >= 2) ? "[x] " : "[!] ", it.Text);
                    if (ImGui::Selectable(row) && it.NodeId >= 0)
                    {
                      selection = it.NodeId;
                    }
                    if (it.NodeId >= 0 && ImGui::IsItemHovered())
                      ImGui::AppGraphHoverNode(&doc->Graph, it.NodeId, ImGui::ImGuiAppHoverSource_External);
                    ImGui::PopStyleColor();
                    ImGui::PopID();
                  }
                  if (doc->Log.Size == 0)
                    ImGui::TextDisabled("(log empty -- actions, file IO and refused links land here)");
                  for (int i = doc->Log.Size - 1; i >= 0; i--)   // newest first
                  {
                    const GraphDocData::DocLogLine& ln = doc->Log.Data[i];
                    if ((ln.Severity >= 2 && !ed->OutputShowErr) || (ln.Severity == 1 && !ed->OutputShowWarn) || (ln.Severity == 0 && !ed->OutputShowInfo)
                        || !ed->OutputFilter.PassFilter(ln.Text))
                      continue;
                    const ImVec4 lcol = ln.Severity >= 2 ? ImVec4(0.92f, 0.45f, 0.45f, 1.0f)
                                      : ln.Severity == 1 ? ImVec4(0.92f, 0.80f, 0.40f, 1.0f)
                                                         : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
                    ImGui::TextColored(lcol, "%s", ln.Text);
                  }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
              }
              ImGui::EndTabBar();
            }
          }
          ImGui::EndChild();
        }
      }
      ImGui::EndChild();

      temp_data->InspGripActivated = false;
      temp_data->BodyMaxX          = 0.0f;
      if (insp_open)
      {
        // Inspector splitter: record raw input; the drag resolves in OnUpdate.
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::InvisibleButton("##isplit", ImVec2(insp_grip, body.y));
        temp_data->InspGripActivated = ImGui::IsItemActivated();
        temp_data->BodyMaxX          = ImGui::GetItemRectMax().x + insp_w;
        if (data->InspDragging || ImGui::IsItemHovered())
        {
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        ImGui::SameLine(0.0f, 0.0f);

        if (ImGui::BeginChild("##Inspector", ImVec2(insp_w, body.y), ImGuiChildFlags_Borders))
        {
          ImGui::AlignTextToFramePadding();
          ImGui::TextUnformatted(ICON_FA_CIRCLE_INFO "  Inspector");
          ImGui::Separator();
          if (selection < 0)
          {
            ShowComposerProjectInspector(doc, graph, temp_data);
          }
          else if (graph->Selection.Size > 1)
          {
            ImGui::EditAppNodesInspectorMulti(graph);
          }
          else
          {
            ImGui::EditAppNodeInspectorEx(graph, selection, doc->Mirror);
            // Preview mocks a DESIGN control's UI from its drafted fields. A live node's reality is
            // already on screen (and its values are the Data/Temp (live) sections above).
            const ImGuiAppNode* seln = ImGui::AppGraphFindNode(graph, selection);
            if (seln != nullptr && !seln->IsLive)
              if (ImGui::AppInspectorSection("##sec_preview", ICON_FA_PLAY, "Preview", nullptr, nullptr))
                ImGui::AppGraphRenderMockPanel(graph, selection, doc->Mirror);
            if (data->HasNodeCode)
            {
              if (ImGui::AppInspectorSection("##sec_code", ICON_FA_CODE, "Generated C++", nullptr, nullptr))
              {
                ImGui::TextDisabled("%s", data->CodeName);
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy"))
                {
                  ImGui::SetClipboardText(data->CodeNodeText.c_str());
                }
                ShowGeneratedCodeView(&doc->Graph, "##inspcode", data->CodeNodeText, data->NodeLines, nullptr, nullptr);
              }
            }
          }
        }
        ImGui::EndChild();
      }

      // The tree/canvas widgets edited the local copy; OnUpdate applies it.
      temp_data->SelectionChanged = (selection != doc->Selection);
      temp_data->Selection = selection;
    }
  };

  //-----------------------------------------------------------------------------
  // [SECTION] Composer host window + demo menu
  //-----------------------------------------------------------------------------

  struct ComposerWindow : ImGuiAppWindow<ComposerWindow>
  {
    ComposerWindow()
    {
      // Fixed label (not type-derived) so the saved .ini dock binding still matches.
      ImStrncpy(this->Label, "ImGuiAppComposer", sizeof(this->Label));
      // The composition fills the window exactly; a window scrollbar is always a layout bug.
      this->Flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    }
    virtual void OnRender(const ImGuiApp*) const override final {}
  };

  // Toggle values + "applied" bookkeeping live in PersistData; OnRender records MenuItem results into
  // TempData; OnUpdate writes them back.
  struct DemoMenuData
  {
    bool ShowBaseWindow;
    bool ShowStatusBar;
    bool ShowRandomTime;
    bool ShowBreathing;
    bool ShowMetrics;
    bool AppliedBaseWindow;
    bool AppliedStatusBar;
    bool AppliedRandomTime;
    bool AppliedBreathing;
  };
  struct DemoMenuTempData
  {
    bool Rendered; // false until OnRender ran once (the first OnUpdate must not consume zeroed toggles)
    bool ShowBaseWindow;
    bool ShowStatusBar;
    bool ShowRandomTime;
    bool ShowBreathing;
    bool ShowMetrics;
  };
  struct DemoMenuControl : ImGuiAppControl<DemoMenuData, DemoMenuTempData>
  {
    virtual void OnInitialize(ImGuiApp* app, DemoMenuData* data) const override final
    {
      IM_UNUSED(app);
      data->ShowRandomTime = true;
      data->ShowBreathing  = true;
      data->ShowMetrics    = true;
    }

    virtual void OnUpdate(float dt, DemoMenuData* data, const DemoMenuTempData* temp_data, const DemoMenuTempData* last_temp_data) const override final
    {
      IM_UNUSED(dt);
      if (!temp_data->Rendered)   // first update precedes any render: zeroed temp must not wipe the defaults
        return;
      data->ShowBaseWindow = temp_data->ShowBaseWindow;
      data->ShowStatusBar  = temp_data->ShowStatusBar;
      data->ShowRandomTime = temp_data->ShowRandomTime;
      data->ShowBreathing  = temp_data->ShowBreathing;
      data->ShowMetrics    = temp_data->ShowMetrics;
    }

    virtual void OnRender(const DemoMenuData* data, DemoMenuTempData* temp_data) const override final
    {
      temp_data->Rendered = true;
      bool show_base    = data->ShowBaseWindow;
      bool show_status  = data->ShowStatusBar;
      bool show_random  = data->ShowRandomTime;
      bool show_breathe = data->ShowBreathing;
      bool show_metrics = data->ShowMetrics;
      if (ImGui::BeginMenuBar())
      {
        if (ImGui::BeginMenu("Examples"))
        {
          ImGui::MenuItem("Base window",          nullptr, &show_base);
          ImGui::MenuItem("Status bar",           nullptr, &show_status);
          ImGui::MenuItem("Random Time control",  nullptr, &show_random);
          ImGui::MenuItem("Breathing control",    nullptr, &show_breathe);
          ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools"))
        {
          ImGui::MenuItem("Composer", nullptr, &show_metrics);
          ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
      }

      ImGui::Text("ImGuiAppLayer says hello! (%s) (%d)", IMGUI_APPLAYER_VERSION, IMGUI_APPLAYER_VERSION_NUM);
      ImGui::Spacing();
      ImGui::TextWrapped("%s", "Enable examples from the Examples menu to push/pop them on a live "
          "ImGuiApp. The Breathing control breathes while hovered for a duration taken from the "
          "Random Time control when it is active (its source), or a default otherwise.");

      if (ImGui::CollapsingHeader("ImGuiApp Status", ImGuiTreeNodeFlags_DefaultOpen))
      {
        ImGui::TextDisabled("See Tools > Composer -> status strip for composition, lifecycle and FPS.");
      }

      temp_data->ShowBaseWindow = show_base;
      temp_data->ShowStatusBar  = show_status;
      temp_data->ShowRandomTime = show_random;
      temp_data->ShowBreathing  = show_breathe;
      temp_data->ShowMetrics    = show_metrics;
    }
  };

  static DemoMenuData* GetDemoMenu(ImGuiApp* app)
  {
    return static_cast<DemoMenuData*>(app->Data.GetVoidPtr(ImGuiType<DemoMenuData>::ID));
  }

  struct DemoPanelWindow : ImGuiAppWindow<DemoPanelWindow>
  {
    DemoPanelWindow() { ImStrncpy(this->Label, "ImGuiAppLayer Demo", sizeof(this->Label)); this->Flags = ImGuiWindowFlags_MenuBar; }
    virtual void OnRender(const ImGuiApp*) const override final {}
  };
}

namespace ImGui
{
  IMGUI_API void SetAppCodeFont(ImGuiAppGraph* g, ImFont* font) { AppGraphEditorState(g)->CodeFont = font; }

  IMGUI_API ImGuiAppGraph* AppLayerDemoGraph(ImGuiApp* host)
  {
    if (host == nullptr)
      return nullptr;
    // Instance data is keyed by data type id in app->Data regardless of which window hosts
    // the control (GraphDocControl is Composer-window-hosted, never in host->Controls).
    GraphDocData* doc = (GraphDocData*)host->Data.GetVoidPtr(ImGuiType<GraphDocData>::ID);
    if (doc == nullptr)
      return nullptr;
    return &doc->Graph;
  }

  // App-time transport frame count (F29): how many state snapshots the running composer has recorded
  // (0 when no transport / not recording). Exposed for the headless scrub test.
  IMGUI_API int AppComposerAppTimeFrames(ImGuiApp* host)
  {
    if (host == nullptr)
      return 0;
    GraphDocData* doc = (GraphDocData*)host->Data.GetVoidPtr(ImGuiType<GraphDocData>::ID);
    if (doc == nullptr || doc->Transport == nullptr)
      return 0;
    return doc->Transport->History.Count;
  }

  // Composer outliner column width (F32): >0 when shown, 0 when hidden. Exposed for the status-zone test.
  IMGUI_API float AppComposerOutlinerWidth(ImGuiApp* host)
  {
    if (host == nullptr)
      return 0.0f;
    GraphDocData* doc = (GraphDocData*)host->Data.GetVoidPtr(ImGuiType<GraphDocData>::ID);
    return doc != nullptr ? doc->TreeW : 0.0f;
  }

  //-----------------------------------------------------------------------------
  // [SECTION] Demo bring-up (ShowAppLayerDemo: ONE application)
  //-----------------------------------------------------------------------------
  // The demo composes its chrome AND its examples INTO the running app -- the same object model
  // the live mirror reflects, so Live shows everything that exists. Called from a late layer's
  // OnRender: windows, sidebars and controls are safe to push/pop here (their phase iterations
  // for this frame already ran; changes take effect next frame). Layers are NOT safe (the layer
  // vector is being iterated right now) -- the demo never pushes one; the host's foundation from
  // InitializeApp is the one layer stack.

  IMGUI_API void ShowAppLayerDemo(bool* p_open, ImGuiApp* host)
  {
      // A caller without an ImGuiApp (plain imgui contexts: samples, tests) gets a demo-owned
      // fallback, which is then the process's one app; the demo drives its frame below.
      static ImGuiApp s_fallback_app;
      static bool s_fallback_ready = false;
      ImGuiApp* app = host;
      if (app == nullptr)
      {
        if (!s_fallback_ready)
        {
          InitializeApp(&s_fallback_app);
          s_fallback_ready = true;
        }
        app = &s_fallback_app;
      }

      // Chrome composition, once. Examples are pushed/popped AFTER the chrome, so they are always
      // the tail of their vectors and the toggle rebuild below can pop them back off.
      static ImGuiApp* s_composed = nullptr;
      IM_ASSERT(s_composed == nullptr || s_composed == app);   // one application per process
      if (s_composed != app)
      {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        // The Composer is ALWAYS the first window pushed: first in app->Windows, first to Begin.
        PushAppWindow<ComposerWindow>(app);
        ImGuiAppWindowBase* metrics = app->Windows.back();
        metrics->HasInitialPlacement = true;
        metrics->InitialSize = ImVec2(vp->WorkSize.x * 0.66f, vp->WorkSize.y * 0.66f);
        metrics->InitialPos  = vp->WorkPos + ImVec2(vp->WorkSize.x * 0.10f, vp->WorkSize.y * 0.10f);
        PushWindowControl<GraphDocControl>(app, metrics);   // producer: owns the doc (push first)
        PushWindowControl<ToolbarControl>(app, metrics);    // consumers depend on GraphDocData
        PushWindowControl<EditorBodyControl>(app, metrics);
        PushWindowControl<StatusStripControl>(app, metrics);   // status bar renders LAST -> window bottom

        PushAppWindow<DemoPanelWindow>(app);
        ImGuiAppWindowBase* panel = app->Windows.back();
        panel->HasInitialPlacement = true;
        panel->InitialSize = ImVec2(vp->WorkSize.x * 0.30f, vp->WorkSize.y * 0.40f);
        panel->InitialPos  = vp->WorkPos + ImVec2(vp->WorkSize.x * 0.02f, vp->WorkSize.y * 0.04f);
        PushWindowControl<DemoMenuControl>(app, panel);
        s_composed = app;
      }

      // Chrome windows, by label (the vector reallocs as examples push/pop).
      ImGuiAppWindowBase* panel = nullptr;
      ImGuiAppWindowBase* metrics = nullptr;
      for (int i = 0; i < app->Windows.Size; i++)
      {
        if (strcmp(app->Windows.Data[i]->Label, "ImGuiAppLayer Demo") == 0)
          panel = app->Windows.Data[i];
        else if (strcmp(app->Windows.Data[i]->Label, "ImGuiAppComposer") == 0)
          metrics = app->Windows.Data[i];
      }
      DemoMenuData* st = GetDemoMenu(app);
      IM_ASSERT(panel != nullptr && metrics != nullptr && st != nullptr);

      // This call sits AFTER this frame's window render: consume the X buttons first, then impose
      // the external/menu flags for the next frame. Without a host flag the panel's Open is its own
      // (X / outliner eye hide it; the eye shows it again).
      if (p_open != nullptr)
      {
        if (!panel->Open)   // panel X closes the whole demo
          *p_open = false;
        panel->Open = *p_open;
      }
      if (!metrics->Open)                      // Composer X syncs the menu toggle
        st->ShowMetrics = false;
      metrics->Open = st->ShowMetrics;

      // Example push/pop on toggle change: pop what was applied (reverse push order -- examples
      // are the tail), re-push what is enabled. Composition changes land in the WAL as usual.
      if (st->AppliedBaseWindow != st->ShowBaseWindow ||
          st->AppliedStatusBar  != st->ShowStatusBar  ||
          st->AppliedRandomTime != st->ShowRandomTime ||
          st->AppliedBreathing  != st->ShowBreathing)
      {
        if (st->AppliedBreathing || st->AppliedRandomTime)
          PopAppWindow(app);   // the sample window pops its hosted controls with it
        if (st->AppliedStatusBar)
          PopAppSidebar(app);
        if (st->AppliedBaseWindow)
          PopAppWindow(app);

        ImGuiViewport* vp = ImGui::GetMainViewport();
        const float em = ImGui::GetFontSize();

        if (st->ShowBaseWindow)
        {
          PushAppWindow<BaseWindow>(app);
          ImGuiAppWindowBase* w = app->Windows.back();
          w->HasInitialPlacement = true;
          w->InitialSize = ImVec2(em * 16.0f, em * 8.0f);
          w->InitialPos  = ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + em * 2.0f);
          PushWindowControl<BaseInfoControl>(app, w);
        }
        if (st->ShowStatusBar)
        {
          PushAppSidebar<StatusBar>(app, vp, ImGuiDir_Down, 0.0f, ImGuiWindowFlags_AlwaysAutoResize);
        }
        if (st->ShowRandomTime || st->ShowBreathing)
        {
          PushAppWindow<SampleWindow>(app);
          ImGuiAppWindowBase* w = app->Windows.back();
          w->HasInitialPlacement = true;
          w->InitialSize = ImVec2(em * 18.0f, 0.0f);
          w->InitialPos  = ImVec2(vp->WorkPos.x + em * 2.0f, vp->WorkPos.y + vp->WorkSize.y * 0.55f);
          if (st->ShowRandomTime)
            PushWindowControl<RandomTimeControlDemo>(app, w);
          if (st->ShowBreathing)
          {
            // Soft-wired to the Random Time example: reads its roll when it is on, falls back
            // to the default otherwise (Random Time pushes first when both are enabled).
            const ImGuiAppDataBinding binds[] = { { ImGuiType<RandomTimeData>::ID, 0, true } };
            PushWindowControl<BreathingControlDemo>(app, w, 0, binds, IM_ARRAYSIZE(binds));
          }
        }

        st->AppliedBaseWindow = st->ShowBaseWindow;
        st->AppliedStatusBar  = st->ShowStatusBar;
        st->AppliedRandomTime = st->ShowRandomTime;
        st->AppliedBreathing  = st->ShowBreathing;
      }

      // Hosted mode ends here -- the host runs its own frame. Fallback mode: the demo owns it.
      if (app == &s_fallback_app)
      {
        UpdateApp(app);
        RenderApp(app);
        if (app->ShutdownPending)
        {
          ShutdownApp(app);
          s_fallback_ready = false;
          s_composed = nullptr;
        }
      }
  }
}
