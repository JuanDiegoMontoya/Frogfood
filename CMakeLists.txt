cmake_minimum_required(VERSION 3.15)

set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "")

project(Frogfood)

set(CMAKE_CXX_STANDARD 20)

option(FROGRENDER_FSR2_ENABLE "Enable FidelityFX Super Resolution 2. Windows only!" FALSE)

find_package(Vulkan REQUIRED)

add_executable(frogRender
    src/main.cpp
    src/Application.cpp
    src/Application.h
    src/SceneLoader.cpp
    src/SceneLoader.h
    vendor/stb_image.cpp
    vendor/tiny_obj_loader.cpp
    src/Gui.cpp
    src/PCG.h
    src/techniques/Bloom.h
    src/techniques/Bloom.cpp
    src/RendererUtilities.h
    src/RendererUtilities.cpp
    src/techniques/AutoExposure.h
    src/techniques/AutoExposure.cpp
    src/techniques/VirtualShadowMaps.h
    src/techniques/VirtualShadowMaps.cpp
    src/Fvog/Shader2.h
    src/Fvog/Shader2.cpp
    src/Fvog/detail/Common.h
    src/Fvog/detail/Common.cpp
    src/Fvog/Pipeline2.h
    src/Fvog/Pipeline2.cpp
    src/Fvog/BasicTypes2.h
    src/Fvog/detail/Flags.h
    src/Fvog/Texture2.h
    src/Fvog/Texture2.cpp
    src/Fvog/Device.h
    src/Fvog/Device.cpp
    src/Fvog/Rendering2.cpp
    src/Fvog/Rendering2.h
    src/Fvog/detail/ApiToEnum2.h
    src/Fvog/detail/ApiToEnum2.cpp
    src/Fvog/Buffer2.h
    src/Fvog/Buffer2.cpp
    src/FrogRenderer2.h
    src/FrogRenderer2.cpp
    src/Fvog/detail/SamplerCache2.h
    src/Fvog/detail/SamplerCache2.cpp
    src/Fvog/detail/Hash2.h
    src/Fvog/TriviallyCopyableByteSpan.h
    src/ImGui/imgui_impl_fvog.cpp
    src/ImGui/imgui_impl_fvog.h
    src/Fvog/Timer2.h
    src/Fvog/Timer2.cpp
    src/Scene.h
    src/Scene.cpp
    src/Renderables.h
    src/Fvog/AccelerationStructure.h
    src/Fvog/AccelerationStructure.cpp
    src/debug/ForwardRenderer.h
    src/debug/ForwardRenderer.cpp
    src/techniques/ao/RayTracedAO.h
    src/techniques/ao/RayTracedAO.cpp
    src/PipelineManager.h
    src/PipelineManager.cpp
    src/Voxels/VoxelRenderer.h
    src/Voxels/VoxelRenderer.cpp
    src/Voxels/Game.cpp
    src/Voxels/Game.h
    src/Voxels/ClassImplMacros.h
    src/Voxels/PlayerHead.h
    src/Voxels/PlayerHead.cpp
    src/Voxels/Assets.h
    src/Voxels/Assets.cpp
    src/Voxels/TwoLevelGrid.h
    src/Voxels/TwoLevelGrid.cpp
    src/Voxels/SketchyBuffer.h
    src/Voxels/SketchyBuffer.cpp
    src/Voxels/Input.h
    src/Voxels/Input.cpp
    src/Voxels/Physics/Physics.h
    src/Voxels/Physics/Physics.cpp
    src/Voxels/Physics/TwoLevelGridShape.h
    src/Voxels/Physics/TwoLevelGridShape.cpp
    src/Voxels/Physics/PhysicsUtils.h
    src/Voxels/Physics/PhysicsUtils.cpp
    src/Voxels/Physics/DebugRenderer.h
    src/Voxels/Physics/DebugRenderer.cpp
    src/debug/Shapes.h
    src/Voxels/Pathfinding.h
    src/Voxels/Pathfinding.cpp
    src/Voxels/LRUCache.h
    src/HashUtilities.h
    src/techniques/denoising/spatial/Bilateral.h
    src/techniques/denoising/spatial/Bilateral.cpp
    src/Voxels/HashGrid.h
)

target_compile_options(frogRender
    PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
    -Wall
    -Wextra
    -pedantic-errors
    -Wno-missing-field-initializers
    -Wno-unused-result
    >
    $<$<CXX_COMPILER_ID:MSVC>:
    /W4
    /WX
    /permissive-
    /wd4324 # structure was padded
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:
    /JMC
    >
    >
)

option(FROGRENDER_FORCE_COLORED_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." TRUE)
if (${FORCE_COLORED_OUTPUT})
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
       add_compile_options(-fdiagnostics-color=always)
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
       add_compile_options(-fcolor-diagnostics)
    endif()
endif()

add_subdirectory(external)

if (FROGRENDER_FSR2_ENABLE)
    set(FSR2_LIBS ffx_fsr2_api_x64 ffx_fsr2_api_vk_x64)
    target_compile_definitions(frogRender PUBLIC FROGRENDER_FSR2_ENABLE)
else()
    set(FSR2_LIBS "")
endif()

target_compile_definitions(frogRender PUBLIC
    VMA_VULKAN_VERSION=1002000 # Allow VMA to use Vulkan 1.2 functions (BDA)
)

target_include_directories(frogRender
    PUBLIC
    ${FSR2_SOURCE}
    ${IMPLOT_SOURCE}
    ${Vulkan_INCLUDE_DIRS}
    vendor
    data
    src
)

target_link_libraries(frogRender
    PRIVATE
    glfw
    glm
    lib_imgui
    ${FSR2_LIBS}
    ktx
    fastgltf
    meshoptimizer
    Tracy::TracyClient
    lib_implot
    volk::volk
    vk-bootstrap::vk-bootstrap
    VulkanMemoryAllocator
    glslang
    glslang-default-resource-limits
    SPIRV
    EnTT
)

target_compile_definitions(glm INTERFACE GLM_FORCE_DEPTH_ZERO_TO_ONE VK_NO_PROTOTYPES GLFW_INCLUDE_NONE ImTextureID=ImU64)

if (MSVC)
    target_compile_definitions(frogRender PUBLIC STBI_MSC_SECURE_CRT)
else()
    target_link_libraries(frogRender PRIVATE tbb)
endif()
