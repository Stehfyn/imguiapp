// dear imgui app: Renderer Host for Win32 + Direct3D11 on a DirectComposition swapchain (composes imgui_impl_win32 + imgui_impl_dx11)
// Self-contained host: owns the immersive borderless window, its WndProc, and the message-pump run loop (does NOT use imguiapp_impl_win32).

// Implemented features:
//  [X] Renderer: exposed ImGuiApp_ImplWin32D2DDXGI_* frame lifecycle (imgui impl pattern), driven by ImGuiApp's frame phases.
//  [X] Platform: immersive window (WS_EX_NOREDIRECTIONBITMAP borderless) + D3D11 device + D2D device context + DXGI composition swapchain + DComp visual in InitPlatform/ShutdownPlatform.
//  [X] Platform: never-resized desktop-sized swapchain; smooth resize via WM_NCCALCSIZE repaint + restart/no-sequence present ladder.
//  [X] Multi-viewport: per-viewport DirectComposition swapchains + DComp targets; pacing-aware per-viewport present skip.
//  [X] AV: synchronous staging-texture CaptureFrame (CopySubresourceRegion + Map; stalls the pipeline).
//  [X] Platform: canonical D2D caption chrome (min/max/close + light/dark, Segoe MDL2 glyph runs), self-hosted on the client seams; opt out via InitInfo::NoChrome.
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
#include <dwrite.h>   // DWriteCreateFactory, IDWriteFontFace::GetGlyphIndices (caption chrome glyphs)

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
    bool NoWaitForVBlank;
    bool ModalRepaintRenderOnly;
    bool EnableClear;
    bool NoBlurBehind;
    bool NoDarkModeLadder;

    // Client seams
    ImGuiApp_ImplWin32D2DDXGI_DeviceDrawCallback PreDrawCallback; // before the imgui pass (raw D3D)
    ImGuiApp_ImplWin32D2DDXGI_DeviceDrawCallback DrawCallback;    // after the imgui pass, D2D BeginDraw/EndDraw bracketed
    ImGuiApp_ImplWin32D2DDXGI_NCHitTestCallback  NCHitTestCallback;
    ImGuiApp_ImplWin32D2DDXGI_ChromeData*        Chrome;          // built-in caption chrome (rides the two seams above); null = none

    // WndProc frame state
    bool Moving;                            // WM_MOVING seen since the last WM_ENTERSIZEMOVE
    bool Resizing;                          // WM_SIZING seen since the last WM_ENTERSIZEMOVE
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

    // Per-viewport pacing (secondary platform windows): decided ONCE per viewport per frame in
    // Platform_RenderWindow, consumed by the render + present hooks. A skipped viewport keeps its last contents.
    ImGuiStorage VpSkip;                    // viewport ID -> skip present this frame

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

