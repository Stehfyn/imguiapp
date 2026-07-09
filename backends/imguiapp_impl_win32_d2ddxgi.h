// dear imgui app: Renderer Host for Win32 + Direct3D11 on a DirectComposition swapchain (composes imgui_impl_win32 + imgui_impl_dx11)
// Self-contained host: owns the immersive borderless window, its WndProc, and the message-pump run loop (does NOT use imguiapp_impl_win32).

// Implemented features:
//  [X] Renderer: exposed ImGuiApp_ImplWin32D2DDXGI_* frame lifecycle (imgui impl pattern), driven by ImGuiApp's frame phases.
//  [X] Platform: immersive window (WS_EX_NOREDIRECTIONBITMAP borderless) + D3D11 device + D2D device context + DXGI composition swapchain + DComp visual in InitPlatform/ShutdownPlatform.
//  [X] Platform: never-resized desktop-sized swapchain; smooth resize via WM_NCCALCSIZE repaint + restart/no-sequence present ladder.
//  [X] Multi-viewport: per-viewport DirectComposition swapchains + DComp targets; pacing-aware per-viewport present skip.
//  [X] AV: synchronous staging-texture CaptureFrame (CopySubresourceRegion + Map; stalls the pipeline).
//  [X] Platform: canonical D2D caption chrome (min/max/close + light/dark, Segoe Fluent Icons / MDL2 cached text layouts), self-hosted on the client seams; opt out via InitInfo::NoChrome.
// Missing features:
//  [ ] Headless modes (use win32-vulkan).

// You can use unmodified imguiapp_impl_* files in your project. See demos/ folder for examples of using this.
// Prefer including the entire imguiapp/ folder into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#pragma once
#include "imguiapp.h"      // IMGUI_API, ImGuiApp, ImGuiAppConfig, ImGuiAppFrameConfig

#ifndef IMGUI_DISABLE

// Main-swapchain present flavor: the reference's EndImmersivePaint (fRestart, fVsync) call shapes.
// Every flavor presents TWICE (flush the front buffer, then queue/replace the back buffer).
enum ImGuiApp_ImplWin32D2DDXGI_PresentMode
{
    ImGuiApp_ImplWin32D2DDXGI_PresentMode_ImmediateRestart = 0, // canonical (1,0): Present(0, TEARING|DO_NOT_WAIT|RESTART) then Present(1, DO_NOT_SEQUENCE)
    ImGuiApp_ImplWin32D2DDXGI_PresentMode_Vsync,                // (0,1): Present(0, TEARING|DO_NOT_WAIT) then Present(1, DO_NOT_SEQUENCE)
    ImGuiApp_ImplWin32D2DDXGI_PresentMode_Immediate,            // (0,0): Present(0, TEARING|DO_NOT_WAIT) twice (free-run)
};

// Client draw hooks around the imgui pass (reference: SetDevicePreDrawCallback/SetDeviceDrawCallback).
typedef void (*ImGuiApp_ImplWin32D2DDXGI_DeviceDrawCallback)(ImGuiApp* app);
// WM_NCHITTEST override: screen coords; return an HT* code, or -1 for the default immersive hittest.
// Button codes (HTCLOSE/HTMINBUTTON/HTMAXBUTTON/HTLIGHTDARK) enter the WndProc's capture-tracked press
// flow: press captures the mouse, release on the same button commits the action (Win32X dwmframex_v2).
typedef int (*ImGuiApp_ImplWin32D2DDXGI_NCHitTestCallback)(ImGuiApp* app, int x, int y);
// Hittest code for the chrome's light/dark caption button (Win32X HTLIGHTDARKBTN; outside the HT* range).
#define IMGUIAPP_WIN32D2DDXGI_HTLIGHTDARK (0xB002)

