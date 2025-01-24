#pragma once
#include "../Texture2.h"
#include <unordered_map>

template<>
struct std::hash<Fvog::SamplerCreateInfo>
{
  std::size_t operator()(const Fvog::SamplerCreateInfo& k) const noexcept;
};

namespace Fwog
{
  class Device;
}

namespace Fvog::detail
{
  class SamplerCache
  {
  public:
    SamplerCache(Device* device) : device_(device) {}
    SamplerCache(const SamplerCache&) = delete;
    SamplerCache& operator=(const SamplerCache&) = delete;
    SamplerCache(SamplerCache&&) noexcept = default;
    SamplerCache& operator=(SamplerCache&&) noexcept = default;
    ~SamplerCache()
    {
      Clear();
    }

    [[nodiscard]] Sampler CreateOrGetCachedTextureSampler(const SamplerCreateInfo& samplerState, std::string name);
    [[nodiscard]] size_t Size() const;
    void Clear();

  private:
    Device* device_;
    std::unordered_map<SamplerCreateInfo, Sampler> samplerCache_;
    std::unordered_map<SamplerCreateInfo, DescriptorInfo> descriptorCache_;
  };
} // namespace Fwog::detail