#include "FrogRenderer.h"

#include <Fwog/Rendering.h>

#include <imgui.h>
#include <imgui_internal.h>

#include FWOG_OPENGL_HEADER
#include <GLFW/glfw3.h>

#include <algorithm>
#include <filesystem>
#include <string>

#include "IconsMaterialDesign.h"

#include "IconsFontAwesome6.h"

namespace
{
  const char* g_defaultIniPath = "config/defaultLayout.ini";

  bool ImGui_FlagCheckbox(const char* label, uint32_t* v, uint32_t bit)
  {
    bool isSet = *v & bit;
    const bool ret = ImGui::Checkbox(label, &isSet);
    *v &= ~bit; // Unset the bit
    if (isSet)
    {
      *v |= bit;
    }
    return ret;
  }
}

void FrogRenderer::InitGui()
{
  // Attempt to load default layout, if it exists
  if (std::filesystem::exists(g_defaultIniPath) && !std::filesystem::is_directory(g_defaultIniPath))
  {
    ImGui::GetIO().IniFilename = g_defaultIniPath;
  }

  constexpr float fontSize = 18;
  ImGui::GetIO().Fonts->AddFontFromFileTTF("textures/RobotoCondensed-Regular.ttf", fontSize);
  // constexpr float iconFontSize = fontSize * 2.0f / 3.0f; // if GlyphOffset.y is not biased, uncomment this

  // These fonts appear to interfere, possibly due to having overlapping ranges.
  // Loading FA first appears to cause less breakage
  {
    constexpr float iconFontSize = fontSize * 4.0f / 5.0f;
    static const ImWchar icons_ranges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.GlyphMinAdvanceX = iconFontSize;
    icons_config.GlyphOffset.y = 0; // Hack to realign the icons
    ImGui::GetIO().Fonts->AddFontFromFileTTF("textures/" FONT_ICON_FILE_NAME_FAS, iconFontSize, &icons_config, icons_ranges);
  }

  {
    constexpr float iconFontSize = fontSize;
    static const ImWchar icons_ranges[] = {ICON_MIN_MD, ICON_MAX_16_MD, 0};
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.GlyphMinAdvanceX = iconFontSize;
    icons_config.GlyphOffset.y = 4; // Hack to realign the icons
    ImGui::GetIO().Fonts->AddFontFromFileTTF("textures/" FONT_ICON_FILE_NAME_MD, iconFontSize, &icons_config, icons_ranges);
  }

  ImGui::StyleColorsDark();

  auto& style = ImGui::GetStyle();
  style.Colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
  style.Colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  style.Colors[ImGuiCol_PopupBg] = ImVec4(0.19f, 0.19f, 0.19f, 0.95f);
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

  if (ImGui::Begin((ICON_MD_ZOOM_IN " Magnifier: " + std::string(magnifierLock ? "Locked" : "Unlocked") + " (Hold Space to unlock)" + "###mag").c_str()))
  {
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
  }
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
        ImGui::LoadIniSettingsFromDisk(g_defaultIniPath);
      }

      if (ImGui::MenuItem("Save layout"))
      {
        ImGui::SaveIniSettingsToDisk(g_defaultIniPath);
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
  if (ImGui::Begin(ICON_MD_STAR " FSR 2###fsr2_window"))
  {
#ifdef FROGRENDER_FSR2_ENABLE
    if (fsr2Enable)
    {
      ImGui::Text("Performance: %f ms", fsr2Performance);
    }
    else
    {
      ImGui::TextUnformatted("Performance: ---");
    }

    if (ImGui::Checkbox("Enable FSR 2", &fsr2Enable))
    {
      shouldResizeNextFrame = true;
    }

    if (!fsr2Enable)
    {
      ImGui::BeginDisabled();
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
      ImGui::EndDisabled();
    }
#else
    ImGui::TextUnformatted("Compile with FROGRENDER_FSR2_ENABLE defined to see FSR 2 options");
#endif
  }
  ImGui::End();
}

