#pragma once
#include "Application.h"
#include "SceneLoader.h"
#include "PCG.h"
#include "techniques/Bloom.h"
#include "techniques/AutoExposure.h"

#ifdef FROGRENDER_FSR2_ENABLE
  #include "src/ffx-fsr2-api/ffx_fsr2.h"
  //#include "src/ffx-fsr2-api/vk/ffx_fsr2_vk.h"
#endif

#include "Fvog/Texture2.h"
#include "Fvog/Buffer2.h"
#include "Fvog/Pipeline2.h"

#include "shaders/Resources.h.glsl"

// TODO: these structs should come from shared headers rather than copying them
FVOG_DECLARE_ARGUMENTS(VisbufferPushConstants)
{
  // Common
  FVOG_UINT32 globalUniformsIndex;
  FVOG_UINT32 meshletInstancesIndex;
  FVOG_UINT32 meshletDataIndex;
  FVOG_UINT32 meshletPrimitivesIndex;
  FVOG_UINT32 meshletVerticesIndex;
  FVOG_UINT32 meshletIndicesIndex;
  FVOG_UINT32 transformsIndex;
  FVOG_UINT32 indirectDrawIndex;
  FVOG_UINT32 materialsIndex;
  FVOG_UINT32 viewIndex;

  // CullMeshlets.comp
  FVOG_UINT32 hzbIndex;
  FVOG_UINT32 hzbSamplerIndex;
  FVOG_UINT32 cullTrianglesDispatchIndex;

  // CullMeshlets.comp and CullTriangles.comp
  FVOG_UINT32 visibleMeshletsIndex;

  // CullTriangles.comp
  FVOG_UINT32 indexBufferIndex;

  // Visbuffer.frag
  FVOG_UINT32 materialSamplerIndex;
  
  // VisbufferMaterialDepth.frag
  FVOG_UINT32 visbufferIndex;

  // VisbufferResolve.frag
  FVOG_UINT32 baseColorIndex;
  FVOG_UINT32 metallicRoughnessIndex;
  FVOG_UINT32 normalIndex;
  FVOG_UINT32 occlusionIndex;
  FVOG_UINT32 emissionIndex;

  // Debug
  FVOG_UINT32 debugAabbBufferIndex;
  FVOG_UINT32 debugRectBufferIndex;
};

FVOG_DECLARE_ARGUMENTS(HzbCopyPushConstants)
{
  FVOG_UINT32 hzbIndex;
  FVOG_UINT32 depthIndex;
  FVOG_UINT32 depthSamplerIndex;
};

FVOG_DECLARE_ARGUMENTS(HzbReducePushConstants)
{
  FVOG_UINT32 prevHzbIndex;
  FVOG_UINT32 curHzbIndex;
};

FVOG_DECLARE_ARGUMENTS(TonemapArguments)
{
  FVOG_UINT32 sceneColorIndex;
  FVOG_UINT32 noiseIndex;
  FVOG_UINT32 nearestSamplerIndex;

  FVOG_UINT32 exposureIndex;
  FVOG_UINT32 tonemapUniformsIndex;
  FVOG_UINT32 outputImageIndex;
};

FVOG_DECLARE_ARGUMENTS(ShadingPushConstants)
{
  FVOG_UINT32 globalUniformsIndex;
  FVOG_UINT32 shadingUniformsIndex;
  FVOG_UINT32 shadowUniformsIndex;
  FVOG_UINT32 lightBufferIndex;

  FVOG_UINT32 gAlbedoIndex;
  FVOG_UINT32 gNormalAndFaceNormalIndex;
  FVOG_UINT32 gDepthIndex;
  FVOG_UINT32 gSmoothVertexNormalIndex;
  FVOG_UINT32 gEmissionIndex;
  FVOG_UINT32 gMetallicRoughnessAoIndex;
};

FVOG_DECLARE_ARGUMENTS(DebugTextureArguments)
{
  FVOG_UINT32 textureIndex;
  FVOG_UINT32 samplerIndex;
};

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
} // namespace Debug

class FrogRenderer2 final : public Application
{
public:
  FrogRenderer2(const Application::CreateInfo& createInfo);
  ~FrogRenderer2() override;

private:
  struct ViewParams;

  void OnWindowResize(uint32_t newWidth, uint32_t newHeight) override;
  void OnUpdate(double dt) override;
  void OnRender(double dt, VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex) override;
  void OnGui(double dt) override;
  void OnPathDrop(std::span<const char*> paths) override;

  void CullMeshletsForView(VkCommandBuffer commandBuffer, const ViewParams& view, std::string_view name = "Cull Meshlet Pass");
  void MakeStaticSceneBuffers(VkCommandBuffer commandBuffer);

