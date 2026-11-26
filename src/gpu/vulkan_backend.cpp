#include "lucid/gpu/vulkan_backend.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#if LUCID_HAS_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace lucid::gpu {
namespace {
void set_error(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

#if LUCID_HAS_VULKAN
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
#endif
} // namespace

struct VulkanBackend::Impl {
#if LUCID_HAS_VULKAN
    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    std::uint32_t queue_family{};
    VkCommandPool command_pool{VK_NULL_HANDLE};
    VkImage image{VK_NULL_HANDLE};
    VkDeviceMemory image_memory{VK_NULL_HANDLE};
    VkImageLayout image_layout{VK_IMAGE_LAYOUT_UNDEFINED};
    std::uint32_t width{};
    std::uint32_t height{};
    std::string device_name{"Vulkan device"};

    ~Impl() {
        if (device != VK_NULL_HANDLE) (void)vkDeviceWaitIdle(device);
        if (device != VK_NULL_HANDLE && image != VK_NULL_HANDLE)
            vkDestroyImage(device, image, nullptr);
        if (device != VK_NULL_HANDLE && image_memory != VK_NULL_HANDLE)
            vkFreeMemory(device, image_memory, nullptr);
        if (device != VK_NULL_HANDLE && command_pool != VK_NULL_HANDLE)
            vkDestroyCommandPool(device, command_pool, nullptr);
        if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
        if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
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

    bool create_offscreen_image(const std::uint32_t requested_width,
                                const std::uint32_t requested_height,
                                std::string* error) {
        if (image != VK_NULL_HANDLE && width == requested_width &&
            height == requested_height) return true;

        if (image != VK_NULL_HANDLE) {
            vkDestroyImage(device, image, nullptr);
            image = VK_NULL_HANDLE;
        }
        if (image_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, image_memory, nullptr);
            image_memory = VK_NULL_HANDLE;
        }

        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        image_info.extent = {requested_width, requested_height, 1U};
        image_info.mipLevels = 1U;
        image_info.arrayLayers = 1U;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (!check_vk(vkCreateImage(device, &image_info, nullptr, &image),
                      "vkCreateImage", error)) return false;

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, image, &requirements);
        std::uint32_t memory_type = 0U;
        if (!find_memory_type(requirements.memoryTypeBits,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              memory_type)) {
            if (!find_memory_type(requirements.memoryTypeBits, 0U, memory_type)) {
                set_error(error, "No compatible Vulkan memory type for the off-screen image.");
                return false;
            }
        }

        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memory_type;
        if (!check_vk(vkAllocateMemory(device, &allocation, nullptr, &image_memory),
                      "vkAllocateMemory(image)", error)) return false;
        if (!check_vk(vkBindImageMemory(device, image, image_memory, 0U),
                      "vkBindImageMemory", error)) return false;

        width = requested_width;
        height = requested_height;
        image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        return true;
    }

    bool upload_rgba(const std::span<const std::uint32_t> pixels,
                     std::string* error) {
        if (image == VK_NULL_HANDLE || pixels.size() !=
            static_cast<std::size_t>(width) * height) {
            set_error(error, "Vulkan off-screen image does not match the completed frame.");
            return false;
        }
        const VkDeviceSize byte_count =
            static_cast<VkDeviceSize>(pixels.size()) * 4U;

        VkBuffer staging_buffer = VK_NULL_HANDLE;
        VkDeviceMemory staging_memory = VK_NULL_HANDLE;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        const auto cleanup = [&]() {
            if (command_buffer != VK_NULL_HANDLE)
                vkFreeCommandBuffers(device, command_pool, 1U, &command_buffer);
            if (staging_buffer != VK_NULL_HANDLE)
                vkDestroyBuffer(device, staging_buffer, nullptr);
            if (staging_memory != VK_NULL_HANDLE)
                vkFreeMemory(device, staging_memory, nullptr);
        };

        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = byte_count;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (!check_vk(vkCreateBuffer(device, &buffer_info, nullptr, &staging_buffer),
                      "vkCreateBuffer(staging)", error)) {
            cleanup();
            return false;
        }

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, staging_buffer, &requirements);
        std::uint32_t memory_type = 0U;
        const auto host_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (!find_memory_type(requirements.memoryTypeBits, host_flags, memory_type)) {
            set_error(error, "No host-visible coherent Vulkan memory type for staging.");
            cleanup();
            return false;
        }

        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memory_type;
        if (!check_vk(vkAllocateMemory(device, &allocation, nullptr, &staging_memory),
                      "vkAllocateMemory(staging)", error) ||
            !check_vk(vkBindBufferMemory(device, staging_buffer, staging_memory, 0U),
                      "vkBindBufferMemory", error)) {
            cleanup();
            return false;
        }

