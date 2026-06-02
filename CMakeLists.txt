cmake_minimum_required(VERSION 3.20)
project(NullWare LANGUAGES C CXX ASM_MASM RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# For multi-config generators (Visual Studio), apply /MT for Release
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ═══════════════════════════════════════════════════════════════════════
# NullWare DLL (Internal Cheat)
# ═══════════════════════════════════════════════════════════════════════

# ImGui sources
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/imgui-master")
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
)

# MinHook sources
set(MINHOOK_DIR "${CMAKE_SOURCE_DIR}/minhook-master")
set(MINHOOK_SOURCES
    ${MINHOOK_DIR}/src/buffer.c
    ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c
    ${MINHOOK_DIR}/src/hde/hde64.c
)

add_library(NullWare SHARED
    src/dllmain.cpp
    src/hooks.cpp
    src/spoofcall.asm
    ${IMGUI_SOURCES}
    ${MINHOOK_SOURCES}
)

target_include_directories(NullWare PRIVATE
    src/
    ${IMGUI_DIR}/
    ${IMGUI_DIR}/backends/
    ${MINHOOK_DIR}/include/
)

target_link_libraries(NullWare PRIVATE
    d3d11
    dxgi
    dwmapi
    Psapi
)

target_compile_definitions(NullWare PRIVATE
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
)

# Force-include corecrt_math.h on C/CXX files only (not ASM)
set_source_files_properties(
    src/dllmain.cpp src/hooks.cpp
    ${IMGUI_SOURCES}
    PROPERTIES COMPILE_OPTIONS "/FI\"corecrt_math.h\""
)

set_target_properties(NullWare PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    OUTPUT_NAME "NullWare"
)

# ═══════════════════════════════════════════════════════════════════════
# VGC Emulator Tools
# ═══════════════════════════════════════════════════════════════════════

# VGC Service (fake vgc.exe)
add_executable(vgc
    tools/fake_vgc.cpp
    tools/vgc_version.rc
)
target_link_libraries(vgc PRIVATE advapi32)
set_target_properties(vgc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/emu"
    OUTPUT_NAME "vgc"
)

# Installer
add_executable(emu_installer
    tools/emu_installer.cpp
)
target_link_libraries(emu_installer PRIVATE advapi32 shell32)
set_target_properties(emu_installer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/emu"
    OUTPUT_NAME "emu_installer"
)
