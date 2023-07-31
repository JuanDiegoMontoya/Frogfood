#include "FrogRenderer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <string>

void FrogRenderer::InitGui()
{
  ImGui::GetIO().Fonts->AddFontFromFileTTF("textures/RobotoCondensed-Regular.ttf", 18);

  ImGui::StyleColorsDark();
  
  auto& style = ImGui::GetStyle();
  style.Colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
  style.Colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  style.Colors[ImGuiCol_PopupBg] = ImVec4(0.19f, 0.19f, 0.19f, 0.92f);
  style.Colors[ImGuiCol_Border] = ImVec4(0.19f, 0.19f, 0.19f, 0.29f);
  style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
  style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
  style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
  style.Colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
  style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
  style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
  style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
  style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
  style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
  style.Colors[ImGuiCol_CheckMark] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
  style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
  style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
  style.Colors[ImGuiCol_Button] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
  style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
  style.Colors[ImGuiCol_Header] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
  style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
  style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
  style.Colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
  style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
  style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
  style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
  style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
  style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
  style.Colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
  style.Colors[ImGuiCol_TabHovered] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
  style.Colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
  style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
  style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
  style.Colors[ImGuiCol_DockingPreview] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
  style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_PlotHistogram] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
  style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
  style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
  style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
  style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
  style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
  style.Colors[ImGuiCol_NavHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 0.70f);
  style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
  style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

  style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0, 0, 0, 0);
  style.WindowMenuButtonPosition = ImGuiDir_None;
}

