cmake_minimum_required(VERSION 3.15)

project(Frogfood)

set(CMAKE_CXX_STANDARD 20)

option(FROGRENDER_FSR2_ENABLE "Enable FSR2 for examples that support it (currently 03_gltf_viewer). Windows only!" FALSE)

find_package(OpenGL REQUIRED)

add_executable(frogRender
	src/main.cpp
	src/Application.cpp
	src/Application.h
	src/SceneLoader.cpp
	src/SceneLoader.h
	src/RsmTechnique.h
	src/RsmTechnique.cpp
	vendor/stb_image.cpp
	src/Gui.cpp
	src/FrogRenderer.h
	src/FrogRenderer.cpp
	src/PCG.h
)

# enable asan for debug builds
if (DEBUG)
    if (WIN32)
        target_compile_options(frogRender PRIVATE /fsanitize=address)
    else()
        target_compile_options(frogRender PRIVATE -fsanitize=address)
    endif()
endif()

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

if (FROGRENDER_FSR2_ENABLE)
    set(FSR2_LIBS ffx_fsr2_api_x64 ffx_fsr2_api_gl_x64)
    target_compile_definitions(frogRender PUBLIC FROGRENDER_FSR2_ENABLE)
else()
    set(FSR2_LIBS "")
endif()

target_compile_definitions(frogRender PUBLIC FWOG_DEFAULT_CLIP_DEPTH_RANGE_ZERO_TO_ONE)

target_include_directories(frogRender
	PUBLIC
	${FSR2_SOURCE}
	vendor
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
)
add_dependencies(frogRender copy_shaders copy_models copy_textures)

if (MSVC)
    target_compile_definitions(frogRender PUBLIC STBI_MSC_SECURE_CRT)
endif()