#include "FrogRenderer2.h"

#include <Fvog/Rendering2.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include "ImGui/imgui_impl_fvog.h"

#include "vulkan/vulkan_core.h"
#include <GLFW/glfw3.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <stack>
#include <unordered_map>

#include <fastgltf/util.hpp>

#include <glm/gtc/type_ptr.hpp>

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

  void ImGui_HoverTooltip(const char* fmt, ...)
  {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
      va_list args;
      va_start(args, fmt);
      ImGui::SetTooltipV(fmt, args);
      va_end(args);
    }
  }

  namespace Gui
  {
    // Some of these helpers have been shamelessly "borrowed" from Oxylus.
    // https://github.com/Hatrickek/OxylusEngine/blob/dev/Oxylus/src/UI/OxUI.cpp
    bool BeginProperties(ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame, bool fixedWidth = false, float  widthIfFixed = 0.5f)
    {
      if (ImGui::BeginTable("table", 2, flags))
      {
        ImGui::TableSetupColumn("name", 0);
        if (!fixedWidth)
        {
          ImGui::TableSetupColumn("property");
        }
        else
        {
          ImGui::TableSetupColumn("property", ImGuiTableColumnFlags_WidthFixed, ImGui::GetWindowWidth() * widthIfFixed);
        }
        return true;
      }

      return false;
    }

    void EndProperties()
    {
      ImGui::EndTable();
    }

    int propertyId = 0;

    void PushPropertyId()
    {
      propertyId++;
      ImGui::PushID(propertyId);
    }

    void PopPropertyId()
    {
      ImGui::PopID();
      propertyId--;
    }

    bool BeginProperty(const char* label, const char* tooltip = nullptr, bool alignTextRight = true)
    {
      PushPropertyId();
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();

      if (alignTextRight)
      {
        const auto posX = ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - ImGui::CalcTextSize(label).x - ImGui::GetScrollX();
        if (posX > ImGui::GetCursorPosX())
        {
          ImGui::SetCursorPosX(posX);
        }
      }

      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(label);
      //ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{});
      //ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{});
      //ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{});
      //ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{});
      //ImGui::PushStyleColor(ImGuiCol_BorderShadow, ImVec4{});
      //const auto pressed = ImGui::Button(label);
      //ImGui::PopStyleColor(5);

      if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
        ImGui::SetTooltip("%s", tooltip);
      }

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1.0f);
      return false;
    }

    void EndProperty()
    {
      ImGui::PopStyleVar();
      PopPropertyId();
    }

    void Text(const char* label, const char* fmt, const char* tooltip = nullptr, ...)
    {
      BeginProperty(label, tooltip, false);
      va_list args;
      va_start(args, tooltip);
      ImGui::TextV(fmt, args);
      va_end(args);
      EndProperty();
    }

    bool Checkbox(const char* label, bool* b, const char* tooltip = nullptr)
    {
      bool pressed0 = BeginProperty(label, tooltip, true);
      bool pressed = ImGui::Checkbox((std::string("##") + label).c_str(), b);
      EndProperty();
      if (pressed0 == true)
      {
        *b = !*b;
      }
      return pressed || pressed0;
    }

    bool SliderFloat(const char* label, float* f, float min, float max, const char* tooltip = nullptr, const char* format = nullptr, ImGuiSliderFlags flags = 0)
    {
      BeginProperty(label, tooltip);
      bool pressed = ImGui::SliderFloat(label, f, min, max, format, flags);
      EndProperty();
      return pressed;
    }

    bool SliderScalar(const char* label, ImGuiDataType type, void* s, const void* min, const void* max, const char* tooltip = nullptr, const char* format = nullptr, ImGuiSliderFlags flags = 0)
    {
      BeginProperty(label, tooltip);
      bool pressed = ImGui::SliderScalar(label, type, s, min, max, format, flags);
      EndProperty();
      return pressed;
    }

    bool ColorEdit3(const char* label, float* f3, const char* tooltip, ImGuiColorEditFlags flags)
    {
      BeginProperty(label, tooltip);
      bool pressed = ImGui::ColorEdit3(label, f3, flags);
      EndProperty();
      return pressed;
    }
  }
}

