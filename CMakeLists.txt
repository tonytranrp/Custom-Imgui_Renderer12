cmake_minimum_required(VERSION 3.14)

project(ImGuiDX12App LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CPM.cmake - CMake Package Manager
set(CPM_DOWNLOAD_VERSION 0.40.2)
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")

if(NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
    message(STATUS "Downloading CPM.cmake...")
    file(DOWNLOAD
         https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
         ${CPM_DOWNLOAD_LOCATION}
    )
endif()

include(${CPM_DOWNLOAD_LOCATION})

# Fetch ImGui
CPMAddPackage(
    NAME imgui
    GITHUB_REPOSITORY ocornut/imgui
    GIT_TAG v1.91.8
)

# Fetch EnTT
CPMAddPackage(
    NAME EnTT
    GITHUB_REPOSITORY skypjack/entt
    GIT_TAG v3.14.0
)

# Fetch Corrosion
include(FetchContent)

# Explicitly find Rust to help Corrosion (MUST be before FetchContent_MakeAvailable)
if(NOT Rust_COMPILER)
    find_program(Rust_COMPILER rustc HINTS "$ENV{USERPROFILE}/.cargo/bin")
    if(Rust_COMPILER)
        message(STATUS "Found Rust Compiler: ${Rust_COMPILER}")
    else()
        message(WARNING "Rust Compiler not found in standard location")
    endif()
endif()

# Explicitly find Cargo
if(NOT Rust_CARGO)
    find_program(Rust_CARGO cargo HINTS "$ENV{USERPROFILE}/.cargo/bin")
    if(Rust_CARGO)
        message(STATUS "Found Cargo: ${Rust_CARGO}")
    else()
        message(WARNING "Cargo not found in standard location")
    endif()
endif()

FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG v0.5.0
)
FetchContent_MakeAvailable(Corrosion)

# Import Rust Crate
corrosion_import_crate(MANIFEST_PATH src/RustComponents/Cargo.toml)

# Ensure proper Runtime Library selection for MSVC
if(MSVC)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()


if(imgui_ADDED)
    # The official ImGui repo does NOT provide a CMakeLists.txt that exports an 'imgui' target.
    # We must define it ourselves.
    if(NOT TARGET imgui)
        add_library(imgui STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_demo.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        )
        target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
    endif()

    # Create a library for the backends
    add_library(imgui_backends OBJECT
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    )
    target_include_directories(imgui_backends PUBLIC 
        ${imgui_SOURCE_DIR} 
        ${imgui_SOURCE_DIR}/backends
    )
    
    target_link_libraries(imgui_backends PUBLIC imgui)
endif()

add_executable(ImGuiDX12App
    src/main.cpp
    src/MainRendering.cpp
    src/MainRendering.hpp
    src/Dx12Init/Dx12Init.cpp
    src/Dx12Init/Dx12Init.hpp
    src/Dx12Init/Dx12Helper.cpp
    src/Render/ImguiRender.cpp
    src/Render/ImguiRender.hpp
    src/Render/RenderUtils/UIRenderer.cpp
    src/Render/RenderUtils/UIRenderer.hpp
    src/Render/RenderUtils/ShaderSystem.cpp
    src/Render/RenderUtils/ShaderSystem.hpp
    src/Render/RenderUtils/UIComponents.hpp
)

target_link_libraries(ImGuiDX12App PRIVATE 
    imgui 
    imgui_backends
    EnTT::EnTT
    rust_components
    d3d12.lib 
    dxgi.lib 
    d3dcompiler.lib
    dxguid.lib
    ws2_32.lib
    userenv.lib
    bcrypt.lib
    msvcrtd.lib
)

target_include_directories(ImGuiDX12App PRIVATE src)

add_custom_command(TARGET ImGuiDX12App POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ImGuiDX12App>/assets/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/shaders/ui_shader_shared.hlsli"
        "$<TARGET_FILE_DIR:ImGuiDX12App>/assets/shaders/ui_shader_shared.hlsli"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/shaders/ui_default_vs.hlsl"
        "$<TARGET_FILE_DIR:ImGuiDX12App>/assets/shaders/ui_default_vs.hlsl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/shaders/glow_default.hlsl"
        "$<TARGET_FILE_DIR:ImGuiDX12App>/assets/shaders/glow_default.hlsl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/shaders/shadow_default.hlsl"
        "$<TARGET_FILE_DIR:ImGuiDX12App>/assets/shaders/shadow_default.hlsl"
)