// Caption strip in SCREEN coords (like the reference: window rect inset by the borders, CaptionHeight tall).
static bool ImGuiApp_ImplWin32D2DDXGI_GetCaptionRect(HWND hwnd, int caption_height, RECT* out_rect)
{
    const UINT dpi    = ::GetDpiForWindow(hwnd);
    const int  height = ::MulDiv(caption_height, (int)dpi, 96);
    if (!::GetWindowRect(hwnd, out_rect))
        return false;
    SIZE border;
    ImGuiApp_ImplWin32D2DDXGI_GetWindowBorders(hwnd, &border);
    const BOOL maximized = ::IsZoomed(hwnd);
    out_rect->top    += (border.cy * !maximized);
    out_rect->left   += (border.cx * !maximized);
    out_rect->right  -= (border.cx * !maximized);
    out_rect->bottom  = out_rect->top + height;
    return true;
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
    desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
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
// then either replace it in sync (DO_NOT_SEQUENCE) or queue the next immediate present.
static void ImGuiApp_ImplWin32D2DDXGI_PresentLadder(ImGuiApp_ImplWin32D2DDXGI_Data* bd, bool restart, bool vsync)
{
    if (bd->Swapchain == nullptr)
        return;
    HRESULT hr;
    hr = bd->Swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING | DXGI_PRESENT_DO_NOT_WAIT | (restart ? DXGI_PRESENT_RESTART : 0));
    IM_ASSERT(SUCCEEDED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING);
    if (restart || vsync)
        hr = bd->Swapchain->Present(1, DXGI_PRESENT_DO_NOT_SEQUENCE);   // present the current buffer ahead and instead of the would-be next back buffer
    else
        hr = bd->Swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING | DXGI_PRESENT_DO_NOT_WAIT);
    IM_ASSERT(SUCCEEDED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING);
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
static LRESULT ImGuiApp_ImplWin32D2DDXGI_DefaultNCHitTest(ImGuiApp_ImplWin32D2DDXGI_Data* bd, HWND hwnd, int x, int y)
{
    SIZE border;
    ImGuiApp_ImplWin32D2DDXGI_GetWindowBorders(hwnd, &border);
    const BOOL maximized = ::IsZoomed(hwnd);
    const POINT cursor = { (LONG)x, (LONG)y + (!maximized * border.cy) + (-1 * !maximized) };
    RECT rc = {};
    ::GetWindowRect(hwnd, &rc);
    const int hit_left   = (cursor.x >= rc.left && cursor.x <= rc.left + ((border.cx + 5) * !maximized)) ? 1 : 0;
    const int hit_top    = (cursor.y >= rc.top - 2 && cursor.y <= rc.top + ((border.cy + 5) * !maximized)) ? 2 : 0;
    const int hit_right  = (cursor.x >= rc.right - ((border.cx + 5) * !maximized) && cursor.x <= rc.right) ? 4 : 0;
    const int hit_bottom = (cursor.y >= rc.bottom - 5 && cursor.y <= rc.bottom + border.cy) ? 8 : 0;
    switch (hit_left | hit_top | hit_right | hit_bottom)
    {
    case 1:     return HTLEFT;
    case 4:     return HTRIGHT;
    case 8:     return HTBOTTOM;
    case 2 | 1: return HTTOPLEFT;
    case 2 | 4: return HTTOPRIGHT;
    case 8 | 1: return HTBOTTOMLEFT;
    case 8 | 4: return HTBOTTOMRIGHT;
    case 2:     return HTTOP;
    case 0:
    {
        RECT caption;
        if (ImGuiApp_ImplWin32D2DDXGI_GetCaptionRect(hwnd, bd != nullptr ? bd->CaptionHeight : 30, &caption) && ::PtInRect(&caption, cursor))
            return HTCAPTION;
        return HTCLIENT;
    }
    default:    return HTNOWHERE;
    }
}

//--------------------------------------------------------------------------------------------------------
// Built-in caption chrome (transcribed from ImmersiveWindow.c BeginImmersivePaint + DrawImmersive* + OnNCHittest)
// Self-hosted on this backend's own client seams: InstallChrome registers the draw + hittest callbacks
// through the same public setters any client would use.
//--------------------------------------------------------------------------------------------------------

// Segoe MDL2 Assets codepoints + the glyph-index-array slots the reference draws from.
#define CP_MDL2_SETTINGS1  (0xE115)
#define CP_MDL2_SETTINGS2  (0xE713)
#define CP_MDL2_SETTINGS3  (0xF8B0)
#define CP_MDL2_LIGHTMODE1 (0xE706)
#define CP_MDL2_LIGHTMODE2 (0xED39)
#define CP_MDL2_DARKMODE1  (0xE708)
#define CP_MDL2_DARKMODE2  (0xEC46)
#define CP_MDL2_DARKMODE3  (0xF0CE)
#define CP_MDL2_LIGHTDARK1 (0xE793)
#define CP_MDL2_LIGHTDARK2 (0xF08C)
#define CP_MDL2_MINIMIZE   (0xE921)
#define CP_MDL2_MAXIMIZE   (0xE922)
#define CP_MDL2_RESTORE    (0xE923)
#define CP_MDL2_CLOSE      (0xE947)
#define CP_MDL2_FEEDBACK   (0xED15)
#define CP_SYM_LIGHTMODE   (0x263C)
#define CP_SYM_DARKMODE    (0xE28C)
#define CP_SYM_LIGHTMODE2  (0xE706)

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
    IDWriteFontCollection* FontCollection;
    IDWriteFontFamily*     FontFamily;       // "Segoe MDL2 Assets"
    IDWriteFont*           Font;
    IDWriteFontFace*       FontFace;
    IDWriteFontFamily*     FontFamily2;      // "Segoe UI Symbol" (created like the reference; the light/dark glyphs draw from MDL2)
    IDWriteFont*           Font2;
    IDWriteFontFace*       FontFace2;
    UINT32                 CodePointsMDL2[15];
    UINT16                 GlyphIndicesMDL2[15];
    UINT32                 CodePointsSym[3];
    UINT16                 GlyphIndicesSym[3];
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