// Initialization data for ImGuiApp_ImplWin32D2DDXGI_Init().
// Zero-clear = the canonical ImmersiveWindow.c behavior on every field.
struct ImGuiApp_ImplWin32D2DDXGI_InitInfo
{
    void*        Hwnd;                   // existing window; nullptr = InitPlatform creates the immersive window
    // Window creation (used only when InitPlatform creates the window)
    unsigned int WindowStyle;            // 0 = canonical: WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_BORDER|WS_SYSMENU|WS_VISIBLE
    unsigned int WindowExStyle;          // 0 = canonical: WS_EX_NOREDIRECTIONBITMAP
    bool         NoCenterWindow;         // false = canonical BCS_CENTERED: center on the cursor's monitor work area
    int          CaptionHeight;          // caption strip height in 96-dpi units for the default WM_NCHITTEST; 0 = 30 (canonical)
    // D3D11 device
    const int*   FeatureLevels;          // D3D_FEATURE_LEVEL[]; nullptr = canonical { 10_0, 11_0, 11_1, 12_0, 12_1 } (first creatable wins, verbatim)
    int          FeatureLevelsCount;
    unsigned int DeviceFlags;            // D3D11_CREATE_DEVICE_*; 0 = canonical: DISABLE_GPU_TIMEOUT|PREVENT_ALTERING_LAYER_SETTINGS_FROM_REGISTRY|SINGLETHREADED|PREVENT_INTERNAL_THREADING_OPTIMIZATIONS|BGRA_SUPPORT
    int          DebugLayer;             // 0 = debug builds only (canonical), 1 = force on, -1 = force off (D3D11 debug + DXGI factory debug + D2D info level)
    // Composition swapchain (main viewport; NEVER resized -- window resize draws into a sub-rect)
    int          BufferCount;            // 0 = 5 (canonical "3 + 1 + 1")
    int          SwapchainWidth;         // 0 = primary desktop resolution (canonical)
    int          SwapchainHeight;
    unsigned int SwapchainFlags;         // DXGI_SWAP_CHAIN_FLAG_*; 0 = canonical: ALLOW_TEARING|ALLOW_MODE_SWITCH|FRAME_LATENCY_WAITABLE_OBJECT
    int          MaxFrameLatency;        // 0 = do not call SetMaximumFrameLatency (canonical); > 0 = call with this value
    // Presentation + pacing
    int          PresentMode;            // ImGuiApp_ImplWin32D2DDXGI_PresentMode_; 0 = canonical restart ladder
    bool         PresentDirtyRects;      // true = FLIP_SEQUENTIAL swapchains + Present1 dirty-rect presentation (client rect only; see docs/dxgi-noflicker.md §7)
    bool         NoWaitForVBlank;        // true = skip the D3DKMT vertical-blank wait in the run loop
    bool         ModalRepaintRenderOnly; // true = WM_NCCALCSIZE/WM_TIMER repaints re-present the last frame instead of running a full app Frame()
    bool         EnableClear;            // false = canonical no-clear; true = clear the target with the frame config's ClearColor (honors NoClear)
    // Window dressing (applied on WM_ACTIVATE)
    bool         NoBlurBehind;           // true = skip DwmExtendFrameIntoClientArea + DwmEnableBlurBehindWindow
    bool         NoDarkModeLadder;       // true = skip the uxtheme dark-mode ladder (SetPreferredAppMode & friends)
    bool         NoChrome;               // true = skip the built-in canonical D2D caption chrome (min/max/close + light/dark buttons)

    ImGuiApp_ImplWin32D2DDXGI_InitInfo() { memset((void*)this, 0, sizeof(*this)); }
};

