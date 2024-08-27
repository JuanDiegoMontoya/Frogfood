#include "Device.h"
#include "detail/Common.h"
#include "detail/SamplerCache2.h"

#include <vk_mem_alloc.h>

#include <volk.h>

#include <tracy/Tracy.hpp>

#include <cstdio>
#include <array>
#include <ranges>

namespace Fvog
{
  namespace
  {
    auto BytesToPostfixAndDivisor(uint64_t bytes)
    {
      const auto* postfix = "B";
      double divisor = 1.0;
      if (bytes > 1000)
      {
        postfix = "KB";
        divisor = 1000;
      }
      if (bytes > 1'000'000)
      {
        postfix = "MB";
        divisor = 1'000'000;
      }
      if (bytes > 1'000'000'000)
      {
        postfix = "GB";
        divisor = 1'000'000'000;
      }
      return std::pair{postfix, divisor};
    }

    constexpr auto deviceTracyHeapName = "GPU usage (Vulkan)";

    void VKAPI_CALL DeviceAllocCallback([[maybe_unused]] VmaAllocator VMA_NOT_NULL allocator,
      [[maybe_unused]] uint32_t memoryType,
      VkDeviceMemory VMA_NOT_NULL_NON_DISPATCHABLE memory,
      VkDeviceSize size,
      [[maybe_unused]] void* VMA_NULLABLE pUserData)
    {
      TracyAllocN(memory, size, deviceTracyHeapName);
    }

    void VKAPI_CALL DeviceFreeCallback([[maybe_unused]] VmaAllocator VMA_NOT_NULL allocator,
      [[maybe_unused]] uint32_t memoryType,
      VkDeviceMemory VMA_NOT_NULL_NON_DISPATCHABLE memory,
      [[maybe_unused]] VkDeviceSize size,
      [[maybe_unused]] void* VMA_NULLABLE pUserData)
    {
      TracyFreeN(memory, deviceTracyHeapName);
    }
  }

  Device::Device(vkb::Instance& instance, VkSurfaceKHR surface)
    : instance_(instance),
      surface_(surface),
      samplerCache_(std::make_unique<detail::SamplerCache>(this))
  {
    using namespace detail;
    ZoneScoped;
    auto selector = vkb::PhysicalDeviceSelector{instance_};

    // physical device
    physicalDevice_ = selector
      .set_minimum_version(1, 3)
      .require_present()
      .set_surface(surface_)
      .add_required_extension(VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME)
      .add_required_extension(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME) // TODO: enable for profiling builds only
    #if defined(FROGRENDER_RAYTRACING_ENABLE)
      .add_required_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
      .add_required_extension_features(VkPhysicalDeviceAccelerationStructureFeaturesKHR {
        //VkPhysicalDeviceAccelerationStructureFeaturesKHR
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .accelerationStructure = true,
        .descriptorBindingAccelerationStructureUpdateAfterBind = true,
      })
      .add_required_extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
      .add_required_extension_features(VkPhysicalDeviceRayTracingPipelineFeaturesKHR {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
        .rayTracingPipeline = true,
      })
      .add_required_extension(VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME)
      .add_required_extension_features(VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR,
        .rayTracingPositionFetch = true
      })
      .add_required_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
      .add_required_extension(VK_KHR_RAY_QUERY_EXTENSION_NAME)
      .add_required_extension_features(VkPhysicalDeviceRayQueryFeaturesKHR{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
        .rayQuery = true,
      })
    #endif
      .set_required_features({
        .independentBlend = true,
        .multiDrawIndirect = true,
        .fillModeNonSolid = true,
        .wideLines = true,
        .samplerAnisotropy = true,
        .textureCompressionBC = true,
        .vertexPipelineStoresAndAtomics = true,
        .fragmentStoresAndAtomics = true,
        .shaderStorageImageExtendedFormats = true,
        // Apparently the next two features are not needed with 1.3 since you can query support in a granular fashion
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
        .shaderInt16 = true,
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
        //.separateDepthStencilLayouts = true,
        .hostQueryReset = true, // TODO: enable for profiling builds only
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

    // Per-frame swapchain sync, command pools, and command buffers
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
      }), nullptr, &frame.swapchainSemaphore));

      CheckVkResult(vkCreateSemaphore(device_, Address(VkSemaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      }), nullptr, &frame.renderSemaphore));
    }

    // Immediate submit stuff (subject to change)
    CheckVkResult(vkCreateCommandPool(device_, Address(VkCommandPoolCreateInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = graphicsQueueFamilyIndex_,
    }), nullptr, &immediateSubmitCommandPool_));

    CheckVkResult(vkAllocateCommandBuffers(device_, Address(VkCommandBufferAllocateInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = immediateSubmitCommandPool_,
      .commandBufferCount = 1,
    }), &immediateSubmitCommandBuffer_));

    // Queue timeline semaphores
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
      .pDeviceMemoryCallbacks = Address(VmaDeviceMemoryCallbacks{
        .pfnAllocate = DeviceAllocCallback,
        .pfnFree = DeviceFreeCallback,
      }),
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
#ifdef FROGRENDER_RAYTRACING_ENABLE
      {.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, .descriptorCount = maxResourceDescriptors},
