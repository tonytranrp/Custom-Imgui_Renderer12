add_library(imgui_testhook STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui_testhook PUBLIC
    ${imgui_SOURCE_DIR}
    "${CMAKE_SOURCE_DIR}/src"
)
target_compile_definitions(imgui_testhook PRIVATE IMGUI_DX12_TESTHOOK_NON_FATAL_ASSERTS=1)

add_library(imgui_backends_testhook OBJECT
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
)
target_include_directories(imgui_backends_testhook PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(imgui_backends_testhook PUBLIC imgui_testhook)
target_compile_definitions(imgui_backends_testhook PRIVATE IMGUI_DX12_TESTHOOK_NON_FATAL_ASSERTS=1)

if(MSVC)
    set(_imdx12_assert_override "/FI${CMAKE_SOURCE_DIR}/src/Render/ImguiAssertOverride.hpp")
    target_compile_options(imgui_testhook PRIVATE ${_imdx12_assert_override})
    target_compile_options(imgui_backends_testhook PRIVATE ${_imdx12_assert_override})
endif()

add_library(ImGuiDX12TestHook SHARED
    ${IMGUI_DX12APP_SHARED_RUNTIME_SOURCES}
    ${IMGUI_DX12APP_TESTHOOK_ONLY_SOURCES}
)

target_include_directories(ImGuiDX12TestHook PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(ImGuiDX12TestHook PRIVATE IMGUI_DX12_TESTHOOK_NON_FATAL_ASSERTS=1)

target_link_libraries(ImGuiDX12TestHook PRIVATE
    imgui_testhook
    imgui_backends_testhook
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
    kiero::kiero-minhook
)

imdx12_apply_warning_profile(ImGuiDX12TestHook)

set_target_properties(ImGuiDX12TestHook PROPERTIES
    OUTPUT_NAME "ImGuiDX12TestHook"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Test"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Test"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Test"
)

foreach(config_type Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config_type}" config_upper)
    set_target_properties(ImGuiDX12TestHook PROPERTIES
        "ARCHIVE_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/Test"
        "LIBRARY_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/Test"
        "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/Test")
endforeach()
