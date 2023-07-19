#pragma once
#include "Application.h"
#include "RsmTechnique.h"
#include "SceneLoader.h"
#include "PCG.h"

#include <Fwog/Texture.h>

#ifdef FROGRENDER_FSR2_ENABLE
  #include "src/ffx-fsr2-api/ffx_fsr2.h"
  #include "src/ffx-fsr2-api/gl/ffx_fsr2_gl.h"
#endif

class FrogRenderer final : public Application
{
public:
  FrogRenderer(const Application::CreateInfo& createInfo, std::optional<std::string_view> filename, float scale, bool binary);

private:
  struct ObjectUniforms
  {
    glm::mat4 model;
  };

  struct GlobalUniforms
  {
    glm::mat4 viewProj;
    glm::mat4 oldViewProjUnjittered;
    glm::mat4 viewProjUnjittered;
    glm::mat4 invViewProj;
    glm::mat4 proj;
    glm::vec4 cameraPos;
    glm::vec4 frustumPlanes[6];
    uint32_t meshletCount;
    float bindlessSamplerLodBias;
    uint32_t _padding[2];
  };

  struct ShadingUniforms
  {
    glm::mat4 sunViewProj;
    glm::vec4 sunDir;
    glm::vec4 sunStrength;
    glm::mat4 sunView;
    glm::mat4 sunProj;
    glm::vec2 random;
  };

  struct ShadowUniforms
  {
    uint32_t shadowMode = 0; // 0 = PCF, 1 = SMRT

    // PCF stuff
    uint32_t pcfSamples = 8;
    float pcfRadius = 0.002f;

    // SMRT stuff
    uint32_t shadowRays = 7;
    uint32_t stepsPerRay = 7;
    float rayStepSize = 0.1f;
    float heightmapThickness = 0.5f;
    float sourceAngleRad = 0.05f;
  };

  struct alignas(16) Light
  {
    glm::vec4 position;
    glm::vec3 intensity;
    float invRadius;
    // uint32_t type; // 0 = point, 1 = spot
  };

  void OnWindowResize(uint32_t newWidth, uint32_t newHeight) override;
  void OnUpdate(double dt) override;
  void OnRender(double dt) override;
  void OnGui(double dt) override;

  void InitGui();
  void GuiDrawMagnifier(glm::vec2 viewportContentOffset, glm::vec2 viewportContentSize);
  void GuiDrawDockspace();
  void GuiDrawFsrWindow();

  // constants
  static constexpr int gShadowmapWidth = 2048;
  static constexpr int gShadowmapHeight = 2048;

  double rsmPerformance = 0;
  double fsr2Performance = 0;

  // scene parameters
  float sunElevation = -2.881f;
  float sunAzimuth = 0;
  float sunStrength = 50;
  glm::vec3 sunColor = {1, 1, 1};

  float aspectRatio = 1;

  // True: output size will be equal to GUI viewport resolution
  // False: output size will be equal to window resolution
  bool useGuiViewportSizeForRendering = true;

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
    std::optional<Fwog::Texture> gNormal;
    std::optional<Fwog::Texture> gEmission;
    std::optional<Fwog::Texture> gMotion;
    std::optional<Fwog::Texture> gDepth;


    // Previous-frame G-buffer textures used for temporal effects
    std::optional<Fwog::Texture> gNormalPrev;
    std::optional<Fwog::Texture> gDepthPrev;

    // Post-lighting
    std::optional<Fwog::Texture> colorHdrRenderRes;

    // Post-upscale (skipped if render res == window res)
    std::optional<Fwog::Texture> colorHdrWindowRes;

    // Final tonemapped color
    std::optional<Fwog::Texture> colorLdrWindowRes;

    // Per-technique frame resources
    std::optional<RSM::RsmTechnique> rsm;