void FrogRenderer2::InitGui()
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

  ImGui_ImplFvog_CreateFontsTexture();

  ImGui::StyleColorsDark();

  auto& style = ImGui::GetStyle();
  style.Colors[ImGuiCol_Text]                  = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  style.Colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
  style.Colors[ImGuiCol_WindowBg]              = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
  style.Colors[ImGuiCol_ChildBg]               = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  style.Colors[ImGuiCol_PopupBg]               = ImVec4(0.19f, 0.19f, 0.19f, 0.95f);
  style.Colors[ImGuiCol_Border]                = ImVec4(0.19f, 0.19f, 0.19f, 0.29f);
  style.Colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
  style.Colors[ImGuiCol_FrameBg]               = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
  style.Colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
  style.Colors[ImGuiCol_FrameBgActive]         = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
  style.Colors[ImGuiCol_TitleBg]               = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_TitleBgActive]         = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
  style.Colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_MenuBarBg]             = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
  style.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
  style.Colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
  style.Colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
  style.Colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
  style.Colors[ImGuiCol_CheckMark]             = ImVec4(0.47f, 0.86f, 0.33f, 1.00f);
  style.Colors[ImGuiCol_SliderGrab]            = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
  style.Colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
  style.Colors[ImGuiCol_Button]                = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
  style.Colors[ImGuiCol_ButtonHovered]         = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
  style.Colors[ImGuiCol_ButtonActive]          = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
  style.Colors[ImGuiCol_Header]                = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_HeaderHovered]         = ImVec4(0.00f, 0.00f, 0.00f, 0.56f);
  style.Colors[ImGuiCol_HeaderActive]          = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
  style.Colors[ImGuiCol_Separator]             = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
  style.Colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
  style.Colors[ImGuiCol_SeparatorActive]       = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
  style.Colors[ImGuiCol_ResizeGrip]            = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
  style.Colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
  style.Colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
  style.Colors[ImGuiCol_Tab]                   = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
  style.Colors[ImGuiCol_TabHovered]            = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
  style.Colors[ImGuiCol_TabActive]             = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
  style.Colors[ImGuiCol_TabUnfocused]          = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
  style.Colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
  style.Colors[ImGuiCol_DockingPreview]        = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
  style.Colors[ImGuiCol_DockingEmptyBg]        = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_PlotLines]             = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_PlotLinesHovered]      = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_PlotHistogram]         = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
  style.Colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
  style.Colors[ImGuiCol_TableBorderLight]      = ImVec4(0.28f, 0.28f, 0.28f, 0.43f);
  style.Colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  style.Colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
  style.Colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
  style.Colors[ImGuiCol_DragDropTarget]        = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
  style.Colors[ImGuiCol_NavHighlight]          = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 0.70f);
  style.Colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
  style.Colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

  style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0, 0, 0, 0);
  style.WindowMenuButtonPosition        = ImGuiDir_None;
  style.IndentSpacing                   = 15;
}

void FrogRenderer2::GuiDrawMagnifier(glm::vec2 viewportContentOffset, glm::vec2 viewportContentSize, bool viewportIsHovered)
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
    //mp.y = viewportContentSize.y - mp.y;

    // Mouse position in UV space
    mp /= glm::vec2(viewportContentSize.x, viewportContentSize.y);
    glm::vec2 magnifierHalfExtentUv = {
      magnifierExtent.x / 2.f / viewportContentSize.x,
      magnifierExtent.y / 2.f / viewportContentSize.y,
    };

    // Calculate the min and max UV of the magnifier window
    glm::vec2 uv0{mp - magnifierHalfExtentUv};
    glm::vec2 uv1{mp + magnifierHalfExtentUv};
    
    ImGui::Image(ImTextureSampler(frame.colorLdrWindowRes.value().ImageView().GetSampledResourceHandle().index, nearestSampler.GetResourceHandle().index),
                 magnifierSize,
                 ImVec2(uv0.x, uv0.y),
                 ImVec2(uv1.x, uv1.y));
  }
  ImGui::End();
}

void FrogRenderer2::GuiDrawDockspace(VkCommandBuffer)
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

