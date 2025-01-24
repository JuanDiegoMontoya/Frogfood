#include "Descriptor.h"
#include "Device.h"

namespace Fvog
{
  DescriptorInfo::DescriptorInfo(DescriptorInfo&& old) noexcept : device_(std::exchange(old.device_, nullptr)), handle_(std::exchange(old.handle_, {}))
  {
  }

  DescriptorInfo& DescriptorInfo::operator=(DescriptorInfo&& old) noexcept
  {
    if (&old == this)
      return *this;
    this->~DescriptorInfo();
    return *new (this) DescriptorInfo(std::move(old));
  }

  DescriptorInfo::~DescriptorInfo()
  {
    if (handle_.type != ResourceType::INVALID)
    {
      device_->descriptorDeletionQueue_.emplace_back(device_->frameNumber, handle_);
    }
  }
}
