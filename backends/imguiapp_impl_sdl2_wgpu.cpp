// dear imgui app: Renderer Host for SDL2 + WebGPU/Dawn (composes imgui_impl_sdl2 + imgui_impl_wgpu)
// This needs to be used along with the SDL2 Platform Host (imguiapp_impl_sdl2: shared browser run loop).

// Implemented features:
//  [X] Renderer: exposed ImGuiApp_ImplSDL2WGPU_* frame lifecycle (imgui impl pattern; RenderDrawData submits AND presents -- no PresentFrame hook).
//  [X] Platform: window/surface/device creation + ImGui context ownership in InitPlatform/ShutdownPlatform.
// Missing features:
//  [ ] AV: CaptureFrame readback (recording unavailable on this host; use win32-vulkan).

// CHANGELOG
//  2026-07-08: Threaded ImGuiApp* through the frame lifecycle; backend data moved to app->BackendData, file-scope backend global removed.
//  2026-07-08: Exposed ImGuiApp_ImplSDL2WGPU_* frame lifecycle (imgui impl pattern); host owns the ImGui context it creates; backend-internal symbols prefixed; IMGUI_DISABLE guards added.

#include "imguiapp_impl_sdl2_wgpu.h"
#ifndef IMGUI_DISABLE

#include "imguiapp_impl_sdl2.h"
#include "imguiapp.h"

#include "imgui_impl_sdl2.h"
#include "imgui_impl_wgpu.h"

#include <SDL.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu.h>

#if defined(IMGUI_IMPL_WEBGPU_BACKEND_DAWN)
#include <webgpu/webgpu_cpp.h>
#endif

#include <cstdio>
#include <cstdint>

// Private impl of the opaque app->PlatformData handle (defined per platform host TU; exactly one links per build).
struct ImGuiAppPlatformData
{
    SDL_Window* Window;
    bool        OwnsImGuiContext; // this host created the ImGui context (none existed)
};

struct ImGuiApp_ImplSDL2WGPU_Data
{
    SDL_Window*              Window;
    const char*              CanvasSelector;
    WGPUInstance             Instance;
    WGPUDevice               Device;
    WGPUSurface              Surface;
    WGPUQueue                Queue;
    WGPUSurfaceConfiguration SurfaceConfiguration;
    int                      SurfaceWidth;
    int                      SurfaceHeight;
    bool                     PlatformBackendInitialized;
    bool                     RendererBackendInitialized;

    ImGuiApp_ImplSDL2WGPU_Data() { memset((void*)this, 0, sizeof(*this)); }
};

// Backend data stored in app->BackendData (the io userdata slots belong to the wrapped imgui backends).
static ImGuiApp_ImplSDL2WGPU_Data* ImGuiApp_ImplSDL2WGPU_GetBackendData(ImGuiApp* app)
{
    return app != nullptr ? (ImGuiApp_ImplSDL2WGPU_Data*)app->BackendData : nullptr;
}

static bool ImGuiApp_ImplSDL2WGPU_IsInitInfoValid(const ImGuiApp_ImplSDL2WGPU_InitInfo* init_info)
{
    return init_info != nullptr && init_info->Window != nullptr;
}

static void ImGuiApp_ImplSDL2WGPU_ReadCanvasSize(ImGuiApp_ImplSDL2WGPU_Data* bd, int* width, int* height)
{
    IM_ASSERT(bd != nullptr);
    IM_ASSERT(width != nullptr);
    IM_ASSERT(height != nullptr);

    int canvas_width = 0;
    int canvas_height = 0;
    if (bd != nullptr &&
        bd->CanvasSelector != nullptr &&
        emscripten_get_canvas_element_size(bd->CanvasSelector, &canvas_width, &canvas_height) == EMSCRIPTEN_RESULT_SUCCESS)
    {
        *width = ImMax(1, canvas_width);
        *height = ImMax(1, canvas_height);
        return;
    }

    if (bd != nullptr && bd->Window != nullptr)
        SDL_GetWindowSize(bd->Window, width, height);
    *width = ImMax(1, *width);
    *height = ImMax(1, *height);
}

static WGPUPresentMode ImGuiApp_ImplSDL2WGPU_SelectPresentMode(const WGPUSurfaceCapabilities& capabilities)
{
    const WGPUPresentMode requested_modes[] =
    {
        WGPUPresentMode_Mailbox,
        WGPUPresentMode_Immediate,
        WGPUPresentMode_Fifo,
    };

    for (WGPUPresentMode requested_mode : requested_modes)
        for (size_t i = 0; i < capabilities.presentModeCount; ++i)
            if (capabilities.presentModes[i] == requested_mode)
                return requested_mode;

    return WGPUPresentMode_Fifo;
}

