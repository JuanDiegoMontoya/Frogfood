#include "FrogRenderer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>

void FrogRenderer::InitGui()
{
  ImGui::GetIO().Fonts->AddFontFromFileTTF("textures/RobotoCondensed-Regular.ttf", 18);
}

void FrogRenderer::GuiDrawMagnifier(glm::vec2 viewportContentOffset, glm::vec2 viewportContentSize)
{
  ImGui::Begin(("Magnifier: " + std::string(magnifierLock ? "Locked (L, Space)" : "Unlocked (L, Space)") + "###mag").c_str());
  if (ImGui::GetKeyPressedAmount(ImGuiKey_KeypadSubtract, 10000, 1))
  {
    magnifierZoom = std::max(magnifierZoom / 1.5f, 1.0f);
  }
  if (ImGui::GetKeyPressedAmount(ImGuiKey_KeypadAdd, 10000, 1))
  {
    magnifierZoom = std::min(magnifierZoom * 1.5f, 50.0f);
  }

  ImGui::SliderFloat("Zoom (+, -)", &magnifierZoom, 1.0f, 50.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
  if (ImGui::GetKeyPressedAmount(ImGuiKey_L, 10000, 1) || ImGui::GetKeyPressedAmount(ImGuiKey_Space, 10000, 1))
  {
    magnifierLock = !magnifierLock;
  }

  // The actual size of the magnifier widget
  // constexpr auto magnifierSize = ImVec2(400, 400);
  const auto magnifierSize = ImGui::GetContentRegionAvail();

  // The dimensions of the region viewed by the magnifier, equal to the magnifier's size (1x zoom) or less
  const auto magnifierExtent =
    ImVec2(std::min(viewportContentSize.x, magnifierSize.x / magnifierZoom), std::min(viewportContentSize.y, magnifierSize.y / magnifierZoom));

  // Get window coords
  double x{}, y{};
  glfwGetCursorPos(window, &x, &y);

  // Window to viewport
  x -= viewportContentOffset.x;
  y -= viewportContentOffset.y;

  // Clamp to smaller region within the viewport so magnifier doesn't view OOB pixels
  x = std::clamp(float(x), magnifierExtent.x / 2.f, viewportContentSize.x - magnifierExtent.x / 2.f);
  y = std::clamp(float(y), magnifierExtent.y / 2.f, viewportContentSize.y - magnifierExtent.y / 2.f);

  // Use stored cursor pos if magnifier is locked
  glm::vec2 mp = magnifierLock ? magnifierLastCursorPos : glm::vec2{x, y};
  magnifierLastCursorPos = mp;

  // Y flip (+Y is up for textures)
  mp.y = viewportContentSize.y - mp.y;

  // Mouse position in UV space
  mp /= glm::vec2(viewportContentSize.x, viewportContentSize.y);
  glm::vec2 magnifierHalfExtentUv = {
    magnifierExtent.x / 2.f / viewportContentSize.x,
    -magnifierExtent.y / 2.f / viewportContentSize.y,
  };

  // Calculate the min and max UV of the magnifier window
  glm::vec2 uv0{mp - magnifierHalfExtentUv};
  glm::vec2 uv1{mp + magnifierHalfExtentUv};

  glTextureParameteri(frame.colorLdrWindowRes.value().Handle(), GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(frame.colorLdrWindowRes.value().Handle())),
               magnifierSize,
               ImVec2(uv0.x, uv0.y),
               ImVec2(uv1.x, uv1.y));
  ImGui::End();
}

void FrogRenderer::GuiDrawDockspace()
{
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);
  ImGui::SetNextWindowViewport(viewport->ID);
  ImGui::SetNextWindowBgAlpha(0.0f);

  constexpr ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                            ImGuiWindowFlags_NoNavFocus;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::Begin("DockSpace Demo", nullptr, window_flags);
  ImGui::PopStyleVar(3);

  ImGuiID dockspace_id = ImGui::GetID("Dockspace");
  ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
  ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
  ImGui::End();
}

void FrogRenderer::GuiDrawFsrWindow()
{
  ImGui::Begin("FSR 2");
#ifdef FROGRENDER_FSR2_ENABLE
  if (fsr2Enable)
  {
    ImGui::Text("Performance: %f ms", fsr2Performance);
  }
  else
  {
    ImGui::Text("Performance: ---");
  }

  if (ImGui::Checkbox("Enable FSR 2", &fsr2Enable))
  {
    shouldResizeNextFrame = true;
  }

  if (!fsr2Enable)
  {
    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
  }

  float ratio = fsr2Ratio;
  if (ImGui::RadioButton("AA (1.0x)", ratio == 1.0f))
    ratio = 1.0f;
  if (ImGui::RadioButton("Ultra Quality (1.3x)", ratio == 1.3f))
    ratio = 1.3f;
  if (ImGui::RadioButton("Quality (1.5x)", ratio == 1.5f))
    ratio = 1.5f;
  if (ImGui::RadioButton("Balanced (1.7x)", ratio == 1.7f))
    ratio = 1.7f;
  if (ImGui::RadioButton("Performance (2.0x)", ratio == 2.0f))
    ratio = 2.0f;
  if (ImGui::RadioButton("Ultra Performance (3.0x)", ratio == 3.0f))
    ratio = 3.0f;
  ImGui::SliderFloat("RCAS Strength", &fsr2Sharpness, 0, 1);

  if (ratio != fsr2Ratio)
  {
    fsr2Ratio = ratio;
    shouldResizeNextFrame = true;
  }

  if (!fsr2Enable)
  {
    ImGui::PopStyleVar();
    ImGui::PopItemFlag();
  }
#else
  ImGui::Text("Compile with FROGRENDER_FSR2_ENABLE defined to see FSR 2 options");
#endif
  ImGui::End();
}