void FrogRenderer::GuiDrawMagnifier(glm::vec2 viewportContentOffset, glm::vec2 viewportContentSize, bool viewportIsHovered)
{
  bool magnifierLock = true;

  if (ImGui::IsKeyDown(ImGuiKey_Space) && viewportIsHovered)
  {
    magnifierLock = false;
  }

  ImGui::Begin(("Magnifier: " + std::string(magnifierLock ? "Locked" : "Unlocked") + " (Hold Space to unlock)" + "###mag").c_str());

  ImGui::SliderFloat("Zoom (+, -)", &magnifierZoom, 1.0f, 50.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
  if (ImGui::GetKeyPressedAmount(ImGuiKey_KeypadSubtract, 10000, 1))
  {
    magnifierZoom = std::max(magnifierZoom / 1.5f, 1.0f);
  }
  if (ImGui::GetKeyPressedAmount(ImGuiKey_KeypadAdd, 10000, 1))
  {
    magnifierZoom = std::min(magnifierZoom * 1.5f, 50.0f);
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

  if (ImGui::BeginMainMenuBar())
  {
    if (ImGui::BeginMenu("File"))
    {
      if (ImGui::MenuItem("Exit", "Esc"))
      {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
      }

      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View"))
    {
      if (ImGui::MenuItem("Reset layout"))
      {
        // TODO
      }

      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }

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
  ImGui::SliderFloat("Upscale Ratio", &ratio, 1.0f, 3.0f, "%.2f");
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

void FrogRenderer::GuiDrawDebugWindow()
{
  ImGui::Begin("Debug");

  ImGui::Checkbox("Update Culling Frustum", &updateCullingFrustum);
  ImGui::Checkbox("Display Main Frustum", &debugDisplayMainFrustum);
  ImGui::Checkbox("Generate Hi-Z Buffer", &generateHizBuffer);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
  {
    ImGui::SetTooltip("If unchecked, the hi-z buffer is cleared every frame, essentially forcing this test to pass");
  }

  ImGui::Checkbox("Execute Meshlet Generation", &executeMeshletGeneration);
  ImGui::Checkbox("Draw Meshlet AABBs", &drawMeshletAabbs);

  ImGui::End();
}

void FrogRenderer::GuiDrawLightsArray()
{
  if (ImGui::Begin("Lights"))
  {
    // Display properties for all lights
    for (size_t i = 0; i < scene.lights.size(); i++)
    {
      auto& light = scene.lights[i];
      const auto id = "##Light " + std::to_string(i);
      ImGui::Text("%s", id.c_str() + 2); // Skip the two octothorpes at the beginning
      ImGui::SameLine();
      if (ImGui::Button(" X ")) // TODO: replace with better symbol
      {
        std::swap(scene.lights[i], scene.lights.back());
        scene.lights.pop_back();
        i--;
        continue;
      }

      const char* typePreview = "";
      if (light.type == Utility::LightType::DIRECTIONAL)
      {
        typePreview = "Directional";
      }
      else if (light.type == Utility::LightType::POINT)
      {
        typePreview = "Point";
      }
      else if (light.type == Utility::LightType::SPOT)
      {
        typePreview = "Spot";
      }

      if (ImGui::BeginCombo(("Type" + id).c_str(), typePreview))
      {
        // if (ImGui::Selectable("Directional", light.type == Utility::LightType::DIRECTIONAL))
        //{
        //   light.type = Utility::LightType::DIRECTIONAL;
        // }
        if (ImGui::Selectable("Point", light.type == Utility::LightType::POINT))
        {
          light.type = Utility::LightType::POINT;
        }
        else if (ImGui::Selectable("Spot", light.type == Utility::LightType::SPOT))
        {
          light.type = Utility::LightType::SPOT;
        }
        ImGui::EndCombo();
      }

      ImGui::ColorEdit3(("Color" + id).c_str(), &light.color[0], ImGuiColorEditFlags_Float);
      ImGui::DragFloat(("Intensity" + id).c_str(), &light.intensity, 1, 0, 1e6f, light.type == Utility::LightType::DIRECTIONAL ? "%.0f lx" : "%.0f cd");

      ImGui::DragFloat3(("Position" + id).c_str(), &light.position[0], .1f, 0, 0, "%.2f");

      if (light.type != Utility::LightType::POINT)
      {
        if (ImGui::SliderFloat3(("Direction" + id).c_str(), &light.direction[0], -1, 1))
        {
          light.direction = glm::normalize(light.direction);
        }
      }

      if (light.type != Utility::LightType::DIRECTIONAL)
      {
        ImGui::DragFloat(("Range" + id).c_str(), &light.range, 0.2f, 0.0f, 100.0f, "%.2f");
      }

      if (light.type == Utility::LightType::SPOT)
      {
        ImGui::SliderFloat(("Inner cone angle" + id).c_str(), &light.innerConeAngle, 0, 3.14f, "%.2f rad");
        ImGui::SliderFloat(("Outer cone angle" + id).c_str(), &light.outerConeAngle, 0, 3.14f, "%.2f rad");
      }

      ImGui::Separator();
    }

    // Adding new lights
    if (ImGui::Button("Add Light"))
    {
      scene.lights.emplace_back(Utility::GpuLight{
        .color = {1, 1, 1},
        .type = Utility::LightType::POINT,
        .direction = glm::normalize(glm::vec3{0.1f, -1, 0.1f}),
        .intensity = 100,
        .position = {0, 0, 0},
        .range = 1000,
        .innerConeAngle = 0.01f,
        .outerConeAngle = 0.4f,
      });
    }
  }
  ImGui::End();
}

void FrogRenderer::GuiDrawBloomWindow()
{
  if (ImGui::Begin("Bloom"))
  {
    constexpr uint32_t zero = 0;
    constexpr uint32_t eight = 8;
    ImGui::SliderScalar("Passes", ImGuiDataType_U32, &bloomPasses, &zero, &eight, "%u");
    ImGui::SliderFloat("Strength", &bloomStrength, 0, 1, "%.4f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
    ImGui::SliderFloat("Width", &bloomWidth, 0, 2);
  }

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
  ImGui::SliderFloat("Sun Elevation", &sunElevation, 0, 3.1415f);
  ImGui::ColorEdit3("Sun Color", &sunColor[0], ImGuiColorEditFlags_Float);
  ImGui::SliderFloat("Sun Strength", &sunStrength, 0, 50);

  ImGui::Separator();

  ImGui::Text("Shadow");

  auto SliderUint = [](const char* label, uint32_t* v, uint32_t v_min, uint32_t v_max) -> bool
  {
    return ImGui::SliderScalar(label, ImGuiDataType_U32, v, &v_min, &v_max, "%u");
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

    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(frame.gRoughnessMetallicAoSwizzled.value().Handle())), {100 * aspect, 100}, {0, 1}, {1, 0});
    ImGui::SameLine();
    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(frame.gEmissionSwizzled.value().Handle())), {100 * aspect, 100}, {0, 1}, {1, 0});

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
  //ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(frame.visResolve.value().Handle())), viewportContentSize, {0, 1}, {1, 0});
  //ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(frame.gAlbedo.value().Handle())), viewportContentSize, {0, 1}, {1, 0});

  const bool viewportIsHovered = ImGui::IsItemHovered();

  ImGui::End();
  ImGui::PopStyleVar();

  GuiDrawMagnifier(viewportContentOffset, {viewportContentSize.x, viewportContentSize.y}, viewportIsHovered);

  GuiDrawDebugWindow();

  GuiDrawLightsArray();

  GuiDrawBloomWindow();
}