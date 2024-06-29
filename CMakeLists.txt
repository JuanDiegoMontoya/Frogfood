cmake_minimum_required(VERSION 3.15)

project(Frogfood)

set(CMAKE_CXX_STANDARD 20)

option(FROGRENDER_FSR2_ENABLE "Enable FSR2 for examples that support it (currently 03_gltf_viewer). Windows only!" FALSE)

find_package(OpenGL REQUIRED)
find_package(Vulkan REQUIRED)

add_executable(frogRender
    src/main.cpp
    src/Application.cpp
    src/Application.h
    src/SceneLoader.cpp
    src/SceneLoader.h
    vendor/stb_image.cpp
    src/Gui.cpp
    #src/FrogRenderer.h
    #src/FrogRenderer.cpp
    src/PCG.h
    src/techniques/Bloom.h
    src/techniques/Bloom.cpp
    src/RendererUtilities.h
    src/RendererUtilities.cpp
    src/techniques/AutoExposure.h
    src/techniques/AutoExposure.cpp
    src/techniques/VirtualShadowMaps.h
    src/techniques/VirtualShadowMaps.cpp
    src/Pipelines.h
    src/Pipelines.cpp
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
    src/Pipelines2.h
    src/Pipelines2.cpp
    src/Fvog/TriviallyCopyableByteSpan.h
    src/ImGui/imgui_impl_fvog.cpp
    src/ImGui/imgui_impl_fvog.h
    src/Fvog/Timer2.h
    src/Fvog/Timer2.cpp
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

add_custom_target(copy_shaders ALL COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/shaders ${CMAKE_CURRENT_BINARY_DIR}/shaders)
add_custom_target(copy_models ALL COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/models ${CMAKE_CURRENT_BINARY_DIR}/models)
add_custom_target(copy_textures ALL COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/textures ${CMAKE_CURRENT_BINARY_DIR}/textures)
add_custom_target(copy_config ALL COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/config ${CMAKE_CURRENT_BINARY_DIR}/config)

if (FROGRENDER_FSR2_ENABLE)
    set(FSR2_LIBS ffx_fsr2_api_x64 ffx_fsr2_api_vk_x64)
    target_compile_definitions(frogRender PUBLIC FROGRENDER_FSR2_ENABLE)
else()
    set(FSR2_LIBS "")
endif()

target_compile_definitions(fwog PUBLIC FWOG_DEFAULT_CLIP_DEPTH_RANGE_ZERO_TO_ONE)

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
    lib_glad
    fwog
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
)
add_dependencies(frogRender copy_shaders copy_models copy_textures copy_config)

target_compile_definitions(glm INTERFACE GLM_FORCE_DEPTH_ZERO_TO_ONE VK_NO_PROTOTYPES GLFW_INCLUDE_NONE ImTextureID=ImU64)

if (MSVC)
    target_compile_definitions(frogRender PUBLIC STBI_MSC_SECURE_CRT)
endif()
