// dear imgui app: Renderer Host for Win32 + Direct3D11 on a DirectComposition swapchain (composes imgui_impl_win32 + imgui_impl_dx11)
// Self-contained host: owns the immersive borderless window, its WndProc, and the message-pump run loop (does NOT use imguiapp_impl_win32).

// Implemented features:
//  [X] Renderer: exposed ImGuiApp_ImplWin32D2DDXGI_* frame lifecycle (imgui impl pattern), driven by ImGuiApp's frame phases.
//  [X] Platform: immersive window (WS_EX_NOREDIRECTIONBITMAP borderless) + D3D11 device + D2D device context + DXGI composition swapchain + DComp visual in InitPlatform/ShutdownPlatform.
//  [X] Platform: never-resized desktop-sized swapchain; smooth resize via WM_NCCALCSIZE repaint + restart/no-sequence present ladder.
//  [X] Multi-viewport: per-viewport DirectComposition swapchains + DComp targets; pacing-aware per-viewport present skip.
//  [X] AV: synchronous staging-texture CaptureFrame (CopySubresourceRegion + Map; stalls the pipeline).
//  [X] Platform: canonical D2D caption chrome (min/max/close + light/dark, Segoe Fluent Icons / MDL2 cached text layouts; system icon + HTSYSMENU slot; caption title; 160ms theme/activation crossfade + hover fades), self-hosted on the client seams; opt out via InitInfo::NoChrome.
// Missing features:
//  [ ] Headless modes (use win32-vulkan).

// This backend transcribes ImmersiveWindow.c (the canonical immersive-window reference): every device/swapchain/
// composition creation parameter, the never-resized swapchain, the WM_NCCALCSIZE repaint path, and the two-step
// present ladder are replicated verbatim; InitInfo zero-clear equals the canonical behavior on every field.

// You can use unmodified imguiapp_impl_* files in your project. See demos/ folder for examples of using this.
// Prefer including the entire imguiapp/ folder into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2026-07-09: Platform: non-client frame contract completed from Win32X dwmframex_v2 -- caption title (system caption font, cached IDWriteTextLayout, win32kfull placement, crossfaded + 60% inactive dim; WM_SETTEXT drops the cache), WM_NCACTIVATE tracks REAL activation with lParam -1 (always-active froze DWM's frame), WM_DPICHANGED snaps to the suggested rect.
//  2026-07-09: Platform: GetChromeThemeBlend -- the chrome's animated dark->light position (0..1 across the 160ms crossfade), published so the app can lerp its own theme in step (the demo crossfades StyleColorsDark <-> StyleColorsLight with it).
//  2026-07-09: Platform: chrome animation (Win32X DfwAdvance/DwfBeginTransition): one 160ms wall-clock timeline crossfades glyph colors on theme (light/dark toggle) and activation changes, and ramps per-button hover/press highlight opacity (v2 fill shades; close glyph fades to white as its red rises). Driven by the continuous render loop, no timer.
//  2026-07-09: Platform: chrome caption system icon (Win32X DwfEnsureIcon): the window's icon rasterized once at native res into a D3D11 texture / D2D bitmap, drawn at the win32kfull DrawCaptionIcon slot; the caption-left square hittests HTSYSMENU (DefWindowProc: menu on click, close on double-click).
//  2026-07-09: Platform: chrome conformed to Win32X dwmframex_v2 -- canonical glyph set (ChromeMinimize/Maximize/Restore/Close 0xE921-3/0xE8BB, light/dark 0xE706/0xE708) from Segoe Fluent Icons (MDL2 fallback) drawn as cached centered IDWriteTextLayouts; client-space hittest (buttons beat the top resize strip, HTLIGHTDARK replaces the HTMENU hack); capture-tracked button presses (press captures, release on the same button commits).
//  2026-07-08: Platform: vblank pace thread -- posts coalesced refresh-rate ticks to the main window while a modal loop is live, so size/move/menu loops repaint at the monitor's refresh rate instead of the WM_TIMER ~64Hz floor (the timer stays as fallback).
//  2026-07-08: Platform: default pacer platform seam (QPC clock, hybrid wait, per-monitor refresh queries) installed by InitPlatform when the client provided none -- ImGuiAppPacerMode_Target + TargetHz <= 0 now paces to the primary monitor's refresh rate; the run loop's canonical idle throttle defers to an active pacer.
//  2026-07-08: Platform: opt-in dirty-rect presentation (InitInfo::PresentDirtyRects): FLIP_SEQUENTIAL swapchains + Present1 declaring the client rect as the only damage over the over-allocated buffers; full-frame present after every (re)creation per DXGI rules. See docs/dxgi-noflicker.md.
//  2026-07-08: [Viewports] No-flicker design ported to secondary viewports: grow-only over-allocated swapchains (ResizeBuffers only on growth, deferred to just before render), imgui's two-transaction move+resize coalesced via wrapped Platform_SetWindowPos/SetWindowSize + flushed right before render, per-viewport content pin/unpin on the viewport's DComp visual. See docs/dxgi-noflicker.md.
//  2026-07-08: Platform: self-healing content pin -- when a resize moves the window origin ahead of the presented content, a compensating DComp visual offset pins the content at its render-time screen position (no rendering); every present unpins in the same compositor latch. Origin-moving edge resizes can no longer show translated content, independent of render latency. See docs/dxgi-noflicker.md.
//  2026-07-08: Platform: modal repaints serialized to the compositor clock (GetFrameStatistics coalescing to one rendered present per compositor frame; WM_NCCALCSIZE latches content via Commit + WaitForCommitCompletion before the geometry commit). See docs/dxgi-noflicker.md.
//  2026-07-08: Inputs: ESC closes the window when foreground (the reference's WM_KEYUP behavior, EndTask softened to WM_CLOSE).
//  2026-07-08: Platform: canonical D2D caption chrome (min/max/close + light/dark glyph buttons) transcribed and self-hosted on the SetDeviceDrawCallback/SetNCHitTestCallback seams; installed by Init unless InitInfo::NoChrome.
//  2026-07-08: Initial version: immersive window + D3D11/D2D/DXGI-composition/DComp creation ladder transcribed from ImmersiveWindow.c; composes imgui_impl_win32 + imgui_impl_dx11; per-viewport composition swapchains; synchronous CaptureFrame.

#include "imguiapp.h"
#ifndef IMGUI_DISABLE
#include "imguiapp_impl_win32_d2ddxgi.h"

#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>    // D3D11CreateDevice, ID3D11Device/DeviceContext/RenderTargetView/Texture2D/Debug
#include <dxgi1_3.h>  // CreateDXGIFactory2, IDXGIFactory2::CreateSwapChainForComposition, IDXGISwapChain2, IDXGIDevice1
#include <d2d1_2.h>   // D2D1CreateFactory, ID2D1Factory2, ID2D1Device1, ID2D1DeviceContext::CreateBitmapFromDxgiSurface
#include <dcomp.h>    // DCompositionCreateDevice, IDCompositionDevice/Target/Visual
#include <dwmapi.h>   // DwmExtendFrameIntoClientArea, DwmEnableBlurBehindWindow, DwmSetWindowAttribute, DwmFlush
#include <uxtheme.h>  // SetWindowTheme
#include <dwrite.h>   // DWriteCreateFactory, IDWriteFactory::CreateTextFormat/CreateTextLayout (caption chrome glyphs)

#ifdef _MSC_VER
#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "d2d1")
#pragma comment(lib, "dcomp")
#pragma comment(lib, "dwmapi")
#pragma comment(lib, "uxtheme")
#pragma comment(lib, "dwrite")
#pragma comment(lib, "gdi32")   // D3DKMT* vertical-blank entry points
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Pace tick posted by the vblank thread: dispatched by MODAL loops (size/move, menus), where the run
// loop's pacing cannot run; keeps repaints at the monitor's refresh rate through them.
#define IMGUIAPP_WIN32D2DDXGI_WM_PACE (WM_APP + 0x69)

// SDK back-fill: not present in every dwmapi/dxgi header vintage.
#ifndef DWMWA_PASSIVE_UPDATE_MODE
#define DWMWA_PASSIVE_UPDATE_MODE ((DWMWINDOWATTRIBUTE)16)
#endif
#ifndef DXGI_PRESENT_ALLOW_TEARING
#define DXGI_PRESENT_ALLOW_TEARING 0x00000200UL
#endif

// Reference macro tier (ImmersiveWindow.c): named composites over the raw D3D11/DXGI flags.
#define D3D11_DEVICE_SINGLETHREADED (                                    \
    D3D11_CREATE_DEVICE_SINGLETHREADED                           |       \
    D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS         \
    )
#define D3D11_DEVICE_AUTHORITATIVE (                                     \
    D3D11_CREATE_DEVICE_DISABLE_GPU_TIMEOUT                           |  \
    D3D11_CREATE_DEVICE_PREVENT_ALTERING_LAYER_SETTINGS_FROM_REGISTRY    \
    )
#define D3D11_DEVICE_D2D_COMPATIBLE ( \
    D3D11_CREATE_DEVICE_BGRA_SUPPORT  \
    )
#define DXGI_SWAPCHAIN_ENABLE_IMMEDIATE_PRESENT ( \
    DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING     |      \
    DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH        \
    )
#define DXGI_SWAPCHAIN_ENABLE_WAIT_FOR_NEXT_FRAME_RESOURCES ( \
    DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT        \
    )

// D3DKMT vertical-blank wait: declarations mirrored from d3dkmthk.h (WDK header); the entry points are gdi32 exports.
typedef UINT D3DKMT_HANDLE;
typedef UINT D3DDDI_VIDEO_PRESENT_SOURCE_ID;
typedef struct _D3DKMT_OPENADAPTERFROMHDC
{
    HDC                            hDc;            // in:  DC that maps to a single display
    D3DKMT_HANDLE                  hAdapter;       // out: adapter handle
    LUID                           AdapterLuid;    // out: adapter LUID
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;  // out: VidPN source ID for that particular display
} D3DKMT_OPENADAPTERFROMHDC;
typedef struct _D3DKMT_WAITFORVERTICALBLANKEVENT
{
    D3DKMT_HANDLE                  hAdapter;       // in: adapter handle
    D3DKMT_HANDLE                  hDevice;        // in: device handle [Optional]
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;  // in: adapter's VidPN Source ID
} D3DKMT_WAITFORVERTICALBLANKEVENT;
typedef struct _D3DKMT_GETSCANLINE
{
    D3DKMT_HANDLE                  hAdapter;       // in: adapter handle
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;  // in: adapter's VidPN Source ID
    BOOLEAN                        InVerticalBlank; // out: within vertical blank
    UINT                           ScanLine;       // out: current scan line
} D3DKMT_GETSCANLINE;
typedef struct _D3DKMT_CLOSEADAPTER
{
    D3DKMT_HANDLE hAdapter;                        // in: adapter handle
} D3DKMT_CLOSEADAPTER;
extern "C" LONG APIENTRY D3DKMTOpenAdapterFromHdc(D3DKMT_OPENADAPTERFROMHDC*);
extern "C" LONG APIENTRY D3DKMTWaitForVerticalBlankEvent(const D3DKMT_WAITFORVERTICALBLANKEVENT*);
extern "C" LONG APIENTRY D3DKMTGetScanLine(D3DKMT_GETSCANLINE*);
extern "C" LONG APIENTRY D3DKMTCloseAdapter(const D3DKMT_CLOSEADAPTER*);

// Undocumented uxtheme dark-mode ordinals (the reference's InitializeSysThunks). Genuinely process-global
// facts (one uxtheme per process), loaded once by the first InitPlatform.
typedef int (WINAPI* ImGuiApp_ImplWin32D2DDXGI_PFN_SetPreferredAppMode)(int app_mode);       // ordinal 135; 1 = AllowDark
typedef void (WINAPI* ImGuiApp_ImplWin32D2DDXGI_PFN_FlushMenuThemes)();                       // ordinal 136
typedef void (WINAPI* ImGuiApp_ImplWin32D2DDXGI_PFN_RefreshImmersiveColorPolicyState)();      // ordinal 104
typedef void (WINAPI* ImGuiApp_ImplWin32D2DDXGI_PFN_InvalidateAppTheme)();                    // ordinal 115
static ImGuiApp_ImplWin32D2DDXGI_PFN_SetPreferredAppMode              ImGuiApp_ImplWin32D2DDXGI_SetPreferredAppMode = nullptr;
static ImGuiApp_ImplWin32D2DDXGI_PFN_FlushMenuThemes                  ImGuiApp_ImplWin32D2DDXGI_FlushMenuThemes = nullptr;
static ImGuiApp_ImplWin32D2DDXGI_PFN_RefreshImmersiveColorPolicyState ImGuiApp_ImplWin32D2DDXGI_RefreshImmersiveColorPolicyState = nullptr;
static ImGuiApp_ImplWin32D2DDXGI_PFN_InvalidateAppTheme               ImGuiApp_ImplWin32D2DDXGI_InvalidateAppTheme = nullptr;

// Private impl of the opaque app->PlatformData slot (per-host type; hosts coexist as ordinary TUs).
struct ImGuiApp_ImplWin32D2DDXGI_PlatformData
{
    HWND        Hwnd;
    WNDCLASSEXA WindowClass;
    bool        OwnsImGuiContext; // this host created the ImGui context (none existed)
};

struct ImGuiApp_ImplWin32D2DDXGI_ChromeData;   // built-in caption chrome tier, defined below the WndProc helpers

struct ImGuiApp_ImplWin32D2DDXGI_Data
{
    void*     Hwnd;
    bool      PlatformBackendInitialized;
    bool      RendererBackendInitialized;
    ImGuiApp* App;                          // pacer app (source of per-viewport pacing decisions)

    // Device objects (the canonical creation ladder's outputs)
    ID3D11Device*           D3DDevice;
    ID3D11DeviceContext*    D3DDeviceContext;
    ID3D11Debug*            D3DDebug;       // debug layer only
    IDXGIDevice1*           DXGIDevice;
    IDXGIFactory2*          DXGIFactory;
    IDXGISwapChain2*        Swapchain;      // composition swapchain; NEVER resized
    IDXGISurface2*          SwapchainSurface;
    ID3D11Texture2D*        BackBuffer;     // buffer 0; flip-model D3D11 aliases the current back buffer
    ID3D11RenderTargetView* MainRTV;
    ID2D1Factory2*          D2DFactory;
    ID2D1Device1*           D2DDevice;
    ID2D1DeviceContext*     D2DDeviceContext;
    ID2D1Bitmap1*           D2DTargetBitmap;
    IDCompositionDevice*    DCompDevice;
    IDCompositionTarget*    DCompTarget;
    IDCompositionVisual*    DCompVisual;
    HANDLE                  FrameLatencyWaitableObject;

    // Options resolved from InitInfo (zero = canonical)
    int  CaptionHeight;                     // 96-dpi units
    int  PresentMode;
    bool PresentDirtyRects;                 // FLIP_SEQUENTIAL + Present1 client-rect dirty presentation
    bool NoWaitForVBlank;
    bool ModalRepaintRenderOnly;
    bool EnableClear;
    bool NoBlurBehind;
    bool NoDarkModeLadder;
    bool PresentedOnce;                     // dirty-rect rule: the first present of a swapchain must be full-frame

    // Client seams
    ImGuiApp_ImplWin32D2DDXGI_DeviceDrawCallback PreDrawCallback; // before the imgui pass (raw D3D)
    ImGuiApp_ImplWin32D2DDXGI_DeviceDrawCallback DrawCallback;    // after the imgui pass, D2D BeginDraw/EndDraw bracketed
    ImGuiApp_ImplWin32D2DDXGI_NCHitTestCallback  NCHitTestCallback;
    ImGuiApp_ImplWin32D2DDXGI_ChromeData*        Chrome;          // built-in caption chrome (rides the two seams above); null = none

    // WndProc frame state
    bool Moving;                            // WM_MOVING seen since the last WM_ENTERSIZEMOVE
    bool Resizing;                          // WM_SIZING seen since the last WM_ENTERSIZEMOVE
    int  NCPressedHit;                      // caption-button HT* code held by a capture-tracked press (Win32X xxxTrackCaptionButton); 0 = none
    bool InModalRepaint;                    // re-entrancy latch for the WndProc-driven repaint paths
    bool InFrame;                           // a frame is on the stack (NewFrame..PresentFrame): viewport create/destroy
                                            // inside it sends WM_WINDOWPOSCHANGED/WM_ACTIVATE synchronously to the main
                                            // window; repainting from those would re-enter ImGui::NewFrame and crash
    bool ModalPresentActive;                // PresentFrame override while a modal repaint frame runs
    bool ModalPresentRestart;
    bool ModalPresentVsync;

    // Compositor-clock latch for the modal repaint paths: the DirectComposition engine consumes all pending
    // presents/batches atomically at each frame start (one frame per vblank), so at most ONE rendered present
    // per compositor frame is useful -- extra ones just churn the queue and let a stale present pair with a
    // newer geometry commit (the rapid-resize flicker). Records the frame a repaint targeted + the client
    // size it drew, so the timer/WM_WINDOWPOSCHANGED paths can skip redundant repaints within that frame.
    LONGLONG ModalLatchedFrameTime;         // DCOMPOSITION_FRAME_STATISTICS::nextEstimatedFrameTime the last modal repaint targeted
    int      ModalLatchedWidth;             // client size that repaint drew
    int      ModalLatchedHeight;

    // Content pin (self-healing): geometry commits and content presents are independent compositor inputs,
    // and a re-render can never be guaranteed to beat the next compositor latch. When a SIZE change moves
    // the window origin ahead of the presented content (left/top-edge resizes), a compensating visual
    // offset pins the old content at the screen position it was rendered for -- microseconds, no rendering
    // -- and every present unpins it in the same batch. No compositor frame can show translated content.
    int  ContentOriginX;                    // window-origin screen position of the last presented frame
    int  ContentOriginY;
    int  ContentWidth;                      // client size of the last presented frame
    int  ContentHeight;
    bool ContentOriginValid;
    bool VisualOffsetActive;                // a compensating offset is currently committed on DCompVisual

    // D3DKMT vertical-blank wait (the reference's WaitForVerticalBlank; first call only opens the adapter)
    D3DKMT_HANDLE VBlankAdapter;
    UINT          VBlankSourceId;

    // Pace thread: blocks on its OWN D3DKMT vblank wait and posts IMGUIAPP_WIN32D2DDXGI_WM_PACE to the
    // main window while a modal loop is live (PaceModalLive), so modal resizes/moves/menus keep repainting
    // at the monitor's refresh rate instead of the WM_TIMER floor. PacePending coalesces to one queued
    // tick (a stalled main thread must not fill the message queue).
    HANDLE        PaceThread;
    HANDLE        PaceWake;                 // auto-reset event: state changes (modal enter/exit, shutdown) wake the thread
    volatile bool PaceThreadStop;
    volatile bool PaceModalLive;
    volatile LONG PacePending;

