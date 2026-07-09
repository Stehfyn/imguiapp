// dear imgui app, v0.5.0 WIP
// (composer demo -- tool UI)

#ifndef IMGUIX_DISABLE_TOOLS   // TOOL (UI): compiled out in a lean build (Phase A4)

// ImGuiAppLayer demo.
//
// Index of this file (search for "[SECTION]"):
// [SECTION] Sample controls (RandomTime, Breathing) -- the framework idioms, showcased
// [SECTION] Composer document (ImGuiAppGraphDocData) + workspace layout persistence
// [SECTION] Playback debugger -- FILE-mode transport (F63)
// [SECTION] Composer toolbar (flow-ordered: compose -> iterate -> persist -> produce | observe)
// [SECTION] Composer status strip (keymap hints + document counts)
// [SECTION] Generated-code view (source-mapped, shared by every code surface)
// [SECTION] Composer editor body (outliner | canvas + bottom console | inspector; project inspector)
// [SECTION] Composer host window + demo menu
// [SECTION] Demo bring-up (ShowAppDemo: ONE application)

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imguiapp.h"
#ifndef IMGUI_DISABLE
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495) // [Static Analyzer] uninitialized member (type.6); memset ctors
#endif
#include "imguiapp_internal.h"
#include "IconsFontAwesome6.h"             // ICON_FA_* glyphs (font merged by the host app)
#include "backends/imguiapp_impl_qoi.h"      // F63: on-demand QOI frame decode + meta extract
#ifdef IMGUIX_HAS_LIBAV
#include "backends/imguiapp_impl_libav.h"    // F63: on-demand mp4 frame decode + meta extract
#endif

#include <ctime>                             // time (rng seed)
#include <cstdlib>
#include <cstdio>
#include <cstring>

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

static const ImVec4 DEMO_GOLD   = ImVec4(0.88f, 0.72f, 0.40f, 1.0f);   // selection / focus accent
static const ImVec4 DEMO_RED    = ImVec4(0.90f, 0.45f, 0.42f, 1.0f);   // errors
static const ImVec4 DEMO_YELLOW = ImVec4(0.90f, 0.75f, 0.35f, 1.0f);   // warnings
static const ImVec4 DEMO_GREEN  = ImVec4(0.50f, 0.75f, 0.50f, 1.0f);   // healthy

// Composer chrome colors come from the one style table (docs/composer-studio-design.md 3.6:
// zero color literals outside it); this converts a row for the ImVec4-taking widget calls.
static ImVec4 ComposerCol(ImU32 row) { return ImGui::ColorConvertU32ToFloat4(row); }

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
    char  Label[128];
    char  Type[128];
    float MaxTimerSecs;
    ImU64 Rng; // seeded once in OnInitialize, stepped only by OnUpdate: snapshots capture it,
               // record/replay reproduces every roll (see ImAppRandom)
};

struct RandomTimeTempData
{
    bool Generate;
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
        snprintf(data->Type, sizeof(data->Type), "%.*s", (int)ImGuiAppType<decltype(this)>::Name.length(), ImGuiAppType<decltype(this)>::Name.data());
        snprintf(data->Label, sizeof(data->Label), "%s", data->Type);

        data->Rng = (ImU64)time(nullptr);
        data->MaxTimerSecs = GenerateTime(&data->Rng);
    }

    virtual void OnUpdate(float dt, RandomTimeData* data, const RandomTimeTempData* temp_data, const RandomTimeTempData* last_temp_data) const override final
    {
        IM_UNUSED(dt);
        IM_UNUSED(last_temp_data);

        if (temp_data->Generate)
            data->MaxTimerSecs = GenerateTime(&data->Rng);
    }

    virtual void OnDraw(const RandomTimeData* data, RandomTimeTempData* temp_data) const override final
    {
        // Renders between the host window's Begin/End.
        ImGui::Text("%s", "Max Timer Seconds");

        temp_data->Generate = ImGui::Button("Generate");
        ImGui::SameLine();

        ImGui::Text("%.1f", data->MaxTimerSecs);
    }
};

struct BreathingControlData
{
    char   Label[128];
    char   Type[128];
    char   Text[128];
    char   TimerText[128];
    float  TimerSecs;
    float  TValue;
    float  TDirection;
    ImVec4 Col;
};

struct BreathingControlTempData
{
    bool Hovered;
};

// RandomTimeData is an Optional dependency: null while the Random Time example is disabled,
// rebound live when it is pushed/popped (the push site passes the Optional binding).
struct BreathingControlDemo : ImGuiAppControl<BreathingControlData, BreathingControlTempData, RandomTimeData>
{
    static constexpr float DefaultMaxTimerSecs = 5.0f;

    static float SourceMaxTimerSecs(const RandomTimeData* src)
    {
        if (src != nullptr)
            return src->MaxTimerSecs;
        return DefaultMaxTimerSecs;
    }

    virtual void OnInitialize(ImGuiApp* app, BreathingControlData* data, const RandomTimeData* src) const override final
    {
        IM_UNUSED(app);
        IM_UNUSED(src);

        snprintf(data->Type, sizeof(data->Type), "%.*s", (int)ImGuiAppType<decltype(this)>::Name.length(), ImGuiAppType<decltype(this)>::Name.data());
        snprintf(data->Label, sizeof(data->Label), "%s", data->Type);
    }

    virtual void OnUpdate(float dt, BreathingControlData* data, const BreathingControlTempData* temp_data, const BreathingControlTempData* last_temp_data, const RandomTimeData* src) const override final
    {
        data->TimerSecs = data->TimerSecs - dt > 0.0f ? data->TimerSecs - dt : 0.0f;

        if (temp_data->Hovered ^ last_temp_data->Hovered)
        {
            data->TimerSecs = temp_data->Hovered * SourceMaxTimerSecs(src);
            data->TValue = 0.0f;
            data->TDirection = 1.0f;
        }

        if (0.0f < data->TimerSecs)
        {
            snprintf(data->TimerText, sizeof(data->TimerText), "%.1f Seconds Left!", data->TimerSecs);
        }
        else
        {
            snprintf(data->TimerText, sizeof(data->TimerText), "%s", "Timer Expired!");
        }

        if (temp_data->Hovered)
        {
            // User-code forms of ImLinearSweep/ImLerp (imgui_internal helpers are off-limits here).
            if (data->TValue < data->TDirection)
                data->TValue = (data->TValue + dt < data->TDirection) ? data->TValue + dt : data->TDirection;
            else if (data->TValue > data->TDirection)
                data->TValue = (data->TValue - dt > data->TDirection) ? data->TValue - dt : data->TDirection;
            data->TDirection = (data->TValue == data->TDirection) ? -data->TDirection : data->TDirection;
            const ImVec4  ca = ImGui::GetStyleColorVec4(ImGuiCol_Button);
            const ImVec4  cb = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
            const float   ct = 0.0f <= data->TValue ? data->TValue : -data->TValue;
            data->Col = ImVec4(ca.x + (cb.x - ca.x) * ct, ca.y + (cb.y - ca.y) * ct, ca.z + (cb.z - ca.z) * ct, ca.w + (cb.w - ca.w) * ct);
        }
    }

    virtual void OnDraw(const BreathingControlData* data, BreathingControlTempData* temp_data, const RandomTimeData* src) const override final
    {
        IM_UNUSED(src);

        // Renders between the host window's Begin/End.
        ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, 2.0f * ImGui::GetFrameHeight());

        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, data->Col);
        ImGui::Button("Hover Me!", size);
        temp_data->Hovered = ImGui::IsItemHovered();
        ImGui::PopStyleColor();

        ImVec2 pos = ImGui::GetCursorScreenPos();
        size = ImGui::GetContentRegionAvail();
        ImRect r = { pos, pos + size };

        ImGui::PushClipRect(r.Min, r.Max, true);
        ImGui::NewLine();
        RenderTextT(data->TimerText, ImGui::CalcTextSize(data->TimerText), ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail(), data->TValue);
        ImGui::PopClipRect();
    }
};

struct BaseWindow : ImGuiAppWindow<BaseWindow>
{
    virtual void OnDraw(const ImGuiApp*) const override final
    {
        ImGui::TextWrapped("%s", "This is a base window managed by ImGuiAppDisplayLayer.");
    }
};

// Window-hosted control: renders inside its host window (no Begin/End).
struct BaseInfoData
{
    int Frames;
};
struct BaseInfoControl : ImGuiAppControl<BaseInfoData, ImGuiAppNoTempData>
{
    virtual void OnInitialize(ImGuiApp*, BaseInfoData* data) const override final
    {
        data->Frames = 0;
    }

    virtual void OnUpdate(float dt, BaseInfoData* data, const ImGuiAppNoTempData* temp_data, const ImGuiAppNoTempData* last_temp_data) const override final
    {
        IM_UNUSED(dt);
        IM_UNUSED(temp_data);
        IM_UNUSED(last_temp_data);
        data->Frames++;
    }

    virtual void OnDraw(const BaseInfoData* data, ImGuiAppNoTempData* temp_data) const override final
    {
        IM_UNUSED(temp_data);
        ImGui::Separator();
        ImGui::Text("Hosted control: %d frames alive", data->Frames);
    }
};

struct StatusBar : ImGuiAppSidebar<StatusBar>
{
    virtual void OnDraw(const ImGuiApp*) const override final
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
// [SECTION] Composer document (ImGuiAppGraphDocData) + workspace layout persistence
//-----------------------------------------------------------------------------

// Bottom-bar panel identities for the one-shot reveal intent.
enum ImGuiAppComposerPanel_
{
    ImGuiAppComposerPanel_None = 0,
    ImGuiAppComposerPanel_Code,
    ImGuiAppComposerPanel_Project,
    ImGuiAppComposerPanel_Preview,
    ImGuiAppComposerPanel_Replay,
    ImGuiAppComposerPanel_Output,
};

// Document verbs registered into the canvas command palette; the toolbar consumes the pick.
enum ImGuiAppComposerHostCmd_
{
    ImGuiAppComposerHostCmd_Save = 1,
    ImGuiAppComposerHostCmd_Load,
    ImGuiAppComposerHostCmd_Generate,
    ImGuiAppComposerHostCmd_CopyCode,
    ImGuiAppComposerHostCmd_Diff,
    ImGuiAppComposerHostCmd_PanelCode,
    ImGuiAppComposerHostCmd_PanelProject,
    ImGuiAppComposerHostCmd_PanelPreview,
    ImGuiAppComposerHostCmd_PanelOutput,
    ImGuiAppComposerHostCmd_ToggleLive,
    ImGuiAppComposerHostCmd_Shortcuts,
    ImGuiAppComposerHostCmd_PanelReplay,
};

// App-time transport (F29): a per-frame snapshot ring of the mirror's snapshottable (trivially-copyable)
// controls plus the scrub position. Lives OFF ImGuiAppGraphDocData's snapshotted bytes (the doc is opaque, so
// AppStateSnapshot never captures this history). Heap, process lifetime. F63 adds the LIVE vs FILE source switch.
struct ImGuiAppComposerTransport
{
    ImGuiAppStateHistory History;
    bool                 Frozen = false;   // engaged: hold + scrub instead of record
    int                  Frame  = 0;       // scrub position (0..Count-1)

    // FILE source (F63). One transport, two sources: LIVE restores bytes into the mirror; FILE decodes
    // the recorded frame image at a tick and blits it. All FILE state rides this object -- no TU globals.
    int                  Source = ImGuiAppTransportSource_LiveRing;
    ImGuiAppRunIndex*    Run = nullptr;                        // the opened run (owned; AppRunClose on close/reopen)
    ImGuiAppRunView      FileView;                             // run index + decoder behind Count()/Show(int)
    ImVector<ImU64>      CommandTicks;                         // ticks with a WAL "execute command" dispatch (marker source)
    ImTextureData*       FrameTex = nullptr;                   // GPU texture holding the decoded frame (lazy)
    int                  FrameTexTick = -1;                    // tick uploaded to FrameTex (decode/upload cache)
    int                  FrameTexW = 0, FrameTexH = 0;         // upload-helper size cache (one blit path, ST2.5)
    char                 RunName[256] = "headless-artifacts/composer-headless";  // recording path (QOI take directory)
    char                 RunDir[256]  = "";                    // resolved directory handed to the decoder
    char                 OpenErr[160] = "";                    // last open failure, shown in the window
};

struct ImGuiAppGraphDocData
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
    int                  RevealPanel;     // one-shot ImGuiAppComposerPanel_*: reveal + select that bottom tab (0 = none)
    int                  LinkErrSeqSeen;  // last Graph.LastLinkErrSeq folded into the log
    ImGuiID              LayoutSavedHash; // hash of the last-persisted layout fields (change detection)
    float                LayoutSaveT;     // debounce: seconds until the next layout-save check
    ImGuiApp*            Mirror;          // THE running app: the one hosting this control (set in OnInitialize)
    ImGuiAppComposerTransport*   Transport;       // App-time scrubber state (heap; opaque, never snapshotted) -- F29
    int                  NumUnbuilt;      // per-frame count: authored nodes with no live counterpart in the running
                                          // binary -- nonzero == stale until Generate + recompile + relaunch
};

// PersistData aliases the start of InstanceData.
static ImGuiAppGraphDocData* GetGraphDoc(ImGuiApp* app)
{
    return (ImGuiAppGraphDocData*)app->Data.GetVoidPtr(ImGuiAppType<ImGuiAppGraphDocData>::ID);
}

//-----------------------------------------------------------------------------
// Workspace layout persistence. A sidecar file, not an imgui settings handler: the Composer
// initializes after imgui loads its ini, and handlers registered late never see their sections.
//-----------------------------------------------------------------------------

static const char* COMPOSER_LAYOUT_PATH = "imguix_composer_layout.ini";

struct ImGuiAppComposerLayout
{
    float                  TreeW, InspW, CodeH;
    bool                   ShowLive;
    ImGuiAppGraphViewState View;
};
static ImGuiAppComposerLayout ComposerLayoutCapture(const ImGuiAppGraphDocData* doc)
{
    ImGuiAppComposerLayout f;
    memset(&f, 0, sizeof(f));   // padding participates in the hash -- keep it deterministic
    f.TreeW = doc->TreeW; f.InspW = doc->InspW; f.CodeH = doc->CodeH;
    f.ShowLive = doc->ShowLive;
    f.View = ImGui::AppGraphEditorState(&doc->Graph)->View;
    return f;
}

static void ComposerLayoutLoad(ImGuiAppGraphDocData* doc)
{
    size_t size = 0;
    char* text = (char*)ImFileLoadToMemory(COMPOSER_LAYOUT_PATH, "rb", &size, 1);
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
    const ImGuiAppComposerLayout f = ComposerLayoutCapture(doc);
    doc->LayoutSavedHash = ImHashData(&f, sizeof(f));
}

