# external content definitions
include(FetchContent)

option(GLFW_BUILD_TESTS "" OFF)
option(GLFW_BUILD_DOCS "" OFF)
option(GLFW_INSTALL "" OFF)
option(GLFW_BUILD_EXAMPLES "" OFF)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG        3.4
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm
    GIT_TAG        1.0.1
)

FetchContent_Declare(
    fastgltf
    GIT_REPOSITORY https://github.com/spnda/fastgltf.git
    GIT_TAG        c15b151c204de0af45c97940a0750c3169407752
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG        fdc084f532189fda8474079f79e74fa5e3541c9f
)

FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG        v0.19
)

FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG        v0.16
)

option(KTX_FEATURE_TESTS "" OFF)
option(KTX_FEATURE_VULKAN "" OFF)
option(KTX_FEATURE_GL_UPLOAD "" OFF)
option(KTX_FEATURE_VK_UPLOAD "" OFF)
option(KTX_FEATURE_WRITE "" OFF)
option(KTX_FEATURE_TOOLS "" OFF)
option(KTX_FEATURE_STATIC_LIBRARY "" ON)
FetchContent_Declare(
    ktx
    GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software.git
    GIT_TAG        v4.1.0
    SYSTEM
)

FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        1.3.270
)

FetchContent_Declare(
    vkbootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap
    GIT_TAG        3ad0388f1b68dbf99746a2fab8c3115c8fa887bb
)

# Since we are loading Vulkan with Volk, do not load it from a static library or DLL
set(VMA_STATIC_VULKAN_FUNCTIONS OFF CACHE BOOL "" FORCE)
set(VMA_DYNAMIC_VULKAN_FUNCTIONS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    GIT_TAG        v3.1.0
    SYSTEM
)

option(ENABLE_OPT "" OFF)
FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG        5398d55e33dff7d26fecdd2c35808add986c558c
    #SYSTEM
)

if(FROGRENDER_FSR2_ENABLE)
    option(GFX_API_DX12 "" OFF)
    option(GFX_API_VK "" OFF)
    option(GFX_API_GL "" OFF)
    option(FFX_FSR2_API_DX12 "" OFF)
    option(FFX_FSR2_API_VK "" ON)
    option(FFX_FSR2_API_GL "" OFF)
    FetchContent_Declare(
        fsr2
        GIT_REPOSITORY https://github.com/JuanDiegoMontoya/FidelityFX-FSR2.git
        GIT_TAG        de42d95ffed4be263629950a6b04ba6e71215931
        GIT_SUBMODULES ""
    )
    FetchContent_MakeAvailable(fsr2)
endif()

FetchContent_Declare(
    tracy
    GIT_REPOSITORY  https://github.com/wolfpld/tracy.git
    GIT_TAG         v0.11.0
)

option(TRACY_ENABLE "Enable profiling" ON)
option(TRACY_ONLY_IPV4 "IPv4 only" ON)

FetchContent_MakeAvailable(glm fastgltf ktx meshoptimizer tracy volk vkbootstrap vma glslang)

set(FSR2_SOURCE ${fsr2_SOURCE_DIR} PARENT_SCOPE)

FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)

    add_library(lib_imgui
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)

    target_include_directories(lib_imgui PUBLIC 
        ${imgui_SOURCE_DIR} 
        ${imgui_SOURCE_DIR}/backends
        ${Vulkan_INCLUDE_DIRS}
        ${glfw_SOURCE_DIR}/include)

    target_compile_definitions(lib_imgui PRIVATE VK_NO_PROTOTYPES ImTextureID=ImU64)
    target_link_libraries(lib_imgui PRIVATE glfw volk::volk)
endif()

FetchContent_GetProperties(implot)
if(NOT implot_POPULATED)
    FetchContent_Populate(implot)

    add_library(lib_implot
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp
        ${implot_SOURCE_DIR}/implot_demo.cpp
    )

    target_include_directories(lib_implot PUBLIC 
        ${implot_SOURCE_DIR}
        ${imgui_SOURCE_DIR} # ImPlot must see ImGui headers
    )
    
    target_compile_definitions(lib_implot PRIVATE ImTextureID=ImU64)
endif()

set(IMPLOT_SOURCE ${implot_SOURCE_DIR} PARENT_SCOPE)

FetchContent_MakeAvailable(glfw)

target_compile_definitions(glm INTERFACE GLM_FORCE_SILENT_WARNINGS)