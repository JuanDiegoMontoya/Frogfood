/* 
 *
 * TODO rendering:
 * Core:
 * [X] glTF loader
 * [X] FSR 2
 * [X] PBR punctual lights
 * [ ] Skinned animation
 * [ ] Transparency
 *
 * Low-level
 * [X] Visibility buffer
 * [X] Frustum culling
 * [X] Hi-z occlusion culling
 * [ ] Clustered light culling
 * [-] Raster occlusion culling
 * [X] Multi-view
 * [X] Triangle culling: https://www.slideshare.net/gwihlidal/optimizing-the-graphics-pipeline-with-compute-gdc-2016
 *   [X] Back-facing
 *   [X] Zero-area
 *   [X] Small primitive (doesn't overlap pixel)
 *   [X] Frustum
 *
 * Atmosphere
 * [ ] Sky
 * [ ] Volumetric fog
 * [ ] Clouds
 *
 * Effects:
 * [X] Bloom
 *
 * Resolve:
 * [X] Auto exposure
 * [ ] Auto whitepoint
 * [ ] Local tonemapping
 * [ ] Purkinje shift
 *
 * Tryhard:
 * [ ] BVH builder
 * [ ] Path tracer
 * [ ] DDGI
 * [ ] Surfel GI
 *
 * TODO UI:
 * [X] Render into an ImGui window
 * [X] Install a font + header from here: https://github.com/juliettef/IconFontCppHeaders
 * [ ] Figure out an epic layout
 * [ ] Config file so stuff doesn't reset every time
 * [ ] Model browser
 * [ ] Command console
 *
 * Debugging:
 * [ ] Meshlet color view
 * [ ] Meshlet HZB LoD view
 * [ ] Frustum visualization for other views
 * [ ] Texture viewer (basically the one from RenderDoc):
 *   [ ] Register textures to list of viewable textures in GUI (perhaps just a vector of Texture*)
 *   [ ] Selector for range of displayable values
 *   [ ] Toggles for visible channels
 *   [ ] Selectors for mip level and array layer, if applicable
 *   [ ] Option to tint the window/image alpha to see the window behind it (i.e., overlay the image onto the main viewport)
 *   [ ] Toggle to scale the image to the viewport (would be helpful to view square textures like the hzb properly overlaid on the scene)
 *   [ ] Standard scroll zoom and drag controls
 */

//#include "FrogRenderer.h"

#include <tracy/Tracy.hpp>

#include <cstring>
#include <iostream>
#include <stdexcept>

#define VK_NO_PROTOTYPES
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <volk.h>

#include <VkBootstrap.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

//#include "RendererUtilities.h"

const char* gVertexSource = R"(
#version 460 core

layout(location = 0) in vec2 a_pos;
//layout(location = 1) in vec3 a_color;

//layout(location = 0) out vec3 v_color;

void main()
{
  //v_color = a_color;
  gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

const char* gFragmentSource = R"(
#version 460 core

layout(location = 0) out vec4 o_color;

//layout(location = 0) in vec3 v_color;

void main()
{
  //o_color = vec4(v_color, 1.0);
  o_color = vec4(0.0, 1.0, 1.0, 1.0);
}
)";

#include <optional>
#include <memory>
#include <string_view>

template<class T>
[[nodiscard]] T* Address(T&& v)
{
  return std::addressof(v);
}

