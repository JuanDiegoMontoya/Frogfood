#include "FrogRenderer2.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

FrogRenderer2::FrogRenderer2(const Application::CreateInfo& createInfo)
  : Application(createInfo)
{
  
}

void FrogRenderer2::OnWindowResize([[maybe_unused]] uint32_t newWidth, [[maybe_unused]] uint32_t newHeight) {}

void FrogRenderer2::OnUpdate([[maybe_unused]] double dt) {}

void FrogRenderer2::OnRender([[maybe_unused]] double dt) {}

void FrogRenderer2::OnGui([[maybe_unused]] double dt)
{
  ImGui::Begin("test");
  ImGui::Text("tset");
  ImGui::End();
}

void FrogRenderer2::OnPathDrop([[maybe_unused]] std::span<const char*> paths)
{
  
}