// At most one disk check per second, skipped mid-gesture so a drag lands once.
static void ComposerLayoutSaveIfChanged(ImGuiAppGraphDocData* doc, float dt)
{
    doc->LayoutSaveT -= dt;
    if (doc->LayoutSaveT > 0.0f || ImGui::IsMouseDown(ImGuiMouseButton_Left))
        return;
    doc->LayoutSaveT = 1.0f;
    const ImGuiAppComposerLayout f = ComposerLayoutCapture(doc);
    const ImGuiID h = ImHashData(&f, sizeof(f));
    if (h == doc->LayoutSavedHash)
        return;
    if (ImFileHandle fh = ImFileOpen(COMPOSER_LAYOUT_PATH, "wt"))
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
static void DocLog(ImGuiAppGraphDocData* doc, int severity, const char* fmt, ...)
{
    ImGuiAppGraphDocData::DocLogLine line;
    line.Severity = severity;
    va_list args;
    va_start(args, fmt);
    ImFormatStringV(line.Text, IM_ARRAYSIZE(line.Text), fmt, args);
    va_end(args);
    doc->Log.push_back(line);
    if (doc->Log.Size > 256)
        doc->Log.erase(doc->Log.Data, doc->Log.Data + 64);
}

// Layout presets (F36): named workspace configs over the sidebar / bottom-panel / live visibilities.
// The active preset is derived, not stored -- it is whichever config the current visibilities match,
// so a manual toggle simply un-lights the preset. Visibilities persist through the layout sidecar.
enum ImGuiAppComposerLayoutPreset_
{
    ImGuiAppComposerLayoutPreset_None = 0,
    ImGuiAppComposerLayoutPreset_Compose,   // authoring: both sidebars, no bottom panel, live hidden
    ImGuiAppComposerLayoutPreset_Review,    // compare design vs generated: sidebars + code panel + live
    ImGuiAppComposerLayoutPreset_Observe,   // watch the running app: canvas + bottom panel + live, no sidebars
};
enum ImGuiAppComposerLayoutVis_          // one bit per toggleable surface
{
    ImGuiAppComposerLayoutVis_None = 0,
    ImGuiAppComposerLayoutVis_Tree = 1 << 0,
    ImGuiAppComposerLayoutVis_Insp = 1 << 1,
    ImGuiAppComposerLayoutVis_Code = 1 << 2,
    ImGuiAppComposerLayoutVis_Live = 1 << 3,
};
static int ComposerLayoutPresetMask(int preset)
{
    switch (preset)
    {
    case ImGuiAppComposerLayoutPreset_Compose: return ImGuiAppComposerLayoutVis_Tree | ImGuiAppComposerLayoutVis_Insp;
    case ImGuiAppComposerLayoutPreset_Review:  return ImGuiAppComposerLayoutVis_Tree | ImGuiAppComposerLayoutVis_Insp | ImGuiAppComposerLayoutVis_Code | ImGuiAppComposerLayoutVis_Live;
    case ImGuiAppComposerLayoutPreset_Observe: return ImGuiAppComposerLayoutVis_Code | ImGuiAppComposerLayoutVis_Live;
    default:                           return 0;
    }
}
static int ComposerLayoutVisFlags(ImGuiAppGraphDocData* doc)
{
    const ImGuiAppGraphViewState* v = ImGui::AppGraphViewState(&doc->Graph);
    return (v->TreeOpen ? ImGuiAppComposerLayoutVis_Tree : 0)
         | (v->InspOpen ? ImGuiAppComposerLayoutVis_Insp : 0)
         | (doc->CodeH > 0.0f ? ImGuiAppComposerLayoutVis_Code : 0)
         | (doc->ShowLive ? ImGuiAppComposerLayoutVis_Live : 0);
}
static void ComposerApplyLayoutPreset(ImGuiAppGraphDocData* doc, int preset)
{
    if (preset == ImGuiAppComposerLayoutPreset_None)
        return;
    const int m = ComposerLayoutPresetMask(preset);
    ImGuiAppGraphViewState* v = ImGui::AppGraphViewState(&doc->Graph);
    v->TreeOpen = (m & ImGuiAppComposerLayoutVis_Tree) != 0;
    v->InspOpen = (m & ImGuiAppComposerLayoutVis_Insp) != 0;
    doc->CodeH = (m & ImGuiAppComposerLayoutVis_Code) != 0 ? ImGui::GetFontSize() * 12.0f : 0.0f;
    doc->ShowLive = (m & ImGuiAppComposerLayoutVis_Live) != 0;
}

// Generate the whole-graph C++ to the header path + stamp the fresh baseline. Shared by the toolbar
// Generate button and the status-bar freshness zone (F32) so both take the identical road.
static void ComposerGenerateHeader(ImGuiAppGraphDocData* doc)
{
    ImGuiTextBuffer full;
    ImGui::AppGraphCodeGenerate(&doc->Graph, &full);
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

struct GraphDocControl : ImGuiAppControl<ImGuiAppGraphDocData, ImGuiAppNoTempData>
{
    virtual void OnInitialize(ImGuiApp* app, ImGuiAppGraphDocData* data) const override final
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
        data->RevealPanel = ImGuiAppComposerPanel_None;
        data->LinkErrSeqSeen = 0;
        data->LayoutSavedHash = 0;
        data->LayoutSaveT = 0.0f;
        data->Mirror      = app;     // ONE application: the mirror IS the app hosting this control
        data->Transport   = IM_NEW(ImGuiAppComposerTransport)();   // App-time scrubber (F29); heap, process lifetime
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
    virtual void OnUpdate(float dt, ImGuiAppGraphDocData* data, const ImGuiAppNoTempData*, const ImGuiAppNoTempData*) const override final
    {
        ComposerLayoutSaveIfChanged(data, dt);

        // Self-mirroring from inside our own update is safe: BuildAppLiveGraph only reads the object model.
        // Build the live mirror first so every panel reads the reconciled graph this frame.
        if (data->Mirror != nullptr)
        {
            ImGui::BuildAppLiveGraph(data->Mirror, &data->Graph);
        }

        // App-time transport (F29): while running, snapshot the live app's snapshottable (POD) controls each frame;
        // while frozen, hold + restore the scrubbed frame's bytes (opaque controls skipped, so only user app state
        // rewinds). F70: the LIVE ring's source is the PREVIEW app when a session runs, else the host live-mirror.
        if (data->Transport != nullptr)
        {
            ImGuiAppComposerTransport* tr = data->Transport;
            ImGuiAppEditorState* ed = ImGui::AppGraphEditorState(&data->Graph);
            ImGuiApp* preview_app = (ed != nullptr && ed->Preview != nullptr && ed->PreviewRun) ? ImGui::AppPreviewApp(ed->Preview) : nullptr;
            ImGuiApp* live_app = preview_app != nullptr ? preview_app : (data->ShowLive ? data->Mirror : nullptr);
            if (live_app != nullptr)
            {
                if (!tr->Frozen)
                {
                    ImGui::AppStateSnapshot(live_app, &tr->History);
                    tr->Frame = tr->History.Count - 1;   // live: follow the newest frame
                }
                else if (tr->History.Count > 0)
                {
                    tr->Frame = ImClamp(tr->Frame, 0, tr->History.Count - 1);
                    ImGui::AppStateRestore(live_app, &tr->History, tr->Frame);
                }
            }
        }

        // Publish the frame's derived values once; GraphDoc updates first (push order), so all consumers see the same values.
        ImGui::AppGraphSyncRevision(&data->Graph);   // one signature fold per frame -> Revision pulse + _SigCache
        data->FrameSig = data->Graph._SigCache;
        if (data->WriteMsg[0] && ImGui::AppGraphIsCodeStale(&data->Graph))
            data->WriteMsg[0] = 0;   // the "wrote/copied" confirmation stops being true the frame the graph diverges from disk
        {
            const ImVector<ImGuiAppGraphIssue>* issues = ImGui::AppGraphIssuesCached(&data->Graph);
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
            ImGui::AppGraphCodeGenerate(&data->Graph, &code);
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

        // Bootstrap staleness: an authored node with no live counterpart exists only in the design -- the document
        // is stale until Generate + recompile + relaunch. Core layers are the foundation representing the live
        // stack; Struct/Field nodes are data shapes carried by their control.
        int unbuilt = 0;
        for (int i = 0; i < data->Graph.Nodes.Size; i++)
        {
            const ImGuiAppNode* n = &data->Graph.Nodes.Data[i];
            if (n->IsLive)
                continue;
            if (ImAppNodeKindIsData(n->Kind))
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
// [SECTION] Playback debugger -- FILE-mode transport (F63)
//-----------------------------------------------------------------------------
// The App-time transport's FILE source: open a recorded QOI run through F62's AppRunOpen,
// scrub it via the shared AppRunViewShow (Count()/Show(int)) reader, and blit the decoded
// frame at the slider tick. All state rides ImGuiAppComposerTransport -- no TU globals.

// Adapt the QOI provider's frame decode to the core ImGuiAppRunFrameDecodeFn seam (user = the
// recording directory). The core index stays provider-agnostic; the demo names the provider.
static bool ComposerQoiDecodeFrame(void* user, int frame_ordinal, ImVector<char>* out_rgba, int* out_w, int* out_h)
{
    return ImGuiApp_ImplQoi_DecodeFrame((const char*)user, frame_ordinal, out_rgba, out_w, out_h);
}
#ifdef IMGUIX_HAS_LIBAV
static bool ComposerLibavDecodeFrame(void* user, int frame_ordinal, ImVector<char>* out_rgba, int* out_w, int* out_h)
{
    return ImGuiApp_ImplLibav_DecodeFrame((const char*)user, frame_ordinal, out_rgba, out_w, out_h);
}
#endif

// WAL command dispatches, bucketed by tick: the sibling <name>.wal's "[tick:N] ... execute command" lines
// correlate through F62's index (AppRunAttachWal fills run->Commands + WalFirst/WalCount). Optional -- the
// recording is authoritative; a missing WAL just omits markers. CommandTicks mirrors ticks for the timeline strip.
static void ComposerLoadWalCommandTicks(ImGuiAppComposerTransport* tr)
{
    tr->CommandTicks.clear();
    if (tr->Run == nullptr)
        return;
    char wal_path[300];
    ImFormatString(wal_path, IM_ARRAYSIZE(wal_path), "%s.wal", tr->RunName);
    ImGui::AppRunAttachWal(tr->Run, wal_path);
    for (int c = 0; c < tr->Run->Commands.Size; c++)
        tr->CommandTicks.push_back(tr->Run->Commands.Data[c].Tick);
}

static void ComposerCloseRun(ImGuiAppComposerTransport* tr)
{
    if (tr->Run != nullptr)
        ImGui::AppRunClose(tr->Run);
    tr->Run = nullptr;
    tr->FileView.Run = nullptr;
    tr->FileView.ShownImage = -1;
    tr->FileView.Scrub = 0;
    tr->CommandTicks.clear();
    tr->FrameTexTick = -1;   // the held texture no longer matches any tick
    tr->RunDir[0] = 0;
}

// F70 preview-session record (previewer-design section 10): drive the preview app under the frame dt and
// export an F61 run container via the meta-only writer -- the SAME records the video recorder embeds, minus
// pixels. Session state rides the editor object; pump once per advanced frame; stop finalizes then opens via AppRunOpen.
static void ComposerPreviewRecordPump(ImGuiAppEditorState* ed, float dt)
{
    if (ed->PreviewRec == nullptr || ed->Preview == nullptr)
        return;
    ImGuiApp* app = ImGui::AppPreviewApp(ed->Preview);
    ImGui::AppInputRecord(app, &ed->PreviewRecInput, dt);   // opt-in replay layer bridges non-snapshot ticks
    const ImU64 tick = ++ed->PreviewRecTick;
    const int snap_every = ed->PreviewRecSnapEvery > 0 ? ed->PreviewRecSnapEvery : 1;
    const bool snap = ((tick - 1) % (ImU64)snap_every) == 0;   // tick 1 + every N: nearest-snapshot restore points
    ImGui::AppMetaRecordTick(ed->PreviewRec, tick, (double)(tick - 1) / 60.0, snap);
}

static void ComposerPreviewRecordStop(ImGuiAppEditorState* ed, ImGuiAppComposerTransport* tr)
{
    if (ed->PreviewRec == nullptr)
        return;
    ImVector<char> meta;
    ImGui::AppMetaRecordEnd(ed->PreviewRec, &meta);   // appends the Digest, frees the recorder
    ed->PreviewRec = nullptr;

    if (ImFileHandle fh = ImFileOpen(ed->PreviewRecPath, "wb"))   // the exported container on disk
    {
        ImFileWrite(meta.Data, sizeof(char), (ImU64)meta.Size, fh);
        ImFileClose(fh);
    }

    // Close the loop in-app: the just-recorded take opens through the SAME AppRunOpen the file playback
    // uses, wired as the transport's FILE source (tick-only scrub -- a meta-only take carries no frames).
    if (tr != nullptr)
    {
        ComposerCloseRun(tr);
        tr->Run = ImGui::AppRunOpen(meta.Data, meta.Size);
        if (tr->Run != nullptr)
        {
            tr->FileView.Run = tr->Run;
            tr->FileView.Decode = nullptr;
            tr->FileView.DecodeUser = nullptr;
            tr->Source = ImGuiAppTransportSource_FileRun;
            ImStrncpy(tr->RunName, ed->PreviewRecPath, sizeof(tr->RunName));
            ImGui::AppRunViewShow(&tr->FileView, 0);   // land on the first tick
        }
        else
        {
            ImFormatString(tr->OpenErr, IM_ARRAYSIZE(tr->OpenErr), "Recorded preview take rejected by AppRunOpen.");
        }
    }
}

// Open RunName as a recorded take: an mp4 (<name>.mp4, when libav is built) or a QOI directory
// (<name>/NNNNNN.qoi). Extract the embedded meta stream, build the F62 index, wire the matching
// frame decoder. Frame images decode on demand at the scrub position (never a full preload).
static void ComposerOpenRun(ImGuiAppComposerTransport* tr)
{
    tr->OpenErr[0] = 0;
    ComposerCloseRun(tr);

    // A .meta path is a meta-only take (tick scrub, no frame images): the file IS the container.
    if (const char* ext = strrchr(tr->RunName, '.'); ext != nullptr && strcmp(ext, ".meta") == 0)
    {
        size_t raw_size = 0;
        void* raw = ImFileLoadToMemory(tr->RunName, "rb", &raw_size);
        if (raw == nullptr)
        {
            ImFormatString(tr->OpenErr, IM_ARRAYSIZE(tr->OpenErr), "No take at '%s'.", tr->RunName);
            return;
        }
        tr->Run = ImGui::AppRunOpen((const char*)raw, (int)raw_size);
        IM_FREE(raw);
        if (tr->Run == nullptr)
        {
            ImFormatString(tr->OpenErr, IM_ARRAYSIZE(tr->OpenErr), "Meta stream in '%s' rejected (bad/absent header).", tr->RunName);
            return;
        }
        tr->FileView.Run = tr->Run;
        tr->FileView.Decode = nullptr;
        tr->FileView.DecodeUser = nullptr;
        tr->FileView.Scrub = 0;
        tr->FrameTexTick = -1;
        ComposerLoadWalCommandTicks(tr);
        ImGui::AppRunViewShow(&tr->FileView, 0);
        return;
    }

    const int embed_rows = ImGuiAppAVEncodeConfig().EmbedRows;   // the take's default strip depth
    ImVector<char> meta;
    bool is_mp4 = false;
    char mp4[300];
    ImFormatString(mp4, IM_ARRAYSIZE(mp4), "%s.mp4", tr->RunName);
#ifdef IMGUIX_HAS_LIBAV
    if (ImGuiApp_ImplLibav_ExtractEmbeddedMeta(mp4, embed_rows, &meta))
        is_mp4 = true;
#endif
    if (!is_mp4 && !ImGuiApp_ImplQoi_ExtractEmbeddedMeta(tr->RunName, embed_rows, &meta))
    {
        ImFormatString(tr->OpenErr, IM_ARRAYSIZE(tr->OpenErr), "No run at '%s' (looked for %s.mp4 and a <dir>/NNNNNN.qoi sequence).", tr->RunName, tr->RunName);
        return;
    }
    ImGuiAppRunIndex* run = ImGui::AppRunOpen(meta.Data, meta.Size);
    if (run == nullptr)
    {
        ImFormatString(tr->OpenErr, IM_ARRAYSIZE(tr->OpenErr), "Meta stream in '%s' rejected (bad/absent header).", tr->RunName);
        return;
    }
    tr->Run = run;
    tr->FileView.Run = run;
#ifdef IMGUIX_HAS_LIBAV
    if (is_mp4)
    {
        ImStrncpy(tr->RunDir, mp4, sizeof(tr->RunDir));
        tr->FileView.Decode = ComposerLibavDecodeFrame;
    }
    else
#endif
    {
        ImStrncpy(tr->RunDir, tr->RunName, sizeof(tr->RunDir));
        tr->FileView.Decode = ComposerQoiDecodeFrame;
    }
    tr->FileView.DecodeUser = tr->RunDir;
    tr->FileView.Scrub = 0;
    tr->FrameTexTick = -1;
    ComposerLoadWalCommandTicks(tr);
    ImGui::AppRunViewShow(&tr->FileView, 0);   // land on the first tick
}

static ImTextureRef ComposerUploadRgbaTexture(ImTextureData** tex, int* tw, int* th, const unsigned char* rgba, int w, int h);   // fwd

// Upload the FILE view's decoded RGBA into the transport's GPU texture. ONE blit path (shared with
// the DLL preview frame): the tick guard keeps it a per-scrub upload, never per-frame.
static bool ComposerSyncFrameTexture(ImGuiAppComposerTransport* tr)
{
    const ImGuiAppRunView* v = &tr->FileView;
    const int need = v->Width * v->Height * 4;
    if (v->ShownImage < 0 || v->Width <= 0 || v->Height <= 0 || v->Pixels.Size < need)
        return false;
    if (tr->FrameTex != nullptr && tr->FrameTex->Status != ImTextureStatus_Destroyed
        && tr->FrameTexTick == (int)v->ShownTick && tr->FrameTexW == v->Width && tr->FrameTexH == v->Height)
        return true;   // tick-current pixels already on the GPU
    ComposerUploadRgbaTexture(&tr->FrameTex, &tr->FrameTexW, &tr->FrameTexH,
                              (const unsigned char*)v->Pixels.Data, v->Width, v->Height);
    tr->FrameTexTick = (int)v->ShownTick;
    return true;
}

// Upload a host-owned RGBA32 buffer into an ImTextureData (created once per size, then full-rect updated),
// the F63 blit path reused for the F78.5 in-panel DLL frame. *tex/*tw/*th are the caller's cached texture
// and its dimensions. Returns the texture ref to Image().
static ImTextureRef ComposerUploadRgbaTexture(ImTextureData** tex, int* tw, int* th, const unsigned char* rgba, int w, int h)
{
    if (*tex != nullptr && (*tex)->Status != ImTextureStatus_Destroyed && (*tw != w || *th != h))
    {
        (*tex)->WantDestroyNextFrame = true;   // size changed: hand the old texture back, take a fresh object
        *tex = nullptr;
    }
    if (*tex == nullptr)
    {
        *tex = IM_NEW(ImTextureData)();
        ImGui::RegisterUserTexture(*tex);
        *tw = w;
        *th = h;
    }
    ImTextureData* t = *tex;
    if (t->Status == ImTextureStatus_Destroyed)
    {
        t->Create(ImTextureFormat_RGBA32, w, h);
        memcpy(t->GetPixels(), rgba, (size_t)w * h * 4);
    }
    else
    {
        memcpy(t->GetPixels(), rgba, (size_t)w * h * 4);
        t->UpdateRect.x = 0;
        t->UpdateRect.y = 0;
        t->UpdateRect.w = (unsigned short)w;
        t->UpdateRect.h = (unsigned short)h;
        t->UsedRect = t->UpdateRect;
        t->SetStatus(ImTextureStatus_WantUpdates);
    }
    return t->GetTexRef();
}

// FILE marks -> the ONE transport rail (ST2.1): per-tick markers from the F62 index (snapshot
// points, opt-in input frames, chain divergence) plus WAL command dispatches. Returns picked
// scrub index (-1 = unchanged); the rail owns hit-testing and the notch grammar.
static int ComposerPlaybackTimeline(ImGuiAppComposerTransport* tr, float em, const ImGuiStyle& style)
{
    IM_UNUSED(style);
    const int count = ImGui::AppRunViewCount(&tr->FileView);
    if (count <= 0)
        return -1;
    const ImGuiAppRunIndex* run = tr->Run;
    const ImU64 first_tick = run->Ticks.Data[0].Tick;

    ImVector<ImGuiAppRailMark> marks;
    auto mark = [&](int idx, int kind)
    {
        ImGuiAppRailMark m;
        m.Index = idx;
        m.Kind = kind;
        marks.push_back(m);
    };
    for (int i = 0; i < count; i++)
        if (run->Ticks.Data[i].InputOffset >= 0)
            mark(i, ImGuiAppRailMark_Input);
    for (int s = 0; s < run->SnapshotTicks.Size; s++)
        mark(run->SnapshotTicks.Data[s], ImGuiAppRailMark_Snapshot);
    for (int c = 0; c < tr->CommandTicks.Size; c++)
        mark((int)(tr->CommandTicks.Data[c] - first_tick), ImGuiAppRailMark_Command);
    if (run->Stats.ChainDivergesAt >= 0)   // first recording-integrity divergence (io-frame ordinal == tick index)
        mark(run->Stats.ChainDivergesAt, ImGuiAppRailMark_Divergence);

    int scrub = tr->FileView.Scrub;
    return ImGui::AppBlTransportRail("###filestrip", ImVec2(ImMax(em * 8.0f, ImGui::GetContentRegionAvail().x), em * 1.6f),
                                     count, &scrub, marks.Data, marks.Size, false) ? scrub : -1;
}

// FILE playback window: open/close a run, timeline strip, exact-tick step + slider, decoded frame + per-tick
// readout. F64 reconstruction restores recorded state into the live Mirror bracketed by a save/restore of
// its own state; replay-needed ticks are NOT reconstructed live (OnUpdate would re-dispatch) -- degrade, don't fake.
static void ComposerRenderStateAtTick(ImGuiAppComposerTransport* tr, ImGuiApp* mirror, int scrub)
{
    const ImGuiAppRunIndex* run = tr->Run;
    const ImGuiAppRunTick* cur = ImGui::AppRunTickAt(run, scrub);
    if (cur == nullptr)
        return;

    ImGui::SeparatorText("State @ tick");

    // Command dispatches at this tick (the WAL slice, F64 command log).
    if (cur->WalCount > 0 && cur->WalFirst >= 0)
    {
        ImGui::TextUnformatted("dispatch:");
        for (int c = cur->WalFirst; c < cur->WalFirst + cur->WalCount && c < run->Commands.Size; c++)
        {
            ImGui::SameLine();
            ImGui::Text("command %d", run->Commands.Data[c].CommandId);
        }
    }
    else
    {
        ImGui::TextDisabled("dispatch: (none this tick)");
    }

    // Value reconstruction: the core's own identity gate first (ST2.4: one verdict source), then
    // restore-nearest(+replay). Live-mirror only.
    if (!ImGui::AppRunIdentityMatches(mirror, run))
    {
        ImGui::TextDisabled("values: composition/schema differ from this build -- reconstruction refused");
        return;
    }
    if (run->SnapshotTicks.Size == 0)
    {
        ImGui::TextDisabled("values: raw-io take (no snapshots) -- image + io + command log only");
        return;
    }
    if (cur->SnapshotOffset < 0)
    {
        ImGui::TextDisabled("values: available at snapshot ticks (gold) -- scrub to one to inspect");
        return;
    }

    // Restore-only (this tick is a snapshot): bracket the live Mirror's state, reconstruct, read, restore.
    // The scratch restore is read-only debugger state, not a real transition -- silence the Mirror's WAL.
    ImGuiAppWAL* backup_wal = mirror->WAL;
    mirror->WAL = nullptr;
    ImGuiAppStateHistory live;
    const bool saved = ImGui::AppStateSnapshot(mirror, &live);
    ImGuiAppRunState st;
    const bool ok = ImGui::AppRunStateAtTick(mirror, run, scrub, &st);
    const ImGuiID recon_hash = ImGui::AppStateHash(mirror);
    if (saved)
        ImGui::AppStateRestore(mirror, &live, 0);   // return the Mirror to its live state
    mirror->WAL = backup_wal;

    if (ok)
        ImGui::Text("values: restored snapshot   hash 0x%08X %s   %d Persist/Temp slot(s)",
                    (unsigned)recon_hash,
                    recon_hash == st.RecordedStateHash ? "== recorded" : "!= recorded",
                    live.SlotIds.Size);
    else
        ImGui::TextDisabled("values: reconstruction unavailable at this tick");
}

// Replay tab body (ST2.3: docked panel under the contract, never a floating window): open/close a
// run, the transport rail, exact-tick stepping, per-tick readout, F64 state-at-tick, frame blit.
static void ComposerReplayTabBody(ImGuiAppComposerTransport* tr, ImGuiApp* mirror, float em, const ImGuiStyle& style)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Run");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 16.0f);
    ImGui::InputText("###filerunname", tr->RunName, IM_ARRAYSIZE(tr->RunName));
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Open###filerunopen"))
        ComposerOpenRun(tr);
    ImGui::SetItemTooltip("Open this recorded run (QOI take directory)");
    if (tr->Run != nullptr)
    {
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_XMARK "  Close###filerunclose"))
            ComposerCloseRun(tr);
    }
    if (tr->OpenErr[0])
        ImGui::TextColored(ComposerCol(ImGui::AppComposerGetStyle()->ErrorText), "%s", tr->OpenErr);

    if (tr->Run == nullptr)
    {
        ImGui::TextDisabled("No run open. Enter a recording path and Open (or record a preview take).");
        return;
    }

    const int count = ImGui::AppRunViewCount(&tr->FileView);

    // Identity + integrity line.
    ImGui::Separator();
    ImGui::Text("composition 0x%08X   schema 0x%08X   %d tick(s)   chain %s   digest %s",
                (unsigned)tr->Run->Identity.CompositionID, (unsigned)tr->Run->Identity.SchemaHash, count,
                tr->Run->Stats.ChainOk ? "ok" : "BROKEN",
                tr->Run->Stats.DigestState == 0 ? "ok" : tr->Run->Stats.DigestState == 1 ? "missing" : "MISMATCH");

    // Step-back | rail | step-forward -- integer tick indices, so every landing is an exact tick.
    int scrub = tr->FileView.Scrub;
    if (ImGui::Button(ICON_FA_BACKWARD_STEP "###filescrubback"))
        scrub = ImMax(0, scrub - 1);
    ImGui::SetItemTooltip("Step back one tick");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FORWARD_STEP "###filescrubfwd"))
        scrub = count > 0 ? ImMin(count - 1, scrub + 1) : 0;
    ImGui::SetItemTooltip("Step forward one tick");

    const int strip_pick = ComposerPlaybackTimeline(tr, em, style);
    if (strip_pick >= 0)
        scrub = strip_pick;

    if (scrub != tr->FileView.Scrub)
        ImGui::AppRunViewShow(&tr->FileView, scrub);   // shared reader: decode + ShownTick = Ticks[scrub].Tick

    // Per-tick readout: the shown tick IS the slider tick (Show addresses Ticks[i].Tick directly).
    const ImGuiAppRunTick* cur = ImGui::AppRunTickAt(tr->Run, tr->FileView.Scrub);
    if (cur != nullptr)
    {
        const bool has_input = cur->InputOffset >= 0;
        const bool has_snap  = cur->SnapshotOffset >= 0;
        ImGui::Text("tick %llu   frame %d/%d   image #%d   t=%.3fs   hash 0x%08X",
                    (unsigned long long)tr->FileView.ShownTick, tr->FileView.Scrub, count - 1,
                    tr->FileView.ShownImage, cur->TimeSec, (unsigned)cur->StateHash);
        ImGui::SameLine();
        ImGui::TextDisabled("%s%s", has_snap ? "[snapshot] " : "", has_input ? "[input] " : "");
    }

    // F64 state-at-tick: the reconstructed values + this tick's command dispatches.
    ComposerRenderStateAtTick(tr, mirror, tr->FileView.Scrub);

    // Decode+blit the frame image at the scrub tick.
    if (ComposerSyncFrameTexture(tr) && tr->FrameTex != nullptr)
    {
        const float avail_w = ImGui::GetContentRegionAvail().x;
        const float avail_h = ImGui::GetContentRegionAvail().y;
        const float sx = avail_w / (float)tr->FileView.Width;
        const float sy = avail_h / (float)tr->FileView.Height;
        const float sc = ImMax(0.05f, ImMin(sx, sy));
        const ImVec2 img_sz = ImVec2(tr->FileView.Width * sc, tr->FileView.Height * sc);
        ImGui::Image(tr->FrameTex->GetTexRef(), img_sz);
    }
    else
    {
        ImGui::TextDisabled("(no frame image at this tick)");
    }
}

//-----------------------------------------------------------------------------
// [SECTION] Composer toolbar (flow-ordered: compose -> iterate -> persist -> produce | observe)
//-----------------------------------------------------------------------------

// Declared ImGuiAppGraphDocData dependency orders updates producer-first; the Doc pointer is the write half only.
struct ImGuiAppToolbarData
{
    ImGuiAppGraphDocData* Doc;
};
// Actions captured in OnDraw, applied in OnUpdate -- TempData is the edit-intent bus.
struct ImGuiAppToolbarTempData
{
    bool Save;
    bool Load;
    bool WriteHeader;
    bool ToggleCode;
    bool ToggleLive;     // Live-eye toggle clicked this frame (OnUpdate derives the new state)
    bool ToggleTree;     // outliner sidebar toggle clicked this frame
    bool ToggleInsp;     // Inspector sidebar toggle clicked this frame
    bool OpenShortcuts;  // reveal the project inspector's Shortcuts section (palette verb)
    bool Undo;           // undo / redo edit-intents (applied in OnUpdate)
    bool Redo;
    bool Diff;           // diff current graph's codegen vs the saved-on-disk graph -> clipboard
    bool HistoryGotoSet; // history dropdown picked a step
    int  HistoryGotoIdx;
    bool CopyCode;       // Generate menu: copy the generated C++ to the clipboard
    int  RevealPanel;    // ImGuiAppComposerPanel_* intent from a palette pick (0 = none)
    bool AddNode;        // toolbar "+ Add" -> open the canvas add palette (the loop's entry point)
    int  ApplyPreset;    // ImGuiAppComposerLayoutPreset_* intent from a preset button (0 = none)
};
struct ImGuiAppToolbarControl : ImGuiAppControl<ImGuiAppToolbarData, ImGuiAppToolbarTempData, ImGuiAppGraphDocData>
{
    virtual void OnInitialize(ImGuiApp* app, ImGuiAppToolbarData* data, const ImGuiAppGraphDocData*) const override final
    {
        data->Doc = GetGraphDoc(app);   // write half only; reads flow through the declared dep
    }

    virtual void OnUpdate(float dt, ImGuiAppToolbarData* data, const ImGuiAppToolbarTempData* temp_data, const ImGuiAppToolbarTempData* last_temp_data, const ImGuiAppGraphDocData*) const override final
    {
        IM_UNUSED(dt);
        ImGuiAppGraphDocData* doc = data->Doc;
        if (temp_data->Save)
        {
            ImGui::AppGraphSave(doc->GraphPath, &doc->Graph);
            DocLog(doc, 0, "saved graph -> %s", doc->GraphPath);
        }
        if (temp_data->Load)
        {
            ImGui::AppGraphLoad(doc->GraphPath, &doc->Graph);
            ImGui::AppGraphEnsureFoundation(&doc->Graph);
            ImGui::AppGraphRequestFitAll(&doc->Graph);
            DocLog(doc, 0, "loaded graph <- %s", doc->GraphPath);
        }
        if (temp_data->WriteHeader)
            ComposerGenerateHeader(doc);
        if (temp_data->OpenShortcuts)
        {
            // The Shortcuts section lives on the project inspector: empty the selection so it shows.
            ImGui::AppGraphViewState(&doc->Graph)->InspOpen = true;
            doc->Selection = 0;
            doc->Graph.Selection.resize(0);
        }
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
        if (temp_data->ApplyPreset != ImGuiAppComposerLayoutPreset_None)
        {
            ComposerApplyLayoutPreset(doc, temp_data->ApplyPreset);
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
            ImGui::AppGraphCodeGenerate(&doc->Graph, &full);
            ImGui::SetClipboardText(full.c_str());
            ImFormatString(doc->WriteMsg, IM_ARRAYSIZE(doc->WriteMsg), "generated C++ -> clipboard");
            DocLog(doc, 0, "copied generated C++ -> clipboard");
        }
        if (temp_data->RevealPanel != ImGuiAppComposerPanel_None)
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
            if (ImGui::AppGraphLoad(doc->GraphPath, &saved))
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

    virtual void OnDraw(const ImGuiAppToolbarData* data, ImGuiAppToolbarTempData* temp_data, const ImGuiAppGraphDocData*) const override final
    {
        ImGuiAppGraphDocData*     doc       = data->Doc;
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
            const bool  fresh = ImGui::AppGraphIsCodeFresh(&doc->Graph);
            // Stable ### id: the visible label swings Generate/Generated with health, but the widget keeps one
            // identity (no focus/press churn across states; test-addressable).
            const char* gen_label = nerr > 0 ? ICON_FA_TRIANGLE_EXCLAMATION "  Generate###generate" : fresh ? ICON_FA_CHECK "  Generated###generate" : ICON_FA_FILE_EXPORT "  Generate###generate";
            ImGui::PushStyleColor(ImGuiCol_Button, nerr > 0 ? ComposerCol(ImGui::AppComposerGetStyle()->HealthBlocked)
                                             : fresh    ? ComposerCol(ImGui::AppComposerGetStyle()->HealthOk)
                                                        : ComposerCol(ImGui::AppComposerGetStyle()->HealthStale));
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
                ImGui::PushStyleColor(ImGuiCol_Text, ComposerCol(ImGui::AppComposerGetStyle()->Gold));
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
                    ImGui::AppGraphCodeGenerate(&doc->Graph, &code);   // pure read; the list regenerates while the popup is open
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
                    ImGui::PushStyleColor(ImGuiCol_Button, ComposerCol(nerr > 0 ? ImGui::AppComposerGetStyle()->HealthBlocked : ImGui::AppComposerGetStyle()->HealthStale));
                    char prob_lbl[48];
                    ImFormatString(prob_lbl, IM_ARRAYSIZE(prob_lbl), ICON_FA_TRIANGLE_EXCLAMATION "  %d###problems", nprob);
                    if (ImGui::Button(prob_lbl))
                    {
                        temp_data->RevealPanel = ImGuiAppComposerPanel_Output;
                        ImGuiAppEditorState* ed = ImGui::AppGraphEditorState(&doc->Graph);
                        ed->OutputShowErr  = true;
                        ed->OutputShowWarn = true;
                        ed->OutputShowInfo = false;   // filter to problems: hide the info/log stream
                    }
                    ImGui::PopStyleColor();
                    ImGui::SetItemTooltip("%d problem(s): %d error(s), %d warning(s) -- click to open Output", nprob, nerr, nwarn_v);
                }
            }

            temp_data->ApplyPreset = ImGuiAppComposerLayoutPreset_None;

            // App-time transport (F29/F63): a SOURCE switch (LIVE ring vs a recorded FILE run) then the
            // source-agnostic scrub chrome. Flow-placed (left of the right-aligned observe cluster); only
            // offered with the live mirror. LIVE freezes + scrubs the running app; FILE opens the playback window.
            if (show_live && doc->Transport != nullptr)
            {
                ImGuiAppComposerTransport* tr = doc->Transport;
                const bool file_src = tr->Source == ImGuiAppTransportSource_FileRun;
                EditorToolSep(em);
                // Source switch (icon-only to keep the flow row narrow, so the right-aligned observe cluster
                // does not shift): toggle LIVE ring <-> a recorded FILE run; FILE reveals the Replay panel.
                if (file_src)
                    ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
                if (ImGui::Button(file_src ? ICON_FA_FILM "###apptimesrc" : ICON_FA_TOWER_BROADCAST "###apptimesrc"))
                {
                    tr->Source = file_src ? ImGuiAppTransportSource_LiveRing : ImGuiAppTransportSource_FileRun;
                    if (tr->Source == ImGuiAppTransportSource_FileRun)
                        temp_data->RevealPanel = ImGuiAppComposerPanel_Replay;
                }
                if (file_src)
                    ImGui::PopStyleColor();
                ImGui::SetItemTooltip(file_src ? "Transport source: recorded FILE run (click for LIVE app ring)"
                                         : "Transport source: LIVE app ring (click for a recorded FILE run)");

                if (!file_src)
                {
                    ImGui::SameLine();
                    const int frames = tr->History.Count;
                    const bool was_frozen = tr->Frozen;   // capture: the button click below flips Frozen mid-Push/Pop
                    if (was_frozen)
                        ImGui::PushStyleColor(ImGuiCol_Button, ComposerCol(ImGui::AppComposerGetStyle()->HealthStale));   // amber: app time engaged
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
                        // The ONE transport rail, compact rendering (ST2.2): same widget as Replay's.
                        int f = frames > 0 ? ImClamp(tr->Frame, 0, frames - 1) : 0;
                        if (ImGui::AppBlTransportRail("###apptimescrub", ImVec2(em * 8.0f, ImGui::GetFrameHeight()), frames, &f, nullptr, 0, false))
                            tr->Frame = f;
                        ImGui::SetItemTooltip("Scrub App-time frame %d / %d (0 = oldest)", f, frames > 0 ? frames - 1 : 0);
                        ImGui::SameLine();
                        if (ImGui::Button(ICON_FA_FORWARD_STEP "###apptimefwd"))
                            tr->Frame = frames > 0 ? ImMin(frames - 1, tr->Frame + 1) : 0;
                        ImGui::SetItemTooltip("Step forward one frame");
                    }
                }
                else
                {
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_FILM "###apptimefile"))
                        temp_data->RevealPanel = ImGuiAppComposerPanel_Replay;
                    ImGui::SetItemTooltip("Open the Replay panel (recorded-run transport)");
                }
            }

            // -- observe (right-aligned): bootstrap-state readout + panel toggles. The Live eye
            // always reflects THE running app -- there is exactly one.
            const bool  unwritten = doc->Graph.GenSignature != 0 && ImGui::AppGraphIsCodeStale(&doc->Graph);
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

            // Layout preset (F36): compact dropdown; its label shows the active preset when the current
            // visibilities match one. A view control, so it lives in the right cluster and the flow row is left alone.
            int cur_preset = ImGuiAppComposerLayoutPreset_None;
            {
                const int cur = ComposerLayoutVisFlags(doc);
                for (int p = ImGuiAppComposerLayoutPreset_Compose; p <= ImGuiAppComposerLayoutPreset_Observe; p++)
                    if (cur == ComposerLayoutPresetMask(p))
                        cur_preset = p;
            }
            static const char* preset_names[] = { "Layout", "Compose", "Review", "Observe" };
            const char* layout_lbl = ICON_FA_TABLE_COLUMNS "###layoutmenu";   // icon-only: the toolbar has no room for a label

            const float pad2 = style.FramePadding.x * 2.0f;
            const float cluster_w = ImGui::CalcTextSize(layout_lbl, ImGui::FindRenderedTextEnd(layout_lbl)).x + pad2 + style.ItemSpacing.x
                              + ImGui::CalcTextSize(code_lbl).x + pad2
                              + ImGui::CalcTextSize(live_lbl, ImGui::FindRenderedTextEnd(live_lbl)).x + pad2 + style.ItemSpacing.x
                              + ImGui::CalcTextSize(sync_lbl, ImGui::FindRenderedTextEnd(sync_lbl)).x + pad2 + style.ItemSpacing.x
                              + ImGui::CalcTextSize(tree_lbl, ImGui::FindRenderedTextEnd(tree_lbl)).x + pad2 + style.ItemSpacing.x
                              + ImGui::CalcTextSize(insp_lbl, ImGui::FindRenderedTextEnd(insp_lbl)).x + pad2 + style.ItemSpacing.x;
            ImGui::SameLine(ImMax(ImGui::GetCursorPosX() + em, ImGui::GetContentRegionMax().x - cluster_w - em * 0.2f));

            // Amber ink when authored work is not compiled into the running app; quiet when in sync.
            const ImVec4 sync_ink = stale ? ImLerp(DEMO_GOLD, style.Colors[ImGuiCol_Text], 0.25f) : style.Colors[ImGuiCol_TextDisabled];
            ImGui::PushStyleColor(ImGuiCol_Text, sync_ink);
            if (ImGui::Button(sync_lbl))
                temp_data->RevealPanel = ImGuiAppComposerPanel_Code;
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
            ImGui::SameLine();

            // Layout dropdown (F36): rightmost in the observe cluster; the picked preset is applied in OnUpdate.
            if (cur_preset != ImGuiAppComposerLayoutPreset_None)
                ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button(layout_lbl))
                ImGui::OpenPopup("##layout_presets");
            if (cur_preset != ImGuiAppComposerLayoutPreset_None)
                ImGui::PopStyleColor();
            if (cur_preset != ImGuiAppComposerLayoutPreset_None)
                ImGui::SetItemTooltip("Workspace layout: %s", preset_names[cur_preset]);
            else
                ImGui::SetItemTooltip("Workspace layout presets (Compose / Review / Observe)");
            if (ImGui::BeginPopup("##layout_presets"))
            {
                if (ImGui::MenuItem("Compose###preset-compose", nullptr, cur_preset == ImGuiAppComposerLayoutPreset_Compose))
                    temp_data->ApplyPreset = ImGuiAppComposerLayoutPreset_Compose;
                if (ImGui::MenuItem("Review###preset-review", nullptr, cur_preset == ImGuiAppComposerLayoutPreset_Review))
                    temp_data->ApplyPreset = ImGuiAppComposerLayoutPreset_Review;
                if (ImGui::MenuItem("Observe###preset-observe", nullptr, cur_preset == ImGuiAppComposerLayoutPreset_Observe))
                    temp_data->ApplyPreset = ImGuiAppComposerLayoutPreset_Observe;
                ImGui::EndPopup();
            }

            // Palette pick from last frame's canvas folds into the same temp flags the buttons set.
            switch (ImGui::AppGraphConsumeHostCommand(&doc->Graph))
            {
            case ImGuiAppComposerHostCmd_Save:         temp_data->Save = true; break;
            case ImGuiAppComposerHostCmd_Load:         temp_data->Load = true; break;
            case ImGuiAppComposerHostCmd_Generate:     temp_data->WriteHeader = true; break;
            case ImGuiAppComposerHostCmd_CopyCode:     temp_data->CopyCode = true; break;
            case ImGuiAppComposerHostCmd_Diff:         temp_data->Diff = true; break;
            case ImGuiAppComposerHostCmd_PanelCode:    temp_data->RevealPanel = ImGuiAppComposerPanel_Code; break;
            case ImGuiAppComposerHostCmd_PanelProject: temp_data->RevealPanel = ImGuiAppComposerPanel_Project; break;
            case ImGuiAppComposerHostCmd_PanelPreview: temp_data->RevealPanel = ImGuiAppComposerPanel_Preview; break;
            case ImGuiAppComposerHostCmd_PanelOutput:  temp_data->RevealPanel = ImGuiAppComposerPanel_Output; break;
            case ImGuiAppComposerHostCmd_ToggleLive:   temp_data->ToggleLive = true; break;
            case ImGuiAppComposerHostCmd_Shortcuts:    temp_data->OpenShortcuts = true; break;
            case ImGuiAppComposerHostCmd_PanelReplay:  temp_data->RevealPanel = ImGuiAppComposerPanel_Replay; break;
            default: break;
            }

            // Phase captions: dim small-type row naming the flow under each cluster.
            {
                static const char* caps[4] = { "compose", "iterate", "persist", "produce" };
                ImGui::PushFont(nullptr, ImGui::GetFontSize() * ImGui::AppComposerGetMotion()->TypeCaption);   // F39 caption tier (was 0.78)
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
typedef int ImGuiAppComposerPillState;
enum ImGuiAppComposerPillState_ { ImGuiAppComposerPillState_Neutral, ImGuiAppComposerPillState_Ok, ImGuiAppComposerPillState_Warn, ImGuiAppComposerPillState_Err };

// State -> style-table row: the pill palette is the one chrome table, never a local triple.
static ImU32 ComposerPillRow(ImGuiAppComposerPillState s)
{
    switch (s)
    {
    case ImGuiAppComposerPillState_Ok:   return ImGui::AppComposerGetStyle()->StatusOk;
    case ImGuiAppComposerPillState_Warn: return ImGui::AppComposerGetStyle()->SevWarn;
    case ImGuiAppComposerPillState_Err:  return ImGui::AppComposerGetStyle()->ErrorText;
    default:                return ImGui::GetColorU32(ImGuiCol_TextDisabled);
    }
}

static ImVec4 ComposerPillColor(ImGuiAppComposerPillState s) { return ComposerCol(ComposerPillRow(s)); }

// The shared draw-list pill (AppBl family): one rounded grammar across strip facts and readouts.
static bool ComposerStatusPill(const char* id, ImGuiAppComposerPillState s, const char* label)
{
    return ImGui::AppBlStatusPill(id, ComposerPillRow(s), label);
}

// Rendered last -> window bottom. Keymap hints left, document counts right; all right-side text is
// derived in OnUpdate, OnDraw only lays out text.
struct ImGuiAppStatusStripData
{
    ImGuiAppGraphDocData* Doc;
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
struct ImGuiAppStatusStripTempData
{
    bool SelectCycle;   // the Select verb was clicked this frame -> jump selection to the cycle nodes (applied next update)
    bool SelectScope;   // F32 breadcrumb zone -> select the current scope owner
    bool RevealTree;    // F32 counts zone     -> reveal the outliner
    bool ToggleLive;    // F32 mirror zone     -> toggle the live mirror
    bool Generate;      // F32 freshness zone  -> generate the header
};
struct ImGuiAppStatusStripControl : ImGuiAppControl<ImGuiAppStatusStripData, ImGuiAppStatusStripTempData, ImGuiAppGraphDocData>
{
    virtual void OnInitialize(ImGuiApp* app, ImGuiAppStatusStripData* data, const ImGuiAppGraphDocData*) const override final
    {
        data->Doc = GetGraphDoc(app);
        data->CycleMsg[0] = 0;
        data->CycleCount  = 0;
        data->CycleSig    = 0;
    }

    virtual void OnUpdate(float dt, ImGuiAppStatusStripData* data, const ImGuiAppStatusStripTempData* temp_data, const ImGuiAppStatusStripTempData*, const ImGuiAppGraphDocData*) const override final
    {
        IM_UNUSED(dt);
        ImGuiAppGraphDocData* doc = data->Doc;

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
            int nw = 0, ns = 0, nt = 0;
            if (a->DisplayLayer != nullptr)
                for (const ImGuiAppNodeBase* node : a->DisplayLayer->Children)
                {
                    nw += node->Kind == ImGuiAppNodeKind_Window;
                    ns += node->Kind == ImGuiAppNodeKind_Sidebar;
                }
            if (a->TaskLayer != nullptr)
                for (const ImGuiAppNodeBase* node : a->TaskLayer->Children)
                    nt += node->Kind == ImGuiAppNodeKind_Task;
            ImFormatString(data->MirrorCounts, IM_ARRAYSIZE(data->MirrorCounts), "L%d W%d S%d T%d", a->Layers.Size, nw, ns, nt);
            data->MirrorInit = a->Layers.Size > 0;   // "composed"; Initialized is the platform flag
        }

        ImGui::AppGraphSelectionBreadcrumb(&doc->Graph, doc->Selection, data->Breadcrumb, IM_ARRAYSIZE(data->Breadcrumb));
        ImStrncpy(data->Msg, doc->WriteMsg, IM_ARRAYSIZE(data->Msg));

        // Data-dependency cycle (F21): recompute on signature change; surface a name + the node set the
        // Select verb jumps to. Applying the recorded Select click is a model write, so it lands here in
        // the update pass, not in OnDraw.
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
            doc->TreeW = doc->TreeW > 0.0f ? 0.0f : ImGui::GetFontSize() * 16.0f;   // show / hide the outliner column (EditorBody's default width)
        if (temp_data->ToggleLive)
            doc->ShowLive = !doc->ShowLive;
        if (temp_data->Generate)
            ComposerGenerateHeader(doc);
    }

    virtual void OnDraw(const ImGuiAppStatusStripData* data, ImGuiAppStatusStripTempData* temp_data, const ImGuiAppGraphDocData* doc_dep) const override final
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
                const bool pressed = ImGui::InvisibleButton(id, ImVec2(ImClamp(sz.x, em, slot), ImGui::GetFrameHeight()));
                const bool hov = ImGui::IsItemHovered();
                if (hov)
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::GetWindowDrawList()->AddText(ImVec2(p.x, p.y + style.FramePadding.y),
                                                    ImGui::GetColorU32(hov ? style.Colors[ImGuiCol_Text] : col), txt);
                return pressed;
            };
            const float slot = em * 6.5f;   // each right zone owns ~6.5em before the next anchor (7em apart)

            // HEALTH pill (F33): the whole graph's state in the shared pill grammar. Cycle -> err + name and a
            // click selects the cycle (folds in F21's Select verb); errors -> err "codegen blocked"; warnings
            // -> warn; else ok "graph ok". The transient keymap notice rides the feedback slot, not here.
            ImGuiAppComposerPillState hstate = ImGuiAppComposerPillState_Ok;
            char hlabel[96] = "graph ok";
            if (data->CycleCount > 0)
            {
                hstate = ImGuiAppComposerPillState_Err;
                ImFormatString(hlabel, IM_ARRAYSIZE(hlabel), ICON_FA_TRIANGLE_EXCLAMATION " %s", data->CycleMsg);
            }
            else if (doc_dep->NumErrors > 0)
            {
                hstate = ImGuiAppComposerPillState_Err;
                ImFormatString(hlabel, IM_ARRAYSIZE(hlabel), ICON_FA_TRIANGLE_EXCLAMATION " codegen blocked");
            }
            else if (doc_dep->NumWarnings > 0)
            {
                hstate = ImGuiAppComposerPillState_Warn;
                ImFormatString(hlabel, IM_ARRAYSIZE(hlabel), "%d warning%s", doc_dep->NumWarnings, doc_dep->NumWarnings == 1 ? "" : "s");
            }
            ImGui::AlignTextToFramePadding();
            // Id encodes the state so the state is externally observable (test) and the pill re-identifies when
            // it changes class.
            const char* hid = hstate == ImGuiAppComposerPillState_Err ? "health-err" : hstate == ImGuiAppComposerPillState_Warn ? "health-warn" : "health-ok";
            if (ComposerStatusPill(hid, hstate, hlabel) && data->CycleCount > 0)
                temp_data->SelectCycle = true;
            if (data->CycleCount > 0)
                ImGui::SetItemTooltip("Dependency cycle blocks codegen -- click to select the %d node(s)", data->CycleCount);
            else if (hstate == ImGuiAppComposerPillState_Err)
                ImGui::SetItemTooltip("%d error(s) in the graph -- see Output", doc_dep->NumErrors);
            else if (hstate == ImGuiAppComposerPillState_Warn)
                ImGui::SetItemTooltip("%d warning(s) in the graph", doc_dep->NumWarnings);
            else
                ImGui::SetItemTooltip("Graph validates clean");

            // PERF pill (F33): FPS + frame ms; tooltip carries the backend + draw counts.
            ImGui::SameLine(0.0f, em * 0.4f);
            const ImGuiIO& io = ImGui::GetIO();
            char plabel[48];
            ImFormatString(plabel, IM_ARRAYSIZE(plabel), "%.0f fps  %.1f ms", io.Framerate, io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
            ComposerStatusPill("perf", ImGuiAppComposerPillState_Neutral, plabel);
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
            const bool fresh = ImGui::AppGraphIsCodeFresh(&doc_dep->Graph);
            char frz[48];
            ImFormatString(frz, IM_ARRAYSIZE(frz), "%s  %s", fresh ? ICON_FA_CIRCLE_CHECK : ICON_FA_FILE_EXPORT, fresh ? "fresh" : "generate");
            if (zone(A_fresh, slot, "###zfresh", frz, ComposerPillColor(fresh ? ImGuiAppComposerPillState_Ok : ImGuiAppComposerPillState_Warn)))
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
                                  const ImVector<ImGuiAppCodeSpan>* spans, int* selection)
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
        const ImVec4 row_gold = ImLerp(DEMO_GOLD, row_ink, 0.15f);

        ImGuiAppHoverSource hsrc = ImGuiAppHoverSource_None;
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
                    ImGui::AppGraphHoverNode(graph, owner, ImGuiAppHoverSource_External);
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

struct ImGuiAppEditorBodyData
{
    ImGuiAppGraphDocData*                     Doc;                     // shared doc, cached non-const in OnInitialize
    bool                              TreeDragging;            // tree splitter drag FSM (advanced only in OnUpdate)
    float                             TreeDragDX;              // grab offset within the grip, captured at drag start
    bool                              InspDragging;            // inspector splitter drag FSM (advanced only in OnUpdate)
    ImGuiTextBuffer                   CodeText;                // whole app's generated C++ (kept current while the panel is open)
    ImVector<ImGuiAppCodeSpan>        CodeSpans;               // source map: node id -> line ranges in CodeText
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
    ImVector<ImGuiAppGraphIssue>        Issues;   // validation problems, recomputed while the panel is open

    // Project tab: the document's files on disk, rescanned on a slow cadence in OnUpdate.
    struct ImGuiAppProjFile { char Name[160]; unsigned long long Size; bool IsGraph; bool IsHeader; };
    ImVector<ImGuiAppProjFile> ProjFiles;
    struct ImGuiAppProjRun { char Name[224]; };   // recorded takes (.meta / .mp4) under the artifact dir
    ImVector<ImGuiAppProjRun>  ProjRuns;
    float              ProjRescan; // seconds until the next directory scan
};
// Raw input recorded by OnDraw (the only place ImGui item geometry exists), consumed by OnUpdate.
struct ImGuiAppEditorBodyTempData
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
    bool  RevealReplay;      // a run was opened/recorded -> reveal + select the Replay tab
    bool  AckReveal;         // the bottom tab bar consumed the one-shot RevealPanel intent this frame
    bool  ClearLog;          // Output tab: clear the document log
    bool  ToggleDiffMode;    // Code tab header: flip whole-program <-> diff-vs-saved
    int   StampPrefab;       // 1-based prefab index to stamp; 0 = none, so a zero-init (first-frame) TempData stamps nothing
};

