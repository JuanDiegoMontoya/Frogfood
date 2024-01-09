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

//layout(location = 0) in vec2 a_pos;
//layout(location = 1) in vec3 a_color;

layout(location = 0) out vec3 v_color;

const vec2 positions[3] = {{-0, -0}, {1, -1}, {1, 1}};
const vec3 colors[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

void main()
{
  v_color = colors[gl_VertexIndex];
  gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
)";

const char* gFragmentSource = R"(
#version 460 core

layout(location = 0) out vec4 o_color;

layout(location = 0) in vec3 v_color;

void main()
{
  o_color = vec4(v_color, 1.0);
  //o_color = vec4(0.0, 1.0, 1.0, 1.0);
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

void CheckVkResult(VkResult result)
{
  // TODO: don't throw for certain non-success codes (since they aren't always errors)
  if (result != VK_SUCCESS)
  {
    throw std::runtime_error("result was not VK_SUCCESS");
  }
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
    printf("Info log: %s\nDebug log: %s\n", shader.getInfoLog(), shader.getInfoDebugLog());
    throw std::runtime_error("rip");
  }

  auto program = glslang::TProgram();
  program.addShader(&shader);

  if (!program.link(EShMessages::EShMsgDefault))
  {
    printf("Info log: %s\nDebug log: %s\n", shader.getInfoLog(), shader.getInfoDebugLog());
    throw std::runtime_error("rip");
  }

  auto options = glslang::SpvOptions{};
  auto spirvVec = std::vector<unsigned>{};
  glslang::GlslangToSpv(*program.getIntermediate(glslangStage), spirvVec, &options);

  return spirvVec;
}

VkPipeline MakeVertexFragmentPipeline(VkDevice device, const char* vertexSource, const char* fragmentSource)
{
  const auto vertexShader = CompileShaderToSpirv(VK_SHADER_STAGE_VERTEX_BIT, vertexSource);
  const auto fragmentShader = CompileShaderToSpirv(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentSource);

  auto vertexModule = VkShaderModule{};
  CheckVkResult(
    vkCreateShaderModule(
      device,
      Address(VkShaderModuleCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vertexShader.size() * sizeof(uint32_t),
        .pCode = vertexShader.data(),
      }),
      nullptr,
      &vertexModule));

  auto fragmentModule = VkShaderModule{};
  CheckVkResult(
    vkCreateShaderModule(
      device,
      Address(VkShaderModuleCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = fragmentShader.size() * sizeof(uint32_t),
        .pCode = fragmentShader.data(),
      }),
      nullptr,
      &fragmentModule));

  const auto vertexStage = VkPipelineShaderStageCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_VERTEX_BIT,
    .module = vertexModule,
    .pName = "main",
  };

  const auto fragmentStage = VkPipelineShaderStageCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
    .module = fragmentModule,
    .pName = "main",
  };

  const std::array stages = {vertexStage, fragmentStage};
  const std::array dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

  auto pipelineLayout = VkPipelineLayout{};
  CheckVkResult(
    vkCreatePipelineLayout(
      device,
      Address(VkPipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,
      }),
      nullptr,
      &pipelineLayout));

  auto swapchainFormat = VK_FORMAT_B8G8R8A8_SRGB;

  auto pipeline = VkPipeline{};
  CheckVkResult(
    vkCreateGraphicsPipelines(
      device,
      nullptr,
      1,
      Address(VkGraphicsPipelineCreateInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = Address(VkPipelineRenderingCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
          .viewMask = 1,
          .colorAttachmentCount = 1,
          .pColorAttachmentFormats = &swapchainFormat,
          .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
          .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
        }),
        .stageCount = (uint32_t)stages.size(),
        .pStages = stages.data(),
        .pVertexInputState = Address(VkPipelineVertexInputStateCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        }),
        .pInputAssemblyState = Address(VkPipelineInputAssemblyStateCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
          .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        }),
        .pViewportState = Address(VkPipelineViewportStateCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
          .viewportCount = 1,
          .pViewports = nullptr, // VK_DYNAMIC_STATE_VIEWPORT
          .scissorCount = 1,
          .pScissors = nullptr, //VK_DYNAMIC_STATE_SCISSOR
        }),
        .pRasterizationState = Address(VkPipelineRasterizationStateCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
          .polygonMode = VK_POLYGON_MODE_FILL,
          .cullMode = VK_CULL_MODE_NONE,
          .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
          .lineWidth = 1.0f,
        }),
        .pMultisampleState = Address(VkPipelineMultisampleStateCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
          .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        }),
        .pDepthStencilState = Address(VkPipelineDepthStencilStateCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        }),
        .pColorBlendState = Address(VkPipelineColorBlendStateCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
          .attachmentCount = 1,
          .pAttachments = Address(VkPipelineColorBlendAttachmentState{
            //.blendEnable = true,
            //.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            //.dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
          }),
        }),
        .pDynamicState = Address(VkPipelineDynamicStateCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
          .dynamicStateCount = (uint32_t)dynamicStates.size(),
          .pDynamicStates = dynamicStates.data(),
        }),
        .layout = pipelineLayout,
        .renderPass = VK_NULL_HANDLE,
      }), 
      nullptr, 
      &pipeline
      ));

  vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
  vkDestroyShaderModule(device, fragmentModule, nullptr);
  vkDestroyShaderModule(device, vertexModule, nullptr);

  return pipeline;
}