  enum class GlobalFlags : uint32_t
  {
    CULL_MESHLET_FRUSTUM    = 1 << 0,
    CULL_MESHLET_HIZ        = 1 << 1,
    CULL_PRIMITIVE_BACKFACE = 1 << 2,
    CULL_PRIMITIVE_FRUSTUM  = 1 << 3,
    CULL_PRIMITIVE_SMALL    = 1 << 4,
    CULL_PRIMITIVE_VSM      = 1 << 5,
    USE_HASHED_TRANSPARENCY = 1 << 6,
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
    | (uint32_t)GlobalFlags::USE_HASHED_TRANSPARENCY
      ;
    float alphaHashScale = 1.0;
    uint32_t _padding[3];
  };

  enum class ViewType : uint32_t
  {
    MAIN    = 0,
    VIRTUAL = 1,
  };

  struct ViewParams
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
    BLEND_NORMALS          = 1 << 5,
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

  // Indirect command and array of cubes that are generated by the GPU. Fixed size buffer!
  std::optional<Fvog::Buffer> debugGpuAabbsBuffer;

  // Indirect command and array of rects (not quads!) generated by the GPU. Fixed size buffer
  std::optional<Fvog::Buffer> debugGpuRectsBuffer;

  // List of debug lines to be drawn. Cleared every frame.
  std::vector<Debug::Line> debugLines;
  bool debugDisplayMainFrustum = false;
  glm::mat4 debugMainViewProj{1};

    // Resources tied to the output resolution
  struct Frame
  {
    // Main view visbuffer
    std::optional<Fvog::Texture> visbuffer;
    constexpr static Fvog::Format visbufferFormat = Fvog::Format::R32_UINT;
    std::optional<Fvog::Texture> materialDepth;
    constexpr static Fvog::Format materialDepthFormat = Fvog::Format::D32_SFLOAT;
    std::optional<Fvog::Texture> hzb;
    constexpr static Fvog::Format hzbFormat = Fvog::Format::R32_SFLOAT;

    // G-buffer textures
    std::optional<Fvog::Texture> gAlbedo;
    constexpr static Fvog::Format gAlbedoFormat = Fvog::Format::R8G8B8A8_SRGB;
    std::optional<Fvog::Texture> gMetallicRoughnessAo;
    constexpr static Fvog::Format gMetallicRoughnessAoFormat = Fvog::Format::R8G8B8A8_UNORM;
    std::optional<Fvog::Texture> gNormalAndFaceNormal;
    constexpr static Fvog::Format gNormalAndFaceNormalFormat = Fvog::Format::R16G16B16A16_SNORM;
    std::optional<Fvog::Texture> gSmoothVertexNormal;
    constexpr static Fvog::Format gSmoothVertexNormalFormat = Fvog::Format::R16G16_SNORM;
    std::optional<Fvog::Texture> gEmission;
    constexpr static Fvog::Format gEmissionFormat = Fvog::Format::B10G11R11_UFLOAT;
    std::optional<Fvog::Texture> gDepth;
    constexpr static Fvog::Format gDepthFormat = Fvog::Format::D32_SFLOAT;
    std::optional<Fvog::Texture> gMotion;
    constexpr static Fvog::Format gMotionFormat = Fvog::Format::R16G16_SFLOAT;
    std::optional<Fvog::Texture> gReactiveMask; // FSR 2 reactive mask texture
    constexpr static Fvog::Format gReactiveMaskFormat = Fvog::Format::R32_SFLOAT;

    // Previous-frame G-buffer textures used for temporal effects
    std::optional<Fvog::Texture> gNormaAndFaceNormallPrev;
    constexpr static Fvog::Format gNormaAndFaceNormallPrevFormat = gNormalAndFaceNormalFormat;
    std::optional<Fvog::Texture> gDepthPrev;
    constexpr static Fvog::Format gDepthPrevFormat = gDepthFormat;

    // Post-lighting
    std::optional<Fvog::Texture> colorHdrRenderRes;
    constexpr static Fvog::Format colorHdrRenderResFormat = Fvog::Format::B10G11R11_UFLOAT;

    // Post-upscale (skipped if render res == window res)
    std::optional<Fvog::Texture> colorHdrWindowRes;
    constexpr static Fvog::Format colorHdrWindowResFormat = colorHdrRenderResFormat;

    // Bloom scratch buffer. Half window resolution.
    std::optional<Fvog::Texture> colorHdrBloomScratchBuffer;
    constexpr static Fvog::Format colorHdrBloomScratchBufferFormat = Fvog::Format::B10G11R11_UFLOAT;

    // Final tonemapped color
    std::optional<Fvog::Texture> colorLdrWindowRes;
    constexpr static Fvog::Format colorLdrWindowResFormat = Fvog::Format::R8G8B8A8_UNORM;