TBuiltInResource GetGlslangResourceLimits()
{
  return
  {
    .maxLights = 32,
    .maxClipPlanes = 6,
    .maxTextureUnits = 32,
    .maxTextureCoords = 32,
    .maxVertexAttribs = 64,
    .maxVertexUniformComponents = 4096,
    .maxVaryingFloats = 64,
    .maxVertexTextureImageUnits = 32,
    .maxCombinedTextureImageUnits = 80,
    .maxTextureImageUnits = 32,
    .maxFragmentUniformComponents = 4096,
    .maxDrawBuffers = 32,
    .maxVertexUniformVectors = 128,
    .maxVaryingVectors = 8,
    .maxFragmentUniformVectors = 16,
    .maxVertexOutputVectors = 16,
    .maxFragmentInputVectors = 15,
    .minProgramTexelOffset = -8,
    .maxProgramTexelOffset = 7,
    .maxClipDistances = 8,
    .maxComputeWorkGroupCountX = 65535,
    .maxComputeWorkGroupCountY = 65535,
    .maxComputeWorkGroupCountZ = 65535,
    .maxComputeWorkGroupSizeX = 1024,
    .maxComputeWorkGroupSizeY = 1024,
    .maxComputeWorkGroupSizeZ = 64,
    .maxComputeUniformComponents = 1024,
    .maxComputeTextureImageUnits = 16,
    .maxComputeImageUniforms = 8,
    .maxComputeAtomicCounters = 8,
    .maxComputeAtomicCounterBuffers = 1,
    .maxVaryingComponents = 60,
    .maxVertexOutputComponents = 64,
    .maxGeometryInputComponents = 64,
    .maxGeometryOutputComponents = 128,
    .maxFragmentInputComponents = 128,
    .maxImageUnits = 8,
    .maxCombinedImageUnitsAndFragmentOutputs = 8,
    .maxCombinedShaderOutputResources = 8,
    .maxImageSamples = 0,
    .maxVertexImageUniforms = 0,
    .maxTessControlImageUniforms = 0,
    .maxTessEvaluationImageUniforms = 0,
    .maxGeometryImageUniforms = 0,
    .maxFragmentImageUniforms = 8,
    .maxCombinedImageUniforms = 8,
    .maxGeometryTextureImageUnits = 16,
    .maxGeometryOutputVertices = 256,
    .maxGeometryTotalOutputComponents = 1024,
    .maxGeometryUniformComponents = 1024,
    .maxGeometryVaryingComponents = 64,
    .maxTessControlInputComponents = 128,
    .maxTessControlOutputComponents = 128,
    .maxTessControlTextureImageUnits = 16,
    .maxTessControlUniformComponents = 1024,
    .maxTessControlTotalOutputComponents = 4096,
    .maxTessEvaluationInputComponents = 128,
    .maxTessEvaluationOutputComponents = 128,
    .maxTessEvaluationTextureImageUnits = 16,
    .maxTessEvaluationUniformComponents = 1024,
    .maxTessPatchComponents = 120,
    .maxPatchVertices = 32,
    .maxTessGenLevel = 64,
    .maxViewports = 16,
    .maxVertexAtomicCounters = 0,
    .maxTessControlAtomicCounters = 0,
    .maxTessEvaluationAtomicCounters = 0,
    .maxGeometryAtomicCounters = 0,
    .maxFragmentAtomicCounters = 8,
    .maxCombinedAtomicCounters = 8,
    .maxAtomicCounterBindings = 1,
    .maxVertexAtomicCounterBuffers = 0,
    .maxTessControlAtomicCounterBuffers = 0,
    .maxTessEvaluationAtomicCounterBuffers = 0,
    .maxGeometryAtomicCounterBuffers = 0,
    .maxFragmentAtomicCounterBuffers = 1,
    .maxCombinedAtomicCounterBuffers = 1,
    .maxAtomicCounterBufferSize = 16384,
    .maxTransformFeedbackBuffers = 4,
    .maxTransformFeedbackInterleavedComponents = 64,
    .maxCullDistances = 8,
    .maxCombinedClipAndCullDistances = 8,
    .maxSamples = 4,
    .maxMeshOutputVerticesNV = 256,
    .maxMeshOutputPrimitivesNV = 512,
    .maxMeshWorkGroupSizeX_NV = 32,
    .maxMeshWorkGroupSizeY_NV = 1,
    .maxMeshWorkGroupSizeZ_NV = 1,
    .maxTaskWorkGroupSizeX_NV = 32,
    .maxTaskWorkGroupSizeY_NV = 1,
    .maxTaskWorkGroupSizeZ_NV = 1,
    .maxMeshViewCountNV = 4,
    .limits =
    {
      .nonInductiveForLoops = true,
      .whileLoops = true,
      .doWhileLoops = true,
      .generalUniformIndexing = true,
      .generalAttributeMatrixVectorIndexing = true,
      .generalVaryingIndexing = true,
      .generalSamplerIndexing = true,
      .generalVariableIndexing = true,
      .generalConstantMatrixVectorIndexing = true,
    },
  };
}

constexpr EShLanguage VkShaderStageToGlslang(VkShaderStageFlagBits stage)
{
  switch (stage)
  {
  case VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT: return EShLanguage::EShLangVertex;
  case VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT: return EShLanguage::EShLangFragment;
  case VkShaderStageFlagBits::VK_SHADER_STAGE_COMPUTE_BIT: return EShLanguage::EShLangCompute;
  }
  return static_cast<EShLanguage>(-1);
}

