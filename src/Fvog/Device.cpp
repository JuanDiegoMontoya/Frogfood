#include "Device.h"
#include "detail/Common.h"

namespace Fvog
{
  Device::Device(vkb::Instance& instance, VkSurfaceKHR surface)
    : instance_(instance),
      surface_(surface)
  {
    using namespace detail;
    auto selector = vkb::PhysicalDeviceSelector{instance_};

    // physical device
    physicalDevice_ = selector
      .set_minimum_version(1, 3)
      .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
      .require_present()
      .set_surface(surface_)
      .set_required_features({
        .multiDrawIndirect = true,
        .textureCompressionBC = true,
        .fragmentStoresAndAtomics = true,
        .shaderStorageImageExtendedFormats = true,
        .shaderStorageImageReadWithoutFormat = true,
        .shaderStorageImageWriteWithoutFormat = true,
        .shaderUniformBufferArrayDynamicIndexing = true,
        .shaderSampledImageArrayDynamicIndexing = true,
        .shaderStorageBufferArrayDynamicIndexing = true,
        .shaderStorageImageArrayDynamicIndexing = true,
        .shaderClipDistance = true,
        .shaderCullDistance = true,
        .shaderFloat64 = true,
        .shaderInt64 = true,
      })
      .set_required_features_11({
        .storageBuffer16BitAccess = true,
        .uniformAndStorageBuffer16BitAccess = true,
        .multiview = true,
        .variablePointersStorageBuffer = true,
        .variablePointers = true,
        .shaderDrawParameters = true,
      })
      .set_required_features_12({
        .drawIndirectCount = true,
        .storageBuffer8BitAccess = true,
        .uniformAndStorageBuffer8BitAccess = true,
        .shaderFloat16 = true,
        .shaderInt8 = true,
        .descriptorIndexing = true,
        .shaderInputAttachmentArrayDynamicIndexing = true,
        .shaderUniformTexelBufferArrayDynamicIndexing = true,
        .shaderStorageTexelBufferArrayDynamicIndexing = true,
        .shaderUniformBufferArrayNonUniformIndexing = true,
        .shaderSampledImageArrayNonUniformIndexing = true,
        .shaderStorageBufferArrayNonUniformIndexing = true,
        .shaderStorageImageArrayNonUniformIndexing = true,
        .shaderUniformTexelBufferArrayNonUniformIndexing = true,
        .shaderStorageTexelBufferArrayNonUniformIndexing = true,
        .descriptorBindingSampledImageUpdateAfterBind = true,
        .descriptorBindingStorageImageUpdateAfterBind = true,
        .descriptorBindingStorageBufferUpdateAfterBind = true,
        .descriptorBindingUniformTexelBufferUpdateAfterBind = true,
        .descriptorBindingUpdateUnusedWhilePending = true,
        .descriptorBindingPartiallyBound = true,
        .descriptorBindingVariableDescriptorCount = true,
        .runtimeDescriptorArray = true,
        .samplerFilterMinmax = true,
        .scalarBlockLayout = true,
        .imagelessFramebuffer = true,
        .uniformBufferStandardLayout = true,
        .shaderSubgroupExtendedTypes = true,
        .separateDepthStencilLayouts = true,
        .hostQueryReset = true,
        .timelineSemaphore = true,
        .bufferDeviceAddress = true,
        .vulkanMemoryModel = true,
        .vulkanMemoryModelDeviceScope = true,
        .subgroupBroadcastDynamicId = true,
      })
      .set_required_features_13({
        .shaderDemoteToHelperInvocation = true,
        .shaderTerminateInvocation = true,
        .synchronization2 = true,
        .dynamicRendering = true,
        .shaderIntegerDotProduct = true,
        .maintenance4 = true,
      })
      .select()
      .value();
    
    device_ = vkb::DeviceBuilder{physicalDevice_}.build().value();
    graphicsQueue_ = device_.get_queue(vkb::QueueType::graphics).value();
    graphicsQueueFamilyIndex_ = device_.get_queue_index(vkb::QueueType::graphics).value();

    // swapchain
    swapchain_ = vkb::SwapchainBuilder{device_}
      .set_old_swapchain(VK_NULL_HANDLE)
      .set_desired_min_image_count(2)
      .use_default_present_mode_selection()
      .set_desired_extent(1920, 1080)
      .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
      .build()
      .value();

    swapchainImages_ = swapchain_.get_images().value();
    swapchainImageViews_ = swapchain_.get_image_views().value();

    // command pools and command buffers
    for (uint32_t i = 0; i < frameOverlap; i++)
    {
      CheckVkResult(vkCreateCommandPool(device_, Address(VkCommandPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = graphicsQueueFamilyIndex_,
      }), nullptr, &frameData[i].commandPool));

      CheckVkResult(vkAllocateCommandBuffers(device_, Address(VkCommandBufferAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = frameData[i].commandPool,
        .commandBufferCount = 1,
      }), &frameData[i].commandBuffer));
      
      CheckVkResult(vkCreateSemaphore(device_, Address(VkSemaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = Address(VkSemaphoreTypeCreateInfo{
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
          .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
          .initialValue = 0,
        }),
      }), nullptr, &frameData[i].renderTimelneSemaphore));

      CheckVkResult(vkCreateSemaphore(device_, Address(VkSemaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      }), nullptr, &frameData[i].presentSemaphore));

      CheckVkResult(vkCreateSemaphore(device_, Address(VkSemaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      }), nullptr, &frameData[i].renderSemaphore));
    }
    
    vmaCreateAllocator(Address(VmaAllocatorCreateInfo{
      .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
      .physicalDevice = physicalDevice_,
      .device = device_,
      .pVulkanFunctions = Address(VmaVulkanFunctions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
        .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
        .vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties,
        .vkAllocateMemory = vkAllocateMemory,
        .vkFreeMemory = vkFreeMemory,
        .vkMapMemory = vkMapMemory,
        .vkUnmapMemory = vkUnmapMemory,
        .vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
        .vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
        .vkBindBufferMemory = vkBindBufferMemory,
        .vkBindImageMemory = vkBindImageMemory,
        .vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
        .vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
        .vkCreateBuffer = vkCreateBuffer,
        .vkDestroyBuffer = vkDestroyBuffer,
        .vkCreateImage = vkCreateImage,
        .vkDestroyImage = vkDestroyImage,
        .vkCmdCopyBuffer = vkCmdCopyBuffer,
        .vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2,
        .vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2,
        .vkBindBufferMemory2KHR = vkBindBufferMemory2,
        .vkBindImageMemory2KHR = vkBindImageMemory2,
        .vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2,
      }),
      .instance = instance_,
      .vulkanApiVersion = VK_API_VERSION_1_2,
    }), &allocator_);
  }

  Device::~Device()
  {
    for (auto& frame : frameData)
    {
      vkDestroyCommandPool(device_, frame.commandPool, nullptr);
      vkDestroySemaphore(device_, frame.renderTimelneSemaphore, nullptr);
      vkDestroySemaphore(device_, frame.renderSemaphore, nullptr);
      vkDestroySemaphore(device_, frame.presentSemaphore, nullptr);
    }

    vkb::destroy_swapchain(swapchain_);

    for (auto view : swapchainImageViews_)
    {
      vkDestroyImageView(device_, view, nullptr);
    }

    vmaDestroyAllocator(allocator_);
    vkb::destroy_device(device_);
  }
} // namespace Fvog