void FrogRenderer::GuiDrawDebugWindow()
{
  if (ImGui::Begin(ICON_FA_SCREWDRIVER_WRENCH " Debug###debug_window"))
  {
    ImGui::Checkbox("Update Culling Frustum", &updateCullingFrustum);
    ImGui::Checkbox("Display Main Frustum", &debugDisplayMainFrustum);
    ImGui::Checkbox("Generate Hi-Z Buffer", &generateHizBuffer);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
      ImGui::SetTooltip("If unchecked, the hi-z buffer is cleared every frame, essentially forcing this test to pass");
    }

    ImGui::Checkbox("Execute Meshlet Generation", &executeMeshletGeneration);

    ImGui::Separator();
    ImGui::Text("Debug AABBs");
    ImGui::Checkbox("Clear Each Frame##clear_debug_aabbs", &clearDebugAabbsEachFrame);
    ImGui::Checkbox("Draw##draw_debug_aabbs", &drawDebugAabbs);

    ImGui::Separator();
    ImGui::Text("Debug Rects");
    ImGui::Checkbox("Clear Each Frame##clear_debug_rects", &clearDebugRectsEachFrame);
    ImGui::Checkbox("Draw##draw_debug_rects", &drawDebugRects);

    ImGui::SliderInt("Fake Lag", &fakeLag, 0, 100, "%dms");
    ImGui::Checkbox("Render to Screen", &debugRenderToSwapchain);

    ImGui::Separator();
    ImGui::TextUnformatted("Culling");
    ImGui_FlagCheckbox("Meshlet: Frustum", &globalUniforms.flags, (uint32_t)GlobalFlags::CULL_MESHLET_FRUSTUM);
    ImGui_FlagCheckbox("Meshlet: Hi-z", &globalUniforms.flags, (uint32_t)GlobalFlags::CULL_MESHLET_HIZ);
    ImGui_FlagCheckbox("Primitive: Back-facing", &globalUniforms.flags, (uint32_t)GlobalFlags::CULL_PRIMITIVE_BACKFACE);
    ImGui_FlagCheckbox("Primitive: Frustum", &globalUniforms.flags, (uint32_t)GlobalFlags::CULL_PRIMITIVE_FRUSTUM);
    ImGui_FlagCheckbox("Primitive: Small", &globalUniforms.flags, (uint32_t)GlobalFlags::CULL_PRIMITIVE_SMALL);
    ImGui_FlagCheckbox("Primitive: VSM", &globalUniforms.flags, (uint32_t)GlobalFlags::CULL_PRIMITIVE_VSM);

    ImGui::Separator();
    ImGui::TextUnformatted("Virtual Shadow Maps");
    ImGui_FlagCheckbox("Show Clipmap ID", &shadingUniforms.debugFlags, (uint32_t)ShadingDebugFlag::VSM_SHOW_CLIPMAP_ID);
    ImGui_FlagCheckbox("Show Page Address", &shadingUniforms.debugFlags, (uint32_t)ShadingDebugFlag::VSM_SHOW_PAGE_ADDRESS);
    ImGui_FlagCheckbox("Show Page Outlines", &shadingUniforms.debugFlags, (uint32_t)ShadingDebugFlag::VSM_SHOW_PAGE_OUTLINES);
    ImGui_FlagCheckbox("Show Shadow Depth", &shadingUniforms.debugFlags, (uint32_t)ShadingDebugFlag::VSM_SHOW_SHADOW_DEPTH);
    ImGui_FlagCheckbox("Show Dirty Pages", &shadingUniforms.debugFlags, (uint32_t)ShadingDebugFlag::VSM_SHOW_DIRTY_PAGES);
  }
  ImGui::End();
}

