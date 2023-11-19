#pragma once
#include "Application.h"
#include "SceneLoader.h"
#include "PCG.h"
#include "techniques/Bloom.h"
#include "techniques/AutoExposure.h"
#include "techniques/VirtualShadowMaps.h"

#include <Fwog/Texture.h>
#include <Fwog/Timer.h>

#ifdef FROGRENDER_FSR2_ENABLE
  #include "src/ffx-fsr2-api/ffx_fsr2.h"
  #include "src/ffx-fsr2-api/gl/ffx_fsr2_gl.h"
#endif

#include "imgui.h"

#include <cmath>

#include <algorithm>
#include <string>
#include <type_traits>
#include <vector>

inline glm::vec3 PolarToCartesian(float elevation, float azimuth)
{
  return {std::sin(elevation) * std::cos(azimuth), std::cos(elevation), std::sin(elevation) * std::sin(azimuth)};
}

namespace Debug
{
  struct Line
  {
    glm::vec3 aPosition;
    glm::vec4 aColor;
    glm::vec3 bPosition;
    glm::vec4 bColor;
  };

  struct Aabb
  {
    glm::vec3 center;
    glm::vec3 halfExtent;
    glm::vec4 color;
  };

  struct Rect
  {
    glm::vec2 minOffset;
    glm::vec2 maxOffset;
    glm::vec4 color;
    float depth;
  };
}

class FrogRenderer final : public Application
{
public:
  FrogRenderer(const Application::CreateInfo& createInfo, std::optional<std::string_view> filename, float scale, bool binary);

private:
  struct ObjectUniforms
  {
    glm::mat4 model;
  };

  enum class GlobalFlags : uint32_t
  {
    CULL_MESHLET_FRUSTUM    = 1 << 0,
    CULL_MESHLET_HIZ        = 1 << 1,
    CULL_PRIMITIVE_BACKFACE = 1 << 2,
    CULL_PRIMITIVE_FRUSTUM  = 1 << 3,
    CULL_PRIMITIVE_SMALL    = 1 << 4,
    CULL_PRIMITIVE_VSM      = 1 << 5,
  };

  struct GlobalUniforms
  {
    glm::mat4 viewProj;
    glm::mat4 oldViewProjUnjittered;
    glm::mat4 viewProjUnjittered;
    glm::mat4 invViewProj;
    glm::mat4 proj;
    glm::mat4 invProj;
    glm::vec4 cameraPos;
    uint32_t meshletCount;
    uint32_t maxIndices;
    float bindlessSamplerLodBias;
    uint32_t flags = 
      (uint32_t)GlobalFlags::CULL_MESHLET_FRUSTUM |
      (uint32_t)GlobalFlags::CULL_MESHLET_HIZ /*|
      (uint32_t)GlobalFlags::CULL_PRIMITIVE_BACKFACE |
      (uint32_t)GlobalFlags::CULL_PRIMITIVE_FRUSTUM |
      (uint32_t)GlobalFlags::CULL_PRIMITIVE_SMALL |
      (uint32_t)GlobalFlags::CULL_PRIMITIVE_VSM*/
      ;
  };

  enum class ViewType : uint32_t
  {
    MAIN    = 0,
    VIRTUAL = 1,
  };

  struct View
  {
    glm::mat4 oldProj;
    glm::mat4 oldView;
    glm::mat4 oldViewProj;
    glm::mat4 proj;
    glm::mat4 view;
    glm::mat4 viewProj;
    glm::mat4 viewProjStableForVsmOnly;
    glm::vec4 cameraPos;
    glm::vec4 frustumPlanes[6];
    glm::vec4 viewport;
    ViewType type = ViewType::MAIN;
    glm::uint virtualTableIndex;
    glm::uvec2 _padding;
  };

  enum class ShadingDebugFlag : uint32_t
  {
    VSM_SHOW_CLIPMAP_ID    = 1 << 0,
    VSM_SHOW_PAGE_ADDRESS  = 1 << 1,
    VSM_SHOW_PAGE_OUTLINES = 1 << 2,
    VSM_SHOW_SHADOW_DEPTH  = 1 << 3,
    VSM_SHOW_DIRTY_PAGES   = 1 << 4,
  };
  //FWOG_DECLARE_FLAG_TYPE(ShadingDebugFlags, ShadingDebugFlag, uint32_t)

  struct ShadingUniforms
  {
    glm::vec4 sunDir;
    glm::vec4 sunStrength;
    glm::vec2 random;
    glm::uint numberOfLights;
    uint32_t debugFlags{};
  };

  struct ShadowUniforms
  {
    uint32_t shadowMode = 0; // 0 = PCSS, 1 = SMRT

