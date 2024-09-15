#pragma once
#include "Application.h"
#include "Renderables.h"
#include "Scene.h"
#include "PCG.h"
#include "techniques/Bloom.h"
#include "techniques/AutoExposure.h"
#include "techniques/VirtualShadowMaps.h"
#include "debug/ForwardRenderer.h"
#ifdef FROGRENDER_RAYTRACING_ENABLE
#include "techniques/ao/RayTracedAO.h"
#endif

#ifdef FROGRENDER_FSR2_ENABLE
  #include "src/ffx-fsr2-api/ffx_fsr2.h"
  #include "src/ffx-fsr2-api/vk/ffx_fsr2_vk.h"
#endif

#include "Fvog/Texture2.h"
#include "Fvog/Buffer2.h"
#include "Fvog/Pipeline2.h"
#include "Fvog/Timer2.h"
#if defined(FROGRENDER_RAYTRACING_ENABLE)
  #include "Fvog/AccelerationStructure.h"
#endif

#include "shaders/Resources.h.glsl"
#include "shaders/ShadeDeferredPbr.h.glsl"
#include "shaders/post/TonemapAndDither.shared.h"

#include <variant>
#include <vector>
#include <span>
#include <memory>
#include <memory_resource>

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

FVOG_DECLARE_ARGUMENTS(DebugTextureArguments)
{
  FVOG_UINT32 textureIndex;
  FVOG_UINT32 samplerIndex;
};

FVOG_DECLARE_ARGUMENTS(DebugAabbArguments)
{
  FVOG_UINT32 globalUniformsIndex;
  FVOG_UINT32 debugAabbBufferIndex;
};

FVOG_DECLARE_ARGUMENTS(DebugLinesPushConstants)
{
  FVOG_UINT32 vertexBufferIndex;
  FVOG_UINT32 globalUniformsIndex;
};

FVOG_DECLARE_ARGUMENTS(DebugRectArguments)
{
  FVOG_UINT32 debugRectBufferIndex;
};

namespace Scene
{
  struct Node;
}