// Empty selection shows the document, in the same section grammar as the node inspector.
static void ShowComposerProjectInspector(ImGuiAppGraphDocData* doc, ImGuiAppGraph* graph, ImGuiAppEditorBodyTempData* temp_data)
{
    const float em = ImGui::GetFontSize();
    const float label_w = em * 5.5f;

    if (ImGui::AppInspectorSection("##psec_doc", ICON_FA_FILE_LINES, "Document", nullptr, nullptr))
    {
        const bool fresh = ImGui::AppGraphIsCodeFresh(&doc->Graph);
        ImGui::TextDisabled("graph");
        ImGui::SameLine(label_w);
        ImGui::TextUnformatted(doc->GraphPath);
        ImGui::TextDisabled("header");
        ImGui::SameLine(label_w);
        ImGui::TextUnformatted(doc->HeaderPath);
        ImGui::SameLine();
        ImGui::TextColored(fresh ? ComposerCol(ImGui::AppComposerGetStyle()->StatusOk) : ComposerCol(ImGui::AppComposerGetStyle()->SevWarn),
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
            ImGui::TextColored(ComposerCol(ImGui::AppComposerGetStyle()->StatusOk), ICON_FA_CHECK "  No configuration problems.");
        else
        {
            ImGui::TextColored(nerr > 0 ? ComposerCol(ImGui::AppComposerGetStyle()->ErrorText) : ComposerCol(ImGui::AppComposerGetStyle()->SevWarn),
                               "%d error(s), %d warning(s)", nerr, nwarn);
            if (ImGui::SmallButton("Open Output"))
                temp_data->OpenOutput = true;
        }
        ImGui::Spacing();
    }

    // Logging (F41): the running app's write-ahead log -- level (what gets recorded) + the file path.
    if (ImGui::AppInspectorSection("##psec_log", ICON_FA_FILE_LINES, "Logging", nullptr, nullptr))
    {
        ImGuiAppWAL* wal = doc->Mirror != nullptr ? doc->Mirror->WAL : nullptr;
        static const char* levels[] = { "Off", "Lifecycle", "Frame" };
        ImGui::TextDisabled("WAL level");
        ImGui::SameLine(label_w);
        int lvl = wal != nullptr ? ImClamp((int)wal->Level, 0, IM_ARRAYSIZE(levels) - 1) : 0;
        ImGui::SetNextItemWidth(em * 9.0f);
        ImGui::BeginDisabled(wal == nullptr);   // no running WAL -> the control still shows, inert
        if (ImGui::Combo("##wallevel", &lvl, levels, IM_ARRAYSIZE(levels)) && wal != nullptr)
            wal->Level = lvl;
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Off = silent; Lifecycle = composition / storage / dispatch; Frame = + per-frame phases");
        ImGui::TextDisabled("path");
        ImGui::SameLine(label_w);
        ImGui::TextUnformatted(wal != nullptr && wal->Path[0] ? wal->Path : "(none)");
        ImGui::Spacing();
    }

    // F75's rebind editor, finally reachable: every chord surface (menus, palette, gizmo tooltips,
    // status hints) renders the effective chord, so a rebind here echoes everywhere.
    if (ImGui::AppInspectorSection("##psec_keys", ICON_FA_KEYBOARD, "Shortcuts", nullptr, nullptr))
    {
        ImGui::AppGraphShowKeymapEditor(graph);
        ImGui::Spacing();
    }

    if (ImGui::AppInspectorSection("##psec_theme", ICON_FA_PALETTE, "Composer theme", nullptr, nullptr))
    {
        ImGuiAppChromeTheme* theme = ImGui::AppGraphChromeTheme();
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
                temp_data->StampPrefab = i + 1;
            ImGui::SetItemTooltip("Instantiate this prefab on the canvas (fresh ids, selected)");
            ImGui::PopID();
        }
    }
}
struct ImGuiAppEditorBodyControl : ImGuiAppControl<ImGuiAppEditorBodyData, ImGuiAppEditorBodyTempData, ImGuiAppGraphDocData>
{
    virtual void OnInitialize(ImGuiApp* app, ImGuiAppEditorBodyData* data, const ImGuiAppGraphDocData*) const override final
    {
        data->Doc = GetGraphDoc(app);
        data->CodeSig = 0;
        data->CodeSel = -2;   // "never generated" (a real empty selection is -1)
        data->InspDragging = false;
        data->ProjRescan = 0.0f;
        data->DiffMode = false;
        data->HasDiff = false;
    }