#if defined(IMGUI_IMPL_WEBGPU_BACKEND_DAWN)
static WGPUAdapter ImGuiApp_ImplSDL2WGPU_RequestAdapter(wgpu::Instance& instance)
{
    wgpu::Adapter acquired_adapter;
    wgpu::RequestAdapterOptions adapter_options = {};
    auto on_request_adapter = [&](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message)
    {
        if (status != wgpu::RequestAdapterStatus::Success)
        {
            IMGUIAPP_ERROR_PRINTF("WebGPU adapter request failed: %.*s\n", (int)message.length, message.data);
            return;
        }
        acquired_adapter = std::move(adapter);
    };

    wgpu::Future wait_adapter { instance.RequestAdapter(&adapter_options, wgpu::CallbackMode::WaitAnyOnly, on_request_adapter) };
    wgpu::WaitStatus wait_status = instance.WaitAny(wait_adapter, UINT64_MAX);
    if (acquired_adapter == nullptr || wait_status != wgpu::WaitStatus::Success)
        return nullptr;
    return acquired_adapter.MoveToCHandle();
}

static WGPUDevice ImGuiApp_ImplSDL2WGPU_RequestDevice(wgpu::Instance& instance, wgpu::Adapter& adapter)
{
    wgpu::DeviceDescriptor device_desc = {};
    device_desc.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
        [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message)
        {
            IMGUIAPP_ERROR_PRINTF("WebGPU device lost (%s): %.*s\n", ImGui_ImplWGPU_GetDeviceLostReasonName((WGPUDeviceLostReason)reason), (int)message.length, message.data);
        });
    device_desc.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message)
        {
            IMGUIAPP_ERROR_PRINTF("WebGPU %s error: %.*s\n", ImGui_ImplWGPU_GetErrorTypeName((WGPUErrorType)type), (int)message.length, message.data);
        });

    wgpu::Device acquired_device;
    auto on_request_device = [&](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message)
    {
        if (status != wgpu::RequestDeviceStatus::Success)
        {
            IMGUIAPP_ERROR_PRINTF("WebGPU device request failed: %.*s\n", (int)message.length, message.data);
            return;
        }
        acquired_device = std::move(device);
    };

    wgpu::Future wait_device { adapter.RequestDevice(&device_desc, wgpu::CallbackMode::WaitAnyOnly, on_request_device) };
    wgpu::WaitStatus wait_status = instance.WaitAny(wait_device, UINT64_MAX);
    if (acquired_device == nullptr || wait_status != wgpu::WaitStatus::Success)
        return nullptr;
    return acquired_device.MoveToCHandle();
}
#endif // #if defined(IMGUI_IMPL_WEBGPU_BACKEND_DAWN)

static bool ImGuiApp_ImplSDL2WGPU_ResizeSurface(ImGuiApp_ImplSDL2WGPU_Data* bd, int width, int height)
{
    if (bd == nullptr || bd->Surface == nullptr || bd->Device == nullptr)
        return false;

    width = ImMax(1, width);
    height = ImMax(1, height);
    if (bd->SurfaceConfiguration.width == (uint32_t)width &&
        bd->SurfaceConfiguration.height == (uint32_t)height)
        return true;

    bd->SurfaceConfiguration.width = bd->SurfaceWidth = width;
    bd->SurfaceConfiguration.height = bd->SurfaceHeight = height;
    wgpuSurfaceConfigure(bd->Surface, &bd->SurfaceConfiguration);
    return true;
}