    // PCSS stuff
    uint32_t pcfSamples = 16;
    float lightWidth = 0.002f;
    float maxPcfRadius = 0.032f;
    uint32_t blockerSearchSamples = 16;
    float blockerSearchRadius = 0.032f;

    // SMRT stuff
    uint32_t shadowRays = 7;
    uint32_t stepsPerRay = 7;
    float rayStepSize = 0.1f;
    float heightmapThickness = 0.5f;
    float sourceAngleRad = 0.05f;
  };

  struct TonemapUniforms
  {
    float saturation = 1.0f;
    float agxDsLinearSection = 0.18f;
    float peak = 1.0f;
    float compression = 0.15f;
    uint32_t enableDithering = true;
  };

  void OnWindowResize(uint32_t newWidth, uint32_t newHeight) override;
  void OnUpdate(double dt) override;
  void OnRender(double dt) override;
  void OnGui(double dt) override;

  void InitGui();
  void GuiDrawMagnifier(glm::vec2 viewportContentOffset, glm::vec2 viewportContentSize, bool viewportIsHovered);
  void GuiDrawDockspace();
  void GuiDrawFsrWindow();
  void GuiDrawDebugWindow();
  void GuiDrawBloomWindow();
  void GuiDrawAutoExposureWindow();
  void GuiDrawCameraWindow();
  void GuiDrawShadowWindow();
  void GuiDrawViewer();
  void GuiDrawMaterialsArray();
  void GuiDrawPerfWindow();
  void GuiDrawSceneGraph();
  void GuiDrawSceneGraphHelper(Utility::Node* node);

  void CullMeshletsForView(const View& view, std::string_view name = "Cull Meshlet Pass");

  double rsmPerformance = 0;
  double fsr2Performance = 0;

  // scene parameters
  float sunElevation = 3.0f;
  float sunAzimuth = 0.3f;
  float sunStrength = 50;
  glm::vec3 sunColor = {1, 1, 1};

  float aspectRatio = 1;

  // True: output size will be equal to GUI viewport resolution
  // False: output size will be equal to window resolution
  bool useGuiViewportSizeForRendering = true;

  // Debugging stuff
  bool updateCullingFrustum = true;
  bool generateHizBuffer = true;
  bool executeMeshletGeneration = true;
  bool drawDebugAabbs = false;
  bool clearDebugAabbsEachFrame = true;
  bool drawDebugRects = false;
  bool clearDebugRectsEachFrame = true;
  int fakeLag = 0;
  bool debugRenderToSwapchain = false;

  // Indirect command and array of cubes that are generated by the GPU. Fixed size buffer!
  std::optional<Fwog::Buffer> debugGpuAabbsBuffer;

  // Indirect command and array of rects (not quads!) generated by the GPU. Fixed size buffer
  std::optional<Fwog::Buffer> debugGpuRectsBuffer;

  // List of debug lines to be drawn. Cleared every frame.
  std::vector<Debug::Line> debugLines;
  bool debugDisplayMainFrustum = false;
  glm::mat4 debugMainViewProj{1};

  // Resources tied to the output resolution
  struct Frame
  {
    // Main view visbuffer
    std::optional<Fwog::Texture> visbuffer;
    std::optional<Fwog::Texture> materialDepth;
    std::optional<Fwog::Texture> hzb;

    // G-buffer textures
    std::optional<Fwog::Texture> gAlbedo;
    std::optional<Fwog::Texture> gMetallicRoughnessAo;
    std::optional<Fwog::Texture> gNormalAndFaceNormal;
    std::optional<Fwog::Texture> gFaceNormal;
    std::optional<Fwog::Texture> gEmission;
    std::optional<Fwog::Texture> gMotion;
    std::optional<Fwog::Texture> gDepth;
    std::optional<Fwog::Texture> gReactiveMask; // FSR 2 reactive mask texture

    // Previous-frame G-buffer textures used for temporal effects
    std::optional<Fwog::Texture> gNormalPrev;
    std::optional<Fwog::Texture> gDepthPrev;

    // Post-lighting
    std::optional<Fwog::Texture> colorHdrRenderRes;

    // Post-upscale (skipped if render res == window res)
    std::optional<Fwog::Texture> colorHdrWindowRes;

    // Bloom scratch buffer. Half window resolution.
    std::optional<Fwog::Texture> colorHdrBloomScratchBuffer;

    // Final tonemapped color
    std::optional<Fwog::Texture> colorLdrWindowRes;

    // For debug drawing with ImGui
    std::optional<Fwog::TextureView> gAlbedoSwizzled;
    std::optional<Fwog::TextureView> gRoughnessMetallicAoSwizzled;
    std::optional<Fwog::TextureView> gEmissionSwizzled;
    std::optional<Fwog::TextureView> gNormalSwizzled;
    std::optional<Fwog::TextureView> gDepthSwizzled;
  };
  Frame frame{};