static inline int  ImGuiApp_ImplWin32D2DDXGI_RectWidth(const RECT& rc)  { return labs(rc.right - rc.left); }
static inline int  ImGuiApp_ImplWin32D2DDXGI_RectHeight(const RECT& rc) { return labs(rc.bottom - rc.top); }
static inline void ImGuiApp_ImplWin32D2DDXGI_RectToLogical(RECT* rc)    { rc->right = ImGuiApp_ImplWin32D2DDXGI_RectWidth(*rc); rc->bottom = ImGuiApp_ImplWin32D2DDXGI_RectHeight(*rc); rc->left = 0; rc->top = 0; }
static inline D2D1_RECT_F ImGuiApp_ImplWin32D2DDXGI_RectToRectF(const RECT& rc) { return D2D1::RectF((FLOAT)rc.left, (FLOAT)rc.top, (FLOAT)rc.right, (FLOAT)rc.bottom); }
static inline FLOAT ImGuiApp_ImplWin32D2DDXGI_Roundf(FLOAT f)           { return (FLOAT)((INT)f > (INT)(f + 0.5f) ? (INT)f : (INT)(f + 0.5f)); } // the reference's MyRoundf

// The reference's GetCaptionButtonRectFast: right-aligned 45*dpi-wide slots, index = buttons from the right
// (0 = close, 1 = maximize, 2 = minimize, 3 = light/dark).
static void ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(const RECT& caption, int button_width, int slot, RECT* out_rect)
{
    *out_rect = caption;
    out_rect->left = (out_rect->right - button_width) + 1;
    if (slot > 0)
    {
        out_rect->left  -= slot * button_width + 1;
        out_rect->right -= slot * button_width + 1;
    }
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

// DrawImmersiveCloseButton, verbatim geometry (glyph drawn twice, GDI_NATURAL, like the reference).
static void ImGuiApp_ImplWin32D2DDXGI_ChromeDrawClose(ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome, ID2D1DeviceContext* ctx, ID2D1SolidColorBrush* text,
                                                      DWRITE_GLYPH_RUN* run, int glyph_inset, int glyph_inset2, float font_size, RECT caption, RECT button)
{
    ImGuiApp_ImplWin32D2DDXGI_RectToLogical(&caption);
    ImGuiApp_ImplWin32D2DDXGI_RectToLogical(&button);
    ::OffsetRect(&button, (ImGuiApp_ImplWin32D2DDXGI_RectWidth(caption) - ImGuiApp_ImplWin32D2DDXGI_RectWidth(button)) - 1, 0);
    D2D1_RECT_F rcf = ImGuiApp_ImplWin32D2DDXGI_RectToRectF(button);

    if (chrome->HotButton == ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Close)
    {
        rcf.left  += 0.5f;
        rcf.right += 0.5f;
        ctx->DrawRectangle(&rcf, chrome->RedBrush, 1.0f, chrome->StrokeStyle);
        ctx->FillRectangle(&rcf, chrome->RedBrush);
    }
    run->fontFace     = chrome->FontFace;
    run->fontEmSize   = font_size * 1.25f;
    run->glyphIndices = &chrome->GlyphIndicesMDL2[13]; // CP_MDL2_CLOSE
    D2D1_POINT_2F pos;
    pos.x = button.left + (0.5f * (ImGuiApp_ImplWin32D2DDXGI_RectWidth(button) - 1)) - glyph_inset;
    pos.y = button.top + (0.5f * ImGuiApp_ImplWin32D2DDXGI_RectHeight(button)) + glyph_inset2 - 1;
    ctx->DrawGlyphRun(pos, run, text, DWRITE_MEASURING_MODE_GDI_NATURAL);
    ctx->DrawGlyphRun(pos, run, text, DWRITE_MEASURING_MODE_GDI_NATURAL);
}

// DrawImmersiveMaximizeButton, verbatim geometry (including the reference's float-on-LONG bottom adjust).
static void ImGuiApp_ImplWin32D2DDXGI_ChromeDrawMaximize(ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome, ID2D1DeviceContext* ctx, ID2D1SolidColorBrush* text,
                                                         DWRITE_GLYPH_RUN* run, int glyph_inset, float font_size, UINT dpi, RECT caption, RECT button, bool maximized)
{
    ImGuiApp_ImplWin32D2DDXGI_RectToLogical(&button);
    ::OffsetRect(&button, (ImGuiApp_ImplWin32D2DDXGI_RectWidth(caption) - (2 * ImGuiApp_ImplWin32D2DDXGI_RectWidth(button))) - 2, 0);
    const D2D1_RECT_F rcf = ImGuiApp_ImplWin32D2DDXGI_RectToRectF(button);

    ::InflateRect(&button, -::MulDiv(18, (int)dpi, 96), -::MulDiv(10, (int)dpi, 96)); // the reference's InsetRect
    button.bottom = (LONG)(button.bottom - 1.5f);
    button.right -= 1;
    button.left  -= 1;

    const bool hot = chrome->HotButton == ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Max;
    if (hot)
    {
        ctx->DrawRectangle(&rcf, chrome->ColorizationBrush, 1.0f, chrome->StrokeStyle);
        ctx->FillRectangle(&rcf, chrome->ColorizationBrush);
    }
    run->fontFace     = chrome->FontFace;
    run->fontEmSize   = font_size;
    run->glyphIndices = maximized ? &chrome->GlyphIndicesMDL2[12] : &chrome->GlyphIndicesMDL2[11]; // RESTORE : MAXIMIZE
    D2D1_POINT_2F pos;
    pos.x = (FLOAT)button.left;
    pos.y = button.top + (0.5f * ImGuiApp_ImplWin32D2DDXGI_RectHeight(button)) + glyph_inset;
    ctx->DrawGlyphRun(pos, run, text, DWRITE_MEASURING_MODE_NATURAL);
}

// DrawImmersiveMinimizeButton, verbatim geometry (the glyph centers on the PRE-inset button rect).
static void ImGuiApp_ImplWin32D2DDXGI_ChromeDrawMinimize(ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome, ID2D1DeviceContext* ctx, ID2D1SolidColorBrush* text,
                                                         DWRITE_GLYPH_RUN* run, int glyph_inset, float font_size, UINT dpi, RECT caption, RECT button)
{
    ImGuiApp_ImplWin32D2DDXGI_RectToLogical(&button);
    ::OffsetRect(&button, (ImGuiApp_ImplWin32D2DDXGI_RectWidth(caption) - (3 * ImGuiApp_ImplWin32D2DDXGI_RectWidth(button))) - 3, 0);
    const D2D1_RECT_F rcf = ImGuiApp_ImplWin32D2DDXGI_RectToRectF(button);

    const int width_symbol = ::MulDiv(9, (int)dpi, 96);
    ::InflateRect(&button,
                  -(LONG)((0.5f * ::MulDiv(45, (int)dpi, 96)) - (0.5f * width_symbol)),
                  -(LONG)(0.5f * ::MulDiv(30, (int)dpi, 96)));

    run->fontFace     = chrome->FontFace;
    run->fontEmSize   = font_size;
    run->glyphIndices = &chrome->GlyphIndicesMDL2[10]; // CP_MDL2_MINIMIZE
    D2D1_POINT_2F pos;
    pos.x = rcf.left + (0.5f * (rcf.right - rcf.left)) - glyph_inset;
    pos.y = rcf.top + (0.5f * (rcf.bottom - rcf.top)) + glyph_inset;
    if (chrome->HotButton == ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Min)
    {
        ctx->DrawRectangle(&rcf, chrome->ColorizationBrush, 1.0f, chrome->StrokeStyle);
        ctx->FillRectangle(&rcf, chrome->ColorizationBrush);
    }
    ctx->DrawGlyphRun(pos, run, text, DWRITE_MEASURING_MODE_NATURAL);
}

// DrawImmersiveLightDarkButton, verbatim geometry (hot state re-fills and re-draws the glyph, like the reference).
static void ImGuiApp_ImplWin32D2DDXGI_ChromeDrawLightDark(ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome, ID2D1DeviceContext* ctx, ID2D1SolidColorBrush* text,
                                                          DWRITE_GLYPH_RUN* run, int glyph_inset2, float font_size, RECT caption, RECT button)
{
    run->fontFace     = chrome->FontFace;
    run->glyphIndices = chrome->LightMode ? &chrome->GlyphIndicesMDL2[7] : &chrome->GlyphIndicesMDL2[3]; // DARKMODE3 : LIGHTMODE1
    run->fontEmSize   = font_size * 1.5f;

    ImGuiApp_ImplWin32D2DDXGI_RectToLogical(&button);
    ::OffsetRect(&button, (ImGuiApp_ImplWin32D2DDXGI_RectWidth(caption) - (4 * ImGuiApp_ImplWin32D2DDXGI_RectWidth(button))) - 2, 0);
    const D2D1_RECT_F rcf = ImGuiApp_ImplWin32D2DDXGI_RectToRectF(button);

    D2D1_POINT_2F pos;
    pos.x = rcf.left + (0.5f * (rcf.right - rcf.left)) - glyph_inset2;
    pos.y = rcf.top + (0.5f * (rcf.bottom - rcf.top)) + glyph_inset2;
    ctx->DrawGlyphRun(pos, run, text, DWRITE_MEASURING_MODE_NATURAL);

    if (chrome->HotButton == ImGuiApp_ImplWin32D2DDXGI_CaptionButton_LightDark)
    {
        ctx->DrawRectangle(&rcf, chrome->ColorizationBrush, 1.0f, chrome->StrokeStyle);
        ctx->FillRectangle(&rcf, chrome->ColorizationBrush);
        ctx->DrawGlyphRun(pos, run, text, DWRITE_MEASURING_MODE_NATURAL);
    }
}

// The reference's BeginImmersivePaint drawing body, driven through the SetDeviceDrawCallback seam
// (the backend's D2D bracket already applied the canonical transform + antialias state).
static void ImGuiApp_ImplWin32D2DDXGI_ChromeDrawCallback(ImGuiApp* app)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);
    ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome = bd != nullptr ? bd->Chrome : nullptr;
    if (chrome == nullptr || bd->D2DDeviceContext == nullptr)
        return;
    HWND hwnd = (HWND)bd->Hwnd;

    const UINT  dpi          = ::GetDpiForWindow(hwnd);
    const bool  maximized    = ::IsZoomed(hwnd) != FALSE;
    const int   button_width = ::MulDiv(45, (int)dpi, 96);
    const float font_size    = (float)::MulDiv(10, (int)dpi, 96);   // the reference's GetFontSizeFromWindowDpi
    const int   glyph_inset  = (int)ImGuiApp_ImplWin32D2DDXGI_Roundf(0.5f * font_size);
    const int   glyph_inset2 = (int)ImGuiApp_ImplWin32D2DDXGI_Roundf(0.5f * font_size * 1.5f);

    RECT caption;
    if (!ImGuiApp_ImplWin32D2DDXGI_GetCaptionRect(hwnd, bd->CaptionHeight, &caption))
        return;
    RECT close_button, max_button, min_button, lightdark_button;
    ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(caption, button_width, 0, &close_button);
    ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(caption, button_width, 1, &max_button);
    ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(caption, button_width, 2, &min_button);
    ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(caption, button_width, 3, &lightdark_button);

    const DWRITE_GLYPH_OFFSET glyph_offsets = {};
    DWRITE_GLYPH_RUN glyph_run = {};
    glyph_run.glyphCount   = 1;
    glyph_run.glyphOffsets = &glyph_offsets;

    ID2D1SolidColorBrush* text = chrome->DarkMode ? chrome->Brush : chrome->BlackBrush;

    ImGuiApp_ImplWin32D2DDXGI_ChromeDrawClose(chrome, bd->D2DDeviceContext, text, &glyph_run, glyph_inset, glyph_inset2, font_size, caption, close_button);
    ImGuiApp_ImplWin32D2DDXGI_ChromeDrawMaximize(chrome, bd->D2DDeviceContext, text, &glyph_run, glyph_inset, font_size, dpi, caption, max_button, maximized);
    ImGuiApp_ImplWin32D2DDXGI_ChromeDrawMinimize(chrome, bd->D2DDeviceContext, text, &glyph_run, glyph_inset, font_size, dpi, caption, min_button);
    ImGuiApp_ImplWin32D2DDXGI_ChromeDrawLightDark(chrome, bd->D2DDeviceContext, text, &glyph_run, glyph_inset2, font_size, caption, lightdark_button);
}

