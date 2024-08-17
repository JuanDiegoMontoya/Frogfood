#include "FrogRenderer2.h"
#include "Renderables.h"
#include "Scene.h"

#include <Fvog/Rendering2.h>
#include <Fvog/detail/ApiToEnum2.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include "ImGui/imgui_impl_fvog.h"

#include "RendererUtilities.h"

#include "vulkan/vulkan_core.h"
#include <GLFW/glfw3.h>

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <stack>
#include <unordered_map>

#include <fastgltf/util.hpp>

#include <glm/gtc/type_ptr.hpp>

#include "IconsMaterialDesign.h"

#include "IconsFontAwesome6.h"

#include "vk_mem_alloc.h"

namespace
{
  const char* g_defaultIniPath = "config/defaultLayout.ini";

  uint32_t SetBits(uint32_t v, uint32_t mask, bool apply)
  {
    v &= ~mask; // Unset bits in the mask
    if (apply)
    {
      v |= mask;
    }
    return v;
  }

  bool ImGui_FlagCheckbox(const char* label, uint32_t* v, uint32_t bit)
  {
    bool isSet = *v & bit;
    const bool ret = ImGui::Checkbox(label, &isSet);
    *v = SetBits(*v, bit, isSet);
    return ret;
  }

  void ImGui_HoverTooltip(const char* fmt, ...)
  {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
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
    constexpr ImGuiTableFlags defaultPropertiesFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame;
    bool BeginProperties(ImGuiTableFlags flags = defaultPropertiesFlags, bool fixedWidth = false, float widthIfFixed = 0.5f)
    {
      if (ImGui::BeginTable("table", 2, flags))
      {
        ImGui::TableSetupColumn("name");
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
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
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

    bool BeginSelectableProperty(const char* label, const char* tooltip = nullptr, bool alignTextRight = true, bool selected = false, ImGuiSelectableFlags flags = {})
    {
      PushPropertyId();
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
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
      
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{});
      ImGui::PushStyleColor(ImGuiCol_BorderShadow, ImVec4{});
      const auto pressed = ImGui::Selectable(label, selected, flags);
      ImGui::PopStyleColor(2);

      if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
        ImGui::SetTooltip("%s", tooltip);
      }

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1.0f);
      return pressed;
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
      bool pressed0 = BeginSelectableProperty(label, tooltip, true, false, ImGuiSelectableFlags_SpanAllColumns);
      ImGui::PushID(label);
      bool pressed = ImGui::Checkbox("", b);
      ImGui::PopID();
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

    bool DragFloat(const char* label, float* f, float speed = 1, float min = 0, float max = 0, const char* tooltip = nullptr, const char* format = nullptr, ImGuiSliderFlags flags = 0)
    {
      BeginProperty(label, tooltip);
      bool pressed = ImGui::DragFloat(label, f, speed, min, max, format, flags);
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

    bool ColorEdit3(const char* label, float* f3, const char* tooltip = nullptr, ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float)
    {
      BeginProperty(label, tooltip);
      bool pressed = ImGui::ColorEdit3(label, f3, flags);
      EndProperty();
      return pressed;
    }

    bool ColorEdit4(const char* label, float* f4, const char* tooltip = nullptr, ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float)
    {
      BeginProperty(label, tooltip);
      bool pressed = ImGui::ColorEdit4(label, f4, flags);
      EndProperty();
      return pressed;
    }

    bool RadioButton(const char* label, bool active, const char* tooltip = nullptr)
    {
      BeginProperty(label, tooltip);
      ImGui::PushID(label);
      bool pressed = ImGui::RadioButton("", active);
      ImGui::PopID();
      EndProperty();
      return pressed;
    }

    // Thingy for putting 16x16 icons after the arrow in a tree node
    constexpr ImGuiTreeNodeFlags defaultTreeNodeImageFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
    bool TreeNodeWithImage16(const char* label, Fvog::Texture& texture, std::optional<Fvog::Sampler> sampler = {}, ImGuiTreeNodeFlags flags = defaultTreeNodeImageFlags)
    {
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{});
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{});
      const auto open = ImGui::TreeNodeEx((std::string("###") + label).c_str(), flags);
      ImGui::SameLine();
      ImGui::Image(ImTextureSampler(texture.ImageView().GetSampledResourceHandle().index, sampler ? sampler->GetResourceHandle().index : 0), {16, 16});
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      ImGui::Text(" %s", label);
      ImGui::PopStyleVar(2);
      return open;
    }