    // Per-viewport pacing (secondary platform windows): decided ONCE per viewport per frame in
    // Platform_RenderWindow, consumed by the render + present hooks. A skipped viewport keeps its last contents.
    ImGuiStorage VpSkip;                    // viewport ID -> skip present this frame

    // Underlying imgui_impl_win32 platform hooks, wrapped so secondary-viewport geometry can be deferred
    // and flushed as one transaction right before that viewport renders (see Platform_RenderWindow).
    void (*UnderlyingPlatformSetWindowPos)(ImGuiViewport*, ImVec2);
    void (*UnderlyingPlatformSetWindowSize)(ImGuiViewport*, ImVec2);

    // Frame capture (AV readback): synchronous staging copy of the client-rect region of buffer 0.
    ID3D11Texture2D* CaptureStaging;
    int              CaptureStagingWidth;
    int              CaptureStagingHeight;
    ImU64            CaptureLastReturned;   // highest FrameID.FrameIndex handed out; a repeat call with no new frame returns false
    ImVector<char>   CaptureRgba;           // BGRA->RGBA converted pixels; valid until the next capture

    // Platform window/loop state sidecar, owned by app->PlatformData (freed in ShutdownPlatform).
    ImGuiApp_ImplWin32D2DDXGI_PlatformData* State;

    ImGuiApp_ImplWin32D2DDXGI_Data() { memset((void*)this, 0, sizeof(*this)); }
};

// Backend data stored in app->BackendData (the io userdata slots belong to the wrapped imgui backends).
static ImGuiApp_ImplWin32D2DDXGI_Data* ImGuiApp_ImplWin32D2DDXGI_GetBackendData(ImGuiApp* app)
{
    return app != nullptr ? (ImGuiApp_ImplWin32D2DDXGI_Data*)app->BackendData : nullptr;
}

// Context-free viewport hooks recover the app through the main viewport's window user data
// (set at the end of InitPlatform; the same slot the WndProc reads for the modal repaint paths).
static ImGuiApp* ImGuiApp_ImplWin32D2DDXGI_GetApp()
{
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    if (main_viewport == nullptr || main_viewport->PlatformHandle == nullptr)
        return nullptr;
    return (ImGuiApp*)::GetWindowLongPtr((HWND)main_viewport->PlatformHandle, GWLP_USERDATA);
}

static bool ImGuiApp_ImplWin32D2DDXGI_IsInitInfoValid(const ImGuiApp_ImplWin32D2DDXGI_InitInfo* init_info)
{
    return init_info != nullptr && init_info->Hwnd != nullptr;
}

//--------------------------------------------------------------------------------------------------------
// Immersive window helpers (transcribed from ImmersiveWindow.c)
//--------------------------------------------------------------------------------------------------------

static void ImGuiApp_ImplWin32D2DDXGI_GetWindowBorders(HWND hwnd, SIZE* out_size)
{
    const UINT dpi         = ::GetDpiForWindow(hwnd);
    const LONG cx_frame    = ::GetSystemMetricsForDpi(SM_CXFRAME, dpi);
    const LONG cy_frame    = ::GetSystemMetricsForDpi(SM_CYFRAME, dpi);
    const LONG cxy_padding = ::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    out_size->cx = cx_frame + cxy_padding;
    out_size->cy = cy_frame + cxy_padding;
}

static void ImGuiApp_ImplWin32D2DDXGI_EnableBlurBehind(HWND hwnd)
{
    DWM_BLURBEHIND bb = {};
    bb.dwFlags  = DWM_BB_ENABLE | DWM_BB_BLURREGION;
    bb.fEnable  = TRUE;
    bb.hRgnBlur = ::CreateRectRgn(0, 0, -1, -1);
    ::DwmEnableBlurBehindWindow(hwnd, &bb);
    ::DeleteObject(bb.hRgnBlur);
}

static void ImGuiApp_ImplWin32D2DDXGI_LoadUxthemeOrdinals()
{
    if (ImGuiApp_ImplWin32D2DDXGI_SetPreferredAppMode != nullptr)
        return;
    HMODULE uxtheme = ::GetModuleHandleA("uxtheme.dll");
    if (uxtheme == nullptr)
        uxtheme = ::LoadLibraryA("uxtheme.dll");
    if (uxtheme == nullptr)
        return;
    ImGuiApp_ImplWin32D2DDXGI_SetPreferredAppMode              = (ImGuiApp_ImplWin32D2DDXGI_PFN_SetPreferredAppMode)(void*)::GetProcAddress(uxtheme, MAKEINTRESOURCEA(135));
    ImGuiApp_ImplWin32D2DDXGI_FlushMenuThemes                  = (ImGuiApp_ImplWin32D2DDXGI_PFN_FlushMenuThemes)(void*)::GetProcAddress(uxtheme, MAKEINTRESOURCEA(136));
    ImGuiApp_ImplWin32D2DDXGI_RefreshImmersiveColorPolicyState = (ImGuiApp_ImplWin32D2DDXGI_PFN_RefreshImmersiveColorPolicyState)(void*)::GetProcAddress(uxtheme, MAKEINTRESOURCEA(104));
    ImGuiApp_ImplWin32D2DDXGI_InvalidateAppTheme               = (ImGuiApp_ImplWin32D2DDXGI_PFN_InvalidateAppTheme)(void*)::GetProcAddress(uxtheme, MAKEINTRESOURCEA(115));
}

// The reference's EnableDarkMode ladder, call order preserved.
static void ImGuiApp_ImplWin32D2DDXGI_EnableDarkMode(HWND hwnd)
{
    if (ImGuiApp_ImplWin32D2DDXGI_SetPreferredAppMode != nullptr)
        ImGuiApp_ImplWin32D2DDXGI_SetPreferredAppMode(1); // PAM_ALLOWDARK
    ::SetPropA(hwnd, "UseImmersiveDarkModeColors", (HANDLE)1);
    if (ImGuiApp_ImplWin32D2DDXGI_RefreshImmersiveColorPolicyState != nullptr)
        ImGuiApp_ImplWin32D2DDXGI_RefreshImmersiveColorPolicyState();
    if (ImGuiApp_ImplWin32D2DDXGI_InvalidateAppTheme != nullptr)
        ImGuiApp_ImplWin32D2DDXGI_InvalidateAppTheme();
    if (ImGuiApp_ImplWin32D2DDXGI_FlushMenuThemes != nullptr)
        ImGuiApp_ImplWin32D2DDXGI_FlushMenuThemes();
    ::SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
}

// WM_ACTIVATE dressing (reference OnActivate): dwm frame extension + blur behind + dark mode.
static void ImGuiApp_ImplWin32D2DDXGI_ApplyWindowDressing(ImGuiApp_ImplWin32D2DDXGI_Data* bd, HWND hwnd)
{
    if (!bd->NoBlurBehind)
    {
        const MARGINS margins = { 1, 1, -1, 1 };
        ::DwmExtendFrameIntoClientArea(hwnd, &margins);
        ImGuiApp_ImplWin32D2DDXGI_EnableBlurBehind(hwnd);
    }
    if (!bd->NoDarkModeLadder)
        ImGuiApp_ImplWin32D2DDXGI_EnableDarkMode(hwnd);
}

// The reference's WaitForVerticalBlank: the first call only opens the adapter (no wait); later calls block
// until vblank then spin until scan-out resumes. The spin is bounded here (the reference spins unbounded;
// D3DKMTGetScanLine fails persistently on indirect/remote adapters).
static bool ImGuiApp_ImplWin32D2DDXGI_WaitForVerticalBlank(ImGuiApp_ImplWin32D2DDXGI_Data* bd, HWND hwnd)
{
    if (bd->VBlankAdapter == 0)
    {
        D3DKMT_OPENADAPTERFROMHDC oa = {};
        oa.hDc = ::GetDC(hwnd);
        const LONG status = D3DKMTOpenAdapterFromHdc(&oa);
        ::ReleaseDC(hwnd, oa.hDc);
        if (status != 0)
            return false;
        bd->VBlankAdapter  = oa.hAdapter;
        bd->VBlankSourceId = oa.VidPnSourceId;
        return false;
    }

    D3DKMT_WAITFORVERTICALBLANKEVENT vbe = {};
    vbe.hAdapter      = bd->VBlankAdapter;
    vbe.VidPnSourceId = bd->VBlankSourceId;
    if (D3DKMTWaitForVerticalBlankEvent(&vbe) != 0)
        return false;

    D3DKMT_GETSCANLINE gsl = {};
    gsl.hAdapter      = bd->VBlankAdapter;
    gsl.VidPnSourceId = bd->VBlankSourceId;
    LONG status = 0;
    int  guard  = 0;
    do
    {
        status = D3DKMTGetScanLine(&gsl);
    } while ((status != 0 || gsl.InVerticalBlank) && ++guard < 100000);
    return true;
}

static float ImGuiApp_ImplWin32D2DDXGI_PacerPrimaryRefreshHz();   // pacer seam, defined with its family below

// DCompositionWaitForCompositorClock (dcomp.dll, Win10 2004+): blocks until the next compositor tick OR
// one of the given handles signals -- the exact primitive for a wakeable vblank-paced thread. Loaded once
// (process-global fact, like the uxtheme ordinals); older systems fall back to the tiers below.
typedef DWORD (WINAPI* ImGuiApp_ImplWin32D2DDXGI_PFN_WaitForCompositorClock)(UINT count, const HANDLE* handles, DWORD timeout_ms);
static ImGuiApp_ImplWin32D2DDXGI_PFN_WaitForCompositorClock ImGuiApp_ImplWin32D2DDXGI_WaitForCompositorClock = nullptr;
static bool ImGuiApp_ImplWin32D2DDXGI_WaitForCompositorClockLoaded = false;

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// Pace thread body: entirely event-driven (no polling sleeps). Parked on the wake event while no modal
// loop is live; while live, waits per tick on the best available clock:
//   1. DCompositionWaitForCompositorClock -- compositor tick, multiplexed with the wake event.
//   2. D3DKMTWaitForVerticalBlankEvent on a per-thread adapter -- hardware vblank (state changes are
//      observed within one blank; the wait cannot be multiplexed).
//   3. A high-resolution waitable timer at the primary refresh period, multiplexed with the wake event.
static DWORD WINAPI ImGuiApp_ImplWin32D2DDXGI_PaceThreadProc(LPVOID param)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = (ImGuiApp_ImplWin32D2DDXGI_Data*)param;
    HWND hwnd = (HWND)bd->Hwnd;

    D3DKMT_OPENADAPTERFROMHDC oa = {};
    oa.hDc = ::GetDC(hwnd);
    LONG open_status = D3DKMTOpenAdapterFromHdc(&oa);
    ::ReleaseDC(hwnd, oa.hDc);
    HANDLE timer = nullptr;

    while (!bd->PaceThreadStop)
    {
        if (!bd->PaceModalLive)
        {
            ::WaitForSingleObject(bd->PaceWake, INFINITE);   // parked: the run loop paces itself outside modal loops
            continue;
        }

        bool tick = false;
        if (ImGuiApp_ImplWin32D2DDXGI_WaitForCompositorClock != nullptr)
        {
            const DWORD wait = ImGuiApp_ImplWin32D2DDXGI_WaitForCompositorClock(1, &bd->PaceWake, INFINITE);
            tick = (wait == WAIT_OBJECT_0 + 1);   // WAIT_OBJECT_0 = state-change wake; re-evaluate
        }
        else if (open_status == 0)
        {
            D3DKMT_WAITFORVERTICALBLANKEVENT vbe = {};
            vbe.hAdapter      = oa.hAdapter;
            vbe.VidPnSourceId = oa.VidPnSourceId;
            if (D3DKMTWaitForVerticalBlankEvent(&vbe) == 0)
                tick = true;
            else
                open_status = -1;   // adapter lost (remote/indirect display): drop to the timer tier
        }
        else
        {
            if (timer == nullptr)
            {
                timer = ::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
                if (timer == nullptr)
                    timer = ::CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
                if (timer == nullptr)
                    break;   // no clock source at all; the WM_TIMER fallback still paces the modal loop
                float hz = ImGuiApp_ImplWin32D2DDXGI_PacerPrimaryRefreshHz();
                if (hz <= 0.0f)
                    hz = 60.0f;
                LARGE_INTEGER due;
                due.QuadPart = -(LONGLONG)(10000000.0 / hz);   // 100ns units, relative
                ::SetWaitableTimer(timer, &due, (LONG)(1000.0f / hz + 0.5f), nullptr, nullptr, FALSE);
            }
            const HANDLE handles[2] = { bd->PaceWake, timer };
            const DWORD wait = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            tick = (wait == WAIT_OBJECT_0 + 1);
        }

        if (tick && !bd->PaceThreadStop && bd->PaceModalLive &&
            ::InterlockedCompareExchange(&bd->PacePending, 1, 0) == 0)
            ::PostMessage(hwnd, IMGUIAPP_WIN32D2DDXGI_WM_PACE, 0, 0);
    }

    if (timer != nullptr)
    {
        ::CancelWaitableTimer(timer);
        ::CloseHandle(timer);
    }
    if (open_status == 0)
    {
        D3DKMT_CLOSEADAPTER close = { oa.hAdapter };
        D3DKMTCloseAdapter(&close);
    }
    return 0;
}

//--------------------------------------------------------------------------------------------------------
// Device creation ladder (transcribed from ImmersiveWindow.c OnInitD2D)
//--------------------------------------------------------------------------------------------------------

// Canonical composition swapchain description: primary desktop resolution, premultiplied BGRA flip-discard,
// tearing + mode-switch + frame-latency-waitable. This buffer is NEVER resized -- the window draws into a sub-rect.
static DXGI_SWAP_CHAIN_DESC1 ImGuiApp_ImplWin32D2DDXGI_GetSwapchainDescription(const ImGuiApp_ImplWin32D2DDXGI_InitInfo* init_info)
{
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SwapEffect         = init_info->PresentDirtyRects ? DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL : DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode          = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Scaling            = DXGI_SCALING_STRETCH;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    if (init_info->SwapchainWidth > 0 && init_info->SwapchainHeight > 0)
    {
        desc.Width  = (UINT)init_info->SwapchainWidth;
        desc.Height = (UINT)init_info->SwapchainHeight;
    }
    else
    {
        HDC dc = ::GetDC(nullptr);
        desc.Width  = (UINT)::GetDeviceCaps(dc, DESKTOPHORZRES);
        desc.Height = (UINT)::GetDeviceCaps(dc, DESKTOPVERTRES);
        ::ReleaseDC(nullptr, dc);
    }
    desc.BufferCount = init_info->BufferCount > 0 ? (UINT)init_info->BufferCount : 5; // canonical "3 + 1 + 1"
    desc.Flags       = init_info->SwapchainFlags != 0 ? init_info->SwapchainFlags :
        (DXGI_SWAPCHAIN_ENABLE_IMMEDIATE_PRESENT | DXGI_SWAPCHAIN_ENABLE_WAIT_FOR_NEXT_FRAME_RESOURCES);
    return desc;
}

static D2D1_BITMAP_PROPERTIES1 ImGuiApp_ImplWin32D2DDXGI_GetSwapchainSurfaceBitmapProperties()
{
    D2D1_BITMAP_PROPERTIES1 props = {};
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    props.pixelFormat.format    = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.bitmapOptions         = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    return props;
}

static bool ImGuiApp_ImplWin32D2DDXGI_CreateDeviceObjects(ImGuiApp_ImplWin32D2DDXGI_Data* bd, const ImGuiApp_ImplWin32D2DDXGI_InitInfo* init_info)
{
    HWND hwnd = (HWND)bd->Hwnd;
    HRESULT hr;

#ifndef NDEBUG
    const bool default_debug = true;
#else
    const bool default_debug = false;
#endif
    const bool debug = init_info->DebugLayer > 0 || (init_info->DebugLayer == 0 && default_debug);

    // Canonical feature-level array, order preserved verbatim: D3D11CreateDevice takes the FIRST level it
    // can create, so 10_0-first yields a feature-level 10_0 device (the reference's observed behavior).
    static const D3D_FEATURE_LEVEL c_feature_levels[] =
    {
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_12_1,
    };
    const D3D_FEATURE_LEVEL* levels = init_info->FeatureLevels != nullptr ? (const D3D_FEATURE_LEVEL*)init_info->FeatureLevels : c_feature_levels;
    const int levels_count          = init_info->FeatureLevels != nullptr ? init_info->FeatureLevelsCount : IM_ARRAYSIZE(c_feature_levels);

    UINT device_flags = init_info->DeviceFlags != 0 ? init_info->DeviceFlags :
        (D3D11_DEVICE_AUTHORITATIVE | D3D11_DEVICE_SINGLETHREADED | D3D11_DEVICE_D2D_COMPATIBLE);
    if (debug)
        device_flags |= D3D11_CREATE_DEVICE_DEBUG;

    hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, device_flags,
                             levels, (UINT)levels_count, D3D11_SDK_VERSION,
                             &bd->D3DDevice, nullptr, &bd->D3DDeviceContext);
    if (FAILED(hr))
        return false;

    if (debug)
        bd->D3DDevice->QueryInterface(IID_PPV_ARGS(&bd->D3DDebug));

    if (FAILED(hr = bd->D3DDevice->QueryInterface(IID_PPV_ARGS(&bd->DXGIDevice))))
        return false;
    if (init_info->MaxFrameLatency > 0) // canonical: not called (the reference keeps its call commented out)
        bd->DXGIDevice->SetMaximumFrameLatency((UINT)init_info->MaxFrameLatency);

    const DXGI_SWAP_CHAIN_DESC1   sc_desc      = ImGuiApp_ImplWin32D2DDXGI_GetSwapchainDescription(init_info);
    const D2D1_FACTORY_OPTIONS    d2d_options  = { D2D1_DEBUG_LEVEL_INFORMATION };
    const D2D1_BITMAP_PROPERTIES1 bitmap_props = ImGuiApp_ImplWin32D2DDXGI_GetSwapchainSurfaceBitmapProperties();

    if (FAILED(hr = ::CreateDXGIFactory2(debug ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&bd->DXGIFactory))))
        return false;

    IDXGISwapChain1* swapchain1 = nullptr;
    if (FAILED(hr = bd->DXGIFactory->CreateSwapChainForComposition(bd->DXGIDevice, &sc_desc, nullptr, &swapchain1)))
        return false;
    hr = swapchain1->QueryInterface(IID_PPV_ARGS(&bd->Swapchain));
    swapchain1->Release();
    if (FAILED(hr))
        return false;

    bd->DXGIFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_PRINT_SCREEN);

    if (FAILED(hr = ::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory2), &d2d_options, (void**)&bd->D2DFactory)))
        return false;
    if (FAILED(hr = bd->D2DFactory->CreateDevice(bd->DXGIDevice, &bd->D2DDevice)))
        return false;
    if (FAILED(hr = bd->D2DDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &bd->D2DDeviceContext)))
        return false;

    // Bind buffer 0 ONCE: D3D11 flip-model buffer 0 aliases the current back buffer, and this swapchain is
    // never resized, so the D2D target bitmap and the RTV below stay valid for the swapchain's lifetime.
    if (FAILED(hr = bd->Swapchain->GetBuffer(0, IID_PPV_ARGS(&bd->SwapchainSurface))))
        return false;
    if (FAILED(hr = bd->D2DDeviceContext->CreateBitmapFromDxgiSurface(bd->SwapchainSurface, &bitmap_props, &bd->D2DTargetBitmap)))
        return false;
    bd->D2DDeviceContext->SetTarget(bd->D2DTargetBitmap);

    if (FAILED(hr = ::DCompositionCreateDevice(bd->DXGIDevice, __uuidof(IDCompositionDevice), (void**)&bd->DCompDevice)))
        return false;
    if (FAILED(hr = bd->DCompDevice->CreateTargetForHwnd(hwnd, TRUE, &bd->DCompTarget)))
        return false;
    if (FAILED(hr = bd->DCompDevice->CreateVisual(&bd->DCompVisual)))
        return false;
    if (FAILED(hr = bd->DCompVisual->SetContent(bd->Swapchain)))
        return false;
    if (FAILED(hr = bd->DCompTarget->SetRoot(bd->DCompVisual)))
        return false;
    if (FAILED(hr = bd->DCompDevice->Commit()))
        return false;

    bd->FrameLatencyWaitableObject = bd->Swapchain->GetFrameLatencyWaitableObject();

    if (FAILED(hr = bd->Swapchain->GetBuffer(0, IID_PPV_ARGS(&bd->BackBuffer))))
        return false;
    if (FAILED(hr = bd->D3DDevice->CreateRenderTargetView(bd->BackBuffer, nullptr, &bd->MainRTV)))
        return false;
    return true;
}