inline glm::vec3 SphericalToCartesian(float elevation, float azimuth, float radius = 1.0f)
{
  return {
    radius * std::sin(elevation) * std::cos(azimuth),
    radius * std::cos(elevation),
    radius * std::sin(elevation) * std::sin(azimuth)
  };
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

  struct MeshGeometryInfo
  {
    std::pmr::vector<Render::Meshlet> meshlets;
    std::pmr::vector<Render::Vertex> vertices;
    std::pmr::vector<Render::index_t> remappedIndices;
    std::pmr::vector<Render::primitive_t> primitives;
    std::pmr::vector<Render::index_t> originalIndices;
  };

  // Life and death
  [[nodiscard]] Render::MeshGeometryID RegisterMeshGeometry(MeshGeometryInfo meshGeometry);
  void UnregisterMeshGeometry(Render::MeshGeometryID meshGeometry);

  [[nodiscard]] Render::MeshID SpawnMesh(Render::MeshGeometryID meshGeometry);
  void DeleteMesh(Render::MeshID mesh);

  [[nodiscard]] Render::LightID SpawnLight(const GpuLight& lightData);
  void DeleteLight(Render::LightID light);

  [[nodiscard]] Render::MaterialID RegisterMaterial(Render::Material&& material);
  void UnregisterMaterial(Render::MaterialID material);

  // Updating
  void UpdateMesh(Render::MeshID mesh, const Render::ObjectUniforms& uniforms);
  void UpdateLight(Render::LightID light, const GpuLight& lightData);
  void UpdateMaterial(Render::MaterialID material, const Render::GpuMaterial& materialData);

  // Querying
  uint32_t GetMaterialGpuIndex(Render::MaterialID material);
  Render::Material& GetMaterial(Render::MaterialID material);

  // Hacky functions, need better interface for this
  VkDeviceAddress GetVertexBufferPointerFromMesh(Render::MeshID meshId);
  VkDeviceAddress GetOriginalIndexBufferPointerFromMesh(Render::MeshID meshId);

private:
  struct ViewParams;

  void OnFramebufferResize(uint32_t newWidth, uint32_t newHeight) override;
  void OnUpdate(double dt) override;
  void OnRender(double dt, VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex) override;
  void OnGui(double dt, VkCommandBuffer commandBuffer) override;
  void OnPathDrop(std::span<const char*> paths) override;

  void InitGui();
  void GuiDrawMagnifier(glm::vec2 viewportContentOffset, glm::vec2 viewportContentSize, bool viewportIsHovered);
  void GuiDrawDockspace(VkCommandBuffer commandBuffer);
  void GuiDrawFsrWindow(VkCommandBuffer commandBuffer);
  void GuiDrawDebugWindow(VkCommandBuffer commandBuffer);
  void GuiDrawShadowWindow(VkCommandBuffer commandBuffer);
  void GuiDrawViewer(VkCommandBuffer commandBuffer);
  void GuiDrawMaterialsArray(VkCommandBuffer commandBuffer);
  void GuiDrawPerfWindow(VkCommandBuffer commandBuffer);
  void GuiDrawSceneGraph(VkCommandBuffer commandBuffer);
  void GuiDrawSceneGraphHelper(Scene::Node* node);
  void GuiDrawComponentEditor(VkCommandBuffer commandBuffer);
  void GuiDrawHdrWindow(VkCommandBuffer commandBuffer);
  void GuiDrawGeometryInspector(VkCommandBuffer commandBuffer);
  void GuiDrawAoWindow(VkCommandBuffer commandBuffer);
  void GuiDrawGlobalIlluminationWindow(VkCommandBuffer commandBuffer);

  void CullMeshletsForView(VkCommandBuffer commandBuffer, const ViewParams& view, Fvog::Buffer& visibleMeshletIds, std::string_view name = "Cull Meshlet Pass");

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

  // scene parameters
  float sunElevation = 3.0f;
  float sunAzimuth = 0.3f;
  float sunIlluminance = 110'000; // Lux
  glm::vec3 sunColor = {1, 1, 1};

  float aspectRatio = 1;

  // True: output size will be equal to GUI viewport resolution
  // False: output size will be equal to window resolution
  bool useGuiViewportSizeForRendering = true;

  // Debugging stuff
  bool generateHizBuffer = true;
  bool drawDebugAabbs = false;
  bool drawDebugRects = false;
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
    constexpr static Fvog::Format gReactiveMaskFormat = Fvog::Format::R8_UNORM;

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
    constexpr static Fvog::Format colorLdrWindowResFormat = Fvog::Format::R16G16B16A16_SFLOAT;

    // For debug drawing with ImGui
    std::optional<Fvog::TextureView> gAlbedoSwizzled;
    std::optional<Fvog::TextureView> gRoughnessMetallicAoSwizzled;
    std::optional<Fvog::TextureView> gEmissionSwizzled;
    std::optional<Fvog::TextureView> gNormalSwizzled;
    std::optional<Fvog::TextureView> gDepthSwizzled;

    std::optional<Fvog::Texture> forwardRenderTarget; // SDR
    std::optional<Fvog::TextureView> forwardRenderTargetSwizzled;
  };
  Frame frame{};

  ShadingUniforms shadingUniforms{};
  ShadowUniforms shadowUniforms{};
  GlobalUniforms globalUniforms{};

  Fvog::NDeviceBuffer<GlobalUniforms> globalUniformsBuffer;
  Fvog::NDeviceBuffer<ShadingUniforms> shadingUniformsBuffer;
  Fvog::NDeviceBuffer<ShadowUniforms> shadowUniformsBuffer;

  struct MeshGeometryAllocs
  {
    Fvog::ManagedBuffer::Alloc meshletsAlloc;
    Fvog::ManagedBuffer::Alloc verticesAlloc;
    Fvog::ManagedBuffer::Alloc indicesAlloc;
    Fvog::ManagedBuffer::Alloc primitivesAlloc;
    Fvog::ManagedBuffer::Alloc originalIndicesAlloc;
#ifdef FROGRENDER_RAYTRACING_ENABLE
    Fvog::Blas blas;
#endif
  };

  size_t totalMeshlets = 0;
  size_t totalVertices = 0;
  size_t totalRemappedIndices = 0;
  size_t totalOriginalIndices = 0;
  size_t totalPrimitives = 0;
  size_t totalBlasMemory = 0;

  struct MeshAllocs
  {
    std::optional<Render::MeshGeometryID> geometryId;
    std::optional<Fvog::ContiguousManagedBuffer::Alloc> meshletInstancesAlloc;
    std::optional<Fvog::ManagedBuffer::Alloc> instanceAlloc;
#ifdef FROGRENDER_RAYTRACING_ENABLE
    std::optional<Fvog::TlasInstance> tlasInstance;
#endif
  };

  struct LightAlloc
  {
    Fvog::ContiguousManagedBuffer::Alloc lightAlloc;
  };

  struct MaterialAlloc
  {
    Fvog::ManagedBuffer::Alloc materialAlloc;
    Render::Material material;
  };

  // Big buffer that holds scene data, materials, transforms, etc. for the GPU
  Fvog::ManagedBuffer geometryBuffer;
  Fvog::ContiguousManagedBuffer meshletInstancesBuffer;
  Fvog::ContiguousManagedBuffer lightsBuffer;