    //// For debug drawing with ImGui
    //std::optional<Fvog::TextureView> gAlbedoSwizzled;
    //std::optional<Fvog::TextureView> gRoughnessMetallicAoSwizzled;
    //std::optional<Fvog::TextureView> gEmissionSwizzled;
    //std::optional<Fvog::TextureView> gNormalSwizzled;
    //std::optional<Fvog::TextureView> gDepthSwizzled;
  };
  Frame frame{};

  ShadingUniforms shadingUniforms{};
  ShadowUniforms shadowUniforms{};
  GlobalUniforms globalUniforms{};

  Fvog::NDeviceBuffer<GlobalUniforms> globalUniformsBuffer;
  Fvog::NDeviceBuffer<ShadingUniforms> shadingUniformsBuffer;
  Fvog::NDeviceBuffer<ShadowUniforms> shadowUniformsBuffer;

  // Meshlet stuff
  std::optional<Fvog::TypedBuffer<Utility::Vertex>> vertexBuffer;
  std::optional<Fvog::TypedBuffer<uint32_t>> indexBuffer;
  std::optional<Fvog::TypedBuffer<uint8_t>> primitiveBuffer;
  std::optional<Fvog::NDeviceBuffer<Utility::ObjectUniforms>> transformBuffer;
  std::optional<Fvog::NDeviceBuffer<Utility::GpuMaterial>> materialStorageBuffer;
  std::optional<Fvog::TypedBuffer<Utility::Meshlet>> meshletBuffer;
  std::optional<Fvog::NDeviceBuffer<Utility::MeshletInstance>> meshletInstancesBuffer;
  //std::optional<Fvog::NDeviceBuffer<ViewParams>> viewBuffer;
  std::optional<Fvog::TypedBuffer<ViewParams>> viewBuffer;
  // Output
  std::optional<Fvog::TypedBuffer<Fvog::DrawIndexedIndirectCommand>> meshletIndirectCommand;
  std::optional<Fvog::TypedBuffer<uint32_t>> instancedMeshletBuffer;
  std::optional<Fvog::TypedBuffer<Fvog::DispatchIndirectCommand>> cullTrianglesDispatchParams;
  std::optional<Fvog::TypedBuffer<uint32_t>> visibleMeshletIds;

  Fvog::ComputePipeline cullMeshletsPipeline;
  Fvog::ComputePipeline cullTrianglesPipeline;
  Fvog::ComputePipeline hzbCopyPipeline;
  Fvog::ComputePipeline hzbReducePipeline;
  Fvog::GraphicsPipeline visbufferPipeline;
  Fvog::GraphicsPipeline materialDepthPipeline;
  Fvog::GraphicsPipeline visbufferResolvePipeline;
  Fvog::GraphicsPipeline shadingPipeline;
  Fvog::ComputePipeline tonemapPipeline;
  Fvog::GraphicsPipeline debugTexturePipeline;
  //Fvog::GraphicsPipeline debugLinesPipeline;
  //Fvog::GraphicsPipeline debugAabbsPipeline;
  //Fvog::GraphicsPipeline debugRectsPipeline;

  // Scene
  Utility::SceneMeshlet scene;

  // Punctual lights
  std::optional<Fvog::NDeviceBuffer<Utility::GpuLight>> lightBuffer;

  Utility::SceneFlattened sceneFlattened;

  // Post processing
  std::optional<Fvog::Texture> noiseTexture;
  Fvog::NDeviceBuffer<TonemapUniforms> tonemapUniformBuffer;
  TonemapUniforms tonemapUniforms{};

  uint32_t renderWidth{};
  uint32_t renderHeight{};
  uint32_t seed = PCG::Hash(17);

#ifdef FROGRENDER_FSR2_ENABLE
  // FSR 2
  // TODO: temporarily set to false until FSR 2 is integrated
  bool fsr2Enable = false;
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

  //// Bloom
  //Techniques::Bloom bloom;
  //bool bloomEnable = true;
  //uint32_t bloomPasses = 6;
  //float bloomStrength = 1.0f / 32.0f;
  //float bloomWidth = 1.0f;
  //bool bloomUseLowPassFilter = true;

  // Auto-exposure
  Techniques::AutoExposure autoExposure;
  Fvog::TypedBuffer<float> exposureBuffer;
  float autoExposureLogMinLuminance = -15.0f;
  float autoExposureLogMaxLuminance = 15.0f;
  // sRGB middle gray (https://en.wikipedia.org/wiki/Middle_gray)
  float autoExposureTargetLuminance = 0.2140f;
  float autoExposureAdjustmentSpeed = 1.0f;

  // Camera
  float cameraNearPlane = 0.1f;
  float cameraFovyRadians = glm::radians(70.0f);

