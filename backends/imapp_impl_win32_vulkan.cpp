
#include "imapp_impl_win32_vulkan.h"
#include "imgui_applayer.h"
#include "imapp_impl_win32_state.h"

#include "imgui_impl_win32.h"

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include "imgui_impl_vulkan.h"

#include <windows.h>
#include <d3dkmthk.h>


#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    struct ImGuiApp_Win32Vulkan_InitInfo
    {
        void*        Hwnd;
        unsigned int MinImageCount;
        bool         EnableValidation;
    };

    struct ImGuiApp_Win32Vulkan_Data
    {
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
        D3DKMT_HANDLE                  VBlankAdapter;
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VBlankSourceId;
        HMONITOR                       VBlankMonitor;
        bool                           VBlankWaitAvailable;
        bool                           VBlankWaitDisabled;
        bool                           SwapChainRebuild;
        bool                           PlatformBackendInitialized;
        bool                           RendererBackendInitialized;
        bool                           VulkanInitialized;
    };

    struct D3DKMTApi
    {
        bool                                Loaded;
        HMODULE                             Module;
        PFND3DKMT_OPENADAPTERFROMHDC        OpenAdapterFromHdc;
        PFND3DKMT_CLOSEADAPTER              CloseAdapter;
        PFND3DKMT_WAITFORVERTICALBLANKEVENT WaitForVerticalBlankEvent;

        bool IsAvailable() const
        {
            return OpenAdapterFromHdc != nullptr &&
                   CloseAdapter != nullptr &&
                   WaitForVerticalBlankEvent != nullptr;
        }
    };

    ImGuiApp_Win32Vulkan_Data GBackend;

    void CheckVkResult(VkResult err)
    {
        if (err == VK_SUCCESS)
            return;
        std::fprintf(stderr, "[vulkan] VkResult = %d\n", err);
        if (err < 0)
            std::abort();
    }

    bool IsInitInfoValid(const ImGuiApp_Win32Vulkan_InitInfo* init_info)
    {
        return init_info != nullptr && init_info->Hwnd != nullptr;
    }

    bool IsNtSuccess(NTSTATUS status)
    {
        return status >= 0;
    }

    D3DKMTApi& GetD3DKMTApi()
    {
        static D3DKMTApi api;
        if (api.Loaded)
            return api;

        api.Loaded = true;
        api.Module = ::GetModuleHandleA("gdi32.dll");
        if (api.Module == nullptr)
            api.Module = ::LoadLibraryA("gdi32.dll");
        if (api.Module == nullptr)
            return api;

        api.OpenAdapterFromHdc = reinterpret_cast<PFND3DKMT_OPENADAPTERFROMHDC>(::GetProcAddress(api.Module, "D3DKMTOpenAdapterFromHdc"));
        api.CloseAdapter = reinterpret_cast<PFND3DKMT_CLOSEADAPTER>(::GetProcAddress(api.Module, "D3DKMTCloseAdapter"));
        api.WaitForVerticalBlankEvent = reinterpret_cast<PFND3DKMT_WAITFORVERTICALBLANKEVENT>(::GetProcAddress(api.Module, "D3DKMTWaitForVerticalBlankEvent"));
        return api;
    }

    bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension)
    {
        for (const VkExtensionProperties& property : properties)
            if (std::strcmp(property.extensionName, extension) == 0)
                return true;
        return false;
    }

    bool IsLayerAvailable(const ImVector<VkLayerProperties>& properties, const char* layer)
    {
        for (const VkLayerProperties& property : properties)
            if (std::strcmp(property.layerName, layer) == 0)
                return true;
        return false;
    }

    void GetClientSize(HWND hwnd, int* width, int* height)
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

    void CloseVBlankWaitAdapter(ImGuiApp_Win32Vulkan_Data* bd)
    {
        if (bd == nullptr || bd->VBlankAdapter == 0)
            return;

        D3DKMTApi& api = GetD3DKMTApi();
        if (api.CloseAdapter != nullptr)
        {
            D3DKMT_CLOSEADAPTER close_adapter = {};
            close_adapter.hAdapter = bd->VBlankAdapter;
            api.CloseAdapter(&close_adapter);
        }

        bd->VBlankAdapter = 0;
        bd->VBlankSourceId = 0;
        bd->VBlankMonitor = nullptr;
        bd->VBlankWaitAvailable = false;
    }

    bool OpenVBlankWaitAdapter(ImGuiApp_Win32Vulkan_Data* bd)
    {
        if (bd == nullptr || bd->Hwnd == nullptr)
            return false;

        D3DKMTApi& api = GetD3DKMTApi();
        if (!api.IsAvailable())
            return false;

        HDC hdc = ::GetDC(bd->Hwnd);
        if (hdc == nullptr)
            return false;

        D3DKMT_OPENADAPTERFROMHDC open_adapter = {};
        open_adapter.hDc = hdc;
        NTSTATUS status = api.OpenAdapterFromHdc(&open_adapter);
        ::ReleaseDC(bd->Hwnd, hdc);

        if (!IsNtSuccess(status) || open_adapter.hAdapter == 0)
            return false;

        CloseVBlankWaitAdapter(bd);
        bd->VBlankAdapter = open_adapter.hAdapter;
        bd->VBlankSourceId = open_adapter.VidPnSourceId;
        bd->VBlankMonitor = ::MonitorFromWindow(bd->Hwnd, MONITOR_DEFAULTTONEAREST);
        bd->VBlankWaitAvailable = true;
        return true;
    }

    void WaitForVBlankBeforePresent(ImGuiApp_Win32Vulkan_Data* bd, ImGui_ImplVulkanH_Window* wd)
    {
        if (bd == nullptr || wd == nullptr || bd->VBlankWaitDisabled || wd->PresentMode != VK_PRESENT_MODE_IMMEDIATE_KHR)
            return;

        HMONITOR monitor = ::MonitorFromWindow(bd->Hwnd, MONITOR_DEFAULTTONEAREST);
        if (!bd->VBlankWaitAvailable || bd->VBlankMonitor != monitor)
        {
            if (!OpenVBlankWaitAdapter(bd))
            {
                bd->VBlankWaitDisabled = true;
                return;
            }
        }

        D3DKMTApi& api = GetD3DKMTApi();
        D3DKMT_WAITFORVERTICALBLANKEVENT wait = {};
        wait.hAdapter = bd->VBlankAdapter;
        wait.VidPnSourceId = bd->VBlankSourceId;

        NTSTATUS status = api.WaitForVerticalBlankEvent(&wait);
        if (IsNtSuccess(status))
            return;

        CloseVBlankWaitAdapter(bd);
        if (!OpenVBlankWaitAdapter(bd))
        {
            bd->VBlankWaitDisabled = true;
            return;
        }

        wait = {};
        wait.hAdapter = bd->VBlankAdapter;
        wait.VidPnSourceId = bd->VBlankSourceId;
        status = api.WaitForVerticalBlankEvent(&wait);
        if (!IsNtSuccess(status))
        {
            CloseVBlankWaitAdapter(bd);
            bd->VBlankWaitDisabled = true;
        }
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
        std::fprintf(stderr, "[vulkan] Debug report from object type %d: %s\n", object_type, message);
        return VK_FALSE;
    }

    bool SetupVulkan(ImGuiApp_Win32Vulkan_Data* bd, ImVector<const char*> instance_extensions, bool enable_validation)
    {
        IM_ASSERT(bd != nullptr);
        if (bd == nullptr)
            return false;

        VkResult err;

        uint32_t properties_count = 0;
        err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
        CheckVkResult(err);
        ImVector<VkExtensionProperties> properties;
        properties.resize(properties_count);
        if (properties_count > 0)
        {
            err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
            CheckVkResult(err);
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

        if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
            instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
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
            CheckVkResult(err);
            ImVector<VkLayerProperties> layers;
            layers.resize(layer_count);
            if (layer_count > 0)
            {
                err = vkEnumerateInstanceLayerProperties(&layer_count, layers.Data);
                CheckVkResult(err);
            }

            validation_enabled = IsLayerAvailable(layers, validation_layer) &&
                                 IsExtensionAvailable(properties, VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
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
        CheckVkResult(err);
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
                CheckVkResult(err);
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
        CheckVkResult(err);
        ImVector<VkExtensionProperties> device_properties;
        device_properties.resize(device_properties_count);
        if (device_properties_count > 0)
        {
            err = vkEnumerateDeviceExtensionProperties(bd->PhysicalDevice, nullptr, &device_properties_count, device_properties.Data);
            CheckVkResult(err);
        }
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
        if (IsExtensionAvailable(device_properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
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
        CheckVkResult(err);
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
        CheckVkResult(err);
        if (bd->DescriptorPool == VK_NULL_HANDLE)
            return false;

        bd->VulkanInitialized = true;
        return true;
    }

    bool SetupVulkanWindow(ImGuiApp_Win32Vulkan_Data* bd, ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height)
    {
        IM_ASSERT(bd != nullptr);
        IM_ASSERT(wd != nullptr);
        if (bd == nullptr || wd == nullptr || surface == VK_NULL_HANDLE || width <= 0 || height <= 0)
            return false;

        VkBool32 supported = VK_FALSE;
        VkResult err = vkGetPhysicalDeviceSurfaceSupportKHR(bd->PhysicalDevice, bd->QueueFamily, surface, &supported);
        CheckVkResult(err);
        if (supported != VK_TRUE)
        {
            std::fprintf(stderr, "[vulkan] selected physical device has no WSI support.\n");
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

        VkPresentModeKHR present_modes[] =
        {
            VK_PRESENT_MODE_IMMEDIATE_KHR,
            VK_PRESENT_MODE_MAILBOX_KHR,
            VK_PRESENT_MODE_FIFO_KHR,
        };
        wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
            bd->PhysicalDevice,
            wd->Surface,
            present_modes,
            IM_COUNTOF(present_modes));

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
            0);
        return true;
    }

    void CleanupVulkanWindow(ImGuiApp_Win32Vulkan_Data* bd, ImGui_ImplVulkanH_Window* wd)
    {
        if (bd == nullptr || wd == nullptr || bd->Instance == VK_NULL_HANDLE || bd->Device == VK_NULL_HANDLE)
            return;

        VkSurfaceKHR surface = wd->Surface;
        ImGui_ImplVulkanH_DestroyWindow(bd->Instance, bd->Device, wd, bd->Allocator);
        if (surface != VK_NULL_HANDLE)
            vkDestroySurfaceKHR(bd->Instance, surface, bd->Allocator);
        *wd = ImGui_ImplVulkanH_Window();
    }

    void CleanupVulkan(ImGuiApp_Win32Vulkan_Data* bd)
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

    void ResizeMainWindowIfNeeded(ImGuiApp_Win32Vulkan_Data* bd)
    {
        if (bd == nullptr || bd->Device == VK_NULL_HANDLE || bd->MainWindow.Surface == VK_NULL_HANDLE)
            return;

        int width = 0;
        int height = 0;
        GetClientSize(bd->Hwnd, &width, &height);
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
            0);
        bd->MainWindow.FrameIndex = 0;
        bd->SwapChainRebuild = false;
    }

    void FrameRender(ImGuiApp_Win32Vulkan_Data* bd, ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data)
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
            CheckVkResult(err);

        ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
        err = vkWaitForFences(bd->Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
        CheckVkResult(err);
        err = vkResetFences(bd->Device, 1, &fd->Fence);
        CheckVkResult(err);

        err = vkResetCommandPool(bd->Device, fd->CommandPool, 0);
        CheckVkResult(err);

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(fd->CommandBuffer, &begin_info);
        CheckVkResult(err);

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
        CheckVkResult(err);
        err = vkQueueSubmit(bd->Queue, 1, &submit_info, fd->Fence);
        CheckVkResult(err);
    }

    void FramePresent(ImGuiApp_Win32Vulkan_Data* bd, ImGui_ImplVulkanH_Window* wd)
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

        WaitForVBlankBeforePresent(bd, wd);
        VkResult err = vkQueuePresentKHR(bd->Queue, &present_info);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
            bd->SwapChainRebuild = true;
        if (err == VK_ERROR_OUT_OF_DATE_KHR)
            return;
        if (err != VK_SUBOPTIMAL_KHR)
            CheckVkResult(err);
        wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;
    }

    int CreateVkSurfaceForViewport(ImGuiViewport* viewport, ImU64 vk_instance, const void* vk_allocator, ImU64* out_vk_surface)
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

    void ShutdownBackend(void* user_data)
    {
        ImGuiApp_Win32Vulkan_Data* bd = (ImGuiApp_Win32Vulkan_Data*)user_data;
        IM_ASSERT(bd != nullptr);
        if (bd == nullptr)
            return;

        if (bd->Device != VK_NULL_HANDLE)
            vkDeviceWaitIdle(bd->Device);

        if (bd->RendererBackendInitialized)
            ImGui_ImplVulkan_Shutdown();
        if (bd->PlatformBackendInitialized)
            ImGui_ImplWin32_Shutdown();

        CloseVBlankWaitAdapter(bd);
        CleanupVulkanWindow(bd, &bd->MainWindow);
        CleanupVulkan(bd);

        *bd = ImGuiApp_Win32Vulkan_Data();
    }

    void NewFrame(void* user_data)
    {
        ImGuiApp_Win32Vulkan_Data* bd = (ImGuiApp_Win32Vulkan_Data*)user_data;
        IM_ASSERT(bd != nullptr);
        if (bd == nullptr)
            return;

        ResizeMainWindowIfNeeded(bd);
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplWin32_NewFrame();
    }

    void RenderDrawData(ImDrawData* draw_data, const ImGuiAppFrameConfig* config, void* user_data)
    {
        ImGuiApp_Win32Vulkan_Data* bd = (ImGuiApp_Win32Vulkan_Data*)user_data;
        IM_ASSERT(bd != nullptr);
        if (bd == nullptr || draw_data == nullptr || config == nullptr)
            return;

        ResizeMainWindowIfNeeded(bd);

        const bool minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
        bd->MainWindow.ClearValue.color.float32[0] = config->ClearColor.x * config->ClearColor.w;
        bd->MainWindow.ClearValue.color.float32[1] = config->ClearColor.y * config->ClearColor.w;
        bd->MainWindow.ClearValue.color.float32[2] = config->ClearColor.z * config->ClearColor.w;
        bd->MainWindow.ClearValue.color.float32[3] = config->ClearColor.w;

        if (!minimized)
            FrameRender(bd, &bd->MainWindow, draw_data);

        if ((config->Flags & ImGuiAppFrameFlags_NoPlatformWindows) == 0 &&
            (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        if (!minimized && (config->Flags & ImGuiAppFrameFlags_NoPresent) == 0)
            FramePresent(bd, &bd->MainWindow);
    }
}

static bool ImGuiApp_Win32Vulkan_Init(const ImGuiApp_Win32Vulkan_InitInfo* init_info)
{
    if (ImGuiX::GetCurrentContext() == nullptr)
        ImGuiX::CreateContext();

    IM_ASSERT(IsInitInfoValid(init_info) && "ImGuiApp_Win32Vulkan_Init: invalid init_info.");
    if (!IsInitInfoValid(init_info))
        return false;

    ImGuiX::Shutdown();

    GBackend.Hwnd = (HWND)init_info->Hwnd;
    GBackend.MinImageCount = init_info->MinImageCount >= 3 ? init_info->MinImageCount : 3;

    ImVector<const char*> extensions;
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    if (!SetupVulkan(&GBackend, extensions, init_info->EnableValidation))
    {
        ShutdownBackend(&GBackend);
        return false;
    }

    int width = 0;
    int height = 0;
    GetClientSize(GBackend.Hwnd, &width, &height);
    if (width <= 0 || height <= 0)
    {
        ShutdownBackend(&GBackend);
        return false;
    }

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkWin32SurfaceCreateInfoKHR surface_info = {};
    surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface_info.hwnd = GBackend.Hwnd;
    surface_info.hinstance = ::GetModuleHandle(nullptr);
    VkResult err = vkCreateWin32SurfaceKHR(GBackend.Instance, &surface_info, GBackend.Allocator, &surface);
    CheckVkResult(err);
    if (surface == VK_NULL_HANDLE || !SetupVulkanWindow(&GBackend, &GBackend.MainWindow, surface, width, height))
    {
        if (surface != VK_NULL_HANDLE)
            vkDestroySurfaceKHR(GBackend.Instance, surface, GBackend.Allocator);
        ShutdownBackend(&GBackend);
        return false;
    }

    if (!ImGui_ImplWin32_Init(GBackend.Hwnd))
    {
        ShutdownBackend(&GBackend);
        return false;
    }
    GBackend.PlatformBackendInitialized = true;
    ImGui::GetPlatformIO().Platform_CreateVkSurface = CreateVkSurfaceForViewport;

    ImGui_ImplVulkan_InitInfo vulkan_init_info = {};
    vulkan_init_info.Instance = GBackend.Instance;
    vulkan_init_info.PhysicalDevice = GBackend.PhysicalDevice;
    vulkan_init_info.Device = GBackend.Device;
    vulkan_init_info.QueueFamily = GBackend.QueueFamily;
    vulkan_init_info.Queue = GBackend.Queue;
    vulkan_init_info.PipelineCache = GBackend.PipelineCache;
    vulkan_init_info.DescriptorPool = GBackend.DescriptorPool;
    vulkan_init_info.MinImageCount = GBackend.MinImageCount;
    vulkan_init_info.ImageCount = GBackend.MainWindow.ImageCount;
    vulkan_init_info.Allocator = GBackend.Allocator;
    vulkan_init_info.PipelineInfoMain.RenderPass = GBackend.MainWindow.RenderPass;
    vulkan_init_info.PipelineInfoMain.Subpass = 0;
    vulkan_init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    vulkan_init_info.CheckVkResultFn = CheckVkResult;

    if (!ImGui_ImplVulkan_Init(&vulkan_init_info))
    {
        ShutdownBackend(&GBackend);
        return false;
    }
    GBackend.RendererBackendInitialized = true;

    ImGuiXInitInfo imguix_init_info;
    imguix_init_info.Backend.Name = "imapp_impl_win32_vulkan";
    imguix_init_info.Backend.UserData = &GBackend;
    imguix_init_info.Backend.Shutdown = ShutdownBackend;
    imguix_init_info.Backend.NewFrame = NewFrame;
    imguix_init_info.Backend.RenderDrawData = RenderDrawData;

    if (!ImGuiX::Initialize(&imguix_init_info))
    {
        ShutdownBackend(&GBackend);
        return false;
    }

    return true;
}

bool ImGuiApp_Win32Vulkan_InitPlatform(ImGuiApp* app, ImGuiAppConfig& config)
{
    ImGuiAppPlatformState* state = IM_NEW(ImGuiAppPlatformState)();
    app->PlatformData = state;

    ImGui_ImplWin32_EnableDpiAwareness();
    const float main_scale    = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    const int   window_width  = (int)(config.WindowWidth  * main_scale);
    const int   window_height = (int)(config.WindowHeight * main_scale);
    config.DpiScale    = main_scale;
    config.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;

    HINSTANCE instance = ::GetModuleHandle(nullptr);
    state->WindowClass = { sizeof(state->WindowClass), CS_CLASSDC, ImGuiApp_ImplWin32_WndProc, 0L, 0L, instance, nullptr, LoadCursor(nullptr, IDC_ARROW), (HBRUSH)(GetStockObject(BLACK_BRUSH)), nullptr, "ImGuiXWindow", nullptr };
    ::RegisterClassExA(&state->WindowClass);
    state->Hwnd = ::CreateWindowA(state->WindowClass.lpszClassName, config.WindowTitle, WS_OVERLAPPEDWINDOW, 100, 100, window_width, window_height, nullptr, nullptr, state->WindowClass.hInstance, nullptr);
    if (state->Hwnd == nullptr)
        return false;

    ::ShowWindow(state->Hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(state->Hwnd);

    ImGuiX::CreateContext();

    ImGuiApp_Win32Vulkan_InitInfo init_info;
    init_info.Hwnd             = state->Hwnd;
    init_info.MinImageCount    = 3;
#if defined(_DEBUG)
    init_info.EnableValidation = true;
#else
    init_info.EnableValidation = false;
#endif
    if (!ImGuiApp_Win32Vulkan_Init(&init_info))
    {
        ImGuiX::DestroyContext();
        return false;
    }

    ImGui::GetIO().ConfigFlags |= config.ConfigFlags;

    app->Platform.Name               = config.Platform.Name;
    app->Platform.NativeWindowHandle = state->Hwnd;
    ::SetWindowLongPtr(state->Hwnd, GWLP_USERDATA, (LONG_PTR)app);
    return true;
}

void ImGuiApp_Win32Vulkan_ShutdownPlatform(ImGuiApp* app)
{
    ImGuiAppPlatformState* state = static_cast<ImGuiAppPlatformState*>(app->PlatformData);
    if (state == nullptr)
        return;
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

static const ImGuiAppPlatformBackend GPlatformBackend =
{
    ImGuiApp_Win32Vulkan_InitPlatform,
    ImGuiApp_Win32Vulkan_ShutdownPlatform,
    ImGuiApp_ImplWin32_RunLoop,
};

const ImGuiAppPlatformBackend* ImGuiApp_GetPlatformBackend() { return &GPlatformBackend; }


