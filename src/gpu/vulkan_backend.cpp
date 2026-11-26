#include "lucid/gpu/vulkan_backend.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef LUCID_HAS_VULKAN
#define LUCID_HAS_VULKAN 0
#endif
#ifndef LUCID_HAS_VULKAN_PIPELINE
#define LUCID_HAS_VULKAN_PIPELINE 0
#endif
#ifndef LUCID_HAS_SDL2
#define LUCID_HAS_SDL2 0
#endif

#if LUCID_HAS_VULKAN_PIPELINE
#include <vulkan/vulkan.h>
#if LUCID_HAS_SDL2
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_vulkan.h>
#endif
#endif

namespace lucid::gpu {
namespace {

void set_error(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

#if LUCID_HAS_VULKAN_PIPELINE
void set_vk_error(std::string* error, const char* operation, const VkResult result) {
    if (error == nullptr) return;
    *error = operation;
    *error += " failed with VkResult ";
    *error += std::to_string(static_cast<int>(result));
}

bool check_vk(const VkResult result, const char* operation, std::string* error) {
    if (result == VK_SUCCESS) return true;
    set_vk_error(error, operation, result);
    return false;
}

struct BufferResource {
    VkBuffer buffer{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
};

struct ImageResource {
    VkImage image{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
};

struct TextureResource {
    ImageResource image{};
    VkSampler sampler{VK_NULL_HANDLE};
    VkDescriptorSet descriptor{VK_NULL_HANDLE};
};
#endif

} // namespace

struct VulkanBackend::Impl {
#if LUCID_HAS_VULKAN_PIPELINE
    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    std::uint32_t queue_family{};
    VkCommandPool command_pool{VK_NULL_HANDLE};
    VkCommandBuffer frame_command_buffer{VK_NULL_HANDLE};

    VkDescriptorSetLayout descriptor_layout{VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
    VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
    VkRenderPass render_pass{VK_NULL_HANDLE};
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkFormat color_format{VK_FORMAT_R8G8B8A8_UNORM};
    VkFormat pipeline_color_format{VK_FORMAT_UNDEFINED};
    VkFormat depth_format{VK_FORMAT_D32_SFLOAT};

    ImageResource color_target{};
    ImageResource depth_target{};
    VkFramebuffer framebuffer{VK_NULL_HANDLE};
    std::uint32_t width{};
    std::uint32_t height{};

    std::unordered_map<TextureId, TextureResource> textures;
    std::vector<BufferResource> frame_buffers;
    std::vector<std::uint32_t> completed_pixels;
    std::string device_name{"Vulkan device"};
    bool discrete_or_integrated{};
    bool initialized{};
    bool frame_open{};

#if LUCID_HAS_SDL2
    SDL_Window* window{};
    bool owns_video_subsystem{};
    bool close_requested{};
    bool swapchain_dirty{};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    VkSwapchainKHR swapchain{VK_NULL_HANDLE};
    VkFormat swapchain_format{VK_FORMAT_UNDEFINED};
    VkExtent2D swapchain_extent{};
    std::vector<VkImage> swapchain_images;
    std::vector<bool> swapchain_initialized;
    VkSemaphore image_available{VK_NULL_HANDLE};
    VkSemaphore render_finished{VK_NULL_HANDLE};
    std::uint32_t acquired_image{};
#endif

    ~Impl() { cleanup(); }

    void destroy_buffer(BufferResource& resource) noexcept {
        if (device != VK_NULL_HANDLE && resource.buffer != VK_NULL_HANDLE)
            vkDestroyBuffer(device, resource.buffer, nullptr);
        if (device != VK_NULL_HANDLE && resource.memory != VK_NULL_HANDLE)
            vkFreeMemory(device, resource.memory, nullptr);
        resource = {};
    }

    void destroy_image(ImageResource& resource) noexcept {
        if (device != VK_NULL_HANDLE && resource.view != VK_NULL_HANDLE)
            vkDestroyImageView(device, resource.view, nullptr);
        if (device != VK_NULL_HANDLE && resource.image != VK_NULL_HANDLE)
            vkDestroyImage(device, resource.image, nullptr);
        if (device != VK_NULL_HANDLE && resource.memory != VK_NULL_HANDLE)
            vkFreeMemory(device, resource.memory, nullptr);
        resource = {};
    }

    void destroy_texture(TextureResource& texture) noexcept {
        if (device != VK_NULL_HANDLE && texture.descriptor != VK_NULL_HANDLE &&
            descriptor_pool != VK_NULL_HANDLE) {
            (void)vkFreeDescriptorSets(device, descriptor_pool, 1U,
                                       &texture.descriptor);
        }
        if (device != VK_NULL_HANDLE && texture.sampler != VK_NULL_HANDLE)
            vkDestroySampler(device, texture.sampler, nullptr);
        destroy_image(texture.image);
        texture = {};
    }

    void destroy_target() noexcept {
        if (device != VK_NULL_HANDLE && framebuffer != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        framebuffer = VK_NULL_HANDLE;
        destroy_image(color_target);
        destroy_image(depth_target);
        width = 0U;
        height = 0U;
    }

#if LUCID_HAS_SDL2
    void destroy_swapchain() noexcept {
        if (device != VK_NULL_HANDLE) {
            if (image_available != VK_NULL_HANDLE)
                vkDestroySemaphore(device, image_available, nullptr);
            if (render_finished != VK_NULL_HANDLE)
                vkDestroySemaphore(device, render_finished, nullptr);
            if (swapchain != VK_NULL_HANDLE)
                vkDestroySwapchainKHR(device, swapchain, nullptr);
        }
        image_available = VK_NULL_HANDLE;
        render_finished = VK_NULL_HANDLE;
        swapchain = VK_NULL_HANDLE;
        swapchain_images.clear();
        swapchain_initialized.clear();
        swapchain_format = VK_FORMAT_UNDEFINED;
        swapchain_extent = {};
    }
#endif

    void cleanup() noexcept {
        if (device != VK_NULL_HANDLE) (void)vkDeviceWaitIdle(device);
        frame_open = false;
        for (auto& resource : frame_buffers) destroy_buffer(resource);
        frame_buffers.clear();
        for (auto& entry : textures) destroy_texture(entry.second);
        textures.clear();
        destroy_target();
        if (device != VK_NULL_HANDLE && pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
        if (device != VK_NULL_HANDLE && render_pass != VK_NULL_HANDLE)
            vkDestroyRenderPass(device, render_pass, nullptr);
        render_pass = VK_NULL_HANDLE;
        if (device != VK_NULL_HANDLE && pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        pipeline_layout = VK_NULL_HANDLE;
        if (device != VK_NULL_HANDLE && descriptor_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        descriptor_pool = VK_NULL_HANDLE;
        if (device != VK_NULL_HANDLE && descriptor_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
        descriptor_layout = VK_NULL_HANDLE;
#if LUCID_HAS_SDL2
        destroy_swapchain();
        if (instance != VK_NULL_HANDLE && surface != VK_NULL_HANDLE)
            vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
#endif
        if (device != VK_NULL_HANDLE && command_pool != VK_NULL_HANDLE)
            vkDestroyCommandPool(device, command_pool, nullptr);
        command_pool = VK_NULL_HANDLE;
        if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
        if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
#if LUCID_HAS_SDL2
        if (window != nullptr) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        if (owns_video_subsystem) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            owns_video_subsystem = false;
        }
#endif
        initialized = false;
    }

    bool find_memory_type(const std::uint32_t allowed_bits,
                          const VkMemoryPropertyFlags required,
                          std::uint32_t& selected) const {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);
        for (std::uint32_t index = 0U; index < properties.memoryTypeCount; ++index) {
            const bool allowed = (allowed_bits & (1U << index)) != 0U;
            const bool has_flags =
                (properties.memoryTypes[index].propertyFlags & required) == required;
            if (allowed && has_flags) {
                selected = index;
                return true;
            }
        }
        return false;
    }

    bool create_instance(std::string* error) {
        std::vector<const char*> extensions;
#if LUCID_HAS_SDL2
        if (window != nullptr) {
            unsigned int count = 0U;
            if (SDL_Vulkan_GetInstanceExtensions(window, &count, nullptr) != SDL_TRUE ||
                count == 0U) {
                if (error != nullptr) *error = SDL_GetError();
                return false;
            }
            extensions.resize(count);
            if (SDL_Vulkan_GetInstanceExtensions(window, &count,
                                                   extensions.data()) != SDL_TRUE) {
                if (error != nullptr) *error = SDL_GetError();
                return false;
            }
        }
#endif
        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "LucidSwitch2";
        application.applicationVersion = VK_MAKE_VERSION(0, 10, 0);
        application.pEngineName = "LucidSwitch2 Native Renderer";
        application.engineVersion = VK_MAKE_VERSION(0, 10, 0);
        application.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &application;
        create_info.enabledExtensionCount =
            static_cast<std::uint32_t>(extensions.size());
        create_info.ppEnabledExtensionNames =
            extensions.empty() ? nullptr : extensions.data();
        return check_vk(vkCreateInstance(&create_info, nullptr, &instance),
                        "vkCreateInstance", error);
    }

    bool device_has_swapchain_extension(const VkPhysicalDevice candidate) const {
#if LUCID_HAS_SDL2
        std::uint32_t count = 0U;
        if (vkEnumerateDeviceExtensionProperties(candidate, nullptr,
                                                  &count, nullptr) != VK_SUCCESS)
            return false;
        std::vector<VkExtensionProperties> extensions(count);
        if (vkEnumerateDeviceExtensionProperties(candidate, nullptr,
                &count, extensions.data()) != VK_SUCCESS) return false;
        return std::any_of(extensions.begin(), extensions.end(),
            [](const VkExtensionProperties& extension) {
                return std::strcmp(extension.extensionName,
                                   VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            });
#else
        (void)candidate;
        return false;
#endif
    }

    bool select_device(std::string* error) {
        std::uint32_t device_count = 0U;
        if (!check_vk(vkEnumeratePhysicalDevices(instance, &device_count, nullptr),
                      "vkEnumeratePhysicalDevices(count)", error)) return false;
        if (device_count == 0U) {
            set_error(error, "No Vulkan physical device was found.");
            return false;
        }
        std::vector<VkPhysicalDevice> devices(device_count);
        if (!check_vk(vkEnumeratePhysicalDevices(instance, &device_count,
                                                  devices.data()),
                      "vkEnumeratePhysicalDevices(list)", error)) return false;

        int best_score = -1;
        for (const VkPhysicalDevice candidate : devices) {
#if LUCID_HAS_SDL2
            if (window != nullptr && !device_has_swapchain_extension(candidate))
                continue;
#endif
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            std::uint32_t family_count = 0U;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
            std::vector<VkQueueFamilyProperties> families(family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count,
                                                       families.data());
            for (std::uint32_t family = 0U; family < family_count; ++family) {
                if (families[family].queueCount == 0U ||
                    (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0U)
                    continue;
#if LUCID_HAS_SDL2
                if (window != nullptr) {
                    VkBool32 supports_present = VK_FALSE;
                    if (vkGetPhysicalDeviceSurfaceSupportKHR(candidate, family,
                            surface, &supports_present) != VK_SUCCESS ||
                        supports_present != VK_TRUE) continue;
                }
#endif
                int score = 1;
                if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                    score = 3;
                else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
                    score = 2;
                if (score > best_score) {
                    best_score = score;
                    physical_device = candidate;
                    queue_family = family;
                    device_name = properties.deviceName;
                    discrete_or_integrated =
                        properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
                        properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
                }
                break;
            }
        }
        if (physical_device == VK_NULL_HANDLE) {
            set_error(error, "No Vulkan graphics-capable queue family with the required presentation support was found.");
            return false;
        }
        return true;
    }

    bool create_device(std::string* error) {
        constexpr float queue_priority = 1.0F;
        VkDeviceQueueCreateInfo queue_info{};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = queue_family;
        queue_info.queueCount = 1U;
        queue_info.pQueuePriorities = &queue_priority;

#if LUCID_HAS_SDL2
        const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        const bool needs_swapchain = window != nullptr;
#else
        const bool needs_swapchain = false;
#endif
        VkDeviceCreateInfo device_info{};
        device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.queueCreateInfoCount = 1U;
        device_info.pQueueCreateInfos = &queue_info;
        device_info.enabledExtensionCount = needs_swapchain ? 1U : 0U;
#if LUCID_HAS_SDL2
        device_info.ppEnabledExtensionNames = needs_swapchain ? extensions : nullptr;
#endif
        if (!check_vk(vkCreateDevice(physical_device, &device_info, nullptr, &device),
                      "vkCreateDevice", error)) return false;
        vkGetDeviceQueue(device, queue_family, 0U, &queue);

        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                          VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = queue_family;
        if (!check_vk(vkCreateCommandPool(device, &pool_info, nullptr,
                                          &command_pool),
                      "vkCreateCommandPool", error)) return false;
        return true;
    }

    bool create_descriptor_state(std::string* error) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0U;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1U;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 1U;
        layout_info.pBindings = &binding;
        if (!check_vk(vkCreateDescriptorSetLayout(device, &layout_info, nullptr,
                                                   &descriptor_layout),
                      "vkCreateDescriptorSetLayout", error)) return false;

        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = 1024U;
        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1024U;
        pool_info.poolSizeCount = 1U;
        pool_info.pPoolSizes = &pool_size;
        if (!check_vk(vkCreateDescriptorPool(device, &pool_info, nullptr,
                                              &descriptor_pool),
                      "vkCreateDescriptorPool", error)) return false;

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_range.offset = 0U;
        push_range.size = sizeof(float) * 2U;
        VkPipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_info.setLayoutCount = 1U;
        pipeline_layout_info.pSetLayouts = &descriptor_layout;
        pipeline_layout_info.pushConstantRangeCount = 1U;
        pipeline_layout_info.pPushConstantRanges = &push_range;
        return check_vk(vkCreatePipelineLayout(device, &pipeline_layout_info,
                                                nullptr, &pipeline_layout),
                        "vkCreatePipelineLayout", error);
    }

    bool create_buffer(const VkDeviceSize size, const VkBufferUsageFlags usage,
                       const void* initial_data, BufferResource& output,
                       std::string* error) {
        if (size == 0U) {
            set_error(error, "Vulkan buffers cannot be empty.");
            return false;
        }
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (!check_vk(vkCreateBuffer(device, &buffer_info, nullptr, &output.buffer),
                      "vkCreateBuffer", error)) return false;

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, output.buffer, &requirements);
        std::uint32_t memory_type = 0U;
        const VkMemoryPropertyFlags host_flags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        const VkMemoryPropertyFlags preferred_flags =
            host_flags | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        if (!find_memory_type(requirements.memoryTypeBits, preferred_flags, memory_type) &&
            !find_memory_type(requirements.memoryTypeBits, host_flags, memory_type)) {
            set_error(error, "No host-visible coherent Vulkan memory type is available for buffer uploads.");
            destroy_buffer(output);
            return false;
        }

        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memory_type;
        if (!check_vk(vkAllocateMemory(device, &allocation, nullptr, &output.memory),
                      "vkAllocateMemory(buffer)", error) ||
            !check_vk(vkBindBufferMemory(device, output.buffer, output.memory, 0U),
                      "vkBindBufferMemory", error)) {
            destroy_buffer(output);
            return false;
        }
        if (initial_data != nullptr) {
            void* mapped = nullptr;
            if (!check_vk(vkMapMemory(device, output.memory, 0U, size, 0U, &mapped),
                          "vkMapMemory(buffer)", error)) {
                destroy_buffer(output);
                return false;
            }
            std::memcpy(mapped, initial_data, static_cast<std::size_t>(size));
            vkUnmapMemory(device, output.memory);
        }
        return true;
    }

    bool create_image(const std::uint32_t image_width,
                      const std::uint32_t image_height,
                      const VkFormat format,
                      const VkImageUsageFlags usage,
                      const VkImageAspectFlags aspect,
                      ImageResource& output,
                      std::string* error) {
        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = format;
        image_info.extent = {image_width, image_height, 1U};
        image_info.mipLevels = 1U;
        image_info.arrayLayers = 1U;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = usage;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (!check_vk(vkCreateImage(device, &image_info, nullptr, &output.image),
                      "vkCreateImage", error)) return false;

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, output.image, &requirements);
        std::uint32_t memory_type = 0U;
        if (!find_memory_type(requirements.memoryTypeBits,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memory_type)) {
            set_error(error, "No device-local Vulkan memory type is available for an image.");
            destroy_image(output);
            return false;
        }
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memory_type;
        if (!check_vk(vkAllocateMemory(device, &allocation, nullptr, &output.memory),
                      "vkAllocateMemory(image)", error) ||
            !check_vk(vkBindImageMemory(device, output.image, output.memory, 0U),
                      "vkBindImageMemory", error)) {
            destroy_image(output);
            return false;
        }

        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = output.image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format;
        view_info.subresourceRange.aspectMask = aspect;
        view_info.subresourceRange.baseMipLevel = 0U;
        view_info.subresourceRange.levelCount = 1U;
        view_info.subresourceRange.baseArrayLayer = 0U;
        view_info.subresourceRange.layerCount = 1U;
        if (!check_vk(vkCreateImageView(device, &view_info, nullptr, &output.view),
                      "vkCreateImageView", error)) {
            destroy_image(output);
            return false;
        }
        return true;
    }

    bool immediate_submit(const auto& record, std::string* error) {
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocation.commandPool = command_pool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1U;
        if (!check_vk(vkAllocateCommandBuffers(device, &allocation, &command_buffer),
                      "vkAllocateCommandBuffers(immediate)", error)) return false;

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        bool success = check_vk(vkBeginCommandBuffer(command_buffer, &begin_info),
                                "vkBeginCommandBuffer(immediate)", error);
        if (success) {
            record(command_buffer);
            success = check_vk(vkEndCommandBuffer(command_buffer),
                               "vkEndCommandBuffer(immediate)", error);
        }
        if (success) {
            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1U;
            submit.pCommandBuffers = &command_buffer;
            success = check_vk(vkQueueSubmit(queue, 1U, &submit, VK_NULL_HANDLE),
                               "vkQueueSubmit(immediate)", error) &&
                      check_vk(vkQueueWaitIdle(queue),
                               "vkQueueWaitIdle(immediate)", error);
        }
        vkFreeCommandBuffers(device, command_pool, 1U, &command_buffer);
        return success;
    }

    bool create_texture_internal(const TextureId id,
                                 const std::uint32_t texture_width,
                                 const std::uint32_t texture_height,
                                 const std::span<const std::uint32_t> pixels,
                                 const SamplerDesc& sampler,
                                 std::string* error) {
        if (texture_width == 0U || texture_height == 0U ||
            texture_width > 8192U || texture_height > 8192U) {
            set_error(error, "Vulkan texture dimensions must be between 1 and 8192.");
            return false;
        }
        const std::uint64_t pixel_count =
            static_cast<std::uint64_t>(texture_width) * texture_height;
        if (pixel_count > 16U * 1024U * 1024U ||
            pixels.size() != static_cast<std::size_t>(pixel_count)) {
            set_error(error, "Vulkan texture pixel data must exactly match a bounded image.");
            return false;
        }

        std::vector<std::uint8_t> rgba_bytes(pixels.size() * 4U);
        for (std::size_t index = 0U; index < pixels.size(); ++index) {
            const std::uint32_t pixel = pixels[index];
            rgba_bytes[index * 4U] =
                static_cast<std::uint8_t>((pixel >> 24U) & 0xFFU);
            rgba_bytes[index * 4U + 1U] =
                static_cast<std::uint8_t>((pixel >> 16U) & 0xFFU);
            rgba_bytes[index * 4U + 2U] =
                static_cast<std::uint8_t>((pixel >> 8U) & 0xFFU);
            rgba_bytes[index * 4U + 3U] =
                static_cast<std::uint8_t>(pixel & 0xFFU);
        }
        BufferResource staging{};
        const VkDeviceSize byte_size = static_cast<VkDeviceSize>(rgba_bytes.size());
        if (!create_buffer(byte_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           rgba_bytes.data(), staging, error)) return false;

        TextureResource texture{};
        if (!create_image(texture_width, texture_height,
                          VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_IMAGE_ASPECT_COLOR_BIT, texture.image, error)) {
            destroy_buffer(staging);
            return false;
        }

        const bool uploaded = immediate_submit(
            [&](const VkCommandBuffer command_buffer) {
                VkImageMemoryBarrier to_transfer{};
                to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                to_transfer.srcAccessMask = 0U;
                to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                to_transfer.image = texture.image.image;
                to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                to_transfer.subresourceRange.levelCount = 1U;
                to_transfer.subresourceRange.layerCount = 1U;
                vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, nullptr, 0U, nullptr,
                    1U, &to_transfer);

                VkBufferImageCopy copy{};
                copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copy.imageSubresource.layerCount = 1U;
                copy.imageExtent = {texture_width, texture_height, 1U};
                vkCmdCopyBufferToImage(command_buffer, staging.buffer,
                    texture.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1U, &copy);

                VkImageMemoryBarrier to_shader{};
                to_shader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                to_shader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                to_shader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                to_shader.image = texture.image.image;
                to_shader.subresourceRange = to_transfer.subresourceRange;
                vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0U, 0U, nullptr,
                    0U, nullptr, 1U, &to_shader);
            }, error);
        destroy_buffer(staging);
        if (!uploaded) {
            destroy_image(texture.image);
            return false;
        }

        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = sampler.mag_filter == TextureFilter::Linear
            ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        sampler_info.minFilter = sampler.min_filter == TextureFilter::Linear
            ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = sampler.address_u == TextureAddress::Repeat
            ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = sampler.address_v == TextureAddress::Repeat
            ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.mipLodBias = 0.0F;
        sampler_info.anisotropyEnable = VK_FALSE;
        sampler_info.maxAnisotropy = 1.0F;
        sampler_info.compareEnable = VK_FALSE;
        sampler_info.minLod = 0.0F;
        sampler_info.maxLod = 0.0F;
        sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        sampler_info.unnormalizedCoordinates = VK_FALSE;
        if (!check_vk(vkCreateSampler(device, &sampler_info, nullptr,
                                      &texture.sampler),
                      "vkCreateSampler", error)) {
            destroy_image(texture.image);
            return false;
        }

        VkDescriptorSetAllocateInfo descriptor_allocation{};
        descriptor_allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptor_allocation.descriptorPool = descriptor_pool;
        descriptor_allocation.descriptorSetCount = 1U;
        descriptor_allocation.pSetLayouts = &descriptor_layout;
        if (!check_vk(vkAllocateDescriptorSets(device, &descriptor_allocation,
                                               &texture.descriptor),
                      "vkAllocateDescriptorSets", error)) {
            destroy_texture(texture);
            return false;
        }

        VkDescriptorImageInfo image_info{};
        image_info.sampler = texture.sampler;
        image_info.imageView = texture.image.view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = texture.descriptor;
        write.dstBinding = 0U;
        write.descriptorCount = 1U;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image_info;
        vkUpdateDescriptorSets(device, 1U, &write, 0U, nullptr);

        textures.emplace(id, texture);
        return true;
    }

    bool read_shader(const char* filename, std::vector<std::uint32_t>& words,
                     std::string* error) const {
        const std::string path = std::string(LUCID_SHADER_DIR) + "/" + filename;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            if (error != nullptr) *error = "Could not open compiled Vulkan shader: " + path;
            return false;
        }
        const std::streamsize size = file.tellg();
        if (size <= 0 || (size % 4) != 0) {
            set_error(error, "Compiled Vulkan shader has an invalid SPIR-V byte length.");
            return false;
        }
        words.resize(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
        file.seekg(0, std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(words.data()), size)) {
            set_error(error, "Could not read the complete compiled SPIR-V shader.");
            return false;
        }
        if (words.empty() || words.front() != 0x07230203U) {
            set_error(error, "Compiled shader does not contain a valid SPIR-V magic word.");
            return false;
        }
        return true;
    }

    bool create_render_pass(std::string* error) {
        std::array<VkAttachmentDescription, 2U> attachments{};
        attachments[0].format = color_format;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        attachments[1].format = depth_format;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference color_reference{};
        color_reference.attachment = 0U;
        color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference depth_reference{};
        depth_reference.attachment = 1U;
        depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1U;
        subpass.pColorAttachments = &color_reference;
        subpass.pDepthStencilAttachment = &depth_reference;

        std::array<VkSubpassDependency, 2U> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0U;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dependencies[0].dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        dependencies[1].srcSubpass = 0U;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        VkRenderPassCreateInfo render_info{};
        render_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        render_info.pAttachments = attachments.data();
        render_info.subpassCount = 1U;
        render_info.pSubpasses = &subpass;
        render_info.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
        render_info.pDependencies = dependencies.data();
        return check_vk(vkCreateRenderPass(device, &render_info, nullptr, &render_pass),
                        "vkCreateRenderPass", error);
    }

    bool create_pipeline(std::string* error) {
        std::vector<std::uint32_t> vertex_words;
        std::vector<std::uint32_t> fragment_words;
        if (!read_shader("lucid.vert.spv", vertex_words, error) ||
            !read_shader("lucid.frag.spv", fragment_words, error)) return false;

        VkShaderModule vertex_module = VK_NULL_HANDLE;
        VkShaderModule fragment_module = VK_NULL_HANDLE;
        VkShaderModuleCreateInfo shader_info{};
        shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_info.codeSize = vertex_words.size() * sizeof(std::uint32_t);
        shader_info.pCode = vertex_words.data();
        if (!check_vk(vkCreateShaderModule(device, &shader_info, nullptr,
                                          &vertex_module),
                      "vkCreateShaderModule(vertex)", error)) return false;

        shader_info.codeSize = fragment_words.size() * sizeof(std::uint32_t);
        shader_info.pCode = fragment_words.data();
        if (!check_vk(vkCreateShaderModule(device, &shader_info, nullptr,
                                          &fragment_module), "vkCreateShaderModule(fragment)",
                      error)) {
            vkDestroyShaderModule(device, vertex_module, nullptr);
            return false;
        }

        std::array<VkPipelineShaderStageCreateInfo, 2U> stages{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex_module;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment_module;
        stages[1].pName = "main";

        VkVertexInputBindingDescription binding{};
        binding.binding = 0U;
        binding.stride = static_cast<std::uint32_t>(sizeof(GraphicsVertex));
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        std::array<VkVertexInputAttributeDescription, 3U> attributes{};
        attributes[0] = {0U, 0U, VK_FORMAT_R32G32B32_SFLOAT,
                          static_cast<std::uint32_t>(offsetof(GraphicsVertex, x))};
        attributes[1] = {1U, 0U, VK_FORMAT_R32_UINT,
                          static_cast<std::uint32_t>(offsetof(GraphicsVertex, rgba))};
        attributes[2] = {2U, 0U, VK_FORMAT_R32G32_SFLOAT,
                          static_cast<std::uint32_t>(offsetof(GraphicsVertex, u))};

        VkPipelineVertexInputStateCreateInfo vertex_input{};
        vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input.vertexBindingDescriptionCount = 1U;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount =
            static_cast<std::uint32_t>(attributes.size());
        vertex_input.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        assembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1U;
        viewport.scissorCount = 1U;

        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.depthClampEnable = VK_FALSE;
        raster.rasterizerDiscardEnable = VK_FALSE;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.depthBiasEnable = VK_FALSE;
        raster.lineWidth = 1.0F;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depth.depthBoundsTestEnable = VK_FALSE;
        depth.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState blend_attachment{};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        blend_attachment.blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1U;
        blend.pAttachments = &blend_attachment;

        constexpr std::array<VkDynamicState, 2U> dynamic_states{
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size());
        dynamic.pDynamicStates = dynamic_states.data();

        VkGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.stageCount = static_cast<std::uint32_t>(stages.size());
        pipeline_info.pStages = stages.data();
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &assembly;
        pipeline_info.pViewportState = &viewport;
        pipeline_info.pRasterizationState = &raster;
        pipeline_info.pMultisampleState = &multisample;
        pipeline_info.pDepthStencilState = &depth;
        pipeline_info.pColorBlendState = &blend;
        pipeline_info.pDynamicState = &dynamic;
        pipeline_info.layout = pipeline_layout;
        pipeline_info.renderPass = render_pass;
        pipeline_info.subpass = 0U;

        const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE,
            1U, &pipeline_info, nullptr, &pipeline);
        vkDestroyShaderModule(device, fragment_module, nullptr);
        vkDestroyShaderModule(device, vertex_module, nullptr);
        return check_vk(result, "vkCreateGraphicsPipelines", error);
    }

    bool ensure_pipeline(std::string* error) {
        if (render_pass != VK_NULL_HANDLE && pipeline_color_format != color_format) {
            destroy_target();
            if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
            vkDestroyRenderPass(device, render_pass, nullptr);
            render_pass = VK_NULL_HANDLE;
            pipeline_color_format = VK_FORMAT_UNDEFINED;
        }
        if (pipeline != VK_NULL_HANDLE && render_pass != VK_NULL_HANDLE) return true;
        VkFormatProperties depth_properties{};
        vkGetPhysicalDeviceFormatProperties(physical_device, VK_FORMAT_D32_SFLOAT,
                                             &depth_properties);
        if ((depth_properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U) {
            depth_format = VK_FORMAT_D32_SFLOAT;
        } else {
            vkGetPhysicalDeviceFormatProperties(physical_device,
                VK_FORMAT_D24_UNORM_S8_UINT, &depth_properties);
            if ((depth_properties.optimalTilingFeatures &
                 VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0U) {
                set_error(error, "The Vulkan device has no supported depth attachment format.");
                return false;
            }
            depth_format = VK_FORMAT_D24_UNORM_S8_UINT;
        }
        if (!create_render_pass(error)) return false;
        if (!create_pipeline(error)) {
            vkDestroyRenderPass(device, render_pass, nullptr);
            render_pass = VK_NULL_HANDLE;
            return false;
        }
        pipeline_color_format = color_format;
        return true;
    }

    bool create_target(const std::uint32_t requested_width,
                       const std::uint32_t requested_height,
                       std::string* error) {
        if (framebuffer != VK_NULL_HANDLE && width == requested_width &&
            height == requested_height && color_target.image != VK_NULL_HANDLE)
            return true;

        destroy_target();
        if (!create_image(requested_width, requested_height, color_format,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, color_target, error)) return false;

        VkImageAspectFlags depth_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (depth_format == VK_FORMAT_D24_UNORM_S8_UINT)
            depth_aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        if (!create_image(requested_width, requested_height, depth_format,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                depth_aspect, depth_target, error)) {
            destroy_image(color_target);
            return false;
        }

        const std::array<VkImageView, 2U> attachments{
            color_target.view, depth_target.view
        };
        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = render_pass;
        framebuffer_info.attachmentCount =
            static_cast<std::uint32_t>(attachments.size());
        framebuffer_info.pAttachments = attachments.data();
        framebuffer_info.width = requested_width;
        framebuffer_info.height = requested_height;
        framebuffer_info.layers = 1U;
        if (!check_vk(vkCreateFramebuffer(device, &framebuffer_info, nullptr,
                                           &framebuffer),
                      "vkCreateFramebuffer", error)) {
            destroy_image(color_target);
            destroy_image(depth_target);
            return false;
        }
        width = requested_width;
        height = requested_height;
        return true;
    }

#if LUCID_HAS_SDL2
    bool create_swapchain(std::string* error) {
        if (!check_vk(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(swapchain)", error))
            return false;
        if (window == nullptr || surface == VK_NULL_HANDLE) {
            set_error(error, "A Vulkan window and surface are required for swapchain creation.");
            return false;
        }
        VkSurfaceCapabilitiesKHR capabilities{};
        if (!check_vk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                physical_device, surface, &capabilities),
                "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", error)) return false;
        if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0U) {
            set_error(error, "The Vulkan surface does not support transfer-destination swapchain images.");
            return false;
        }

        std::uint32_t format_count = 0U;
        if (!check_vk(vkGetPhysicalDeviceSurfaceFormatsKHR(
                physical_device, surface, &format_count, nullptr),
                "vkGetPhysicalDeviceSurfaceFormatsKHR(count)", error) ||
            format_count == 0U) {
            set_error(error, "The Vulkan surface exposes no presentation formats.");
            return false;
        }
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        if (!check_vk(vkGetPhysicalDeviceSurfaceFormatsKHR(
                physical_device, surface, &format_count, formats.data()),
                "vkGetPhysicalDeviceSurfaceFormatsKHR(list)", error)) return false;

        VkSurfaceFormatKHR selected = formats.front();
        const std::array<VkFormat, 2U> preferred{
            VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM
        };
        bool found_format = false;
        for (const VkFormat wanted : preferred) {
            const auto it = std::find_if(formats.begin(), formats.end(),
                [wanted](const VkSurfaceFormatKHR& candidate) {
                    return candidate.format == wanted &&
                        candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                });
            if (it != formats.end()) {
                selected = *it;
                found_format = true;
                break;
            }
        }
        if (!found_format && selected.format == VK_FORMAT_UNDEFINED) {
            selected.format = VK_FORMAT_R8G8B8A8_UNORM;
            selected.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        }
        VkFormatProperties format_properties{};
        vkGetPhysicalDeviceFormatProperties(physical_device, selected.format,
                                              &format_properties);
        if ((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) == 0U) {
            set_error(error, "The selected swapchain format cannot receive a Vulkan blit.");
            return false;
        }
        vkGetPhysicalDeviceFormatProperties(physical_device, selected.format,
                                             &format_properties);
        if ((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) == 0U) {
            set_error(error, "The selected swapchain format cannot be used as a Vulkan blit source.");
            return false;
        }

        int drawable_width = 0;
        int drawable_height = 0;
        SDL_Vulkan_GetDrawableSize(window, &drawable_width, &drawable_height);
        if (drawable_width <= 0 || drawable_height <= 0) {
            set_error(error, "The Vulkan window has no drawable client area.");
            return false;
        }
        VkExtent2D extent = capabilities.currentExtent;
        if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
            extent.width = static_cast<std::uint32_t>(drawable_width);
            extent.height = static_cast<std::uint32_t>(drawable_height);
            extent.width = std::clamp(extent.width,
                capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height = std::clamp(extent.height,
                capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }

        std::uint32_t image_count = capabilities.minImageCount + 1U;
        if (capabilities.maxImageCount > 0U)
            image_count = std::min(image_count, capabilities.maxImageCount);

        VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if ((capabilities.supportedCompositeAlpha & composite) == 0U) {
            constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4U> candidates{
                VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
            };
            const auto it = std::find_if(candidates.begin(), candidates.end(),
                [&](const VkCompositeAlphaFlagBitsKHR value) {
                    return (capabilities.supportedCompositeAlpha & value) != 0U;
                });
            if (it == candidates.end()) {
                set_error(error, "The Vulkan surface exposes no supported composite alpha mode.");
                return false;
            }
            composite = *it;
        }

        VkSwapchainCreateInfoKHR swapchain_info{};
        swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchain_info.surface = surface;
        swapchain_info.minImageCount = image_count;
        swapchain_info.imageFormat = selected.format;
        swapchain_info.imageColorSpace = selected.colorSpace;
        swapchain_info.imageExtent = extent;
        swapchain_info.imageArrayLayers = 1U;
        swapchain_info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchain_info.preTransform = capabilities.currentTransform;
        swapchain_info.compositeAlpha = composite;
        swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapchain_info.clipped = VK_TRUE;
        swapchain_info.oldSwapchain = swapchain;

        VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
        if (!check_vk(vkCreateSwapchainKHR(device, &swapchain_info, nullptr,
                                            &new_swapchain),
                      "vkCreateSwapchainKHR", error)) return false;

        std::uint32_t new_image_count = 0U;
        VkResult result = vkGetSwapchainImagesKHR(device, new_swapchain,
                                                   &new_image_count, nullptr);
        if (!check_vk(result, "vkGetSwapchainImagesKHR(count)", error)) {
            vkDestroySwapchainKHR(device, new_swapchain, nullptr);
            return false;
        }
        std::vector<VkImage> new_images(new_image_count);
        result = vkGetSwapchainImagesKHR(device, new_swapchain,
                                          &new_image_count, new_images.data());
        if (!check_vk(result, "vkGetSwapchainImagesKHR(list)", error)) {
            vkDestroySwapchainKHR(device, new_swapchain, nullptr);
            return false;
        }

        if (image_available != VK_NULL_HANDLE)
            vkDestroySemaphore(device, image_available, nullptr);
        if (render_finished != VK_NULL_HANDLE)
            vkDestroySemaphore(device, render_finished, nullptr);
        if (swapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = new_swapchain;
        swapchain_images = std::move(new_images);
        swapchain_initialized.assign(swapchain_images.size(), false);
        swapchain_format = selected.format;
        swapchain_extent = extent;
        color_format = swapchain_format;

        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (!check_vk(vkCreateSemaphore(device, &semaphore_info, nullptr,
                                         &image_available),
                      "vkCreateSemaphore(image-available)", error) ||
            !check_vk(vkCreateSemaphore(device, &semaphore_info, nullptr,
                                         &render_finished),
                      "vkCreateSemaphore(render-finished)", error)) return false;
        swapchain_dirty = false;
        return true;
    }

    bool acquire_swapchain_image(std::string* error) {
        VkResult result = vkAcquireNextImageKHR(device, swapchain,
            std::numeric_limits<std::uint64_t>::max(), image_available,
            VK_NULL_HANDLE, &acquired_image);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchain_dirty = true;
            if (!create_swapchain(error)) return false;
            result = vkAcquireNextImageKHR(device, swapchain,
                std::numeric_limits<std::uint64_t>::max(), image_available,
                VK_NULL_HANDLE, &acquired_image);
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            set_vk_error(error, "vkAcquireNextImageKHR", result);
            return false;
        }
        if (result == VK_SUBOPTIMAL_KHR) swapchain_dirty = true;
        return true;
    }
#endif

    bool initialize_backend(std::string* error) {
        if (initialized) return true;
        if (!create_instance(error)) return false;
#if LUCID_HAS_SDL2
        if (window != nullptr &&
            SDL_Vulkan_CreateSurface(window, instance, &surface) != SDL_TRUE) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
#endif
        if (!select_device(error) || !create_device(error) ||
            !create_descriptor_state(error)) return false;
#if LUCID_HAS_SDL2
        if (window != nullptr && !create_swapchain(error)) return false;
#endif
        color_format = 
#if LUCID_HAS_SDL2
            window != nullptr ? swapchain_format :
#endif
            VK_FORMAT_R8G8B8A8_UNORM;

        constexpr std::array<std::uint32_t, 1U> white_pixel{0xFFFFFFFFU};
        if (!create_texture_internal(0U, 1U, 1U, white_pixel, {}, error))
            return false;
        initialized = true;
        return true;
    }
#else
    bool initialized{};
    std::string device_name{"Vulkan native pipeline not compiled"};
#endif
};

VulkanBackend::VulkanBackend() : impl_(std::make_unique<Impl>()) {}
VulkanBackend::~VulkanBackend() = default;
VulkanBackend::VulkanBackend(VulkanBackend&&) noexcept = default;
VulkanBackend& VulkanBackend::operator=(VulkanBackend&&) noexcept = default;

bool VulkanBackend::compiled_with_vulkan() noexcept {
#if LUCID_HAS_VULKAN
    return true;
#else
    return false;
#endif
}

bool VulkanBackend::compiled_with_native_pipeline() noexcept {
#if LUCID_HAS_VULKAN_PIPELINE
    return true;
#else
    return false;
#endif
}

bool VulkanBackend::compiled_with_vulkan_window() noexcept {
#if LUCID_HAS_VULKAN_PIPELINE && LUCID_HAS_SDL2
    return true;
#else
    return false;
#endif
}

bool VulkanBackend::initialize(std::string* error) {
#if LUCID_HAS_VULKAN_PIPELINE
#if LUCID_HAS_SDL2
    if (impl_->window != nullptr) {
        set_error(error, "This Vulkan backend was initialized for a window.");
        return false;
    }
#endif
    return impl_->initialize_backend(error);
#else
    set_error(error, "Native Vulkan graphics was not compiled. Install the Vulkan SDK and glslc, then reconfigure CMake.");
    return false;
#endif
}

bool VulkanBackend::initialize_window(const std::string_view title,
                                      const std::uint32_t width,
                                      const std::uint32_t height,
                                      std::string* error) {
#if LUCID_HAS_VULKAN_PIPELINE && LUCID_HAS_SDL2
    if (impl_->initialized || impl_->window != nullptr) {
        set_error(error, "The Vulkan window backend has already been initialized.");
        return false;
    }
    if (width == 0U || height == 0U ||
        width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        set_error(error, "Vulkan window dimensions must be nonzero and fit SDL integer sizes.");
        return false;
    }
    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0U) {
        SDL_SetMainReady();
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
        impl_->owns_video_subsystem = true;
    }
    const std::string window_title(title);
    impl_->window = SDL_CreateWindow(window_title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        static_cast<int>(width), static_cast<int>(height),
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (impl_->window == nullptr) {
        if (error != nullptr) *error = SDL_GetError();
        if (impl_->owns_video_subsystem) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            impl_->owns_video_subsystem = false;
        }
        return false;
    }
    impl_->close_requested = false;
    if (!impl_->initialize_backend(error)) return false;
    return true;
#else
    (void)title; (void)width; (void)height;
    set_error(error, "Vulkan swapchain presentation requires Vulkan, glslc, and SDL2 at configure time.");
    return false;
#endif
}

void VulkanBackend::poll_window_events() noexcept {
#if LUCID_HAS_VULKAN_PIPELINE && LUCID_HAS_SDL2
    if (impl_ == nullptr || impl_->window == nullptr) return;
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT ||
            (event.type == SDL_WINDOWEVENT &&
             event.window.event == SDL_WINDOWEVENT_CLOSE)) {
            impl_->close_requested = true;
        } else if (event.type == SDL_WINDOWEVENT &&
                  (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                   event.window.event == SDL_WINDOWEVENT_RESIZED)) {
            impl_->swapchain_dirty = true;
        }
    }
#endif
}

bool VulkanBackend::window_open() const noexcept {
#if LUCID_HAS_VULKAN_PIPELINE && LUCID_HAS_SDL2
    return impl_ != nullptr && impl_->window != nullptr &&
           !impl_->close_requested;
#else
    return false;
#endif
}

bool VulkanBackend::begin_frame(const std::uint32_t width,
                                const std::uint32_t height,
                                std::string* error) {
#if LUCID_HAS_VULKAN_PIPELINE
    if (!impl_->initialized) {
        set_error(error, "Native Vulkan backend is not initialized.");
        return false;
    }
    if (impl_->frame_open) {
        set_error(error, "The previous Vulkan frame has not been presented.");
        return false;
    }
    const std::uint64_t pixel_count =
        static_cast<std::uint64_t>(width) * height;
    if (width == 0U || height == 0U || width > 8192U || height > 8192U ||
        pixel_count > 16U * 1024U * 1024U) {
        set_error(error, "Vulkan framebuffer dimensions exceed the 8192 / 16 megapixel limits.");
        return false;
    }
#if LUCID_HAS_SDL2
    if (impl_->window != nullptr) {
        if (impl_->swapchain_dirty && !impl_->create_swapchain(error)) return false;
        impl_->color_format = impl_->swapchain_format;
        if (!impl_->acquire_swapchain_image(error)) return false;
    }
#endif
    if (!impl_->ensure_pipeline(error) ||
        !impl_->create_target(width, height, error)) return false;

    VkCommandBufferAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = impl_->command_pool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1U;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    if (!check_vk(vkAllocateCommandBuffers(impl_->device, &allocation,
                                            &command_buffer),
                  "vkAllocateCommandBuffers(frame)", error)) return false;

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!check_vk(vkBeginCommandBuffer(command_buffer, &begin_info),
                  "vkBeginCommandBuffer(frame)", error)) {
        vkFreeCommandBuffers(impl_->device, impl_->command_pool, 1U, &command_buffer);
        return false;
    }

    std::array<VkClearValue, 2U> clears{};
    clears[0].color.float32[0] = 0.0F;
    clears[0].color.float32[1] = 0.0F;
    clears[0].color.float32[2] = 0.0F;
    clears[0].color.float32[3] = 1.0F;
    clears[1].depthStencil.depth = 1.0F;
    clears[1].depthStencil.stencil = 0U;
    VkRenderPassBeginInfo render_begin{};
    render_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_begin.renderPass = impl_->render_pass;
    render_begin.framebuffer = impl_->framebuffer;
    render_begin.renderArea.offset = {0, 0};
    render_begin.renderArea.extent = {width, height};
    render_begin.clearValueCount = static_cast<std::uint32_t>(clears.size());
    render_begin.pClearValues = clears.data();
    vkCmdBeginRenderPass(command_buffer, &render_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {width, height};
    vkCmdSetViewport(command_buffer, 0U, 1U, &viewport);
    vkCmdSetScissor(command_buffer, 0U, 1U, &scissor);
    impl_->frame_command_buffer = command_buffer;
    impl_->frame_open = true;
    return true;
#else
    (void)width; (void)height;
    set_error(error, "Native Vulkan pipeline support was not compiled into this build.");
    return false;
#endif
}

bool VulkanBackend::clear(const std::uint32_t rgba, std::string* error) {
#if LUCID_HAS_VULKAN_PIPELINE
    if (!impl_->frame_open) {
        set_error(error, "Vulkan clear requires an open frame.");
        return false;
    }
    VkClearAttachment attachment{};
    attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    attachment.colorAttachment = 0U;
    attachment.clearValue.color.float32[0] =
        static_cast<float>((rgba >> 24U) & 0xFFU) / 255.0F;
    attachment.clearValue.color.float32[1] =
        static_cast<float>((rgba >> 16U) & 0xFFU) / 255.0F;
    attachment.clearValue.color.float32[2] =
        static_cast<float>((rgba >> 8U) & 0xFFU) / 255.0F;
    attachment.clearValue.color.float32[3] =
        static_cast<float>(rgba & 0xFFU) / 255.0F;
    VkClearRect rect{};
    rect.rect.offset = {0, 0};
    rect.rect.extent = {impl_->width, impl_->height};
    rect.baseArrayLayer = 0U;
    rect.layerCount = 1U;
    vkCmdClearAttachments(impl_->frame_command_buffer, 1U, &attachment, 1U, &rect);
    return true;
#else
    (void)rgba;
    set_error(error, "Native Vulkan pipeline support was not compiled into this build.");
    return false;
#endif
}

bool VulkanBackend::clear_depth(const float depth, std::string* error) {
#if LUCID_HAS_VULKAN_PIPELINE
    if (!impl_->frame_open || !std::isfinite(depth) || depth < 0.0F || depth > 1.0F) {
        set_error(error, "Vulkan depth clear requires an open frame and a finite depth in [0,1].");
        return false;
    }
    VkClearAttachment attachment{};
    attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    attachment.clearValue.depthStencil.depth = depth;
    attachment.clearValue.depthStencil.stencil = 0U;
    VkClearRect rect{};
    rect.rect.offset = {0, 0};
    rect.rect.extent = {impl_->width, impl_->height};
    rect.baseArrayLayer = 0U;
    rect.layerCount = 1U;
    vkCmdClearAttachments(impl_->frame_command_buffer, 1U, &attachment, 1U, &rect);
    return true;
#else
    (void)depth;
    set_error(error, "Native Vulkan pipeline support was not compiled into this build.");
    return false;
#endif
}

bool VulkanBackend::draw_rect(const GraphicsRect& rect,
                              const std::uint32_t rgba,
                              std::string* error) {
    if (rect.width == 0U || rect.height == 0U) {
        set_error(error, "Vulkan rectangle draws require nonzero dimensions.");
        return false;
    }
    const float x0 = static_cast<float>(rect.x);
    const float y0 = static_cast<float>(rect.y);
    const float x1 = static_cast<float>(static_cast<std::uint64_t>(rect.x) + rect.width);
    const float y1 = static_cast<float>(static_cast<std::uint64_t>(rect.y) + rect.height);
    const std::array<GraphicsVertex, 4U> vertices{{
        {x0, y0, 0.0F, rgba, 0.0F, 0.0F},
        {x1, y0, 0.0F, rgba, 0.0F, 0.0F},
        {x1, y1, 0.0F, rgba, 0.0F, 0.0F},
        {x0, y1, 0.0F, rgba, 0.0F, 0.0F}
    }};
    constexpr std::array<std::uint32_t, 6U> indices{0U, 1U, 2U, 0U, 2U, 3U};
    return draw_indexed(vertices, indices, 0U, error);
}

bool VulkanBackend::create_texture(const TextureId id,
                                   const std::uint32_t width,
                                   const std::uint32_t height,
                                   const std::span<const std::uint32_t> pixels,
                                   const SamplerDesc& sampler,
                                   std::string* error) {
#if LUCID_HAS_VULKAN_PIPELINE
    if (!impl_->initialized || impl_->frame_open || id == 0U ||
        impl_->textures.contains(id)) {
        set_error(error, "Vulkan texture creation requires an initialized idle backend and a new nonzero ID.");
        return false;
    }
    return impl_->create_texture_internal(id, width, height, pixels, sampler, error);
#else
    (void)id; (void)width; (void)height; (void)pixels; (void)sampler;
    set_error(error, "Native Vulkan pipeline support was not compiled into this build.");
    return false;
#endif
}

bool VulkanBackend::destroy_texture(const TextureId id, std::string* error) {
#if LUCID_HAS_VULKAN_PIPELINE
    if (!impl_->initialized || impl_->frame_open || id == 0U) {
        set_error(error, "Vulkan texture destruction requires an idle frame and a nonzero texture ID.");
        return false;
    }
    const auto it = impl_->textures.find(id);
    if (it == impl_->textures.end()) {
        set_error(error, "The Vulkan texture ID is not registered.");
        return false;
    }
    impl_->destroy_texture(it->second);
    impl_->textures.erase(it);
    return true;
#else
    (void)id;
    set_error(error, "Native Vulkan pipeline support was not compiled into this build.");
    return false;
#endif
}

bool VulkanBackend::draw_indexed(
    const std::span<const GraphicsVertex> vertices,
    const std::span<const std::uint32_t> indices,
    const TextureId texture, std::string* error) {
#if LUCID_HAS_VULKAN_PIPELINE
    if (!impl_->frame_open || vertices.empty() || indices.empty() ||
        indices.size() % 3U != 0U ||
        vertices.size() > 1000000U || indices.size() > 3000000U ||
        indices.size() > static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max()) ||
        !impl_->textures.contains(texture)) {
        set_error(error, "Vulkan indexed draw requires valid buffers, triangle indices, and a registered texture.");
        return false;
    }
    for (const std::uint32_t index : indices) {
        if (index >= vertices.size()) {
            set_error(error, "Vulkan indexed draw contains an out-of-range vertex index.");
            return false;
        }
    }
    for (const GraphicsVertex& vertex : vertices) {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
            !std::isfinite(vertex.z) || !std::isfinite(vertex.u) ||
            !std::isfinite(vertex.v) || vertex.z < 0.0F || vertex.z > 1.0F) {
            set_error(error, "Vulkan vertex attributes must be finite and depth must be in [0,1].");
            return false;
        }
    }