  //// VSM
  //Techniques::VirtualShadowMaps::Context vsmContext;
  //Techniques::VirtualShadowMaps::DirectionalVirtualShadowMap vsmSun;
  //Fwog::GraphicsPipeline vsmShadowPipeline;
  //Fwog::TypedBuffer<uint32_t> vsmShadowUniformBuffer;
  //Techniques::VirtualShadowMaps::Context::VsmGlobalUniforms vsmUniforms{};
  //float vsmFirstClipmapWidth = 10.0f;
  //float vsmDirectionalProjectionZLength = 100.0f;

  //// Texture viewer
  //struct ViewerUniforms
  //{
  //  int32_t texLayer = 0;
  //  int32_t texLevel = 0;
  //};

  //ViewerUniforms viewerUniforms{};
  //Fwog::TypedBuffer<ViewerUniforms> viewerUniformsBuffer;
  //const Fwog::Texture* viewerCurrentTexture = nullptr;
  //Fwog::GraphicsPipeline viewerVsmPageTablesPipeline;
  //Fwog::GraphicsPipeline viewerVsmPhysicalPagesPipeline;
  //Fwog::GraphicsPipeline viewerVsmBitmaskHzbPipeline;
  //std::optional<Fwog::Texture> viewerOutputTexture;

  Fvog::Sampler nearestSampler;
  Fvog::Sampler linearMipmapSampler;
  Fvog::Sampler hzbSampler;





















  
  //template<typename T>
  //struct ScrollingBuffer
  //{
  //  ScrollingBuffer(size_t capacity = 2000) : capacity(capacity)
  //  {
  //    data = std::make_unique<T[]>(capacity);
  //  }

  //  void Push(const T& v)
  //  {
  //    data[offset] = v;
  //    offset = (offset + 1) % capacity;
  //    if (size < capacity)
  //      size++;
  //  }

  //  void Clear()
  //  {
  //    std::fill_n(data.get(), capacity, T{});
  //  }

  //  size_t capacity;
  //  size_t offset = 0;
  //  size_t size = 0;
  //  std::unique_ptr<T[]> data;
  //};

  //enum class StatGroup
  //{
  //  eMainGpu,
  //  eVsm,

  //  eCount
  //};

  //struct StatGroupInfo
  //{
  //  const char* groupName;
  //  std::vector<const char*> statNames;
  //};

  //const static inline StatGroupInfo statGroups[] = {
  //  {
  //    "Main GPU",
  //   {
  //     "Frame",
  //     "Cull Meshlets Main",
  //     "Render Visbuffer Main",
  //     "Virtual Shadow Maps",
  //     "Build Hi-Z Buffer",
  //     "Make Material Depth Buffer",
  //     "Resolve Visibility Buffer",
  //     "Shade Opaque",
  //     "Debug Geometry",
  //     "Auto Exposure",
  //     "FSR 2",
  //     "Bloom",
  //     "Resolve Image",
  //   }},
  //  {
  //    "Virtual Shadow Maps",
  //   {
  //     "VSM Reset Page Visibility",
  //     "VSM Mark Visible Pages",
  //     "VSM Free Non-Visible Pages",
  //     "VSM Allocate Pages",
  //     "VSM Generate HPB",
  //     "VSM Clear Pages",
  //     "VSM Render Pages",
  //   }},
  //};

  //// static_assert((int)StatGroup::eCount == std::extent_v<decltype(statGroupNames)>);

  //enum MainGpuStat
  //{
  //  eFrame = 0,
  //  eCullMeshletsMain,
  //  eRenderVisbufferMain,
  //  eVsm,
  //  eHzb,
  //  eMakeMaterialDepthBuffer,
  //  eResolveVisbuffer,
  //  eShadeOpaque,
  //  eDebugGeometry,
  //  eAutoExposure,
  //  eFsr2,
  //  eBloom,
  //  eResolveImage,
  //};

  //enum VsmStat
  //{
  //  eVsmResetPageVisibility,
  //  eVsmMarkVisiblePages,
  //  eVsmFreeNonVisiblePages,
  //  eVsmAllocatePages,
  //  eVsmGenerateHpb,
  //  eVsmClearDirtyPages,
  //  eVsmRenderDirtyPages,
  //};

  //// static_assert(eStatCount == std::extent_v<decltype(statNames)>);

  //struct StatInfo
  //{
  //  // std::string name;
  //  ScrollingBuffer<double> timings;
  //  Fwog::TimerQueryAsync timer{5};

  //  void Measure()
  //  {
  //    if (auto t = timer.PopTimestamp())
  //    {
  //      timings.Push(*t / 10e5); // ns to ms
  //    }
  //    else
  //    {
  //      timings.Push(0);
  //    }
  //  }

  //  [[nodiscard]] auto MakeScopedTimer()
  //  {
  //    return Fwog::TimerScoped(timer);
  //  }
  //};

  //std::vector<std::vector<StatInfo>> stats;
  //ScrollingBuffer<double> accumTimes;
  //double accumTime = 0;

  bool showGui = false;
};