void FrogRenderer::OnGui([[maybe_unused]] double dt)
{
  GuiDrawDockspace();

  ImGui::ShowDemoWindow();

  ImGui::Begin("Reflective Shadow Maps");
  ImGui::Text("Performance: %f ms", rsmPerformance);
  frame.rsm->DrawGui();
  ImGui::End();

  ImGui::GetStyle().WindowMenuButtonPosition = ImGuiDir_None;

  GuiDrawFsrWindow();

  ImGui::Begin("glTF Viewer");
  ImGui::Text("Framerate: %.0f Hertz", 1 / dt);

  if (ImGui::Checkbox("Use GUI viewport size", &useGuiViewportSizeForRendering))
  {
    if (useGuiViewportSizeForRendering == false)
    {
      int x, y;
      glfwGetFramebufferSize(window, &x, &y);
      windowWidth = static_cast<uint32_t>(x);
      windowHeight = static_cast<uint32_t>(y);
      shouldResizeNextFrame = true;
    }
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
  {
    ImGui::SetTooltip("If set, the internal render resolution is equal to the viewport.\nOtherwise, it will be the window's framebuffer size,\nresulting in potentially non-square pixels in the viewport");
  }

  ImGui::SliderFloat("Sun Azimuth", &sunAzimuth, -3.1415f, 3.1415f);
  ImGui::SliderFloat("Sun Elevation", &sunElevation, -3.1415f, 3.1415f);
  ImGui::ColorEdit3("Sun Color", &sunColor[0], ImGuiColorEditFlags_Float);
  ImGui::SliderFloat("Sun Strength", &sunStrength, 0, 20);

  ImGui::Separator();

  ImGui::Text("Shadow");

  auto SliderUint = [](const char* label, uint32_t* v, uint32_t v_min, uint32_t v_max) -> bool
  {
    int tempv = static_cast<int>(*v);
    if (ImGui::SliderInt(label, &tempv, static_cast<int>(v_min), static_cast<int>(v_max)))
    {
      *v = static_cast<uint32_t>(tempv);
      return true;
    }
    return false;
  };

  int shadowMode = shadowUniforms.shadowMode;
  ImGui::RadioButton("PCF", &shadowMode, 0);
  ImGui::SameLine();
  ImGui::RadioButton("SMRT", &shadowMode, 1);
  shadowUniforms.shadowMode = shadowMode;

  if (shadowMode == 0)
  {
    SliderUint("PCF Samples", &shadowUniforms.pcfSamples, 1, 16);
    ImGui::SliderFloat("PCF Radius", &shadowUniforms.pcfRadius, 0, 0.01f, "%.4f");
  }
  else if (shadowMode == 1)
  {
    SliderUint("Shadow Rays", &shadowUniforms.shadowRays, 1, 10);
    SliderUint("Steps Per Ray", &shadowUniforms.stepsPerRay, 1, 20);
    ImGui::SliderFloat("Ray Step Size", &shadowUniforms.rayStepSize, 0.01f, 1.0f);
    ImGui::SliderFloat("Heightmap Thickness", &shadowUniforms.heightmapThickness, 0.05f, 1.0f);
    ImGui::SliderFloat("Light Spread", &shadowUniforms.sourceAngleRad, 0.001f, 0.3f);
  }

  ImGui::BeginTabBar("tabbed");
  if (ImGui::BeginTabItem("G-Buffers"))
  {
    float aspect = float(renderWidth) / renderHeight;
    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(frame.gAlbedoSwizzled.value().Handle())), {100 * aspect, 100}, {0, 1}, {1, 0});
    ImGui::SameLine();
    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(frame.gNormalSwizzled.value().Handle())), {100 * aspect, 100}, {0, 1}, {1, 0});
    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(frame.gDepthSwizzled.value().Handle())), {100 * aspect, 100}, {0, 1}, {1, 0});
    ImGui::SameLine();
    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(frame.gRsmIlluminanceSwizzled.value().Handle())), {100 * aspect, 100}, {0, 1}, {1, 0});
    ImGui::EndTabItem();
  }
  if (ImGui::BeginTabItem("RSM Buffers"))
  {
    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(rsmDepthSwizzled.Handle())), {100, 100}, {0, 1}, {1, 0});
    ImGui::SameLine();
    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(rsmNormalSwizzled.Handle())), {100, 100}, {0, 1}, {1, 0});
    ImGui::SameLine();
    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(rsmFluxSwizzled.Handle())), {100, 100}, {0, 1}, {1, 0});
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
  ImGui::End();

  // Draw viewport
  constexpr auto viewportFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  ImGui::Begin("Viewport", nullptr, viewportFlags);

  const auto viewportContentSize = ImGui::GetContentRegionAvail();

  if (useGuiViewportSizeForRendering)
  {
    if (viewportContentSize.x != windowWidth || viewportContentSize.y != windowHeight)
    {
      windowWidth = (uint32_t)viewportContentSize.x;
      windowHeight = (uint32_t)viewportContentSize.y;
      shouldResizeNextFrame = true;
    }
  }

  const auto viewportContentOffset = []() -> glm::vec2
  {
    auto vMin = ImGui::GetWindowContentRegionMin();
    return {
      vMin.x + ImGui::GetWindowPos().x,
      vMin.y + ImGui::GetWindowPos().y,
    };
  }();

  aspectRatio = viewportContentSize.x / viewportContentSize.y;

  ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(frame.colorLdrWindowRes.value().Handle())), viewportContentSize, {0, 1}, {1, 0});
  ImGui::End();
  ImGui::PopStyleVar();

  GuiDrawMagnifier(viewportContentOffset, {viewportContentSize.x, viewportContentSize.y});
}