    bool DragFloat3(const char* label, float* f3, float speed, float min = 0, float max = 0, const char* format = "%.3f", ImGuiSliderFlags flags = 0, const char* tooltip = nullptr)
    {
      const float frameHeight = ImGui::GetFrameHeight();
      const ImVec2 buttonSize = {2.f, frameHeight};

      const auto buttonFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoBorder | ImGuiColorEditFlags_NoPicker |
                               ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_NoTooltip;

      BeginProperty(label, tooltip);

      // The cursor falls a few pixels off the edge on the last element, causing it to be clipped and appear smaller than the other items.
      // Presumably this is caused by the tiny color buttons and item spacing shenanigans we do.
      // This magic somewhat mitigates the issue by allocating a bit less than a third of the available area to each big widget,
      // giving some room for items to safely spill into (without being clipped).
      // The downside to this approach is that the first time opening such a table causes it to slowly "expand" to fill its space.
      constexpr auto magic = 3.25f;
      const auto avail = ImGui::GetContentRegionAvail().x;

      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
      ImGui::PushID(label);

      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, 0});
      ImGui::ColorButton("##0", {1, 0, 0, 1}, buttonFlags, buttonSize);
      ImGui::SameLine();
      ImGui::PushItemWidth(avail / magic);
      bool a = ImGui::DragFloat("##x", &f3[0], speed, min, max, format, flags);
      ImGui::PopItemWidth();
      ImGui::PopStyleVar();
      
      ImGui::SameLine();
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, 0});
      ImGui::ColorButton("##1", {0, 1, 0, 1}, buttonFlags, buttonSize);
      ImGui::SameLine();
      ImGui::PushItemWidth(avail / magic);
      bool b = ImGui::DragFloat("##y", &f3[1], speed, min, max, format, flags);
      ImGui::PopItemWidth();
      ImGui::PopStyleVar();

      ImGui::SameLine();

      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, 0});
      ImGui::ColorButton("##2", {0, 0, 1, 1}, buttonFlags, buttonSize);
      ImGui::SameLine();
      ImGui::PushItemWidth(avail / magic);
      bool c = ImGui::DragFloat("##z", &f3[2], speed, min, max, format, flags);
      ImGui::PopItemWidth();
      ImGui::PopStyleVar();

      ImGui::PopID();
      ImGui::PopStyleVar();

      EndProperty();

      return a || b || c;
    }

    bool FlagCheckbox(const char* label, uint32_t* bitfield, uint32_t bits, const char* tooltip = nullptr)
    {
      bool pressed0 = BeginSelectableProperty(label, tooltip, true, false, ImGuiSelectableFlags_SpanAllColumns);
      ImGui::PushID(label);
      bool pressed = ImGui_FlagCheckbox("", bitfield, bits);
      ImGui::PopID();
      EndProperty();
      if (pressed0 == true)
      {
        *bitfield = SetBits(*bitfield, bits, !(*bitfield & bits));
      }
      return pressed || pressed0;
    }
  }

  const char* StringifyRendererColorSpace(uint32_t colorSpace)
  {
    switch (colorSpace)
    {
    case COLOR_SPACE_sRGB_LINEAR: return "sRGB_LINEAR";
    case COLOR_SPACE_scRGB_LINEAR: return "scRGB_LINEAR";
    case COLOR_SPACE_sRGB_NONLINEAR: return "sRGB_NONLINEAR";
    case COLOR_SPACE_BT2020_LINEAR: return "BT2020_LINEAR";
    case COLOR_SPACE_HDR10_ST2084: return "HDR10_ST2084";
    default: return "Unknown color space";
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

  guiIcons.emplace("icon_object", LoadTextureShrimple(*device_, "textures/icons/icon_object.png"));
  //guiIcons.emplace("icon_object", LoadTextureShrimple(*device_, "textures/icons/icon_mesh_cube.png"));
  guiIcons.emplace("icon_camera", LoadTextureShrimple(*device_, "textures/icons/icon_camera.png"));
  guiIcons.emplace("icon_sun", LoadTextureShrimple(*device_, "textures/icons/icon_sun.png"));
  guiIcons.emplace("chroma_scope", LoadTextureShrimple(*device_, "textures/icons/icon_chroma_scope.png"));
  guiIcons.emplace("rgb", LoadTextureShrimple(*device_, "textures/icons/icon_rgb.png"));
  guiIcons.emplace("camera_flash", LoadTextureShrimple(*device_, "textures/icons/icon_camera_flash.png"));
  guiIcons.emplace("particles", LoadTextureShrimple(*device_, "textures/icons/particles.png"));
  guiIcons.emplace("ease_in_out", LoadTextureShrimple(*device_, "textures/icons/ease_in_out.png"));
  guiIcons.emplace("histogram", LoadTextureShrimple(*device_, "textures/icons/histogram.png"));
  guiIcons.emplace("curve", LoadTextureShrimple(*device_, "textures/icons/curve.png"));
  guiIcons.emplace("scene", LoadTextureShrimple(*device_, "textures/icons/scene.png"));
  guiIcons.emplace("lattice", LoadTextureShrimple(*device_, "textures/icons/lattice.png"));
  guiIcons.emplace("lamp_spot", LoadTextureShrimple(*device_, "textures/icons/lamp_spot.png"));
  guiIcons.emplace("lamp_point", LoadTextureShrimple(*device_, "textures/icons/lamp_point.png"));
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

    ImGui::Image(ImTextureSampler(frame.colorLdrWindowRes.value().ImageView().GetSampledResourceHandle().index,
                   nearestSampler.GetResourceHandle().index,
                   tonemapUniforms.tonemapOutputColorSpace),
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
    // TODO: make this only display available present modes
    const auto items = std::array{"Immediate", "Mailbox", "FIFO"};
    auto pMode = static_cast<int>(presentMode);
    if (ImGui::Combo("Present Mode", &pMode, items.data(), (int)items.size()))
    {
      shouldRemakeSwapchainNextFrame = true;
      presentMode = static_cast<VkPresentModeKHR>(pMode);
    }

    const auto stringifySurfaceFormat = [](VkSurfaceFormatKHR surfaceFormat)
    {
      return std::string(Fvog::detail::FormatToString(Fvog::detail::VkToFormat(surfaceFormat.format))) + " | " +
             Fvog::detail::VkColorSpaceToString(surfaceFormat.colorSpace);
    };

    const auto currentFormatStr = stringifySurfaceFormat(nextSwapchainFormat_);
    if (ImGui::BeginCombo("Surface Format", currentFormatStr.c_str(), ImGuiComboFlags_HeightLarge))
    {
      ImGui::BeginTable("surface formats", 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody);
      ImGui::TableSetupColumn("name");

      for (auto surfaceFormat : availableSurfaceFormats_)
      {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::Selectable(stringifySurfaceFormat(surfaceFormat).c_str(),
              surfaceFormat.colorSpace == nextSwapchainFormat_.colorSpace && surfaceFormat.format == nextSwapchainFormat_.format,
              ImGuiSelectableFlags_SpanAllColumns))
        {
          nextSwapchainFormat_ = surfaceFormat;
          shouldRemakeSwapchainNextFrame = true;
        }
      }

      ImGui::EndTable();
      ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Internal Color Space", StringifyRendererColorSpace(shadingUniforms.shadingInternalColorSpace), ImGuiComboFlags_HeightLarge))
    {
      if (ImGui::Selectable("sRGB_LINEAR", shadingUniforms.shadingInternalColorSpace == COLOR_SPACE_sRGB_LINEAR))
      {
        shadingUniforms.shadingInternalColorSpace = COLOR_SPACE_sRGB_LINEAR;
      }
      if (ImGui::Selectable("BT2020_LINEAR", shadingUniforms.shadingInternalColorSpace == COLOR_SPACE_BT2020_LINEAR))
      {
        shadingUniforms.shadingInternalColorSpace = COLOR_SPACE_BT2020_LINEAR;
      }
      ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Output Color Space", StringifyRendererColorSpace(tonemapUniforms.tonemapOutputColorSpace), ImGuiComboFlags_HeightLarge))
    {
      if (ImGui::Selectable("sRGB_NONLINEAR", tonemapUniforms.tonemapOutputColorSpace == COLOR_SPACE_sRGB_NONLINEAR))
      {
        tonemapUniforms.tonemapOutputColorSpace = COLOR_SPACE_sRGB_NONLINEAR;
      }
      if (ImGui::Selectable("scRGB_LINEAR", tonemapUniforms.tonemapOutputColorSpace == COLOR_SPACE_scRGB_LINEAR))
      {
        tonemapUniforms.tonemapOutputColorSpace = COLOR_SPACE_scRGB_LINEAR;
      }
      if (ImGui::Selectable("BT2020_LINEAR", tonemapUniforms.tonemapOutputColorSpace == COLOR_SPACE_BT2020_LINEAR))
      {
        tonemapUniforms.tonemapOutputColorSpace = COLOR_SPACE_BT2020_LINEAR;
      }
      if (ImGui::Selectable("HDR10_ST2084", tonemapUniforms.tonemapOutputColorSpace == COLOR_SPACE_HDR10_ST2084))
      {
        tonemapUniforms.tonemapOutputColorSpace = COLOR_SPACE_HDR10_ST2084;
      }
      ImGui::EndCombo();
    }

    Gui::BeginProperties(ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV);
    if (Gui::SliderFloat("Peak Display Nits", &maxDisplayNits, 1, 10000, nullptr, nullptr, ImGuiSliderFlags_Logarithmic))
    {
      tonemapUniforms.maxDisplayNits = maxDisplayNits;
    }
    Gui::EndProperties();
    
    ImGui::Checkbox("Display Main Frustum", &debugDisplayMainFrustum);
    ImGui::Checkbox("Generate Hi-Z Buffer", &generateHizBuffer);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
    {
      ImGui::SetTooltip("If unchecked, the hi-z buffer is cleared every frame, essentially forcing this test to pass");
    }
    

    ImGui::SeparatorText("Debug Drawing");
    ImGui::Checkbox("Draw AABBs##draw_debug_aabbs", &drawDebugAabbs);
    ImGui::Checkbox("Draw Rects##draw_debug_rects", &drawDebugRects);

    ImGui::SliderInt("Fake Lag", &fakeLag, 0, 100, "%dms");

    ImGui::SeparatorText("Culling");
    Gui::BeginProperties();
    Gui::FlagCheckbox("Meshlet: Frustum", &globalUniforms.flags, (uint32_t)GlobalFlags::CULL_MESHLET_FRUSTUM);
    Gui::FlagCheckbox("Meshlet: Hi-z", &globalUniforms.flags, (uint32_t)GlobalFlags::CULL_MESHLET_HIZ);
    Gui::FlagCheckbox("Primitive: Back-facing", &globalUniforms.flags, (uint32_t)GlobalFlags::CULL_PRIMITIVE_BACKFACE);
    Gui::FlagCheckbox("Primitive: Frustum", &globalUniforms.flags, (uint32_t)GlobalFlags::CULL_PRIMITIVE_FRUSTUM);
    Gui::FlagCheckbox("Primitive: Small", &globalUniforms.flags, (uint32_t)GlobalFlags::CULL_PRIMITIVE_SMALL);
    Gui::FlagCheckbox("Primitive: VSM", &globalUniforms.flags, (uint32_t)GlobalFlags::CULL_PRIMITIVE_VSM);
    Gui::EndProperties();

    ImGui::SeparatorText("Virtual Shadow Maps");
    ImGui_FlagCheckbox("Show Clipmap ID", &shadingUniforms.debugFlags, VSM_SHOW_CLIPMAP_ID);
    ImGui_FlagCheckbox("Show Page Address", &shadingUniforms.debugFlags, VSM_SHOW_PAGE_ADDRESS);
    ImGui_FlagCheckbox("Show Page Outlines", &shadingUniforms.debugFlags, VSM_SHOW_PAGE_OUTLINES);
    ImGui_FlagCheckbox("Show Shadow Depth", &shadingUniforms.debugFlags, VSM_SHOW_SHADOW_DEPTH);
    ImGui_FlagCheckbox("Show Dirty Pages", &shadingUniforms.debugFlags, VSM_SHOW_DIRTY_PAGES);
    ImGui_FlagCheckbox("Show Overdraw", &shadingUniforms.debugFlags, VSM_SHOW_OVERDRAW);
    ImGui_FlagCheckbox("Blend Normals", &shadingUniforms.debugFlags, BLEND_NORMALS);

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
      vsmSun.UpdateExpensive(commandBuffer, mainCamera.position, -SphericalToCartesian(sunElevation, sunAzimuth), vsmFirstClipmapWidth, vsmDirectionalProjectionZLength);
    }

    if (ImGui::SliderFloat("Projection Z Length", &vsmDirectionalProjectionZLength, 1.0f, 3000.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
    {
      vsmSun.UpdateExpensive(commandBuffer, mainCamera.position, -SphericalToCartesian(sunElevation, sunAzimuth), vsmFirstClipmapWidth, vsmDirectionalProjectionZLength);
    }
    
    ImGui_FlagCheckbox("Disable HPB", &vsmUniforms.debugFlags, (uint32_t)Techniques::VirtualShadowMaps::DebugFlag::VSM_HZB_FORCE_SUCCESS);
    ImGui_HoverTooltip("The HPB (hierarchical page buffer) is used to cull\nmeshlets and primitives that are not touching an active page.");
    ImGui_FlagCheckbox("Disable Page Caching", &vsmUniforms.debugFlags, (uint32_t)Techniques::VirtualShadowMaps::DebugFlag::VSM_FORCE_DIRTY_VISIBLE_PAGES);
    ImGui_HoverTooltip("Page caching reduces the amount of per-frame work\nby only drawing the pages whose visibility changed this frame.");

    auto SliderUint = [](const char* label, uint32_t* v, uint32_t v_min, uint32_t v_max) -> bool
    { return ImGui::SliderScalar(label, ImGuiDataType_U32, v, &v_min, &v_max, "%u"); };

    int shadowMode = shadowUniforms.shadowFilter;
    ImGui::RadioButton("PCSS", &shadowMode, SHADOW_FILTER_PCSS);
    ImGui::SameLine();
    ImGui::RadioButton("SMRT", &shadowMode, SHADOW_FILTER_SMRT);
    ImGui::SameLine();
    ImGui::RadioButton("None", &shadowMode, SHADOW_FILTER_NONE);
    shadowUniforms.shadowFilter = shadowMode;

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
#if 0
  if (ImGui::Begin(" Materials##materials_window"))
  {
    ImGui::BeginTable("materials", 1, ImGuiTableFlags_RowBg);
    ImGui::TableSetupColumn("mat");
    for (size_t i = 0; i < scene.materials.size(); i++)
    {
      ImGui::PushID((int)i);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      bool selected = false;
      if (auto* p = std::get_if<MaterialSelected>(&selectedThingy); p && p->index == i)
      {
        selected = true;
      }

      if (ImGui::Selectable(("Material " + std::to_string(i)).c_str(), selected))
      {
        selectedThingy = MaterialSelected{i};
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::End();
#endif
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

bool TraverseLightNode(FrogRenderer2& renderer, Scene::Node& node)
{
  assert(node.lightId);

  const char* typePreview = "";
  const char* typeIcon = "";
  if (node.light.type == LIGHT_TYPE_DIRECTIONAL)
  {
    typePreview = "Directional";
    typeIcon = ICON_MD_SUNNY "  ";
  }
  else if (node.light.type == LIGHT_TYPE_SPOT)
  {
    typePreview = "Point";
    typeIcon = ICON_FA_LIGHTBULB "  ";
  }
  else if (node.light.type == LIGHT_TYPE_SPOT)
  {
    typePreview = "Spot";
    typeIcon = ICON_FA_FILTER "  ";
  }

  //const auto id = std::string("##") + typePreview + " Light " + std::to_string(i);

  if (ImGui::Button(ICON_FA_TRASH_CAN))
  {
    node.DeleteLight(renderer);
  }

  ImGui::SameLine();

  const bool isOpen = ImGui::TreeNodeEx((std::string(typePreview) + " Light ").c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);

  // Hack to right-align the light icon
  ImGui::SameLine(ImGui::GetWindowWidth() - 40);

  ImGui::PushStyleColor(ImGuiCol_Text, glm::packUnorm4x8(glm::vec4(node.light.color, 1.0f)));
  ImGui::TextUnformatted(typeIcon);
  ImGui::PopStyleColor();

  bool modified = false;
  if (isOpen)
  {
    if (ImGui::BeginCombo("Type", typePreview))
    {
      // if (ImGui::Selectable("Directional", light.type == Utility::LightType::DIRECTIONAL))
      //{
      //   light.type = Utility::LightType::DIRECTIONAL;
      // }
      if (ImGui::Selectable("Point", node.light.type == LIGHT_TYPE_POINT))
      {
        node.light.type = LIGHT_TYPE_POINT;
        modified   = true;
      }
      else if (ImGui::Selectable("Spot", node.light.type == LIGHT_TYPE_SPOT))
      {
        node.light.type = LIGHT_TYPE_SPOT;
        modified   = true;
      }
      ImGui::EndCombo();
    }

    modified |= ImGui::ColorEdit3("Color", &node.light.color[0], ImGuiColorEditFlags_Float);
    modified |= ImGui::DragFloat("Intensity", &node.light.intensity, 1, 0, 1e6f, node.light.type == LIGHT_TYPE_DIRECTIONAL ? "%.0f lx" : "%.0f cd");

    if (ImGui::BeginCombo("Color Space", StringifyRendererColorSpace(node.light.colorSpace), ImGuiComboFlags_HeightLarge))
    {
      if (ImGui::Selectable("sRGB_LINEAR", node.light.colorSpace == COLOR_SPACE_sRGB_LINEAR))
      {
        node.light.colorSpace = COLOR_SPACE_sRGB_LINEAR;
        modified              = true;
      }
      if (ImGui::Selectable("BT2020_LINEAR", node.light.colorSpace == COLOR_SPACE_BT2020_LINEAR))
      {
        node.light.colorSpace = COLOR_SPACE_BT2020_LINEAR;
        modified              = true;
      }
      ImGui::EndCombo();
    }

    if (node.light.type != LIGHT_TYPE_DIRECTIONAL)
    {
      modified |= ImGui::DragFloat("Range", &node.light.range, 0.2f, 0.0f, 100.0f, "%.2f");
    }

    if (node.light.type == LIGHT_TYPE_SPOT)
    {
      modified |= ImGui::SliderFloat("Inner cone angle", &node.light.innerConeAngle, 0, 3.14f, "%.2f rad");
      modified |= ImGui::SliderFloat("Outer cone angle", &node.light.outerConeAngle, 0, 3.14f, "%.2f rad");
    }

    ImGui::TreePop();
  }
  return modified;
}

void FrogRenderer2::GuiDrawSceneGraphHelper(Scene::Node* node)
{
  ImGui::PushID(static_cast<int>(std::hash<const void*>{}(node)));
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  
  auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_AllowOverlap;

  if (node->children.empty() && !node->meshIds.empty())
  {
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet;
  }

  if (auto* p = std::get_if<Scene::Node*>(&selectedThingy); p && *p == node)
  {
    flags |= ImGuiTreeNodeFlags_Selected;
  }

  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{});
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{});
  const bool isTreeNodeOpen = ImGui::TreeNodeEx("", flags);

  // Single-clicking anywhere should select the node
  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
  {
    selectedThingy = node;
  }

  ImGui::SameLine();
  if (!node->meshIds.empty())
  {
    ImGui::Image(ImTextureSampler(guiIcons.at("icon_object").ImageView().GetSampledResourceHandle().index), {16, 16});
  }
  else if (node->lightId)
  {
    if (node->light.type == LIGHT_TYPE_POINT)
    {
      ImGui::Image(ImTextureSampler(guiIcons.at("lamp_point").ImageView().GetSampledResourceHandle().index), {16, 16});
    }
    else if (node->light.type == LIGHT_TYPE_SPOT)
    {
      ImGui::Image(ImTextureSampler(guiIcons.at("lamp_spot").ImageView().GetSampledResourceHandle().index), {16, 16});
    }
  }
  else
  {
    ImGui::Image(ImTextureSampler(guiIcons.at("scene").ImageView().GetSampledResourceHandle().index), {16, 16});
  }
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  ImGui::Text(" %s", node->name.c_str());
  ImGui::PopStyleVar(2);

  if (isTreeNodeOpen)
  {
    for (auto* childNode : node->children)
    {
      GuiDrawSceneGraphHelper(childNode);
    }

#if 0
    if (node->meshId)
    {
      // Show list of meshlet instances on this node.
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      const bool isInstancesNodeOpen =
        Gui::TreeNodeWithImage16("Meshlet instances: ", guiIcons.at("lattice"), {}, ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_AllowOverlap);
      ImGui::SameLine();
      auto meshletInstances = GetMeshletInstances(GetMeshInstance(node->meshId));
      ImGui::Text("%d", (int)meshletInstances.size());
      if (isInstancesNodeOpen)
      {
        Gui::BeginProperties(ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoHostExtendX);
        // Omit instanceId since it isn't populated until scene traversal.
        for (size_t i = 0; i < meshletInstances.size(); i++)
        {
          ImGui::PushID((int)i);
          const auto& meshletInstance = meshletInstances[i];

          bool selected = false;
          if (auto* p = std::get_if<MaterialSelected>(&selectedThingy); p && p->index == meshletInstance.materialId)
          {
            selected = true;
          }

          Gui::BeginSelectableProperty((std::to_string(i) + ":" + " Meshlet " + std::to_string(meshletInstance.meshletId)).c_str());
          if (ImGui::Selectable(("Material " + std::to_string(meshletInstance.materialId)).c_str(), selected))
          {
            selectedThingy = MaterialSelected{meshletInstance.materialId};
          }
          Gui::EndProperty();
          ImGui::PopID();
        }

        Gui::EndProperties();
        ImGui::TreePop();
      }
    }
#endif

    ImGui::TreePop();
  }
  ImGui::PopID();
}

void FrogRenderer2::GuiDrawSceneGraph(VkCommandBuffer)
{
  if (ImGui::Begin("Scene Graph##scene_graph_window"))
  {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{});
    ImGui::BeginTable("scene hierarchy", 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody);
    ImGui::TableSetupColumn("name");

    constexpr auto baseFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_AllowOverlap;

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    const auto as = std::get_if<SunSelected>(&selectedThingy);
    if (ImGui::TreeNodeEx("###sun", baseFlags | (as ? ImGuiTreeNodeFlags_Selected : 0)))
    {
      if (ImGui::IsItemClicked())
      {
        selectedThingy = SunSelected{};
      }
      ImGui::TreePop();
    }
    ImGui::SameLine();
    ImGui::Image(ImTextureSampler(guiIcons.at("icon_sun").ImageView().GetSampledResourceHandle().index), {16, 16});
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(" Sun");

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    const auto cs = std::get_if<CameraSelected>(&selectedThingy);
    if (ImGui::TreeNodeEx("###camera", baseFlags | (cs ? ImGuiTreeNodeFlags_Selected : 0)))
    {
      if (ImGui::IsItemClicked())
      {
        selectedThingy = CameraSelected{};
      }
      ImGui::TreePop();
    }
    ImGui::SameLine();
    ImGui::Image(ImTextureSampler(guiIcons.at("icon_camera").ImageView().GetSampledResourceHandle().index), {16, 16});
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(" Camera");
    ImGui::PopStyleVar(2);

    for (auto* node : scene.rootNodes)
    {
      GuiDrawSceneGraphHelper(node);
    }

    if (!ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered() && ImGui::GetIO().MouseClicked[ImGuiMouseButton_Left])
    {
      selectedThingy = std::monostate{};
    }

    ImGui::EndTable();
  }
  ImGui::End();
}

void FrogRenderer2::GuiDrawComponentEditor(VkCommandBuffer commandBuffer)
{
  if (ImGui::Begin("Component Editor###component_editor_window"))
  {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{});
    ImGui::BeginTable("scene hierarchy", 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody);
    ImGui::PopStyleVar(2);
    ImGui::TableSetupColumn("name");

    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    if (std::get_if<CameraSelected>(&selectedThingy))
    {
      if (Gui::TreeNodeWithImage16("Post Processing", guiIcons.at("rgb")))
      {
        const char* tonemapNames[] = {"AgX", "Tony McMapface", "Linear Clip", "Gran Turismo"};

        ImGui::SeparatorText("Display Mapper Propertes");
        if (ImGui::BeginCombo("###Display Mapper", tonemapNames[tonemapUniforms.tonemapper]))
        {
          if (ImGui::Selectable("AgX", tonemapUniforms.tonemapper == AgX))
          {
            tonemapUniforms.tonemapper = AgX;
          }
          if (ImGui::Selectable("Tony McMapface", tonemapUniforms.tonemapper == TonyMcMapface))
          {
            tonemapUniforms.tonemapper = TonyMcMapface;
          }
          if (ImGui::Selectable("Linear Clip", tonemapUniforms.tonemapper == LinearClip))
          {
            tonemapUniforms.tonemapper = LinearClip;
          }
          if (ImGui::Selectable("Gran Turismo", tonemapUniforms.tonemapper == GTMapper))
          {
            tonemapUniforms.tonemapper = GTMapper;
          }
          ImGui::EndCombo();
        }

        Gui::BeginProperties(ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV);

        if (tonemapUniforms.tonemapper == AgX)
        {
          Gui::SliderFloat("Saturation", &tonemapUniforms.agx.saturation, 0, 2, nullptr, "%.2f", ImGuiSliderFlags_NoRoundToFormat);
          Gui::SliderFloat("Linear Length", &tonemapUniforms.agx.linear, 0, maxDisplayNits, nullptr, "%.2f", ImGuiSliderFlags_NoRoundToFormat);
          Gui::SliderFloat("Compression", &tonemapUniforms.agx.compression, 0, 0.999f, "Amount to compress the gamut by.", "%.2f", ImGuiSliderFlags_NoRoundToFormat);
        }
        if (tonemapUniforms.tonemapper == GTMapper)
        {
          Gui::SliderFloat("Contrast", &tonemapUniforms.gt.contrast, 0, 1);
          Gui::SliderFloat("Linear Start", &tonemapUniforms.gt.startOfLinearSection, 0, 1);
          Gui::SliderFloat("Linear Length", &tonemapUniforms.gt.lengthOfLinearSection, 0, tonemapUniforms.maxDisplayNits);
          Gui::SliderFloat("Toe Curviness", &tonemapUniforms.gt.toeCurviness, 1, 3);
          Gui::SliderFloat("Toe Floor", &tonemapUniforms.gt.toeFloor, 0, 1);
        }
        bool enableDither = tonemapUniforms.enableDithering;
        Gui::Checkbox("1 ULP Dither", &enableDither, "Apply a small dither to color values before quantizing to 8 bits-per-pixel.\n"
                                                     "This mitigates banding artifacts when there are large-scale gradients,\n"
                                                     "while introducing practically imperceptible noise across the image.");
        Gui::EndProperties();

        tonemapUniforms.enableDithering = enableDither;
        if (ImGui::Button("Reset", {-1, 0}))
        {
          tonemapUniforms = TonemapUniforms{};
        }
        ImGui::TreePop();
      }

      ImGui::TableNextRow();
      ImGui::TableNextColumn();

      if (Gui::TreeNodeWithImage16("Auto Exposure", guiIcons.at("histogram")))
      {
        Gui::BeginProperties(ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV);
        Gui::SliderFloat("Min Exposure", &autoExposureLogMinLuminance, -15.0f, autoExposureLogMaxLuminance, nullptr, "%.3f", ImGuiSliderFlags_NoRoundToFormat);
        Gui::SliderFloat("Max Exposure", &autoExposureLogMaxLuminance, autoExposureLogMinLuminance, 15.0f, nullptr, "%.3f", ImGuiSliderFlags_NoRoundToFormat);
        Gui::SliderFloat("Target Luminance", &autoExposureTargetLuminance, 0.001f, 1, nullptr, "%.3f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
        Gui::SliderFloat("Adjustment Rate", &autoExposureAdjustmentSpeed, 0, 5, nullptr, "%.4f", ImGuiSliderFlags_NoRoundToFormat);
        Gui::EndProperties();
        ImGui::TreePop();
      }

      ImGui::TableNextRow();
      ImGui::TableNextColumn();

      // TODO: ICON_MD_CAMERA looks like a house due to a glyph conflict
      if (Gui::TreeNodeWithImage16("Bloom", guiIcons.at("particles")))
      {
        Gui::BeginProperties(ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV);
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
        Gui::Checkbox("Low-Pass Filter", &bloomUseLowPassFilter);

        if (!bloomEnable)
        {
          ImGui::EndDisabled();
        }
        Gui::EndProperties();
        ImGui::TreePop();
      }
    }

    if (std::get_if<SunSelected>(&selectedThingy))
    {
      Gui::BeginProperties(Gui::defaultPropertiesFlags, true, 0.6f);
      auto sunRotated = Gui::SliderFloat("Sun Azimuth", &sunAzimuth, -3.1415f, 3.1415f);
      sunRotated |= Gui::SliderFloat("Sun Elevation", &sunElevation, 0, 3.1415f);
      if (sunRotated)
      {
        vsmSun.UpdateExpensive(commandBuffer, mainCamera.position, -SphericalToCartesian(sunElevation, sunAzimuth), vsmFirstClipmapWidth, vsmDirectionalProjectionZLength);
      }

      Gui::ColorEdit3("Sun Color", &sunColor[0], nullptr, ImGuiColorEditFlags_Float);
      Gui::SliderFloat("Sun Strength", &sunStrength, 0, 500, nullptr, "%.2f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);

      Gui::EndProperties();
    }

    if (auto* p = std::get_if<Scene::Node*>(&selectedThingy))
    {
      auto node = *p;
      
      Gui::BeginProperties(ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit);
      bool modified = Gui::DragFloat3("Position", glm::value_ptr(node->translation), 0.0625f);
      auto euler = glm::eulerAngles(node->rotation);
      if (Gui::DragFloat3("Rotation", glm::value_ptr(euler), 1.0f / 64))
      {
        node->rotation = glm::quat(euler);
        modified = true;
      }
      modified |= Gui::DragFloat3("Scale", glm::value_ptr(node->scale), 1.0f / 64, 1.0f / 32, 10000, "%.3f", ImGuiSliderFlags_NoRoundToFormat);
      Gui::EndProperties();

      if (node->lightId)
      {
        modified |= TraverseLightNode(*this, *node);
      }

      if (modified)
      {
        node->MarkDirty();
      }
    }

#if 0
    if (auto* p = std::get_if<MaterialSelected>(&selectedThingy))
    {
      auto i = p->index;

      auto& gpuMat = scene.materials[i].gpuMaterial;

      Gui::BeginProperties(Gui::defaultPropertiesFlags, true, 0.55f);
      Gui::SliderFloat("Alpha Cutoff", &gpuMat.alphaCutoff, 0, 1);
      Gui::SliderFloat("Metallic Factor", &gpuMat.metallicFactor, 0, 1);
      Gui::SliderFloat("Roughness Factor", &gpuMat.roughnessFactor, 0, 1);
      Gui::ColorEdit4("Base Color Factor", &gpuMat.baseColorFactor[0]);
      Gui::ColorEdit3("Emissive Factor", &gpuMat.emissiveFactor[0]);
      Gui::DragFloat("Emissive Strength", &gpuMat.emissiveStrength, 0.25f, 0, 10000);
      Gui::SliderFloat("Normal Scale", &gpuMat.normalXyScale, 0, 1);
      Gui::EndProperties();
    }
#endif
    
    ImGui::EndTable();
  }
  ImGui::End();
}

void FrogRenderer2::OnGui([[maybe_unused]] double dt, VkCommandBuffer commandBuffer)
{
  ZoneScoped;

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

  ImGui::TextUnformatted(device_->device_.physical_device.properties.deviceName);
  Gui::BeginProperties();

  auto budgets = std::make_unique<VmaBudget[]>(device_->physicalDevice_.memory_properties.memoryHeapCount);

  // Search for the first heap with DEVICE_LOCAL_BIT.
  // This could totally fail and report e.g. a host-visible BAR heap, but that's fine because this is just a silly lil' UI element.
  // Existing systems seem to put the normal device-local heap first anyway.
  uint32_t deviceHeapIndex = 0;
  for (uint32_t i = 0; i < device_->physicalDevice_.memory_properties.memoryHeapCount; i++)
  {
    if (device_->physicalDevice_.memory_properties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
    {
      deviceHeapIndex = i;
      break;
    }
  }
  vmaGetHeapBudgets(device_->allocator_, budgets.get());
  const auto usageMb    = budgets[deviceHeapIndex].usage / 1'000'000;
  const auto budgetMb   = budgets[deviceHeapIndex].budget / 1'000'000;
  Gui::Text("VRAM Consumed", "%llu/%llu MB", "The total is a budget that is affected\nby factors external to this program.", usageMb, budgetMb);
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

    auto viewportImageParams = ImTextureSampler(frame.colorLdrWindowRes->ImageView().GetSampledResourceHandle().index,
      ImTextureSampler::DefaultSamplerIndex,
      tonemapUniforms.tonemapOutputColorSpace);
    ImGui::Image(viewportImageParams, viewportContentSize);

    viewportIsHovered = ImGui::IsItemHovered();

    // FPS viewer
    {
      auto newCursorPos = ImGui::GetWindowContentRegionMin();
      newCursorPos.x += 6;
      ImGui::SetCursorPos(newCursorPos);
      Gui::BeginProperties(ImGuiTableFlags_SizingFixedFit);

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

        Gui::Text("Meshlet Instances", "%u", nullptr, NumMeshletInstances());
        Gui::Text("Lights", "%u", nullptr, NumLights());
#if 0
        Gui::Text("Meshlets", "%llu", nullptr, scene.meshlets.size());
        Gui::Text("Indices", "%llu", nullptr, scene.indices.size());
        Gui::Text("Vertices", "%llu", nullptr, scene.vertices.size());
        Gui::Text("Primitives", "%llu", nullptr, scene.primitives.size());
        Gui::Text("Materials", "%llu", nullptr, scene.materials.size());
#endif
        Gui::Text("Camera Position", "%.2f, %.2f, %.2f", nullptr, mainCamera.position.x, mainCamera.position.y, mainCamera.position.z);
        Gui::Text("Camera Rotation", "%.3f, %.3f", nullptr, mainCamera.pitch, mainCamera.yaw);
      }

      ImGui::PopStyleVar();

      Gui::EndProperties();
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