#include "SamplerCache2.h"
#include "../Device.h"
#include "ApiToEnum2.h"
#include "Hash2.h"
#include "Common.h"

#include <tracy/Tracy.hpp>

#include <volk.h>

namespace Fvog::detail
{
  Sampler SamplerCache::CreateOrGetCachedTextureSampler(const SamplerCreateInfo& samplerState, std::string name)
  {
    ZoneScoped;
    ZoneNamed(_, true);
    ZoneNameV(_, name.data(), name.size());
    if (auto it = samplerCache_.find(samplerState); it != samplerCache_.end())
    {
      return it->second;
    }

    auto sampler = VkSampler{};
    vkCreateSampler(device_->device_, Address(VkSamplerCreateInfo{
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = samplerState.magFilter,
      .minFilter = samplerState.minFilter,
      .mipmapMode = samplerState.mipmapMode,
      .addressModeU = samplerState.addressModeU,
      .addressModeV = samplerState.addressModeV,
      .addressModeW = samplerState.addressModeW,
      .mipLodBias = samplerState.mipLodBias,
      .anisotropyEnable = samplerState.maxAnisotropy >= 1.0f,
      .maxAnisotropy = samplerState.maxAnisotropy,
      .compareEnable = samplerState.compareEnable,
      .compareOp = samplerState.compareOp,
      .minLod = samplerState.minLod,
      .maxLod = samplerState.maxLod,
      .borderColor = samplerState.borderColor,
      .unnormalizedCoordinates = VK_FALSE,
    }), nullptr, &sampler);
    
    // TODO: gate behind compile-time switch
    vkSetDebugUtilsObjectNameEXT(device_->device_, detail::Address(VkDebugUtilsObjectNameInfoEXT{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = VK_OBJECT_TYPE_SAMPLER,
      .objectHandle = reinterpret_cast<uint64_t>(sampler),
      .pObjectName = name.data(),
    }));

    //detail::InvokeVerboseMessageCallback("Created sampler with handle ", sampler);

    auto& descriptorInfo = descriptorCache_.insert({samplerState, device_->AllocateSamplerDescriptor(sampler)}).first->second;
    return samplerCache_.insert({samplerState, Sampler(sampler, descriptorInfo)}).first->second;
  }

  size_t SamplerCache::Size() const
  {
    return samplerCache_.size();
  }

  void SamplerCache::Clear()
  {
    for (const auto& [_, sampler] : samplerCache_)
    {
      //detail::InvokeVerboseMessageCallback("Destroyed sampler with handle ", sampler.id_);
      vkDestroySampler(device_->device_, sampler.Handle(), nullptr);
    }

    descriptorCache_.clear();
    samplerCache_.clear();
  }
} // namespace Fwog::detail

std::size_t std::hash<Fvog::SamplerCreateInfo>::operator()(const Fvog::SamplerCreateInfo& k) const noexcept
{
  auto hashed = std::make_tuple(
    k.minFilter,
    k.magFilter,
    k.mipmapMode,
    k.addressModeU,
    k.addressModeV,
    k.addressModeW,
    k.borderColor,
    k.maxAnisotropy,
    k.compareEnable,
    k.compareOp,
    k.mipLodBias,
    k.minLod,
    k.maxLod
  );

  return Fvog::detail::hashing::hash<decltype(hashed)>{}(hashed);
}