static void ImGuiApp_ImplWin32D2DDXGI_DestroyDeviceObjects(ImGuiApp_ImplWin32D2DDXGI_Data* bd)
{
    if (bd->CaptureStaging != nullptr)            { bd->CaptureStaging->Release(); bd->CaptureStaging = nullptr; }
    if (bd->MainRTV != nullptr)                   { bd->MainRTV->Release(); bd->MainRTV = nullptr; }
    if (bd->BackBuffer != nullptr)                { bd->BackBuffer->Release(); bd->BackBuffer = nullptr; }
    if (bd->FrameLatencyWaitableObject != nullptr) { ::CloseHandle(bd->FrameLatencyWaitableObject); bd->FrameLatencyWaitableObject = nullptr; }
    if (bd->DCompVisual != nullptr)               { bd->DCompVisual->Release(); bd->DCompVisual = nullptr; }
    if (bd->DCompTarget != nullptr)               { bd->DCompTarget->Release(); bd->DCompTarget = nullptr; }
    if (bd->DCompDevice != nullptr)               { bd->DCompDevice->Release(); bd->DCompDevice = nullptr; }
    if (bd->D2DTargetBitmap != nullptr)           { bd->D2DTargetBitmap->Release(); bd->D2DTargetBitmap = nullptr; }
    if (bd->D2DDeviceContext != nullptr)          { bd->D2DDeviceContext->Release(); bd->D2DDeviceContext = nullptr; }
    if (bd->D2DDevice != nullptr)                 { bd->D2DDevice->Release(); bd->D2DDevice = nullptr; }
    if (bd->D2DFactory != nullptr)                { bd->D2DFactory->Release(); bd->D2DFactory = nullptr; }
    if (bd->SwapchainSurface != nullptr)          { bd->SwapchainSurface->Release(); bd->SwapchainSurface = nullptr; }
    if (bd->Swapchain != nullptr)                 { bd->Swapchain->Release(); bd->Swapchain = nullptr; }
    if (bd->DXGIFactory != nullptr)               { bd->DXGIFactory->Release(); bd->DXGIFactory = nullptr; }
    if (bd->DXGIDevice != nullptr)                { bd->DXGIDevice->Release(); bd->DXGIDevice = nullptr; }
    if (bd->D3DDeviceContext != nullptr)          { bd->D3DDeviceContext->Release(); bd->D3DDeviceContext = nullptr; }
    if (bd->D3DDebug != nullptr)
    {
        bd->D3DDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
        bd->D3DDebug->Release();
        bd->D3DDebug = nullptr;
    }
    if (bd->D3DDevice != nullptr)                 { bd->D3DDevice->Release(); bd->D3DDevice = nullptr; }
    if (bd->VBlankAdapter != 0)
    {
        D3DKMT_CLOSEADAPTER close = { bd->VBlankAdapter };
        D3DKMTCloseAdapter(&close);
        bd->VBlankAdapter = 0;
    }
}

//--------------------------------------------------------------------------------------------------------
// Present ladder + modal repaint (transcribed from ImmersiveWindow.c EndImmersivePaint / OnNCCalcSize / OnTimer)
//--------------------------------------------------------------------------------------------------------

// The reference's two-step present: flush the front buffer immediately (optionally cancelling queued frames),
// then either replace it in sync (DO_NOT_SEQUENCE) or queue the next immediate present. In dirty-rect mode
// (FLIP_SEQUENTIAL) each step is a Present1 declaring the client rect as the only damage -- the compositor
// recomposes just that sub-rect of the desktop-sized buffer. The first present must be full-frame (DXGI rule).
static void ImGuiApp_ImplWin32D2DDXGI_PresentLadder(ImGuiApp_ImplWin32D2DDXGI_Data* bd, bool restart, bool vsync)
{
    if (bd->Swapchain == nullptr)
        return;
    // Dirty-rect mode drops ALLOW_TEARING (invalid alongside partial-presentation parameters, and
    // meaningless through the compositor).
    const UINT tearing      = bd->PresentDirtyRects ? 0 : DXGI_PRESENT_ALLOW_TEARING;
    const UINT flags_first  = tearing | DXGI_PRESENT_DO_NOT_WAIT | (restart ? DXGI_PRESENT_RESTART : 0);
    const UINT flags_second = (restart || vsync) ? DXGI_PRESENT_DO_NOT_SEQUENCE : (tearing | DXGI_PRESENT_DO_NOT_WAIT);
    const UINT sync_second  = (restart || vsync) ? 1 : 0;
    HRESULT hr;
    if (bd->PresentDirtyRects)
    {
        RECT dirty = {};
        ::GetClientRect((HWND)bd->Hwnd, &dirty);
        DXGI_PRESENT_PARAMETERS params = {};
        if (bd->PresentedOnce && dirty.right > 0 && dirty.bottom > 0)
        {
            params.DirtyRectsCount = 1;
            params.pDirtyRects     = &dirty;
        }
        DXGI_PRESENT_PARAMETERS full = {};   // the DO_NOT_SEQUENCE replace re-presents the SAME buffer; partial parameters are invalid there
        hr = bd->Swapchain->Present1(0, flags_first, &params);
        IM_ASSERT(SUCCEEDED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING);
        hr = bd->Swapchain->Present1(sync_second, flags_second, &full);
        IM_ASSERT(SUCCEEDED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING);
    }
    else
    {
        hr = bd->Swapchain->Present(0, flags_first);
        IM_ASSERT(SUCCEEDED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING);
        hr = bd->Swapchain->Present(sync_second, flags_second);   // DO_NOT_SEQUENCE: the current buffer, ahead and instead of the would-be next one
        IM_ASSERT(SUCCEEDED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING);
    }
    bd->PresentedOnce = true;
}

// True when the last modal repaint already produced content for the UPCOMING compositor frame at the
// window's CURRENT client size -- another repaint before that frame starts would be pure queue churn.
static bool ImGuiApp_ImplWin32D2DDXGI_ModalContentCurrent(ImGuiApp_ImplWin32D2DDXGI_Data* bd, HWND hwnd)
{
    if (bd->DCompDevice == nullptr)
        return false;
    DCOMPOSITION_FRAME_STATISTICS stats = {};
    if (FAILED(bd->DCompDevice->GetFrameStatistics(&stats)))
        return false;
    if (stats.nextEstimatedFrameTime.QuadPart != bd->ModalLatchedFrameTime)
        return false;
    RECT rect = {};
    if (!::GetClientRect(hwnd, &rect))
        return false;
    return (rect.right - rect.left) == bd->ModalLatchedWidth && (rect.bottom - rect.top) == bd->ModalLatchedHeight;
}

// Content pin (see the ImGuiApp_ImplWin32D2DDXGI_Data fields): geometry outran the presented content --
// pin the content at the screen position it was rendered for via a compensating visual offset. Cheap
// enough (one batched property + Commit) to run inside WM_WINDOWPOSCHANGED with no repaint at all.
static void ImGuiApp_ImplWin32D2DDXGI_PinContent(ImGuiApp_ImplWin32D2DDXGI_Data* bd, HWND hwnd)
{
    if (bd->DCompVisual == nullptr || bd->DCompDevice == nullptr || !bd->ContentOriginValid)
        return;
    RECT rect = {};
    if (!::GetWindowRect(hwnd, &rect))
        return;
    const int dx = bd->ContentOriginX - rect.left;
    const int dy = bd->ContentOriginY - rect.top;
    if (dx == 0 && dy == 0 && !bd->VisualOffsetActive)
        return;
    bd->DCompVisual->SetOffsetX((float)dx);
    bd->DCompVisual->SetOffsetY((float)dy);
    bd->DCompDevice->Commit();
    bd->VisualOffsetActive = (dx != 0 || dy != 0);
}

// Unpin + re-anchor, called right after every main-swapchain present: the offset reset rides the same
// compositor latch as the present (both pending together), so fresh content never shows pre-translated.
static void ImGuiApp_ImplWin32D2DDXGI_UnpinContent(ImGuiApp_ImplWin32D2DDXGI_Data* bd)
{
    HWND hwnd = (HWND)bd->Hwnd;
    if (bd->VisualOffsetActive && bd->DCompVisual != nullptr && bd->DCompDevice != nullptr)
    {
        bd->DCompVisual->SetOffsetX(0.0f);
        bd->DCompVisual->SetOffsetY(0.0f);
        bd->DCompDevice->Commit();
        bd->VisualOffsetActive = false;
    }
    RECT window_rect = {};
    RECT client_rect = {};
    if (::GetWindowRect(hwnd, &window_rect) && ::GetClientRect(hwnd, &client_rect))
    {
        bd->ContentOriginX     = window_rect.left;
        bd->ContentOriginY     = window_rect.top;
        bd->ContentWidth       = client_rect.right - client_rect.left;
        bd->ContentHeight      = client_rect.bottom - client_rect.top;
        bd->ContentOriginValid = true;
    }
}

// WndProc-driven repaint (resize/move/modal loops). Default = a full app Frame() with the present flavor
// overridden to the reference's (fRestart, fVsync) pair for that path; render-only mode just re-presents.
// wait_for_vblank matches the reference per path: WM_NCCALCSIZE and the modal timer wait, WM_WINDOWPOSCHANGED
// must NOT -- its repaint corrects an already-committed origin change and any delay flashes a shifted stale frame.
static void ImGuiApp_ImplWin32D2DDXGI_ModalRepaint(ImGuiApp* app, ImGuiApp_ImplWin32D2DDXGI_Data* bd, HWND hwnd, bool restart, bool vsync, bool wait_for_vblank)
{
    if (app == nullptr || bd == nullptr || bd->Swapchain == nullptr || bd->InModalRepaint || bd->InFrame || !app->IsInitialized())
        return;
    bd->InModalRepaint = true;
    if (wait_for_vblank && bd->FrameLatencyWaitableObject != nullptr)
        ::WaitForSingleObject(bd->FrameLatencyWaitableObject, 0);   // the reference's WaitForNextFrameResource: zero-timeout poll
    if (wait_for_vblank && !bd->NoWaitForVBlank)
        ImGuiApp_ImplWin32D2DDXGI_WaitForVerticalBlank(bd, hwnd);

    // Stamp the compositor frame this repaint targets + the client size it draws (see ModalContentCurrent).
    DCOMPOSITION_FRAME_STATISTICS stats = {};
    if (bd->DCompDevice != nullptr && SUCCEEDED(bd->DCompDevice->GetFrameStatistics(&stats)))
        bd->ModalLatchedFrameTime = stats.nextEstimatedFrameTime.QuadPart;
    RECT rect = {};
    if (::GetClientRect(hwnd, &rect))
    {
        bd->ModalLatchedWidth  = rect.right - rect.left;
        bd->ModalLatchedHeight = rect.bottom - rect.top;
    }

    if (bd->ModalRepaintRenderOnly)
    {
        ImGuiApp_ImplWin32D2DDXGI_PresentLadder(bd, restart, vsync);
        ImGuiApp_ImplWin32D2DDXGI_UnpinContent(bd);   // the full-frame path unpins in PresentFrame
    }
    else
    {
        bd->ModalPresentActive  = true;
        bd->ModalPresentRestart = restart;
        bd->ModalPresentVsync   = vsync;
        app->Frame();
        bd->ModalPresentActive = false;
    }
    bd->InModalRepaint = false;
}

//--------------------------------------------------------------------------------------------------------
// Frame capture (AV readback)
//--------------------------------------------------------------------------------------------------------

// Synchronous client-rect readback of buffer 0: CopySubresourceRegion into a staging texture, Map, and
// BGRA->RGBA swizzle. Every call returns the CURRENT frame (encode phase runs before present); a repeat
// call with no new frame rendered returns false.
static bool ImGuiApp_ImplWin32D2DDXGI_CaptureFrame(ImGuiApp* app, ImGuiAppAVFrame* out_frame)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);
    if (bd == nullptr || out_frame == nullptr || bd->Swapchain == nullptr || bd->BackBuffer == nullptr)
        return false;
    if (app->FrameID.FrameIndex <= bd->CaptureLastReturned)
        return false;   // drain/gap call: this frame was already handed out

    RECT rect = {};
    if (!::GetClientRect((HWND)bd->Hwnd, &rect))
        return false;
    const int width  = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0)
        return false;

    if (bd->CaptureStaging == nullptr || bd->CaptureStagingWidth != width || bd->CaptureStagingHeight != height)
    {
        if (bd->CaptureStaging != nullptr)
        {
            bd->CaptureStaging->Release();
            bd->CaptureStaging = nullptr;
        }
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width            = (UINT)width;
        desc.Height           = (UINT)height;
        desc.MipLevels        = 1;
        desc.ArraySize        = 1;
        desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage            = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;
        if (FAILED(bd->D3DDevice->CreateTexture2D(&desc, nullptr, &bd->CaptureStaging)))
            return false;
        bd->CaptureStagingWidth  = width;
        bd->CaptureStagingHeight = height;
    }

    D3D11_BOX box = { 0, 0, 0, (UINT)width, (UINT)height, 1 };
    bd->D3DDeviceContext->CopySubresourceRegion(bd->CaptureStaging, 0, 0, 0, 0, bd->BackBuffer, 0, &box);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(bd->D3DDeviceContext->Map(bd->CaptureStaging, 0, D3D11_MAP_READ, 0, &mapped)))
        return false;

    const int row_bytes = width * 4;
    bd->CaptureRgba.resize(row_bytes * height);
    for (int y = 0; y < height; y++)
        memcpy(bd->CaptureRgba.Data + (size_t)y * row_bytes, (const char*)mapped.pData + (size_t)y * mapped.RowPitch, (size_t)row_bytes);
    bd->D3DDeviceContext->Unmap(bd->CaptureStaging, 0);

    // In-place BGRA -> RGBA word swizzle (same shape as the vulkan host's conversion).
    unsigned int*  px = (unsigned int*)bd->CaptureRgba.Data;
    const size_t   n  = (size_t)width * (size_t)height;
    for (size_t i = 0; i < n; i++)
    {
        const unsigned int v = px[i];
        px[i] = (v & 0xFF00FF00u) | ((v & 0x000000FFu) << 16) | ((v >> 16) & 0x000000FFu);
    }

    out_frame->Width      = width;
    out_frame->Height     = height;
    out_frame->PitchBytes = row_bytes;
    out_frame->Pixels     = bd->CaptureRgba.Data;
    out_frame->FrameID    = app->FrameID;   // synchronous path: pixels ARE the current frame
    bd->CaptureLastReturned = app->FrameID.FrameIndex;
    return true;
}

//--------------------------------------------------------------------------------------------------------
// Frame lifecycle
//--------------------------------------------------------------------------------------------------------

bool ImGuiApp_ImplWin32D2DDXGI_Init(ImGuiApp* app, const ImGuiApp_ImplWin32D2DDXGI_InitInfo* init_info)
{
    IMGUI_CHECKVERSION();
    IM_ASSERT(app != nullptr && app->BackendData == nullptr && "Already initialized a platform backend!");
    IM_ASSERT(ImGuiApp_ImplWin32D2DDXGI_IsInitInfoValid(init_info) && "ImGuiApp_ImplWin32D2DDXGI_Init: invalid init_info.");
    if (app == nullptr || app->BackendData != nullptr || !ImGuiApp_ImplWin32D2DDXGI_IsInitInfoValid(init_info))
        return false;

    ImGuiApp_ImplWin32D2DDXGI_Data* bd = IM_NEW(ImGuiApp_ImplWin32D2DDXGI_Data)();
    app->BackendData = bd;
    bd->App  = app;
    bd->Hwnd = init_info->Hwnd;
    bd->CaptionHeight         = init_info->CaptionHeight > 0 ? init_info->CaptionHeight : 30;
    bd->PresentMode           = init_info->PresentMode;
    bd->PresentDirtyRects     = init_info->PresentDirtyRects;
    bd->NoWaitForVBlank       = init_info->NoWaitForVBlank;
    bd->ModalRepaintRenderOnly = init_info->ModalRepaintRenderOnly;
    bd->EnableClear           = init_info->EnableClear;
    bd->NoBlurBehind          = init_info->NoBlurBehind;
    bd->NoDarkModeLadder      = init_info->NoDarkModeLadder;

    if (!ImGui_ImplWin32_Init(init_info->Hwnd))
    {
        ImGuiApp_ImplWin32D2DDXGI_Shutdown(app);
        return false;
    }
    bd->PlatformBackendInitialized = true;

    if (!ImGuiApp_ImplWin32D2DDXGI_CreateDeviceObjects(bd, init_info))
    {
        ImGuiApp_ImplWin32D2DDXGI_Shutdown(app);
        return false;
    }

    if (!ImGui_ImplDX11_Init(bd->D3DDevice, bd->D3DDeviceContext))
    {
        ImGuiApp_ImplWin32D2DDXGI_Shutdown(app);
        return false;
    }
    bd->RendererBackendInitialized = true;

    // Modal-loop pacing: keeps repaints at monitor refresh rate through size/move/menu loops.
    if (!ImGuiApp_ImplWin32D2DDXGI_WaitForCompositorClockLoaded)
    {
        ImGuiApp_ImplWin32D2DDXGI_WaitForCompositorClockLoaded = true;
        if (HMODULE dcomp = ::GetModuleHandleA("dcomp.dll"))
            ImGuiApp_ImplWin32D2DDXGI_WaitForCompositorClock = (ImGuiApp_ImplWin32D2DDXGI_PFN_WaitForCompositorClock)(void*)::GetProcAddress(dcomp, "DCompositionWaitForCompositorClock");
    }
    bd->PaceWake   = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    bd->PaceThread = bd->PaceWake != nullptr ? ::CreateThread(nullptr, 0, ImGuiApp_ImplWin32D2DDXGI_PaceThreadProc, bd, 0, nullptr) : nullptr;

    if (!init_info->NoChrome)
        ImGuiApp_ImplWin32D2DDXGI_InstallChrome(app);   // canonical caption chrome; failure (fonts absent) just leaves it off
    return true;
}