struct VulkanStuff
{
  VulkanStuff();
  ~VulkanStuff();

  void Run();

  constexpr static uint32_t frameOverlap = 2;

  struct PerFrameData
  {
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkFence renderFence;
    VkSemaphore presentSemaphore;
    VkSemaphore renderSemaphore;
  };

  PerFrameData frameData[frameOverlap]{};

  PerFrameData& GetCurrentFrameData()
  {
    return frameData[frameNumber % frameOverlap];
  }

  GLFWwindow* window{};
  vkb::Instance vkbInstance{};
  VkSurfaceKHR surface{VK_NULL_HANDLE};
  vkb::PhysicalDevice vkbPhysicalDevice;
  vkb::Device vkbDevice;
  vkb::Swapchain vkbSwapchain;
  std::vector<VkImage> swapchainImages;
  std::vector<VkImageView> swapchainImageViews;
  VkPipeline pipeline;

  uint64_t frameNumber{};
  VkQueue graphicsQueue{};
  uint32_t graphicsQueueFamily{};
};

VulkanStuff::VulkanStuff()
{
  // init glfw and create window
  if (glfwInit() != GLFW_TRUE)
  {
    throw std::runtime_error("rip");
  }

  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  window = glfwCreateWindow(1920, 1080, "hello", nullptr, nullptr);
  if (!window)
  {
    throw std::runtime_error("rip");
  }

  // instance
  vkbInstance = vkb::InstanceBuilder()
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

  // surface
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

  // physical device
  vkbPhysicalDevice = selector
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

  // device
  vkbDevice = vkb::DeviceBuilder{vkbPhysicalDevice}.build().value();
  graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
  graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

  // swapchain
  vkbSwapchain = vkb::SwapchainBuilder{vkbDevice}
    .set_old_swapchain(VK_NULL_HANDLE)
    .set_desired_min_image_count(2)
    .use_default_present_mode_selection()
    .set_desired_extent(1920, 1080)
    .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
    .build()
    .value();

  swapchainImages = vkbSwapchain.get_images().value();
  swapchainImageViews = vkbSwapchain.get_image_views().value();

  // command pools and command buffers
  for (uint32_t i = 0; i < frameOverlap; i++)
  {
    CheckVkResult(vkCreateCommandPool(vkbDevice.device, Address(VkCommandPoolCreateInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = graphicsQueueFamily,
    }), nullptr, &frameData[i].commandPool));

    CheckVkResult(vkAllocateCommandBuffers(vkbDevice.device, Address(VkCommandBufferAllocateInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = frameData[i].commandPool,
      .commandBufferCount = 1,
    }), &frameData[i].commandBuffer));

    CheckVkResult(vkCreateFence(vkbDevice.device, Address(VkFenceCreateInfo{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    }), nullptr, &frameData[i].renderFence));

    CheckVkResult(vkCreateSemaphore(vkbDevice.device, Address(VkSemaphoreCreateInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    }), nullptr, &frameData[i].presentSemaphore));

    CheckVkResult(vkCreateSemaphore(vkbDevice.device, Address(VkSemaphoreCreateInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    }), nullptr, &frameData[i].renderSemaphore));
  }

  if (!glslang::InitializeProcess())
  {
    throw std::runtime_error("rip");
  }

  pipeline = MakeVertexFragmentPipeline(vkbDevice.device, gVertexSource, gFragmentSource);
}

VulkanStuff::~VulkanStuff()
{
  vkDeviceWaitIdle(vkbDevice.device);

  vkDestroyPipeline(vkbDevice.device, pipeline, nullptr);

  glslang::FinalizeProcess();

  for (auto& frame : frameData)
  {
    vkDestroyCommandPool(vkbDevice.device, frame.commandPool, nullptr);
    vkDestroyFence(vkbDevice.device, frame.renderFence, nullptr);
    vkDestroySemaphore(vkbDevice.device, frame.renderSemaphore, nullptr);
    vkDestroySemaphore(vkbDevice.device, frame.presentSemaphore, nullptr);
  }

  vkb::destroy_swapchain(vkbSwapchain);

  for (auto view : swapchainImageViews)
  {
    vkDestroyImageView(vkbDevice.device, view, nullptr);
  }

  vkb::destroy_surface(vkbInstance, surface);
  vkb::destroy_device(vkbDevice);
  vkb::destroy_debug_utils_messenger(vkbInstance.instance, vkbInstance.debug_messenger, nullptr);
  vkDestroyInstance(vkbInstance.instance, nullptr);

  glfwDestroyWindow(window);
  glfwTerminate();
}