static bool ImGuiApp_ImplSDL2WGPU_InitWGPU(ImGuiApp_ImplSDL2WGPU_Data* bd)
{
    IM_ASSERT(bd != nullptr);
    if (bd == nullptr)
        return false;

    WGPUTextureFormat preferred_format = WGPUTextureFormat_Undefined;
    WGPUPresentMode preferred_present_mode = WGPUPresentMode_Fifo;

#if defined(IMGUI_IMPL_WEBGPU_BACKEND_DAWN)
    wgpu::InstanceDescriptor instance_desc = {};
    static constexpr wgpu::InstanceFeatureName timed_wait_any = wgpu::InstanceFeatureName::TimedWaitAny;
    instance_desc.requiredFeatureCount = 1;
    instance_desc.requiredFeatures = &timed_wait_any;
    wgpu::Instance instance = wgpu::CreateInstance(&instance_desc);
    if (!instance)
        return false;

    wgpu::Adapter adapter = ImGuiApp_ImplSDL2WGPU_RequestAdapter(instance);
    if (!adapter)
        return false;
    ImGui_ImplWGPU_DebugPrintAdapterInfo(adapter.Get());

    bd->Device = ImGuiApp_ImplSDL2WGPU_RequestDevice(instance, adapter);
    if (bd->Device == nullptr)
        return false;

    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas_desc = {};
    canvas_desc.selector = bd->CanvasSelector != nullptr ? bd->CanvasSelector : "#canvas";

    wgpu::SurfaceDescriptor surface_desc = {};
    surface_desc.nextInChain = &canvas_desc;
    wgpu::Surface surface = instance.CreateSurface(&surface_desc);
    if (!surface)
        return false;

    bd->Instance = instance.MoveToCHandle();
    bd->Surface = surface.MoveToCHandle();

    WGPUSurfaceCapabilities surface_capabilities = {};
    if (wgpuSurfaceGetCapabilities(bd->Surface, adapter.Get(), &surface_capabilities) == WGPUStatus_Success)
    {
        if (surface_capabilities.formatCount > 0)
            preferred_format = surface_capabilities.formats[0];
        if (surface_capabilities.presentModeCount > 0)
            preferred_present_mode = ImGuiApp_ImplSDL2WGPU_SelectPresentMode(surface_capabilities);
        wgpuSurfaceCapabilitiesFreeMembers(surface_capabilities);
    }
#else
#error "ImGuiX SDL2 WebGPU backend currently expects IMGUI_IMPL_WEBGPU_BACKEND_DAWN."
#endif // #if defined(IMGUI_IMPL_WEBGPU_BACKEND_DAWN)

    if (preferred_format == WGPUTextureFormat_Undefined)
        preferred_format = WGPUTextureFormat_BGRA8Unorm;

    int width = 1;
    int height = 1;
    ImGuiApp_ImplSDL2WGPU_ReadCanvasSize(bd, &width, &height);

    bd->SurfaceConfiguration.presentMode = preferred_present_mode;
    bd->SurfaceConfiguration.alphaMode = WGPUCompositeAlphaMode_Auto;
    bd->SurfaceConfiguration.usage = WGPUTextureUsage_RenderAttachment;
    bd->SurfaceConfiguration.width = bd->SurfaceWidth = width;
    bd->SurfaceConfiguration.height = bd->SurfaceHeight = height;
    bd->SurfaceConfiguration.device = bd->Device;
    bd->SurfaceConfiguration.format = preferred_format;
    wgpuSurfaceConfigure(bd->Surface, &bd->SurfaceConfiguration);

    bd->Queue = wgpuDeviceGetQueue(bd->Device);
    return bd->Queue != nullptr;
}


void ImGuiApp_ImplSDL2WGPU_Shutdown(ImGuiApp* app)
{
    ImGuiApp_ImplSDL2WGPU_Data* bd = ImGuiApp_ImplSDL2WGPU_GetBackendData(app);
    IM_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
    if (bd == nullptr)
        return;

    if (bd->RendererBackendInitialized)
        ImGui_ImplWGPU_Shutdown();
    if (bd->PlatformBackendInitialized)
        ImGui_ImplSDL2_Shutdown();

    if (bd->Surface != nullptr)
    {
        wgpuSurfaceUnconfigure(bd->Surface);
        wgpuSurfaceRelease(bd->Surface);
    }
    if (bd->Queue != nullptr)
        wgpuQueueRelease(bd->Queue);
    if (bd->Device != nullptr)
        wgpuDeviceRelease(bd->Device);
    if (bd->Instance != nullptr)
        wgpuInstanceRelease(bd->Instance);

    app->BackendData = nullptr;
    IM_DELETE(bd);
}

void ImGuiApp_ImplSDL2WGPU_NewFrame(ImGuiApp* app)
{
    ImGuiApp_ImplSDL2WGPU_Data* bd = ImGuiApp_ImplSDL2WGPU_GetBackendData(app);
    IM_ASSERT(bd != nullptr && "Backend not initialized! Did you call ImGuiApp_ImplSDL2WGPU_Init()?");
    if (bd == nullptr)
        return;

    int width = 1;
    int height = 1;
    ImGuiApp_ImplSDL2WGPU_ReadCanvasSize(bd, &width, &height);
    ImGuiApp_ImplSDL2WGPU_ResizeSurface(bd, width, height);

    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL2_NewFrame();
}