  ShadingUniforms shadingUniforms{};
  ShadowUniforms shadowUniforms{};
  GlobalUniforms globalUniforms{};

  Fwog::TypedBuffer<GlobalUniforms> globalUniformsBuffer;
  Fwog::TypedBuffer<ShadingUniforms> shadingUniformsBuffer;
  Fwog::TypedBuffer<ShadowUniforms> shadowUniformsBuffer;

  // Meshlet stuff
  std::optional<Fwog::TypedBuffer<Utility::Vertex>> vertexBuffer;
  std::optional<Fwog::TypedBuffer<uint32_t>> indexBuffer;
  std::optional<Fwog::TypedBuffer<uint8_t>> primitiveBuffer;
  std::optional<Fwog::TypedBuffer<glm::mat4>> transformBuffer;
  std::optional<Fwog::TypedBuffer<Utility::GpuMaterial>> materialStorageBuffer;
  std::optional<Fwog::TypedBuffer<View>> viewBuffer;
  // Output
  std::optional<Fwog::TypedBuffer<Fwog::DrawIndexedIndirectCommand>> meshletIndirectCommand;
  std::optional<Fwog::TypedBuffer<uint32_t>> instancedMeshletBuffer;
  std::optional<Fwog::TypedBuffer<Fwog::DispatchIndirectCommand>> cullTrianglesDispatchParams;
  std::optional<Fwog::TypedBuffer<uint32_t>> visibleMeshletIds;

  Fwog::ComputePipeline cullMeshletsPipeline;
  Fwog::ComputePipeline cullTrianglesPipeline;
  Fwog::ComputePipeline hzbCopyPipeline;
  Fwog::ComputePipeline hzbReducePipeline;
  Fwog::GraphicsPipeline visbufferPipeline;
  Fwog::GraphicsPipeline shadowMainPipeline;
  Fwog::GraphicsPipeline materialDepthPipeline;
  Fwog::GraphicsPipeline visbufferResolvePipeline;
  Fwog::GraphicsPipeline shadingPipeline;
  Fwog::ComputePipeline tonemapPipeline;
  Fwog::GraphicsPipeline debugTexturePipeline;
  Fwog::GraphicsPipeline debugLinesPipeline;
  Fwog::GraphicsPipeline debugAabbsPipeline;
  Fwog::GraphicsPipeline debugRectsPipeline;

  // Scene
  Utility::SceneMeshlet scene;

  // Punctual lights
  std::optional<Fwog::TypedBuffer<Utility::GpuLight>> lightBuffer;
  std::optional<Fwog::TypedBuffer<Utility::Meshlet>> meshletBuffer; 
  std::optional<Fwog::TypedBuffer<ObjectUniforms>> meshUniformBuffer;

  Utility::SceneFlattened sceneFlattened;

  // Post processing
  std::optional<Fwog::Texture> noiseTexture;
  Fwog::TypedBuffer<TonemapUniforms> tonemapUniformBuffer;
  TonemapUniforms tonemapUniforms{};

  uint32_t renderWidth{};
  uint32_t renderHeight{};
  uint32_t frameIndex = 0;
  uint32_t seed = PCG::Hash(17);

#ifdef FROGRENDER_FSR2_ENABLE
  // FSR 2
  bool fsr2Enable = true;
  bool fsr2FirstInit = true;
  float fsr2Sharpness = 0;
  float fsr2Ratio = 1.5f; // FFX_FSR2_QUALITY_MODE_QUALITY
  FfxFsr2Context fsr2Context{};
  std::unique_ptr<char[]> fsr2ScratchMemory;
#else
  const bool fsr2Enable = false;
#endif

  // Magnifier
  float magnifierZoom = 4;
  glm::vec2 magnifierLastCursorPos = {400, 400};

  // Bloom
  Techniques::Bloom bloom;
  bool bloomEnable = true;
  uint32_t bloomPasses = 6;
  float bloomStrength = 1.0f / 32.0f;
  float bloomWidth = 1.0f;
  bool bloomUseLowPassFilter = true;

  // Auto-exposure
  Techniques::AutoExposure autoExposure;
  Fwog::TypedBuffer<float> exposureBuffer;
  float autoExposureLogMinLuminance = -15.0f;
  float autoExposureLogMaxLuminance = 15.0f;
  // sRGB middle gray (https://en.wikipedia.org/wiki/Middle_gray)
  float autoExposureTargetLuminance = 0.2140f;
  float autoExposureAdjustmentSpeed = 1.0f;

  // Camera
  float cameraNearPlane = 0.1f;
  float cameraFovyRadians = glm::radians(70.0f);

