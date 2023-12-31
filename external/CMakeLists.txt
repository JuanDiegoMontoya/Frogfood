# external content definitions
include(FetchContent)

option(GLFW_BUILD_TESTS "" OFF)
option(GLFW_BUILD_DOCS "" OFF)
option(GLFW_INSTALL "" OFF)
option(GLFW_BUILD_EXAMPLES "" OFF)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG        3.3.2
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm
    GIT_TAG        cc98465e3508535ba8c7f6208df934c156a018dc
)

FetchContent_Declare(
    fastgltf
    GIT_REPOSITORY https://github.com/spnda/fastgltf.git
    GIT_TAG        c15b151c204de0af45c97940a0750c3169407752
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG        dc3e531ff28450bff73fde0163b1d076b6bb5605
)

FetchContent_Declare(
    fwog
    GIT_REPOSITORY https://github.com/JuanDiegoMontoya/Fwog.git
    GIT_TAG        3bacfe1dc531283d923117eaf423586a6523a795
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
)

if(FROGRENDER_FSR2_ENABLE)
    option(GFX_API_DX12 "" OFF)
    option(GFX_API_VK "" OFF)
    option(GFX_API_GL "" ON)
    option(FFX_FSR2_API_DX12 "" OFF)
    option(FFX_FSR2_API_VK "" OFF)
    option(FFX_FSR2_API_GL "" ON)
    FetchContent_Declare(
        fsr2
        GIT_REPOSITORY https://github.com/JuanDiegoMontoya/FidelityFX-FSR2.git
        GIT_TAG        e6259912ed19d247ed431e2bc212085b01f74fc9
        GIT_SUBMODULES ""
    )
    FetchContent_MakeAvailable(fsr2)
endif()

FetchContent_Declare(
    tracy
    GIT_REPOSITORY  https://github.com/wolfpld/tracy.git
    GIT_TAG         v0.9.1
)

option(TRACY_ENABLE "Enable profiling" ON)
option(TRACY_ONLY_IPV4 "IPv4 only" ON)

FetchContent_MakeAvailable(glm fwog fastgltf ktx meshoptimizer tracy)

# Disable warnings for libktx
target_compile_options(ktx
    PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
    -Wno-everything>
    $<$<CXX_COMPILER_ID:MSVC>:
    -w>
)

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
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)

    target_include_directories(lib_imgui PUBLIC 
        ${imgui_SOURCE_DIR} 
        ${imgui_SOURCE_DIR}/backends
        ${glfw_SOURCE_DIR}/include)

    target_link_libraries(lib_imgui PRIVATE glfw)
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
endif()

set(IMPLOT_SOURCE ${implot_SOURCE_DIR} PARENT_SCOPE)

FetchContent_MakeAvailable(glfw)

target_compile_definitions(glm INTERFACE GLM_FORCE_SILENT_WARNINGS)