void ImGuiApp_ImplWin32D2DDXGI_Shutdown(ImGuiApp* app)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);
    IM_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
    if (bd == nullptr)
        return;

    if (bd->PaceThread != nullptr)
    {
        bd->PaceThreadStop = true;
        ::SetEvent(bd->PaceWake);   // wakes a parked or clock-multiplexed wait; a bare vblank wait exits within one blank
        ::WaitForSingleObject(bd->PaceThread, 2000);
        ::CloseHandle(bd->PaceThread);
        bd->PaceThread = nullptr;
    }
    if (bd->PaceWake != nullptr)
    {
        ::CloseHandle(bd->PaceWake);
        bd->PaceWake = nullptr;
    }

    ImGuiApp_ImplWin32D2DDXGI_UninstallChrome(app);   // chrome brushes/fonts ride the D2D context released below

    if (bd->RendererBackendInitialized)
        ImGui_ImplDX11_Shutdown();   // destroys the platform windows, reaching our viewport hooks (bd must stay alive)
    if (bd->PlatformBackendInitialized)
        ImGui_ImplWin32_Shutdown();

    ImGuiApp_ImplWin32D2DDXGI_DestroyDeviceObjects(bd);

    app->BackendData = nullptr;
    IM_DELETE(bd);
}

void ImGuiApp_ImplWin32D2DDXGI_NewFrame(ImGuiApp* app)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);
    IM_ASSERT(bd != nullptr && "App or backend not initialized! Did you call ImGuiApp_ImplWin32D2DDXGI_Init()?");
    bd->InFrame = true;   // cleared at the end of PresentFrame; blocks WndProc repaint re-entry
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
}

void ImGuiApp_ImplWin32D2DDXGI_RenderDrawData(ImGuiApp* app, ImDrawData* draw_data, const ImGuiAppFrameConfig* config)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);
    IM_ASSERT(bd != nullptr && "App or backend not initialized! Did you call ImGuiApp_ImplWin32D2DDXGI_Init()?");
    if (bd == nullptr || bd->MainRTV == nullptr)
        return;

    // The desktop-sized target is bound whole; imgui's render state scopes the viewport to
    // draw_data->DisplaySize, so the frame lands in the window's client-rect sub-region.
    bd->D3DDeviceContext->OMSetRenderTargets(1, &bd->MainRTV, nullptr);

    // Canonical behavior never clears (stale pixels beyond the client rect stay clipped by the window).
    if (bd->EnableClear && (config->Flags & ImGuiAppFrameFlags_NoClear) == 0)
    {
        const ImVec4& c = config->ClearColor;
        const float clear_color[4] = { c.x * c.w, c.y * c.w, c.z * c.w, c.w };   // premultiplied-alpha swapchain
        bd->D3DDeviceContext->ClearRenderTargetView(bd->MainRTV, clear_color);
    }

    if (bd->PreDrawCallback != nullptr)
        bd->PreDrawCallback(app);

    ImGui_ImplDX11_RenderDrawData(draw_data);

    // Client D2D pass over the imgui frame (the reference's chrome slot), bracketed with the canonical
    // target state: identity transform, ClearType text, per-primitive antialiasing.
    if (bd->DrawCallback != nullptr && bd->D2DDeviceContext != nullptr)
    {
        bd->D2DDeviceContext->BeginDraw();
        bd->D2DDeviceContext->SetTransform(D2D1::Matrix3x2F::Identity());
        bd->D2DDeviceContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
        bd->D2DDeviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        bd->DrawCallback(app);
        HRESULT hr = bd->D2DDeviceContext->EndDraw();
        IM_ASSERT(SUCCEEDED(hr));
        IM_UNUSED(hr);
    }

    if ((config->Flags & ImGuiAppFrameFlags_NoPlatformWindows) == 0 &&
        (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

// Present phase: the encode phase runs between RenderDrawData and this, reading
// back the frame just rendered before it goes on screen.
void ImGuiApp_ImplWin32D2DDXGI_PresentFrame(ImGuiApp* app, const ImGuiAppFrameConfig* config)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);
    if (bd == nullptr || config == nullptr)
        return;
    bd->InFrame = false;   // the frame ends here regardless of the present decision below
    if ((config->Flags & ImGuiAppFrameFlags_NoPresent) != 0)
        return;

    bool restart = false;
    bool vsync   = false;
    if (bd->ModalPresentActive)   // WndProc repaint paths pin the reference's per-path (fRestart, fVsync) pair
    {
        restart = bd->ModalPresentRestart;
        vsync   = bd->ModalPresentVsync;
    }
    else
    {
        switch (bd->PresentMode)
        {
        case ImGuiApp_ImplWin32D2DDXGI_PresentMode_Vsync:     restart = false; vsync = true;  break;
        case ImGuiApp_ImplWin32D2DDXGI_PresentMode_Immediate: restart = false; vsync = false; break;
        case ImGuiApp_ImplWin32D2DDXGI_PresentMode_ImmediateRestart:
        default:                                            restart = true;  vsync = false; break;
        }
    }
    ImGuiApp_ImplWin32D2DDXGI_PresentLadder(bd, restart, vsync);
    ImGuiApp_ImplWin32D2DDXGI_UnpinContent(bd);   // offset reset rides the same compositor latch as this present
}

//--------------------------------------------------------------------------------------------------------
// Immersive window procedure (transcribed from ImmersiveWindow.c DefImmersiveProc)
//--------------------------------------------------------------------------------------------------------

// Default immersive hittest: resize borders from the reference's clamp arithmetic, then the caption strip.
// Button sub-rects are the client's business (imgui draws the caption widgets; override via the callback).
// Win32X dwmframex_v2 DwfHitTest shape, in CLIENT coords: 3x3 resize ring from the window borders
// (left/right/bottom borders sit OUTSIDE the client rect -- WM_NCCALCSIZE insets them; the top border
// rides INSIDE it), then the caption strip, then client.
static LRESULT ImGuiApp_ImplWin32D2DDXGI_DefaultNCHitTest(ImGuiApp_ImplWin32D2DDXGI_Data* bd, HWND hwnd, int x, int y)
{
    POINT pt = { (LONG)x, (LONG)y };
    ::ScreenToClient(hwnd, &pt);
    RECT client = {};
    ::GetClientRect(hwnd, &client);
    const bool maximized = ::IsZoomed(hwnd) != FALSE;
    const bool sizable   = (::GetWindowLongPtr(hwnd, GWL_STYLE) & WS_THICKFRAME) != 0 && !maximized;
    if (sizable)
    {
        SIZE border;
        ImGuiApp_ImplWin32D2DDXGI_GetWindowBorders(hwnd, &border);
        int row = 1, col = 1;
        if (pt.y < border.cy)             row = 0;
        else if (pt.y >= client.bottom)   row = 2;
        if (pt.x < 0)                     col = 0;
        else if (pt.x >= client.right)    col = 2;
        if (row == 0) return col == 0 ? HTTOPLEFT    : col == 2 ? HTTOPRIGHT    : HTTOP;
        if (row == 2) return col == 0 ? HTBOTTOMLEFT : col == 2 ? HTBOTTOMRIGHT : HTBOTTOM;
        if (col == 0) return HTLEFT;
        if (col == 2) return HTRIGHT;
    }
    const int caption = ::MulDiv(bd != nullptr ? bd->CaptionHeight : 30, (int)::GetDpiForWindow(hwnd), 96);
    if (pt.y < caption)
        return HTCAPTION;
    return HTCLIENT;
}

// The WM_NCHITTEST resolution order (client callback seam, then the default), reused by the
// capture-tracked press flow to re-test the hit at a screen point.
static int ImGuiApp_ImplWin32D2DDXGI_NCHitTestAt(ImGuiApp* app, ImGuiApp_ImplWin32D2DDXGI_Data* bd, HWND hwnd, int x, int y)
{
    if (bd != nullptr && bd->NCHitTestCallback != nullptr)
    {
        const int hit = bd->NCHitTestCallback(app, x, y);
        if (hit != -1)
            return hit;
    }
    return (int)ImGuiApp_ImplWin32D2DDXGI_DefaultNCHitTest(bd, hwnd, x, y);
}

// Caption-button press start (Win32X xxxTrackCaptionButton shape): a button hit captures the mouse;
// WM_MOUSEMOVE re-tests the hit while captured and WM_LBUTTONUP commits only if the release lands on
// the pressed button. DefWindowProc never sees the press (HTCLOSE would get the classic NC button
// behavior; the old HTMENU light/dark code entered a menu loop).
static bool ImGuiApp_ImplWin32D2DDXGI_NCButtonPress(ImGuiApp_ImplWin32D2DDXGI_Data* bd, HWND hwnd, WPARAM hit)
{
    if (bd == nullptr)
        return false;
    if (hit != HTCLOSE && hit != HTMINBUTTON && hit != HTMAXBUTTON && hit != IMGUIAPP_WIN32D2DDXGI_HTLIGHTDARK)
        return false;
    bd->NCPressedHit = (int)hit;
    ::SetCapture(hwnd);
    return true;
}

//--------------------------------------------------------------------------------------------------------
// Built-in caption chrome (transcribed from ImmersiveWindow.c BeginImmersivePaint + DrawImmersive* + OnNCHittest)
// Self-hosted on this backend's own client seams: InstallChrome registers the draw + hittest callbacks
// through the same public setters any client would use.
//--------------------------------------------------------------------------------------------------------

// The canonical caption glyph set (Win32X dwmframex_v2 g_dwfGlyphCp): Segoe Fluent Icons / Segoe MDL2
// Assets codepoints. Light mode shows QuietHours (moon), dark mode Brightness (sun) -- v1/v2 parity.
enum ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph
{
    ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_QuietHours = 0,   // 0xE708: light/dark button in light mode
    ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_Brightness,       // 0xE706: light/dark button in dark mode
    ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_Minimize,         // 0xE921 ChromeMinimize
    ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_Maximize,         // 0xE922 ChromeMaximize
    ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_Restore,          // 0xE923 ChromeRestore
    ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_Close,            // 0xE8BB ChromeClose
    ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_COUNT,
};
static const WCHAR IMGUIAPP_WIN32D2DDXGI_CHROME_GLYPHS[ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_COUNT] = { 0xE708, 0xE706, 0xE921, 0xE922, 0xE923, 0xE8BB };

// The reference's CAPTIONBUTTON hot-tracking states (only the drawn buttons kept).
enum ImGuiApp_ImplWin32D2DDXGI_CaptionButton
{
    ImGuiApp_ImplWin32D2DDXGI_CaptionButton_None = 0,
    ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Caption,
    ImGuiApp_ImplWin32D2DDXGI_CaptionButton_LightDark,
    ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Min,
    ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Max,
    ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Close,
};

struct ImGuiApp_ImplWin32D2DDXGI_ChromeData
{
    IDWriteFactory*        DWriteFactory;
    IDWriteTextFormat*     IconFormat;       // Segoe Fluent Icons / MDL2 fallback, centered both ways (Win32X DwfCreateIconFormat)
    IDWriteTextLayout*     GlyphLayouts[ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_COUNT]; // one cached layout per glyph, box = button cell
    UINT                   GlyphLayoutDpi;   // dpi the format + layouts were built at; a mismatch rebuilds them
    ID2D1Bitmap1*          IconBitmap;       // cached high-res caption icon; DrawBitmap downscales (Win32X DwfEnsureIcon)
    bool                   IconTried;        // attempted-once latch: a window with no icon stays iconless
    IDWriteTextFormat*     TitleFormat;      // system caption font (lfCaptionFont) at the window dpi
    IDWriteTextLayout*     TitleLayout;      // cached caption-title layout; dropped on WM_SETTEXT, rebuilt on dpi change
    UINT                   TitleDpi;         // dpi the title format + layout were built at
    ID2D1SolidColorBrush*  AnimBrush;        // the ONE recolored brush for animated fills + glyphs (Win32X DwfBrush)
    bool                   WndActive;        // current (target) window-activation state
    bool                   DarkFrom;         // dark state at crossfade start (the "from" color set)
    bool                   ActiveFrom;       // window-active state at crossfade start
    bool                   Anim;             // a 160ms crossfade is running
    float                  AnimT;            // crossfade progress 0..1
    ULONGLONG              AnimStartMs;
    ULONGLONG              LastTickMs;       // previous animation tick (per-button opacity ramp step)
    float                  BtnOpacity[6];    // per-button highlight opacity, indexed by ImGuiApp_ImplWin32D2DDXGI_CaptionButton_
    ID2D1SolidColorBrush*  Brush;            // light-mode surface / dark-mode text
    ID2D1SolidColorBrush*  RedBrush;         // close-button hot
    ID2D1SolidColorBrush*  BlackBrush;       // dark-mode surface / light-mode text
    ID2D1SolidColorBrush*  InactiveBrush;
    ID2D1SolidColorBrush*  ColorizationBrush;
    ID2D1SolidColorBrush*  FadedColorizationBrush;
    ID2D1StrokeStyle1*     StrokeStyle;
    int                    HotButton;        // ImGuiApp_ImplWin32D2DDXGI_CaptionButton_
    bool                   LightMode;
    bool                   DarkMode;
    ULONGLONG              LastLightDarkChangeMs; // 150ms debounce (reference)

    ImGuiApp_ImplWin32D2DDXGI_ChromeData() { memset((void*)this, 0, sizeof(*this)); }
};

#define IMGUIAPP_WIN32D2DDXGI_CHROME_ANIM_MS (160.0f)   // uDWM's shared crossfade timeline (Win32X DWF_ANIM_DURATION)

static inline D2D1_COLOR_F ImGuiApp_ImplWin32D2DDXGI_ChromeColor(COLORREF cr)
{
    return D2D1::ColorF(GetRValue(cr) / 255.0f, GetGValue(cr) / 255.0f, GetBValue(cr) / 255.0f, 1.0f);
}

