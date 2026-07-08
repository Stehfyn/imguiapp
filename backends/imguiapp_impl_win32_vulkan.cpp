// dear imgui app: Renderer Host for Win32 + Vulkan (composes imgui_impl_win32 + imgui_impl_vulkan)
// This needs to be used along with the Win32 Platform Host (imguiapp_impl_win32: shared WndProc + message-pump run loop).

// Implemented features:
//  [X] Renderer: exposed ImGuiApp_ImplWin32Vulkan_* frame lifecycle (imgui impl pattern), driven by ImGuiApp's frame phases.
//  [X] Platform: window/instance/device/swapchain creation + ImGui context ownership in InitPlatform/ShutdownPlatform.
//  [X] Multi-viewport: upstream vulkan viewport hooks wrapped for pacing-aware per-viewport present skip.
//  [X] AV: pipelined staging-buffer CaptureFrame (no pipeline stall; FrameID travels with the pixels).
//  [X] Headless: Offscreen render target behind a hidden input window (ImGuiAppHeadlessMode_Offscreen).

// You can use unmodified imguiapp_impl_* files in your project. See demos/ folder for examples of using this.
// Prefer including the entire imguiapp/ folder into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2026-07-08: Lifecycle: Per-host PlatformData type + exported ImGuiApp_ImplWin32Vulkan_GetPlatformBackend(); hosts coexist in one binary (build-selected binding wires the active one).
//  2026-07-08: Docs: Header block conformed to the backend anatomy (B1/B2 grammar).
//  2026-07-08: Lifecycle: Threaded ImGuiApp* through the frame lifecycle; backend data moved to app->BackendData, file-scope backend global removed; viewport hooks recover the app via the main viewport's GWLP_USERDATA.
//  2026-07-08: Misc: Exposed ImGuiApp_ImplWin32Vulkan_* frame lifecycle (imgui impl pattern); host owns the ImGui context it creates; backend-internal symbols prefixed; IMGUI_DISABLE guards added.

#include "imguiapp.h"
#ifndef IMGUI_DISABLE
#include "imguiapp_impl_win32_vulkan.h"
#include "imguiapp_impl_win32.h"
#include "imguiapp_internal.h"          // ImGuiAppAVFrame (CaptureFrame payload)

#include "imgui_impl_win32.h"

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include "imgui_impl_vulkan.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern LRESULT WINAPI ImGuiApp_ImplWin32_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam); // copied from imguiapp_impl_win32.h ('#if 0' there keeps <windows.h> out of the header)

// Private impl of the opaque app->PlatformData slot (per-host type; hosts coexist as ordinary TUs).
struct ImGuiApp_ImplWin32Vulkan_PlatformData
{
    HWND        Hwnd;
    WNDCLASSEXA WindowClass;
    bool        OwnsImGuiContext; // this host created the ImGui context (none existed)
};

struct ImGuiApp_ImplWin32Vulkan_Data
{
    ImGuiApp*                      App;               // owner; source of FrameID at capture-copy time
    HWND                           Hwnd;
    VkAllocationCallbacks*         Allocator;
    VkInstance                     Instance;
    VkPhysicalDevice               PhysicalDevice;
    VkDevice                       Device;
    uint32_t                       QueueFamily;
    VkQueue                        Queue;
    VkDebugReportCallbackEXT       DebugReport;
    VkPipelineCache                PipelineCache;
    VkDescriptorPool               DescriptorPool;
    ImGui_ImplVulkanH_Window       MainWindow;
    uint32_t                       MinImageCount;
    bool                           SwapChainRebuild;
    bool                           LastFrameRendered; // set by RenderDrawData, consumed by PresentFrame
    bool                           PlatformBackendInitialized;
    bool                           RendererBackendInitialized;
    bool                           VulkanInitialized;

    // Per-viewport pacing (secondary platform windows): the upstream vulkan viewport hooks
    // are wrapped so a paced-out viewport skips BOTH RenderWindow and SwapBuffers. Decision
    // made ONCE per viewport per frame in the RenderWindow wrapper, consumed by SwapBuffers.
    ImGuiStorage                   VpSkip;            // viewport ID -> skip present this frame
    void                         (*UnderlyingViewportRenderWindow)(ImGuiViewport*, void*);
    void                         (*UnderlyingViewportSwapBuffers)(ImGuiViewport*, void*);

    // Headless offscreen target (ImGuiAppHeadlessMode_Offscreen): renders into OffscreenImage
    // instead of a swapchain; render pass finalLayout is TRANSFER_SRC_OPTIMAL so capture copies
    // need no layout round-trip. Single command buffer + fence: headless frames run lockstep.
    bool            OffscreenActive;
    int             OffscreenWidth;
    int             OffscreenHeight;
    VkImage         OffscreenImage;
    VkDeviceMemory  OffscreenMemory;
    VkImageView     OffscreenView;
    VkRenderPass    OffscreenRenderPass;
    VkFramebuffer   OffscreenFramebuffer;
    VkCommandPool   OffscreenCommandPool;
    VkCommandBuffer OffscreenCommandBuffer;
    VkFence         OffscreenFence;

    // Frame capture (AV readback, docs/designs.md (av-design)): armed by the first CaptureFrame call;
    // ImGuiApp_ImplWin32Vulkan_FrameRender then records a copy into one of two host-visible staging buffers and
    // CaptureFrame returns the other (previous frame's) buffer -- no pipeline stall.
    bool            CaptureSupported;       // swapchain surface allows TRANSFER_SRC (always true offscreen)
    bool            CaptureArmed;
    int             CaptureWriteIndex;      // staging buffer the next recorded copy targets
    VkBuffer        CaptureBuffer[2];
    VkDeviceMemory  CaptureMemory[2];
    void*           CaptureMapped[2];
    VkDeviceSize    CaptureCapacity[2];
    int             CaptureCopyWidth[2];    // geometry/format of the copy each buffer holds; 0 = none yet
    int             CaptureCopyHeight[2];
    VkFormat        CaptureCopyFormat[2];
    ImGuiAppFrameID CaptureCopyId[2];       // FrameID at copy-record time: the pixels' TRUE identity under double-buffer latency
    VkFence         CapturePendingFence[2]; // fence of the submit containing the copy; null = data at rest
    ImU64           CaptureLastReturned;    // highest FrameID.FrameIndex handed to a caller; gates staleness/duplicates
    VkCommandPool   CaptureSyncPool;        // transient one-shot copy for the take's FIRST frame (pipeline not primed yet)
    VkCommandBuffer CaptureSyncCmd;
    VkFence         CaptureSyncFence;
    ImVector<char>  CaptureRgba;            // RGBA8 conversion buffer handed to CaptureFrame callers

    // memset ctor (B6); MainWindow's upstream ctor defaults re-seeded after the wipe.
    ImGuiApp_ImplWin32Vulkan_Data()
    {
        memset((void*)this, 0, sizeof(*this));
        MainWindow = ImGui_ImplVulkanH_Window();
    }
};

// Backend data stored in app->BackendData (the io userdata slots belong to the wrapped imgui backends).
// IM_NEW'd at Init, freed by Shutdown.
static ImGuiApp_ImplWin32Vulkan_Data* ImGuiApp_ImplWin32Vulkan_GetBackendData(ImGuiApp* app)
{
    return app != nullptr ? (ImGuiApp_ImplWin32Vulkan_Data*)app->BackendData : nullptr;
}

// Context-free viewport hooks recover the app through the main viewport's window user data
// (set at the end of InitPlatform; the same slot the shared WndProc reads for WM_TIMER repaints).
static ImGuiApp* ImGuiApp_ImplWin32Vulkan_GetApp()
{
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    if (main_viewport == nullptr || main_viewport->PlatformHandle == nullptr)
        return nullptr;
    return (ImGuiApp*)::GetWindowLongPtr((HWND)main_viewport->PlatformHandle, GWLP_USERDATA);
}

