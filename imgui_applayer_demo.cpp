// ImGuiAppLayer demo. Mirrors Dear ImGui's split of imgui.cpp / imgui_demo.cpp and its demo
// window layout: a single "ImGuiAppLayer Demo" window with collapsing-header sections that drive
// a live ImGuiApp, pushing/popping the real layers, windows, sidebars and controls so each
// framework feature (incl. the Breathing control and its Random Time dependency) is showcased.

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_applayer.h"
#include "imgui_applayer_nodes.h"
#include "imgui_internal.h"
#include "imnodes.h"
#include "IconsFontAwesome6.h"

#include <ctime>
#include <cstdlib>
#include <cstdio>
#include <cstring>                         // strncmp (codegen-warning scan)

namespace
{
  // Monospace font for the generated-code inspector (set via ImGui::SetAppCodeFont). Null -> UI font.
  static ImFont* g_AppCodeFont = nullptr;

  // Encourage "pure" design, such that a control is "agnostic to the data passing through it."
  static void RenderTextT(const char* text, ImVec2 text_size, ImVec2 pos, ImVec2 avail, float t_value)
  {
    // Draw directly into the (clipped) window draw list: this is a purely visual centered effect,
    // so it must not move the layout cursor (SetCursorScreenPos past the content max trips
    // imgui's "using SetCursorPos to extend boundaries" assert).
    float offset = avail.x * 0.5f + (t_value * 0.5f * (avail.x - text_size.x));
    ImVec2 text_pos = pos + ImVec2(offset, 0.0f) + (text_size * -0.5f);
    ImGui::GetWindowDrawList()->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), text);
  }

  struct RandomTimeData
  {
    char label[128];
    char type[128];
    float max_timer_secs;
    ImU64 rng;                 // deterministic effect source: randomness kept IN the state (see ImAppRandom).
                               // Seeded once from the clock in OnInitialize (an init-time effect becomes
                               // state), then only ever stepped by OnUpdate -- so snapshots capture it and
                               // record/replay reproduces every "random" roll exactly. rand()/srand() would
                               // be a hidden effect that breaks the replay theorem (contract 9 detects it).
  };

  struct RandomTimeTempData
  {
    bool generate;
  };

  struct RandomTimeControlDemo : ImGuiAppControl <RandomTimeData, RandomTimeTempData>
  {
    // Random time between 1 and 30 seconds, drawn from the control's own seed.
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
      IM_UNUSED(data);
      IM_UNUSED(temp_data);

      const ImGuiViewport* vp = ImGui::GetMainViewport();
      const float em = ImGui::GetFontSize();
      ImGui::SetNextWindowSize(ImVec2(em * 14.0f, 0.0f), ImGuiCond_FirstUseEver);
      ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + em * 2.0f, vp->WorkPos.y + vp->WorkSize.y * 0.55f), ImGuiCond_FirstUseEver);

      if (ImGui::Begin(data->label))
      {
        ImGui::Text("%s", "Max Timer Seconds");

        temp_data->generate = ImGui::Button("Generate");
        ImGui::SameLine();

        ImGui::Text("%.1f", data->max_timer_secs);
      }
      ImGui::End();
    }
  };

  struct BreathingControlData
  {
    char label[128];
    char type[128];
    char text[128];
    char timer_text[128];
    const ImGuiApp* app;       // used to read the optional Random Time "source" control
    float timer_secs;
    float t_value;
    float t_direction;
    ImVec4 col;
  };

  struct BreathingControlTempData
  {
    bool hovered;
  };

  struct BreathingControlDemo : ImGuiAppControl<BreathingControlData, BreathingControlTempData>
  {
    // Used when the Random Time "source" control is not active.
    static constexpr float DefaultMaxTimerSecs = 5.0f;

    // Read the Random Time source control's value if that control is active, else the default.
    float SourceMaxTimerSecs(const BreathingControlData* data) const
    {
      if (data->app != nullptr)
        if (const RandomTimeData* src = static_cast<const RandomTimeData*>(data->app->Data.GetVoidPtr(ImGuiType<RandomTimeData>::ID)))
          return src->max_timer_secs;
      return DefaultMaxTimerSecs;
    }

    virtual void OnInitialize(ImGuiApp* app, BreathingControlData* data) const override final
    {
      std::string_view sv;

      data->app = app;

      sv = ImGuiType<decltype(this)>::Name;

      ImStrncpy(data->type, sv.data(), sv.length() + 1); // +1: ImStrncpy copies count-1 chars
      ImFormatString(data->label, sizeof(data->label), "%s", data->type);
    }

    virtual void OnUpdate(float dt, BreathingControlData* data, const BreathingControlTempData* temp_data, const BreathingControlTempData* last_temp_data) const override final
    {
      data->timer_secs = ImMax(0.0f, data->timer_secs - dt);

      if (temp_data->hovered ^ last_temp_data->hovered)
      {
        // On hover, sample the Random Time source (or the default when that control is inactive).
        data->timer_secs = temp_data->hovered * SourceMaxTimerSecs(data);
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

    virtual void OnRender(const BreathingControlData* data, BreathingControlTempData* temp_data) const override final
    {
      const ImGuiViewport* vp = ImGui::GetMainViewport();
      const float em = ImGui::GetFontSize();
      ImGui::SetNextWindowSize(ImVec2(em * 18.0f, em * 9.0f), ImGuiCond_FirstUseEver);
      ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + em * 18.0f, vp->WorkPos.y + vp->WorkSize.y * 0.55f), ImGuiCond_FirstUseEver);

      if (ImGui::Begin(data->label))
      {
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
      ImGui::End();
    }
  };

  struct BaseWindow : ImGuiAppWindow<BaseWindow>
  {
    virtual void OnRender(const ImGuiApp*) const override final
    {
      ImGui::TextWrapped("%s", "This is a base window managed by ImGuiAppWindowLayer.");
    }
  };

  // A window-hosted control: renders INSIDE its host window (child content, no Begin/End). Hosted on BaseWindow,
  // it exercises the live mirror's hosted-control + containment edge (control -> BaseWindow) in the editor.
  struct BaseInfoData
  {
    int Frames;   // alive-frame counter, advanced in OnUpdate
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

  // Seed a fresh graph. The graph IS the app: the collective of layer/window/control nodes composes it,
  // so there is no "App" container node. Seed one window + one control to show the two pillars at once.
  void SeedAppGraph(ImGuiAppGraph* graph)
  {
    // The authored foundation is guaranteed here: the four layers are the frame's phases and anchor the canvas
    // root whether or not a live mirror exists. The mirror upserts ONTO them (BuildAppLiveGraph skips live
    // layers that have a design twin), so there are never design/live duplicates of a phase.
    ImGui::AppGraphEnsureFoundation(graph);

    // Explicit positions clear to the RIGHT of the layer master column + its pipeline group box (the column
    // packs at x ~110..630 and the box adds padding), so the default window + control never occlude the layer
    // stack. Set HasGridPos so it sticks.
    ImGuiAppNode* win  = ImGui::AppGraphAddNode(graph, ImGuiAppNodeKind_Window,  "MainWindow");
    win->GridPos = ImVec2(760.0f, 96.0f);
    win->HasGridPos = true;
    win->_NeedsPlace = true;

    ImGuiAppNode* ctrl = ImGui::AppGraphAddNode(graph, ImGuiAppNodeKind_Control, "NewControl");
    ctrl->GridPos = ImVec2(760.0f, 320.0f);
    ctrl->HasGridPos = true;
    ctrl->_NeedsPlace = true;
  }

  // "+ Add node" palette: layers, windows, sidebars, controls -- the pieces that compose the app. Builtin
  // controls are backed by the demo's compiled types (RandomTime / Breathing) so their real data types
  // become wireable graph nodes.
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
    // The core phases are guaranteed by the foundation; the only authorable layer is a Custom subclass.
    if (ImGui::MenuItem("Custom Layer"))
    {
      ImGuiAppNode* n = ImGui::AppGraphAddNode(graph, ImGuiAppNodeKind_Layer, "CustomLayer");
      n->LayerType = ImGuiAppLayerType_Custom;
    }
    if (ImGui::MenuItem("Struct"))  ImGui::AppGraphAddNode(graph, ImGuiAppNodeKind_Struct,  "NewStruct");
    if (ImGui::MenuItem("Window"))  ImGui::AppGraphAddNode(graph, ImGuiAppNodeKind_Window,  "Window");
    if (ImGui::MenuItem("Sidebar")) ImGui::AppGraphAddNode(graph, ImGuiAppNodeKind_Sidebar, "Sidebar");
  }

  //---------------------------------------------------------------------------------------------------
  // Dogfooded Composer node editor: the editor is itself an ImGuiApp object model.
  //
  // The graph IS the document. ONE control (GraphDocControl) owns it as PersistData and is the single
  // writer; the panels are controls that receive `const GraphDocData*` as a typed dependency (read), and
  // the interactive panels mutate the doc through that pointer (a localized, deliberate escape from the
  // framework's read-only dependency rule -- the interim until an edit-intent bus lands). TempData is empty
  // on every panel: actions apply immediately, nothing needs to survive to next frame.
  //
  // No ImGuiApp* is passed to OnUpdate/OnRender, so the doc stashes the app it MIRRORS (the demo's example
  // app) in PersistData -- the Breathing-control convention (imgui_applayer_demo.cpp data->app).
  //---------------------------------------------------------------------------------------------------

  // PersistData is value-initialized by PushAppControl<>, then seeded in GraphDocControl::OnInitialize -- no
  // member initializers here (defaults belong to OnInitialize, not the declaration).
  struct GraphDocData
  {
    ImGuiAppGraph   Graph;
    int             Selection;        // node selection shared by tree + canvas
    bool            ShowLive;         // show vs hide (never delete) live-mirror nodes
    float           TreeW;            // outliner width (0 -> default on first use)
    float           CodeH;            // code-inspector height, bottom split under the canvas (0 == collapsed)
    char            WriteMsg[64];     // transient "wrote header" confirmation
    ImGuiID         WrittenSig;       // AppGraphSignature at the last header write (0 = never) -> Generate state
    char            GraphPath[256];
    char            HeaderPath[256];
    ImGuiApp*       Mirror;           // app reflected into the live graph (set after push; see ShowAppLayerDemo)
    ImGuiAppStateHistory MirrorHistory;   // the mirrored app's recorded state ring (time travel)
    bool            TimeScrub;        // true: freeze the mirror at TimeScrubIndex instead of recording
    int             TimeScrubIndex;
  };
  struct GraphDocTempData {};

  // Mutable fetch of the shared doc from an app's storage (PersistData aliases the start of InstanceData).
  static GraphDocData* GetGraphDoc(ImGuiApp* app)
  {
    return static_cast<GraphDocData*>(app->Data.GetVoidPtr(ImGuiType<GraphDocData>::ID));
  }

  struct GraphDocControl : ImGuiAppControl<GraphDocData, GraphDocTempData>
  {
    virtual void OnInitialize(ImGuiApp* app, GraphDocData* data) const override final
    {
      IM_UNUSED(app);
      data->Selection   = -1;
      data->ShowLive    = true;
      data->TreeW       = 0.0f;          // 0 -> EditorBody picks a default on first layout
      data->CodeH       = 0.0f;          // collapsed
      data->WriteMsg[0] = 0;
      data->WrittenSig  = 0;
      data->Mirror      = nullptr;       // set after push by ShowAppLayerDemo
      data->TimeScrub   = false;
      data->TimeScrubIndex = 0;
      ImStrncpy(data->GraphPath,  "imguix_node_graph.txt",      sizeof(data->GraphPath));
      ImStrncpy(data->HeaderPath, "imguix_generated_control.h", sizeof(data->HeaderPath));
      if (data->Graph.Nodes.empty())
      {
        SeedAppGraph(&data->Graph);
      }
    }
    virtual void OnUpdate(float dt, GraphDocData* data, const GraphDocTempData*, const GraphDocTempData*) const override final
    {
      IM_UNUSED(dt);
      // Reconcile-before-report: build the live mirror first so every panel reads the reconciled graph this frame.
      if (data->Mirror != nullptr)
      {
        ImGui::BuildAppLiveGraph(data->Mirror, &data->Graph);
      }

      // Time travel over the mirrored app. While scrubbing, re-impose the chosen snapshot every frame (the
      // mirror advances exactly one frame from it after we return -- a stable freeze at that moment);
      // otherwise record this frame. Nothing here is example-specific: the state discipline (all durable
      // state in registered storage, OnUpdate the sole mutator) makes ANY framework app scrubbable.
      if (data->Mirror != nullptr && data->Mirror->Layers.Size > 0)   // composed (IsInitialized == platform-only)
      {
        if (data->TimeScrub)
        {
          if (!ImGui::AppStateRestore(data->Mirror, &data->MirrorHistory, data->TimeScrubIndex))
            data->TimeScrub = false;   // composition changed under us -> timeline no longer applies
        }
        else
        {
          ImGui::AppStateSnapshot(data->Mirror, &data->MirrorHistory);
        }
      }
    }
  };

  static void EditorToolSep(float em)
  {
    ImGui::SameLine(0.0f, em * 0.6f);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0.0f, em * 0.6f);
  }

  // Each panel caches the shared doc's true (non-const) pointer from app storage in OnInitialize -- the
  // BreathingControlDemo::data->app convention. No control receives the doc as a const dependency to cast away.
  struct ToolbarData
  {
    GraphDocData* Doc;
  };
  // The button bar's discrete actions are captured here in OnRender and applied in OnUpdate -- TempData IS the
  // edit-intent bus (so file IO and geometry changes leave the render path entirely).
  struct ToolbarTempData
  {
    bool Save;
    bool Load;
    bool WriteHeader;
    bool ToggleCode;
    bool ToggleLive;    // Live-eye toggle clicked this frame (OnUpdate derives the new state)
    bool Undo;          // undo / redo edit-intents (applied in OnUpdate)
    bool Redo;
    bool OpenProblems;  // problems chip clicked -> reveal the code/problems panel
    bool Diff;          // diff current graph's codegen vs the saved-on-disk graph -> clipboard
  };
  struct ToolbarControl : ImGuiAppControl<ToolbarData, ToolbarTempData>
  {
    virtual void OnInitialize(ImGuiApp* app, ToolbarData* data) const override final
    {
      data->Doc = GetGraphDoc(app);
    }

    virtual void OnUpdate(float dt, ToolbarData* data, const ToolbarTempData* temp_data, const ToolbarTempData* last_temp_data) const override final
    {
      IM_UNUSED(dt);
      GraphDocData* doc = data->Doc;
      if (temp_data->Save)
      {
        ImGui::SaveAppGraph(doc->GraphPath, &doc->Graph);
      }
      if (temp_data->Load)
      {
        ImGui::LoadAppGraph(doc->GraphPath, &doc->Graph);
        ImGui::AppGraphEnsureFoundation(&doc->Graph);   // the frame's phases anchor the root, always
      }
      if (temp_data->WriteHeader)
      {
        ImGuiTextBuffer full;
        ImGui::GenerateAppGraphCode(&doc->Graph, &full);
        if (ImFileHandle fh = ImFileOpen(doc->HeaderPath, "wt"))
        {
          ImFileWrite(full.c_str(), sizeof(char), (ImU64)full.size(), fh);
          ImFileClose(fh);
          ImFormatString(doc->WriteMsg, IM_ARRAYSIZE(doc->WriteMsg), "wrote %s", doc->HeaderPath);
          doc->WrittenSig = ImGui::AppGraphSignature(&doc->Graph);   // Generate button reads fresh vs stale off this
        }
      }
      if (temp_data->OpenProblems && doc->CodeH <= 0.0f)
      {
        doc->CodeH = ImGui::GetFontSize() * 12.0f;   // reveal the panel that hosts the Problems tab
      }
      if (temp_data->ToggleCode)
      {
        doc->CodeH = (doc->CodeH > 0.0f) ? 0.0f : ImGui::GetFontSize() * 12.0f;
      }
      if (temp_data->ToggleLive)
      {
        doc->ShowLive = !doc->ShowLive;
      }
      if (temp_data->Undo)
      {
        ImGui::AppGraphUndo(&doc->Graph);
      }
      if (temp_data->Redo)
      {
        ImGui::AppGraphRedo(&doc->Graph);
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
        }
        else
        {
          ImFormatString(doc->WriteMsg, IM_ARRAYSIZE(doc->WriteMsg), "no saved graph to diff (Save first)");
        }
      }
    }

    virtual void OnRender(const ToolbarData* data, ToolbarTempData* temp_data) const override final
    {
      GraphDocData*     doc       = data->Doc;
      const float       em        = ImGui::GetFontSize();
      const ImGuiStyle& style     = ImGui::GetStyle();
      const bool        code_open = doc->CodeH > 0.0f;
      bool              show_live = doc->ShowLive;
      // Document toolbar, game-editor grammar: the PRIMARY action leads and carries the document's health
      // as its state (UE's Compile button); file verbs next; edit history after; view/panel toggles and run
      // controls right-aligned (Unity's toolbar). View verbs (add/frame/tidy/snap/overlays) live on the
      // viewport's gizmo cluster, not here.
      if (ImGui::BeginChild("##Toolbar", ImVec2(0.0f, 0.0f), ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
      {
        // -- Generate: green check = header on disk matches the graph; amber = model changed since the last
        //    write; red = validation errors (writing stays allowed; the ambient marks say where to look).
        const ImVector<ImGui::ImGuiAppGraphIssue>* issues = ImGui::AppGraphIssuesCached(&doc->Graph);
        int nerr = 0, nwarn = 0;
        for (int i = 0; i < issues->Size; i++)
          (issues->Data[i].Severity >= 2 ? nerr : nwarn)++;
        const bool  fresh     = doc->WrittenSig != 0 && doc->WrittenSig == ImGui::AppGraphSignature(&doc->Graph);
        const char* gen_label = nerr > 0 ? ICON_FA_TRIANGLE_EXCLAMATION "  Generate" : fresh ? ICON_FA_CHECK "  Generated" : ICON_FA_FILE_EXPORT "  Generate";
        ImGui::PushStyleColor(ImGuiCol_Button, nerr > 0 ? ImVec4(0.55f, 0.21f, 0.18f, 1.0f)
                                             : fresh    ? ImVec4(0.16f, 0.38f, 0.22f, 1.0f)
                                                        : ImVec4(0.52f, 0.39f, 0.14f, 1.0f));
        temp_data->WriteHeader = ImGui::Button(gen_label);
        ImGui::PopStyleColor();
        if (nerr > 0)
          ImGui::SetItemTooltip("%d error(s) in the graph -- writes anyway; see the Problems chip", nerr);
        else if (fresh)
          ImGui::SetItemTooltip("%s matches the graph -- click to rewrite", doc->HeaderPath);
        else
          ImGui::SetItemTooltip("Graph changed -- write whole-graph C++ -> %s", doc->HeaderPath);

        EditorToolSep(em);
        temp_data->Save = ImGui::Button(ICON_FA_FLOPPY_DISK "  Save");
        ImGui::SetItemTooltip("Save graph -> %s", doc->GraphPath);
        ImGui::SameLine();
        temp_data->Load = ImGui::Button(ICON_FA_FOLDER_OPEN "  Load");
        ImGui::SetItemTooltip("Load graph <- %s", doc->GraphPath);
        ImGui::SameLine();
        temp_data->Diff = ImGui::Button(ICON_FA_CODE_COMPARE "  Diff");
        ImGui::SetItemTooltip("Diff generated C++ vs the saved graph -> clipboard");

        EditorToolSep(em);
        ImGui::BeginDisabled(!ImGui::AppGraphCanUndo(&doc->Graph));
        temp_data->Undo = ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT "##undo");
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Undo (Ctrl+Z)");
        ImGui::SameLine();
        ImGui::BeginDisabled(!ImGui::AppGraphCanRedo(&doc->Graph));
        temp_data->Redo = ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##redo");
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Redo (Ctrl+Y)");

        // -- Right cluster: problems chip + panel toggles. Width measured so it hugs the edge. Run controls
        //    (App time) live on the viewport's transport overlay, not here.
        char prob_lbl[48];
        ImFormatString(prob_lbl, IM_ARRAYSIZE(prob_lbl), "%s %d##problems", (nerr + nwarn) > 0 ? ICON_FA_TRIANGLE_EXCLAMATION : ICON_FA_CHECK, nerr + nwarn);
        const char* code_lbl = ICON_FA_CODE "  Code";
        const char* live_lbl = show_live ? ICON_FA_EYE "  Live###live" : ICON_FA_EYE_SLASH "  Live###live";
        const float pad2 = style.FramePadding.x * 2.0f;
        const float cluster_w = ImGui::CalcTextSize(prob_lbl, ImGui::FindRenderedTextEnd(prob_lbl)).x + pad2
                              + ImGui::CalcTextSize(code_lbl).x + pad2 + style.ItemSpacing.x
                              + ImGui::CalcTextSize(live_lbl, ImGui::FindRenderedTextEnd(live_lbl)).x + pad2 + style.ItemSpacing.x;
        ImGui::SameLine(ImMax(ImGui::GetCursorPosX() + em, ImGui::GetContentRegionMax().x - cluster_w - em * 0.2f));

        // Problems chip: severity-colored count; click reveals the Problems tab's panel.
        ImGui::PushStyleColor(ImGuiCol_Text, nerr > 0 ? ImVec4(0.90f, 0.45f, 0.42f, 1.0f)
                                           : nwarn > 0 ? ImVec4(0.90f, 0.75f, 0.35f, 1.0f)
                                                       : ImVec4(0.50f, 0.75f, 0.50f, 1.0f));
        temp_data->OpenProblems = ImGui::Button(prob_lbl);
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%d error(s), %d warning(s) -- click to open Problems", nerr, nwarn);
        ImGui::SameLine();

        if (code_open)
          ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
        temp_data->ToggleCode = ImGui::Button(code_lbl);
        if (code_open)
          ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Show / hide the generated-code + problems panel");
        ImGui::SameLine();

        // Live-mirror visibility: a latched toggle, lit while the mirror is shown. Hiding never deletes.
        // The render path records only the CLICK; OnUpdate derives the new state.
        if (show_live)
          ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
        temp_data->ToggleLive = ImGui::Button(live_lbl);
        if (show_live)
          ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Show / hide read-only nodes mirrored from the running app");
      }
      ImGui::EndChild();
    }
  };

  // The window's status bar (rendered LAST, at the bottom -- Blender's status bar): live keymap hints on
  // the left (composed by the editor, read via AppGraphStatusHint), document/selection facts on the right.
  // All right-side text is derived from the doc in OnUpdate and parked here; OnRender only lays out text.
  struct StatusStripData
  {
    GraphDocData* Doc;
    char CountMsg[96];                  // "design N  live N  promoted N"
    bool HasMirror;                     // doc->Mirror != nullptr
    bool MirrorInit;                    // mirror app initialized
    char MirrorCounts[64];              // "L# W# S# C#"
    char Breadcrumb[IM_LABEL_SIZE * 2]; // selection breadcrumb
    char Msg[64];                       // transient write/diff confirmation (doc->WriteMsg snapshot)
  };
  struct StatusStripTempData {};   // empty by design: the strip is read-only display, it captures no input
  struct StatusStripControl : ImGuiAppControl<StatusStripData, StatusStripTempData>
  {
    virtual void OnInitialize(ImGuiApp* app, StatusStripData* data) const override final
    {
      data->Doc = GetGraphDoc(app);
    }

    virtual void OnUpdate(float dt, StatusStripData* data, const StatusStripTempData*, const StatusStripTempData*) const override final
    {
      IM_UNUSED(dt);
      const GraphDocData* doc = data->Doc;

      // Cycle/validation state now lives on the Generate button + Problems chip; the strip carries facts.
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
      {
        ImFormatString(data->CountMsg, IM_ARRAYSIZE(data->CountMsg), "design %d  live %d  promoted %d", nd, nl, np);
      }
      else
      {
        ImFormatString(data->CountMsg, IM_ARRAYSIZE(data->CountMsg), "design %d", nd);
      }

      data->HasMirror = (doc->Mirror != nullptr);
      if (data->HasMirror)
      {
        const ImGuiApp* a = doc->Mirror;
        ImFormatString(data->MirrorCounts, IM_ARRAYSIZE(data->MirrorCounts), "L%d W%d S%d C%d", a->Layers.Size, a->Windows.Size, a->Sidebars.Size, a->Controls.Size);
        data->MirrorInit = a->Layers.Size > 0;   // "composed"; Initialized is the platform flag, never set here
      }

      ImGui::AppGraphSelectionBreadcrumb(&doc->Graph, doc->Selection, data->Breadcrumb, IM_ARRAYSIZE(data->Breadcrumb));
      ImStrncpy(data->Msg, doc->WriteMsg, IM_ARRAYSIZE(data->Msg));
    }

    virtual void OnRender(const StatusStripData* data, StatusStripTempData*) const override final
    {
      const float       em    = ImGui::GetFontSize();
      const ImGuiStyle& style = ImGui::GetStyle();
      if (ImGui::BeginChild("##Strip", ImVec2(0.0f, 0.0f), ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
      {
        // Left: the editor's live keymap hint (what the mouse does right now); refused links show in red.
        int sev = 0;
        const char* hint = ImGui::AppGraphStatusHint(&sev);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(sev >= 2 ? ImVec4(0.90f, 0.42f, 0.38f, 1.0f) : style.Colors[ImGuiCol_TextDisabled], "%s", hint);

        // Right: transient confirmation, selection breadcrumb, node counts, mirrored-app composition.
        char right[512];
        int  len = 0;
        if (data->Msg[0])
          len += ImFormatString(right + len, IM_ARRAYSIZE(right) - len, "%s      ", data->Msg);
        len += ImFormatString(right + len, IM_ARRAYSIZE(right) - len, "%s      %s", data->Breadcrumb, data->CountMsg);
        if (data->HasMirror)
          ImFormatString(right + len, IM_ARRAYSIZE(right) - len, "      %s  %s", data->MirrorCounts, data->MirrorInit ? "composed" : "uncomposed");
        const float w = ImGui::CalcTextSize(right).x;
        ImGui::SameLine(ImMax(ImGui::GetCursorPosX() + em, ImGui::GetContentRegionMax().x - w - em * 0.4f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", right);
      }
      ImGui::EndChild();
    }
  };

  // The one generated-code view, used by every code tab (consistency: code reads identically wherever it
  // appears). Monospace, line-numbered gutter, clipped rendering. With a source map it also carries the
  // coordinated-view interactions: the selection's lines highlight + auto-scroll into view, a node hovered
  // in any other view tints its lines, hovering a line brushes its node back out, clicking a line selects.
  // Pure view: reads const data, records the click into *selection (the caller's intent capture).
  static void ShowGeneratedCodeView(const char* str_id, const ImGuiTextBuffer& text, const ImVector<int>& lines,
                                    const ImVector<ImGui::ImGuiAppCodeSpan>* spans, int* selection)
  {
    if (g_AppCodeFont)
      ImGui::PushFont(g_AppCodeFont, 0.0f);
    if (ImGui::BeginChild(str_id, ImVec2(-FLT_MIN, -FLT_MIN), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
    {
      const char* buf        = text.Buf.Data;
      const int   text_len   = text.size();
      const int   line_count = lines.Size;
      const float line_h     = ImGui::GetTextLineHeight();

      // Gutter sized to the widest line number (min 3 digits so short files don't jitter the layout).
      int digits = 3;
      for (int n = line_count; n >= 1000; n /= 10)
        digits++;
      const float gutter_w = ImGui::CalcTextSize("0").x * (float)digits;
      const ImU32 gutter_col = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.75f);

      ImGui::ImGuiAppHoverSource hsrc = ImGui::ImGuiAppHoverSource_None;
      const int brushed_node = spans != nullptr ? ImGui::AppGraphHoveredNode(&hsrc) : -1;
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

      // Focus: when the selection changes, scroll its first span into the top quarter. The latch is window
      // view state (like the scroll position itself), so it lives in the window's state storage.
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
            dl->AddRectFilled(row_min, row_max, hl_sel ? IM_COL32(220, 170, 90, 34) : IM_COL32(235, 235, 240, 16));
            dl->AddRectFilled(row_min, ImVec2(row_min.x + 3.0f, row_max.y), hl_sel ? IM_COL32(220, 170, 90, 220) : IM_COL32(235, 235, 240, 90));
          }

          // Line number, right-aligned in the gutter; then the code text.
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
            ImGui::AppGraphHoverNode(owner, ImGui::ImGuiAppHoverSource_External);
            if (selection != nullptr && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
              *selection = owner;   // click code -> select the node (flows into tree + canvas)
          }
        }
      }
      ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    if (g_AppCodeFont)
      ImGui::PopFont();
  }

  // PersistData: durable state, mutated only in OnUpdate. CodeText/CodeName are the rendered output (Breathing's
  // timer_text pattern -- derived in OnUpdate, drawn from const data in OnRender).
  struct EditorBodyData
  {
    GraphDocData*   Doc;                     // shared doc, cached non-const in OnInitialize
    bool            TreeDragging;            // tree splitter drag FSM (advanced only in OnUpdate)
    float           TreeDragDX;              // grab offset within the grip, captured at drag start
    ImGuiTextBuffer CodeText;                // the WHOLE app's generated C++ (kept current while the panel is open)
    ImVector<ImGui::ImGuiAppCodeSpan> CodeSpans;   // source map: node id -> line ranges in CodeText
    ImVector<int>   CodeLines;               // byte offset of each line start in CodeText (render index)
    ImGuiTextBuffer CodeNodeText;            // the selected node's code (the focused "Node" tab)
    ImVector<int>   NodeLines;               // line starts in CodeNodeText
    ImGuiID         CodeSig;                 // graph signature the buffers were generated from (regen gate)
    int             CodeSel;                 // selection they were generated for (regen gate)
    bool            HasCode;                 // CodeText is non-empty
    bool            HasNodeCode;             // a node is selected and its code was generated
    char            CodeName[IM_LABEL_SIZE]; // selected node's draft name (the Node tab label)
    ImVector<ImGui::ImGuiAppGraphIssue> Issues;  // validation problems, recomputed while the panel is open
  };
  // TempData: raw input recorded by OnRender (the only place ImGui item geometry exists) and consumed by OnUpdate.
  // OnRender performs no logic and mutates no state -- it just records what the user did, exactly like the demo's
  // RandomTimeTempData::generate / BreathingControlTempData::hovered.
  struct EditorBodyTempData
  {
    bool  TreeGripActivated;   // drag started on the tree grip this frame
    bool  MouseLeftDown;       // left button held
    float MouseX;              // mouse x (screen)
    float TreeGripMinX;        // tree grip left edge
    float TreeOriginX;         // body row left edge
    bool  CodeGripActive;      // code grip held this frame
    float CodeResolved;        // this frame's clamped code height (drag base)
    float CodeMax;             // this frame's max code height
    float MouseDY;             // mouse y delta while dragging the code grip
    bool  CodeSnapClosed;      // resolved code height collapsed below threshold while idle
    bool  SelectionChanged;    // tree/canvas changed the selection this frame
    int   Selection;           // the new selection
    bool  ToggleScrub;         // transport overlay: "App time" freeze button clicked (OnUpdate derives the new state)
    bool  ScrubIdxSet;         // transport overlay: frame scrubber moved this frame
    int   ScrubIdx;            // target app-state snapshot index
  };
  struct EditorBodyControl : ImGuiAppControl<EditorBodyData, EditorBodyTempData>
  {
    virtual void OnInitialize(ImGuiApp* app, EditorBodyData* data) const override final
    {
      data->Doc = GetGraphDoc(app);
      data->CodeSig = 0;
      data->CodeSel = -2;   // "never generated" (a real empty selection is -1)
    }

    virtual void OnUpdate(float dt, EditorBodyData* data, const EditorBodyTempData* temp_data, const EditorBodyTempData* last_temp_data) const override final
    {
      IM_UNUSED(dt);
      GraphDocData* doc = data->Doc;

      // Tree splitter FSM, driven entirely by input captured last render.
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

      if (temp_data->SelectionChanged)
      {
        doc->Selection = temp_data->Selection;
      }

      // Transport overlay intents (App-time freeze + frame scrub over the mirror's state ring). The button
      // records only the CLICK; deriving the next state is this function's job, not the render path's.
      if (temp_data->ToggleScrub)
      {
        doc->TimeScrub = !doc->TimeScrub;
        if (doc->TimeScrub)
          doc->TimeScrubIndex = ImMax(0, doc->MirrorHistory.Count - 1);   // enter the timeline at "now"
      }
      if (temp_data->ScrubIdxSet)
      {
        doc->TimeScrubIndex = temp_data->ScrubIdx;
      }

      // Regenerate the code buffers only when their inputs changed (graph signature or selection): derived
      // state lives in PersistData, computed here, drawn from const data in OnRender. A closed panel keeps
      // its stale buffers -- the signature gate makes them correct again the moment they are next needed.
      const ImGuiID gsig = ImGui::AppGraphSignature(&doc->Graph);
      if (doc->CodeH > 0.0f && (gsig != data->CodeSig || doc->Selection != data->CodeSel))
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
          ImGui::GenerateAppNodeCode(&doc->Graph, seln, &data->CodeNodeText);
          ImStrncpy(data->CodeName, seln->Draft.Name, sizeof(data->CodeName));
          data->HasNodeCode = data->CodeNodeText.size() > 0;
        }
        index_lines(data->CodeNodeText, &data->NodeLines);
      }

      // Validation problems for the Problems tab (only while the panel is open -- it scans the whole graph).
      data->Issues.clear();
      if (doc->CodeH > 0.0f)
      {
        ImGui::AppGraphValidate(&doc->Graph, &data->Issues);
      }
    }

    virtual void OnRender(const EditorBodyData* data, EditorBodyTempData* temp_data) const override final
    {
      GraphDocData* doc = data->Doc;
      if (doc->Mirror == nullptr)                            // tree + canvas mirror the example app
      {
        return;
      }
      ImGuiApp*      app   = doc->Mirror;                    // non-const: the viewer/canvas APIs edit through it
      ImGuiAppGraph* graph = &doc->Graph;

      // All layout is local + display-only (TreeW == 0 -> default); nothing is written to the doc from OnRender.
      const float    em            = ImGui::GetFontSize();
      const ImGuiIO& io            = ImGui::GetIO();
      ImVec2         body          = ImGui::GetContentRegionAvail();
      body.y = ImMax(em * 4.0f, body.y - ImGui::GetFrameHeightWithSpacing());   // reserve the status bar row below
      const float    tree_origin_x = ImGui::GetCursorScreenPos().x;
      const float    tree_grip     = em * 0.5f;
      const float    min_canvas_w  = em * 16.0f;
      const float    min_canvas_h  = em * 8.0f;
      const float    code_grip     = (doc->CodeH > 0.0f) ? em * 0.5f : 0.0f;
      const float    code_max      = ImMax(0.0f, body.y - min_canvas_h - code_grip);
      const float    code_h        = ImClamp(doc->CodeH, 0.0f, code_max);
      const float    canvas_h      = ImMax(0.0f, body.y - code_h - code_grip);
      const float    tree_w        = ImClamp((doc->TreeW > 0.0f) ? doc->TreeW : em * 16.0f, em * 9.0f, ImMax(em * 9.0f, body.x - tree_grip - min_canvas_w));
      const float    right_w       = ImMax(0.0f, body.x - tree_w - tree_grip);
      int            selection     = doc->Selection;
      float          col_w         = 0.0f;     // assigned once inside ##Right (needs that child's content region)

      // Left: tree sidebar (full height above the status bar).
      if (ImGui::BeginChild("##Tree", ImVec2(tree_w, body.y), ImGuiChildFlags_Borders))
      {
        ImGui::ShowAppGraphTree(app, graph, &selection);
      }
      ImGui::EndChild();

      // Tree splitter: record raw input; the drag is resolved in OnUpdate. Cursor is a pure visual.
      ImGui::SameLine(0.0f, 0.0f);
      ImGui::InvisibleButton("##tsplit", ImVec2(tree_grip, body.y));
      temp_data->TreeGripActivated = ImGui::IsItemActivated();
      temp_data->MouseLeftDown     = ImGui::IsMouseDown(ImGuiMouseButton_Left);
      temp_data->MouseX            = io.MousePos.x;
      temp_data->TreeGripMinX      = ImGui::GetItemRectMin().x;
      temp_data->TreeOriginX       = tree_origin_x;
      if (data->TreeDragging || ImGui::IsItemHovered())
      {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      }
      ImGui::SameLine(0.0f, 0.0f);

      // Right column: node graph canvas on top, code inspector on the bottom split.
      if (ImGui::BeginChild("##Right", ImVec2(right_w, body.y)))
      {
        col_w = ImGui::GetContentRegionAvail().x;

        if (ImGui::BeginChild("##NodeGraph", ImVec2(col_w, canvas_h), ImGuiChildFlags_Borders))
        {
          ImGui::ShowAppGraphEditor(app, graph, &selection, doc->ShowLive);

          // Transport overlay (bottom-center of the canvas): the run-time controls float on the viewport,
          // the game-editor play bar. Freeze the MIRRORED app ("App time") and scrub its recorded state
          // ring -- the framework's state discipline makes ANY app scrubbable; this is that theorem as a
          // slider. Real ImGui items submitted after the editor, so they win hover over the canvas.
          temp_data->ToggleScrub = false;
          temp_data->ScrubIdxSet = false;
          const int app_frames = doc->MirrorHistory.Count;
          if (app_frames > 1 || doc->TimeScrub)
          {
            const ImGuiStyle& tstyle  = ImGui::GetStyle();
            const char*       t_lbl   = doc->TimeScrub ? ICON_FA_CLOCK "  App time###apptime" : ICON_FA_CLOCK "###apptime";
            const float       t_btn_w = ImGui::CalcTextSize(t_lbl, ImGui::FindRenderedTextEnd(t_lbl)).x + tstyle.FramePadding.x * 2.0f;
            const float       t_w     = t_btn_w + (doc->TimeScrub ? tstyle.ItemSpacing.x + em * 11.0f : 0.0f);
            const ImVec2      c_min   = ImGui::GetWindowPos();
            const ImVec2      c_size  = ImGui::GetWindowSize();
            const ImVec2      t_pos(c_min.x + (c_size.x - t_w) * 0.5f, c_min.y + c_size.y - ImGui::GetFrameHeight() - em * 0.7f);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(t_pos.x - em * 0.4f, t_pos.y - em * 0.25f),
                                                      ImVec2(t_pos.x + t_w + em * 0.4f, t_pos.y + ImGui::GetFrameHeight() + em * 0.25f),
                                                      IM_COL32(24, 25, 28, 215), em * 0.5f);
            ImGui::SetCursorScreenPos(t_pos);
            if (doc->TimeScrub)
              ImGui::PushStyleColor(ImGuiCol_Button, tstyle.Colors[ImGuiCol_ButtonActive]);
            temp_data->ToggleScrub = ImGui::Button(t_lbl);   // record the click; OnUpdate derives the new state
            if (doc->TimeScrub)
              ImGui::PopStyleColor();
            ImGui::SetItemTooltip("Freeze the running app and scrub its recorded state history (%d frames)", app_frames);
            if (doc->TimeScrub && app_frames > 0)
            {
              ImGui::SameLine();
              ImGui::SetNextItemWidth(em * 11.0f);
              int idx = ImClamp(doc->TimeScrubIndex, 0, app_frames - 1);
              if (ImGui::SliderInt("##appscrub", &idx, 0, app_frames - 1, "frame %d"))
              {
                temp_data->ScrubIdxSet = true;
                temp_data->ScrubIdx    = idx;
              }
            }
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
              if (ImGui::BeginTabItem("Inspector"))
              {
                ImGui::EditAppNodeInspector(graph, selection);   // edit the selected node's data (name / fields / props)
                ImGui::EndTabItem();
              }
              // Code: the whole generated program, source-mapped -- the selection's lines highlight and
              // scroll into view, hover brushes both ways, clicking a line selects the node. Scopes are
              // TABS (the idiom this panel already speaks), so the focused view is its own tab below.
              if (ImGui::BeginTabItem("Code"))
              {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("Whole program");
                if (data->HasCode)
                {
                  ImGui::SameLine();
                  if (ImGui::Button("Copy"))
                  {
                    ImGui::SetClipboardText(data->CodeText.c_str());
                  }
                }
                if (doc->WriteMsg[0])
                {
                  ImGui::SameLine();
                  ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", doc->WriteMsg);
                }
                if (!data->HasCode)
                {
                  ImGui::TextDisabled("The graph generates no code yet -- add a window or a control.");
                }
                else
                {
                  ShowGeneratedCodeView("##codeall", data->CodeText, data->CodeLines, &data->CodeSpans, &selection);
                }
                ImGui::EndTabItem();
              }
              // The selected node's contribution, as its own named tab -- present exactly while a node with
              // code is selected ("###nodecode" keeps the tab's identity stable across selection changes).
              if (data->HasNodeCode)
              {
                char node_tab[IM_LABEL_SIZE + 16];
                ImFormatString(node_tab, IM_ARRAYSIZE(node_tab), "%s###nodecode", data->CodeName);
                if (ImGui::BeginTabItem(node_tab))
                {
                  ImGui::AlignTextToFramePadding();
                  ImGui::TextDisabled("Selected node's contribution");
                  ImGui::SameLine();
                  if (ImGui::Button("Copy"))
                  {
                    ImGui::SetClipboardText(data->CodeNodeText.c_str());
                  }
                  ShowGeneratedCodeView("##codenode", data->CodeNodeText, data->NodeLines, nullptr, nullptr);
                  ImGui::EndTabItem();
                }
              }
              // Preview ("Play"): render the selected control's fields as a live mock UI.
              if (ImGui::BeginTabItem("Preview"))
              {
                ImGui::AppGraphRenderMockPanel(graph, selection);
                ImGui::EndTabItem();
              }
              // Problems: validation findings. The tab label carries the count; clicking a row reveals the node.
              char problems_label[32];
              if (data->Issues.Size > 0)
              {
                ImFormatString(problems_label, IM_ARRAYSIZE(problems_label), "Problems (%d)###problems", data->Issues.Size);
              }
              else
              {
                ImStrncpy(problems_label, "Problems###problems", IM_ARRAYSIZE(problems_label));
              }
              if (ImGui::BeginTabItem(problems_label))
              {
                if (data->Issues.Size == 0)
                {
                  ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "No problems found.");
                }
                else
                {
                  for (int i = 0; i < data->Issues.Size; i++)
                  {
                    const ImGui::ImGuiAppGraphIssue& it = data->Issues.Data[i];
                    const ImVec4 col = (it.Severity >= 2) ? ImVec4(0.92f, 0.45f, 0.45f, 1.0f) : ImVec4(0.92f, 0.80f, 0.40f, 1.0f);
                    ImGui::PushID(i);
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    const char* dot = (it.Severity >= 2) ? "[x] " : "[!] ";
                    char row[288];
                    ImFormatString(row, IM_ARRAYSIZE(row), "%s%s", dot, it.Text);
                    if (ImGui::Selectable(row) && it.NodeId >= 0)
                    {
                      selection = it.NodeId;   // reveal + select the offending node in tree + canvas
                    }
                    // Brushing: hovering a problem previews its node everywhere, before committing a click.
                    if (it.NodeId >= 0 && ImGui::IsItemHovered())
                      ImGui::AppGraphHoverNode(it.NodeId, ImGui::ImGuiAppHoverSource_External);
                    ImGui::PopStyleColor();
                    ImGui::PopID();
                  }
                }
                ImGui::EndTabItem();
              }
              ImGui::EndTabBar();
            }
          }
          ImGui::EndChild();
        }
      }
      ImGui::EndChild();

      // Record the selection change for OnUpdate to apply (the tree/canvas widgets edited the local copy).
      temp_data->SelectionChanged = (selection != doc->Selection);
      temp_data->Selection = selection;
    }
  };

  // The host window: empty body; its hosted controls (toolbar, strip, body) fill it in push order. Label is
  // fixed (not the type-derived unique label) so the saved .ini dock binding + central-node dock still match.
  struct ComposerWindow : ImGuiAppWindow<ComposerWindow>
  {
    ComposerWindow() { ImStrncpy(this->Label, "ImGuiAppLayer Composer", sizeof(this->Label)); }
    virtual void OnRender(const ImGuiApp*) const override final {}
  };

  // The demo's own control panel, dogfooded as a framework window+control rather than a pile of function statics.
  // The toggle values + their "applied" bookkeeping live in PersistData; OnRender copies them into locals for the
  // ImGui-bound MenuItems, then records the result into TempData; OnUpdate writes it back (RandomTime's pattern).
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
    bool Rendered;   // false until OnRender ran once (the first OnUpdate must not consume zeroed toggles)
    bool ShowBaseWindow;
    bool ShowStatusBar;
    bool ShowRandomTime;
    bool ShowBreathing;
    bool ShowMetrics;
  };
  struct DemoMenuControl : ImGuiAppControl<DemoMenuData, DemoMenuTempData>
  {
    // First-run defaults show the framework doing its job: the two idiom controls running live and the
    // Composer mirroring them. An empty screen taught nothing.
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
  IMGUI_API void SetAppCodeFont(ImFont* font) { g_AppCodeFont = font; }

  IMGUI_API void ShowAppLayerDemo(bool* p_open)
  {
      static ImGuiApp app;          // the mirrored "example" app the demo composes

      // imnodes shares the current ImGui context; create its context once (lives for the session).
      static bool imnodes_ready = false;
      if (!imnodes_ready)
      {
        ImNodes::CreateContext();
        // Ctrl adds to the selection (so box-select and Ctrl-click compose); left-drag on empty canvas still
        // rubber-bands. Panning stays on the middle mouse (imnodes' default AltMouseButton), leaving right-click
        // free for the canvas context menu.
        ImNodes::GetIO().MultipleSelectModifier.Modifier = &ImGui::GetIO().KeyCtrl;

        // Blender-flat node-editor theme: rounded nodes, soft outlines, roomy padding, a dark recessed canvas
        // with low-contrast grid, and thicker curvier links. Tuned to match the Bl-palette node bodies.
        ImNodesStyle& ns = ImNodes::GetStyle();
        ns.NodeCornerRounding   = 5.0f;
        ns.NodePadding          = ImVec2(9.0f, 7.0f);
        ns.NodeBorderThickness  = 1.0f;
        ns.LinkThickness        = 2.6f;
        ns.LinkLineSegmentsPerLength = 0.15f;
        ns.PinCircleRadius      = 4.2f;
        ns.PinHoverRadius       = 10.0f;
        ns.GridSpacing          = 26.0f;
        ImU32* c = ns.Colors;
        c[ImNodesCol_NodeBackground]         = IM_COL32(48, 48, 50, 255);
        c[ImNodesCol_NodeBackgroundHovered]  = IM_COL32(56, 56, 58, 255);
        c[ImNodesCol_NodeBackgroundSelected] = IM_COL32(60, 60, 62, 255);
        c[ImNodesCol_NodeOutline]            = IM_COL32(28, 28, 30, 255);
        c[ImNodesCol_TitleBarSelected]       = IM_COL32(220, 170, 90, 255);
        c[ImNodesCol_GridBackground]         = IM_COL32(30, 30, 32, 255);
        c[ImNodesCol_GridLine]               = IM_COL32(42, 42, 45, 255);
        c[ImNodesCol_GridLinePrimary]        = IM_COL32(52, 52, 56, 255);
        c[ImNodesCol_Link]                   = IM_COL32(170, 170, 175, 200);
        c[ImNodesCol_LinkHovered]            = IM_COL32(220, 180, 100, 255);
        c[ImNodesCol_LinkSelected]           = IM_COL32(230, 190, 110, 255);
        c[ImNodesCol_BoxSelector]            = IM_COL32(220, 170, 90, 40);
        c[ImNodesCol_BoxSelectorOutline]     = IM_COL32(220, 170, 90, 150);
        imnodes_ready = true;
      }

      // ---- editor_app: a dedicated, never-rebuilt ImGuiApp hosting BOTH the demo's control panel and the
      //      dogfooded Composer. Pushed once so the graph + toggle state survive example rebuilds. Toggle
      //      state lives in DemoMenuData (PersistData), not function statics. Only the WindowLayer is needed.
      static ImGuiApp editor_app;
      static bool editor_ready = false;
      if (!editor_ready)
      {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        // Task layer FIRST: control OnUpdate runs there since the core moved it out of the Window layer --
        // without it every Composer control's update (toolbar intents, splitters, mirror, time travel) is
        // silently skipped. No Command/Status layers: the Composer emits no commands and the status overlay
        // belongs to the example app.
        ImGui::PushAppLayer<ImGuiAppTaskLayer>(&editor_app);
        ImGui::PushAppLayer<ImGuiAppWindowLayer>(&editor_app);

        // Demo control panel (menu + blurb).
        ImGui::PushAppWindow<DemoPanelWindow>(&editor_app);
        ImGuiAppWindowBase* panel = editor_app.Windows.back();
        panel->HasInitialPlacement = true;
        panel->InitialSize = ImVec2(vp->WorkSize.x * 0.30f, vp->WorkSize.y * 0.40f);
        panel->InitialPos  = vp->WorkPos + ImVec2(vp->WorkSize.x * 0.02f, vp->WorkSize.y * 0.04f);
        ImGui::PushWindowControl<DemoMenuControl>(&editor_app, panel);

        // Composer (GraphDoc + Toolbar + StatusStrip + EditorBody).
        ImGui::PushAppWindow<ComposerWindow>(&editor_app);
        ImGuiAppWindowBase* metrics = editor_app.Windows.back();
        metrics->HasInitialPlacement = true;
        metrics->InitialSize = ImVec2(vp->WorkSize.x * 0.66f, vp->WorkSize.y * 0.66f);
        metrics->InitialPos  = vp->WorkPos + ImVec2(vp->WorkSize.x * 0.10f, vp->WorkSize.y * 0.10f);
        ImGui::PushWindowControl<GraphDocControl>(&editor_app, metrics);   // producer: owns the doc (push first)
        ImGui::PushWindowControl<ToolbarControl>(&editor_app, metrics);    // consumers depend on GraphDocData
        ImGui::PushWindowControl<EditorBodyControl>(&editor_app, metrics);
        ImGui::PushWindowControl<StatusStripControl>(&editor_app, metrics);   // status bar renders LAST -> window bottom
        if (GraphDocData* d = GetGraphDoc(&editor_app))
        {
          d->Mirror = &app;
        }
        editor_ready = true;
      }

      ImGuiAppWindowBase* panel   = editor_app.Windows[0];
      ImGuiAppWindowBase* metrics = editor_app.Windows[1];
      DemoMenuData*       st      = GetDemoMenu(&editor_app);

      // Drive the framework windows' Open from the external/menu flags, tick the editor app, then read the X
      // buttons back out. The menu (a control on `panel`) toggles st->* during RenderApp.
      panel->Open   = (p_open == nullptr) || *p_open;
      metrics->Open = st->ShowMetrics;
      ImGui::UpdateApp(&editor_app);
      ImGui::RenderApp(&editor_app);
      if (p_open != nullptr)     // panel X closes the whole demo
      {
        *p_open = panel->Open;
      }
      if (!metrics->Open)        // metrics X syncs the menu toggle
      {
        st->ShowMetrics = false;
      }

      // Reconcile desired -> live app. Full rebuild keeps app storage consistent across toggles. Also fire on
      // the first frame (no layers yet) so the framework foundation exists immediately and is visible in the
      // tree/canvas without toggling an example. NOT !IsInitialized(): that flag belongs to the PLATFORM
      // bring-up (ImGuiApp::Initialize) and never goes true for this embedded app -- gating on it rebuilt the
      // example EVERY frame (RandomTime re-rolled per frame, Breathing never accumulated, the state history
      // reset before it could hold two frames).
      if (app.Layers.empty() ||
          st->AppliedBaseWindow != st->ShowBaseWindow ||
          st->AppliedStatusBar  != st->ShowStatusBar  ||
          st->AppliedRandomTime != st->ShowRandomTime ||
          st->AppliedBreathing  != st->ShowBreathing)
      {
        ShutdownApp(&app);
        InitializeApp(&app);

        ImGuiViewport* vp = ImGui::GetMainViewport();
        const float em = ImGui::GetFontSize();       // text size: drives all sizing, scales with DPI/font

        if (st->ShowBaseWindow)
        {
          PushAppWindow<BaseWindow>(&app);
          ImGuiAppWindowBase* w = app.Windows.back();
          w->HasInitialPlacement = true;
          w->InitialSize = ImVec2(em * 16.0f, em * 8.0f);
          w->InitialPos  = ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + em * 2.0f);
          PushWindowControl<BaseInfoControl>(&app, w);   // hosted control -> live containment edge in the editor
        }

        if (st->ShowStatusBar)
        {
          PushAppSidebar<StatusBar>(&app, vp, ImGuiDir_Down, 0.0f, ImGuiWindowFlags_AlwaysAutoResize);
        }

        // Controls are app-level: they render their own windows, no host sidebar needed.
        if (st->ShowRandomTime)
        {
          PushAppControl<RandomTimeControlDemo>(&app);
        }
        if (st->ShowBreathing)
        {
          PushAppControl<BreathingControlDemo>(&app);
        }

        st->AppliedBaseWindow = st->ShowBaseWindow;
        st->AppliedStatusBar  = st->ShowStatusBar;
        st->AppliedRandomTime = st->ShowRandomTime;
        st->AppliedBreathing  = st->ShowBreathing;
      }

      UpdateApp(&app);
      RenderApp(&app);

      if (app.ShutdownPending)
      {
        ShutdownApp(&app);
      }
  }
}