        void* mapped = nullptr;
        if (!check_vk(vkMapMemory(device, staging_memory, 0U, byte_count, 0U, &mapped),
                      "vkMapMemory", error)) {
            cleanup();
            return false;
        }
        auto* destination = static_cast<std::uint8_t*>(mapped);
        for (std::size_t index = 0U; index < pixels.size(); ++index) {
            const auto pixel = pixels[index];
            destination[index * 4U] =
                static_cast<std::uint8_t>((pixel >> 24U) & 0xFFU);
            destination[index * 4U + 1U] =
                static_cast<std::uint8_t>((pixel >> 16U) & 0xFFU);
            destination[index * 4U + 2U] =
                static_cast<std::uint8_t>((pixel >> 8U) & 0xFFU);
            destination[index * 4U + 3U] =
                static_cast<std::uint8_t>(pixel & 0xFFU);
        }
        vkUnmapMemory(device, staging_memory);

        VkCommandBufferAllocateInfo command_allocation{};
        command_allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_allocation.commandPool = command_pool;
        command_allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_allocation.commandBufferCount = 1U;
        if (!check_vk(vkAllocateCommandBuffers(device, &command_allocation,
                                               &command_buffer),
                      "vkAllocateCommandBuffers", error)) {
            cleanup();
            return false;
        }

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!check_vk(vkBeginCommandBuffer(command_buffer, &begin_info),
                      "vkBeginCommandBuffer", error)) {
            cleanup();
            return false;
        }

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = image_layout == VK_IMAGE_LAYOUT_UNDEFINED
            ? 0U : VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = image_layout;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0U;
        barrier.subresourceRange.levelCount = 1U;
        barrier.subresourceRange.baseArrayLayer = 0U;
        barrier.subresourceRange.layerCount = 1U;
        const VkPipelineStageFlags source_stage =
            image_layout == VK_IMAGE_LAYOUT_UNDEFINED
                ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                : VK_PIPELINE_STAGE_TRANSFER_BIT;
        vkCmdPipelineBarrier(command_buffer, source_stage,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, nullptr, 0U, nullptr,
            1U, &barrier);

        VkBufferImageCopy copy{};
        copy.bufferOffset = 0U;
        copy.bufferRowLength = 0U;
        copy.bufferImageHeight = 0U;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = 0U;
        copy.imageSubresource.baseArrayLayer = 0U;
        copy.imageSubresource.layerCount = 1U;
        copy.imageOffset = {0, 0, 0};
        copy.imageExtent = {width, height, 1U};
        vkCmdCopyBufferToImage(command_buffer, staging_buffer, image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &copy);

        if (!check_vk(vkEndCommandBuffer(command_buffer),
                      "vkEndCommandBuffer", error)) {
            cleanup();
            return false;
        }
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1U;
        submit.pCommandBuffers = &command_buffer;
        const auto submit_result = vkQueueSubmit(queue, 1U, &submit, VK_NULL_HANDLE);
        if (!check_vk(submit_result, "vkQueueSubmit", error) ||
            !check_vk(vkQueueWaitIdle(queue), "vkQueueWaitIdle", error)) {
            cleanup();
            return false;
        }
        image_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        cleanup();
        return true;
    }
#else
    std::string device_name{"Vulkan support not compiled"};
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

bool VulkanBackend::initialize(std::string* error) {
#if LUCID_HAS_VULKAN
    if (initialized_) return true;

    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "LucidSwitch2";
    application.applicationVersion = VK_MAKE_VERSION(0, 8, 0);
    application.pEngineName = "Lucid Graphics Foundation";
    application.engineVersion = VK_MAKE_VERSION(0, 8, 0);
    application.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instance_info{};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &application;
    if (!check_vk(vkCreateInstance(&instance_info, nullptr, &impl_->instance),
                  "vkCreateInstance", error)) return false;

    std::uint32_t physical_count = 0U;
    if (!check_vk(vkEnumeratePhysicalDevices(impl_->instance, &physical_count, nullptr),
                  "vkEnumeratePhysicalDevices(count)", error) || physical_count == 0U) {
        set_error(error, "No Vulkan physical device was found.");
        return false;
    }
    std::vector<VkPhysicalDevice> physical_devices(physical_count);
    if (!check_vk(vkEnumeratePhysicalDevices(impl_->instance, &physical_count,
                                              physical_devices.data()),
                  "vkEnumeratePhysicalDevices(list)", error)) return false;

    int best_score = -1;
    for (const auto candidate : physical_devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        std::uint32_t family_count = 0U;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count,
                                                   families.data());
        for (std::uint32_t family = 0U; family < family_count; ++family) {
            if (families[family].queueCount == 0U ||
                (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0U) continue;
            const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
                ? 2 : 1;
            if (score > best_score) {
                best_score = score;
                impl_->physical_device = candidate;
                impl_->queue_family = family;
                impl_->device_name = properties.deviceName;
            }
            break;
        }
    }
    if (impl_->physical_device == VK_NULL_HANDLE) {
        set_error(error, "No Vulkan graphics-capable queue family was found.");
        return false;
    }

    constexpr float queue_priority = 1.0F;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = impl_->queue_family;
    queue_info.queueCount = 1U;
    queue_info.pQueuePriorities = &queue_priority;
    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1U;
    device_info.pQueueCreateInfos = &queue_info;
    if (!check_vk(vkCreateDevice(impl_->physical_device, &device_info, nullptr,
                                 &impl_->device),
                  "vkCreateDevice", error)) return false;
    vkGetDeviceQueue(impl_->device, impl_->queue_family, 0U, &impl_->queue);

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                      VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = impl_->queue_family;
    if (!check_vk(vkCreateCommandPool(impl_->device, &pool_info, nullptr,
                                      &impl_->command_pool),
                  "vkCreateCommandPool", error)) return false;

    if (!reference_renderer_.initialize(error)) return false;
    initialized_ = true;
    return true;
#else
    set_error(error, "Vulkan support was not found at configure time. Install the Vulkan SDK and reconfigure CMake.");
    return false;
#endif
}