// The reference's OnNCHittest client-area branch: borders from the default hittest, then the button
// rects (hot-tracked) and the caption strip. Registered through the SetNCHitTestCallback seam.
static int ImGuiApp_ImplWin32D2DDXGI_ChromeNCHitTest(ImGuiApp* app, int x, int y)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);
    ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome = bd != nullptr ? bd->Chrome : nullptr;
    if (chrome == nullptr)
        return -1;
    HWND hwnd = (HWND)bd->Hwnd;

    const LRESULT hit = ImGuiApp_ImplWin32D2DDXGI_DefaultNCHitTest(bd, hwnd, x, y);
    if (hit != HTCAPTION && hit != HTCLIENT)
    {
        chrome->HotButton = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_None;
        return (int)hit;
    }

    SIZE border;
    ImGuiApp_ImplWin32D2DDXGI_GetWindowBorders(hwnd, &border);
    const BOOL maximized = ::IsZoomed(hwnd);
    const POINT cursor = { (LONG)x, (LONG)y + (!maximized * border.cy) + (-1 * !maximized) };

    RECT caption;
    if (ImGuiApp_ImplWin32D2DDXGI_GetCaptionRect(hwnd, bd->CaptionHeight, &caption))
    {
        const int button_width = ::MulDiv(45, (int)::GetDpiForWindow(hwnd), 96);
        RECT close_button, max_button, min_button, lightdark_button;
        ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(caption, button_width, 0, &close_button);
        ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(caption, button_width, 1, &max_button);
        ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(caption, button_width, 2, &min_button);
        ImGuiApp_ImplWin32D2DDXGI_GetCaptionButtonRect(caption, button_width, 3, &lightdark_button);

        if (::PtInRect(&max_button, cursor))       { chrome->HotButton = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Max;       return HTMAXBUTTON; }
        if (::PtInRect(&min_button, cursor))       { chrome->HotButton = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Min;       return HTMINBUTTON; }
        if (::PtInRect(&close_button, cursor))     { chrome->HotButton = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Close;     return HTCLOSE; }
        if (::PtInRect(&lightdark_button, cursor)) { chrome->HotButton = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_LightDark; return HTMENU; }
        if (::PtInRect(&caption, cursor))          { chrome->HotButton = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_Caption;   return HTCAPTION; }
    }
    chrome->HotButton = ImGuiApp_ImplWin32D2DDXGI_CaptionButton_None;
    return (int)hit;
}