static void ImGuiApp_ImplWin32Vulkan_CheckVkResult(VkResult err)
{
    if (err == VK_SUCCESS)
        return;
    IMGUIAPP_ERROR_PRINTF("[vulkan] VkResult = %d\n", err);
    if (err < 0)
        IMGUIAPP_ABORT();
}

static bool ImGuiApp_ImplWin32Vulkan_IsInitInfoValid(const ImGuiApp_ImplWin32Vulkan_InitInfo* init_info)
{
    return init_info != nullptr && init_info->Hwnd != nullptr;
}

static uint32_t ImGuiApp_ImplWin32Vulkan_FindMemoryType(VkPhysicalDevice physical_device, uint32_t type_bits, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++)
        if ((type_bits & (1u << i)) != 0 && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    return (uint32_t)-1;
}

static void ImGuiApp_ImplWin32Vulkan_DestroyCaptureBuffers(ImGuiApp_ImplWin32Vulkan_Data* bd)
{
    if (bd == nullptr || bd->Device == VK_NULL_HANDLE)
        return;
    if (bd->CaptureSyncFence != VK_NULL_HANDLE)
    {
        vkDestroyFence(bd->Device, bd->CaptureSyncFence, bd->Allocator);
        bd->CaptureSyncFence = VK_NULL_HANDLE;
    }
    if (bd->CaptureSyncPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(bd->Device, bd->CaptureSyncPool, bd->Allocator);   // frees CaptureSyncCmd with it
        bd->CaptureSyncPool = VK_NULL_HANDLE;
        bd->CaptureSyncCmd = VK_NULL_HANDLE;
    }
    for (int i = 0; i < 2; i++)
    {
        if (bd->CapturePendingFence[i] != VK_NULL_HANDLE)
        {
            vkWaitForFences(bd->Device, 1, &bd->CapturePendingFence[i], VK_TRUE, UINT64_MAX);
            bd->CapturePendingFence[i] = VK_NULL_HANDLE;
        }
        if (bd->CaptureMapped[i] != nullptr)
        {
            vkUnmapMemory(bd->Device, bd->CaptureMemory[i]);
            bd->CaptureMapped[i] = nullptr;
        }
        if (bd->CaptureBuffer[i] != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(bd->Device, bd->CaptureBuffer[i], bd->Allocator);
            bd->CaptureBuffer[i] = VK_NULL_HANDLE;
        }
        if (bd->CaptureMemory[i] != VK_NULL_HANDLE)
        {
            vkFreeMemory(bd->Device, bd->CaptureMemory[i], bd->Allocator);
            bd->CaptureMemory[i] = VK_NULL_HANDLE;
        }
        bd->CaptureCapacity[i] = 0;
        bd->CaptureCopyWidth[i] = 0;
        bd->CaptureCopyHeight[i] = 0;
    }
}