#endif
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
#ifdef FROGRENDER_RAYTRACING_ENABLE
      {accelerationStructureBinding, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, maxResourceDescriptors, VK_SHADER_STAGE_ALL},
#endif
    });

    constexpr auto bindingsFlags = std::to_array<VkDescriptorBindingFlags>({
      {VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT},
      {VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT},
      {VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT},
      {VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT},
      {VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT},
#ifdef FROGRENDER_RAYTRACING_ENABLE
      {VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT},
#endif
    });
    
    static_assert(bindings.size() == bindingsFlags.size());
    static_assert(poolSizes.size() == bindingsFlags.size());

    CheckVkResult(vkCreateDescriptorSetLayout(device_, Address(VkDescriptorSetLayoutCreateInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = Address(VkDescriptorSetLayoutBindingFlagsCreateInfo
      {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(bindingsFlags.size()),
        .pBindingFlags = bindingsFlags.data(),
      }),
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
    
    Fvog::detail::CheckVkResult(
      vkCreatePipelineLayout(
        device_,
        Fvog::detail::Address(VkPipelineLayoutCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .setLayoutCount = 1,
          .pSetLayouts = &descriptorSetLayout_,
          .pushConstantRangeCount = 1,
          .pPushConstantRanges = Fvog::detail::Address(VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_ALL,
            .offset = 0,
            .size = 128,
          }),
        }),
        nullptr,
        &defaultPipelineLayout));
  }
  
  Device::~Device()
  {
    ZoneScoped;
    detail::CheckVkResult(vkDeviceWaitIdle(device_));

    FreeUnusedResources();

    vkDestroyPipelineLayout(device_, defaultPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
    vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);

    vkDestroyCommandPool(device_, immediateSubmitCommandPool_, nullptr);

    for (const auto& frame : frameData)
    {
      vkDestroyCommandPool(device_, frame.commandPool, nullptr);
      vkDestroySemaphore(device_, frame.renderSemaphore, nullptr);
      vkDestroySemaphore(device_, frame.swapchainSemaphore, nullptr);
    }

    vkDestroySemaphore(device_, graphicsQueueTimelineSemaphore_, nullptr);
    
    vmaDestroyAllocator(allocator_);

    samplerCache_.reset();
    vkb::destroy_device(device_);
  }

  void Device::ImmediateSubmit(const std::function<void(VkCommandBuffer)>& function) const
  {
    ZoneScoped;
    using namespace detail;
    CheckVkResult(vkResetCommandBuffer(immediateSubmitCommandBuffer_, 0));
    CheckVkResult(vkBeginCommandBuffer(immediateSubmitCommandBuffer_, Address(VkCommandBufferBeginInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    })));

    function(immediateSubmitCommandBuffer_);

    vkCmdPipelineBarrier2(immediateSubmitCommandBuffer_, Address(VkDependencyInfo{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = 1,
      .pMemoryBarriers = Address(VkMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
      }),
    }));

    CheckVkResult(vkEndCommandBuffer(immediateSubmitCommandBuffer_));

    vkQueueSubmit2(graphicsQueue_, 1, Address(VkSubmitInfo2{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = Address(VkCommandBufferSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = immediateSubmitCommandBuffer_,
      }),
    }), VK_NULL_HANDLE);

    // TODO: Horrible sin
    CheckVkResult(vkQueueWaitIdle(graphicsQueue_));
  }

  void Device::FreeUnusedResources()
  {
    ZoneScoped;
    auto value = uint64_t{};

    {
      ZoneScopedN("Get graphics queue semaphore value");
      value = GetCurrentFrameData().renderTimelineSemaphoreWaitValue;
    }

    {
      ZoneScopedN("Free unused buffers");
      std::erase_if(bufferDeletionQueue_,
        [this, value](const BufferDeleteInfo& bufferAlloc)
        {
          if (value >= bufferAlloc.frameOfLastUse)
          {
            ZoneScopedN("Destroy VMA buffer");
            VmaAllocationInfo info{};
            vmaGetAllocationInfo(allocator_, bufferAlloc.allocation, &info);
            auto [postfix, divisor] = BytesToPostfixAndDivisor(info.size);
            char buffer[128]{};
            auto size = snprintf(buffer, std::size(buffer), "Size: %.1f %s", double(info.size) / divisor, postfix);
            ZoneText(buffer, size);
            ZoneName(bufferAlloc.name.c_str(), bufferAlloc.name.size());
            vmaDestroyBuffer(allocator_, bufferAlloc.buffer, bufferAlloc.allocation);
            return true;
          }
          return false;
        });
    }
    {
      ZoneScopedN("Free unused images");
      std::erase_if(imageDeletionQueue_,
        [this, value](const ImageDeleteInfo& imageAlloc)
        {
          if (value >= imageAlloc.frameOfLastUse)
          {
            ZoneScopedN("vmaDestroyImage");
            VmaAllocationInfo info{};
            vmaGetAllocationInfo(allocator_, imageAlloc.allocation, &info);
            auto [postfix, divisor] = BytesToPostfixAndDivisor(info.size);
            char buffer[128]{};
            auto size = snprintf(buffer, std::size(buffer), "Size: %.1f %s", double(info.size) / divisor, postfix);
            ZoneText(buffer, size);
            ZoneName(imageAlloc.name.c_str(), imageAlloc.name.size());
            vmaDestroyImage(allocator_, imageAlloc.image, imageAlloc.allocation);
            return true;
          }
          return false;
        });
    }
    {
      ZoneScopedN("Free unused image views");
      std::erase_if(imageViewDeletionQueue_,
        [this, value](const ImageViewDeleteInfo& imageAlloc)
        {
          if (value >= imageAlloc.frameOfLastUse)
          {
            ZoneScopedN("vkDestroyImageView");
            ZoneName(imageAlloc.name.data(), imageAlloc.name.size());
            vkDestroyImageView(device_, imageAlloc.imageView, nullptr);
            return true;
          }
          return false;
        });
    }
    {
      ZoneScopedN("Free unused descriptor indices");
      std::erase_if(descriptorDeletionQueue_,
        [this, value](const DescriptorDeleteInfo& descriptorAlloc)
        {
          if (value >= descriptorAlloc.frameOfLastUse)
          {
            switch (descriptorAlloc.handle.type)
            {
            case ResourceType::STORAGE_BUFFER: storageBufferDescriptorAllocator.Free(descriptorAlloc.handle.index); return true;
            case ResourceType::COMBINED_IMAGE_SAMPLER: combinedImageSamplerDescriptorAllocator.Free(descriptorAlloc.handle.index); return true;
            case ResourceType::STORAGE_IMAGE: storageImageDescriptorAllocator.Free(descriptorAlloc.handle.index); return true;
            case ResourceType::SAMPLED_IMAGE: sampledImageDescriptorAllocator.Free(descriptorAlloc.handle.index); return true;
            case ResourceType::SAMPLER: samplerDescriptorAllocator.Free(descriptorAlloc.handle.index); return true;
#ifdef FROGRENDER_RAYTRACING_ENABLE
            case ResourceType::ACCELERATION_STRUCTURE: accelerationStructureDescriptorAllocator.Free(descriptorAlloc.handle.index); return true;
#endif
            case ResourceType::INVALID:
            default: assert(0); return true;
            }
          }
          return false;
        });
    }
    {
      ZoneScopedN("Free generic");
      std::erase_if(genericDeletionQueue_, [this, value](auto& fn) { return fn(value); });
    }
  }

  Device::DescriptorInfo::DescriptorInfo(DescriptorInfo&& old) noexcept
    : device_(std::exchange(old.device_, nullptr)),
      handle_(std::exchange(old.handle_, {}))
  {
  }

  Device::DescriptorInfo& Device::DescriptorInfo::operator=(DescriptorInfo&& old) noexcept
  {
    if (&old == this)
      return *this;
    this->~DescriptorInfo();
    return *new (this) DescriptorInfo(std::move(old));
  }

  Device::DescriptorInfo::~DescriptorInfo()
  {
    if (handle_.type != ResourceType::INVALID)
    {
      device_->descriptorDeletionQueue_.emplace_back(device_->frameNumber, handle_);
    }
  }

  Device::DescriptorInfo Device::AllocateStorageBufferDescriptor(VkBuffer buffer)
  {
    ZoneScoped;
    const auto myIdx = storageBufferDescriptorAllocator.Allocate();

    vkUpdateDescriptorSets(device_, 1, detail::Address(VkWriteDescriptorSet{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = descriptorSet_,
      .dstBinding = storageBufferBinding,
      .dstArrayElement = myIdx,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = detail::Address(VkDescriptorBufferInfo{
        .buffer = buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
      }),
    }), 0, nullptr);

    return DescriptorInfo{
      *this,
      DescriptorInfo::ResourceHandle{
        ResourceType::STORAGE_BUFFER,
        myIdx,
      }};
  }

  Device::DescriptorInfo Device::AllocateCombinedImageSamplerDescriptor(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout)
  {
    ZoneScoped;
    const auto myIdx = combinedImageSamplerDescriptorAllocator.Allocate();

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

    return DescriptorInfo{
      *this,
      DescriptorInfo::ResourceHandle{
        ResourceType::COMBINED_IMAGE_SAMPLER,
        myIdx,
      }};
  }

  Device::DescriptorInfo Device::AllocateStorageImageDescriptor(VkImageView imageView, VkImageLayout imageLayout)
  {
    ZoneScoped;
    const auto myIdx = storageImageDescriptorAllocator.Allocate();

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

    return DescriptorInfo{
      *this,
      DescriptorInfo::ResourceHandle{
        ResourceType::STORAGE_IMAGE,
        myIdx,
      }};
  }

  Device::DescriptorInfo Device::AllocateSampledImageDescriptor(VkImageView imageView, VkImageLayout imageLayout)
  {
    ZoneScoped;
    const auto myIdx = sampledImageDescriptorAllocator.Allocate();

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

    return DescriptorInfo{
      *this,
      DescriptorInfo::ResourceHandle{
        ResourceType::SAMPLED_IMAGE,
        myIdx,
      }};
  }

  Device::DescriptorInfo Device::AllocateSamplerDescriptor(VkSampler sampler)
  {
    ZoneScoped;
    const auto myIdx = samplerDescriptorAllocator.Allocate();

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

    return DescriptorInfo{
      *this,
      DescriptorInfo::ResourceHandle{
        ResourceType::SAMPLER,
        myIdx,
      }};
  }