    virtual void OnUpdate(float dt, ImGuiAppEditorBodyData* data, const ImGuiAppEditorBodyTempData* temp_data, const ImGuiAppEditorBodyTempData* last_temp_data, const ImGuiAppGraphDocData*) const override final
    {
        ImGuiAppGraphDocData* doc = data->Doc;

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
            doc->RevealPanel = ImGuiAppComposerPanel_Output;
        if (temp_data->RevealReplay)
            doc->RevealPanel = ImGuiAppComposerPanel_Replay;
        if (doc->RevealPanel != ImGuiAppComposerPanel_None && doc->CodeH <= 0.0f)
            doc->CodeH = ImGui::GetFontSize() * 12.0f;
        if (temp_data->AckReveal)
            doc->RevealPanel = ImGuiAppComposerPanel_None;
        if (temp_data->ClearLog)
        {
            doc->Log.resize(0);
        }

        if (temp_data->ProjLoadGraph)
        {
            ImGui::AppGraphLoad(doc->GraphPath, &doc->Graph);
            ImGui::AppGraphEnsureFoundation(&doc->Graph);
            ImGui::AppGraphRequestFitAll(&doc->Graph);
            DocLog(doc, 0, "loaded graph <- %s (Project)", doc->GraphPath);
        }
        if (temp_data->StampPrefab > 0)
        {
            const int prefab_idx = temp_data->StampPrefab - 1;
            ImGui::AppGraphInstantiatePrefab(&doc->Graph, prefab_idx, ImVec2(140.0f, 140.0f));
            DocLog(doc, 0, "stamped prefab '%s'", ImGui::AppGraphPrefabName(&doc->Graph, prefab_idx));
        }
        data->ProjRescan -= dt;
        if (data->ProjRescan <= 0.0f)
        {
            data->ProjRescan = 2.0f;
            data->ProjFiles.resize(0);
            struct ProjScan
            {
                ImGuiAppEditorBodyData* Data;
                const char*             GraphPath;
                const char*             HeaderPath;
                static void Visit(const char* name, ImU64 size_bytes, void* user_data)
                {
                    ProjScan* scan = (ProjScan*)user_data;
                    if (scan->Data->ProjFiles.Size >= 64)
                        return;
                    const char* ext = strrchr(name, '.');
                    if (ext == nullptr || (strcmp(ext, ".txt") != 0 && strcmp(ext, ".h") != 0 && strcmp(ext, ".wal") != 0 && strcmp(ext, ".ini") != 0))
                        return;
                    ImGuiAppEditorBodyData::ImGuiAppProjFile f;
                    ImFormatString(f.Name, IM_ARRAYSIZE(f.Name), "%s", name);
                    f.Size = (unsigned long long)size_bytes;
                    f.IsGraph  = strcmp(f.Name, scan->GraphPath) == 0;
                    f.IsHeader = strcmp(f.Name, scan->HeaderPath) == 0;
                    scan->Data->ProjFiles.push_back(f);
                }
            };
            ProjScan scan = { data, doc->GraphPath, doc->HeaderPath };
            ImGui::AppFileSystemFuncs()->ScanDirFn(".", ProjScan::Visit, &scan);

            // Recorded takes under the artifact dir (.meta tick-only / .mp4 full) -- ST2.6.
            data->ProjRuns.resize(0);
            struct RunScan
            {
                ImGuiAppEditorBodyData* Data;
                static void Visit(const char* name, ImU64 size_bytes, void* user_data)
                {
                    IM_UNUSED(size_bytes);
                    RunScan* scan = (RunScan*)user_data;
                    if (scan->Data->ProjRuns.Size >= 16)
                        return;
                    const char* ext = strrchr(name, '.');
                    if (ext == nullptr || (strcmp(ext, ".meta") != 0 && strcmp(ext, ".mp4") != 0))
                        return;
                    ImGuiAppEditorBodyData::ImGuiAppProjRun r;
                    ImFormatString(r.Name, IM_ARRAYSIZE(r.Name), "headless-artifacts/%s", name);
                    scan->Data->ProjRuns.push_back(r);
                }
            };
            RunScan rscan = { data };
            ImGui::AppFileSystemFuncs()->ScanDirFn("headless-artifacts", RunScan::Visit, &rscan);
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
            if (const ImGuiAppNode* seln = doc->Selection >= 0 ? ImGui::AppGraphFindNode(&doc->Graph, doc->Selection) : nullptr)
            {
                ImGui::AppNodeCodeGenerate(&doc->Graph, seln, &data->CodeNodeText, doc->Mirror);
                ImStrncpy(data->CodeName, seln->Draft.Name, sizeof(data->CodeName));
                data->HasNodeCode = data->CodeNodeText.size() > 0;
            }
            index_lines(data->CodeNodeText, &data->NodeLines);

            data->DiffText.clear();
            data->HasDiff = false;
            if (data->DiffMode)
            {
                ImGuiAppGraph saved;
                if (ImGui::AppGraphLoad(doc->GraphPath, &saved))
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

    virtual void OnDraw(const ImGuiAppEditorBodyData* data, ImGuiAppEditorBodyTempData* temp_data, const ImGuiAppGraphDocData*) const override final
    {
        ImGuiAppGraphDocData* doc = data->Doc;
        if (doc->Mirror == nullptr)
        {
            return;
        }
        ImGuiApp*      app   = doc->Mirror;                    // non-const: the viewer/canvas APIs edit through it
        ImGuiAppGraph* graph = &doc->Graph;

        // Canvas theme rides the graph's canvas; an explicit applied flag on the editor's view
        // state (zero-inert, view-side -- no doc write from a draw path), never a metric doubling
        // as a sentinel.
        {
            ImGuiAppCanvasStyle* cs = ImGui::CanvasGetStyle(ImGui::AppGraphEditorCanvas(graph));
            if (!ImGui::AppGraphEditorState(graph)->HostCanvasThemed)
            {
                ImGui::AppGraphEditorState(graph)->HostCanvasThemed = true;
                const ImVec4 wire_ink = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                cs->WireHovered    = DemoThemeCol(ImLerp(DEMO_GOLD, wire_ink, 0.10f), 1.0f);
                cs->WireSelected   = DemoThemeCol(ImLerp(DEMO_GOLD, wire_ink, 0.18f), 1.0f);
                cs->NodeRounding   = 5.0f;
                cs->NodePadding    = ImVec2(9.0f, 7.0f);
                cs->NodeBorder     = 1.0f;
                cs->WireThickness  = 2.6f;
                cs->PinRadius      = 4.2f;
                cs->PinHoverRadius = 10.0f;
                cs->GridSpacing    = 26.0f;
            }
        }

        // Layout is local + display-only; nothing is written to the doc from OnDraw.
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
                // Origin legend (F37): the three node origins with the SAME dot colours the canvas draws, so
                // the outliner teaches what Design / Live / Promoted mean. Display-only flat labels; the
                // HelpMarker carries the design -> live -> promotion story.
                {
                    const ImGuiAppComposerStyle* cst = ImGui::AppComposerGetStyle();
                    const ImGuiStyle&                   st  = ImGui::GetStyle();
                    auto legend = [&](const char* label, ImU32 col)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_Text,          col);
                        ImGui::Button(label);   // flat + non-interactive by look; a real id so the legend copy is test-addressable
                        ImGui::PopStyleColor(4);
                    };
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, st.FramePadding.y));
                    // Row 1: the three origin swatches. Row 2 leads with the HelpMarker so nothing is clipped
                    // off the narrow outliner's right edge.
                    legend(ICON_FA_CIRCLE "  Design###origin-design", ImGui::GetColorU32(ImGuiCol_TextDisabled));
                    ImGui::SameLine(0.0f, em * 0.1f);
                    legend(ICON_FA_CIRCLE "  Live###origin-live", cst->OriginLive);
                    ImGui::SameLine(0.0f, em * 0.1f);
                    legend(ICON_FA_CIRCLE "  Promoted###origin-promoted", cst->OriginPromoted);
                    legend("(?)###originhelp", ImGui::GetColorU32(ImGuiCol_TextDisabled));
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("You author Design nodes. Generate the C++, recompile, and the running app\n"
                                          "mirrors itself back as Live (read-only, blue). Promote a Live node to author\n"
                                          "against it; a running promoted design shows green.");
                    ImGui::SameLine(0.0f, em * 0.1f);
                    legend("Show live mirror: hiding never deletes your design.###livereassure", ImGui::GetColorU32(ImGuiCol_TextDisabled));
                    ImGui::PopStyleVar();
                    ImGui::Separator();
                }
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
                static const ImGuiAppEditorCommand host_cmds[] =
                {
                    // Id                                   Icon Label                               Shortcut  Key            Mods           Surfaces                    AddKind                  Source
                    { ImGuiAppComposerHostCmd_Save,         "",  "File: Save graph",                 "Ctrl+S", ImGuiKey_S,    ImGuiMod_Ctrl, ImGuiAppCmdSurface_Palette, ImGuiAppNodeKind_COUNT,  ImGuiAppCommandSource_Host },
                    { ImGuiAppComposerHostCmd_Load,         "",  "File: Load graph",                 "",       ImGuiKey_None, 0,             ImGuiAppCmdSurface_Palette, ImGuiAppNodeKind_COUNT,  ImGuiAppCommandSource_Host },
                    { ImGuiAppComposerHostCmd_Generate,     "",  "File: Generate C++ header",        "",       ImGuiKey_None, 0,             ImGuiAppCmdSurface_Palette, ImGuiAppNodeKind_COUNT,  ImGuiAppCommandSource_Host },
                    { ImGuiAppComposerHostCmd_CopyCode,     "",  "File: Copy generated C++",         "",       ImGuiKey_None, 0,             ImGuiAppCmdSurface_Palette, ImGuiAppNodeKind_COUNT,  ImGuiAppCommandSource_Host },
                    { ImGuiAppComposerHostCmd_Diff,         "",  "File: Diff vs saved -> clipboard", "",       ImGuiKey_None, 0,             ImGuiAppCmdSurface_Palette, ImGuiAppNodeKind_COUNT,  ImGuiAppCommandSource_Host },
                    { ImGuiAppComposerHostCmd_PanelCode,    "",  "Panel: Code",                      "",       ImGuiKey_None, 0,             ImGuiAppCmdSurface_Palette, ImGuiAppNodeKind_COUNT,  ImGuiAppCommandSource_Host },
                    { ImGuiAppComposerHostCmd_PanelProject, "",  "Panel: Project",                   "",       ImGuiKey_None, 0,             ImGuiAppCmdSurface_Palette, ImGuiAppNodeKind_COUNT,  ImGuiAppCommandSource_Host },
                    { ImGuiAppComposerHostCmd_PanelPreview, "",  "Panel: Preview",                   "",       ImGuiKey_None, 0,             ImGuiAppCmdSurface_Palette, ImGuiAppNodeKind_COUNT,  ImGuiAppCommandSource_Host },
                    { ImGuiAppComposerHostCmd_PanelOutput,  "",  "Panel: Output",                    "",       ImGuiKey_None, 0,             ImGuiAppCmdSurface_Palette, ImGuiAppNodeKind_COUNT,  ImGuiAppCommandSource_Host },
                    { ImGuiAppComposerHostCmd_ToggleLive,   "",  "View: Toggle live mirror",         "",       ImGuiKey_None, 0,             ImGuiAppCmdSurface_Palette, ImGuiAppNodeKind_COUNT,  ImGuiAppCommandSource_Host },
                    { ImGuiAppComposerHostCmd_Shortcuts,    "",  "View: Rebind shortcuts...",        "",       ImGuiKey_None, 0,             ImGuiAppCmdSurface_Palette, ImGuiAppNodeKind_COUNT,  ImGuiAppCommandSource_Host },
                    { ImGuiAppComposerHostCmd_PanelReplay,  "",  "Panel: Replay",                    "",       ImGuiKey_None, 0,             ImGuiAppCmdSurface_Palette, ImGuiAppNodeKind_COUNT,  ImGuiAppCommandSource_Host },
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
                    dl->AddRectFilled(vmin, vmax, ImGui::AppComposerGetStyle()->RunTintWash);        // amber wash
                    dl->AddRect(ImVec2(vmin.x + 1.0f, vmin.y + 1.0f), ImVec2(vmax.x - 1.0f, vmax.y - 1.0f),
                                ImGui::AppComposerGetStyle()->RunTintBorder, 0.0f, 0, ImMax(2.0f, em * 0.15f));   // engaged border
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
                        const ImU32 health_col = DemoThemeCol(ImLerp(nerr > 0 ? DEMO_RED : nwarn > 0 ? DEMO_YELLOW : DEMO_GREEN, health_ink, 0.15f), 1.0f);
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
                    if (ImGui::BeginTabBar("##bottomtabs", ImGuiTabBarFlags_FittingPolicyResizeDown))   // five tabs: shrink, never scroll-hide
                    {
                        temp_data->ToggleDiffMode = false;
                        if (ImGui::BeginTabItem("Code", nullptr, doc->RevealPanel == ImGuiAppComposerPanel_Code ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
                        {
                            // Shared tab header grammar: context label left, actions right.
                            ImGui::AlignTextToFramePadding();
                            // Header context label. Amber when the shown code is ahead of the file on disk (never
                            // written, or diverged since); neutral when fresh, empty, or diffing.
                            const bool code_fresh = ImGui::AppGraphIsCodeFresh(&doc->Graph);
                            const bool code_ahead = !code_fresh && data->HasCode && !data->DiffMode;
                            if (data->DiffMode)
                                ImGui::TextDisabled("Diff vs saved graph");
                            else if (!code_ahead)
                                ImGui::TextDisabled("Whole program");
                            else
                                ImGui::TextColored(ImLerp(DEMO_GOLD, ImGui::GetStyleColorVec4(ImGuiCol_Text), 0.25f),
                                                   doc->Graph.GenSignature != 0 ? ICON_FA_TRIANGLE_EXCLAMATION "  Whole program -- ahead of file"
                                                                  : ICON_FA_TRIANGLE_EXCLAMATION "  Whole program -- unwritten");
                            if (doc->WriteMsg[0])
                            {
                                ImGui::SameLine();
                                ImGui::TextColored(ComposerCol(ImGui::AppComposerGetStyle()->StatusOk), "%s", doc->WriteMsg);
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
                                    ImGui::PushStyleColor(ImGuiCol_Text, ImLerp(DEMO_GOLD, ImGui::GetStyleColorVec4(ImGuiCol_Text), 0.2f));
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
                        if (ImGui::BeginTabItem("Project", nullptr, doc->RevealPanel == ImGuiAppComposerPanel_Project ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
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
                                const bool fresh = ImGui::AppGraphIsCodeFresh(&doc->Graph);
                                for (int i = 0; i < data->ProjFiles.Size; i++)
                                {
                                    const ImGuiAppEditorBodyData::ImGuiAppProjFile& f = data->ProjFiles.Data[i];
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
                                        ImGui::TextColored(fresh ? ComposerCol(ImGui::AppComposerGetStyle()->StatusOk) : ComposerCol(ImGui::AppComposerGetStyle()->SevWarn),
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

                            // Recorded runs (ST2.6): takes are document artifacts; the handoff into
                            // Replay is visible here instead of implicit.
                            ImGui::Spacing();
                            ImGui::TextDisabled("Recorded runs");
                            if (data->ProjRuns.Size == 0)
                                ImGui::TextDisabled("No takes yet -- Record in the Preview tab (or a headless capture).");
                            for (int i = 0; i < data->ProjRuns.Size; i++)
                            {
                                ImGui::PushID(1000 + i);
                                ImGui::AlignTextToFramePadding();
                                ImGui::Text(ICON_FA_FILM "  %s", data->ProjRuns.Data[i].Name);
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Open in Replay") && doc->Transport != nullptr)
                                {
                                    ImGuiAppComposerTransport* rtr = doc->Transport;
                                    ImStrncpy(rtr->RunName, data->ProjRuns.Data[i].Name, sizeof(rtr->RunName));
                                    if (char* mp4 = (char*)strstr(rtr->RunName, ".mp4"))
                                        *mp4 = 0;   // ComposerOpenRun re-appends the provider extension
                                    ComposerOpenRun(rtr);
                                    rtr->Source = ImGuiAppTransportSource_FileRun;
                                    temp_data->RevealReplay = true;
                                }
                                ImGui::SetItemTooltip("Open this take as the transport's FILE source");
                                ImGui::PopID();
                            }
                            ImGui::EndTabItem();
                        }
                        // Preview (F68): run the composed graph live -- the interpreter (imguiapp_preview) builds a real
                        // ImGuiApp from the graph and renders its controls' widgets here, interactive. Edits apply next
                        // frame (preserve-by-(name,type) reconcile); selection brushes both ways with the canvas/tree.
                        if (ImGui::BeginTabItem("Preview", nullptr, doc->RevealPanel == ImGuiAppComposerPanel_Preview ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
                        {
                            ImGuiAppEditorState* ed = ImGui::AppGraphEditorState(graph);

                            // Create lazily on first view; a signature change reconciles preserving values by (name,type)
                            // field (design 7). A dependency cycle refuses -- the last good preview keeps running.
                            char pverr[192] = "";
                            const ImGuiID pvsig = ImGui::AppGraphSignature(graph);
                            if (ed->Preview == nullptr)
                            {
                                ed->Preview = ImGui::AppPreviewCreate(graph, pverr, IM_ARRAYSIZE(pverr));
                                ImGui::AppPreviewSetSurface(ed->Preview, true);
                                ed->PreviewSig = pvsig;
                            }
                            else if (pvsig != ed->PreviewSig)
                            {
                                if (ImGui::AppPreviewReconcile(ed->Preview, pverr, IM_ARRAYSIZE(pverr)))
                                    ed->PreviewSig = pvsig;
                            }

                            // F78.5 DLL backend: the compiled real program. Created lazily when selected AND a toolset
                            // exists; a signature change recompiles + hot-swaps (preserve-by-label). A compile/load failure
                            // leaves PreviewDllErr set and the panel renders the interpreter surface instead.
                            const bool dll_toolset = ImGui::AppPreviewDllIsToolsetAvailable();
                            if (ed->PreviewUseDll && dll_toolset)
                            {
                                if (ed->PreviewDll == nullptr)
                                {
                                    ed->PreviewDll = ImGui::AppPreviewDllCreate(graph, "imguix_dllpreview_composer", ed->PreviewDllErr, IM_ARRAYSIZE(ed->PreviewDllErr));
                                    ed->PreviewDllSig = pvsig;
                                }
                                else if (pvsig != ed->PreviewDllSig)
                                {
                                    if (ImGui::AppPreviewDllReload(ed->PreviewDll, graph, ed->PreviewDllErr, IM_ARRAYSIZE(ed->PreviewDllErr)))
                                        ed->PreviewDllSig = pvsig;
                                }
                            }

                            // Transport: run / pause / reinit. Icon+text buttons (never bare glyphs), theme-styled.
                            ImGui::AlignTextToFramePadding();
                            if (ImGui::Button(ed->PreviewRun ? ICON_FA_PAUSE "  Pause###pvrun" : ICON_FA_PLAY "  Run###pvrun"))
                                ed->PreviewRun = !ed->PreviewRun;
                            ImGui::SetItemTooltip(ed->PreviewRun ? "Pause the previewed model (widgets stay live)" : "Run the previewed model");
                            ImGui::SameLine();
                            if (ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT "  Reinit"))
                            {
                                ComposerPreviewRecordStop(ed, doc->Transport);   // a live take can't outlive its app
                                ImGui::AppPreviewDestroy(ed->Preview);   // rebuilt from field defaults next frame
                                ed->Preview = nullptr;
                                ImGui::AppPreviewDllDestroy(ed->PreviewDll);   // F78.5: the DLL session rebuilds from defaults too
                                ed->PreviewDll = nullptr;
                                ed->PreviewDllErr[0] = 0;
                            }
                            ImGui::SetItemTooltip("Rebuild the preview from field defaults (discard live values)");
                            ImGui::SameLine();

                            // F78.5: backend selector -- interpreter (default) vs the compiled DLL (real program). Only
                            // offered when a toolset was located; otherwise the interpreter is the only path.
                            if (dll_toolset)
                            {
                                if (ImGui::Button(ed->PreviewUseDll ? ICON_FA_MICROCHIP "  DLL###pvbackend" : ICON_FA_CODE "  Interp###pvbackend"))
                                {
                                    ed->PreviewUseDll = !ed->PreviewUseDll;
                                    ed->PreviewDllErr[0] = 0;
                                }
                                ImGui::SetItemTooltip(ed->PreviewUseDll
                    ? "Backend: compiled DLL (the real program, rendered in-panel). Click for the interpreter."
                    : "Backend: interpreter. Click to compile + run the real program (DLL).");
                                ImGui::SameLine();
                            }

                            // F70: record the preview session to an F61 run container the playback debugger opens.
                            const bool pv_recording = ed->PreviewRec != nullptr;
                            if (ImGui::Button(pv_recording ? ICON_FA_STOP "  Stop###pvrec" : ICON_FA_CIRCLE "  Record###pvrec") && ed->Preview != nullptr)
                            {
                                if (pv_recording)
                                {
                                    ComposerPreviewRecordStop(ed, doc->Transport);
                                    temp_data->RevealReplay = true;   // the take opened as the FILE source: show it (ST2.6 handoff)
                                }
                                else
                                {
                                    ImGuiApp* pv_app = ImGui::AppPreviewApp(ed->Preview);
                                    ImGui::AppInputLogClear(&ed->PreviewRecInput);   // frames before this take are excluded
                                    ed->PreviewRecTick = 0;
                                    ed->PreviewRec = ImGui::AppMetaRecordBegin(pv_app, 60.0f, ImGuiAppAVEncodeConfig().EmbedRows);
                                    ImGui::AppMetaRecordAttachInputLog(ed->PreviewRec, &ed->PreviewRecInput);
                                    ed->PreviewRun = true;   // recording captures advancing frames
                                }
                            }
                            ImGui::SetItemTooltip(pv_recording ? "Stop recording; export the run container + open it in the transport" : "Record the previewed model to an F61 run container");
                            ImGui::SameLine();
                            const bool dll_active = ed->PreviewUseDll && dll_toolset && ed->PreviewDll != nullptr;
                            if (ed->PreviewUseDll && dll_toolset && ed->PreviewDllErr[0] != 0)
                                ImGui::TextColored(ComposerCol(ImGui::AppComposerGetStyle()->ErrorText), ICON_FA_TRIANGLE_EXCLAMATION "  %s", ed->PreviewDllErr);
                            else if (pverr[0] != 0)
                                ImGui::TextColored(ComposerCol(ImGui::AppComposerGetStyle()->ErrorText), ICON_FA_TRIANGLE_EXCLAMATION "  %s", pverr);
                            else if (dll_active)
                                ImGui::TextDisabled("Compiled DLL -- the real program, rendered in-panel (recompiles on edit)");
                            else
                                ImGui::TextDisabled("Interpreted live -- edits apply next frame");
                            ImGui::Separator();

                            // The surface. The interpreter's controls render their manifest-bound widgets into this child;
                            // interaction records TempData that next frame's Task consumes (design 8.1). Brushing: the
                            // selected/hovered node's widgets halo here; a widget click selects its node in the canvas.
                            if (ImGui::BeginChild("##pvsurface", ImVec2(-FLT_MIN, -FLT_MIN)))
                            {
                                // F78.5: DLL backend rendered in-panel. Tick the compiled program at the panel size, copy its rendered
                                // frame across the boundary (draw data + atlas, bytes only), CPU-rasterize, blit as a texture. A frame
                                // with no geometry (or a create failure) falls back to the interpreter surface below.
                                bool dll_shown = false;
                                if (dll_active)
                                {
                                    const ImVec2 avail = ImGui::GetContentRegionAvail();
                                    const int fw = (int)avail.x;
                                    const int fh = (int)avail.y;
                                    if (fw > 0 && fh > 0)
                                    {
                                        // Display size follows the panel only while ticking: the rasterized draw data and the
                                        // size it was authored at stay a coherent pair -- a paused resize letterboxes the
                                        // frozen frame rather than re-mapping stale geometry into a new size.
                                        if (ed->PreviewRun)
                                        {
                                            ImGui::AppPreviewDllSetDisplaySize(ed->PreviewDll, fw, fh);
                                            ImGui::AppPreviewDllTick(ed->PreviewDll, io.DeltaTime);
                                        }
                                        const ImU32 clear = ImGui::GetColorU32(ImGuiCol_WindowBg);
                                        if (ImGui::AppPreviewDllRasterizeFrame(ed->PreviewDll, fw, fh, clear, &ed->PreviewDllRgba))
                                        {
                                            const ImTextureRef ref = ComposerUploadRgbaTexture(&ed->PreviewDllTex, &ed->PreviewDllTexW, &ed->PreviewDllTexH,
                                                                                               ed->PreviewDllRgba.Data, fw, fh);
                                            ImGui::Image(ref, ImVec2((float)fw, (float)fh));
                                            dll_shown = true;
                                        }
                                    }
                                }
                                if (!dll_shown && ed->Preview != nullptr)
                                {
                                    int hov_src = 0;
                                    const int canvas_hover = ImGui::AppGraphHoveredNode(graph, &hov_src);
                                    ImGui::AppPreviewSetBrush(ed->Preview, selection, canvas_hover);   // composer -> preview

                                    if (ed->PreviewRun) ImGui::AppPreviewFrame(ed->Preview, io.DeltaTime);
                                    else                ImGui::AppPreviewRender(ed->Preview);

                                    // F70: an active take records this advanced tick (raw io + opt-in input + snapshot cadence).
                                    if (ed->PreviewRec != nullptr && ed->PreviewRun)
                                        ComposerPreviewRecordPump(ed, io.DeltaTime);

                                    const int pv_hover = ImGui::AppPreviewHoveredNode(ed->Preview);    // preview -> composer
                                    if (pv_hover >= 0)
                                        ImGui::AppGraphHoverNode(graph, pv_hover, ImGuiAppHoverSource_External);
                                    const int pv_click = ImGui::AppPreviewTakeClickedNode(ed->Preview);
                                    if (pv_click >= 0)
                                        selection = pv_click;
                                }
                                else if (!dll_shown)
                                {
                                    ImGui::TextDisabled("No preview -- the graph has a dependency cycle or no controls yet.");
                                }
                            }
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                        char output_label[32];
                        if (data->Issues.Size > 0)
                            ImFormatString(output_label, IM_ARRAYSIZE(output_label), "Output (%d)###output", data->Issues.Size);
                        else
                            ImStrncpy(output_label, "Output###output", IM_ARRAYSIZE(output_label));
                        temp_data->AckReveal = doc->RevealPanel != ImGuiAppComposerPanel_None;   // consume the one-shot select
                        if (ImGui::BeginTabItem(output_label, nullptr, doc->RevealPanel == ImGuiAppComposerPanel_Output ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
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
                                sev_toggle("err",  &ed->OutputShowErr,  ComposerCol(ImGui::AppComposerGetStyle()->ErrorText));
                                sev_toggle("warn", &ed->OutputShowWarn, ComposerCol(ImGui::AppComposerGetStyle()->SevWarn));
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
                                    ImGui::TextColored(ComposerCol(ImGui::AppComposerGetStyle()->StatusOk), ICON_FA_CHECK "  No configuration problems.");
                                for (int i = 0; i < data->Issues.Size; i++)
                                {
                                    const ImGuiAppGraphIssue& it = data->Issues.Data[i];
                                    if ((it.Severity >= 2 && !ed->OutputShowErr) || (it.Severity < 2 && !ed->OutputShowWarn) || !ed->OutputFilter.PassFilter(it.Text))
                                        continue;
                                    const ImVec4 col = (it.Severity >= 2) ? ComposerCol(ImGui::AppComposerGetStyle()->ErrorText) : ComposerCol(ImGui::AppComposerGetStyle()->SevWarn);
                                    ImGui::PushID(i);
                                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                                    char row[288];
                                    ImFormatString(row, IM_ARRAYSIZE(row), "%s%s", (it.Severity >= 2) ? "[x] " : "[!] ", it.Text);
                                    if (ImGui::Selectable(row) && it.NodeId >= 0)
                                    {
                                        selection = it.NodeId;
                                    }
                                    if (it.NodeId >= 0 && ImGui::IsItemHovered())
                                        ImGui::AppGraphHoverNode(&doc->Graph, it.NodeId, ImGuiAppHoverSource_External);
                                    ImGui::PopStyleColor();
                                    ImGui::PopID();
                                }
                                if (doc->Log.Size == 0)
                                    ImGui::TextDisabled("(log empty -- actions, file IO and refused links land here)");
                                for (int i = doc->Log.Size - 1; i >= 0; i--)   // newest first
                                {
                                    const ImGuiAppGraphDocData::DocLogLine& ln = doc->Log.Data[i];
                                    if ((ln.Severity >= 2 && !ed->OutputShowErr) || (ln.Severity == 1 && !ed->OutputShowWarn) || (ln.Severity == 0 && !ed->OutputShowInfo)
                        || !ed->OutputFilter.PassFilter(ln.Text))
                                        continue;
                                    const ImVec4 lcol = ln.Severity >= 2 ? ComposerCol(ImGui::AppComposerGetStyle()->ErrorText)
                                      : ln.Severity == 1 ? ComposerCol(ImGui::AppComposerGetStyle()->SevWarn)
                                                         : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
                                    ImGui::TextColored(lcol, "%s", ln.Text);
                                }
                            }
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }

                        // Replay (ST2.3): the FILE-mode playback debugger, docked under the panel
                        // contract like every other surface -- the floating window died with it.
                        if (ImGui::BeginTabItem(ICON_FA_FILM " Replay###replay", nullptr, doc->RevealPanel == ImGuiAppComposerPanel_Replay ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
                        {
                            if (doc->Transport != nullptr)
                                ComposerReplayTabBody(doc->Transport, doc->Mirror, em, lay_style);
                            else
                                ImGui::TextDisabled("(no transport -- the mirror is not running)");
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
                // Scope header row (F46): while drilled, name the current scope at the top of the inspector;
                // clicking it steps up one level (breadcrumb parity). ViewScope is transient view state.
                if (graph->ViewScope.Size > 0)
                {
                    const ImGuiAppNode* sn = ImGui::AppGraphFindNode(graph, graph->ViewScope.back());
                    const char* snm = (sn != nullptr && sn->Draft.Name[0]) ? sn->Draft.Name : "scope";
                    char hdr[96];
                    ImFormatString(hdr, IM_ARRAYSIZE(hdr), ICON_FA_LAYER_GROUP "  Scope: %s###scopehdr", snm);
                    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
                    if (ImGui::Button(hdr))
                        graph->ViewScope.pop_back();
                    ImGui::PopStyleColor();
                    ImGui::SetItemTooltip("Current drill scope -- click to step up one level");
                    ImGui::Separator();
                }
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
                            ImGui::AppGraphDrawMockPanel(graph, selection, doc->Mirror);
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
    virtual void OnDraw(const ImGuiApp*) const override final {}
};

// Toggle values + "applied" bookkeeping live in PersistData; OnDraw records MenuItem results into
// TempData; OnUpdate writes them back.
// Demo host slot (Δ6 accessor idiom): set by ShowAppDemo each call; lets host-less demo
// controls (DemoMenu) reach host-scoped introspection accessors.
static ImGuiApp*& AppDemoHost() { return AppState().DemoHost; }

struct ImGuiAppDemoMenuData
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
struct ImGuiAppDemoMenuTempData
{
    bool Rendered; // false until OnDraw ran once (the first OnUpdate must not consume zeroed toggles)
    bool ShowBaseWindow;
    bool ShowStatusBar;
    bool ShowRandomTime;
    bool ShowBreathing;
    bool ShowMetrics;
};
struct ImGuiAppDemoMenuControl : ImGuiAppControl<ImGuiAppDemoMenuData, ImGuiAppDemoMenuTempData>
{
    virtual void OnInitialize(ImGuiApp* app, ImGuiAppDemoMenuData* data) const override final
    {
        IM_UNUSED(app);
        data->ShowRandomTime = true;
        data->ShowBreathing  = true;
        data->ShowMetrics    = true;
    }

    virtual void OnUpdate(float dt, ImGuiAppDemoMenuData* data, const ImGuiAppDemoMenuTempData* temp_data, const ImGuiAppDemoMenuTempData* last_temp_data) const override final
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

    virtual void OnDraw(const ImGuiAppDemoMenuData* data, ImGuiAppDemoMenuTempData* temp_data) const override final
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

        ImGui::Text("ImGuiAppLayer says hello! (%s) (%d)", IMGUIAPP_VERSION, IMGUIAPP_VERSION_NUM);
        ImGui::Spacing();
        ImGui::TextWrapped("%s", "Enable examples from the Examples menu to push/pop them on a live "
                                 "ImGuiApp. The Breathing control breathes while hovered for a duration taken from the "
                                 "Random Time control when it is active (its source), or a default otherwise.");

        if (ImGui::CollapsingHeader("ImGuiApp Status", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("See Tools > Composer -> status strip for composition, lifecycle and FPS.");
        }
#ifndef IMGUI_DISABLE_DEBUG_TOOLS
        if (ImGui::CollapsingHeader("Debug"))
        {
            if (ImGuiAppGraph* doc = AppDemoHost() != nullptr ? ImGui::AppLayerDemoGraph(AppDemoHost()) : nullptr)
                ImGui::DebugNodeAppGraph(doc, "Composer document");
            else
                ImGui::TextDisabled("No Composer document yet (open Tools > Composer).");
        }
#endif // #ifndef IMGUI_DISABLE_DEBUG_TOOLS

        temp_data->ShowBaseWindow = show_base;
        temp_data->ShowStatusBar  = show_status;
        temp_data->ShowRandomTime = show_random;
        temp_data->ShowBreathing  = show_breathe;
        temp_data->ShowMetrics    = show_metrics;
    }
};

static ImGuiAppDemoMenuData* GetDemoMenu(ImGuiApp* app)
{
    return (ImGuiAppDemoMenuData*)app->Data.GetVoidPtr(ImGuiAppType<ImGuiAppDemoMenuData>::ID);
}

struct DemoPanelWindow : ImGuiAppWindow<DemoPanelWindow>
{
    DemoPanelWindow() { ImStrncpy(this->Label, "ImGuiAppLayer Demo", sizeof(this->Label)); this->Flags = ImGuiWindowFlags_MenuBar; }
    virtual void OnDraw(const ImGuiApp*) const override final {}
};
} // namespace

namespace ImGui
{
IMGUI_API void SetAppCodeFont(ImGuiAppGraph* g, ImFont* font) { AppGraphEditorState(g)->CodeFont = font; }

IMGUI_API ImGuiAppGraph* AppLayerDemoGraph(ImGuiApp* host)
{
    if (host == nullptr)
        return nullptr;
    // Instance data is keyed by data type id in app->Data regardless of which window hosts
    // the control (GraphDocControl is Composer-window-hosted, never in host->Children).
    ImGuiAppGraphDocData* doc = (ImGuiAppGraphDocData*)host->Data.GetVoidPtr(ImGuiAppType<ImGuiAppGraphDocData>::ID);
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
    ImGuiAppGraphDocData* doc = (ImGuiAppGraphDocData*)host->Data.GetVoidPtr(ImGuiAppType<ImGuiAppGraphDocData>::ID);
    if (doc == nullptr || doc->Transport == nullptr)
        return 0;
    return doc->Transport->History.Count;
}

// Transport source (F63): ImGuiAppTransportSource_ (LiveRing default). Exposed for the FILE-mode test.
IMGUI_API int AppComposerTransportSource(ImGuiApp* host)
{
    if (host == nullptr)
        return ImGuiAppTransportSource_LiveRing;
    ImGuiAppGraphDocData* doc = (ImGuiAppGraphDocData*)host->Data.GetVoidPtr(ImGuiAppType<ImGuiAppGraphDocData>::ID);
    if (doc == nullptr || doc->Transport == nullptr)
        return ImGuiAppTransportSource_LiveRing;
    return doc->Transport->Source;
}

// Tick currently shown by the FILE-mode transport (F63): == Ticks[Scrub].Tick, 0 when no run is open.
// The scrub-to-tick acceptance reads this back after driving the timeline.
IMGUI_API ImU64 AppComposerFileRunShownTick(ImGuiApp* host)
{
    if (host == nullptr)
        return 0;
    ImGuiAppGraphDocData* doc = (ImGuiAppGraphDocData*)host->Data.GetVoidPtr(ImGuiAppType<ImGuiAppGraphDocData>::ID);
    if (doc == nullptr || doc->Transport == nullptr || doc->Transport->Run == nullptr)
        return 0;
    return doc->Transport->FileView.ShownTick;
}

// Composer outliner column width (F32): >0 when shown, 0 when hidden. Exposed for the status-zone test.
IMGUI_API float AppComposerOutlinerWidth(ImGuiApp* host)
{
    if (host == nullptr)
        return 0.0f;
    ImGuiAppGraphDocData* doc = (ImGuiAppGraphDocData*)host->Data.GetVoidPtr(ImGuiAppType<ImGuiAppGraphDocData>::ID);
    return doc != nullptr ? doc->TreeW : 0.0f;
}

// Composer layout-preset visibilities (F36): bitmask tree|insp|code|live for the preset-switch test.
IMGUI_API int AppComposerLayoutFlags(ImGuiApp* host)
{
    if (host == nullptr)
        return 0;
    ImGuiAppGraphDocData* doc = (ImGuiAppGraphDocData*)host->Data.GetVoidPtr(ImGuiAppType<ImGuiAppGraphDocData>::ID);
    return doc != nullptr ? ComposerLayoutVisFlags(doc) : 0;
}

//-----------------------------------------------------------------------------
// [SECTION] Demo bring-up (ShowAppDemo: ONE application)
//-----------------------------------------------------------------------------
// The demo composes its chrome AND its examples INTO the running app -- the same object model the live
// mirror reflects, so Live shows everything. Called from a late layer's OnDraw: windows/sidebars/controls
// are safe to push/pop here (their phase iterations for this frame already ran); layers are NOT (vector mid-iteration).

IMGUI_API void ShowAppDemo(bool* p_open, ImGuiApp* host)
{
    // A caller without an ImGuiApp (plain imgui contexts: samples, tests) gets a demo-owned
    // fallback, which is then the process's one app; the demo drives its frame below.
    ImGuiApp* app = host;
    if (app == nullptr)
    {
        if (AppState().DemoFallbackApp == nullptr)
        {
            AppState().DemoFallbackApp = IM_NEW(ImGuiApp)();
            InitializeApp(AppState().DemoFallbackApp);
        }
        app = AppState().DemoFallbackApp;
    }

    // Chrome composition, once. Examples are pushed/popped AFTER the chrome, so they are always
    // the tail of their vectors and the toggle rebuild below can pop them back off.
    AppDemoHost() = app;

    ImGuiApp*& s_composed = AppState().DemoComposed;
    IM_ASSERT((s_composed == nullptr || s_composed == app) && "One composed application per process.");
    if (s_composed != app)
    {
        ImGuiViewport* vp = GetMainViewport();
        // The Composer is ALWAYS the first window pushed: first window in the display Children, first to Begin.
        PushAppWindow<ComposerWindow>(app);
        ImGuiAppWindowBase* metrics = (ImGuiAppWindowBase*)app->DisplayLayer->Children.back();
        metrics->HasInitialPlacement = true;
        metrics->InitialSize = ImVec2(vp->WorkSize.x * 0.66f, vp->WorkSize.y * 0.66f);
        metrics->InitialPos  = vp->WorkPos + ImVec2(vp->WorkSize.x * 0.10f, vp->WorkSize.y * 0.10f);
        PushWindowControl<GraphDocControl>(app, metrics);   // producer: owns the doc (push first)
        PushWindowControl<ImGuiAppToolbarControl>(app, metrics);    // consumers depend on ImGuiAppGraphDocData
        PushWindowControl<ImGuiAppEditorBodyControl>(app, metrics);
        PushWindowControl<ImGuiAppStatusStripControl>(app, metrics);   // status bar renders LAST -> window bottom

        PushAppWindow<DemoPanelWindow>(app);
        ImGuiAppWindowBase* panel = (ImGuiAppWindowBase*)app->DisplayLayer->Children.back();
        panel->HasInitialPlacement = true;
        panel->InitialSize = ImVec2(vp->WorkSize.x * 0.30f, vp->WorkSize.y * 0.40f);
        panel->InitialPos  = vp->WorkPos + ImVec2(vp->WorkSize.x * 0.02f, vp->WorkSize.y * 0.04f);
        PushWindowControl<ImGuiAppDemoMenuControl>(app, panel);
        s_composed = app;
    }

    // Chrome windows, by label (the vector reallocs as examples push/pop).
    ImGuiAppWindowBase* panel = nullptr;
    ImGuiAppWindowBase* metrics = nullptr;
    for (int i = 0; i < app->DisplayLayer->Children.Size; i++)
    {
        if (app->DisplayLayer->Children.Data[i]->Kind != ImGuiAppNodeKind_Window)
            continue;
        if (strcmp(app->DisplayLayer->Children.Data[i]->Label, "ImGuiAppLayer Demo") == 0)
            panel = (ImGuiAppWindowBase*)app->DisplayLayer->Children.Data[i];
        else if (strcmp(app->DisplayLayer->Children.Data[i]->Label, "ImGuiAppComposer") == 0)
            metrics = (ImGuiAppWindowBase*)app->DisplayLayer->Children.Data[i];
    }
    ImGuiAppDemoMenuData* st = GetDemoMenu(app);
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

        ImGuiViewport* vp = GetMainViewport();
        const float em = GetFontSize();

        if (st->ShowBaseWindow)
        {
            PushAppWindow<BaseWindow>(app);
            ImGuiAppWindowBase* w = (ImGuiAppWindowBase*)app->DisplayLayer->Children.back();
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
            ImGuiAppWindowBase* w = (ImGuiAppWindowBase*)app->DisplayLayer->Children.back();
            w->HasInitialPlacement = true;
            w->InitialSize = ImVec2(em * 18.0f, 0.0f);
            w->InitialPos  = ImVec2(vp->WorkPos.x + em * 2.0f, vp->WorkPos.y + vp->WorkSize.y * 0.55f);
            if (st->ShowRandomTime)
                PushWindowControl<RandomTimeControlDemo>(app, w);
            if (st->ShowBreathing)
            {
                // Soft-wired to the Random Time example: reads its roll when it is on, falls back
                // to the default otherwise (Random Time pushes first when both are enabled).
                const ImGuiAppDataBinding binds[] = { { ImGuiAppType<RandomTimeData>::ID, 0, true } };
                PushWindowControl<BreathingControlDemo>(app, w, 0, binds, IM_ARRAYSIZE(binds));
            }
        }

        st->AppliedBaseWindow = st->ShowBaseWindow;
        st->AppliedStatusBar  = st->ShowStatusBar;
        st->AppliedRandomTime = st->ShowRandomTime;
        st->AppliedBreathing  = st->ShowBreathing;
    }

    // Hosted mode ends here -- the host runs its own frame. Fallback mode: the demo owns it.
    if (app == AppState().DemoFallbackApp)
    {
        UpdateApp(app);
        RenderApp(app);
        if (app->ShutdownPending)
        {
            ShutdownApp(app);
            IM_DELETE(AppState().DemoFallbackApp);
            AppState().DemoFallbackApp = nullptr;
            s_composed = nullptr;
        }
    }
}
} // namespace ImGui

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif // #ifndef IMGUI_DISABLE
#endif // IMGUIX_DISABLE_TOOLS
