#include "Device.h"
#include "detail/Common.h"

#include <vk_mem_alloc.h>

#include <volk.h>

#include <array>

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
    for (auto& frame : frameData)
    {
      CheckVkResult(vkCreateCommandPool(device_, Address(VkCommandPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = graphicsQueueFamilyIndex_,
      }), nullptr, &frame.commandPool));

      CheckVkResult(vkAllocateCommandBuffers(device_, Address(VkCommandBufferAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = frame.commandPool,
        .commandBufferCount = 1,
      }), &frame.commandBuffer));

      CheckVkResult(vkCreateSemaphore(device_, Address(VkSemaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      }), nullptr, &frame.presentSemaphore));

      CheckVkResult(vkCreateSemaphore(device_, Address(VkSemaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      }), nullptr, &frame.renderSemaphore));
    }
    
    CheckVkResult(vkCreateSemaphore(device_, Address(VkSemaphoreCreateInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = Address(VkSemaphoreTypeCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = 0,
      }),
    }), nullptr, &graphicsQueueTimelineSemaphore_));
    
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

    // Create mega descriptor set
    constexpr auto poolSizes = std::to_array<VkDescriptorPoolSize>({
      {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = maxResourceDescriptors},
      {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = maxResourceDescriptors}, // TODO: remove this in favor of separate images + samplers
      {.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = maxResourceDescriptors},
      {.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = maxResourceDescriptors},
      {.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = maxSamplerDescriptors},
    });

    CheckVkResult(vkCreateDescriptorPool(device_, Address(VkDescriptorPoolCreateInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
      .maxSets = 1,
      .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
      .pPoolSizes = poolSizes.data(),
    }), nullptr, &descriptorPool_));


    constexpr auto bindings = std::to_array<VkDescriptorSetLayoutBinding>({
      {.binding = storageBufferBinding, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = maxResourceDescriptors, .stageFlags = VK_SHADER_STAGE_ALL},
      {combinedImageSamplerBinding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxResourceDescriptors, VK_SHADER_STAGE_ALL},
      {storageImageBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxResourceDescriptors, VK_SHADER_STAGE_ALL},
      {sampledImageBinding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxResourceDescriptors, VK_SHADER_STAGE_ALL},
      {samplerBinding, VK_DESCRIPTOR_TYPE_SAMPLER, maxSamplerDescriptors, VK_SHADER_STAGE_ALL},
    });

    CheckVkResult(vkCreateDescriptorSetLayout(device_, Address(VkDescriptorSetLayoutCreateInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
      .bindingCount = static_cast<uint32_t>(bindings.size()),
      .pBindings = bindings.data(),
    }), nullptr, &descriptorSetLayout_));

    CheckVkResult(vkAllocateDescriptorSets(device_, Address(VkDescriptorSetAllocateInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = descriptorPool_,
      .descriptorSetCount = 1,
      .pSetLayouts = &descriptorSetLayout_,
    }), &descriptorSet_));
  }
  
  Device::~Device()
  {
    vkDeviceWaitIdle(device_);

    vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
    vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);

    for (const auto& frame : frameData)
    {
      vkDestroyCommandPool(device_, frame.commandPool, nullptr);
      vkDestroySemaphore(device_, frame.renderSemaphore, nullptr);
      vkDestroySemaphore(device_, frame.presentSemaphore, nullptr);
    }

    vkDestroySemaphore(device_, graphicsQueueTimelineSemaphore_, nullptr);

    vkb::destroy_swapchain(swapchain_);

    for (auto view : swapchainImageViews_)
    {
      vkDestroyImageView(device_, view, nullptr);
    }

    for (const auto& imageAlloc : imageDeletionQueue_)
    {
      vkDestroyImageView(device_, imageAlloc.imageView, nullptr);
      vmaDestroyImage(allocator_, imageAlloc.image, imageAlloc.allocation);
    }

    vmaDestroyAllocator(allocator_);
    vkb::destroy_device(device_);
  }

  uint32_t Device::AllocateStorageBufferDescriptor(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range)
  {
    assert(currentStorageBufferDescriptorIndex < maxResourceDescriptors);
    const auto myIdx = currentStorageBufferDescriptorIndex++;
    vkUpdateDescriptorSets(device_, 1, detail::Address(VkWriteDescriptorSet{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = descriptorSet_,
      .dstBinding = storageBufferBinding,
      .dstArrayElement = myIdx,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = detail::Address(VkDescriptorBufferInfo{
        .buffer = buffer,
        .offset = offset,
        .range = range,
      }),
    }), 0, nullptr);
    return myIdx;
  }

  uint32_t Device::AllocateCombinedImageSamplerDescriptor(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout)
  {
    assert(currentCombinedImageSamplerDescriptorIndex < maxResourceDescriptors);
    const auto myIdx = currentCombinedImageSamplerDescriptorIndex++;
    vkUpdateDescriptorSets(device_, 1, detail::Address(VkWriteDescriptorSet{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = descriptorSet_,
      .dstBinding = combinedImageSamplerBinding,
      .dstArrayElement = myIdx,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImageInfo = detail::Address(VkDescriptorImageInfo{
        .sampler = sampler,
        .imageView = imageView,
        .imageLayout = imageLayout,
      })
    }), 0, nullptr);
    return myIdx;
  }

  uint32_t Device::AllocateStorageImageDescriptor(VkImageView imageView, VkImageLayout imageLayout)
  {
    assert(currentStorageImageDescriptorIndex < maxResourceDescriptors);
    const auto myIdx = currentStorageImageDescriptorIndex++;
    vkUpdateDescriptorSets(device_, 1, detail::Address(VkWriteDescriptorSet{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = descriptorSet_,
      .dstBinding = storageImageBinding,
      .dstArrayElement = myIdx,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .pImageInfo = detail::Address(VkDescriptorImageInfo{
        .imageView = imageView,
        .imageLayout = imageLayout,
      })
    }), 0, nullptr);
    return myIdx;
  }

  uint32_t Device::AllocateSampledImageDescriptor(VkImageView imageView, VkImageLayout imageLayout)
  {
    assert(currentSampledImageDescriptorIndex < maxResourceDescriptors);
    const auto myIdx = currentSampledImageDescriptorIndex++;
    vkUpdateDescriptorSets(device_, 1, detail::Address(VkWriteDescriptorSet{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = descriptorSet_,
      .dstBinding = sampledImageBinding,
      .dstArrayElement = myIdx,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
      .pImageInfo = detail::Address(VkDescriptorImageInfo{
        .imageView = imageView,
        .imageLayout = imageLayout,
      })
    }), 0, nullptr);
    return myIdx;
  }

  uint32_t Device::AllocateSamplerDescriptor(VkSampler sampler)
  {
    assert(currentSamplerDescriptorIndex < maxResourceDescriptors);
    const auto myIdx = currentSamplerDescriptorIndex++;
    vkUpdateDescriptorSets(device_, 1, detail::Address(VkWriteDescriptorSet{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = descriptorSet_,
      .dstBinding = samplerBinding,
      .dstArrayElement = myIdx,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
      .pImageInfo = detail::Address(VkDescriptorImageInfo{
        .sampler = sampler,
      })
    }), 0, nullptr);
    return myIdx;
  }
} // namespace Fvog