// Submits and presents in one hook (no PresentFrame; legacy single-hook contract).
void ImGuiApp_ImplSDL2WGPU_RenderDrawData(ImGuiApp* app, ImDrawData* draw_data, const ImGuiAppFrameConfig* config)
{
    ImGuiApp_ImplSDL2WGPU_Data* bd = ImGuiApp_ImplSDL2WGPU_GetBackendData(app);
    IM_ASSERT(bd != nullptr && "Backend not initialized! Did you call ImGuiApp_ImplSDL2WGPU_Init()?");
    if (bd == nullptr || draw_data == nullptr || config == nullptr || bd->Surface == nullptr)
        return;

    int width = 1;
    int height = 1;
    ImGuiApp_ImplSDL2WGPU_ReadCanvasSize(bd, &width, &height);
    ImGuiApp_ImplSDL2WGPU_ResizeSurface(bd, width, height);

    WGPUSurfaceTexture surface_texture = {};
    wgpuSurfaceGetCurrentTexture(bd->Surface, &surface_texture);
    if (ImGui_ImplWGPU_IsSurfaceStatusError(surface_texture.status))
    {
        IMGUIAPP_ERROR_PRINTF("WebGPU unrecoverable surface status: %#.8x\n", surface_texture.status);
        if (surface_texture.texture != nullptr)
            wgpuTextureRelease(surface_texture.texture);
        return;
    }
    if (ImGui_ImplWGPU_IsSurfaceStatusSubOptimal(surface_texture.status))
    {
        if (surface_texture.texture != nullptr)
            wgpuTextureRelease(surface_texture.texture);
        ImGuiApp_ImplSDL2WGPU_ResizeSurface(bd, width, height);
        return;
    }

    WGPUTextureViewDescriptor view_desc = {};
    view_desc.format = bd->SurfaceConfiguration.format;
    view_desc.dimension = WGPUTextureViewDimension_2D;
    view_desc.mipLevelCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED;
    view_desc.arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED;
    view_desc.aspect = WGPUTextureAspect_All;
    WGPUTextureView texture_view = wgpuTextureCreateView(surface_texture.texture, &view_desc);
    if (texture_view == nullptr)
        return;

    WGPURenderPassColorAttachment color_attachment = {};
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_attachment.loadOp = (config->Flags & ImGuiAppFrameFlags_NoClear) ? WGPULoadOp_Load : WGPULoadOp_Clear;
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.clearValue = { config->ClearColor.x * config->ClearColor.w, config->ClearColor.y * config->ClearColor.w, config->ClearColor.z * config->ClearColor.w, config->ClearColor.w };
    color_attachment.view = texture_view;

    WGPURenderPassDescriptor render_pass_desc = {};
    render_pass_desc.colorAttachmentCount = 1;
    render_pass_desc.colorAttachments = &color_attachment;
    render_pass_desc.depthStencilAttachment = nullptr;

    WGPUCommandEncoderDescriptor encoder_desc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(bd->Device, &encoder_desc);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &render_pass_desc);
    ImGui_ImplWGPU_RenderDrawData(draw_data, pass);
    wgpuRenderPassEncoderEnd(pass);

    WGPUCommandBufferDescriptor command_buffer_desc = {};
    WGPUCommandBuffer command_buffer = wgpuCommandEncoderFinish(encoder, &command_buffer_desc);
    if ((config->Flags & ImGuiAppFrameFlags_NoPresent) == 0)
        wgpuQueueSubmit(bd->Queue, 1, &command_buffer);

    wgpuTextureViewRelease(texture_view);
    wgpuRenderPassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
    wgpuCommandBufferRelease(command_buffer);
}