static inline D2D1_COLOR_F ImGuiApp_ImplWin32D2DDXGI_ChromeLerp(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t)
{
    return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

// Caption-button glyph color for a (dark, active) state (Win32X DwfGlyphColor).
static inline D2D1_COLOR_F ImGuiApp_ImplWin32D2DDXGI_ChromeGlyphColor(bool dark, bool active)
{
    return ImGuiApp_ImplWin32D2DDXGI_ChromeColor(active ? (dark ? RGB(255, 255, 255) : RGB(0, 0, 0))
                                                        : (dark ? RGB(0xAA, 0xAA, 0xAA) : RGB(0x64, 0x64, 0x64)));
}

// Caption-title text color for a (dark, active) state: inactive dims to 60% alpha, like the shell (Win32X DwfTextColor).
static inline D2D1_COLOR_F ImGuiApp_ImplWin32D2DDXGI_ChromeTextColor(bool dark, bool active)
{
    D2D1_COLOR_F c = ImGuiApp_ImplWin32D2DDXGI_ChromeColor(dark ? RGB(255, 255, 255) : RGB(0, 0, 0));
    if (!active)
        c.a = 0.60f;
    return c;
}

// Target highlight opacity for a button: 1 when pressed, or hot with no other press; else 0 (Win32X DwfBtnTarget).
static inline float ImGuiApp_ImplWin32D2DDXGI_ChromeBtnTarget(const ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome, int pressed, int button)
{
    if (pressed == button)
        return 1.0f;
    if (chrome->HotButton == button && pressed == ImGuiApp_ImplWin32D2DDXGI_CaptionButton_None)
        return 1.0f;
    return 0.0f;
}

// Advance the crossfade + per-button opacities by wall-clock (Win32X DfwAdvance): the continuous render
// loop calls the draw callback every frame, so the timeline is driven here, not by a timer.
static void ImGuiApp_ImplWin32D2DDXGI_ChromeAdvance(ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome, int pressed)
{
    const ULONGLONG now = ::GetTickCount64();
    const float dt = (float)(now - chrome->LastTickMs) / IMGUIAPP_WIN32D2DDXGI_CHROME_ANIM_MS;
    chrome->LastTickMs = now;
    if (chrome->Anim)
    {
        float t = (float)(now - chrome->AnimStartMs) / IMGUIAPP_WIN32D2DDXGI_CHROME_ANIM_MS;
        if (t >= 1.0f) { t = 1.0f; chrome->Anim = false; }
        chrome->AnimT = t;
    }
    else
    {
        chrome->AnimT = 1.0f;
    }
    for (int i = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_LightDark; i <= ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Close; i++)
    {
        const float target = ImGuiApp_ImplWin32D2DDXGI_ChromeBtnTarget(chrome, pressed, i);
        float cur = chrome->BtnOpacity[i];
        if (cur < target)      { cur += dt; if (cur > target) cur = target; }
        else if (cur > target) { cur -= dt; if (cur < target) cur = target; }
        chrome->BtnOpacity[i] = cur;
    }
}

// Start a color crossfade toward (dark_to, active_to): capture the current shown state as the origin,
// arm the 160ms timeline (Win32X DwfBeginTransition). No-op when the target already matches -- the
// duplicate NCACTIVATE+ACTIVATE pair must not restart it. The caller owns flipping DarkMode itself.
static void ImGuiApp_ImplWin32D2DDXGI_ChromeBeginTransition(ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome, bool dark_to, bool active_to)
{
    if (chrome->DarkMode == dark_to && chrome->WndActive == active_to)
        return;
    chrome->DarkFrom    = chrome->DarkMode;   // current shown colors become the crossfade origin
    chrome->ActiveFrom  = chrome->WndActive;
    chrome->WndActive   = active_to;
    chrome->AnimStartMs = ::GetTickCount64();
    chrome->AnimT       = 0.0f;
    chrome->Anim        = true;
    chrome->LastTickMs  = chrome->AnimStartMs;
}

// Right-aligned caption-button cells in CLIENT coords (Win32X DwfButtonRects): slot buttons from the
// right (0 = close, 1 = maximize, 2 = minimize, 3 = light/dark), 47*dpi wide, caption-strip tall.
static void ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(const RECT& client, int caption_height, int button_width, int slot, RECT* out_rect)
{
    out_rect->right  = client.right - slot * button_width;
    out_rect->left   = out_rect->right - button_width;
    out_rect->top    = 0;
    out_rect->bottom = caption_height;
}

// The reference's EnableLightMode ladder, call order preserved.
static void ImGuiApp_ImplWin32D2DDXGI_EnableLightMode(HWND hwnd)
{
    if (ImGuiApp_ImplWin32D2DDXGI_SetPreferredAppMode != nullptr)
        ImGuiApp_ImplWin32D2DDXGI_SetPreferredAppMode(0); // PAM_DEFAULT
    ::SetPropA(hwnd, "UseImmersiveDarkModeColors", (HANDLE)0);
    if (ImGuiApp_ImplWin32D2DDXGI_RefreshImmersiveColorPolicyState != nullptr)
        ImGuiApp_ImplWin32D2DDXGI_RefreshImmersiveColorPolicyState();
    if (ImGuiApp_ImplWin32D2DDXGI_InvalidateAppTheme != nullptr)
        ImGuiApp_ImplWin32D2DDXGI_InvalidateAppTheme();
    if (ImGuiApp_ImplWin32D2DDXGI_FlushMenuThemes != nullptr)
        ImGuiApp_ImplWin32D2DDXGI_FlushMenuThemes();
    ::SetWindowTheme(hwnd, L"DarkMode_Explorer", L" ");
}

// (Re)build the icon text format + the cached per-glyph layouts at this dpi (Win32X DwfCreateIconFormat
// + DfwRefreshMetrics): glyph size = 0.36 * caption height (min 8), layout box = one button cell with
// center/center alignment, so DrawTextLayout at the cell origin centers the glyph -- no inset math,
// no per-frame layout allocation.
static bool ImGuiApp_ImplWin32D2DDXGI_ChromeEnsureGlyphLayouts(ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome, UINT dpi, int caption_height, int button_width)
{
    if (chrome->DWriteFactory == nullptr)
        return false;
    if (chrome->IconFormat != nullptr && chrome->GlyphLayoutDpi == dpi)
        return true;

    for (int i = 0; i < ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_COUNT; i++)
        if (chrome->GlyphLayouts[i] != nullptr) { chrome->GlyphLayouts[i]->Release(); chrome->GlyphLayouts[i] = nullptr; }
    if (chrome->IconFormat != nullptr) { chrome->IconFormat->Release(); chrome->IconFormat = nullptr; }

    float size = (float)caption_height * 0.36f;
    if (size < 8.0f)
        size = 8.0f;
    // CreateTextFormat does NOT validate the family name (a missing one silently falls back per glyph
    // and draws tofu), so probe the system collection: Segoe Fluent Icons ships on Win11 only, Segoe
    // MDL2 Assets on Win10+ carries the same chrome codepoints.
    const WCHAR* family = L"Segoe MDL2 Assets";
    IDWriteFontCollection* fonts = nullptr;
    if (SUCCEEDED(chrome->DWriteFactory->GetSystemFontCollection(&fonts, FALSE)))
    {
        UINT32 index  = 0;
        BOOL   exists = FALSE;
        if (SUCCEEDED(fonts->FindFamilyName(L"Segoe Fluent Icons", &index, &exists)) && exists)
            family = L"Segoe Fluent Icons";
        fonts->Release();
    }
    chrome->DWriteFactory->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &chrome->IconFormat);
    if (chrome->IconFormat == nullptr)
        return false;
    chrome->IconFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    chrome->IconFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    chrome->IconFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    for (int i = 0; i < ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_COUNT; i++)
        chrome->DWriteFactory->CreateTextLayout(&IMGUIAPP_WIN32D2DDXGI_CHROME_GLYPHS[i], 1, chrome->IconFormat, (float)button_width, (float)caption_height, &chrome->GlyphLayouts[i]);
    chrome->GlyphLayoutDpi = dpi;
    return true;
}

// (Re)build the cached caption-title layout (Win32X DwfCreateTextFormat + DfwRefreshMetrics): the
// system caption font (lfCaptionFont, the face uDWM's CDWriteText uses) at the window dpi, leading /
// vertically centered / no-wrap. WM_SETTEXT drops the layout; a dpi change rebuilds format + layout.
static void ImGuiApp_ImplWin32D2DDXGI_ChromeEnsureTitle(ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome, HWND hwnd, UINT dpi, int caption_height)
{
    if (chrome->DWriteFactory == nullptr || (chrome->TitleLayout != nullptr && chrome->TitleDpi == dpi))
        return;
    if (chrome->TitleLayout != nullptr) { chrome->TitleLayout->Release(); chrome->TitleLayout = nullptr; }

    if (chrome->TitleFormat == nullptr || chrome->TitleDpi != dpi)
    {
        if (chrome->TitleFormat != nullptr) { chrome->TitleFormat->Release(); chrome->TitleFormat = nullptr; }
        NONCLIENTMETRICSW ncm = {};
        ncm.cbSize = sizeof(ncm);
        if (!::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi))
            return;
        float size = (float)(ncm.lfCaptionFont.lfHeight < 0 ? -ncm.lfCaptionFont.lfHeight : ncm.lfCaptionFont.lfHeight);
        if (size < 1.0f)
            size = 12.0f;
        chrome->DWriteFactory->CreateTextFormat(ncm.lfCaptionFont.lfFaceName, nullptr, (DWRITE_FONT_WEIGHT)ncm.lfCaptionFont.lfWeight,
                                                ncm.lfCaptionFont.lfItalic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                                                DWRITE_FONT_STRETCH_NORMAL, size, L"", &chrome->TitleFormat);
        if (chrome->TitleFormat == nullptr)
            return;
        chrome->TitleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        chrome->TitleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        chrome->TitleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    WCHAR title[256];
    int len = ::GetWindowTextW(hwnd, title, IM_ARRAYSIZE(title));
    if (len < 0)
        len = 0;
    chrome->DWriteFactory->CreateTextLayout(title, (UINT32)len, chrome->TitleFormat, 8192.0f, (float)caption_height, &chrome->TitleLayout);
    chrome->TitleDpi = dpi;
}

// Build the cached caption icon ONCE, high-res (Win32X DwfEnsureIcon): the window's big icon rasterized
// at its native resolution into a premultiplied BGRA DIB, uploaded as a D3D11 texture, wrapped as a D2D
// bitmap -- DrawBitmap downscales with linear filtering (crisp at any DPI, unlike the 16px small icon).
static void ImGuiApp_ImplWin32D2DDXGI_ChromeEnsureIcon(ImGuiApp_ImplWin32D2DDXGI_Data* bd, ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome, HWND hwnd)
{
    if (chrome->IconBitmap != nullptr || chrome->IconTried || bd->D3DDevice == nullptr || bd->D2DDeviceContext == nullptr)
        return;
    chrome->IconTried = true;

    HICON icon = (HICON)::SendMessage(hwnd, WM_GETICON, ICON_BIG, 0);
    if (icon == nullptr) icon = (HICON)::SendMessage(hwnd, WM_GETICON, ICON_SMALL2, 0);
    if (icon == nullptr) icon = (HICON)::GetClassLongPtr(hwnd, GCLP_HICON);
    if (icon == nullptr) icon = (HICON)::GetClassLongPtr(hwnd, GCLP_HICONSM);
    if (icon == nullptr) icon = ::LoadIcon(nullptr, IDI_APPLICATION);
    if (icon == nullptr)
        return;

    // Native icon resolution (the highest available frame -> high-res source), clamped 16..256.
    int n = 32;
    ICONINFO ii = {};
    if (::GetIconInfo(icon, &ii))
    {
        BITMAP bm;
        if (ii.hbmColor != nullptr && ::GetObject(ii.hbmColor, sizeof(bm), &bm) != 0)
            n = bm.bmWidth;
        if (ii.hbmColor != nullptr) ::DeleteObject(ii.hbmColor);
        if (ii.hbmMask != nullptr)  ::DeleteObject(ii.hbmMask);
    }
    n = n < 16 ? 16 : n > 256 ? 256 : n;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = n;
    bi.bmiHeader.biHeight      = -n;   // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void*   bits      = nullptr;
    HDC     screen_dc = ::GetDC(nullptr);
    HDC     mem_dc    = ::CreateCompatibleDC(screen_dc);
    HBITMAP dib       = ::CreateDIBSection(screen_dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (screen_dc != nullptr)
        ::ReleaseDC(nullptr, screen_dc);
    if (mem_dc != nullptr && dib != nullptr && bits != nullptr)
    {
        HBITMAP old = (HBITMAP)::SelectObject(mem_dc, dib);
        ::DrawIconEx(mem_dc, 0, 0, icon, n, n, 0, nullptr, DI_NORMAL);
        ::GdiFlush();
        ::SelectObject(mem_dc, old);

        // DrawIconEx gives straight-alpha BGRA; premultiply for the premultiplied D2D bitmap.
        BYTE* p = (BYTE*)bits;
        for (int i = 0; i < n * n; i++, p += 4)
        {
            const UINT a = p[3];
            p[0] = (BYTE)(((UINT)p[0] * a) / 255u);
            p[1] = (BYTE)(((UINT)p[1] * a) / 255u);
            p[2] = (BYTE)(((UINT)p[2] * a) / 255u);
        }

        D3D11_TEXTURE2D_DESC td = {};
        td.Width            = (UINT)n;
        td.Height           = (UINT)n;
        td.MipLevels        = 1;
        td.ArraySize        = 1;
        td.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage            = D3D11_USAGE_DEFAULT;
        td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem     = bits;
        sd.SysMemPitch = (UINT)(n * 4);
        ID3D11Texture2D* tex = nullptr;
        bd->D3DDevice->CreateTexture2D(&td, &sd, &tex);
        IDXGISurface* surface = nullptr;
        if (tex != nullptr)
            tex->QueryInterface(__uuidof(IDXGISurface), (void**)&surface);
        if (surface != nullptr)
        {
            D2D1_BITMAP_PROPERTIES1 bp = {};
            bp.pixelFormat.format    = DXGI_FORMAT_B8G8R8A8_UNORM;
            bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
            bp.dpiX = 96.0f;
            bp.dpiY = 96.0f;
            bp.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;
            bd->D2DDeviceContext->CreateBitmapFromDxgiSurface(surface, &bp, &chrome->IconBitmap);
        }
        if (surface != nullptr) surface->Release();
        if (tex != nullptr)     tex->Release();
    }
    if (dib != nullptr)    ::DeleteObject(dib);
    if (mem_dc != nullptr) ::DeleteDC(mem_dc);
}

// One caption button (Win32X DwfDrawButton): the highlight fill is alpha-faded by the button's 160ms
// opacity so hover/press cross-fade instead of snapping; the fill shade switches by press state, the
// alpha animates. glyph_col is the crossfaded normal-state glyph color; Close's glyph cross-fades to
// white as its red highlight rises.
static void ImGuiApp_ImplWin32D2DDXGI_ChromeDrawButton(ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome, ID2D1DeviceContext* ctx,
                                                       const RECT& cell, int button, int glyph, int pressed_button, bool dark, D2D1_COLOR_F glyph_col)
{
    const bool  pressed = pressed_button == button;
    const float hover   = chrome->BtnOpacity[button];
    COLORREF fill;
    if (button == ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Close)
    {
        fill      = pressed ? RGB(0xC8, 0x3C, 0x2F) : RGB(0xC4, 0x2B, 0x1C);
        glyph_col = ImGuiApp_ImplWin32D2DDXGI_ChromeLerp(glyph_col, ImGuiApp_ImplWin32D2DDXGI_ChromeColor(RGB(255, 255, 255)), hover);
    }
    else
    {
        fill = pressed ? (dark ? RGB(0x50, 0x50, 0x50) : RGB(0xCC, 0xCC, 0xCC))
                       : (dark ? RGB(0x3D, 0x3D, 0x3D) : RGB(0xE9, 0xE9, 0xE9));
    }
    if (hover > 0.001f)
    {
        D2D1_COLOR_F cf = ImGuiApp_ImplWin32D2DDXGI_ChromeColor(fill);
        cf.a = hover;   // fade the highlight in/out over the timeline
        chrome->AnimBrush->SetColor(&cf);
        const D2D1_RECT_F rcf = D2D1::RectF((FLOAT)cell.left, (FLOAT)cell.top, (FLOAT)cell.right, (FLOAT)cell.bottom);
        ctx->FillRectangle(&rcf, chrome->AnimBrush);
    }
    chrome->AnimBrush->SetColor(&glyph_col);
    if (chrome->GlyphLayouts[glyph] != nullptr)
        ctx->DrawTextLayout(D2D1::Point2F((FLOAT)cell.left, (FLOAT)cell.top), chrome->GlyphLayouts[glyph], chrome->AnimBrush, D2D1_DRAW_TEXT_OPTIONS_NONE);
}

// The caption chrome's draw pass, driven through the SetDeviceDrawCallback seam (the backend's D2D
// bracket already applied the canonical transform + antialias state). Cells and glyphs conform to
// Win32X dwmframex_v2 (DwfButtonRects + DwfDrawButton over cached layouts).
static void ImGuiApp_ImplWin32D2DDXGI_ChromeDrawCallback(ImGuiApp* app)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);
    ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome = bd != nullptr ? bd->Chrome : nullptr;
    if (chrome == nullptr || bd->D2DDeviceContext == nullptr)
        return;
    HWND hwnd = (HWND)bd->Hwnd;

    const UINT dpi            = ::GetDpiForWindow(hwnd);
    const int  button_width   = ::MulDiv(47, (int)dpi, 96);
    const int  caption_height = ::MulDiv(bd->CaptionHeight, (int)dpi, 96);
    RECT client = {};
    ::GetClientRect(hwnd, &client);
    if (!ImGuiApp_ImplWin32D2DDXGI_ChromeEnsureGlyphLayouts(chrome, dpi, caption_height, button_width))
        return;

    RECT close_button, max_button, min_button, lightdark_button;
    ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(client, caption_height, button_width, 0, &close_button);
    ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(client, caption_height, button_width, 1, &max_button);
    ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(client, caption_height, button_width, 2, &min_button);
    ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(client, caption_height, button_width, 3, &lightdark_button);

    ID2D1DeviceContext* ctx = bd->D2DDeviceContext;

    // Animation timeline + effective glyph color: during a crossfade, lerp between the (from) state
    // captured at transition start and the (to = current) state by AnimT -- one 160ms path serves the
    // theme (dark<->light) AND the activation (active<->inactive) transitions (Win32X DfwRenderEx).
    int pressed = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_None;
    switch (bd->NCPressedHit)
    {
    case HTCLOSE:                            pressed = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Close;     break;
    case HTMAXBUTTON:                        pressed = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Max;       break;
    case HTMINBUTTON:                        pressed = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Min;       break;
    case IMGUIAPP_WIN32D2DDXGI_HTLIGHTDARK:  pressed = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_LightDark; break;
    default:                                 break;
    }
    ImGuiApp_ImplWin32D2DDXGI_ChromeAdvance(chrome, pressed);
    const bool  dark    = chrome->DarkMode;
    const bool  active  = chrome->WndActive;
    const bool  dark_1  = chrome->Anim ? chrome->DarkFrom   : dark;
    const bool  activ_1 = chrome->Anim ? chrome->ActiveFrom : active;
    const float anim_t  = chrome->Anim ? chrome->AnimT      : 1.0f;
    const D2D1_COLOR_F glyph_col = ImGuiApp_ImplWin32D2DDXGI_ChromeLerp(ImGuiApp_ImplWin32D2DDXGI_ChromeGlyphColor(dark_1, activ_1),
                                                                        ImGuiApp_ImplWin32D2DDXGI_ChromeGlyphColor(dark, active), anim_t);

    // High-res caption system icon, placed as win32kfull!DrawCaptionIcon does: SM_CXSMICON x SM_CYSMICON
    // for the window dpi, centered in the caption-height square slot at the caption left.
    if (chrome->IconBitmap != nullptr)
    {
        const int iw = ::GetSystemMetricsForDpi(SM_CXSMICON, dpi);
        const int ih = ::GetSystemMetricsForDpi(SM_CYSMICON, dpi);
        const float ix = (float)((caption_height - iw) / 2 + 1);
        const float iy = (float)((caption_height - ih) / 2);
        const D2D1_RECT_F ri = D2D1::RectF(ix, iy, ix + (float)iw, iy + (float)ih);
        ctx->DrawBitmap(chrome->IconBitmap, &ri, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
    }

    // Caption title (win32kfull xxxDrawCaptionTemp: the text starts one caption-height in, after the
    // icon slot), crossfaded like the glyphs and dimmed to 60% when inactive.
    ImGuiApp_ImplWin32D2DDXGI_ChromeEnsureTitle(chrome, hwnd, dpi, caption_height);
    if (chrome->TitleLayout != nullptr)
    {
        const D2D1_COLOR_F text_col = ImGuiApp_ImplWin32D2DDXGI_ChromeLerp(ImGuiApp_ImplWin32D2DDXGI_ChromeTextColor(dark_1, activ_1),
                                                                           ImGuiApp_ImplWin32D2DDXGI_ChromeTextColor(dark, active), anim_t);
        chrome->AnimBrush->SetColor(&text_col);
        ctx->DrawTextLayout(D2D1::Point2F((FLOAT)caption_height, 0.0f), chrome->TitleLayout, chrome->AnimBrush, D2D1_DRAW_TEXT_OPTIONS_NONE);
    }

    const int max_glyph = ::IsZoomed(hwnd) ? ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_Restore : ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_Maximize;
    const int ld_glyph  = chrome->DarkMode ? ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_Brightness : ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_QuietHours;

    ImGuiApp_ImplWin32D2DDXGI_ChromeDrawButton(chrome, ctx, lightdark_button, ImGuiApp_ImplWin32D2DDXGI_CaptionButton_LightDark, ld_glyph,  pressed, dark, glyph_col);
    ImGuiApp_ImplWin32D2DDXGI_ChromeDrawButton(chrome, ctx, min_button,       ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Min,       ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_Minimize, pressed, dark, glyph_col);
    ImGuiApp_ImplWin32D2DDXGI_ChromeDrawButton(chrome, ctx, max_button,       ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Max,       max_glyph, pressed, dark, glyph_col);
    ImGuiApp_ImplWin32D2DDXGI_ChromeDrawButton(chrome, ctx, close_button,     ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Close,     ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_Close,    pressed, dark, glyph_col);
}

