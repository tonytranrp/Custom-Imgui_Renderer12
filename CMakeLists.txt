cmake_minimum_required(VERSION 3.21)

project(ImGuiDX12App LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer optimized code generation for release-style configurations.s
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)

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

# Compile-time file embed helper (for shaders/resources).
set(B_EMBED_SILENCE_DEVMODE_WARNING ON CACHE BOOL "Silence battery::embed dev-mode warning for this project" FORCE)
CPMAddPackage("gh:batterycenter/embed@1.2.19")

# Fetch Corrosion
include(FetchContent)

# Resolve real rust toolchain binaries (not rustup proxy shims).
# Corrosion v0.5.0 can mis-parse rustup's "(active, default)" list output in some environments.
find_program(RUSTUP_EXECUTABLE rustup HINTS "$ENV{USERPROFILE}/.cargo/bin" "$ENV{HOME}/.cargo/bin")
if(NOT RUSTUP_EXECUTABLE)
    message(FATAL_ERROR
        "rustup was not found. Install rustup and run:\n"
        "  rustup toolchain install stable-x86_64-pc-windows-msvc")
endif()

execute_process(
    COMMAND "${RUSTUP_EXECUTABLE}" which rustc
    RESULT_VARIABLE _RUSTUP_WHICH_RUSTC_RESULT
    OUTPUT_VARIABLE _RUSTC_REAL
    ERROR_VARIABLE _RUSTUP_WHICH_RUSTC_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
    COMMAND "${RUSTUP_EXECUTABLE}" which cargo
    RESULT_VARIABLE _RUSTUP_WHICH_CARGO_RESULT
    OUTPUT_VARIABLE _CARGO_REAL
    ERROR_VARIABLE _RUSTUP_WHICH_CARGO_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT _RUSTUP_WHICH_RUSTC_RESULT EQUAL 0 OR NOT EXISTS "${_RUSTC_REAL}")
    message(FATAL_ERROR
        "Failed to resolve real rustc path via rustup.\n"
        "rustup stderr:\n${_RUSTUP_WHICH_RUSTC_ERROR}\n"
        "Resolved path: '${_RUSTC_REAL}'\n"
        "Run: rustup toolchain install stable-x86_64-pc-windows-msvc")
endif()
if(NOT _RUSTUP_WHICH_CARGO_RESULT EQUAL 0 OR NOT EXISTS "${_CARGO_REAL}")
    message(FATAL_ERROR
        "Failed to resolve real cargo path via rustup.\n"
        "rustup stderr:\n${_RUSTUP_WHICH_CARGO_ERROR}\n"
        "Resolved path: '${_CARGO_REAL}'\n"
        "Run: rustup toolchain install stable-x86_64-pc-windows-msvc")
endif()

set(Rust_COMPILER "${_RUSTC_REAL}" CACHE FILEPATH "Concrete rustc used by Corrosion" FORCE)
set(Rust_CARGO "${_CARGO_REAL}" CACHE FILEPATH "Concrete cargo used by Corrosion" FORCE)
set(Rust_TOOLCHAIN "stable-x86_64-pc-windows-msvc" CACHE STRING "Requested rustup toolchain" FORCE)
# Force OFF to bypass rustup toolchain-list parsing issues in this project.
set(Rust_RESOLVE_RUSTUP_TOOLCHAINS OFF CACHE BOOL "Disable rustup toolchain descent in Corrosion" FORCE)

execute_process(
    COMMAND "${Rust_COMPILER}" --version --verbose
    RESULT_VARIABLE _RUSTC_VERSION_RESULT
    OUTPUT_VARIABLE _RUSTC_VERSION_OUT
    ERROR_VARIABLE _RUSTC_VERSION_ERR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _RUSTC_VERSION_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Resolved rustc failed sanity check.\n"
        "Path: ${Rust_COMPILER}\n"
        "stderr:\n${_RUSTC_VERSION_ERR}\n"
        "Run: rustup toolchain install stable-x86_64-pc-windows-msvc")
endif()

execute_process(
    COMMAND "${Rust_CARGO}" --version --verbose
    RESULT_VARIABLE _CARGO_VERSION_RESULT
    OUTPUT_VARIABLE _CARGO_VERSION_OUT
    ERROR_VARIABLE _CARGO_VERSION_ERR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _CARGO_VERSION_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Resolved cargo failed sanity check.\n"
        "Path: ${Rust_CARGO}\n"
        "stderr:\n${_CARGO_VERSION_ERR}\n"
        "Run: rustup toolchain install stable-x86_64-pc-windows-msvc")
endif()

message(STATUS "Using Rust compiler: ${Rust_COMPILER}")
message(STATUS "Using Cargo: ${Rust_CARGO}")
message(STATUS "Rust toolchain descent via rustup is disabled for Corrosion in this project.")
# If Rust path/toolchain changes, clear stale CMake cache:
#   out/build/<config>/CMakeCache.txt
#   out/build/<config>/CMakeFiles/

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
    # Use one warning level source of truth to avoid CL D9025 (/W3 overridden by /W4).
    string(REPLACE "/W3" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    string(REPLACE "/W3" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
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

# Embed shipped shaders at compile time (no runtime asset copy required).
b_embed(ImGuiDX12App assets/shaders/ui_shader_shared.hlsli)
b_embed(ImGuiDX12App assets/shaders/ui_default_vs.hlsl)
b_embed(ImGuiDX12App assets/shaders/glow_default.hlsl)
b_embed(ImGuiDX12App assets/shaders/shadow_default.hlsl)

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
    $<$<CONFIG:Debug>:msvcrtd.lib>
)

target_include_directories(ImGuiDX12App PRIVATE src)

option(IMGUI_DX12APP_WARNINGS_AS_ERRORS "Treat project warnings as errors for ImGuiDX12App" ON)
if(MSVC)
    target_compile_options(ImGuiDX12App PRIVATE /W4 /permissive-)
    target_compile_options(ImGuiDX12App PRIVATE
        $<$<CONFIG:Release>:/O2 /Oi /Ot /Gy /Gw /GF /Zc:inline>
        $<$<CONFIG:RelWithDebInfo>:/O2 /Oi /Ot /Gy /Gw /GF /Zc:inline>
        $<$<CONFIG:MinSizeRel>:/O2 /Oi /Ot /Gy /Gw /GF /Zc:inline>
    )
    target_link_options(ImGuiDX12App PRIVATE
        $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /INCREMENTAL:NO>
        $<$<CONFIG:RelWithDebInfo>:/OPT:REF /OPT:ICF /INCREMENTAL:NO>
        $<$<CONFIG:MinSizeRel>:/OPT:REF /OPT:ICF /INCREMENTAL:NO>
    )
    if(IMGUI_DX12APP_WARNINGS_AS_ERRORS)
        target_compile_options(ImGuiDX12App PRIVATE /WX)
    endif()
else()
    target_compile_options(ImGuiDX12App PRIVATE -Wall -Wextra -Wpedantic)
    if(IMGUI_DX12APP_WARNINGS_AS_ERRORS)
        target_compile_options(ImGuiDX12App PRIVATE -Werror)
    endif()
endif()