// The caption light/dark button's WM_NCLBUTTONUP action (reference OnNCLButtonUp CB_LIGHTDARKBUTTON branch).
static void ImGuiApp_ImplWin32D2DDXGI_ChromeToggleLightDark(ImGuiApp_ImplWin32D2DDXGI_ChromeData* chrome, HWND hwnd)
{
    const ULONGLONG now = ::GetTickCount64();
    if (now - chrome->LastLightDarkChangeMs <= 150)
        return;
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
    chrome->DarkMode = true;   // the reference's OnCreate default

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

    const D2D1_STROKE_STYLE_PROPERTIES1 stroke_props =
    {
        D2D1_CAP_STYLE_SQUARE, D2D1_CAP_STYLE_SQUARE, D2D1_CAP_STYLE_SQUARE,
        D2D1_LINE_JOIN_ROUND, 0.0f, D2D1_DASH_STYLE_SOLID, 0.0f, D2D1_STROKE_TRANSFORM_TYPE_HAIRLINE,
    };
    bd->D2DFactory->CreateStrokeStyle(stroke_props, nullptr, 0, &chrome->StrokeStyle);

    // DWrite glyph ladder (OnInitD2D verbatim; base interfaces carry the same calls the reference's C macros hit).
    bool fonts_ok = SUCCEEDED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&chrome->DWriteFactory)) &&
                    SUCCEEDED(chrome->DWriteFactory->GetSystemFontCollection(&chrome->FontCollection, TRUE));
    if (fonts_ok)
    {
        UINT32 index  = 0;
        BOOL   exists = FALSE;
        fonts_ok = SUCCEEDED(chrome->FontCollection->FindFamilyName(L"Segoe MDL2 Assets", &index, &exists)) && exists &&
                   SUCCEEDED(chrome->FontCollection->GetFontFamily(index, &chrome->FontFamily)) &&
                   SUCCEEDED(chrome->FontFamily->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_THIN, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &chrome->Font)) &&
                   SUCCEEDED(chrome->Font->CreateFontFace(&chrome->FontFace));
    }
    if (fonts_ok)
    {
        const UINT32 codepoints[15] =
        {
            CP_MDL2_SETTINGS1, CP_MDL2_SETTINGS2, CP_MDL2_SETTINGS3, CP_MDL2_LIGHTMODE1, CP_MDL2_LIGHTMODE2,
            CP_MDL2_DARKMODE1, CP_MDL2_DARKMODE2, CP_MDL2_DARKMODE3, CP_MDL2_LIGHTDARK1, CP_MDL2_LIGHTDARK2,
            CP_MDL2_MINIMIZE, CP_MDL2_MAXIMIZE, CP_MDL2_RESTORE, CP_MDL2_CLOSE, CP_MDL2_FEEDBACK,
        };
        memcpy(chrome->CodePointsMDL2, codepoints, sizeof(codepoints));
        fonts_ok = SUCCEEDED(chrome->FontFace->GetGlyphIndices(chrome->CodePointsMDL2, 15, chrome->GlyphIndicesMDL2));
    }
    if (fonts_ok)
    {
        // "Segoe UI Symbol" secondary face, created like the reference (the drawn glyphs come from MDL2).
        UINT32 index  = 0;
        BOOL   exists = FALSE;
        if (SUCCEEDED(chrome->FontCollection->FindFamilyName(L"Segoe UI Symbol", &index, &exists)) && exists &&
            SUCCEEDED(chrome->FontCollection->GetFontFamily(index, &chrome->FontFamily2)) &&
            SUCCEEDED(chrome->FontFamily2->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_THIN, DWRITE_FONT_STRETCH_UNDEFINED, DWRITE_FONT_STYLE_NORMAL, &chrome->Font2)) &&
            SUCCEEDED(chrome->Font2->CreateFontFace(&chrome->FontFace2)))
        {
            const UINT32 sym_codepoints[3] = { CP_SYM_DARKMODE, CP_SYM_LIGHTMODE, CP_SYM_LIGHTMODE2 };
            memcpy(chrome->CodePointsSym, sym_codepoints, sizeof(sym_codepoints));
            chrome->FontFace2->GetGlyphIndices(chrome->CodePointsSym, 3, chrome->GlyphIndicesSym);
        }
    }

    if (!fonts_ok || chrome->Brush == nullptr || chrome->StrokeStyle == nullptr)
    {
        bd->Chrome = chrome;   // let Uninstall release whatever was created
        ImGuiApp_ImplWin32D2DDXGI_UninstallChrome(app);
        return false;
    }

    bd->Chrome = chrome;
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

    if (chrome->FontFace2 != nullptr)              { chrome->FontFace2->Release(); }
    if (chrome->Font2 != nullptr)                  { chrome->Font2->Release(); }
    if (chrome->FontFamily2 != nullptr)            { chrome->FontFamily2->Release(); }
    if (chrome->FontFace != nullptr)               { chrome->FontFace->Release(); }
    if (chrome->Font != nullptr)                   { chrome->Font->Release(); }
    if (chrome->FontFamily != nullptr)             { chrome->FontFamily->Release(); }
    if (chrome->FontCollection != nullptr)         { chrome->FontCollection->Release(); }
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