void VulkanStuff::Run()
{
  while (!glfwWindowShouldClose(window))
  {
    glfwPollEvents();

    if (glfwGetKey(window, GLFW_KEY_ESCAPE))
    {
      glfwSetWindowShouldClose(window, true);
    }

    CheckVkResult(vkWaitForFences(vkbDevice.device, 1, &GetCurrentFrameData().renderFence, VK_TRUE, static_cast<uint64_t>(-1)));
    CheckVkResult(vkResetFences(vkbDevice.device, 1, &GetCurrentFrameData().renderFence));

    // TODO:
    // On success, this command returns
    //    VK_SUCCESS
    //    VK_TIMEOUT
    //    VK_NOT_READY
    //    VK_SUBOPTIMAL_KHR
    uint32_t swapchainImageIndex{};
    CheckVkResult(vkAcquireNextImage2KHR(vkbDevice.device, Address(VkAcquireNextImageInfoKHR{
      .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
      .swapchain = vkbSwapchain.swapchain,
      .timeout = static_cast<uint64_t>(-1),
      .semaphore = GetCurrentFrameData().presentSemaphore,
      .deviceMask = 1,
    }), &swapchainImageIndex));

    auto commandBuffer = GetCurrentFrameData().commandBuffer;

    CheckVkResult(vkResetCommandPool(vkbDevice.device, GetCurrentFrameData().commandPool, 0));

    CheckVkResult(vkBeginCommandBuffer(commandBuffer, Address(VkCommandBufferBeginInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    })));

    // undefined -> general
    vkCmdPipelineBarrier2(commandBuffer, Address(VkDependencyInfo{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = Address(VkImageMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL, // TODO: pick a more specific layout
        .image = swapchainImages[swapchainImageIndex],
        .subresourceRange = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .levelCount = VK_REMAINING_MIP_LEVELS,
          .layerCount = VK_REMAINING_ARRAY_LAYERS,
        },
      }),
    }));

    vkCmdClearColorImage(
      commandBuffer,
      swapchainImages[swapchainImageIndex],
      VK_IMAGE_LAYOUT_GENERAL,
      Address(VkClearColorValue{.float32 = {1, 1, std::sinf(frameNumber / 1000.0f) * .5f + .5f, 1}}),
      1,
      Address(VkImageSubresourceRange{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
      }));

    vkCmdBeginRendering(commandBuffer, Address(VkRenderingInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {0, 0, 1920, 1080},
      .layerCount = 1,
      .viewMask = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = Address(VkRenderingAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchainImageViews[swapchainImageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      }),
    }));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdSetViewport(commandBuffer, 0, 1, Address(VkViewport{0, 0, 1920, 1080, 0, 1}));
    vkCmdSetScissor(commandBuffer, 0, 1, Address(VkRect2D{0, 0, 1920, 1080}));
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    vkCmdEndRendering(commandBuffer);

    // general -> presentable
    vkCmdPipelineBarrier2(commandBuffer, Address(VkDependencyInfo{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = Address(VkImageMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL, // TODO: pick a more specific layout
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = swapchainImages[swapchainImageIndex],
        .subresourceRange = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .levelCount = VK_REMAINING_MIP_LEVELS,
          .layerCount = VK_REMAINING_ARRAY_LAYERS,
        },
      }),
    }));

    // End recording
    CheckVkResult(vkEndCommandBuffer(commandBuffer));

    // Submit
    CheckVkResult(vkQueueSubmit2(
      graphicsQueue,
      1,
      Address(VkSubmitInfo2{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = Address(VkSemaphoreSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = GetCurrentFrameData().presentSemaphore,
          .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        }),
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = Address(VkCommandBufferSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
          .commandBuffer = commandBuffer,
        }),
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = Address(VkSemaphoreSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = GetCurrentFrameData().renderSemaphore,
          .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        }),
      }),
      GetCurrentFrameData().renderFence)
    );
    
    // Present
    CheckVkResult(vkQueuePresentKHR(graphicsQueue, Address(VkPresentInfoKHR{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &GetCurrentFrameData().renderSemaphore,
      .swapchainCount = 1,
      .pSwapchains = &vkbSwapchain.swapchain,
      .pImageIndices = &swapchainImageIndex,
    })));

    frameNumber++;
  }
}

int main()
{
  ZoneScoped;

  VulkanStuff stuff;

  stuff.Run();

  //auto appInfo = Application::CreateInfo{.name = "FrogRender", .vsync = true};
  //auto app = FrogRenderer(appInfo);
  //app.Run();

  return 0;
}