std::vector<uint32_t> CompileShaderToSpirv(VkShaderStageFlagBits stage, const char* source)
{
  constexpr auto compilerMessages = EShMessages(EShMessages::EShMsgSpvRules | EShMessages::EShMsgVulkanRules);
  const auto resourceLimits = GetGlslangResourceLimits();
  const auto glslangStage = VkShaderStageToGlslang(stage);

  auto shader = glslang::TShader(glslangStage);
  shader.setStrings(&source, 1);
  shader.setEnvInput(glslang::EShSource::EShSourceGlsl, glslangStage, glslang::EShClient::EShClientVulkan, 460);
  shader.setEnvClient(glslang::EShClient::EShClientVulkan, glslang::EShTargetClientVersion::EShTargetVulkan_1_3);
  shader.setEnvTarget(glslang::EShTargetLanguage::EShTargetSpv, glslang::EShTargetLanguageVersion::EShTargetSpv_1_6);
  if (!shader.parse(&resourceLimits, 450, EProfile::ECoreProfile, false, false, compilerMessages))
  {
    throw std::runtime_error("rip");
  }

  auto program = glslang::TProgram();
  program.addShader(&shader);

  if (!program.link(EShMessages::EShMsgDefault))
  {
    throw std::runtime_error("rip");
  }

  auto options = glslang::SpvOptions{};
  auto spirvVec = std::vector<unsigned>{};
  glslang::GlslangToSpv(*program.getIntermediate(glslangStage), spirvVec, &options);

  return spirvVec;
}

