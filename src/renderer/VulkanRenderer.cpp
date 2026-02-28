#include "VulkanRenderer.h"

#include <ranges>

#include <helper/VulkanHelper.h>

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <openvr.h>
#include <overlay/Overlay.hpp>

#include <config.hpp>

VulkanRenderer::VulkanRenderer() 
{
    vulkan_instance_ = VK_NULL_HANDLE;
    vulkan_physical_device_ = VK_NULL_HANDLE;
    vulkan_queue_family_ = 0;
    vulkan_allocator_ = nullptr;
    vulkan_device_ = VK_NULL_HANDLE;
    vulkan_queue_ = VK_NULL_HANDLE;
    vulkan_descriptor_pool_ = VK_NULL_HANDLE;
    vulkan_pipeline_cache_ = VK_NULL_HANDLE;
    vulkan_instance_extensions_ = {};
    vulkan_instance_extensions_.clear();
    vulkan_device_extensions_ = {};
    vulkan_device_extensions_.clear();
    debug_report_ = VK_NULL_HANDLE;
    device_list_.clear();
    f_vkCmdBeginRenderingKHR = nullptr;
    f_vkCmdEndRenderingKHR = nullptr;
}

auto VulkanRenderer::Initialize()  -> void
{
    VkResult vk_result = {};

    auto get_instance_extensions = [](const std::vector<std::string>& extensions) -> std::vector<const char*> {
        std::vector<const char*> result;
        for (auto& extension : extensions)
            result.push_back(extension.data());
        return result;
    };

    vulkan_instance_extensions_ = GetVulkanInstanceExtensionsRequiredByOpenVR();
    auto instance_extensions = get_instance_extensions(vulkan_instance_extensions_);

#ifdef ENABLE_VULKAN_VALIDATION
    instance_extensions.push_back("VK_EXT_debug_report");
#endif

    VkInstanceCreateInfo instance_create_info = {};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.enabledExtensionCount = (uint32_t)instance_extensions.size();
    instance_create_info.ppEnabledExtensionNames = instance_extensions.data();

#ifdef ENABLE_VULKAN_VALIDATION
    const char* enabled_layers[] = 
    { 
        "VK_LAYER_KHRONOS_validation" 
    };
    instance_create_info.enabledLayerCount = 1;
    instance_create_info.ppEnabledLayerNames = enabled_layers;
#endif

    vk_result = vkCreateInstance(&instance_create_info, vulkan_allocator_, &vulkan_instance_);
    VK_VALIDATE_RESULT(vk_result);

#ifdef ENABLE_VULKAN_VALIDATION
    auto DebugReport = [](VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData) -> VkBool32
    {
        (void)flags; 
        (void)object;
        (void)location; 
        (void)messageCode; 
        (void)pUserData; 
        (void)pLayerPrefix;

        fprintf(stderr, "[Vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
        return VK_FALSE;
    };

    auto f_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(vulkan_instance_, "vkCreateDebugReportCallbackEXT");
    VkDebugReportCallbackCreateInfoEXT debug_report_create_info{};
    debug_report_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    debug_report_create_info.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
    debug_report_create_info.pfnCallback = DebugReport;
    debug_report_create_info.pUserData = nullptr;

    vk_result = f_vkCreateDebugReportCallbackEXT(vulkan_instance_, &debug_report_create_info, vulkan_allocator_, &debug_report_);
    VK_VALIDATE_RESULT(vk_result);
#endif

    uint32_t device_count = {};
    vk_result = vkEnumeratePhysicalDevices(vulkan_instance_, &device_count, nullptr);
    VK_VALIDATE_RESULT(vk_result);

    // TODO: describe this error better to the user.
    if (device_count == 0)
        std::exit(EXIT_FAILURE);

    device_list_.resize(device_count);
    vk_result = vkEnumeratePhysicalDevices(vulkan_instance_, &device_count, device_list_.data());
    VK_VALIDATE_RESULT(vk_result);

    for (VkPhysicalDevice& device : device_list_)
    {
        VkPhysicalDeviceProperties properties = {};
        vkGetPhysicalDeviceProperties(device, &properties);

        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            vulkan_physical_device_ = device;
            continue;
        }

        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            vulkan_physical_device_ = device;
            break;
        }
    }

    VkPhysicalDeviceProperties vulkan_physical_device_properties = {};
    vkGetPhysicalDeviceProperties(vulkan_physical_device_, &vulkan_physical_device_properties);

    if (
        vulkan_physical_device_properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU &&
        vulkan_physical_device_properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) 
    {
        throw std::runtime_error("You need an GPU to run this program, make sure your drivers are up to date!");
    }

    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(vulkan_physical_device_, &properties);

    printf("Using device %s, Discrete: %s\n", properties.deviceName, properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "Yes" : "No");
    assert(vulkan_physical_device_ != VK_NULL_HANDLE);

    uint32_t family_prop_count = {};
    vkGetPhysicalDeviceQueueFamilyProperties(vulkan_physical_device_, &family_prop_count, nullptr);

    std::vector<VkQueueFamilyProperties> queues_properties = {};
    queues_properties.resize(family_prop_count);

    vkGetPhysicalDeviceQueueFamilyProperties(vulkan_physical_device_, &family_prop_count, queues_properties.data());

    for (size_t idx = 0; idx < queues_properties.size(); ++idx)
    {
        const auto& property = queues_properties[idx];
        if (property.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            vulkan_queue_family_ = static_cast<uint32_t>(idx);
            break;
        }
    }

    queues_properties.clear();
    assert(vulkan_queue_family_ != (uint32_t)-1);

    auto get_device_extensions = [](const std::vector<std::string>& extensions) -> std::vector<const char*> {
        std::vector<const char*> result = {};
        for (auto& extension : extensions)
            result.push_back(extension.c_str());
        return result;
    };

    vulkan_device_extensions_ = GetVulkanDeviceExtensionsRequiredByOpenVR(vulkan_physical_device_);

    bool has_dynamic_rendering = true;

    if (!IsVulkanDeviceExtensionAvailable(vulkan_physical_device_, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME))
        has_dynamic_rendering = false;

    if (!IsVulkanDeviceExtensionAvailable(vulkan_physical_device_, VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME))
        has_dynamic_rendering = false;

    if (!IsVulkanDeviceExtensionAvailable(vulkan_physical_device_, VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME))
        has_dynamic_rendering = false;

    if (!has_dynamic_rendering)
        throw std::runtime_error("Your graphics card drivers do not support dynamic rendering, upgrade your drivers and make sure your GPU supports it.");

    vulkan_device_extensions_.insert(vulkan_device_extensions_.end(), { 
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, 
        VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME, 
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME 
    });

    auto device_extensions = get_device_extensions(vulkan_device_extensions_);

    constexpr float queue_priority = 1.0f;
    
    VkDeviceQueueCreateInfo device_queue_info = {};
    device_queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    device_queue_info.queueFamilyIndex = vulkan_queue_family_;
    device_queue_info.queueCount = 1;
    device_queue_info.pQueuePriorities = &queue_priority;

    VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore_features = {};
    timeline_semaphore_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    timeline_semaphore_features.timelineSemaphore = VK_TRUE;

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_features = {};
    dynamic_rendering_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    dynamic_rendering_features.pNext = &timeline_semaphore_features;
    dynamic_rendering_features.dynamicRendering = VK_TRUE;

    VkDeviceCreateInfo device_create_info =  {};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.pNext = &dynamic_rendering_features;
    device_create_info.queueCreateInfoCount = 1;
    device_create_info.pQueueCreateInfos = &device_queue_info;
    device_create_info.enabledExtensionCount = (uint32_t)device_extensions.size();
    device_create_info.ppEnabledExtensionNames = device_extensions.data();

    vk_result = vkCreateDevice(vulkan_physical_device_, &device_create_info, vulkan_allocator_, &vulkan_device_);
    VK_VALIDATE_RESULT(vk_result);

    vkGetDeviceQueue(vulkan_device_, vulkan_queue_family_, 0, &vulkan_queue_);

    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER,                128 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          128 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          128 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   64  },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   64  },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         256 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         256 },
    };
    
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 2048;
    pool_info.poolSizeCount = std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    for (VkDescriptorPoolSize& pool_size : pool_sizes)
        pool_info.maxSets += pool_size.descriptorCount;

    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    vk_result = vkCreateDescriptorPool(vulkan_device_, &pool_info, vulkan_allocator_, &vulkan_descriptor_pool_);
    VK_VALIDATE_RESULT(vk_result);

    this->f_vkCmdBeginRenderingKHR = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(vulkan_device_, "vkCmdBeginRenderingKHR");
    assert(f_vkCmdBeginRenderingKHR != nullptr);
    this->f_vkCmdEndRenderingKHR = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(vulkan_device_, "vkCmdEndRenderingKHR");
    assert(f_vkCmdEndRenderingKHR != nullptr);
}