    // For debug drawing with ImGui
    std::optional<Fwog::TextureView> gAlbedoSwizzled;
    std::optional<Fwog::TextureView> gRoughnessMetallicAoSwizzled;
    std::optional<Fwog::TextureView> gEmissionSwizzled;
    std::optional<Fwog::TextureView> gNormalSwizzled;
    std::optional<Fwog::TextureView> gDepthSwizzled;
    std::optional<Fwog::TextureView> gRsmIlluminanceSwizzled;
  };
  Frame frame{};

  // Reflective shadow map textures
  Fwog::Texture rsmFlux;
  Fwog::Texture rsmNormal;
  Fwog::Texture rsmDepth;

  // For debug drawing with ImGui
  Fwog::TextureView rsmFluxSwizzled;
  Fwog::TextureView rsmNormalSwizzled;
  Fwog::TextureView rsmDepthSwizzled;

  ShadingUniforms shadingUniforms{};
  ShadowUniforms shadowUniforms{};
  GlobalUniforms mainCameraUniforms{};

  Fwog::TypedBuffer<GlobalUniforms> globalUniformsBuffer;
  Fwog::TypedBuffer<ShadingUniforms> shadingUniformsBuffer;
  Fwog::TypedBuffer<ShadowUniforms> shadowUniformsBuffer;

  // Meshlet stuff
  std::optional<Fwog::TypedBuffer<Utility::Meshlet>> meshletBuffer;
  std::optional<Fwog::TypedBuffer<Utility::Vertex>> vertexBuffer;
  std::optional<Fwog::TypedBuffer<uint32_t>> indexBuffer;
  std::optional<Fwog::TypedBuffer<uint8_t>> primitiveBuffer;
  std::optional<Fwog::TypedBuffer<glm::mat4>> transformBuffer;
  std::optional<Fwog::TypedBuffer<Utility::GpuMaterial>> materialStorageBuffer;
  // Output
  std::optional<Fwog::TypedBuffer<Fwog::DrawIndexedIndirectCommand>> mesheletIndirectCommand;
  std::optional<Fwog::TypedBuffer<uint32_t>> instancedMeshletBuffer;

  Fwog::TypedBuffer<glm::mat4> rsmUniforms;

  Fwog::ComputePipeline meshletGeneratePipeline;
  Fwog::ComputePipeline hzbCopyPipeline;
  Fwog::ComputePipeline hzbReducePipeline;
  Fwog::GraphicsPipeline visbufferPipeline;
  Fwog::GraphicsPipeline materialDepthPipeline;
  Fwog::GraphicsPipeline visbufferResolvePipeline;
  Fwog::GraphicsPipeline scenePipeline;
  Fwog::GraphicsPipeline rsmScenePipeline;
  Fwog::GraphicsPipeline shadingPipeline;
  Fwog::GraphicsPipeline postprocessingPipeline;
  Fwog::GraphicsPipeline debugTexturePipeline;

  // Scene
  Utility::SceneMeshlet scene;
  std::optional<Fwog::TypedBuffer<Light>> lightBuffer;
  std::optional<Fwog::TypedBuffer<ObjectUniforms>> meshUniformBuffer;

  // Post processing
  std::optional<Fwog::Texture> noiseTexture;

  uint32_t renderWidth;
  uint32_t renderHeight;
  uint32_t frameIndex = 0;
  uint32_t seed = PCG::Hash(17);

#ifdef FROGRENDER_FSR2_ENABLE
  // FSR 2
  bool fsr2Enable = true;
  bool fsr2FirstInit = true;
  float fsr2Sharpness = 0;
  float fsr2Ratio = 1.7f; // FFX_FSR2_QUALITY_MODE_BALANCED
  FfxFsr2Context fsr2Context;
  std::unique_ptr<char[]> fsr2ScratchMemory;
#else
  const bool fsr2Enable = false;
#endif

  // Magnifier
  bool magnifierLock = false;
  float magnifierZoom = 4;
  glm::vec2 magnifierLastCursorPos = {};
};