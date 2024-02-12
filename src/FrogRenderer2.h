#pragma once

#include "Application.h"

class FrogRenderer2 final : public Application
{
public:
  FrogRenderer2(const Application::CreateInfo& createInfo);

private:
  void OnWindowResize(uint32_t newWidth, uint32_t newHeight) override;
  void OnUpdate(double dt) override;
  void OnRender(double dt) override;
  void OnGui(double dt) override;
  void OnPathDrop(std::span<const char*> paths) override;
};