// Persistently mapped host-visible+coherent staging buffer of at least `size` bytes.
// current_submit_fence: this frame's (already waited + reset, unsubmitted) fence -- waiting
// on it here would deadlock; equality means the old copy already retired at frame start.
static bool ImGuiApp_ImplWin32Vulkan_EnsureCaptureBuffer(ImGuiApp_ImplWin32Vulkan_Data* bd, int index, VkDeviceSize size, VkFence current_submit_fence)
{
    if (bd->CaptureBuffer[index] != VK_NULL_HANDLE && bd->CaptureCapacity[index] >= size)
        return true;

    // The old buffer may still be a transfer target of an in-flight submit.
    if (bd->CapturePendingFence[index] != VK_NULL_HANDLE)
    {
        if (bd->CapturePendingFence[index] != current_submit_fence)
            vkWaitForFences(bd->Device, 1, &bd->CapturePendingFence[index], VK_TRUE, UINT64_MAX);
        bd->CapturePendingFence[index] = VK_NULL_HANDLE;
    }
    if (bd->CaptureMapped[index] != nullptr)
    {
        vkUnmapMemory(bd->Device, bd->CaptureMemory[index]);
        bd->CaptureMapped[index] = nullptr;
    }
    if (bd->CaptureBuffer[index] != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(bd->Device, bd->CaptureBuffer[index], bd->Allocator);
        bd->CaptureBuffer[index] = VK_NULL_HANDLE;
    }
    if (bd->CaptureMemory[index] != VK_NULL_HANDLE)
    {
        vkFreeMemory(bd->Device, bd->CaptureMemory[index], bd->Allocator);
        bd->CaptureMemory[index] = VK_NULL_HANDLE;
    }
    bd->CaptureCapacity[index] = 0;
    bd->CaptureCopyWidth[index] = 0;
    bd->CaptureCopyHeight[index] = 0;

    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult err = vkCreateBuffer(bd->Device, &buffer_info, bd->Allocator, &bd->CaptureBuffer[index]);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    if (bd->CaptureBuffer[index] == VK_NULL_HANDLE)
        return false;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(bd->Device, bd->CaptureBuffer[index], &req);
    // Prefer HOST_CACHED: the CPU reads this memory back every frame, and reads from
    // uncached (write-combined) mappings are ~100x slow. Coherent+cached exists on
    // most desktop devices; plain coherent is the fallback (reads then go through the
    // bulk memcpy in CaptureFrame, never per-byte).
    uint32_t type = ImGuiApp_ImplWin32Vulkan_FindMemoryType(bd->PhysicalDevice, req.memoryTypeBits,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (type == (uint32_t)-1)
        type = ImGuiApp_ImplWin32Vulkan_FindMemoryType(bd->PhysicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type == (uint32_t)-1)
        return false;

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = req.size;
    alloc_info.memoryTypeIndex = type;
    err = vkAllocateMemory(bd->Device, &alloc_info, bd->Allocator, &bd->CaptureMemory[index]);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    if (bd->CaptureMemory[index] == VK_NULL_HANDLE)
        return false;

    err = vkBindBufferMemory(bd->Device, bd->CaptureBuffer[index], bd->CaptureMemory[index], 0);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    err = vkMapMemory(bd->Device, bd->CaptureMemory[index], 0, VK_WHOLE_SIZE, 0, &bd->CaptureMapped[index]);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    if (bd->CaptureMapped[index] == nullptr)
        return false;

    bd->CaptureCapacity[index] = size;
    return true;
}

// Records image -> staging copy into the frame's command buffer (before vkEndCommandBuffer).
// `layout` is the image's layout at this point in the command buffer; it is restored after.
static void ImGuiApp_ImplWin32Vulkan_RecordCaptureCopy(ImGuiApp_ImplWin32Vulkan_Data* bd, VkCommandBuffer cb, VkImage image, int width, int height, VkFormat format, VkImageLayout layout, VkFence submit_fence)
{
    if (!bd->CaptureArmed || width <= 0 || height <= 0)
        return;

    const int w = bd->CaptureWriteIndex;
    if (!ImGuiApp_ImplWin32Vulkan_EnsureCaptureBuffer(bd, w, (VkDeviceSize)width * (VkDeviceSize)height * 4, submit_fence))
        return;

    // The staging buffer's previous copy (2 frames ago) must have retired before it is
    // rewritten. Equality with this frame's fence means that submission was already waited
    // at frame start (the fence sits reset and unsubmitted -- waiting would deadlock).
    if (bd->CapturePendingFence[w] != VK_NULL_HANDLE)
    {
        if (bd->CapturePendingFence[w] != submit_fence)
            vkWaitForFences(bd->Device, 1, &bd->CapturePendingFence[w], VK_TRUE, UINT64_MAX);
        bd->CapturePendingFence[w] = VK_NULL_HANDLE;
    }

    // Always issued: even without a layout change (offscreen), the copy needs the
    // COLOR_ATTACHMENT_WRITE -> TRANSFER_READ memory dependency.
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = layout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (uint32_t)width;
    region.imageExtent.height = (uint32_t)height;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, bd->CaptureBuffer[w], 1, &region);

    if (layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = layout;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    bd->CaptureCopyWidth[w] = width;
    bd->CaptureCopyHeight[w] = height;
    bd->CaptureCopyFormat[w] = format;
    bd->CaptureCopyId[w] = bd->App != nullptr ? bd->App->FrameID : ImGuiAppFrameID();
    bd->CapturePendingFence[w] = submit_fence;
    bd->CaptureWriteIndex = w ^ 1;
}

static void ImGuiApp_ImplWin32Vulkan_DestroyOffscreenTarget(ImGuiApp_ImplWin32Vulkan_Data* bd)
{
    if (bd == nullptr || bd->Device == VK_NULL_HANDLE)
        return;
    if (bd->OffscreenFence != VK_NULL_HANDLE)
    {
        vkWaitForFences(bd->Device, 1, &bd->OffscreenFence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(bd->Device, bd->OffscreenFence, bd->Allocator);
        bd->OffscreenFence = VK_NULL_HANDLE;
    }
    if (bd->OffscreenCommandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(bd->Device, bd->OffscreenCommandPool, bd->Allocator);
        bd->OffscreenCommandPool = VK_NULL_HANDLE;
        bd->OffscreenCommandBuffer = VK_NULL_HANDLE;
    }
    if (bd->OffscreenFramebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(bd->Device, bd->OffscreenFramebuffer, bd->Allocator);
        bd->OffscreenFramebuffer = VK_NULL_HANDLE;
    }
    if (bd->OffscreenRenderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(bd->Device, bd->OffscreenRenderPass, bd->Allocator);
        bd->OffscreenRenderPass = VK_NULL_HANDLE;
    }
    if (bd->OffscreenView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(bd->Device, bd->OffscreenView, bd->Allocator);
        bd->OffscreenView = VK_NULL_HANDLE;
    }
    if (bd->OffscreenImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(bd->Device, bd->OffscreenImage, bd->Allocator);
        bd->OffscreenImage = VK_NULL_HANDLE;
    }
    if (bd->OffscreenMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(bd->Device, bd->OffscreenMemory, bd->Allocator);
        bd->OffscreenMemory = VK_NULL_HANDLE;
    }
    bd->OffscreenActive = false;
}

// Fixed-size RGBA8 render target. finalLayout is TRANSFER_SRC_OPTIMAL: the capture copy
// then needs no layout round-trip, and nothing ever presents this image.
static bool ImGuiApp_ImplWin32Vulkan_CreateOffscreenTarget(ImGuiApp_ImplWin32Vulkan_Data* bd, int width, int height)
{
    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkResult err;

    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent.width = (uint32_t)width;
    image_info.extent.height = (uint32_t)height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    err = vkCreateImage(bd->Device, &image_info, bd->Allocator, &bd->OffscreenImage);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    if (bd->OffscreenImage == VK_NULL_HANDLE)
        return false;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(bd->Device, bd->OffscreenImage, &req);
    const uint32_t type = ImGuiApp_ImplWin32Vulkan_FindMemoryType(bd->PhysicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == (uint32_t)-1)
        return false;

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = req.size;
    alloc_info.memoryTypeIndex = type;
    err = vkAllocateMemory(bd->Device, &alloc_info, bd->Allocator, &bd->OffscreenMemory);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    if (bd->OffscreenMemory == VK_NULL_HANDLE)
        return false;
    err = vkBindImageMemory(bd->Device, bd->OffscreenImage, bd->OffscreenMemory, 0);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = bd->OffscreenImage;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    err = vkCreateImageView(bd->Device, &view_info, bd->Allocator, &bd->OffscreenView);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    if (bd->OffscreenView == VK_NULL_HANDLE)
        return false;

    VkAttachmentDescription attachment = {};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference color_ref = {};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo pass_info = {};
    pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass_info.attachmentCount = 1;
    pass_info.pAttachments = &attachment;
    pass_info.subpassCount = 1;
    pass_info.pSubpasses = &subpass;
    pass_info.dependencyCount = 1;
    pass_info.pDependencies = &dependency;
    err = vkCreateRenderPass(bd->Device, &pass_info, bd->Allocator, &bd->OffscreenRenderPass);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    if (bd->OffscreenRenderPass == VK_NULL_HANDLE)
        return false;

    VkFramebufferCreateInfo fb_info = {};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = bd->OffscreenRenderPass;
    fb_info.attachmentCount = 1;
    fb_info.pAttachments = &bd->OffscreenView;
    fb_info.width = (uint32_t)width;
    fb_info.height = (uint32_t)height;
    fb_info.layers = 1;
    err = vkCreateFramebuffer(bd->Device, &fb_info, bd->Allocator, &bd->OffscreenFramebuffer);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    if (bd->OffscreenFramebuffer == VK_NULL_HANDLE)
        return false;

    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = bd->QueueFamily;
    err = vkCreateCommandPool(bd->Device, &pool_info, bd->Allocator, &bd->OffscreenCommandPool);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    if (bd->OffscreenCommandPool == VK_NULL_HANDLE)
        return false;

    VkCommandBufferAllocateInfo cb_info = {};
    cb_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cb_info.commandPool = bd->OffscreenCommandPool;
    cb_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_info.commandBufferCount = 1;
    err = vkAllocateCommandBuffers(bd->Device, &cb_info, &bd->OffscreenCommandBuffer);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    err = vkCreateFence(bd->Device, &fence_info, bd->Allocator, &bd->OffscreenFence);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    if (bd->OffscreenFence == VK_NULL_HANDLE)
        return false;

    bd->OffscreenWidth = width;
    bd->OffscreenHeight = height;
    bd->OffscreenActive = true;
    bd->CaptureSupported = true;   // our image always carries TRANSFER_SRC
    return true;
}

// Lockstep offscreen frame: wait fence -> record -> submit. No acquire, no semaphores,
// no present; the render pass leaves the image in TRANSFER_SRC for capture.
static void ImGuiApp_ImplWin32Vulkan_FrameRenderOffscreen(ImGuiApp_ImplWin32Vulkan_Data* bd, ImDrawData* draw_data, const VkClearValue* clear_value)
{
    VkResult err = vkWaitForFences(bd->Device, 1, &bd->OffscreenFence, VK_TRUE, UINT64_MAX);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    err = vkResetFences(bd->Device, 1, &bd->OffscreenFence);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);

    err = vkResetCommandBuffer(bd->OffscreenCommandBuffer, 0);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    err = vkBeginCommandBuffer(bd->OffscreenCommandBuffer, &begin_info);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);

    VkRenderPassBeginInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = bd->OffscreenRenderPass;
    render_pass_info.framebuffer = bd->OffscreenFramebuffer;
    render_pass_info.renderArea.extent.width = (uint32_t)bd->OffscreenWidth;
    render_pass_info.renderArea.extent.height = (uint32_t)bd->OffscreenHeight;
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = clear_value;
    vkCmdBeginRenderPass(bd->OffscreenCommandBuffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    ImGui_ImplVulkan_RenderDrawData(draw_data, bd->OffscreenCommandBuffer);

    vkCmdEndRenderPass(bd->OffscreenCommandBuffer);

    if (bd->CaptureArmed)
        ImGuiApp_ImplWin32Vulkan_RecordCaptureCopy(bd, bd->OffscreenCommandBuffer, bd->OffscreenImage, bd->OffscreenWidth, bd->OffscreenHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, bd->OffscreenFence);

    err = vkEndCommandBuffer(bd->OffscreenCommandBuffer);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &bd->OffscreenCommandBuffer;
    err = vkQueueSubmit(bd->Queue, 1, &submit_info, bd->OffscreenFence);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
}

static bool ImGuiApp_ImplWin32Vulkan_IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension)
{
    for (const VkExtensionProperties& property : properties)
        if (std::strcmp(property.extensionName, extension) == 0)
            return true;
    return false;
}

static bool ImGuiApp_ImplWin32Vulkan_IsLayerAvailable(const ImVector<VkLayerProperties>& properties, const char* layer)
{
    for (const VkLayerProperties& property : properties)
        if (std::strcmp(property.layerName, layer) == 0)
            return true;
    return false;
}