void FrogRenderer::GuiDrawLightsArray()
{
  if (ImGui::Begin(ICON_MD_SUNNY "  Lights###lights_window"))
  {
    // Display properties for all lights
    for (size_t i = 0; i < scene.lights.size(); i++)
    {
      auto& light = scene.lights[i];

      const char* typePreview = "";
      const char* typeIcon = "";
      if (light.type == Utility::LightType::DIRECTIONAL)
      {
        typePreview = "Directional";
        typeIcon = ICON_MD_SUNNY "  ";
      }
      else if (light.type == Utility::LightType::POINT)
      {
        typePreview = "Point";
        typeIcon = ICON_FA_LIGHTBULB "  ";
      }
      else if (light.type == Utility::LightType::SPOT)
      {
        typePreview = "Spot";
        typeIcon = ICON_FA_FILTER "  ";
      }

      const auto id = std::string("##") + typePreview + " Light " + std::to_string(i);

      if (ImGui::Button((ICON_FA_TRASH_CAN + id).c_str()))
      {
        // Erasing from the middle is inefficient, but leads to more intuitive UX compared to swap & pop
        scene.lights.erase(scene.lights.begin() + i);
        i--;
        continue;
      }

      ImGui::SameLine();

      const bool isOpen = ImGui::TreeNode(id.c_str() + 2);

      // Hack to right-align the light icon
      ImGui::SameLine(ImGui::GetWindowWidth() - 40);

      ImGui::PushStyleColor(ImGuiCol_Text, glm::packUnorm4x8(glm::vec4(light.color, 1.0f)));
      ImGui::TextUnformatted(typeIcon);
      ImGui::PopStyleColor();

      if (isOpen)
      {
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

        ImGui::TreePop();
      }
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
        .range = 100,
        .innerConeAngle = 0.01f,
        .outerConeAngle = 0.4f,
      });
    }
  }
  ImGui::End();
}