#ifdef FROGRENDER_RAYTRACING_ENABLE
  std::optional<Fvog::Tlas> tlas;
#endif

  uint32_t NumMeshletInstances() const noexcept
  {
    return (uint32_t)meshletInstancesBuffer.GetCurrentSize() / sizeof(Render::MeshletInstance);
  }

  uint32_t NumLights() const noexcept
  {
    return (uint32_t)lightsBuffer.GetCurrentSize() / sizeof(GpuLight);
  }

  uint64_t nextId = 1; // 0 is reserved for "null" IDs
  std::unordered_map<uint64_t, MeshGeometryAllocs> meshGeometryAllocations;

  std::unordered_map<uint64_t, MeshAllocs> meshAllocations;
  std::unordered_map<uint64_t, LightAlloc> lightAllocations;
  std::unordered_map<uint64_t, MaterialAlloc> materialAllocations;

  // Will be batch uploaded
  struct SpawnedMesh
  {
    Render::MeshID id;
    Render::MeshGeometryID meshGeometryId;
  };

  std::unordered_map<uint64_t, Render::ObjectUniforms> modifiedMeshUniforms;
  std::unordered_map<uint64_t, GpuLight> modifiedLights;
  std::unordered_map<uint64_t, Render::GpuMaterial> modifiedMaterials;
  std::vector<SpawnedMesh> spawnedMeshes;
  std::vector<uint64_t> deletedMeshes;
  std::vector<std::pair<uint64_t, GpuLight>> spawnedLights;
  std::vector<uint64_t> deletedLights;

  void FlushUpdatedSceneData(VkCommandBuffer commandBuffer);
  
  std::optional<Fvog::TypedBuffer<ViewParams>> viewBuffer;
  // Output
  std::optional<Fvog::TypedBuffer<Fvog::DrawIndexedIndirectCommand>> meshletIndirectCommand;
  std::optional<Fvog::TypedBuffer<uint32_t>> instancedMeshletBuffer;
  std::optional<Fvog::TypedBuffer<Fvog::DispatchIndirectCommand>> cullTrianglesDispatchParams;

  // These buffers serve two purposes:
  // First, they store the IDs of meshlet instances that passed meshlet culling.
  // Second, they allow us to remap the range 0-2^24 to meshlet instances that may
  // have an index outside that range. That can allow us to render scenes with more than 2^24
  // meshlet instances correctly, as long as 2^24 meshlet instances or fewer are visible.
  std::optional<Fvog::TypedBuffer<uint32_t>> persistentVisibleMeshletIds; // For when the data needs to be retrieved later (i.e. it is stored in the visbuffer)
  std::optional<Fvog::TypedBuffer<uint32_t>> transientVisibleMeshletIds;  // For shadows or forward passes

  Fvog::ComputePipeline cullMeshletsPipeline;
  Fvog::ComputePipeline cullTrianglesPipeline;
  Fvog::ComputePipeline hzbCopyPipeline;
  Fvog::ComputePipeline hzbReducePipeline;
  Fvog::GraphicsPipeline visbufferPipeline;
  Fvog::GraphicsPipeline visbufferResolvePipeline;
  Fvog::GraphicsPipeline shadingPipeline;
  Fvog::ComputePipeline tonemapPipeline;
  Fvog::GraphicsPipeline debugTexturePipeline;
  Fvog::GraphicsPipeline debugLinesPipeline;
  Fvog::GraphicsPipeline debugAabbsPipeline;
  Fvog::GraphicsPipeline debugRectsPipeline;

  // TODO: remove