int main()
{
  ZoneScoped;

  if (glfwInit() != GLFW_TRUE)
  {
    throw std::runtime_error("rip");
  }

  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(800, 600, "hello", nullptr, nullptr);
  if (!window)
  {
    throw std::runtime_error("rip");
  }

  auto vkbInstance = vkb::InstanceBuilder()
    .set_app_name("Frogrenderer")
    .require_api_version(1, 3, 0)
    .request_validation_layers()
    .use_default_debug_messenger()
    .build()
    .value();

  if (volkInitialize() != VK_SUCCESS)
  {
    throw std::runtime_error("rip");
  }

  volkLoadInstance(vkbInstance.instance);

  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (auto err = glfwCreateWindowSurface(vkbInstance.instance, window, nullptr, &surface); err != VK_SUCCESS)
  {
    const char* error_msg;
    if (int ret = glfwGetError(&error_msg))
    {
      std::cout << ret << " ";
      if (error_msg != nullptr)
        std::cout << error_msg;
      std::cout << "\n";
    }
    throw std::runtime_error("rip");
  }

  auto selector = vkb::PhysicalDeviceSelector{vkbInstance};

  auto vkbPhysicalDevice = selector
    .set_minimum_version(1, 3)
    .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
    .require_present()
    .set_surface(surface)
    .set_required_features({
      .multiDrawIndirect = true,
      .textureCompressionBC = true,
      .fragmentStoresAndAtomics = true,
      .shaderStorageImageExtendedFormats = true,
      .shaderStorageImageReadWithoutFormat = true,
      .shaderStorageImageWriteWithoutFormat = true,
      .shaderUniformBufferArrayDynamicIndexing = true,
      .shaderSampledImageArrayDynamicIndexing = true,
      .shaderStorageBufferArrayDynamicIndexing = true,
      .shaderStorageImageArrayDynamicIndexing = true,
      .shaderClipDistance = true,
      .shaderCullDistance = true,
      .shaderFloat64 = true,
      .shaderInt64 = true,
    })
    .set_required_features_11({
      .storageBuffer16BitAccess = true,
      .uniformAndStorageBuffer16BitAccess = true,
      .multiview = true,
      .variablePointersStorageBuffer = true,
      .variablePointers = true,
      .shaderDrawParameters = true,
    })
    .set_required_features_12({
      .drawIndirectCount = true,
      .storageBuffer8BitAccess = true,
      .uniformAndStorageBuffer8BitAccess = true,
      .shaderFloat16 = true,
      .shaderInt8 = true,
      .descriptorIndexing = true,
      .shaderInputAttachmentArrayDynamicIndexing = true,
      .shaderUniformTexelBufferArrayDynamicIndexing = true,
      .shaderStorageTexelBufferArrayDynamicIndexing = true,
      .shaderUniformBufferArrayNonUniformIndexing = true,
      .shaderSampledImageArrayNonUniformIndexing = true,
      .shaderStorageBufferArrayNonUniformIndexing = true,
      .shaderStorageImageArrayNonUniformIndexing = true,
      .shaderUniformTexelBufferArrayNonUniformIndexing = true,
      .shaderStorageTexelBufferArrayNonUniformIndexing = true,
      .descriptorBindingSampledImageUpdateAfterBind = true,
      .descriptorBindingStorageImageUpdateAfterBind = true,
      .descriptorBindingStorageBufferUpdateAfterBind = true,
      .descriptorBindingUniformTexelBufferUpdateAfterBind = true,
      .descriptorBindingUpdateUnusedWhilePending = true,
      .descriptorBindingPartiallyBound = true,
      .descriptorBindingVariableDescriptorCount = true,
      .runtimeDescriptorArray = true,
      .samplerFilterMinmax = true,
      .scalarBlockLayout = true,
      .imagelessFramebuffer = true,
      .uniformBufferStandardLayout = true,
      .shaderSubgroupExtendedTypes = true,
      .separateDepthStencilLayouts = true,
      .hostQueryReset = true,
      .timelineSemaphore = true,
      .bufferDeviceAddress = true,
      .vulkanMemoryModel = true,
      .vulkanMemoryModelDeviceScope = true,
      .subgroupBroadcastDynamicId = true,
    })
    .set_required_features_13({
      .shaderDemoteToHelperInvocation = true,
      .shaderTerminateInvocation = true,
      .synchronization2 = true,
      .dynamicRendering = true,
      .shaderIntegerDotProduct = true,
      .maintenance4 = true,
    })
    .select()
    .value();

  auto vkbDevice = vkb::DeviceBuilder{vkbPhysicalDevice}.build().value();

  auto vkbSwapchain = vkb::SwapchainBuilder{vkbDevice}
    .set_old_swapchain(VK_NULL_HANDLE)
    .set_desired_min_image_count(2)
    .use_default_present_mode_selection()
    .build()
    .value();

  if (!glslang::InitializeProcess())
  {
    throw std::runtime_error("rip");
  }

  const auto vertexShader = CompileShaderToSpirv(VK_SHADER_STAGE_VERTEX_BIT, gVertexSource);
  const auto fragmentShader = CompileShaderToSpirv(VK_SHADER_STAGE_FRAGMENT_BIT, gFragmentSource);

  auto vertexModule = VkShaderModule{};
  if (vkCreateShaderModule(
    vkbDevice.device,
    Address(VkShaderModuleCreateInfo{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = vertexShader.size() * sizeof(uint32_t),
      .pCode = vertexShader.data(),
    }),
    nullptr,
    &vertexModule
  ) != VK_SUCCESS)
  {
    throw std::runtime_error("rip");
  }

  auto fragmentModule = VkShaderModule{};
  if (vkCreateShaderModule(
    vkbDevice.device,
    Address(VkShaderModuleCreateInfo{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = fragmentShader.size() * sizeof(uint32_t),
      .pCode = fragmentShader.data(),
    }),
    nullptr,
    &fragmentModule
  ) != VK_SUCCESS)
  {
    throw std::runtime_error("rip");
  }

  const auto vertexStage = VkPipelineShaderStageCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_VERTEX_BIT,
    .module = vertexModule,
    .pName = "main",
  };

  const auto fragmentStage = VkPipelineShaderStageCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_VERTEX_BIT,
    .module = fragmentModule,
    .pName = "main",
  };

  const std::array stages = {vertexStage, fragmentStage};
  const std::array dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

  auto pipeline = VkPipeline{};
  vkCreateGraphicsPipelines(
    vkbDevice.device,
    nullptr,
    1,
    Address(VkGraphicsPipelineCreateInfo{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = stages.size(),
      .pStages = stages.data(),
      .pVertexInputState = Address(VkPipelineVertexInputStateCreateInfo{

      }),
      .pInputAssemblyState = Address(VkPipelineInputAssemblyStateCreateInfo{

      }),
      .pViewportState = Address(VkPipelineViewportStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = nullptr, // VK_DYNAMIC_STATE_VIEWPORT
        .scissorCount = 1,
        .pScissors = nullptr, //VK_DYNAMIC_STATE_SCISSOR
      }),
      .pRasterizationState = Address(VkPipelineRasterizationStateCreateInfo{

      }),
      .pDepthStencilState = Address(VkPipelineDepthStencilStateCreateInfo{

      }),
      .pColorBlendState = Address(VkPipelineColorBlendStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = Address(VkPipelineColorBlendAttachmentState{
          .blendEnable = false,
        }),
      }),
      .pDynamicState = Address(VkPipelineDynamicStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = dynamicStates.size(),
        .pDynamicStates = dynamicStates.data(),
      }),
    }), 
    nullptr, 
    &pipeline);

  //auto appInfo = Application::CreateInfo{.name = "FrogRender", .vsync = true};
  //auto app = FrogRenderer(appInfo);
  //app.Run();

  glslang::FinalizeProcess();
  vkb::destroy_swapchain(vkbSwapchain);
  vkb::destroy_surface(vkbInstance, surface);
  vkb::destroy_device(vkbDevice);
  vkb::destroy_debug_utils_messenger(vkbInstance.instance, vkbInstance.debug_messenger, nullptr);
  vkDestroyInstance(vkbInstance.instance, nullptr);

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}