  // VSM
  Techniques::VirtualShadowMaps::Context vsmContext;
  Techniques::VirtualShadowMaps::DirectionalVirtualShadowMap vsmSun;
  Fwog::GraphicsPipeline vsmShadowPipeline;
  Fwog::TypedBuffer<uint32_t> vsmShadowUniformBuffer;
  Techniques::VirtualShadowMaps::Context::VsmGlobalUniforms vsmUniforms{};
  float vsmFirstClipmapWidth = 10.0f;
  float vsmDirectionalProjectionZLength = 100.0f;

  // Texture viewer
  struct ViewerUniforms
  {
    int32_t texLayer = 0;
    int32_t texLevel = 0;
  };

  ViewerUniforms viewerUniforms{};
  Fwog::TypedBuffer<ViewerUniforms> viewerUniformsBuffer;
  const Fwog::Texture* viewerCurrentTexture = nullptr;
  Fwog::GraphicsPipeline viewerVsmPageTablesPipeline;
  Fwog::GraphicsPipeline viewerVsmPhysicalPagesPipeline;
  Fwog::GraphicsPipeline viewerVsmBitmaskHzbPipeline;
  std::optional<Fwog::Texture> viewerOutputTexture;

  template<typename T>
  struct ScrollingBuffer
  {
    ScrollingBuffer(size_t capacity = 2000) : capacity(capacity)
    {
      data = std::make_unique<T[]>(capacity);
    }

    void Push(const T& v)
    {
      data[offset] = v;
      offset = (offset + 1) % capacity;
      if (size < capacity)
        size++;
    }

    void Clear()
    {
      std::fill_n(data.get(), capacity, T{});
    }

    size_t capacity;
    size_t offset = 0;
    size_t size = 0;
    std::unique_ptr<T[]> data;
  };

  enum class StatGroup
  {
    eMainGpu,
    eVsm,

    eCount
  };

  struct StatGroupInfo
  {
    const char* groupName;
    std::vector<const char*> statNames;
  };

  const static inline StatGroupInfo statGroups[] =
  {
    {
      "Main GPU",
      {
        "Frame",
        "Cull Meshlets Main",
        "Render Visbuffer Main",
        "Virtual Shadow Maps",
        "Build Hi-Z Buffer",
        "Make Material Depth Buffer",
        "Resolve Visibility Buffer",
        "Shade Opaque",
        "Debug Geometry",
        "Auto Exposure",
        "FSR 2",
        "Bloom",
        "Resolve Image",
      }
    },
    {
      "Virtual Shadow Maps",
      {
        "VSM Reset Page Visibility",
        "VSM Mark Visible Pages",
        "VSM Free Non-Visible Pages",
        "VSM Allocate Pages",
        "VSM Generate HPB",
        "VSM Clear Pages",
        "VSM Render Pages",
      }
    },
  };

  //static_assert((int)StatGroup::eCount == std::extent_v<decltype(statGroupNames)>);

  enum MainGpuStat
  {
    eFrame = 0,
    eCullMeshletsMain,
    eRenderVisbufferMain,
    eVsm,
    eHzb,
    eMakeMaterialDepthBuffer,
    eResolveVisbuffer,
    eShadeOpaque,
    eDebugGeometry,
    eAutoExposure,
    eFsr2,
    eBloom,
    eResolveImage,
  };

  enum VsmStat
  {
    eVsmResetPageVisibility,
    eVsmMarkVisiblePages,
    eVsmFreeNonVisiblePages,
    eVsmAllocatePages,
    eVsmGenerateHpb,
    eVsmClearDirtyPages,
    eVsmRenderDirtyPages,
  };

  //static_assert(eStatCount == std::extent_v<decltype(statNames)>);

  struct StatInfo
  {
    // std::string name;
    ScrollingBuffer<double> timings;
    Fwog::TimerQueryAsync timer{5};

    void Measure()
    {
      if (auto t = timer.PopTimestamp())
      {
        timings.Push(*t / 10e5); // ns to ms
      }
      else
      {
        timings.Push(0);
      }
    }

    [[nodiscard]] auto MakeScopedTimer()
    {
      return Fwog::TimerScoped(timer);
    }
  };

  std::vector<std::vector<StatInfo>> stats;
  ScrollingBuffer<double> accumTimes;
  double accumTime = 0;
};

#define CONCAT_HELPER(x, y) x##y
#define CONCAT(x, y) CONCAT_HELPER(x, y)
#define TIME_SCOPE_GPU(statGroup, statEnum) \
  stats[(int)statGroup][statEnum].Measure();     \
  const auto CONCAT(gpu_timer_, __LINE__) = stats[(int)statGroup][statEnum].MakeScopedTimer()