static LRESULT WINAPI ImGuiApp_ImplWin32D2DDXGI_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    ImGuiApp* app = (ImGuiApp*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(app);

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
    case WM_NCLBUTTONDBLCLK:
        if (wParam == HTCAPTION)
        {
            if (::IsZoomed(hWnd))
                ::PostMessage(hWnd, WM_SYSCOMMAND, SC_RESTORE, 0);
            else
                ::PostMessage(hWnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
            return 0;
        }
        break;
    case WM_NCLBUTTONUP:
        // Button codes come from the chrome's hittest (or a client callback); the default hittest never reports them.
        switch (wParam)
        {
        case HTCLOSE:     ::PostMessage(hWnd, WM_CLOSE, 0, 0); return 0;
        case HTMINBUTTON: ::PostMessage(hWnd, WM_SYSCOMMAND, SC_MINIMIZE, 0); return 0;
        case HTMAXBUTTON: ::PostMessage(hWnd, WM_SYSCOMMAND, ::IsZoomed(hWnd) ? SC_RESTORE : SC_MAXIMIZE, 0); return 0;
        case HTMENU:
            if (bd != nullptr && bd->Chrome != nullptr && bd->Chrome->HotButton == ImGuiApp_ImplWin32D2DDXGI_CaptionButton_LightDark)
            {
                ImGuiApp_ImplWin32D2DDXGI_ChromeToggleLightDark(bd->Chrome, hWnd);
                return 0;
            }
            break;
        default: break;
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
            bd->Moving   = false;
            bd->Resizing = false;
        }
        ::SetTimer(hWnd, 0x69, USER_TIMER_MINIMUM, nullptr);
        return 0;
    case WM_EXITSIZEMOVE:
    case WM_EXITMENULOOP:
        ::KillTimer(hWnd, 0x69);
        ImGuiApp_ImplWin32D2DDXGI_ModalRepaint(app, bd, hWnd, false, true, true);
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
        const int  button_width = ::MulDiv(45, (int)dpi, 96);
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
        return ::DefWindowProc(hWnd, WM_NCACTIVATE, TRUE, 0);   // reference: always claim active (no dwm frame flash)
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
struct ImGuiApp_ImplWin32D2DDXGI_ViewportData
{
    IDXGISwapChain1*        Swapchain;
    ID3D11RenderTargetView* RTView;
    IDCompositionTarget*    DCompTarget;
    IDCompositionVisual*    DCompVisual;

    ImGuiApp_ImplWin32D2DDXGI_ViewportData()  { Swapchain = nullptr; RTView = nullptr; DCompTarget = nullptr; DCompVisual = nullptr; }
    ~ImGuiApp_ImplWin32D2DDXGI_ViewportData() { IM_ASSERT(Swapchain == nullptr && RTView == nullptr && DCompTarget == nullptr && DCompVisual == nullptr); }
};

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

    // Secondary viewports get their own composition swapchain: sized to the viewport (resized on demand,
    // unlike the main one), no tearing/waitable flags -- they present through the compositor.
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode          = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Scaling            = DXGI_SCALING_STRETCH;
    desc.SampleDesc.Count   = 1;
    desc.Width              = (UINT)viewport->Size.x;
    desc.Height             = (UINT)viewport->Size.y;
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

static void ImGuiApp_ImplWin32D2DDXGI_Renderer_SetWindowSize(ImGuiViewport* viewport, ImVec2 size)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(ImGuiApp_ImplWin32D2DDXGI_GetApp());
    ImGuiApp_ImplWin32D2DDXGI_ViewportData* vd = (ImGuiApp_ImplWin32D2DDXGI_ViewportData*)viewport->RendererUserData;
    if (bd == nullptr || vd == nullptr || vd->Swapchain == nullptr)
        return;
    if (vd->RTView != nullptr)
    {
        vd->RTView->Release();
        vd->RTView = nullptr;
    }
    vd->Swapchain->ResizeBuffers(0, (UINT)size.x, (UINT)size.y, DXGI_FORMAT_UNKNOWN, 0);
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

// Per-viewport pacing: the decision is made ONCE per viewport per frame here (the first per-viewport hook
// RenderPlatformWindowsDefault runs) and consumed by the render + present hooks below.
static void ImGuiApp_ImplWin32D2DDXGI_Platform_RenderWindow(ImGuiViewport* viewport, void*)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(ImGuiApp_ImplWin32D2DDXGI_GetApp());
    if (bd == nullptr)
        return;
    const bool present = ImGui::AppPacerViewportShouldPresent(bd->App, viewport);
    bd->VpSkip.SetBool(viewport->ID, !present);
}

static void ImGuiApp_ImplWin32D2DDXGI_Renderer_RenderWindow(ImGuiViewport* viewport, void*)
{
    ImGuiApp_ImplWin32D2DDXGI_Data* bd = ImGuiApp_ImplWin32D2DDXGI_GetBackendData(ImGuiApp_ImplWin32D2DDXGI_GetApp());
    if (bd == nullptr || bd->VpSkip.GetBool(viewport->ID))
        return;
    ImGuiApp_ImplWin32D2DDXGI_ViewportData* vd = (ImGuiApp_ImplWin32D2DDXGI_ViewportData*)viewport->RendererUserData;
    if (vd == nullptr || vd->RTView == nullptr)
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
    if (ImGuiApp_ImplWin32D2DDXGI_ViewportData* vd = (ImGuiApp_ImplWin32D2DDXGI_ViewportData*)viewport->RendererUserData)
        if (vd->Swapchain != nullptr)
            vd->Swapchain->Present(0, 0);
}

// REPLACES the hooks imgui_impl_dx11 installed (its per-hwnd blt swapchains would fight the composition
// tree); teardown rides the wrapped backends' Shutdown (they call ClearPlatformHandlers/ClearRendererHandlers).
static void ImGuiApp_ImplWin32D2DDXGI_InitMultiViewportSupport(ImGuiApp_ImplWin32D2DDXGI_Data* bd)
{
    IM_UNUSED(bd);
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    IM_ASSERT(platform_io.Platform_RenderWindow == nullptr);
    platform_io.Renderer_CreateWindow  = ImGuiApp_ImplWin32D2DDXGI_Renderer_CreateWindow;
    platform_io.Renderer_DestroyWindow = ImGuiApp_ImplWin32D2DDXGI_Renderer_DestroyWindow;
    platform_io.Renderer_SetWindowSize = ImGuiApp_ImplWin32D2DDXGI_Renderer_SetWindowSize;
    platform_io.Renderer_RenderWindow  = ImGuiApp_ImplWin32D2DDXGI_Renderer_RenderWindow;
    platform_io.Renderer_SwapBuffers   = ImGuiApp_ImplWin32D2DDXGI_Renderer_SwapBuffers;
    platform_io.Platform_RenderWindow  = ImGuiApp_ImplWin32D2DDXGI_Platform_RenderWindow;
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
        if (!::GetInputState())   // canonical idle tail: sync composition, then wait for events or ~one timer tick
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