// Frame lifecycle (imgui impl pattern, app-threaded); registered on the seam via ImGuiAppPlatformBackend.
// Backend data lives in app->BackendData (io.BackendXxxUserData analog; both io slots belong to the wrapped imgui backends).
IMGUI_API bool ImGuiApp_ImplWin32D2DDXGI_Init(ImGuiApp* app, const ImGuiApp_ImplWin32D2DDXGI_InitInfo* init_info);
IMGUI_API void ImGuiApp_ImplWin32D2DDXGI_Shutdown(ImGuiApp* app);
IMGUI_API void ImGuiApp_ImplWin32D2DDXGI_NewFrame(ImGuiApp* app);
IMGUI_API void ImGuiApp_ImplWin32D2DDXGI_RenderDrawData(ImGuiApp* app, ImDrawData* draw_data, const ImGuiAppFrameConfig* config);
IMGUI_API void ImGuiApp_ImplWin32D2DDXGI_PresentFrame(ImGuiApp* app, const ImGuiAppFrameConfig* config);

IMGUI_API bool ImGuiApp_ImplWin32D2DDXGI_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config);
IMGUI_API void ImGuiApp_ImplWin32D2DDXGI_ShutdownPlatform(ImGuiApp* app);
IMGUI_API int  ImGuiApp_ImplWin32D2DDXGI_RunLoop(ImGuiApp* app);

// (Optional) Client access to the device objects + draw seams (reference: GetDeviceAndSwapchain / SetDevice*DrawCallback).
IMGUI_API bool ImGuiApp_ImplWin32D2DDXGI_GetDeviceAndSwapchain(ImGuiApp* app, void** device, void** device_context, void** swapchain, void** d2d_device_context, void** dcomp_device); // ID3D11Device* / ID3D11DeviceContext* / IDXGISwapChain2* / ID2D1DeviceContext* / IDCompositionDevice*
IMGUI_API void ImGuiApp_ImplWin32D2DDXGI_SetDevicePreDrawCallback(ImGuiApp* app, ImGuiApp_ImplWin32D2DDXGI_DeviceDrawCallback callback); // runs before the imgui pass (raw D3D, target bound)
IMGUI_API void ImGuiApp_ImplWin32D2DDXGI_SetDeviceDrawCallback(ImGuiApp* app, ImGuiApp_ImplWin32D2DDXGI_DeviceDrawCallback callback);    // runs after the imgui pass, inside a D2D BeginDraw/EndDraw bracket
IMGUI_API void ImGuiApp_ImplWin32D2DDXGI_SetNCHitTestCallback(ImGuiApp* app, ImGuiApp_ImplWin32D2DDXGI_NCHitTestCallback callback);
IMGUI_API int  ImGuiApp_ImplWin32D2DDXGI_GetCaptionHeight(ImGuiApp* app); // current caption strip height in window pixels (dpi-scaled)

// (Optional) Pacer platform seam (QPC clock, hybrid sleep/spin wait, per-monitor refresh queries).
// InitPlatform installs it when app->Pacer.Funcs is null, so ImGuiAppPacerMode_Target with TargetHz <= 0
// paces the app to the primary monitor's refresh rate out of the box.
IMGUI_API const ImGuiAppPacerFuncs* ImGuiApp_ImplWin32D2DDXGI_GetPacerFuncs();

// (Optional) The D2D caption chrome (min/max/close + light/dark buttons; Win32X dwmframex_v2 glyphs,
// hittest, and cached-text-layout positioning), self-hosted on this backend's own client seams
// (SetDeviceDrawCallback + SetNCHitTestCallback -- installing your own callbacks displaces the chrome's).
// Init installs it by default (InitInfo::NoChrome opts out); the light/dark button drives the uxtheme ladder.
IMGUI_API bool ImGuiApp_ImplWin32D2DDXGI_InstallChrome(ImGuiApp* app);
IMGUI_API void ImGuiApp_ImplWin32D2DDXGI_UninstallChrome(ImGuiApp* app);
IMGUI_API bool ImGuiApp_ImplWin32D2DDXGI_GetChromeLightMode(ImGuiApp* app); // the caption light/dark toggle's current state (true = light)

// This host's seam vtable; the app's wiring hands it to the app layer as ImGuiAppGetPlatformBackend().
IMGUI_API const ImGuiAppPlatformBackend* ImGuiApp_ImplWin32D2DDXGI_GetPlatformBackend();

#endif // #ifndef IMGUI_DISABLE