// Win32X FindNCHit order: caption buttons FIRST (they beat the top resize strip -- v2's DwfHitTest),
// then the default ring/caption/client. Hot-tracks the button under the cursor for the draw callback.
// Registered through the SetNCHitTestCallback seam.
static int ImGuiApp_ImplWin32D2DDXGI_ChromeNCHitTest(ImGuiApp* app, int x, int y)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);
    ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome = bd != nullptr ? bd->Chrome : nullptr;
    if (chrome == nullptr)
        return -1;
    HWND hwnd = (HWND)bd->Hwnd;

    POINT pt = { (LONG)x, (LONG)y };
    ::ScreenToClient(hwnd, &pt);
    RECT client = {};
    ::GetClientRect(hwnd, &client);
    const int button_width   = ::MulDiv(47, (int)::GetDpiForWindow(hwnd), 96);
    const int caption_height = ::MulDiv(bd->CaptionHeight, (int)::GetDpiForWindow(hwnd), 96);

    RECT close_button, max_button, min_button, lightdark_button;
    ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(client, caption_height, button_width, 0, &close_button);
    ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(client, caption_height, button_width, 1, &max_button);
    ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(client, caption_height, button_width, 2, &min_button);
    ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(client, caption_height, button_width, 3, &lightdark_button);

    if (::PtInRect(&lightdark_button, pt)) { chrome->HotButton = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_LightDark; return IMGUIAPP_WIN32D2DDXGI_HTLIGHTDARK; }
    if (::PtInRect(&min_button, pt))       { chrome->HotButton = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Min;       return HTMINBUTTON; }
    if (::PtInRect(&max_button, pt))       { chrome->HotButton = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Max;       return HTMAXBUTTON; }
    if (::PtInRect(&close_button, pt))     { chrome->HotButton = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Close;     return HTCLOSE; }

    // System-menu icon slot (win32kfull DrawCaptionIcon: a caption-height square at the caption left).
    // Checked before the resize ring like the buttons (Win32X DwfHitTest order); DefWindowProc answers
    // HTSYSMENU with the system menu (click) and SC_CLOSE (double-click).
    const RECT icon_slot = { 0, 0, caption_height, caption_height };
    if (::PtInRect(&icon_slot, pt))        { chrome->HotButton = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_None;      return HTSYSMENU; }

    const LRESULT hit = ImGuiApp_ImplWin32D2DDXGI_DefaultNCHitTest(bd, hwnd, x, y);
    chrome->HotButton = hit == HTCAPTION ? ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Caption : ImGuiApp_ImplWin32D2DDXGI_CaptionButton_None;
    return (int)hit;
}

// The caption light/dark button's WM_NCLBUTTONUP action (reference OnNCLButtonUp CB_LIGHTDARKBUTTON branch).
static void ImGuiApp_ImplWin32D2DDXGI_ChromeToggleLightDark(ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome, HWND hwnd)
{
    const ULONGLONG now = ::GetTickCount64();
    if (now - chrome->LastLightDarkChangeMs <= 150)
        return;
    // Crossfade origin = the currently shown theme; the flips below set the target state.
    ImGuiApp_ImplWin32D2DDXGI_ChromeBeginTransition(chrome, !chrome->DarkMode, chrome->WndActive);
    if (chrome->LightMode)
    {
        chrome->DarkMode  = true;
        chrome->LightMode = false;
        ImGuiApp_ImplWin32D2DDXGI_EnableDarkMode(hwnd);
    }
    else
    {
        chrome->DarkMode  = false;
        chrome->LightMode = true;
        ImGuiApp_ImplWin32D2DDXGI_EnableLightMode(hwnd);
    }
    chrome->LastLightDarkChangeMs = now;
}

bool ImGuiApp_ImplWin32D2DDXGI_InstallChrome(ImGuiApp* app)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);
    IM_ASSERT(bd != nullptr && "App or backend not initialized! Did you call ImGuiApp_ImplWin32D2DDXGI_Init()?");
    if (bd == nullptr || bd->D2DDeviceContext == nullptr || bd->D2DFactory == nullptr)
        return false;
    if (bd->Chrome != nullptr)
        return true;

    ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome = IM_NEW(ImGuiApp_ImplWin32D2DDXGI_ChromeData)();
    chrome->DarkMode   = true;   // the reference's OnCreate default
    chrome->WndActive  = true;   // foreground on first show; WM_ACTIVATE corrects it
    chrome->LastTickMs = ::GetTickCount64();

    // Brushes (colors verbatim from OnInitD2D).
    ID2D1DeviceContext* ctx = bd->D2DDeviceContext;
    const COLORREF inactive_ref     = ::GetSysColor(COLOR_BTNSHADOW);
    const COLORREF colorization_ref = ::GetSysColor(COLOR_MENUHILIGHT);
    ctx->CreateSolidColorBrush(D2D1::ColorF(1.00f, 1.00f, 1.00f, 0.95f), &chrome->Brush);
    ctx->CreateSolidColorBrush(D2D1::ColorF(196.0f / 255.0f, 43.0f / 255.0f, 28.0f / 255.0f, 1.0f), &chrome->RedBrush);
    ctx->CreateSolidColorBrush(D2D1::ColorF(43.0f / 255.0f, 43.0f / 255.0f, 43.0f / 255.0f, 0.85f), &chrome->BlackBrush);
    ctx->CreateSolidColorBrush(D2D1::ColorF(GetRValue(inactive_ref) / 255.0f, GetGValue(inactive_ref) / 255.0f, GetBValue(inactive_ref) / 255.0f, 1.0f), &chrome->InactiveBrush);
    ctx->CreateSolidColorBrush(D2D1::ColorF(GetRValue(colorization_ref) / 255.0f, GetGValue(colorization_ref) / 255.0f, GetBValue(colorization_ref) / 255.0f, 1.0f), &chrome->ColorizationBrush);
    ctx->CreateSolidColorBrush(D2D1::ColorF(GetRValue(colorization_ref) / 255.0f, GetGValue(colorization_ref) / 255.0f, GetBValue(colorization_ref) / 255.0f, 0.1f), &chrome->FadedColorizationBrush);
    ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &chrome->AnimBrush);   // recolored per fill/glyph (Win32X DwfBrush)

    const D2D1_STROKE_STYLE_PROPERTIES1 stroke_props =
    {
        D2D1_CAP_STYLE_SQUARE, D2D1_CAP_STYLE_SQUARE, D2D1_CAP_STYLE_SQUARE,
        D2D1_LINE_JOIN_ROUND, 0.0f, D2D1_DASH_STYLE_SOLID, 0.0f, D2D1_STROKE_TRANSFORM_TYPE_HAIRLINE,
    };
    bd->D2DFactory->CreateStrokeStyle(stroke_props, nullptr, 0, &chrome->StrokeStyle);

    // DWrite factory only; the icon format + cached glyph layouts build lazily at the first draw
    // (and rebuild on dpi change) in ChromeEnsureGlyphLayouts.
    const bool fonts_ok = SUCCEEDED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&chrome->DWriteFactory));

    if (!fonts_ok || chrome->Brush == nullptr || chrome->AnimBrush == nullptr || chrome->StrokeStyle == nullptr)
    {
        bd->Chrome = chrome;   // let Uninstall release whatever was created
        ImGuiApp_ImplWin32D2DDXGI_UninstallChrome(app);
        return false;
    }

    bd->Chrome = chrome;
    ImGuiApp_ImplWin32D2DDXGI_ChromeEnsureIcon(bd, chrome, (HWND)bd->Hwnd);   // built once at install (reference parity)
    // Self-hosted on the public seams: any client callback installed later displaces the chrome's.
    ImGuiApp_ImplWin32D2DDXGI_SetDeviceDrawCallback(app, ImGuiApp_ImplWin32D2DDXGI_ChromeDrawCallback);
    ImGuiApp_ImplWin32D2DDXGI_SetNCHitTestCallback(app, ImGuiApp_ImplWin32D2DDXGI_ChromeNCHitTest);
    return true;
}

void ImGuiApp_ImplWin32D2DDXGI_UninstallChrome(ImGuiApp* app)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);
    if (bd == nullptr || bd->Chrome == nullptr)
        return;
    ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome = bd->Chrome;
    if (bd->DrawCallback == ImGuiApp_ImplWin32D2DDXGI_ChromeDrawCallback)
        ImGuiApp_ImplWin32D2DDXGI_SetDeviceDrawCallback(app, nullptr);
    if (bd->NCHitTestCallback == ImGuiApp_ImplWin32D2DDXGI_ChromeNCHitTest)
        ImGuiApp_ImplWin32D2DDXGI_SetNCHitTestCallback(app, nullptr);

    for (int i = 0; i < ImGuiApp_ImplWin32D2DDXGI_ChromeGlyph_COUNT; i++)
        if (chrome->GlyphLayouts[i] != nullptr)    { chrome->GlyphLayouts[i]->Release(); }
    if (chrome->TitleLayout != nullptr)            { chrome->TitleLayout->Release(); }
    if (chrome->TitleFormat != nullptr)            { chrome->TitleFormat->Release(); }
    if (chrome->IconBitmap != nullptr)             { chrome->IconBitmap->Release(); }
    if (chrome->AnimBrush != nullptr)              { chrome->AnimBrush->Release(); }
    if (chrome->IconFormat != nullptr)             { chrome->IconFormat->Release(); }
    if (chrome->DWriteFactory != nullptr)          { chrome->DWriteFactory->Release(); }
    if (chrome->StrokeStyle != nullptr)            { chrome->StrokeStyle->Release(); }
    if (chrome->FadedColorizationBrush != nullptr) { chrome->FadedColorizationBrush->Release(); }
    if (chrome->ColorizationBrush != nullptr)      { chrome->ColorizationBrush->Release(); }
    if (chrome->InactiveBrush != nullptr)          { chrome->InactiveBrush->Release(); }
    if (chrome->BlackBrush != nullptr)             { chrome->BlackBrush->Release(); }
    if (chrome->RedBrush != nullptr)               { chrome->RedBrush->Release(); }
    if (chrome->Brush != nullptr)                  { chrome->Brush->Release(); }
    IM_DELETE(chrome);
    bd->Chrome = nullptr;
}

bool ImGuiApp_ImplWin32D2DDXGI_GetChromeLightMode(ImGuiApp* app)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);
    return bd != nullptr && bd->Chrome != nullptr && bd->Chrome->LightMode;
}

float ImGuiApp_ImplWin32D2DDXGI_GetChromeThemeBlend(ImGuiApp* app)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);
    ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome = bd != nullptr ? bd->Chrome : nullptr;
    if (chrome == nullptr)
        return 0.0f;
    // An activation-only crossfade keeps DarkFrom == DarkMode, so the blend holds steady through it.
    const float to = chrome->DarkMode ? 0.0f : 1.0f;
    if (!chrome->Anim)
        return to;
    const float from = chrome->DarkFrom ? 0.0f : 1.0f;
    return from + (to - from) * chrome->AnimT;
}