bool ImGuiApp_ImplSDL2WGPU_Init(ImGuiApp* app, const ImGuiApp_ImplSDL2WGPU_InitInfo* init_info)
{
    IM_ASSERT(app != nullptr && app->BackendData == nullptr && "Already initialized a platform backend!");
    IM_ASSERT(ImGuiApp_ImplSDL2WGPU_IsInitInfoValid(init_info) && "ImGuiApp_ImplSDL2WGPU_Init: invalid init_info.");
    if (app == nullptr || app->BackendData != nullptr || !ImGuiApp_ImplSDL2WGPU_IsInitInfoValid(init_info))
        return false;

    ImGuiApp_ImplSDL2WGPU_Data* bd = IM_NEW(ImGuiApp_ImplSDL2WGPU_Data)();
    app->BackendData = bd;
    bd->Window = (SDL_Window*)init_info->Window;
    bd->CanvasSelector = init_info->CanvasSelector != nullptr ? init_info->CanvasSelector : "#canvas";

    if (!ImGuiApp_ImplSDL2WGPU_InitWGPU(bd))
    {
        ImGuiApp_ImplSDL2WGPU_Shutdown(app);
        return false;
    }

    if (!ImGui_ImplSDL2_InitForOther(bd->Window))
    {
        ImGuiApp_ImplSDL2WGPU_Shutdown(app);
        return false;
    }
    bd->PlatformBackendInitialized = true;

    ImGui_ImplWGPU_InitInfo wgpu_init_info;
    wgpu_init_info.Device = bd->Device;
    wgpu_init_info.NumFramesInFlight = 3;
    wgpu_init_info.RenderTargetFormat = bd->SurfaceConfiguration.format;
    wgpu_init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;
    if (!ImGui_ImplWGPU_Init(&wgpu_init_info))
    {
        ImGuiApp_ImplSDL2WGPU_Shutdown(app);
        return false;
    }
    bd->RendererBackendInitialized = true;
    return true;
}

bool ImGuiApp_ImplSDL2WGPU_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config)
{
    ImGuiAppPlatformData* state = IM_NEW(ImGuiAppPlatformData)();
    app->PlatformData = state;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
        return false;

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    state->Window = SDL_CreateWindow(config.WindowTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, config.WindowWidth, config.WindowHeight, window_flags);
    if (state->Window == nullptr)
    {
        SDL_Quit();
        return false;
    }

    config.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

    if (ImGui::GetCurrentContext() == nullptr)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        state->OwnsImGuiContext = true;
    }

    ImGuiApp_ImplSDL2WGPU_InitInfo init_info;
    init_info.Window         = state->Window;
    init_info.CanvasSelector = "#canvas";
    if (!ImGuiApp_ImplSDL2WGPU_Init(app, &init_info))
    {
        SDL_DestroyWindow(state->Window);
        state->Window = nullptr;
        SDL_Quit();
        return false;
    }

    ImGui::GetIO().ConfigFlags |= config.ConfigFlags;

    app->Platform.Name               = config.Platform.Name;
    app->Platform.NativeWindowHandle = state->Window;
    return true;
}

void ImGuiApp_ImplSDL2WGPU_ShutdownPlatform(ImGuiApp* app)
{
    // Graphics first (wrapped imgui backends + WGPU objects need the window alive), then the host.
    if (ImGuiApp_ImplSDL2WGPU_GetBackendData(app) != nullptr)
        ImGuiApp_ImplSDL2WGPU_Shutdown(app);

    ImGuiAppPlatformData* state = app->PlatformData;
    if (state == nullptr)
        return;
    if (state->OwnsImGuiContext)
    {
        ImGui::DestroyContext();
        state->OwnsImGuiContext = false;
    }
    if (state->Window != nullptr)
    {
        SDL_DestroyWindow(state->Window);
        state->Window = nullptr;
    }
    SDL_Quit();

    IM_DELETE(state);
    app->PlatformData = nullptr;
}

static const ImGuiAppPlatformBackend ImGuiApp_ImplSDL2WGPU_PlatformBackend =
{
    ImGuiApp_ImplSDL2WGPU_InitPlatform,
    ImGuiApp_ImplSDL2WGPU_ShutdownPlatform,
    ImGuiApp_ImplSDL2_RunLoop,
    nullptr, // CaptureFrame
    "imguiapp_impl_sdl2_wgpu",
    ImGuiApp_ImplSDL2WGPU_Shutdown,
    ImGuiApp_ImplSDL2WGPU_NewFrame,
    ImGuiApp_ImplSDL2WGPU_RenderDrawData,
    nullptr, // PresentFrame: RenderDrawData presents (legacy single-hook)
};

const ImGuiAppPlatformBackend* ImGuiAppGetPlatformBackend() { return &ImGuiApp_ImplSDL2WGPU_PlatformBackend; }


#endif // #ifndef IMGUI_DISABLE