#ifdef FROGRENDER_RAYTRACING_ENABLE
  Device::DescriptorInfo Device::AllocateAccelerationStructureDescriptor(VkAccelerationStructureKHR tlas)
  {
    ZoneScoped;
    const auto myIdx = accelerationStructureDescriptorAllocator.Allocate();

    vkUpdateDescriptorSets(device_, 1, detail::Address(VkWriteDescriptorSet{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .pNext = detail::Address(VkWriteDescriptorSetAccelerationStructureKHR{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &tlas,
      }),
      .dstSet = descriptorSet_,
      .dstBinding = accelerationStructureBinding,
      .dstArrayElement = myIdx,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
    }), 0, nullptr);

    return DescriptorInfo{
      *this,
      DescriptorInfo::ResourceHandle{
        ResourceType::ACCELERATION_STRUCTURE,
        myIdx,
      }};
  }
#endif

  Device::IndexAllocator::IndexAllocator(uint32_t numIndices)
  {
    for (auto i : std::ranges::reverse_view(std::views::iota(uint32_t(0), numIndices)))
    {
      freeSlots_.push(i);
    }
  }

  uint32_t Device::IndexAllocator::Allocate()
  {
    const auto index = freeSlots_.top();
    freeSlots_.pop();
    return index;
  }

  void Device::IndexAllocator::Free(uint32_t index)
  {
    freeSlots_.push(index);
  }
} // namespace Fvog