static LRESULT WINAPI ImGuiApp_ImplWin32D2DDXGI_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ImGuiApp* app = (ImGuiApp*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);

    // A capture-tracked caption-button press owns the mouse stream BEFORE the imgui handler sees it:
    // imgui never saw the NC button down, so its WM_LBUTTONUP path would ReleaseCapture (zero buttons
    // tracked) and the synchronous WM_CAPTURECHANGED would cancel the press before the commit ran.
    if (bd != nullptr && bd->NCPressedHit != 0)
    {
        switch (msg)
        {
        case WM_MOUSEMOVE:
        {
            // Re-test the hit so the chrome hot-tracks the button under the cursor.
            POINT pt = { (LONG)(short)LOWORD(lParam), (LONG)(short)HIWORD(lParam) };
            ::ClientToScreen(hWnd, &pt);
            ImGuiApp_ImplWin32D2DDXGI_NCHitTestAt(app, bd, hWnd, pt.x, pt.y);
            return 0;
        }
        case WM_LBUTTONUP:
        {
            const int pressed = bd->NCPressedHit;
            bd->NCPressedHit  = 0;
            ::ReleaseCapture();
            POINT pt = { (LONG)(short)LOWORD(lParam), (LONG)(short)HIWORD(lParam) };
            ::ClientToScreen(hWnd, &pt);
            if (pressed == ImGuiApp_ImplWin32D2DDXGI_NCHitTestAt(app, bd, hWnd, pt.x, pt.y))
            {
                switch (pressed)
                {
                case HTCLOSE:     ::PostMessage(hWnd, WM_SYSCOMMAND, SC_CLOSE, 0); break;
                case HTMINBUTTON: ::PostMessage(hWnd, WM_SYSCOMMAND, SC_MINIMIZE, 0); break;
                case HTMAXBUTTON: ::PostMessage(hWnd, WM_SYSCOMMAND, ::IsZoomed(hWnd) ? SC_RESTORE : SC_MAXIMIZE, 0); break;
                case IMGUIAPP_WIN32D2DDXGI_HTLIGHTDARK:
                    if (bd->Chrome != nullptr)
                        ImGuiApp_ImplWin32D2DDXGI_ChromeToggleLightDark(bd->Chrome, hWnd);
                    break;
                default: break;
                }
            }
            return 0;
        }
        case WM_CAPTURECHANGED:   // something genuinely stole the capture: cancel the press
            bd->NCPressedHit = 0;
            return 0;
        default:
            break;
        }
    }

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_NCCREATE:
    {
        ::EnableNonClientDpiScaling(hWnd);
        BOOL allow_ncpaint = FALSE;
        ::DwmSetWindowAttribute(hWnd, DWMWA_ALLOW_NCPAINT, &allow_ncpaint, sizeof(allow_ncpaint));
        break;
    }
    case WM_CREATE:
        // The reference's SyncFrameChange: force a WM_NCCALCSIZE pass so the borderless client rect applies.
        ::SetWindowPos(hWnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
        break;
    case WM_ACTIVATE:
        if (bd != nullptr && bd->Chrome != nullptr)   // activation crossfade (Win32X: BeginTransition in response to WM_ACTIVATE)
            ImGuiApp_ImplWin32D2DDXGI_ChromeBeginTransition(bd->Chrome, bd->Chrome->DarkMode, LOWORD(wParam) != WA_INACTIVE);
        if (bd != nullptr && HIWORD(wParam) == 0 && !bd->InFrame)   // not minimized; viewport churn mid-frame also lands here
            ImGuiApp_ImplWin32D2DDXGI_ApplyWindowDressing(bd, hWnd);
        break;
    case WM_NCCALCSIZE:
        if (wParam)
        {
            NCCALCSIZE_PARAMS* params = (NCCALCSIZE_PARAMS*)lParam;
            params->rgrc[1] = params->rgrc[2];   // lie to dwm (reference)
            if (::IsZoomed(hWnd))
            {
                MONITORINFO mi = {};
                mi.cbSize = sizeof(mi);
                if (::GetMonitorInfo(::MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &mi))
                    params->rgrc[0] = mi.rcWork;
                return 0;
            }
            // The load-bearing smooth-resize trick: repaint + restart-present from INSIDE the resize
            // calculation -- BEFORE the window manager commits the new frame -- then wait until the
            // composition engine has consumed the update (Commit + WaitForCommitCompletion: batches and
            // presents are latched atomically at the compositor's next frame start). Content is therefore
            // strictly ordered before geometry; the geometry commit can never pair with a stale present.
            if (bd != nullptr && bd->Swapchain != nullptr && !bd->Moving)
            {
                GUITHREADINFO gti = {};
                gti.cbSize = sizeof(gti);
                ::GetGUIThreadInfo(::GetCurrentThreadId(), &gti);
                // Render at most once per compositor frame (a previous tick's post-commit repaint may
                // already have latched this frame's content); the ordering wait below runs regardless.
                if (!ImGuiApp_ImplWin32D2DDXGI_ModalContentCurrent(bd, hWnd))
                    ImGuiApp_ImplWin32D2DDXGI_ModalRepaint(app, bd, hWnd, true, false, true);
                if (gti.hwndMoveSize != nullptr && bd->DCompDevice != nullptr)
                {
                    bd->DCompDevice->Commit();
                    bd->DCompDevice->WaitForCommitCompletion();
                }
            }
            SIZE border;
            ImGuiApp_ImplWin32D2DDXGI_GetWindowBorders(hWnd, &border);
            params->rgrc[0].left   += border.cx;
            params->rgrc[0].right  -= border.cx;
            params->rgrc[0].bottom -= border.cy;
            return 0;
        }
        return 0;
    case WM_NCHITTEST:
    {
        const int x = (int)(short)LOWORD(lParam);
        const int y = (int)(short)HIWORD(lParam);
        if (bd != nullptr && bd->NCHitTestCallback != nullptr)
        {
            const int hit = bd->NCHitTestCallback(app, x, y);
            if (hit != -1)
                return hit;
        }
        return ImGuiApp_ImplWin32D2DDXGI_DefaultNCHitTest(bd, hWnd, x, y);
    }
    case WM_NCLBUTTONDOWN:
        // Button codes come from the chrome's hittest (or a client callback); the default hittest never reports them.
        if (ImGuiApp_ImplWin32D2DDXGI_NCButtonPress(bd, hWnd, wParam))
            return 0;
        break;
    case WM_NCLBUTTONDBLCLK:
        // NC double-clicks arrive without CS_DBLCLKS: a fast second click on a button is still a press.
        if (ImGuiApp_ImplWin32D2DDXGI_NCButtonPress(bd, hWnd, wParam))
            return 0;
        if (wParam == HTCAPTION)
        {
            if (::IsZoomed(hWnd))
                ::PostMessage(hWnd, WM_SYSCOMMAND, SC_RESTORE, 0);
            else
                ::PostMessage(hWnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
            return 0;
        }
        break;
    case WM_NCRBUTTONUP:
        if (wParam == HTCAPTION)
        {
            HMENU menu = ::GetSystemMenu(hWnd, FALSE);
            POINT pt = {};
            ::GetCursorPos(&pt);
            ::SetMenuDefaultItem(menu, (UINT)-1, TRUE);
            const BOOL cmd = ::TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, pt.x, pt.y, hWnd, nullptr);
            if (cmd)
                ::PostMessage(hWnd, WM_SYSCOMMAND, (WPARAM)cmd, 0);
            return 0;
        }
        break;
    case WM_MOVING:
        if (bd != nullptr)
            bd->Moving = true;
        break;
    case WM_SIZING:
        if (bd != nullptr)
            bd->Resizing = true;
        break;
    case WM_ENTERSIZEMOVE:
    case WM_ENTERMENULOOP:
        if (bd != nullptr)
        {
            bd->Moving        = false;
            bd->Resizing      = false;
            bd->PaceModalLive = true;   // vblank pace thread starts posting refresh-rate ticks
            if (bd->PaceWake != nullptr)
                ::SetEvent(bd->PaceWake);   // unpark
        }
        ::SetTimer(hWnd, 0x69, USER_TIMER_MINIMUM, nullptr);   // fallback cadence (pace thread may lack a vblank source)
        return 0;
    case WM_EXITSIZEMOVE:
    case WM_EXITMENULOOP:
        if (bd != nullptr)
        {
            bd->PaceModalLive = false;
            if (bd->PaceWake != nullptr)
                ::SetEvent(bd->PaceWake);   // return the thread to its parked wait
        }
        ::KillTimer(hWnd, 0x69);
        ImGuiApp_ImplWin32D2DDXGI_ModalRepaint(app, bd, hWnd, false, true, true);
        return 0;
    case IMGUIAPP_WIN32D2DDXGI_WM_PACE:
        // Refresh-rate modal tick from the pace thread. The thread already supplied the vblank phase, so
        // the repaint runs waitless; compositor-frame coalescing dedupes against the resize-tick repaints.
        if (bd != nullptr)
        {
            ::InterlockedExchange(&bd->PacePending, 0);
            if (bd->PaceModalLive && !ImGuiApp_ImplWin32D2DDXGI_ModalContentCurrent(bd, hWnd))
                ImGuiApp_ImplWin32D2DDXGI_ModalRepaint(app, bd, hWnd, false, true, false);
        }
        return 0;
    case WM_TIMER:
        if (wParam == 0x69)
        {
            // One rendered present per compositor frame: skip when this frame's content is already queued
            // at the current size (the resize tick's repaints own the cadence during a live resize).
            if (bd != nullptr && !ImGuiApp_ImplWin32D2DDXGI_ModalContentCurrent(bd, hWnd))
            {
                ImGuiApp_ImplWin32D2DDXGI_ModalRepaint(app, bd, hWnd, false, true, true);
                if (!bd->Moving)
                    ::DwmFlush();
            }
            return 0;
        }
        break;
    case WM_WINDOWPOSCHANGED:
        // Not forwarded (no WM_SIZE/WM_MOVE generation), matching the reference. NO vblank wait here:
        // the geometry already committed. Order of operations is the flicker guarantee:
        // 1. Self-heal: if this change RESIZED the window, the presented content's origin may have moved
        //    out from under it (left/top edges) -- pin the content at its render-time screen position
        //    (microseconds, no rendering). A pure move needs no pin: content correctly travels along.
        // 2. Repaint at the new size, coalesced to one rendered present per compositor frame; its present
        //    unpins the content in the same compositor latch.
        if (bd != nullptr && bd->Swapchain != nullptr && !bd->InModalRepaint)
        {
            RECT client_rect = {};
            ::GetClientRect(hWnd, &client_rect);
            const bool size_changed = bd->ContentOriginValid &&
                ((client_rect.right - client_rect.left) != bd->ContentWidth ||
                 (client_rect.bottom - client_rect.top) != bd->ContentHeight);
            if (size_changed)
                ImGuiApp_ImplWin32D2DDXGI_PinContent(bd, hWnd);
            if (!ImGuiApp_ImplWin32D2DDXGI_ModalContentCurrent(bd, hWnd))
                ImGuiApp_ImplWin32D2DDXGI_ModalRepaint(app, bd, hWnd, true, true, false);
        }
        return 0;
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        SIZE border;
        ImGuiApp_ImplWin32D2DDXGI_GetWindowBorders(hWnd, &border);
        const UINT dpi          = ::GetDpiForWindow(hWnd);
        const int  button_width = ::MulDiv(47, (int)dpi, 96);
        const int  caption      = ::MulDiv(bd != nullptr ? bd->CaptionHeight : 30, (int)dpi, 96);
        mmi->ptMinTrackSize.x = 5 * button_width + 2 * border.cy;
        mmi->ptMinTrackSize.y = caption + border.cy;
        GUITHREADINFO gti = {};
        gti.cbSize = sizeof(gti);
        if (::GetGUIThreadInfo(::GetCurrentThreadId(), &gti) && gti.hwndMoveSize == hWnd)
            return 0;
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (::GetMonitorInfo(::MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &mi))
        {
            mmi->ptMaxPosition.x  = labs(mi.rcMonitor.left - mi.rcWork.left);
            mmi->ptMaxPosition.y  = labs(mi.rcMonitor.top - mi.rcWork.top) + 1;
            mmi->ptMaxTrackSize.x = mi.rcWork.right - mi.rcWork.left;
            mmi->ptMaxTrackSize.y = (mi.rcWork.bottom - mi.rcWork.top) - 1;
            mmi->ptMaxSize        = mmi->ptMaxTrackSize;
        }
        return 0;
    }
    case WM_PAINT:
        // WS_EX_NOREDIRECTIONBITMAP: we *should* never receive this (reference comment preserved in spirit).
        ::ValidateRect(hWnd, nullptr);
        return 0;
    case WM_ERASEBKGND:
    {
        RECT update = {};
        ::GetUpdateRect(hWnd, &update, FALSE);
        ::ValidateRect(hWnd, &update);
        return TRUE;
    }
    case WM_NCPAINT:
        ::ValidateRgn(hWnd, (HRGN)wParam);
        break;
    case WM_NCACTIVATE:
        // Track REAL activation, propagated with lParam -1 (update the window's activation state, do
        // NOT repaint the standard NC -- we own the caption) so DWM tracks activation and renders the
        // correct frame. Always claiming active froze DWM's frame until the next real foreground
        // change (Win32X: the "click the desktop to fix it" symptom).
        if (bd != nullptr && bd->Chrome != nullptr)
            ImGuiApp_ImplWin32D2DDXGI_ChromeBeginTransition(bd->Chrome, bd->Chrome->DarkMode, wParam != FALSE);
        return ::DefWindowProc(hWnd, WM_NCACTIVATE, wParam, (LPARAM)-1);
    case WM_SETTEXT:
    {
        // Let DefWindowProc store the new title, THEN drop the cached caption-title layout (rebuilt
        // off the hot path at the next chrome draw).
        const LRESULT result = ::DefWindowProc(hWnd, WM_SETTEXT, wParam, lParam);
        if (bd != nullptr && bd->Chrome != nullptr && bd->Chrome->TitleLayout != nullptr)
        {
            bd->Chrome->TitleLayout->Release();
            bd->Chrome->TitleLayout = nullptr;
        }
        return result;
    }
    case WM_DPICHANGED:
    {
        // Snap to the system's suggested rect; every dpi-scaled metric re-derives from the window's
        // new dpi on the next frame (the chrome's cached layouts rebuild on their dpi key).
        const RECT* suggested = (const RECT*)lParam;
        if (suggested != nullptr)
            ::SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                           suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DWMNCRENDERINGCHANGED:
        if (wParam)
        {
            const BOOL allow_ncpaint  = FALSE;
            const BOOL passive_update = TRUE;
            ::DwmSetWindowAttribute(hWnd, DWMWA_ALLOW_NCPAINT, &allow_ncpaint, sizeof(allow_ncpaint));
            ::DwmSetWindowAttribute(hWnd, DWMWA_PASSIVE_UPDATE_MODE, &passive_update, sizeof(passive_update));
        }
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)   // disable ALT application menu
            return 0;
        if ((wParam & 0xfff0) == SC_MOVE)
            ::PostMessage(hWnd, WM_MOUSEMOVE, 0, 0);
        break;
    case WM_MOUSEMOVE:
        return 0;   // reference swallows it (the imgui handler above already consumed the position)
    case WM_NCMOUSELEAVE:
    case WM_MOUSELEAVE:
        if (bd != nullptr && bd->Chrome != nullptr)
            bd->Chrome->HotButton = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_None;
        break;
    case WM_KEYUP:
        // The reference's ESC behavior (EndTask softened to WM_CLOSE): quit when foreground.
        if (wParam == VK_ESCAPE && ::GetForegroundWindow() == hWnd)
        {
            ::PostMessage(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_CLOSE:
        ::DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

//--------------------------------------------------------------------------------------------------------
// MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
// This is an _advanced_ and _optional_ feature, allowing the backend to create and handle multiple viewports simultaneously.
// If you are new to dear imgui or creating a new binding for dear imgui, it is recommended that you completely ignore this section first..
// Per-viewport hooks installed into ImGuiPlatformIO; they run as context-free callbacks and reach
// backend state through the GetBackendData accessor.
//--------------------------------------------------------------------------------------------------------

// Helper structure we store in the void* RendererUserData field of each ImGuiViewport to easily retrieve our backend data.
// Secondary viewports replicate the main window's no-flicker design at small scale (docs/dxgi-noflicker.md):
// an over-allocated grow-only swapchain (the window clips the unused region, so most resizes need no
// ResizeBuffers at all), geometry deferred + flushed as one transaction right before render, and a
// per-viewport content pin on the viewport's own DComp visual.
struct ImGuiApp_ImplWin32D2DDXGI_ViewportData
{
    IDXGISwapChain1*        Swapchain;
    ID3D11RenderTargetView* RTView;
    IDCompositionTarget*    DCompTarget;
    IDCompositionVisual*    DCompVisual;
    int                     CapacityWidth;      // allocated buffer size (grow-only, 256px granularity)
    int                     CapacityHeight;
    bool                    PendingGrow;        // capacity grew: ResizeBuffers right before the next render
    bool                    HasPendingPos;      // deferred Platform_SetWindowPos/SetWindowSize, flushed in
    bool                    HasPendingSize;     // Platform_RenderWindow as one back-to-back transaction
    ImVec2                  PendingPos;
    ImVec2                  PendingSize;
    int                     ContentOriginX;     // window-origin screen position of the last presented frame
    int                     ContentOriginY;
    int                     ContentWidth;       // client size of the last presented frame
    int                     ContentHeight;
    bool                    ContentOriginValid;
    bool                    VisualOffsetActive; // a compensating offset is currently committed on DCompVisual
    bool                    PresentedOnce;      // dirty-rect rule: full-frame present required after (re)creation

    ImGuiApp_ImplWin32D2DDXGI_ViewportData()  { memset((void*)this, 0, sizeof(*this)); }
    ~ImGuiApp_ImplWin32D2DDXGI_ViewportData() { IM_ASSERT(Swapchain == nullptr && RTView == nullptr && DCompTarget == nullptr && DCompVisual == nullptr); }
};

// Grow-only capacity granularity: over-allocate so interactive resizes stay within capacity (no
// ResizeBuffers, no content loss); the window clips the unused buffer region, exactly like the main
// swapchain's desktop-sized buffer.
static inline int ImGuiApp_ImplWin32D2DDXGI_RoundUpCapacity(int v)
{
    return (v + 255) & ~255;
}

static void ImGuiApp_ImplWin32D2DDXGI_Renderer_CreateWindow(ImGuiViewport* viewport)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(ImGuiApp_ImplWin32D2DDXGI_GetApp());
    IM_ASSERT(bd != nullptr && viewport->RendererUserData == nullptr);
    if (bd == nullptr)
        return;

    HWND hwnd = viewport->PlatformHandleRaw ? (HWND)viewport->PlatformHandleRaw : (HWND)viewport->PlatformHandle;
    IM_ASSERT(hwnd != nullptr);

    ImGuiApp_ImplWin32D2DDXGI_ViewportData* vd = IM_NEW(ImGuiApp_ImplWin32D2DDXGI_ViewportData)();
    viewport->RendererUserData = vd;

    // Secondary viewports get their own composition swapchain, over-allocated + grow-only (the window
    // clips the unused region); no tearing/waitable flags -- they present through the compositor.
    vd->CapacityWidth  = ImGuiApp_ImplWin32D2DDXGI_RoundUpCapacity((int)viewport->Size.x);
    vd->CapacityHeight = ImGuiApp_ImplWin32D2DDXGI_RoundUpCapacity((int)viewport->Size.y);
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SwapEffect         = bd->PresentDirtyRects ? DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL : DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode          = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Scaling            = DXGI_SCALING_STRETCH;
    desc.SampleDesc.Count   = 1;
    desc.Width              = (UINT)vd->CapacityWidth;
    desc.Height             = (UINT)vd->CapacityHeight;
    desc.BufferCount        = 3;

    HRESULT hr = bd->DXGIFactory->CreateSwapChainForComposition(bd->DXGIDevice, &desc, nullptr, &vd->Swapchain);
    IM_ASSERT(SUCCEEDED(hr));
    if (FAILED(hr))
        return;
    bd->DXGIFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_PRINT_SCREEN);

    ID3D11Texture2D* back_buffer = nullptr;
    vd->Swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (back_buffer != nullptr)
    {
        bd->D3DDevice->CreateRenderTargetView(back_buffer, nullptr, &vd->RTView);
        back_buffer->Release();
    }

    if (SUCCEEDED(bd->DCompDevice->CreateTargetForHwnd(hwnd, TRUE, &vd->DCompTarget)) &&
        SUCCEEDED(bd->DCompDevice->CreateVisual(&vd->DCompVisual)))
    {
        vd->DCompVisual->SetContent(vd->Swapchain);
        vd->DCompTarget->SetRoot(vd->DCompVisual);
        bd->DCompDevice->Commit();
    }
}

static void ImGuiApp_ImplWin32D2DDXGI_Renderer_DestroyWindow(ImGuiViewport* viewport)
{
    // The main viewport (owned by the application) will always have RendererUserData == nullptr since we didn't create the data for it.
    if (ImGuiApp_ImplWin32D2DDXGI_ViewportData* vd = (ImGuiApp_ImplWin32D2DDXGI_ViewportData*)viewport->RendererUserData)
    {
        if (vd->DCompVisual != nullptr) { vd->DCompVisual->Release(); vd->DCompVisual = nullptr; }
        if (vd->DCompTarget != nullptr) { vd->DCompTarget->Release(); vd->DCompTarget = nullptr; }
        if (vd->RTView != nullptr)      { vd->RTView->Release(); vd->RTView = nullptr; }
        if (vd->Swapchain != nullptr)   { vd->Swapchain->Release(); vd->Swapchain = nullptr; }
        IM_DELETE(vd);
    }
    viewport->RendererUserData = nullptr;
}

// Grow-only: a size within capacity needs NO ResizeBuffers (the old presented content survives, and the
// window clips the unused region). Growth is deferred to right before the next render so the content-less
// window between buffer loss and the fresh present is as small as possible.
static void ImGuiApp_ImplWin32D2DDXGI_Renderer_SetWindowSize(ImGuiViewport* viewport, ImVec2 size)
{
    ImGuiApp_ImplWin32D2DDXGI_ViewportData* vd = (ImGuiApp_ImplWin32D2DDXGI_ViewportData*)viewport->RendererUserData;
    if (vd == nullptr || vd->Swapchain == nullptr)
        return;
    if ((int)size.x > vd->CapacityWidth || (int)size.y > vd->CapacityHeight)
    {
        vd->CapacityWidth  = ImGuiApp_ImplWin32D2DDXGI_RoundUpCapacity((int)size.x > vd->CapacityWidth ? (int)size.x : vd->CapacityWidth);
        vd->CapacityHeight = ImGuiApp_ImplWin32D2DDXGI_RoundUpCapacity((int)size.y > vd->CapacityHeight ? (int)size.y : vd->CapacityHeight);
        vd->PendingGrow    = true;
    }
}

// Deferred platform geometry: imgui applies a secondary viewport's move + resize as two separate OS
// transactions (SetWindowPos then SetWindowSize) early in UpdatePlatformWindows, a full render away from
// the matching present. Record both here; Platform_RenderWindow flushes them back-to-back and pins.
static void ImGuiApp_ImplWin32D2DDXGI_Platform_SetWindowPos(ImGuiViewport* viewport, ImVec2 pos)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(ImGuiApp_ImplWin32D2DDXGI_GetApp());
    ImGuiApp_ImplWin32D2DDXGI_ViewportData* vd = (ImGuiApp_ImplWin32D2DDXGI_ViewportData*)viewport->RendererUserData;
    if (bd == nullptr || vd == nullptr)
    {
        if (bd != nullptr && bd->UnderlyingPlatformSetWindowPos != nullptr)
            bd->UnderlyingPlatformSetWindowPos(viewport, pos);
        return;
    }
    vd->PendingPos    = pos;
    vd->HasPendingPos = true;
}

static void ImGuiApp_ImplWin32D2DDXGI_Platform_SetWindowSize(ImGuiViewport* viewport, ImVec2 size)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(ImGuiApp_ImplWin32D2DDXGI_GetApp());
    ImGuiApp_ImplWin32D2DDXGI_ViewportData* vd = (ImGuiApp_ImplWin32D2DDXGI_ViewportData*)viewport->RendererUserData;
    if (bd == nullptr || vd == nullptr)
    {
        if (bd != nullptr && bd->UnderlyingPlatformSetWindowSize != nullptr)
            bd->UnderlyingPlatformSetWindowSize(viewport, size);
        return;
    }
    vd->PendingSize    = size;
    vd->HasPendingSize = true;
}

// First per-viewport hook RenderPlatformWindowsDefault runs. Three jobs, in order:
// 1. Flush deferred geometry as one back-to-back transaction (pos then size, upstream order).
// 2. Self-heal: if the geometry outran the presented content (a resize moved the origin), pin the
//    content at its render-time screen position on THIS viewport's visual (same mechanism as the main
//    window's R6, docs/dxgi-noflicker.md); the present in Renderer_SwapBuffers unpins it.
// 3. Make the pacing decision ONCE per viewport per frame, consumed by the render + present hooks below.
static void ImGuiApp_ImplWin32D2DDXGI_Platform_RenderWindow(ImGuiViewport* viewport, void*)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(ImGuiApp_ImplWin32D2DDXGI_GetApp());
    if (bd == nullptr)
        return;

    if (ImGuiApp_ImplWin32D2DDXGI_ViewportData* vd = (ImGuiApp_ImplWin32D2DDXGI_ViewportData*)viewport->RendererUserData)
    {
        if (vd->HasPendingPos && bd->UnderlyingPlatformSetWindowPos != nullptr)
            bd->UnderlyingPlatformSetWindowPos(viewport, vd->PendingPos);
        if (vd->HasPendingSize && bd->UnderlyingPlatformSetWindowSize != nullptr)
            bd->UnderlyingPlatformSetWindowSize(viewport, vd->PendingSize);
        const bool resized = vd->HasPendingSize;
        vd->HasPendingPos  = false;
        vd->HasPendingSize = false;

        HWND hwnd = viewport->PlatformHandleRaw ? (HWND)viewport->PlatformHandleRaw : (HWND)viewport->PlatformHandle;
        RECT window_rect = {};
        if (resized && vd->ContentOriginValid && vd->DCompVisual != nullptr && ::GetWindowRect(hwnd, &window_rect))
        {
            const int dx = vd->ContentOriginX - window_rect.left;
            const int dy = vd->ContentOriginY - window_rect.top;
            if (dx != 0 || dy != 0 || vd->VisualOffsetActive)
            {
                vd->DCompVisual->SetOffsetX((float)dx);
                vd->DCompVisual->SetOffsetY((float)dy);
                bd->DCompDevice->Commit();
                vd->VisualOffsetActive = (dx != 0 || dy != 0);
            }
        }
    }

    const bool present = ImGui::AppPacerViewportShouldPresent(bd->App, viewport);
    bd->VpSkip.SetBool(viewport->ID, !present);
}