    BufferResource vertex_buffer{};
    BufferResource index_buffer{};
    const VkDeviceSize vertex_bytes =
        static_cast<VkDeviceSize>(vertices.size_bytes());
    const VkDeviceSize index_bytes =
        static_cast<VkDeviceSize>(indices.size_bytes());
    if (!impl_->create_buffer(vertex_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              vertices.data(), vertex_buffer, error)) return false;
    if (!impl_->create_buffer(index_bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                              indices.data(), index_buffer, error)) {
        impl_->destroy_buffer(vertex_buffer);
        return false;
    }
    impl_->frame_buffers.push_back(vertex_buffer);
    impl_->frame_buffers.push_back(index_buffer);

    const VkDeviceSize offset = 0U;
    vkCmdBindPipeline(impl_->frame_command_buffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS, impl_->pipeline);
    vkCmdBindVertexBuffers(impl_->frame_command_buffer, 0U, 1U,
                           &vertex_buffer.buffer, &offset);
    vkCmdBindIndexBuffer(impl_->frame_command_buffer, index_buffer.buffer,
                         0U, VK_INDEX_TYPE_UINT32);
    const VkDescriptorSet descriptor = impl_->textures.at(texture).descriptor;
    vkCmdBindDescriptorSets(impl_->frame_command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS, impl_->pipeline_layout,
        0U, 1U, &descriptor, 0U, nullptr);
    const std::array<float, 2U> target_size{
        static_cast<float>(impl_->width), static_cast<float>(impl_->height)
    };
    vkCmdPushConstants(impl_->frame_command_buffer, impl_->pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT, 0U,
        static_cast<std::uint32_t>(sizeof(target_size)), target_size.data());
    vkCmdDrawIndexed(impl_->frame_command_buffer,
        static_cast<std::uint32_t>(indices.size()), 1U, 0U, 0, 0U);
    return true;
#else
    (void)vertices; (void)indices; (void)texture;
    set_error(error, "Native Vulkan pipeline support was not compiled into this build.");
    return false;
#endif
}

bool VulkanBackend::present_frame(std::string* error) {
#if LUCID_HAS_VULKAN_PIPELINE
    if (!impl_->frame_open) {
        set_error(error, "Vulkan present requires an open frame.");
        return false;
    }
    vkCmdEndRenderPass(impl_->frame_command_buffer);

    BufferResource readback{};
    const VkDeviceSize readback_size =
        static_cast<VkDeviceSize>(impl_->width) * impl_->height * 4U;
    if (!impl_->create_buffer(readback_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              nullptr, readback, error)) {
        vkFreeCommandBuffers(impl_->device, impl_->command_pool, 1U,
                             &impl_->frame_command_buffer);
        impl_->frame_command_buffer = VK_NULL_HANDLE;
        impl_->frame_open = false;
        return false;
    }

    VkBufferImageCopy image_copy{};
    image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_copy.imageSubresource.layerCount = 1U;
    image_copy.imageExtent = {impl_->width, impl_->height, 1U};
    vkCmdCopyImageToBuffer(impl_->frame_command_buffer,
        impl_->color_target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        readback.buffer, 1U, &image_copy);

    VkBufferMemoryBarrier readback_barrier{};
    readback_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    readback_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    readback_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    readback_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readback_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readback_barrier.buffer = readback.buffer;
    readback_barrier.offset = 0U;
    readback_barrier.size = readback_size;
    vkCmdPipelineBarrier(impl_->frame_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, 0U, 0U, nullptr, 1U, &readback_barrier,
        0U, nullptr);

#if LUCID_HAS_SDL2
    const bool has_window = impl_->window != nullptr;
    if (has_window) {
        VkImage swap_image = impl_->swapchain_images[impl_->acquired_image];
        VkImageMemoryBarrier to_transfer{};
        to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_transfer.srcAccessMask = 0U;
        to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_transfer.oldLayout = impl_->swapchain_initialized[impl_->acquired_image]
            ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
        to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.image = swap_image;
        to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_transfer.subresourceRange.levelCount = 1U;
        to_transfer.subresourceRange.layerCount = 1U;
        vkCmdPipelineBarrier(impl_->frame_command_buffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0U, 0U, nullptr, 0U, nullptr, 1U, &to_transfer);

        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = 1U;
        blit.srcOffsets[1] = {
            static_cast<std::int32_t>(impl_->width),
            static_cast<std::int32_t>(impl_->height), 1
        };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.layerCount = 1U;
        blit.dstOffsets[1] = {
            static_cast<std::int32_t>(impl_->swapchain_extent.width),
            static_cast<std::int32_t>(impl_->swapchain_extent.height), 1
        };
        vkCmdBlitImage(impl_->frame_command_buffer,
            impl_->color_target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            swap_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1U, &blit, VK_FILTER_NEAREST);

        VkImageMemoryBarrier to_present{};
        to_present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_present.dstAccessMask = 0U;
        to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_present.image = swap_image;
        to_present.subresourceRange = to_transfer.subresourceRange;
        vkCmdPipelineBarrier(impl_->frame_command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0U, 0U, nullptr, 0U, nullptr, 1U, &to_present);
    }
#else
    constexpr bool has_window = false;
#endif

    bool success = check_vk(vkEndCommandBuffer(impl_->frame_command_buffer),
                            "vkEndCommandBuffer(frame)", error);
    if (success) {
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
#if LUCID_HAS_SDL2
        if (has_window) {
            submit.waitSemaphoreCount = 1U;
            submit.pWaitSemaphores = &impl_->image_available;
            submit.pWaitDstStageMask = &wait_stage;
            submit.signalSemaphoreCount = 1U;
            submit.pSignalSemaphores = &impl_->render_finished;
        }
#else
        (void)wait_stage;
#endif
        submit.commandBufferCount = 1U;
        submit.pCommandBuffers = &impl_->frame_command_buffer;
        success = check_vk(vkQueueSubmit(impl_->queue, 1U, &submit, VK_NULL_HANDLE),
                           "vkQueueSubmit(frame)", error) &&
                  check_vk(vkQueueWaitIdle(impl_->queue),
                           "vkQueueWaitIdle(frame)", error);
    }

#if LUCID_HAS_SDL2
    if (success && has_window) {
        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1U;
        present.pWaitSemaphores = &impl_->render_finished;
        present.swapchainCount = 1U;
        present.pSwapchains = &impl_->swapchain;
        present.pImageIndices = &impl_->acquired_image;
        const VkResult result = vkQueuePresentKHR(impl_->queue, &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            impl_->swapchain_dirty = true;
        } else if (!check_vk(result, "vkQueuePresentKHR", error)) {
            success = false;
        }
        if (success)
            success = check_vk(vkQueueWaitIdle(impl_->queue),
                               "vkQueueWaitIdle(present)", error);
        impl_->swapchain_initialized[impl_->acquired_image] = true;
    }
#endif

    if (success) {
        void* mapped = nullptr;
        success = check_vk(vkMapMemory(impl_->device, readback.memory, 0U,
            readback_size, 0U, &mapped), "vkMapMemory(readback)", error);
        if (success) {
            const auto* bytes = static_cast<const std::uint8_t*>(mapped);
            const std::size_t pixel_count =
                static_cast<std::size_t>(impl_->width) * impl_->height;
            impl_->completed_pixels.resize(pixel_count);
            const bool bgra = impl_->color_format == VK_FORMAT_B8G8R8A8_UNORM ||
                              impl_->color_format == VK_FORMAT_B8G8R8A8_SRGB;
            for (std::size_t index = 0U; index < pixel_count; ++index) {
                const std::uint32_t r = bytes[index * 4U + (bgra ? 2U : 0U)];
                const std::uint32_t g = bytes[index * 4U + 1U];
                const std::uint32_t b = bytes[index * 4U + (bgra ? 0U : 2U)];
                const std::uint32_t a = bytes[index * 4U + 3U];
                impl_->completed_pixels[index] =
                    (r << 24U) | (g << 16U) | (b << 8U) | a;
            }
            vkUnmapMemory(impl_->device, readback.memory);
        }
    }

    impl_->destroy_buffer(readback);
    for (auto& resource : impl_->frame_buffers) impl_->destroy_buffer(resource);
    impl_->frame_buffers.clear();
    vkFreeCommandBuffers(impl_->device, impl_->command_pool, 1U,
                         &impl_->frame_command_buffer);
    impl_->frame_command_buffer = VK_NULL_HANDLE;
    impl_->frame_open = false;
    return success;
#else
    set_error(error, "Native Vulkan pipeline support was not compiled into this build.");
    return false;
#endif
}

bool VulkanBackend::readback(const std::span<std::uint32_t> pixels,
                             std::string* error) const {
#if LUCID_HAS_VULKAN_PIPELINE
    if (impl_->frame_open || impl_->completed_pixels.empty() ||
        pixels.size() != impl_->completed_pixels.size()) {
        set_error(error, "Vulkan readback requires a completed frame and an exactly sized pixel buffer.");
        return false;
    }
    std::copy(impl_->completed_pixels.begin(), impl_->completed_pixels.end(),
              pixels.begin());
    return true;
#else
    (void)pixels;
    set_error(error, "Native Vulkan pipeline support was not compiled into this build.");
    return false;
#endif
}

GraphicsCapabilities VulkanBackend::capabilities() const noexcept {
#if LUCID_HAS_VULKAN_PIPELINE
    return {
        impl_ != nullptr && impl_->discrete_or_integrated,
        true,
        true,
        true,
        compiled_with_vulkan_window(),
        "Native Vulkan graphics pipeline with GPU vertex/index buffers, sampled images, depth testing, off-screen readback, and optional SDL2 swapchain presentation.",
        true
    };
#elif LUCID_HAS_VULKAN
    return {false, false, false, false, false,
            "Vulkan loader found, but glslc was not available to build the native shader pipeline.",
            false};
#else
    return {false, false, false, false, false,
            "Vulkan SDK and shader compiler not available; use the software reference renderer.",
            false};
#endif
}

std::string_view VulkanBackend::name() const noexcept {
    return impl_ != nullptr ? std::string_view(impl_->device_name)
                            : std::string_view("Vulkan backend unavailable");
}

} // namespace lucid::gpu