bool VulkanBackend::begin_frame(const std::uint32_t width,
                                const std::uint32_t height,
                                std::string* error) {
#if LUCID_HAS_VULKAN
    if (!initialized_) {
        set_error(error, "Vulkan backend is not initialized.");
        return false;
    }
    if (!reference_renderer_.begin_frame(width, height, error)) return false;
    return impl_->create_offscreen_image(width, height, error);
#else
    (void)width; (void)height;
    set_error(error, "Vulkan support was not compiled into this build.");
    return false;
#endif
}

bool VulkanBackend::clear(const std::uint32_t rgba, std::string* error) {
#if LUCID_HAS_VULKAN
    return initialized_ && reference_renderer_.clear(rgba, error);
#else
    (void)rgba;
    set_error(error, "Vulkan support was not compiled into this build.");
    return false;
#endif
}

bool VulkanBackend::draw_rect(const GraphicsRect& rect,
                              const std::uint32_t rgba,
                              std::string* error) {
#if LUCID_HAS_VULKAN
    return initialized_ && reference_renderer_.draw_rect(rect, rgba, error);
#else
    (void)rect; (void)rgba;
    set_error(error, "Vulkan support was not compiled into this build.");
    return false;
#endif
}

bool VulkanBackend::clear_depth(const float depth, std::string* error) {
    return reference_renderer_.clear_depth(depth, error);
}

bool VulkanBackend::create_texture(const TextureId id,
                                   const std::uint32_t width,
                                   const std::uint32_t height,
                                   const std::span<const std::uint32_t> pixels,
                                   const SamplerDesc& sampler,
                                   std::string* error) {
    return reference_renderer_.create_texture(id, width, height, pixels,
                                               sampler, error);
}

bool VulkanBackend::destroy_texture(const TextureId id, std::string* error) {
    return reference_renderer_.destroy_texture(id, error);
}

bool VulkanBackend::draw_indexed(
    const std::span<const GraphicsVertex> vertices,
    const std::span<const std::uint32_t> indices,
    const TextureId texture, std::string* error) {
    return reference_renderer_.draw_indexed(vertices, indices, texture, error);
}

bool VulkanBackend::present_frame(std::string* error) {
#if LUCID_HAS_VULKAN
    if (!initialized_ || !reference_renderer_.present_frame(error)) return false;
    const auto pixel_count =
        static_cast<std::size_t>(reference_renderer_.width()) *
        reference_renderer_.height();
    std::vector<std::uint32_t> pixels(pixel_count);
    if (!reference_renderer_.readback(pixels, error)) return false;
    return impl_->upload_rgba(pixels, error);
#else
    set_error(error, "Vulkan support was not compiled into this build.");
    return false;
#endif
}

bool VulkanBackend::readback(const std::span<std::uint32_t> pixels,
                             std::string* error) const {
    return reference_renderer_.readback(pixels, error);
}

GraphicsCapabilities VulkanBackend::capabilities() const noexcept {
#if LUCID_HAS_VULKAN
    return {false, true, true, true, false,
        "CPU indexed reference rasterization with Vulkan device-local RGBA8 image upload"};
#else
    return {false, false, true, true, false,
        "Vulkan SDK was not available; indexed geometry and textures use the CPU reference renderer"};
#endif
}

std::string_view VulkanBackend::name() const noexcept {
#if LUCID_HAS_VULKAN
    return impl_ != nullptr ? std::string_view(impl_->device_name)
                            : std::string_view("Vulkan backend");
#else
    return "Vulkan backend (not compiled)";
#endif
}

} // namespace lucid::gpu