auto VulkanRenderer::SetupSurface(Overlay* overlay, uint32_t width, uint32_t height, VkSurfaceFormatKHR format) -> void
{
    VkResult vk_result = {};

    overlay->Surface()->width = width;
    overlay->Surface()->height = height;
    overlay->Surface()->texture_format = format;
    for (uint32_t i = 0; i < Vulkan_Surface::ImageCount; ++i)
        overlay->Surface()->first_use[i] = true;

    for (uint32_t i = 0; i < Vulkan_Surface::ImageCount; ++i) 
    {
        VkCommandPoolCreateInfo command_pool_create_info = {};
        command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        command_pool_create_info.queueFamilyIndex = vulkan_queue_family_;

        vk_result = vkCreateCommandPool(vulkan_device_, &command_pool_create_info, vulkan_allocator_, &overlay->Surface()->command_pools[i]);
        VK_VALIDATE_RESULT(vk_result);

        VkCommandBufferAllocateInfo command_buffer_allocate_info = {};
        command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_buffer_allocate_info.commandPool = overlay->Surface()->command_pools[i];
        command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_buffer_allocate_info.commandBufferCount = 1;

        vk_result = vkAllocateCommandBuffers(vulkan_device_, &command_buffer_allocate_info, &overlay->Surface()->command_buffers[i]);
        VK_VALIDATE_RESULT(vk_result);

        vkGetDeviceQueue(vulkan_device_, vulkan_queue_family_, 0, &overlay->Surface()->queue);

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vk_result = vkBeginCommandBuffer(overlay->Surface()->command_buffers[i], &begin_info);
        VK_VALIDATE_RESULT(vk_result);

        VkImageCreateInfo image_create_info = {};
        image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_create_info.imageType = VK_IMAGE_TYPE_2D;
        image_create_info.format = overlay->Surface()->texture_format.format;
        image_create_info.extent.width = overlay->Surface()->width;
        image_create_info.extent.height = overlay->Surface()->height;
        image_create_info.extent.depth = 1;
        image_create_info.mipLevels = 1;
        image_create_info.arrayLayers = 1;
        image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        vk_result = vkCreateImage(vulkan_device_, &image_create_info, nullptr, &overlay->Surface()->textures[i]);
        VK_VALIDATE_RESULT(vk_result);

        auto find_memory_type_index = [&](uint32_t type, VkMemoryPropertyFlags properties) -> uint32_t {
            VkPhysicalDeviceMemoryProperties memory_requirements = {};
            vkGetPhysicalDeviceMemoryProperties(vulkan_physical_device_, &memory_requirements);

            for (uint32_t i = 0; i < memory_requirements.memoryTypeCount; i++) {
                if ((type & (1 << i)) && (memory_requirements.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }
            throw std::runtime_error("Failed to find suitable memory type!");
            };

        VkMemoryRequirements memory_requirements = {};
        vkGetImageMemoryRequirements(vulkan_device_, overlay->Surface()->textures[i], &memory_requirements);

        VkMemoryAllocateInfo memory_alloc_info = {};
        memory_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory_alloc_info.allocationSize = memory_requirements.size;
        memory_alloc_info.memoryTypeIndex = find_memory_type_index(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vk_result = vkAllocateMemory(vulkan_device_, &memory_alloc_info, nullptr, &overlay->Surface()->texture_memories[i]);
        VK_VALIDATE_RESULT(vk_result);

        vk_result = vkBindImageMemory(vulkan_device_, overlay->Surface()->textures[i], overlay->Surface()->texture_memories[i], 0);
        VK_VALIDATE_RESULT(vk_result);


        VkImageViewCreateInfo image_view_info = {};
        image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_info.image = overlay->Surface()->textures[i];
        image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        image_view_info.format = overlay->Surface()->texture_format.format;
        image_view_info.components.r = VK_COMPONENT_SWIZZLE_R;
        image_view_info.components.g = VK_COMPONENT_SWIZZLE_G;
        image_view_info.components.b = VK_COMPONENT_SWIZZLE_B;
        image_view_info.components.a = VK_COMPONENT_SWIZZLE_A;
        image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        image_view_info.subresourceRange.baseMipLevel = 0;
        image_view_info.subresourceRange.levelCount = 1;
        image_view_info.subresourceRange.baseArrayLayer = 0;
        image_view_info.subresourceRange.layerCount = 1;

        vk_result = vkCreateImageView(vulkan_device_, &image_view_info, vulkan_allocator_, &overlay->Surface()->texture_views[i]);
        VK_VALIDATE_RESULT(vk_result);

        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = overlay->Surface()->textures[i];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(overlay->Surface()->command_buffers[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkFenceCreateInfo fence_create_info = {};
        fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        vk_result = vkCreateFence(vulkan_device_, &fence_create_info, vulkan_allocator_, &overlay->Surface()->fences[i]);
        VK_VALIDATE_RESULT(vk_result);

        vk_result = vkEndCommandBuffer(overlay->Surface()->command_buffers[i]);
        VK_VALIDATE_RESULT(vk_result);

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &overlay->Surface()->command_buffers[i];

        VkFence init_fence = {};
        VkFenceCreateInfo init_fence_info = {};
        init_fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

        vk_result = vkCreateFence(vulkan_device_, &init_fence_info, nullptr, &init_fence);
        VK_VALIDATE_RESULT(vk_result);

        vk_result = vkQueueSubmit(overlay->Surface()->queue, 1, &submit_info, init_fence);
        VK_VALIDATE_RESULT(vk_result);

        vk_result = vkWaitForFences(vulkan_device_, 1, &init_fence, VK_TRUE, UINT64_MAX);
        VK_VALIDATE_RESULT(vk_result);

        vkDestroyFence(vulkan_device_, init_fence, nullptr);
    }
}

auto VulkanRenderer::RenderSurface(ImDrawData* draw_data, Overlay* overlay) const -> void
{
    uint32_t idx = overlay->Surface()->frame_index;

    if (!overlay->IsVisible())
        return;

    VkResult vk_result = {};

    VkCommandBufferBeginInfo buffer_begin_info = {};
    buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

    VkRenderingAttachmentInfoKHR color_attachment = {};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    color_attachment.imageView = overlay->Surface()->texture_views[idx];
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.resolveMode = VK_RESOLVE_MODE_NONE_KHR;
    color_attachment.resolveImageView = VK_NULL_HANDLE;
    color_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue.color.float32[0] = 0.0f;
    color_attachment.clearValue.color.float32[1] = 0.0f;
    color_attachment.clearValue.color.float32[2] = 0.0f;
    color_attachment.clearValue.color.float32[3] = 0.0f;

    VkRenderingInfoKHR rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    rendering_info.flags = 0;
    rendering_info.renderArea.extent.width = overlay->Surface()->width;
    rendering_info.renderArea.extent.height = overlay->Surface()->height;
    rendering_info.layerCount = 1;
    rendering_info.viewMask = 0;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;
    rendering_info.pDepthAttachment = nullptr;
    rendering_info.pStencilAttachment = nullptr;

    vk_result = vkWaitForFences(vulkan_device_, 1, &overlay->Surface()->fences[idx], VK_TRUE, UINT64_MAX);
    VK_VALIDATE_RESULT(vk_result);

    vk_result = vkResetFences(vulkan_device_, 1, &overlay->Surface()->fences[idx]);
    VK_VALIDATE_RESULT(vk_result);

    vk_result = vkBeginCommandBuffer(overlay->Surface()->command_buffers[idx], &buffer_begin_info);
    VK_VALIDATE_RESULT(vk_result);

    if (!overlay->Surface()->first_use[idx]) {
        VkImageMemoryBarrier barrier_restore = {};
        barrier_restore.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_restore.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier_restore.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier_restore.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier_restore.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier_restore.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier_restore.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier_restore.image = overlay->Surface()->textures[idx];
        barrier_restore.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier_restore.subresourceRange.baseMipLevel = 0;
        barrier_restore.subresourceRange.levelCount = 1;
        barrier_restore.subresourceRange.baseArrayLayer = 0;
        barrier_restore.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            overlay->Surface()->command_buffers[idx],
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier_restore
        );
    }
    else {
        overlay->Surface()->first_use[idx] = false;
    }


    f_vkCmdBeginRenderingKHR(overlay->Surface()->command_buffers[idx], &rendering_info);
    ImGui_ImplVulkan_RenderDrawData(draw_data, overlay->Surface()->command_buffers[idx]);
    f_vkCmdEndRenderingKHR(overlay->Surface()->command_buffers[idx]);

    VkImageMemoryBarrier barrier_optimal = {};
    barrier_optimal.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier_optimal.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier_optimal.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier_optimal.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier_optimal.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier_optimal.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier_optimal.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier_optimal.image = overlay->Surface()->textures[idx];
    barrier_optimal.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier_optimal.subresourceRange.baseMipLevel = 0;
    barrier_optimal.subresourceRange.levelCount = 1;
    barrier_optimal.subresourceRange.baseArrayLayer = 0;
    barrier_optimal.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(overlay->Surface()->command_buffers[idx], VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier_optimal);

    vk_result = vkEndCommandBuffer(overlay->Surface()->command_buffers[idx]);
    VK_VALIDATE_RESULT(vk_result);

    VkSubmitInfo submit_info_barrier = {};
    submit_info_barrier.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info_barrier.commandBufferCount = 1;
    submit_info_barrier.pCommandBuffers = &overlay->Surface()->command_buffers[idx];

    vk_result = vkQueueSubmit(overlay->Surface()->queue, 1, &submit_info_barrier, overlay->Surface()->fences[idx]);
    VK_VALIDATE_RESULT(vk_result);

    vr::VRVulkanTextureData_t vulkanTexure{};
    vulkanTexure.m_nImage = (uintptr_t)overlay->Surface()->textures[idx];
    vulkanTexure.m_pDevice = vulkan_device_;
    vulkanTexure.m_pPhysicalDevice = vulkan_physical_device_;
    vulkanTexure.m_pInstance = vulkan_instance_;
    vulkanTexure.m_pQueue = vulkan_queue_;
    vulkanTexure.m_nQueueFamilyIndex = (uint32_t)vulkan_queue_family_;
    vulkanTexure.m_nWidth = overlay->Surface()->width;
    vulkanTexure.m_nHeight = overlay->Surface()->height;
    vulkanTexure.m_nFormat = (uint32_t)overlay->Surface()->texture_format.format;
    vulkanTexure.m_nSampleCount = VK_SAMPLE_COUNT_1_BIT;

    vr::Texture_t vrTexture{};
    vrTexture.handle = (void*)&vulkanTexure;
    vrTexture.eType = vr::TextureType_Vulkan;
    vrTexture.eColorSpace = vr::ColorSpace_Auto;

    try {
        overlay->SetTexture(vrTexture);
    }
    catch (std::exception& ex) {
        printf("Failed to set overlay texture\n%s\n\n", ex.what());
        return;
    }

    overlay->Surface()->frame_index = (overlay->Surface()->frame_index + 1) % Vulkan_Surface::ImageCount;
}

auto VulkanRenderer::DestroySurface(Vulkan_Surface* surface) const -> void
{
    VkResult vk_result = {};
    vk_result = vkQueueWaitIdle(vulkan_queue_);
    VK_VALIDATE_RESULT(vk_result);

    for (uint32_t idx = 0; idx < Vulkan_Surface::ImageCount; ++idx)
    {

        vkFreeCommandBuffers(vulkan_device_, surface->command_pools[idx], 1, &surface->command_buffers[idx]);
        vkDestroyCommandPool(vulkan_device_, surface->command_pools[idx], vulkan_allocator_);
        vkDestroyFence(vulkan_device_, surface->fences[idx], vulkan_allocator_);
        vkDestroyImageView(vulkan_device_, surface->texture_views[idx], vulkan_allocator_);
        vkDestroyImage(vulkan_device_, surface->textures[idx], vulkan_allocator_);
        vkFreeMemory(vulkan_device_, surface->texture_memories[idx], vulkan_allocator_);


        surface->command_pools[idx] = VK_NULL_HANDLE;
        surface->command_buffers[idx] = VK_NULL_HANDLE;
        surface->fences[idx] = VK_NULL_HANDLE;
        surface->texture_views[idx] = VK_NULL_HANDLE;
        surface->textures[idx] = VK_NULL_HANDLE;
        surface->texture_memories[idx] = VK_NULL_HANDLE;
    }
}

auto VulkanRenderer::Destroy() const -> void
{
    VkResult vk_result = {};

    vk_result = vkQueueWaitIdle(vulkan_queue_);
    VK_VALIDATE_RESULT(vk_result);

#ifdef ENABLE_VULKAN_VALIDATION
    auto f_vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(vulkan_instance_, "vkDestroyDebugReportCallbackEXT");
    f_vkDestroyDebugReportCallbackEXT(vulkan_instance_, nullptr, vulkan_allocator_);
#endif

    vkDestroyDevice(vulkan_device_, vulkan_allocator_);
    vkDestroyInstance(vulkan_instance_, vulkan_allocator_);
}