#ifdef FROGRENDER_RAYTRACING_ENABLE
  Fvog::RayTracingPipeline testRayTracingPipeline;
  std::optional<Fvog::Texture> testRayTracingOutput;
#endif

  std::optional<Fvog::NDeviceBuffer<Debug::Line>> lineVertexBuffer;

  // Debug
  bool debugDrawForwardRender_   = false;
  bool showPerfWindow            = true;
  bool showHdrWindow             = true;
  bool showComponentEditorWindow = true;
  bool showSceneGraphWindow      = true;
  bool showTextureViewerWindow   = true;
  bool showShadowWindow          = true;
  bool showDebugWindow           = true;
  bool showFsr2Window            = true;
  bool showMagnifierWindow       = true;
  bool showGeometryInspector     = false;
  bool showMaterialWindow        = true;
  bool showAoWindow              = true;
  bool showGiWindow              = true;
  Debug::ForwardRenderer forwardRenderer_;
  std::unique_ptr<std::byte[]> geometryBufferData_;

  // Scene
  Scene::SceneMeshlet scene;

  enum DisplayMap
  {
    AgX,
    TonyMcMapface,
    LinearClip, // Do nothing
    GTMapper, // For HDR, but works with SDR
  };

  // Post processing
  std::optional<Fvog::Texture> noiseTexture;
  Fvog::NDeviceBuffer<shared::TonemapUniforms> tonemapUniformBuffer;
  shared::TonemapUniforms tonemapUniforms{};
  Fvog::Texture tonyMcMapfaceLut;
  Fvog::Texture calibrateHdrTexture;
  Fvog::ComputePipeline calibrateHdrPipeline;

  uint32_t renderInternalWidth{};
  uint32_t renderInternalHeight{};
  uint32_t renderOutputWidth{};
  uint32_t renderOutputHeight{};
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
  Fvog::TypedBuffer<float> exposureBuffer;
  float autoExposureLogMinLuminance = -15.0f;
  float autoExposureLogMaxLuminance = 15.0f;
  // sRGB middle gray (https://en.wikipedia.org/wiki/Middle_gray)
  float autoExposureTargetLuminance = 0.2140f;
  float autoExposureAdjustmentSpeed = 1.0f;

  // Camera
  float cameraNearPlane = 0.075f;
  float cameraFovyRadians = glm::radians(70.0f);

  // VSM
  Techniques::VirtualShadowMaps::Context vsmContext;
  Techniques::VirtualShadowMaps::DirectionalVirtualShadowMap vsmSun;
  Fvog::GraphicsPipeline vsmShadowPipeline;
  Fvog::TypedBuffer<uint32_t> vsmShadowUniformBuffer;
  std::optional<Fvog::Texture> vsmTempDepthStencil;
  Techniques::VirtualShadowMaps::Context::VsmGlobalUniforms vsmUniforms{};
  float vsmFirstClipmapWidth = 10.0f;
  float vsmDirectionalProjectionZLength = 100.0f;

  // Ambient Occlusion (AO)
  enum class AoMethod
  {
    NONE,
    RAY_TRACED,
  };
  AoMethod aoMethod_ = AoMethod::NONE;
  bool aoUsePerFrameRng = true;
  Fvog::Texture whiteTexture_;