void FrogRenderer::GuiDrawBloomWindow()
{
  if (ImGui::Begin(ICON_MD_CAMERA " Bloom###bloom_window"))
  {
    ImGui::Checkbox("Enable", &bloomEnable);

    if (!bloomEnable)
    {
      ImGui::BeginDisabled();
    }

    constexpr uint32_t zero = 0;
    constexpr uint32_t eight = 8;
    ImGui::SliderScalar("Passes", ImGuiDataType_U32, &bloomPasses, &zero, &eight, "%u");
    ImGui::SliderFloat("Strength", &bloomStrength, 0, 1, "%.4f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
    ImGui::SliderFloat("Upscale Width", &bloomWidth, 0, 2);
    ImGui::Checkbox("Use Low-Pass Filter", &bloomUseLowPassFilter);

    if (!bloomEnable)
    {
      ImGui::EndDisabled();
    }
  }
  ImGui::End();
}

void FrogRenderer::GuiDrawAutoExposureWindow()
{
  if (ImGui::Begin(ICON_MD_BRIGHTNESS_AUTO " Auto Exposure###auto_exposure_window"))
  {
    ImGui::SliderFloat("Min Exposure", &autoExposureLogMinLuminance, -15.0f, autoExposureLogMaxLuminance, "%.3f", ImGuiSliderFlags_NoRoundToFormat);
    ImGui::SliderFloat("Max Exposure", &autoExposureLogMaxLuminance, autoExposureLogMinLuminance, 15.0f, "%.3f", ImGuiSliderFlags_NoRoundToFormat);
    ImGui::SliderFloat("Target Luminance", &autoExposureTargetLuminance, 0.001f, 1, "%.3f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
    ImGui::SliderFloat("Adjustment Speed", &autoExposureAdjustmentSpeed, 0, 5, "%.4f", ImGuiSliderFlags_NoRoundToFormat);
  }

  ImGui::End();
}

void FrogRenderer::GuiDrawCameraWindow()
{
  if (ImGui::Begin(ICON_FA_CAMERA " Camera###camera_window"))
  {
    ImGui::SliderFloat("Saturation", &tonemapUniforms.saturation, 0, 2, "%.2f", ImGuiSliderFlags_NoRoundToFormat);
    ImGui::SliderFloat("AgX Linear Section", &tonemapUniforms.agxDsLinearSection, 0, tonemapUniforms.peak, "%.2f", ImGuiSliderFlags_NoRoundToFormat);
    ImGui::SliderFloat("Compression", &tonemapUniforms.compression, 0, 0.999f, "%.2f", ImGuiSliderFlags_NoRoundToFormat);
    bool enableDither = tonemapUniforms.enableDithering;
    ImGui::Checkbox("Enable Dither", &enableDither);
    tonemapUniforms.enableDithering = enableDither;
    if (ImGui::Button("Reset"))
    {
      tonemapUniforms = TonemapUniforms{};
    }
  }
  ImGui::End();
}

void FrogRenderer::GuiDrawShadowWindow()
{
  // TODO: pick icon for this window
  if (ImGui::Begin(" Shadow"))
  {
    ImGui::TextUnformatted("VSM");
    ImGui::SliderFloat("LoD Bias", &vsmUniforms.lodBias, -3, 3, "%.2f");

    if (ImGui::SliderFloat("First Clipmap Width", &vsmFirstClipmapWidth, 1.0f, 100.0f))
    {
      vsmSun.UpdateExpensive(mainCamera.position, -PolarToCartesian(sunElevation, sunAzimuth), vsmFirstClipmapWidth, vsmDirectionalProjectionZLength);
    }

    if (ImGui::SliderFloat("Projection Z Length", &vsmDirectionalProjectionZLength, 1.0f, 3000.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
    {
      vsmSun.UpdateExpensive(mainCamera.position, -PolarToCartesian(sunElevation, sunAzimuth), vsmFirstClipmapWidth, vsmDirectionalProjectionZLength);
    }
    
    ImGui_FlagCheckbox("Disable Page Culling", &vsmUniforms.debugFlags, (uint32_t)Techniques::VirtualShadowMaps::DebugFlag::VSM_HZB_FORCE_SUCCESS);
    ImGui_FlagCheckbox("Disable Page Caching", &vsmUniforms.debugFlags, (uint32_t)Techniques::VirtualShadowMaps::DebugFlag::VSM_FORCE_DIRTY_VISIBLE_PAGES);
  }
  ImGui::End();
}

void FrogRenderer::GuiDrawViewer()
{
  // TODO: pick icon for this window (eye?)
  if (ImGui::Begin(" Viewer##viewer_window"))
  {
    bool selectedTex = false;

    struct TexInfo
    {
      bool operator==(const TexInfo&) const noexcept = default;
      std::string name;
      Fwog::GraphicsPipeline* pipeline;
    };
    auto map = std::unordered_map<const Fwog::Texture*, TexInfo>();
    map[nullptr] = {"None", nullptr};
    map[&vsmContext.pageTables_] = {"VSM Page Tables", &viewerVsmPageTablesPipeline};
    map[&vsmContext.physicalPages_] = {"VSM Physical Pages", &viewerVsmPhysicalPagesPipeline};
    map[&vsmContext.vsmBitmaskHzb_] = {"VSM Bitmask HZB", &viewerVsmBitmaskHzbPipeline};

    if (ImGui::BeginCombo("Texture", map.find(viewerCurrentTexture)->second.name.c_str()))
    {
      if (ImGui::Selectable(map[nullptr].name.c_str()))
      {
        viewerCurrentTexture = nullptr;
      }
      if (ImGui::Selectable(map[&vsmContext.pageTables_].name.c_str()))
      {
        viewerCurrentTexture = &vsmContext.pageTables_;
        selectedTex = true;
      }
      if (ImGui::Selectable(map[&vsmContext.physicalPages_].name.c_str()))
      {
        viewerCurrentTexture = &vsmContext.physicalPages_;
        selectedTex = true;
      }
      if (ImGui::Selectable(map[&vsmContext.vsmBitmaskHzb_].name.c_str()))
      {
        viewerCurrentTexture = &vsmContext.vsmBitmaskHzb_;
        selectedTex = true;
      }

      ImGui::EndCombo();
    }

    if (selectedTex)
    {
      viewerOutputTexture = Fwog::CreateTexture2D({viewerCurrentTexture->Extent().width, viewerCurrentTexture->Extent().height}, Fwog::Format::R8G8B8A8_UNORM);
      glTextureParameteri(viewerOutputTexture->Handle(), GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTextureParameteri(viewerOutputTexture->Handle(), GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    
    if (viewerOutputTexture && viewerCurrentTexture)
    {
      ImGui::SliderInt("Layer", &viewerUniforms.texLayer, 0, std::max(0, (int)viewerCurrentTexture->GetCreateInfo().arrayLayers - 1));
      ImGui::SliderInt("Level", &viewerUniforms.texLevel, 0, std::max(0, (int)viewerCurrentTexture->GetCreateInfo().mipLevels - 1));

      viewerUniformsBuffer.UpdateData(viewerUniforms);

      auto attachment = Fwog::RenderColorAttachment{
        .texture = viewerOutputTexture.value(),
        .loadOp = Fwog::AttachmentLoadOp::DONT_CARE,
      };
      Fwog::Render(
        {
          .name = "Texture Viewer",
          .colorAttachments = {&attachment, 1},
        },
        [&]
        {
          Fwog::Cmd::BindGraphicsPipeline(*map.find(viewerCurrentTexture)->second.pipeline);
          Fwog::Cmd::BindSampledImage(0,
                                      *viewerCurrentTexture,
                                      Fwog::Sampler(Fwog::SamplerState{
                                        .minFilter = Fwog::Filter::NEAREST,
                                        .magFilter = Fwog::Filter::NEAREST,
                                        .mipmapFilter = Fwog::Filter::NEAREST,
                                      }));
          Fwog::Cmd::BindUniformBuffer(0, viewerUniformsBuffer);
          Fwog::Cmd::Draw(3, 1, 0, 0);
        });

      ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(viewerOutputTexture.value().Handle())), ImGui::GetContentRegionAvail(), {0, 1}, {1, 0});
    }
  }
  ImGui::End();
}

void FrogRenderer::OnGui(double dt)
{
  GuiDrawDockspace();

  //ImGui::ShowDemoWindow();

  ImGui::Begin("Reflective Shadow Maps");
  ImGui::Text("Performance: %f ms", rsmPerformance);
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
    ImGui::SetTooltip("If set, the internal render resolution is equal to the viewport.\nOtherwise, it will be the window's framebuffer size,\nresulting in "
                      "potentially non-square pixels in the viewport");
  }

  auto sunRotated = ImGui::SliderFloat("Sun Azimuth", &sunAzimuth, -3.1415f, 3.1415f);
  sunRotated |= ImGui::SliderFloat("Sun Elevation", &sunElevation, 0, 3.1415f);
  if (sunRotated)
  {
    vsmSun.UpdateExpensive(mainCamera.position, -PolarToCartesian(sunElevation, sunAzimuth), vsmFirstClipmapWidth, vsmDirectionalProjectionZLength);
  }

  ImGui::ColorEdit3("Sun Color", &sunColor[0], ImGuiColorEditFlags_Float);
  ImGui::SliderFloat("Sun Strength", &sunStrength, 0, 500, "%.2f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);

  ImGui::Separator();

  ImGui::TextUnformatted("Shadow");

  auto SliderUint = [](const char* label, uint32_t* v, uint32_t v_min, uint32_t v_max) -> bool
  { return ImGui::SliderScalar(label, ImGuiDataType_U32, v, &v_min, &v_max, "%u"); };

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

    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
  ImGui::End();

  // Draw viewport
  constexpr auto viewportFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  ImGui::Begin(ICON_MD_BRUSH " Viewport###viewport_window", nullptr, viewportFlags);

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

  const bool viewportIsHovered = ImGui::IsItemHovered();

  ImGui::End();
  ImGui::PopStyleVar();

  GuiDrawMagnifier(viewportContentOffset, {viewportContentSize.x, viewportContentSize.y}, viewportIsHovered);
  GuiDrawDebugWindow();
  GuiDrawLightsArray();
  GuiDrawBloomWindow();
  GuiDrawAutoExposureWindow();
  GuiDrawCameraWindow();
  GuiDrawShadowWindow();
  GuiDrawViewer();
}