static void ImGuiApp_ImplWin32D2DDXGI_Renderer_RenderWindow(ImGuiViewport* viewport, void*)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(ImGuiApp_ImplWin32D2DDXGI_GetApp());
    if (bd == nullptr || bd->VpSkip.GetBool(viewport->ID))
        return;
    ImGuiApp_ImplWin32D2DDXGI_ViewportData* vd = (ImGuiApp_ImplWin32D2DDXGI_ViewportData*)viewport->RendererUserData;
    if (vd == nullptr || vd->Swapchain == nullptr)
        return;

    // Deferred capacity growth: the only ResizeBuffers a secondary viewport ever runs, placed directly
    // before its render + present so the buffer is content-less for the smallest possible window.
    if (vd->PendingGrow)
    {
        vd->PendingGrow = false;
        if (vd->RTView != nullptr)
        {
            vd->RTView->Release();
            vd->RTView = nullptr;
        }
        vd->Swapchain->ResizeBuffers(0, (UINT)vd->CapacityWidth, (UINT)vd->CapacityHeight, DXGI_FORMAT_UNKNOWN, 0);
        vd->PresentedOnce = false;   // dirty-rect rule: full-frame present after buffer recreation
        ID3D11Texture2D* back_buffer = nullptr;
        vd->Swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
        if (back_buffer == nullptr)
        {
            IMGUIAPP_ERROR_PRINTF("imguiapp_impl_win32_d2ddxgi: viewport ResizeBuffers failed.\n");
            return;
        }
        bd->D3DDevice->CreateRenderTargetView(back_buffer, nullptr, &vd->RTView);
        back_buffer->Release();
    }
    if (vd->RTView == nullptr)
        return;

    bd->D3DDeviceContext->OMSetRenderTargets(1, &vd->RTView, nullptr);
    if ((viewport->Flags & ImGuiViewportFlags_NoRendererClear) == 0)
    {
        const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        bd->D3DDeviceContext->ClearRenderTargetView(vd->RTView, clear_color);
    }
    ImGui_ImplDX11_RenderDrawData(viewport->DrawData);
}

static void ImGuiApp_ImplWin32D2DDXGI_Renderer_SwapBuffers(ImGuiViewport* viewport, void*)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(ImGuiApp_ImplWin32D2DDXGI_GetApp());
    if (bd == nullptr || bd->VpSkip.GetBool(viewport->ID))
        return;
    ImGuiApp_ImplWin32D2DDXGI_ViewportData* vd = (ImGuiApp_ImplWin32D2DDXGI_ViewportData*)viewport->RendererUserData;
    if (vd == nullptr || vd->Swapchain == nullptr)
        return;
    HWND hwnd = viewport->PlatformHandleRaw ? (HWND)viewport->PlatformHandleRaw : (HWND)viewport->PlatformHandle;
    if (bd->PresentDirtyRects)
    {
        // Client-rect damage over the over-allocated buffer; first present after (re)creation is full-frame.
        RECT dirty = {};
        ::GetClientRect(hwnd, &dirty);
        DXGI_PRESENT_PARAMETERS params = {};
        if (vd->PresentedOnce && dirty.right > 0 && dirty.bottom > 0)
        {
            params.DirtyRectsCount = 1;
            params.pDirtyRects     = &dirty;
        }
        vd->Swapchain->Present1(0, 0, &params);
    }
    else
    {
        vd->Swapchain->Present(0, 0);
    }
    vd->PresentedOnce = true;

    // Unpin + re-anchor in the same compositor latch as the present (the main window's UnpinContent, per viewport).
    if (vd->VisualOffsetActive && vd->DCompVisual != nullptr)
    {
        vd->DCompVisual->SetOffsetX(0.0f);
        vd->DCompVisual->SetOffsetY(0.0f);
        bd->DCompDevice->Commit();
        vd->VisualOffsetActive = false;
    }
    RECT window_rect = {};
    RECT client_rect = {};
    if (::GetWindowRect(hwnd, &window_rect) && ::GetClientRect(hwnd, &client_rect))
    {
        vd->ContentOriginX     = window_rect.left;
        vd->ContentOriginY     = window_rect.top;
        vd->ContentWidth       = client_rect.right - client_rect.left;
        vd->ContentHeight      = client_rect.bottom - client_rect.top;
        vd->ContentOriginValid = true;
    }
}

// REPLACES the hooks imgui_impl_dx11 installed (its per-hwnd blt swapchains would fight the composition
// tree) and WRAPS imgui_impl_win32's SetWindowPos/SetWindowSize (deferred-geometry flush, see
// Platform_RenderWindow); teardown rides the wrapped backends' Shutdown (ClearPlatformHandlers/ClearRendererHandlers).
static void ImGuiApp_ImplWin32D2DDXGI_InitMultiViewportSupport(ImGuiApp_ImplWin32D2DDXGI_Data* bd)
{
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    IM_ASSERT(platform_io.Platform_RenderWindow == nullptr);
    IM_ASSERT(platform_io.Platform_SetWindowPos != nullptr && platform_io.Platform_SetWindowSize != nullptr);
    platform_io.Renderer_CreateWindow  = ImGuiApp_ImplWin32D2DDXGI_Renderer_CreateWindow;
    platform_io.Renderer_DestroyWindow = ImGuiApp_ImplWin32D2DDXGI_Renderer_DestroyWindow;
    platform_io.Renderer_SetWindowSize = ImGuiApp_ImplWin32D2DDXGI_Renderer_SetWindowSize;
    platform_io.Renderer_RenderWindow  = ImGuiApp_ImplWin32D2DDXGI_Renderer_RenderWindow;
    platform_io.Renderer_SwapBuffers   = ImGuiApp_ImplWin32D2DDXGI_Renderer_SwapBuffers;
    platform_io.Platform_RenderWindow  = ImGuiApp_ImplWin32D2DDXGI_Platform_RenderWindow;
    bd->UnderlyingPlatformSetWindowPos  = platform_io.Platform_SetWindowPos;
    bd->UnderlyingPlatformSetWindowSize = platform_io.Platform_SetWindowSize;
    platform_io.Platform_SetWindowPos  = ImGuiApp_ImplWin32D2DDXGI_Platform_SetWindowPos;
    platform_io.Platform_SetWindowSize = ImGuiApp_ImplWin32D2DDXGI_Platform_SetWindowSize;
}

//--------------------------------------------------------------------------------------------------------
// Client accessors
//--------------------------------------------------------------------------------------------------------

bool ImGuiApp_ImplWin32D2DDXGI_GetDeviceAndSwapchain(ImGuiApp* app, void** device, void** device_context, void** swapchain, void** d2d_device_context, void** dcomp_device)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);
    if (bd == nullptr)
        return false;
    if (device != nullptr)             *device             = bd->D3DDevice;
    if (device_context != nullptr)     *device_context     = bd->D3DDeviceContext;
    if (swapchain != nullptr)          *swapchain          = bd->Swapchain;
    if (d2d_device_context != nullptr) *d2d_device_context = bd->D2DDeviceContext;
    if (dcomp_device != nullptr)       *dcomp_device       = bd->DCompDevice;
    return true;
}

void ImGuiApp_ImplWin32D2DDXGI_SetDevicePreDrawCallback(ImGuiApp* app, ImGuiApp_ImplWin32D2DDXGI_DeviceDrawCallback callback)
{
    if (ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app))
        bd->PreDrawCallback = callback;
}

void ImGuiApp_ImplWin32D2DDXGI_SetDeviceDrawCallback(ImGuiApp* app, ImGuiApp_ImplWin32D2DDXGI_DeviceDrawCallback callback)
{
    if (ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app))
        bd->DrawCallback = callback;
}

void ImGuiApp_ImplWin32D2DDXGI_SetNCHitTestCallback(ImGuiApp* app, ImGuiApp_ImplWin32D2DDXGI_NCHitTestCallback callback)
{
    if (ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app))
        bd->NCHitTestCallback = callback;
}

int ImGuiApp_ImplWin32D2DDXGI_GetCaptionHeight(ImGuiApp* app)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);
    if (bd == nullptr || bd->Hwnd == nullptr)
        return 0;
    return ::MulDiv(bd->CaptionHeight, (int)::GetDpiForWindow((HWND)bd->Hwnd), 96);
}

//--------------------------------------------------------------------------------------------------------
// Pacer platform seam (QPC clock, hybrid sleep/spin wait, per-monitor refresh queries)
//--------------------------------------------------------------------------------------------------------

static double ImGuiApp_ImplWin32D2DDXGI_PacerNow()
{
    static LONGLONG qpc_hz = 0;
    if (qpc_hz == 0)
    {
        LARGE_INTEGER freq;
        ::QueryPerformanceFrequency(&freq);
        qpc_hz = freq.QuadPart;
    }
    LARGE_INTEGER now;
    ::QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)qpc_hz;
}

static void ImGuiApp_ImplWin32D2DDXGI_PacerWaitUntil(double time_sec, float sleep_slack_ms)
{
    for (;;)
    {
        const double remaining_ms = (time_sec - ImGuiApp_ImplWin32D2DDXGI_PacerNow()) * 1000.0;
        if (remaining_ms <= 0.0)
            return;
        if (remaining_ms > (double)sleep_slack_ms)
            ::Sleep(1);
        // else: spin the slack window (OS sleep granularity guard)
    }
}

static float ImGuiApp_ImplWin32D2DDXGI_PacerPrimaryRefreshHz()
{
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (::EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
        return (float)dm.dmDisplayFrequency;
    return 0.0f;   // pacer assumes 60
}

static float ImGuiApp_ImplWin32D2DDXGI_PacerViewportRefreshHz(const ImGuiViewport* viewport)
{
    if (viewport == nullptr || viewport->PlatformHandle == nullptr)
        return 0.0f;   // pacer falls back to primary
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (!::GetMonitorInfoW(::MonitorFromWindow((HWND)viewport->PlatformHandle, MONITOR_DEFAULTTONEAREST), &mi))
        return 0.0f;
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (::EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
        return (float)dm.dmDisplayFrequency;
    return 0.0f;
}

static const ImGuiAppPacerFuncs ImGuiApp_ImplWin32D2DDXGI_PacerFuncs =
{
    ImGuiApp_ImplWin32D2DDXGI_PacerNow,
    ImGuiApp_ImplWin32D2DDXGI_PacerWaitUntil,
    nullptr,   // no wait machinery to release
    ImGuiApp_ImplWin32D2DDXGI_PacerPrimaryRefreshHz,
    ImGuiApp_ImplWin32D2DDXGI_PacerViewportRefreshHz,
};

const ImGuiAppPacerFuncs* ImGuiApp_ImplWin32D2DDXGI_GetPacerFuncs() { return &ImGuiApp_ImplWin32D2DDXGI_PacerFuncs; }

//--------------------------------------------------------------------------------------------------------
// Platform lifecycle + run loop
//--------------------------------------------------------------------------------------------------------

bool ImGuiApp_ImplWin32D2DDXGI_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config)
{
    if (config.Headless != ImGuiAppHeadlessMode_None)
    {
        IMGUIAPP_ERROR_PRINTF("imguiapp_impl_win32_d2ddxgi: headless modes are not implemented for this backend (use win32-vulkan).\n");
        return false;
    }

    ImGuiApp_ImplWin32D2DDXGI_PlatformData* state = IM_NEW(ImGuiApp_ImplWin32D2DDXGI_PlatformData)();
    state->Hwnd             = nullptr;
    state->WindowClass      = {};
    state->OwnsImGuiContext = false;
    app->PlatformData = state;

    ImGui_ImplWin32_EnableDpiAwareness();
    const float main_scale    = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    const int   window_width  = (int)(config.WindowWidth  * main_scale);
    const int   window_height = (int)(config.WindowHeight * main_scale);
    config.DpiScale    = main_scale;
    config.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;

    // The reference's InitImmersiveControls: load the uxtheme ordinals + allow dark mode process-wide.
    ImGuiApp_ImplWin32D2DDXGI_LoadUxthemeOrdinals();
    if (ImGuiApp_ImplWin32D2DDXGI_SetPreferredAppMode != nullptr)
        ImGuiApp_ImplWin32D2DDXGI_SetPreferredAppMode(1); // PAM_ALLOWDARK

    // Canonical BCS_CENTERED: center the window on the cursor's monitor work area (computed pre-create).
    int window_x = 100;
    int window_y = 100;
    POINT cursor = {};
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (::GetCursorPos(&cursor) && ::GetMonitorInfo(::MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &mi))
    {
        window_x = mi.rcWork.left + (int)(0.5f * ((mi.rcWork.right - mi.rcWork.left) - window_width));
        window_y = mi.rcWork.top  + (int)(0.5f * ((mi.rcWork.bottom - mi.rcWork.top) - window_height));
    }

    HINSTANCE instance = ::GetModuleHandle(nullptr);
    state->WindowClass = { sizeof(state->WindowClass), CS_OWNDC | CS_BYTEALIGNWINDOW | CS_BYTEALIGNCLIENT, ImGuiApp_ImplWin32D2DDXGI_WndProc, 0L, 0L, instance, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, "ImGuiXImmersiveWindow", nullptr };
    ::RegisterClassExA(&state->WindowClass);
    // Canonical creation: WS_EX_NOREDIRECTIONBITMAP + BCS_WINDOW styles, visible at birth.
    state->Hwnd = ::CreateWindowExA(WS_EX_NOREDIRECTIONBITMAP, state->WindowClass.lpszClassName, config.WindowTitle,
                                    WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_BORDER | WS_SYSMENU | WS_VISIBLE,
                                    window_x, window_y, window_width, window_height, nullptr, nullptr, state->WindowClass.hInstance, nullptr);
    if (state->Hwnd == nullptr)
        return false;
    ::UpdateWindow(state->Hwnd);

    if (ImGui::GetCurrentContext() == nullptr)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        state->OwnsImGuiContext = true;
    }

    ImGuiApp_ImplWin32D2DDXGI_InitInfo init_info;   // zero-clear = canonical everything
    init_info.Hwnd = state->Hwnd;
    if (!ImGuiApp_ImplWin32D2DDXGI_Init(app, &init_info))
        return false;
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);
    bd->State = state;

    ImGui::GetIO().ConfigFlags |= config.ConfigFlags;

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        ImGuiApp_ImplWin32D2DDXGI_InitMultiViewportSupport(bd);

    app->PlatformName         = config.PlatformName;
    app->PlatformWindowHandle = state->Hwnd;
    ::SetWindowLongPtr(state->Hwnd, GWLP_USERDATA, (LONG_PTR)app);

    // Default pacer platform seam: with it installed, ImGuiAppPacerMode_Target + TargetHz <= 0 paces
    // the app to the primary monitor's refresh rate (a client-installed seam wins).
    if (app->Pacer.Funcs == nullptr)
        app->Pacer.Funcs = ImGuiApp_ImplWin32D2DDXGI_GetPacerFuncs();

    // The first WM_ACTIVATE fired before the backend existed; apply the window dressing now.
    ImGuiApp_ImplWin32D2DDXGI_ApplyWindowDressing(bd, state->Hwnd);
    return true;
}

void ImGuiApp_ImplWin32D2DDXGI_ShutdownPlatform(ImGuiApp* app)
{
    // Graphics first (wrapped imgui backends + device objects need the window alive), then the host.
    if (ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app) != nullptr)
        ImGuiApp_ImplWin32D2DDXGI_Shutdown(app);

    ImGuiApp_ImplWin32D2DDXGI_PlatformData* state = (ImGuiApp_ImplWin32D2DDXGI_PlatformData*)app->PlatformData;
    if (state == nullptr)
        return;
    if (state->OwnsImGuiContext)
    {
        ImGui::DestroyContext();
        state->OwnsImGuiContext = false;
    }
    if (state->Hwnd != nullptr)
        ::SetWindowLongPtr(state->Hwnd, GWLP_USERDATA, 0);
    if (state->Hwnd != nullptr)
    {
        ::DestroyWindow(state->Hwnd);
        state->Hwnd = nullptr;
    }
    if (state->WindowClass.lpszClassName != nullptr && state->WindowClass.hInstance != nullptr)
    {
        ::UnregisterClassA(state->WindowClass.lpszClassName, state->WindowClass.hInstance);
        state->WindowClass = {};
    }
    IM_DELETE(state);
    app->PlatformData = nullptr;
}

// Message-pump run loop, the canonical demo shape: pump, vblank-wait, frame, DwmFlush + throttle when idle.
int ImGuiApp_ImplWin32D2DDXGI_RunLoop(ImGuiApp* app)
{
    HWND hwnd = (HWND)app->PlatformWindowHandle;
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;
        // Recording must encode every frame -- a minimized app keeps running so the
        // recorder can synthesize pause frames (the backend renders nothing at 0 size).
        if (hwnd && ::IsIconic(hwnd) && app->Recorder == nullptr)
        {
            ::Sleep(10);
            continue;
        }
        ImGui::AppPacerWait(app);   // unconditional; Off returns immediately. Modal WM_TIMER repaints stay unpaced.
        ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);
        if (bd != nullptr && !bd->NoWaitForVBlank)
            ImGuiApp_ImplWin32D2DDXGI_WaitForVerticalBlank(bd, hwnd);
        app->Frame();
        // Canonical idle tail (sync composition + wait for events or ~one timer tick) ONLY when no pacer
        // owns the cadence: with an active pacer it would throttle the frame rate below the paced target.
        if (app->Pacer.Mode == ImGuiAppPacerMode_Off && !::GetInputState())
        {
            ::DwmFlush();
            ::MsgWaitForMultipleObjects(0, nullptr, FALSE, USER_TIMER_MINIMUM - 1, QS_ALLEVENTS);
        }
    }

    app->Shutdown();
    return 0;
}

static const ImGuiAppPlatformBackend ImGuiApp_ImplWin32D2DDXGI_PlatformBackend =
{
    ImGuiApp_ImplWin32D2DDXGI_InitPlatform,
    ImGuiApp_ImplWin32D2DDXGI_ShutdownPlatform,
    ImGuiApp_ImplWin32D2DDXGI_RunLoop,
    ImGuiApp_ImplWin32D2DDXGI_CaptureFrame,
    "imguiapp_impl_win32_d2ddxgi",
    ImGuiApp_ImplWin32D2DDXGI_Shutdown,
    ImGuiApp_ImplWin32D2DDXGI_NewFrame,
    ImGuiApp_ImplWin32D2DDXGI_RenderDrawData,
    ImGuiApp_ImplWin32D2DDXGI_PresentFrame,
};

const ImGuiAppPlatformBackend* ImGuiApp_ImplWin32D2DDXGI_GetPlatformBackend() { return &ImGuiApp_ImplWin32D2DDXGI_PlatformBackend; }

#endif // #ifndef IMGUI_DISABLE