void FrogRenderer2::GuiDrawFsrWindow(VkCommandBuffer)
{
  if (ImGui::Begin(ICON_MD_STAR " FSR 2###fsr2_window"))
  {
#ifdef FROGRENDER_FSR2_ENABLE
    if (fsr2Enable)
    {
      // TODO
      //ImGui::Text("Performance: %f ms", fsr2Performance);
    }
    else
    {
      //ImGui::TextUnformatted("Performance: ---");
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

void FrogRenderer2::GuiDrawDebugWindow(VkCommandBuffer)
{
  if (ImGui::Begin(ICON_FA_SCREWDRIVER_WRENCH " Debug###debug_window"))
  {
    const auto items = std::array{"Immediate", "Mailbox", "FIFO"};
    auto pMode = static_cast<int>(presentMode);
    if (ImGui::Combo("Present Mode", &pMode, items.data(), (int)items.size()))
    {
      shouldRemakeSwapchainNextFrame = true;
      presentMode = static_cast<VkPresentModeKHR>(pMode);
    }

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
    ImGui_FlagCheckbox("Show Overdraw", &shadingUniforms.debugFlags, (uint32_t)ShadingDebugFlag::VSM_SHOW_OVERDRAW);
    ImGui_FlagCheckbox("Blend Normals", &shadingUniforms.debugFlags, (uint32_t)ShadingDebugFlag::BLEND_NORMALS);

    ImGui::Separator();
    ImGui::SliderFloat("Alpha Hash Scale", &globalUniforms.alphaHashScale, 1, 3);
    ImGui_FlagCheckbox("Use Hashed Transparency", &globalUniforms.flags, (uint32_t)GlobalFlags::USE_HASHED_TRANSPARENCY);
  }
  ImGui::End();
}

void FrogRenderer2::GuiDrawShadowWindow(VkCommandBuffer commandBuffer)
{
  // TODO: pick icon for this window
  if (ImGui::Begin(" Shadow"))
  {
    ImGui::TextUnformatted("VSM");
    ImGui::SliderFloat("LoD Bias", &vsmUniforms.lodBias, -3, 3, "%.2f");

    if (ImGui::SliderFloat("First Clipmap Width", &vsmFirstClipmapWidth, 1.0f, 100.0f))
    {
      vsmSun.UpdateExpensive(commandBuffer, mainCamera.position, -PolarToCartesian(sunElevation, sunAzimuth), vsmFirstClipmapWidth, vsmDirectionalProjectionZLength);
    }

    if (ImGui::SliderFloat("Projection Z Length", &vsmDirectionalProjectionZLength, 1.0f, 3000.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
    {
      vsmSun.UpdateExpensive(commandBuffer, mainCamera.position, -PolarToCartesian(sunElevation, sunAzimuth), vsmFirstClipmapWidth, vsmDirectionalProjectionZLength);
    }
    
    ImGui_FlagCheckbox("Disable HPB", &vsmUniforms.debugFlags, (uint32_t)Techniques::VirtualShadowMaps::DebugFlag::VSM_HZB_FORCE_SUCCESS);
    ImGui_HoverTooltip("The HPB (hierarchical page buffer) is used to cull\nmeshlets and primitives that are not touching an active page.");
    ImGui_FlagCheckbox("Disable Page Caching", &vsmUniforms.debugFlags, (uint32_t)Techniques::VirtualShadowMaps::DebugFlag::VSM_FORCE_DIRTY_VISIBLE_PAGES);
    ImGui_HoverTooltip("Page caching reduces the amount of per-frame work\nby only drawing to pages whose visibility changed this frame.");

    auto SliderUint = [](const char* label, uint32_t* v, uint32_t v_min, uint32_t v_max) -> bool
    { return ImGui::SliderScalar(label, ImGuiDataType_U32, v, &v_min, &v_max, "%u"); };

    int shadowMode = shadowUniforms.shadowMode;
    ImGui::RadioButton("PCSS", &shadowMode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("SMRT", &shadowMode, 1);
    shadowUniforms.shadowMode = shadowMode;

    if (shadowMode == 0)
    {
      SliderUint("PCF Samples", &shadowUniforms.pcfSamples, 1, 64);
      ImGui::SliderFloat("Light Width", &shadowUniforms.lightWidth, 0, 0.1f, "%.4f");
      ImGui::SliderFloat("Max PCF Radius", &shadowUniforms.maxPcfRadius, 0, 0.1f, "%.4f");
      SliderUint("Blocker Search Samples", &shadowUniforms.blockerSearchSamples, 1, 64);
      ImGui::SliderFloat("Blocker Search Radius", &shadowUniforms.blockerSearchRadius, 0, 0.1f, "%.4f");
    }
    else if (shadowMode == 1)
    {
      SliderUint("Shadow Rays", &shadowUniforms.shadowRays, 1, 10);
      SliderUint("Steps Per Ray", &shadowUniforms.stepsPerRay, 1, 20);
      ImGui::SliderFloat("Ray Step Size", &shadowUniforms.rayStepSize, 0.01f, 1.0f);
      ImGui::SliderFloat("Heightmap Thickness", &shadowUniforms.heightmapThickness, 0.05f, 1.0f);
      ImGui::SliderFloat("Light Spread", &shadowUniforms.sourceAngleRad, 0.001f, 0.3f);
    }
  }
  ImGui::End();
}

void FrogRenderer2::GuiDrawViewer(VkCommandBuffer commandBuffer)
{
  // TODO: pick icon for this window (eye?)
  if (ImGui::Begin("Texture Viewer##viewer_window"))
  {
    bool selectedTex = false;

    struct TexInfo
    {
      bool operator==(const TexInfo&) const noexcept = default;
      std::string name;
      Fvog::GraphicsPipeline* pipeline{};
    };
    auto map = std::unordered_map<const Fvog::Texture*, TexInfo>();
    map[nullptr] = {"None", nullptr};
    map[&vsmContext.pageTables_] = {"VSM Page Tables", &viewerVsmPageTablesPipeline};
    map[&vsmContext.physicalPages_] = {"VSM Physical Pages", &viewerVsmPhysicalPagesPipeline};
    map[&vsmContext.vsmBitmaskHzb_] = {"VSM Bitmask HZB", &viewerVsmBitmaskHzbPipeline};
    map[&vsmContext.physicalPagesOverdrawHeatmap_] = {"VSM Overdraw", &viewerVsmPhysicalPagesOverdrawPipeline};

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
      if (ImGui::Selectable(map[&vsmContext.physicalPagesOverdrawHeatmap_].name.c_str()))
      {
        viewerCurrentTexture = &vsmContext.physicalPagesOverdrawHeatmap_;
        selectedTex = true;
      }

      ImGui::EndCombo();
    }

    if (selectedTex)
    {
      viewerOutputTexture = Fvog::CreateTexture2D(*device_,
                                                  {viewerCurrentTexture->GetCreateInfo().extent.width, viewerCurrentTexture->GetCreateInfo().extent.height},
                                                  Fvog::Format::R8G8B8A8_UNORM,
                                                  Fvog::TextureUsage::ATTACHMENT_READ_ONLY);
    }
    
    if (viewerOutputTexture && viewerCurrentTexture)
    {
      ImGui::SliderInt("Layer", &viewerUniforms.texLayer, 0, std::max(0, (int)viewerCurrentTexture->GetCreateInfo().arrayLayers - 1));
      ImGui::SliderInt("Level", &viewerUniforms.texLevel, 0, std::max(0, (int)viewerCurrentTexture->GetCreateInfo().mipLevels - 1));
      viewerUniforms.textureIndex = viewerCurrentTexture->ImageView().GetSampledResourceHandle().index;
      viewerUniforms.samplerIndex = nearestSampler.GetResourceHandle().index;

      auto ctx = Fvog::Context(*device_, commandBuffer);

      ctx.ImageBarrier(*viewerCurrentTexture, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
      ctx.ImageBarrierDiscard(*viewerOutputTexture, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

      auto attachment = Fvog::RenderColorAttachment{
        .texture = viewerOutputTexture.value().ImageView(),
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      };

      ctx.BeginRendering({.colorAttachments = {{attachment}}});
      ctx.BindGraphicsPipeline(*map.find(viewerCurrentTexture)->second.pipeline);
      ctx.SetPushConstants(viewerUniforms);
      ctx.Draw(3, 1, 0, 0);
      ctx.EndRendering();

      ctx.ImageBarrier(*viewerOutputTexture, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

      ImGui::Image(ImTextureSampler(viewerOutputTexture.value().ImageView().GetSampledResourceHandle().index, nearestSampler.GetResourceHandle().index),
                   ImGui::GetContentRegionAvail());
    }
  }
  ImGui::End();
}

void FrogRenderer2::GuiDrawMaterialsArray(VkCommandBuffer)
{
  if (ImGui::Begin(" Materials##materials_window"))
  {
    int id = 0;
    for (auto& material : scene.materials)
    {
      //ImGui::Text("");
      ImGui::PushID(id++);
      if (ImGui::TreeNode("Material"))
      {
        auto& gpuMat = material.gpuMaterial;
        ImGui::SliderFloat("Alpha Cutoff", &gpuMat.alphaCutoff, 0, 1);
        ImGui::SliderFloat("Metallic Factor", &gpuMat.metallicFactor, 0, 1);
        ImGui::SliderFloat("Roughness Factor", &gpuMat.roughnessFactor, 0, 1);
        ImGui::ColorEdit4("Base Color Factor", &gpuMat.baseColorFactor[0]);
        ImGui::ColorEdit3("Emissive Factor", &gpuMat.emissiveFactor[0]);
        ImGui::DragFloat("Emissive Strength", &gpuMat.emissiveStrength, 0.25f, 0, 10000);
        ImGui::SliderFloat("Normal Scale", &gpuMat.normalXyScale, 0, 1);
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
  }
  ImGui::End();
}

void FrogRenderer2::GuiDrawPerfWindow(VkCommandBuffer)
{
  if (ImGui::Begin("Perf##perf_window", nullptr, 0))
  {
    for (size_t groupIdx = 0; const auto& statGroup : statGroups)
    {
      if (ImPlot::BeginPlot(statGroup.groupName, ImVec2(-1, 250)))
      {
        ImPlot::SetupAxes(nullptr, nullptr, 0, ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit);
        ImPlot::SetupAxisFormat(ImAxis_X1, "%g s");
        ImPlot::SetupAxisFormat(ImAxis_Y1, "%g ms");
        ImPlot::SetupAxisLimits(ImAxis_X1, accumTime - 5.0, accumTime, ImGuiCond_Always);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Outside);

        for (size_t statIdx = 0; const auto& stat : stats[groupIdx])
        {
          ImPlot::SetNextFillStyle(IMPLOT_AUTO_COL, 1.0f);
          ImPlot::PlotLine(statGroup.statNames[statIdx], accumTimes.data.get(), stat.timings.data.get(), (int)stat.timings.size, 0, (int)stat.timings.offset, sizeof(double));
          statIdx++;
        }
        ImPlot::EndPlot();
      }

      if (ImGui::TreeNode(statGroup.groupName))
      {
        ImGui::Text("%-20s: %-10s", "Stat Name", "Average");
        for (size_t statIdx = 0; statIdx < stats[groupIdx].size(); statIdx++)
        {
          const auto& stat = stats[groupIdx][statIdx];
          ImGui::Text("%-20s: %-10fms", statGroup.statNames[statIdx], stat.movingAverage);
          ImGui::Separator();
        }
        ImGui::TreePop();
      }
      groupIdx++;
    }
  }
  ImGui::End();
}

void TraverseLight(std::optional<Utility::GpuLight>& lightOpt)
{
  assert(lightOpt.has_value());
  auto& light = *lightOpt;

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

  //const auto id = std::string("##") + typePreview + " Light " + std::to_string(i);

  if (ImGui::Button(ICON_FA_TRASH_CAN))
  {
    lightOpt.reset();
  }

  ImGui::SameLine();

  const bool isOpen = ImGui::TreeNode((std::string(typePreview) + " Light ").c_str());

  // Hack to right-align the light icon
  ImGui::SameLine(ImGui::GetWindowWidth() - 40);

  ImGui::PushStyleColor(ImGuiCol_Text, glm::packUnorm4x8(glm::vec4(light.color, 1.0f)));
  ImGui::TextUnformatted(typeIcon);
  ImGui::PopStyleColor();

  if (isOpen)
  {
    if (ImGui::BeginCombo("Type", typePreview))
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

    ImGui::ColorEdit3("Color", &light.color[0], ImGuiColorEditFlags_Float);
    ImGui::DragFloat("Intensity", &light.intensity, 1, 0, 1e6f, light.type == Utility::LightType::DIRECTIONAL ? "%.0f lx" : "%.0f cd");

    if (light.type != Utility::LightType::DIRECTIONAL)
    {
      ImGui::DragFloat("Range", &light.range, 0.2f, 0.0f, 100.0f, "%.2f");
    }

    if (light.type == Utility::LightType::SPOT)
    {
      ImGui::SliderFloat("Inner cone angle", &light.innerConeAngle, 0, 3.14f, "%.2f rad");
      ImGui::SliderFloat("Outer cone angle", &light.outerConeAngle, 0, 3.14f, "%.2f rad");
    }

    ImGui::TreePop();
  }
}

void FrogRenderer2::GuiDrawSceneGraphHelper(Utility::Node* node)
{
  ImGui::PushID(static_cast<int>(std::hash<const void*>{}(node)));
  
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
  if (node->children.empty())
  {
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet;
  }

  if (auto* p = std::get_if<Utility::Node*>(&selectedThingy); p && *p == node)
  {
    flags |= ImGuiTreeNodeFlags_Selected;
  }

  const bool isTreeNodeOpen = ImGui::TreeNodeEx("", flags, "%s", node->name.c_str());

  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
  {
    selectedThingy = node;
  }

  if (isTreeNodeOpen)
  {
    for (auto* childNode : node->children)
    {
      GuiDrawSceneGraphHelper(childNode);
    }

    ImGui::TreePop();
  }
  ImGui::PopID();
}

void FrogRenderer2::GuiDrawSceneGraph(VkCommandBuffer)
{
  ImGui::SetNextWindowBgAlpha(0);
  if (ImGui::Begin("Scene Graph##scene_graph_window"))
  {
    const auto as = std::get_if<AtmosphereSelected>(&selectedThingy);
    if (ImGui::TreeNodeEx(ICON_MD_SUNNY " Atmosphere",
          ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | (as ? ImGuiTreeNodeFlags_Selected : 0)))
    {
      if (ImGui::IsItemClicked())
      {
        selectedThingy = AtmosphereSelected{};
      }
      ImGui::TreePop();
    }

    const auto cs = std::get_if<CameraSelected>(&selectedThingy);
    if (ImGui::TreeNodeEx(ICON_FA_CAMERA " Camera",
          ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | (cs ? ImGuiTreeNodeFlags_Selected : 0)))
    {
      if (ImGui::IsItemClicked())
      {
        selectedThingy = CameraSelected{};
      }
      ImGui::TreePop();
    }

    for (auto* node : scene.rootNodes)
    {
      GuiDrawSceneGraphHelper(node);
    }

    if (!ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered() && ImGui::GetIO().MouseClicked[ImGuiMouseButton_Left])
    {
      selectedThingy = std::monostate{};
    }
  }
  ImGui::End();
}

void FrogRenderer2::GuiDrawComponentEditor(VkCommandBuffer commandBuffer)
{
  if (ImGui::Begin("Component Editor###component_editor_window"))
  {
    if (std::get_if<CameraSelected>(&selectedThingy))
    {
      if (ImGui::TreeNode("Postprocessing"))
      {
        Gui::BeginProperties();
        Gui::SliderFloat("Saturation", &tonemapUniforms.saturation, 0, 2, nullptr, "%.2f", ImGuiSliderFlags_NoRoundToFormat);
        Gui::SliderFloat("AgX Linear Section", &tonemapUniforms.agxDsLinearSection, 0, tonemapUniforms.peak, nullptr, "%.2f", ImGuiSliderFlags_NoRoundToFormat);
        Gui::SliderFloat("Compression", &tonemapUniforms.compression, 0, 0.999f, nullptr, "%.2f", ImGuiSliderFlags_NoRoundToFormat);
        bool enableDither = tonemapUniforms.enableDithering;
        Gui::Checkbox("Enable Dither", &enableDither);
        Gui::EndProperties();

        tonemapUniforms.enableDithering = enableDither;
        if (ImGui::Button("Reset"))
        {
          tonemapUniforms = TonemapUniforms{};
        }
        ImGui::TreePop();
      }

      ImGui::Separator();

      if (ImGui::TreeNode("Auto Exposure"))
      {
        Gui::BeginProperties();
        Gui::SliderFloat("Min Exposure", &autoExposureLogMinLuminance, -15.0f, autoExposureLogMaxLuminance, nullptr, "%.3f", ImGuiSliderFlags_NoRoundToFormat);
        Gui::SliderFloat("Max Exposure", &autoExposureLogMaxLuminance, autoExposureLogMinLuminance, 15.0f, nullptr, "%.3f", ImGuiSliderFlags_NoRoundToFormat);
        Gui::SliderFloat("Target Luminance", &autoExposureTargetLuminance, 0.001f, 1, nullptr, "%.3f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
        Gui::SliderFloat("Adjustment Speed", &autoExposureAdjustmentSpeed, 0, 5, nullptr, "%.4f", ImGuiSliderFlags_NoRoundToFormat);
        Gui::EndProperties();
        ImGui::TreePop();
      }

      ImGui::Separator();

      // TODO: ICON_MD_CAMERA looks like a house due to a glyph conflict
      if (ImGui::TreeNode(ICON_MD_CAMERA " Bloom###bloom_window"))
      {
        Gui::BeginProperties();
        Gui::Checkbox("Enable", &bloomEnable);

        if (!bloomEnable)
        {
          ImGui::BeginDisabled();
        }

        constexpr uint32_t zero  = 0;
        constexpr uint32_t eight = 8;
        Gui::SliderScalar("Passes", ImGuiDataType_U32, &bloomPasses, &zero, &eight, nullptr, "%u", ImGuiSliderFlags_AlwaysClamp);
        Gui::SliderFloat("Strength", &bloomStrength, 0, 1, nullptr, "%.4f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
        Gui::SliderFloat("Upscale Width", &bloomWidth, 0, 2);
        Gui::Checkbox("Use Low-Pass Filter", &bloomUseLowPassFilter);

        if (!bloomEnable)
        {
          ImGui::EndDisabled();
        }
        Gui::EndProperties();
        ImGui::TreePop();
      }
    }

    if (std::get_if<AtmosphereSelected>(&selectedThingy))
    {
      Gui::BeginProperties();
      auto sunRotated = Gui::SliderFloat("Sun Azimuth", &sunAzimuth, -3.1415f, 3.1415f);
      sunRotated |= Gui::SliderFloat("Sun Elevation", &sunElevation, 0, 3.1415f);
      if (sunRotated)
      {
        vsmSun.UpdateExpensive(commandBuffer, mainCamera.position, -PolarToCartesian(sunElevation, sunAzimuth), vsmFirstClipmapWidth, vsmDirectionalProjectionZLength);
      }

      Gui::ColorEdit3("Sun Color", &sunColor[0], nullptr, ImGuiColorEditFlags_Float);
      Gui::SliderFloat("Sun Strength", &sunStrength, 0, 500, nullptr, "%.2f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);

      Gui::EndProperties();
    }

    if (auto* p = std::get_if<Utility::Node*>(&selectedThingy))
    {
      auto node = *p;

      ImGui::DragFloat3("Position", glm::value_ptr(node->translation), 0.0625f);
      auto euler = glm::eulerAngles(node->rotation);
      if (ImGui::DragFloat3("Angles", glm::value_ptr(euler), 1.0f / 64))
      {
        node->rotation = glm::quat(euler);
      }
      ImGui::DragFloat3("Scale", glm::value_ptr(node->scale), 1.0f / 64, 1.0f / 32, 10000, "%.3f", ImGuiSliderFlags_NoRoundToFormat);

      if (node->light.has_value())
      {
        TraverseLight(node->light);
      }

      if (!node->meshletInstances.empty())
      {
        // Show list of meshlet instances on this node.
        const bool isInstancesNodeOpen = ImGui::TreeNode("Meshlet instances: ");
        ImGui::SameLine();
        ImGui::Text("%d", (int)node->meshletInstances.size());
        if (isInstancesNodeOpen)
        {
          // Omit instanceId since it isn't populated until scene traversal.
          ImGui::Text("#: (meshlet, material)");
          for (size_t i = 0; i < node->meshletInstances.size(); i++)
          {
            const auto& meshletInstance = node->meshletInstances[i];
            ImGui::Text("%d: (%u, %u)", (int)i, meshletInstance.meshletId, meshletInstance.materialId);
          }
          ImGui::TreePop();
        }
      }
    }
  }
  ImGui::End();
}

void FrogRenderer2::OnGui([[maybe_unused]] double dt, VkCommandBuffer commandBuffer)
{
  if (ImGui::GetKeyPressedAmount(ImGuiKey_F1, 10000, 1))
  {
    showGui = !showGui;

    if (!showGui)
    {
      int x, y;
      glfwGetFramebufferSize(window, &x, &y);
      renderOutputWidth = static_cast<uint32_t>(x);
      renderOutputHeight = static_cast<uint32_t>(y);
      shouldResizeNextFrame = true;
    }
  }

  if (!showGui)
  {
    return;
  }

  GuiDrawDockspace(commandBuffer);

  ImGui::ShowDemoWindow();
  //ImPlot::ShowDemoWindow();

  GuiDrawFsrWindow(commandBuffer);

  ImGui::Begin("glTF Viewer");

  Gui::BeginProperties();

  Gui::Checkbox("Show FPS", &showFpsInfo);
  Gui::Checkbox("Show Scene Info", &showSceneInfo);

  if (Gui::Checkbox("Use GUI viewport size",
        &useGuiViewportSizeForRendering,
        "If set, the internal render resolution is equal to the viewport.\nOtherwise, it will be the window's framebuffer size,\nresulting in potentially non-square pixels in the viewport"))
  {
    if (useGuiViewportSizeForRendering == false)
    {
      int x, y;
      glfwGetFramebufferSize(window, &x, &y);
      renderOutputWidth     = static_cast<uint32_t>(x);
      renderOutputHeight    = static_cast<uint32_t>(y);
      shouldResizeNextFrame = true;
    }
  }

  Gui::EndProperties();

  ImGui::Separator();

  ImGui::BeginTabBar("tabbed");
  if (ImGui::BeginTabItem("G-Buffers"))
  {
    float aspect = float(renderInternalWidth) / renderInternalHeight;

    ImGui::Image(frame.gAlbedoSwizzled.value().GetSampledResourceHandle().index, {100 * aspect, 100});
    ImGui::SameLine();
    ImGui::Image(frame.gNormalSwizzled.value().GetSampledResourceHandle().index, {100 * aspect, 100});

    ImGui::Image(frame.gRoughnessMetallicAoSwizzled.value().GetSampledResourceHandle().index, {100 * aspect, 100});
    ImGui::SameLine();
    ImGui::Image(frame.gEmissionSwizzled.value().GetSampledResourceHandle().index, {100 * aspect, 100});

    ImGui::Image(frame.gDepthSwizzled.value().GetSampledResourceHandle().index, {100 * aspect, 100});

    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
  ImGui::End();

  // Draw viewport
  bool viewportIsHovered          = false;
  glm::vec2 viewportContentOffset = {};
  ImVec2 viewportContentSize      = {};
  constexpr auto viewportFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  if (ImGui::Begin(ICON_MD_BRUSH " Viewport###viewport_window", nullptr, viewportFlags))
  {
    viewportContentSize = ImGui::GetContentRegionAvail();

    if (useGuiViewportSizeForRendering)
    {
      if (viewportContentSize.x != renderOutputWidth || viewportContentSize.y != renderOutputHeight)
      {
        renderOutputWidth = (uint32_t)viewportContentSize.x;
        renderOutputHeight = (uint32_t)viewportContentSize.y;
        shouldResizeNextFrame = true;
      }
    }

    viewportContentOffset = []() -> glm::vec2
    {
      auto vMin = ImGui::GetWindowContentRegionMin();
      return {
        vMin.x + ImGui::GetWindowPos().x,
        vMin.y + ImGui::GetWindowPos().y,
      };
    }();

    aspectRatio = viewportContentSize.x / viewportContentSize.y;
  
    ImGui::Image(frame.colorLdrWindowRes->ImageView().GetSampledResourceHandle().index, viewportContentSize);

    viewportIsHovered = ImGui::IsItemHovered();

    // The FPS viewer is a child of the viewport
    {
      ImGui::PushStyleColor(ImGuiCol_TableBorderLight, ImVec4{});
      ImGui::SetNextWindowPos({viewportContentOffset.x + 5, viewportContentOffset.y});
      ImGui::Begin("FPS Window",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration |
          ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);

      Gui::BeginProperties();

      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});

      if (showFpsInfo)
      {
        Gui::Text("Framerate", "%0.f Hertz", nullptr, 1 / dt);
        Gui::Text("AFPS", "%.0f Rad/s", nullptr, glm::two_pi<double>() / dt);
      }

      if (showSceneInfo)
      {
        Gui::Text("Render In", "%d, %d", nullptr, renderInternalWidth, renderInternalHeight);
        Gui::Text("Render Out", "%d, %d", nullptr, renderOutputWidth, renderOutputHeight);
        Gui::Text("Window", "%d, %d", nullptr, windowFramebufferWidth, windowFramebufferHeight);

        Gui::Text("Meshlets", "%llu", nullptr, scene.meshlets.size());
        Gui::Text("Meshlet Instances", "%llu", nullptr, sceneFlattened.meshletInstances.size());
        Gui::Text("Indices", "%llu", nullptr, scene.indices.size());
        Gui::Text("Vertices", "%llu", nullptr, scene.vertices.size());
        Gui::Text("Primitives", "%llu", nullptr, scene.primitives.size());
        Gui::Text("Lights", "%llu", nullptr, sceneFlattened.lights.size());
        Gui::Text("Materials", "%llu", nullptr, scene.materials.size());
        Gui::Text("Camera Position", "%.2f, %.2f, %.2f", nullptr, mainCamera.position.x, mainCamera.position.y, mainCamera.position.z);
        Gui::Text("Camera Rotation", "%.3f, %.3f", nullptr, mainCamera.pitch, mainCamera.yaw);
      }

      ImGui::PopStyleVar();

      Gui::EndProperties();

      ImGui::End();
      ImGui::PopStyleColor();
    }
  }
  ImGui::End();
  ImGui::PopStyleVar();

  GuiDrawMagnifier(viewportContentOffset, {viewportContentSize.x, viewportContentSize.y}, viewportIsHovered);
  GuiDrawDebugWindow(commandBuffer);
  GuiDrawShadowWindow(commandBuffer);
  GuiDrawViewer(commandBuffer);
  GuiDrawMaterialsArray(commandBuffer);
  GuiDrawPerfWindow(commandBuffer);
  GuiDrawSceneGraph(commandBuffer);
  GuiDrawComponentEditor(commandBuffer);
}