static void ImGuiApp_ImplWin32Vulkan_GetClientSize(HWND hwnd, int* width, int* height)
{
    IM_ASSERT(width != nullptr);
    IM_ASSERT(height != nullptr);

    RECT rect = {};
    if (hwnd != nullptr && ::GetClientRect(hwnd, &rect))
    {
        *width = rect.right - rect.left;
        *height = rect.bottom - rect.top;
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    *width = (int)io.DisplaySize.x;
    *height = (int)io.DisplaySize.y;
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugReportCallback(
    VkDebugReportFlagsEXT flags,
    VkDebugReportObjectTypeEXT object_type,
    uint64_t object,
    size_t location,
    int32_t message_code,
    const char* layer_prefix,
    const char* message,
    void* user_data)
{
    IM_UNUSED(flags);
    IM_UNUSED(object);
    IM_UNUSED(location);
    IM_UNUSED(message_code);
    IM_UNUSED(user_data);
    IM_UNUSED(layer_prefix);
    IMGUIAPP_ERROR_PRINTF("[vulkan] Debug report from object type %d: %s\n", object_type, message);
    return VK_FALSE;
}

static bool ImGuiApp_ImplWin32Vulkan_SetupVulkan(ImGuiApp_ImplWin32Vulkan_Data* bd, ImVector<const char*> instance_extensions, bool enable_validation)
{
    IM_ASSERT(bd != nullptr);
    if (bd == nullptr)
        return false;

    VkResult err;

    uint32_t properties_count = 0;
    err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    ImVector<VkExtensionProperties> properties;
    properties.resize(properties_count);
    if (properties_count > 0)
    {
        err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
        ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    }

    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "ImGuiX";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "ImGuiX";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;
    instance_info.pApplicationInfo = &app_info;

    if (ImGuiApp_ImplWin32Vulkan_IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    if (ImGuiApp_ImplWin32Vulkan_IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
    {
        instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instance_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif

    const char* validation_layer = "VK_LAYER_KHRONOS_validation";
    bool validation_enabled = false;
    if (enable_validation)
    {
        uint32_t layer_count = 0;
        err = vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
        ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
        ImVector<VkLayerProperties> layers;
        layers.resize(layer_count);
        if (layer_count > 0)
        {
            err = vkEnumerateInstanceLayerProperties(&layer_count, layers.Data);
            ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
        }

        validation_enabled = ImGuiApp_ImplWin32Vulkan_IsLayerAvailable(layers, validation_layer) &&
                             ImGuiApp_ImplWin32Vulkan_IsExtensionAvailable(properties, VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        if (validation_enabled)
            instance_extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    }

    instance_info.enabledExtensionCount = (uint32_t)instance_extensions.Size;
    instance_info.ppEnabledExtensionNames = instance_extensions.Data;
    if (validation_enabled)
    {
        instance_info.enabledLayerCount = 1;
        instance_info.ppEnabledLayerNames = &validation_layer;
    }

    err = vkCreateInstance(&instance_info, bd->Allocator, &bd->Instance);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    if (bd->Instance == VK_NULL_HANDLE)
        return false;

    if (validation_enabled)
    {
        auto create_debug_report = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(bd->Instance, "vkCreateDebugReportCallbackEXT");
        if (create_debug_report != nullptr)
        {
            VkDebugReportCallbackCreateInfoEXT debug_info = {};
            debug_info.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
            debug_info.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
            debug_info.pfnCallback = DebugReportCallback;
            err = create_debug_report(bd->Instance, &debug_info, bd->Allocator, &bd->DebugReport);
            ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
        }
    }

    bd->PhysicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(bd->Instance);
    if (bd->PhysicalDevice == VK_NULL_HANDLE)
        return false;

    bd->QueueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(bd->PhysicalDevice);
    if (bd->QueueFamily == (uint32_t)-1)
        return false;

    ImVector<const char*> device_extensions;
    device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    uint32_t device_properties_count = 0;
    err = vkEnumerateDeviceExtensionProperties(bd->PhysicalDevice, nullptr, &device_properties_count, nullptr);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    ImVector<VkExtensionProperties> device_properties;
    device_properties.resize(device_properties_count);
    if (device_properties_count > 0)
    {
        err = vkEnumerateDeviceExtensionProperties(bd->PhysicalDevice, nullptr, &device_properties_count, device_properties.Data);
        ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    }
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    if (ImGuiApp_ImplWin32Vulkan_IsExtensionAvailable(device_properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
        device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

    const float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = bd->QueueFamily;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo device_info = {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = (uint32_t)device_extensions.Size;
    device_info.ppEnabledExtensionNames = device_extensions.Data;

    err = vkCreateDevice(bd->PhysicalDevice, &device_info, bd->Allocator, &bd->Device);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    if (bd->Device == VK_NULL_HANDLE)
        return false;
    vkGetDeviceQueue(bd->Device, bd->QueueFamily, 0, &bd->Queue);

    VkDescriptorPoolSize pool_sizes[] =
    {
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE },
        { VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE },
    };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 0;
    for (const VkDescriptorPoolSize& pool_size : pool_sizes)
        pool_info.maxSets += pool_size.descriptorCount;
    pool_info.poolSizeCount = (uint32_t)IM_COUNTOF(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    err = vkCreateDescriptorPool(bd->Device, &pool_info, bd->Allocator, &bd->DescriptorPool);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    if (bd->DescriptorPool == VK_NULL_HANDLE)
        return false;

    bd->VulkanInitialized = true;
    return true;
}

static bool ImGuiApp_ImplWin32Vulkan_SetupVulkanWindow(ImGuiApp_ImplWin32Vulkan_Data* bd, ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height)
{
    IM_ASSERT(bd != nullptr);
    IM_ASSERT(wd != nullptr);
    if (bd == nullptr || wd == nullptr || surface == VK_NULL_HANDLE || width <= 0 || height <= 0)
        return false;

    VkBool32 supported = VK_FALSE;
    VkResult err = vkGetPhysicalDeviceSurfaceSupportKHR(bd->PhysicalDevice, bd->QueueFamily, surface, &supported);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    if (supported != VK_TRUE)
    {
        IMGUIAPP_ERROR_PRINTF("[vulkan] selected physical device has no WSI support.\n");
        return false;
    }

    const VkFormat request_formats[] =
    {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8_UNORM,
        VK_FORMAT_R8G8B8_UNORM,
    };
    const VkColorSpaceKHR request_color_space = VK_COLORSPACE_SRGB_NONLINEAR_KHR;

    wd->Surface = surface;
    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        bd->PhysicalDevice,
        wd->Surface,
        request_formats,
        (int)IM_COUNTOF(request_formats),
        request_color_space);

    // Immediate present: the pacer owns frame timing. FIFO only as the spec-mandated fallback.
    VkPresentModeKHR present_modes[] =
    {
        VK_PRESENT_MODE_IMMEDIATE_KHR,
        VK_PRESENT_MODE_FIFO_KHR,
    };
    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
        bd->PhysicalDevice,
        wd->Surface,
        present_modes,
        IM_COUNTOF(present_modes));

    // Frame capture copies straight from the swapchain image, which requires TRANSFER_SRC
    // usage the surface must support (universally true on desktop; capture degrades to
    // unavailable rather than failing the swapchain when it is not).
    VkSurfaceCapabilitiesKHR surface_caps = {};
    err = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(bd->PhysicalDevice, wd->Surface, &surface_caps);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    bd->CaptureSupported = (surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;

    IM_ASSERT(bd->MinImageCount >= 3);
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        bd->Instance,
        bd->PhysicalDevice,
        bd->Device,
        wd,
        bd->QueueFamily,
        bd->Allocator,
        width,
        height,
        bd->MinImageCount,
        bd->CaptureSupported ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    return true;
}

static void ImGuiApp_ImplWin32Vulkan_CleanupVulkanWindow(ImGuiApp_ImplWin32Vulkan_Data* bd, ImGui_ImplVulkanH_Window* wd)
{
    if (bd == nullptr || wd == nullptr || bd->Instance == VK_NULL_HANDLE || bd->Device == VK_NULL_HANDLE)
        return;

    VkSurfaceKHR surface = wd->Surface;
    ImGui_ImplVulkanH_DestroyWindow(bd->Instance, bd->Device, wd, bd->Allocator);
    if (surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(bd->Instance, surface, bd->Allocator);
    *wd = ImGui_ImplVulkanH_Window();
}

static void ImGuiApp_ImplWin32Vulkan_CleanupVulkan(ImGuiApp_ImplWin32Vulkan_Data* bd)
{
    if (bd == nullptr)
        return;

    if (bd->Device != VK_NULL_HANDLE && bd->DescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(bd->Device, bd->DescriptorPool, bd->Allocator);
        bd->DescriptorPool = VK_NULL_HANDLE;
    }

    if (bd->Instance != VK_NULL_HANDLE && bd->DebugReport != VK_NULL_HANDLE)
    {
        auto destroy_debug_report = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(bd->Instance, "vkDestroyDebugReportCallbackEXT");
        if (destroy_debug_report != nullptr)
            destroy_debug_report(bd->Instance, bd->DebugReport, bd->Allocator);
        bd->DebugReport = VK_NULL_HANDLE;
    }

    if (bd->Device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(bd->Device, bd->Allocator);
        bd->Device = VK_NULL_HANDLE;
    }

    if (bd->Instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(bd->Instance, bd->Allocator);
        bd->Instance = VK_NULL_HANDLE;
    }

    bd->PhysicalDevice = VK_NULL_HANDLE;
    bd->QueueFamily = (uint32_t)-1;
    bd->Queue = VK_NULL_HANDLE;
    bd->PipelineCache = VK_NULL_HANDLE;
    bd->VulkanInitialized = false;
}

static void ImGuiApp_ImplWin32Vulkan_ResizeMainWindowIfNeeded(ImGuiApp_ImplWin32Vulkan_Data* bd)
{
    if (bd == nullptr || bd->Device == VK_NULL_HANDLE || bd->OffscreenActive || bd->MainWindow.Surface == VK_NULL_HANDLE)
        return;

    int width = 0;
    int height = 0;
    ImGuiApp_ImplWin32Vulkan_GetClientSize(bd->Hwnd, &width, &height);
    if (width <= 0 || height <= 0)
        return;

    if (!bd->SwapChainRebuild && bd->MainWindow.Width == width && bd->MainWindow.Height == height)
        return;

    vkDeviceWaitIdle(bd->Device);
    ImGui_ImplVulkan_SetMinImageCount(bd->MinImageCount);
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        bd->Instance,
        bd->PhysicalDevice,
        bd->Device,
        &bd->MainWindow,
        bd->QueueFamily,
        bd->Allocator,
        width,
        height,
        bd->MinImageCount,
        bd->CaptureSupported ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    bd->MainWindow.FrameIndex = 0;
    bd->SwapChainRebuild = false;
}

static void ImGuiApp_ImplWin32Vulkan_FrameRender(ImGuiApp_ImplWin32Vulkan_Data* bd, ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data)
{
    if (bd == nullptr || wd == nullptr || wd->Swapchain == VK_NULL_HANDLE)
        return;

    VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkResult err = vkAcquireNextImageKHR(bd->Device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        bd->SwapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
        return;
    if (err != VK_SUBOPTIMAL_KHR)
        ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
    err = vkWaitForFences(bd->Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    err = vkResetFences(bd->Device, 1, &fd->Fence);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);

    err = vkResetCommandPool(bd->Device, fd->CommandPool, 0);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    err = vkBeginCommandBuffer(fd->CommandBuffer, &begin_info);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);

    VkRenderPassBeginInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = wd->RenderPass;
    render_pass_info.framebuffer = fd->Framebuffer;
    render_pass_info.renderArea.extent.width = wd->Width;
    render_pass_info.renderArea.extent.height = wd->Height;
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &wd->ClearValue;
    vkCmdBeginRenderPass(fd->CommandBuffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

    vkCmdEndRenderPass(fd->CommandBuffer);

    // The render pass left the backbuffer in PRESENT_SRC; capture rides the same
    // command buffer and fence, so it adds no extra submit or synchronization.
    if (bd->CaptureArmed && bd->CaptureSupported)
        ImGuiApp_ImplWin32Vulkan_RecordCaptureCopy(bd, fd->CommandBuffer, fd->Backbuffer, wd->Width, wd->Height, wd->SurfaceFormat.format, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, fd->Fence);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_acquired_semaphore;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &fd->CommandBuffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_complete_semaphore;

    err = vkEndCommandBuffer(fd->CommandBuffer);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    err = vkQueueSubmit(bd->Queue, 1, &submit_info, fd->Fence);
    ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
}

static void ImGuiApp_ImplWin32Vulkan_FramePresent(ImGuiApp_ImplWin32Vulkan_Data* bd, ImGui_ImplVulkanH_Window* wd)
{
    if (bd == nullptr || wd == nullptr || bd->SwapChainRebuild || wd->Swapchain == VK_NULL_HANDLE)
        return;

    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_complete_semaphore;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &wd->Swapchain;
    present_info.pImageIndices = &wd->FrameIndex;

    VkResult err = vkQueuePresentKHR(bd->Queue, &present_info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        bd->SwapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
        return;
    if (err != VK_SUBOPTIMAL_KHR)
        ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;
}

static int ImGuiApp_ImplWin32Vulkan_CreateVkSurfaceForViewport(ImGuiViewport* viewport, ImU64 vk_instance, const void* vk_allocator, ImU64* out_vk_surface)
{
    VkWin32SurfaceCreateInfoKHR create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    create_info.hwnd = (HWND)viewport->PlatformHandleRaw;
    create_info.hinstance = ::GetModuleHandle(nullptr);
    return (int)vkCreateWin32SurfaceKHR(
        (VkInstance)vk_instance,
        &create_info,
        (const VkAllocationCallbacks*)vk_allocator,
        (VkSurfaceKHR*)out_vk_surface);
}


void ImGuiApp_ImplWin32Vulkan_Shutdown(ImGuiApp* app)
{
    ImGuiApp_ImplWin32Vulkan_Data* bd = ImGuiApp_ImplWin32Vulkan_GetBackendData(app);
    IM_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
    if (bd == nullptr)
        return;

    if (bd->Device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(bd->Device);

    if (bd->RendererBackendInitialized)
        ImGui_ImplVulkan_Shutdown();
    if (bd->PlatformBackendInitialized)
        ImGui_ImplWin32_Shutdown();

    ImGuiApp_ImplWin32Vulkan_DestroyCaptureBuffers(bd);
    ImGuiApp_ImplWin32Vulkan_DestroyOffscreenTarget(bd);
    ImGuiApp_ImplWin32Vulkan_CleanupVulkanWindow(bd, &bd->MainWindow);
    ImGuiApp_ImplWin32Vulkan_CleanupVulkan(bd);

    app->BackendData = nullptr;
    IM_DELETE(bd);
}

void ImGuiApp_ImplWin32Vulkan_NewFrame(ImGuiApp* app)
{
    ImGuiApp_ImplWin32Vulkan_Data* bd = ImGuiApp_ImplWin32Vulkan_GetBackendData(app);
    IM_ASSERT(bd != nullptr && "App or backend not initialized! Did you call ImGuiApp_ImplWin32Vulkan_Init()?");
    if (bd == nullptr)
        return;

    ImGuiApp_ImplWin32Vulkan_ResizeMainWindowIfNeeded(bd);
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();
}

void ImGuiApp_ImplWin32Vulkan_RenderDrawData(ImGuiApp* app, ImDrawData* draw_data, const ImGuiAppFrameConfig* config)
{
    ImGuiApp_ImplWin32Vulkan_Data* bd = ImGuiApp_ImplWin32Vulkan_GetBackendData(app);
    IM_ASSERT(bd != nullptr && "App or backend not initialized! Did you call ImGuiApp_ImplWin32Vulkan_Init()?");
    if (bd == nullptr || draw_data == nullptr || config == nullptr)
        return;

    ImGuiApp_ImplWin32Vulkan_ResizeMainWindowIfNeeded(bd);

    const bool minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
    bd->MainWindow.ClearValue.color.float32[0] = config->ClearColor.x * config->ClearColor.w;
    bd->MainWindow.ClearValue.color.float32[1] = config->ClearColor.y * config->ClearColor.w;
    bd->MainWindow.ClearValue.color.float32[2] = config->ClearColor.z * config->ClearColor.w;
    bd->MainWindow.ClearValue.color.float32[3] = config->ClearColor.w;

    if (bd->OffscreenActive)
    {
        if (!minimized)
            ImGuiApp_ImplWin32Vulkan_FrameRenderOffscreen(bd, draw_data, &bd->MainWindow.ClearValue);
        bd->LastFrameRendered = false;   // no swapchain: PresentFrame must no-op
        return;
    }

    if (!minimized)
        ImGuiApp_ImplWin32Vulkan_FrameRender(bd, &bd->MainWindow, draw_data);

    if ((config->Flags & ImGuiAppFrameFlags_NoPlatformWindows) == 0 &&
        (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    bd->LastFrameRendered = !minimized;
}

// Present phase: the encode phase runs between RenderDrawData and this, reading
// back the frame just rendered before it goes on screen.
void ImGuiApp_ImplWin32Vulkan_PresentFrame(ImGuiApp* app, const ImGuiAppFrameConfig* config)
{
    ImGuiApp_ImplWin32Vulkan_Data* bd = ImGuiApp_ImplWin32Vulkan_GetBackendData(app);
    if (bd == nullptr || config == nullptr)
        return;
    if (bd->LastFrameRendered && (config->Flags & ImGuiAppFrameFlags_NoPresent) == 0)
        ImGuiApp_ImplWin32Vulkan_FramePresent(bd, &bd->MainWindow);
    bd->LastFrameRendered = false;
}

//--------------------------------------------------------------------------------------------------------
// MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
// This is an _advanced_ and _optional_ feature, allowing the backend to create and handle multiple viewports simultaneously.
// If you are new to dear imgui or creating a new binding for dear imgui, it is recommended that you completely ignore this section first..
// Per-viewport hooks installed into ImGuiPlatformIO; they run as context-free callbacks and reach
// backend state through the GetBackendData accessor.
//--------------------------------------------------------------------------------------------------------

// Per-viewport pacing wrappers (secondary platform windows). Installed into platform_io,
// so they run as context-free callbacks and reach backend state through the GetBackendData
// accessor. The deadline chain advances once per viewport per frame in RenderWindow; the
// skip decision is cached in VpSkip and consumed by SwapBuffers.
static void ImGuiApp_ImplWin32Vulkan_Viewport_RenderWindow(ImGuiViewport* viewport, void* render_arg)
{
    ImGuiApp_ImplWin32Vulkan_Data* bd = ImGuiApp_ImplWin32Vulkan_GetBackendData(ImGuiApp_ImplWin32Vulkan_GetApp());
    if (bd == nullptr)
        return;
    const bool present = ImGui::AppPacerViewportShouldPresent(bd->App, viewport);
    bd->VpSkip.SetBool(viewport->ID, !present);
    if (present && bd->UnderlyingViewportRenderWindow != nullptr)
        bd->UnderlyingViewportRenderWindow(viewport, render_arg);
}

static void ImGuiApp_ImplWin32Vulkan_Viewport_SwapBuffers(ImGuiViewport* viewport, void* render_arg)
{
    ImGuiApp_ImplWin32Vulkan_Data* bd = ImGuiApp_ImplWin32Vulkan_GetBackendData(ImGuiApp_ImplWin32Vulkan_GetApp());
    if (bd == nullptr || bd->VpSkip.GetBool(viewport->ID))
        return;
    if (bd->UnderlyingViewportSwapBuffers != nullptr)
        bd->UnderlyingViewportSwapBuffers(viewport, render_arg);
}

// Registration only: teardown rides the wrapped backends' Shutdown (they call
// platform_io.ClearPlatformHandlers/ClearRendererHandlers). Wraps the upstream vulkan
// viewport hooks so a paced-out viewport skips BOTH RenderWindow (acquire + submit) and
// SwapBuffers (present) -- the same both-skipped shape upstream takes on
// VK_ERROR_OUT_OF_DATE_KHR, so SemaphoreIndex and per-frame state stay consistent.
// Presenting without its paired acquire/submit would not be safe; the pair is decided
// once in the RenderWindow wrapper.
static void ImGuiApp_ImplWin32Vulkan_InitMultiViewportSupport(ImGuiApp_ImplWin32Vulkan_Data* bd)
{
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    bd->UnderlyingViewportRenderWindow = platform_io.Renderer_RenderWindow;
    bd->UnderlyingViewportSwapBuffers  = platform_io.Renderer_SwapBuffers;
    if (bd->UnderlyingViewportRenderWindow != nullptr && bd->UnderlyingViewportSwapBuffers != nullptr)
    {
        platform_io.Renderer_RenderWindow = ImGuiApp_ImplWin32Vulkan_Viewport_RenderWindow;
        platform_io.Renderer_SwapBuffers  = ImGuiApp_ImplWin32Vulkan_Viewport_SwapBuffers;
    }
}

bool ImGuiApp_ImplWin32Vulkan_Init(ImGuiApp* app, const ImGuiApp_ImplWin32Vulkan_InitInfo* init_info)
{
    IMGUI_CHECKVERSION();
    IM_ASSERT(app != nullptr && app->BackendData == nullptr && "Already initialized a platform backend!");
    IM_ASSERT(ImGuiApp_ImplWin32Vulkan_IsInitInfoValid(init_info) && "ImGuiApp_ImplWin32Vulkan_Init: invalid init_info.");
    if (app == nullptr || app->BackendData != nullptr || !ImGuiApp_ImplWin32Vulkan_IsInitInfoValid(init_info))
        return false;

    ImGuiApp_ImplWin32Vulkan_Data* bd = IM_NEW(ImGuiApp_ImplWin32Vulkan_Data)();
    app->BackendData = bd;
    bd->App = app;
    bd->Hwnd = (HWND)init_info->Hwnd;
    bd->MinImageCount = init_info->MinImageCount >= 3 ? init_info->MinImageCount : 3;

    ImVector<const char*> extensions;
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    if (!ImGuiApp_ImplWin32Vulkan_SetupVulkan(bd, extensions, init_info->EnableValidation))
    {
        ImGuiApp_ImplWin32Vulkan_Shutdown(app);
        return false;
    }

    const bool offscreen = init_info->Headless == ImGuiAppHeadlessMode_Offscreen;

    int width = 0;
    int height = 0;
    if (offscreen)
    {
        width = init_info->OffscreenWidth;
        height = init_info->OffscreenHeight;
    }
    else
    {
        ImGuiApp_ImplWin32Vulkan_GetClientSize(bd->Hwnd, &width, &height);
    }
    if (width <= 0 || height <= 0)
    {
        ImGuiApp_ImplWin32Vulkan_Shutdown(app);
        return false;
    }

    if (offscreen)
    {
        if (!ImGuiApp_ImplWin32Vulkan_CreateOffscreenTarget(bd, width, height))
        {
            ImGuiApp_ImplWin32Vulkan_Shutdown(app);
            return false;
        }
    }
    else
    {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkWin32SurfaceCreateInfoKHR surface_info = {};
        surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surface_info.hwnd = bd->Hwnd;
        surface_info.hinstance = ::GetModuleHandle(nullptr);
        VkResult err = vkCreateWin32SurfaceKHR(bd->Instance, &surface_info, bd->Allocator, &surface);
        ImGuiApp_ImplWin32Vulkan_CheckVkResult(err);
        if (surface == VK_NULL_HANDLE || !ImGuiApp_ImplWin32Vulkan_SetupVulkanWindow(bd, &bd->MainWindow, surface, width, height))
        {
            if (surface != VK_NULL_HANDLE)
                vkDestroySurfaceKHR(bd->Instance, surface, bd->Allocator);
            ImGuiApp_ImplWin32Vulkan_Shutdown(app);
            return false;
        }
    }

    if (!ImGui_ImplWin32_Init(bd->Hwnd))
    {
        ImGuiApp_ImplWin32Vulkan_Shutdown(app);
        return false;
    }
    bd->PlatformBackendInitialized = true;
    ImGui::GetPlatformIO().Platform_CreateVkSurface = ImGuiApp_ImplWin32Vulkan_CreateVkSurfaceForViewport;

    ImGui_ImplVulkan_InitInfo vulkan_init_info = {};
    vulkan_init_info.Instance = bd->Instance;
    vulkan_init_info.PhysicalDevice = bd->PhysicalDevice;
    vulkan_init_info.Device = bd->Device;
    vulkan_init_info.QueueFamily = bd->QueueFamily;
    vulkan_init_info.Queue = bd->Queue;
    vulkan_init_info.PipelineCache = bd->PipelineCache;
    vulkan_init_info.DescriptorPool = bd->DescriptorPool;
    vulkan_init_info.MinImageCount = offscreen ? 2 : bd->MinImageCount;
    vulkan_init_info.ImageCount = offscreen ? 2 : bd->MainWindow.ImageCount;
    vulkan_init_info.Allocator = bd->Allocator;
    vulkan_init_info.PipelineInfoMain.RenderPass = offscreen ? bd->OffscreenRenderPass : bd->MainWindow.RenderPass;
    vulkan_init_info.PipelineInfoMain.Subpass = 0;
    vulkan_init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    vulkan_init_info.CheckVkResultFn = ImGuiApp_ImplWin32Vulkan_CheckVkResult;

    if (!ImGui_ImplVulkan_Init(&vulkan_init_info))
    {
        ImGuiApp_ImplWin32Vulkan_Shutdown(app);
        return false;
    }
    bd->RendererBackendInitialized = true;

    ImGuiApp_ImplWin32Vulkan_InitMultiViewportSupport(bd);
    return true;
}

bool ImGuiApp_ImplWin32Vulkan_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config)
{
    ImGuiApp_ImplWin32Vulkan_PlatformData* state = IM_NEW(ImGuiApp_ImplWin32Vulkan_PlatformData)();
    app->PlatformData = state;

    // Offscreen headless keeps a HIDDEN window: ImGui_ImplWin32 needs an HWND for input/DPI,
    // and the test engine synthesizes inputs through it. Nothing renders to it.
    const bool offscreen = config.Headless == ImGuiAppHeadlessMode_Offscreen;
    IM_ASSERT(config.Headless != ImGuiAppHeadlessMode_Null && "Null headless mode has no renderer; use the test engine's null backend or Offscreen.");

    ImGui_ImplWin32_EnableDpiAwareness();
    const float main_scale    = offscreen ? 1.0f : ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    const int   window_width  = (int)(config.WindowWidth  * main_scale);
    const int   window_height = (int)(config.WindowHeight * main_scale);
    config.DpiScale    = main_scale;
    config.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_DockingEnable;
    if (!offscreen)
        config.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // offscreen has no OS windows to spawn

    HINSTANCE instance = ::GetModuleHandle(nullptr);
    state->WindowClass = { sizeof(state->WindowClass), CS_CLASSDC, ImGuiApp_ImplWin32_WndProc, 0L, 0L, instance, nullptr, LoadCursor(nullptr, IDC_ARROW), (HBRUSH)(GetStockObject(BLACK_BRUSH)), nullptr, "ImGuiXWindow", nullptr };
    ::RegisterClassExA(&state->WindowClass);
    // Offscreen uses WS_POPUP so the client rect (io.DisplaySize) exactly equals the render target.
    const DWORD window_style = offscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    state->Hwnd = ::CreateWindowA(state->WindowClass.lpszClassName, config.WindowTitle, window_style, 100, 100, window_width, window_height, nullptr, nullptr, state->WindowClass.hInstance, nullptr);
    if (state->Hwnd == nullptr)
        return false;

    if (!offscreen)
    {
        ::ShowWindow(state->Hwnd, SW_SHOWDEFAULT);
        ::UpdateWindow(state->Hwnd);
    }

    if (ImGui::GetCurrentContext() == nullptr)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        state->OwnsImGuiContext = true;
    }

    ImGuiApp_ImplWin32Vulkan_InitInfo init_info;
    init_info.Hwnd             = state->Hwnd;
    init_info.MinImageCount    = 3;
#if defined(_DEBUG)
    init_info.EnableValidation = true;
#else
    init_info.EnableValidation = false;
#endif
    init_info.Headless         = config.Headless;
    init_info.OffscreenWidth   = window_width;
    init_info.OffscreenHeight  = window_height;
    if (!ImGuiApp_ImplWin32Vulkan_Init(app, &init_info))
        return false;

    ImGui::GetIO().ConfigFlags |= config.ConfigFlags;

    app->PlatformName         = config.PlatformName;
    app->PlatformWindowHandle = state->Hwnd;
    ::SetWindowLongPtr(state->Hwnd, GWLP_USERDATA, (LONG_PTR)app);
    return true;
}

void ImGuiApp_ImplWin32Vulkan_ShutdownPlatform(ImGuiApp* app)
{
    // Graphics first (wrapped imgui backends + vulkan objects need the window alive), then the host.
    if (ImGuiApp_ImplWin32Vulkan_GetBackendData(app) != nullptr)
        ImGuiApp_ImplWin32Vulkan_Shutdown(app);

    ImGuiApp_ImplWin32Vulkan_PlatformData* state = (ImGuiApp_ImplWin32Vulkan_PlatformData*)app->PlatformData;
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

// Converts staging buffer `index` into CaptureRgba and fills out_frame. False on an
// unsupported (non-32-bit) format.
static bool ImGuiApp_ImplWin32Vulkan_CaptureConvertAndFill(ImGuiApp_ImplWin32Vulkan_Data* bd, int index, ImGuiAppAVFrame* out_frame)
{
    const int      width = bd->CaptureCopyWidth[index];
    const int      height = bd->CaptureCopyHeight[index];
    const VkFormat format = bd->CaptureCopyFormat[index];
    const unsigned char* src = (const unsigned char*)bd->CaptureMapped[index];

    if (format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_R8G8B8A8_SRGB &&
        format != VK_FORMAT_B8G8R8A8_UNORM && format != VK_FORMAT_B8G8R8A8_SRGB)
        return false;

    // The mapped staging memory is typically WRITE-COMBINED (uncached): per-byte reads
    // from it are ~100x slow (observed 1.3s/frame). Bulk-copy into cached CPU memory
    // FIRST, then swizzle in place with word ops.
    bd->CaptureRgba.resize(width * height * 4);
    memcpy(bd->CaptureRgba.Data, src, (size_t)width * (size_t)height * 4);
    if (format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB)
    {
        unsigned int* px = (unsigned int*)bd->CaptureRgba.Data;
        const size_t n = (size_t)width * (size_t)height;
        for (size_t i = 0; i < n; i++)
        {
            const unsigned int v = px[i];
            px[i] = (v & 0xFF00FF00u) | ((v & 0x000000FFu) << 16) | ((v >> 16) & 0x000000FFu);
        }
    }

    out_frame->Width = width;
    out_frame->Height = height;
    out_frame->PitchBytes = width * 4;
    out_frame->Pixels = bd->CaptureRgba.Data;
    out_frame->FrameID = bd->CaptureCopyId[index];   // the pixels' identity, not the pumping frame's
    bd->CaptureLastReturned = bd->CaptureCopyId[index].FrameIndex;
    return true;
}

// One-shot synchronous copy of the CURRENT image (valid only in the encode phase: rendered,
// not yet presented). Fills staging slot CaptureWriteIndex WITHOUT flipping it -- the next
// rendered frame overwrites the slot, whose content this call consumed.
static bool ImGuiApp_ImplWin32Vulkan_CaptureSyncNow(ImGuiApp_ImplWin32Vulkan_Data* bd, VkImage image, int width, int height, VkFormat format, VkImageLayout layout)
{
    if (image == VK_NULL_HANDLE || width <= 0 || height <= 0)
        return false;

    if (bd->CaptureSyncPool == VK_NULL_HANDLE)
    {
        VkCommandPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = bd->QueueFamily;
        if (vkCreateCommandPool(bd->Device, &pool_info, bd->Allocator, &bd->CaptureSyncPool) != VK_SUCCESS)
            return false;
        VkCommandBufferAllocateInfo cmd_info = {};
        cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_info.commandPool = bd->CaptureSyncPool;
        cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_info.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(bd->Device, &cmd_info, &bd->CaptureSyncCmd) != VK_SUCCESS)
            return false;
        VkFenceCreateInfo fence_info = {};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(bd->Device, &fence_info, bd->Allocator, &bd->CaptureSyncFence) != VK_SUCCESS)
            return false;
    }

    const int w = bd->CaptureWriteIndex;
    if (!ImGuiApp_ImplWin32Vulkan_EnsureCaptureBuffer(bd, w, (VkDeviceSize)width * (VkDeviceSize)height * 4, VK_NULL_HANDLE))
        return false;

    vkResetCommandBuffer(bd->CaptureSyncCmd, 0);
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(bd->CaptureSyncCmd, &begin_info) != VK_SUCCESS)
        return false;

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = layout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(bd->CaptureSyncCmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (uint32_t)width;
    region.imageExtent.height = (uint32_t)height;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(bd->CaptureSyncCmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, bd->CaptureBuffer[w], 1, &region);

    if (layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = layout;
        vkCmdPipelineBarrier(bd->CaptureSyncCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    if (vkEndCommandBuffer(bd->CaptureSyncCmd) != VK_SUCCESS)
        return false;

    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &bd->CaptureSyncCmd;
    vkResetFences(bd->Device, 1, &bd->CaptureSyncFence);
    if (vkQueueSubmit(bd->Queue, 1, &submit, bd->CaptureSyncFence) != VK_SUCCESS)
        return false;
    vkWaitForFences(bd->Device, 1, &bd->CaptureSyncFence, VK_TRUE, UINT64_MAX);

    bd->CaptureCopyWidth[w] = width;
    bd->CaptureCopyHeight[w] = height;
    bd->CaptureCopyFormat[w] = format;
    bd->CaptureCopyId[w] = bd->App != nullptr ? bd->App->FrameID : ImGuiAppFrameID();
    bd->CapturePendingFence[w] = VK_NULL_HANDLE;
    return true;
}

// Readback contract (encode phase, i.e. after render, before present):
// - FIRST call: one synchronous out-of-band copy of the frame just rendered -- frame 1 of a
//   take is never lost to pipeline priming. Later frames use the in-frame pipelined copy.
// - Steady state: returns the PREVIOUS frame's retired copy (one frame of latency, no stall);
//   the FrameID travels with the pixels, so identity holds.
// - No new retired copy (pipeline bubble, or nothing rendered since the last call): returns
//   the freshest unreturned copy IF its fence already signaled (never blocks mid-take), else
//   false. Callers drain the final frame by re-calling after the GPU settles.
// - Never returns the same FrameIndex twice (CaptureLastReturned gate).
static bool ImGuiApp_ImplWin32Vulkan_CaptureFrame(ImGuiApp* app, ImGuiAppAVFrame* out_frame)
{
    ImGuiApp_ImplWin32Vulkan_Data* bd = ImGuiApp_ImplWin32Vulkan_GetBackendData(app);
    if (bd == nullptr)
        return false;
    if (out_frame == nullptr || !bd->VulkanInitialized || !bd->CaptureSupported)
        return false;

    if (!bd->CaptureArmed)
    {
        bd->CaptureArmed = true;
        if (bd->OffscreenActive)
        {
            if (ImGuiApp_ImplWin32Vulkan_CaptureSyncNow(bd, bd->OffscreenImage, bd->OffscreenWidth, bd->OffscreenHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL))
                return ImGuiApp_ImplWin32Vulkan_CaptureConvertAndFill(bd, bd->CaptureWriteIndex, out_frame);
            return false;
        }
        ImGui_ImplVulkanH_Window* wd = &bd->MainWindow;
        if (!bd->LastFrameRendered)
            return false;   // minimized/unrendered first frame: nothing to copy yet
        VkImage backbuffer = wd->Frames[wd->FrameIndex].Backbuffer;
        if (ImGuiApp_ImplWin32Vulkan_CaptureSyncNow(bd, backbuffer, wd->Width, wd->Height, wd->SurfaceFormat.format, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR))
            return ImGuiApp_ImplWin32Vulkan_CaptureConvertAndFill(bd, bd->CaptureWriteIndex, out_frame);
        return false;
    }

    // Retired path: ImGuiApp_ImplWin32Vulkan_RecordCaptureCopy flips the write index AFTER writing, so the un-flipped
    // index holds the previous frame's copy; its fence was submitted last frame (in steady
    // state long signaled -- if the swapchain reused it for a newer submit the wait only
    // lengthens; this buffer's copy completed before the fence's first signal, so no tearing).
    const int r = bd->CaptureWriteIndex;
    if (bd->CaptureCopyWidth[r] > 0 && bd->CaptureMapped[r] != nullptr &&
        bd->CaptureCopyId[r].FrameIndex > bd->CaptureLastReturned)
    {
        if (bd->CapturePendingFence[r] != VK_NULL_HANDLE)
        {
            vkWaitForFences(bd->Device, 1, &bd->CapturePendingFence[r], VK_TRUE, UINT64_MAX);
            bd->CapturePendingFence[r] = VK_NULL_HANDLE;
        }
        return ImGuiApp_ImplWin32Vulkan_CaptureConvertAndFill(bd, r, out_frame);
    }

    // Catch-up path: the fresh slot may hold an unreturned copy (final frame of a take, or a
    // fast GPU that already signaled this frame's fence). STATUS check only -- never block.
    const int f = r ^ 1;
    if (bd->CaptureCopyWidth[f] > 0 && bd->CaptureMapped[f] != nullptr &&
        bd->CaptureCopyId[f].FrameIndex > bd->CaptureLastReturned)
    {
        if (bd->CapturePendingFence[f] == VK_NULL_HANDLE ||
            vkGetFenceStatus(bd->Device, bd->CapturePendingFence[f]) == VK_SUCCESS)
        {
            bd->CapturePendingFence[f] = VK_NULL_HANDLE;
            return ImGuiApp_ImplWin32Vulkan_CaptureConvertAndFill(bd, f, out_frame);
        }
    }

    return false;
}

static const ImGuiAppPlatformBackend ImGuiApp_ImplWin32Vulkan_PlatformBackend =
{
    ImGuiApp_ImplWin32Vulkan_InitPlatform,
    ImGuiApp_ImplWin32Vulkan_ShutdownPlatform,
    ImGuiApp_ImplWin32_RunLoop,
    ImGuiApp_ImplWin32Vulkan_CaptureFrame,
    "imguiapp_impl_win32_vulkan",
    ImGuiApp_ImplWin32Vulkan_Shutdown,
    ImGuiApp_ImplWin32Vulkan_NewFrame,
    ImGuiApp_ImplWin32Vulkan_RenderDrawData,
    ImGuiApp_ImplWin32Vulkan_PresentFrame,
};

const ImGuiAppPlatformBackend* ImGuiApp_ImplWin32Vulkan_GetPlatformBackend() { return &ImGuiApp_ImplWin32Vulkan_PlatformBackend; }



#endif // #ifndef IMGUI_DISABLE