#ifdef FROGRENDER_RAYTRACING_ENABLE
  Techniques::RayTracedAO rayTracedAo_;
  Techniques::RayTracedAO::ComputeParams rayTracedAoParams_;
#endif

  // Texture viewer
  struct ViewerUniforms
  {
    FVOG_UINT32 textureIndex{};
    FVOG_UINT32 samplerIndex{};
    FVOG_INT32 texLayer = 0;
    FVOG_INT32 texLevel = 0;
  };

  ViewerUniforms viewerUniforms{};
  Fvog::Texture* viewerCurrentTexture = nullptr;
  Fvog::GraphicsPipeline viewerVsmPageTablesPipeline;
  Fvog::GraphicsPipeline viewerVsmPhysicalPagesPipeline;
  Fvog::GraphicsPipeline viewerVsmBitmaskHzbPipeline;
  Fvog::GraphicsPipeline viewerVsmPhysicalPagesOverdrawPipeline;
  std::optional<Fvog::Texture> viewerOutputTexture;
  constexpr static Fvog::Format viewerOutputTextureFormat = Fvog::Format::R8G8B8A8_UNORM;

  Fvog::Sampler nearestSampler;
  Fvog::Sampler linearMipmapSampler;
  Fvog::Sampler linearClampSampler;
  Fvog::Sampler hzbSampler;
  



















  
  
  template<typename T>
  struct ScrollingBuffer
  {
    ScrollingBuffer(size_t capacity = 3000) : capacity(capacity)
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

  const static inline StatGroupInfo statGroups[] = {
    {
      "Main GPU",
     {
       "Frame",
       "Cull Meshlets Main",
       "Render Visbuffer Main",
       "Virtual Shadow Maps",
       "Build Hi-Z Buffer",
       "Resolve Visibility Buffer",
       "Shade Opaque",
       "Debug Geometry",
       "Auto Exposure",
       "FSR 2",
       "Bloom",
       "Resolve Image",
     }},
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
     }},
  };

  // static_assert((int)StatGroup::eCount == std::extent_v<decltype(statGroupNames)>);

  enum MainGpuStat
  {
    eFrame = 0,
    eCullMeshletsMain,
    eRenderVisbufferMain,
    eVsm,
    eHzb,
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

  // static_assert(eStatCount == std::extent_v<decltype(statNames)>);

  struct StatInfo
  {
    explicit StatInfo(std::string name)
      : timer(Fvog::Device::frameOverlap, std::move(name))
    {
    }

    void Measure()
    {
      if (auto t = timer.PopTimestamp())
      {
        auto t_ms = *t / 10e5; // ns to ms
        timings.Push(t_ms);
        double weight = movingAverageWeight;
        if (timings.size < 100) // Quicker accumulation at the start
        {
          weight = 1.0 / timings.size;
        }
        movingAverage = movingAverage * (1.0 - weight) + t_ms * weight;
      }
      else
      {
        timings.Push(0);
      }
    }

    [[nodiscard]] auto MakeScopedTimer(VkCommandBuffer commandBuffer)
    {
      return Fvog::TimerScoped(timer, commandBuffer);
    }

    // std::string name;
    ScrollingBuffer<double> timings;
    double movingAverage                        = 0;
    static constexpr double movingAverageWeight = 0.005;
    Fvog::TimerQueryAsync timer;
  };

  std::vector<std::vector<StatInfo>> stats;
  ScrollingBuffer<double> accumTimes;
  double accumTime = 0;

  // GUI
  bool showGui       = true;
  bool showFpsInfo   = true;
  bool showSceneInfo = false;
  struct CameraSelected{};
  struct SunSelected{};
  struct MaterialSelected
  {
    Render::MaterialID material;
  };
  using SelectedThingyType = std::variant<std::monostate, CameraSelected, SunSelected, Scene::Node*, MaterialSelected>;
  SelectedThingyType selectedThingy = std::monostate{};

  std::unordered_map<std::string, Fvog::Texture> guiIcons;
};
