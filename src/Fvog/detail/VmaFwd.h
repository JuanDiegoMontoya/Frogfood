#pragma once
#include "vulkan/vulkan_core.h"

typedef struct VmaAllocator_T* VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VmaVirtualAllocation);
VK_DEFINE_HANDLE(VmaVirtualBlock);