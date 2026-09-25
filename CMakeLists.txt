cmake_minimum_required(VERSION 3.20)
project(FluidWallpaper CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# static CRT: the exe runs on machines without the VC++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Settings window = Dear ImGui (vendored, MIT, src/third_party/imgui) on its own D3D11 device.
set(IMGUI_DIR src/third_party/imgui)
set(IMGUI_SOURCES ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
set_source_files_properties(${IMGUI_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")   # third party: no warnings

add_executable(FluidWallpaper WIN32 src/main.cpp src/fluid.cpp src/ui/ui_model.cpp src/ui/ui_window.cpp src/ui/ui_cycle.cpp src/ui/ui_presets.cpp
    src/analyzer.cpp src/cycle.cpp src/journey.cpp src/app.rc ${IMGUI_SOURCES})
target_compile_definitions(FluidWallpaper PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(FluidWallpaper PRIVATE /utf-8)   # source files are UTF-8 (em dashes in UI strings)
target_include_directories(FluidWallpaper PRIVATE ${IMGUI_DIR})
target_link_libraries(FluidWallpaper PRIVATE d3d12 d3d11 dxgi d3dcompiler user32 shell32 ole32 windowscodecs dwmapi)

# OilWallpaper — standalone lava-lamp oil-blob proof of concept (src_oil/).
# Separate launcher; shares no sources with the FluidWallpaper target.
add_executable(OilWallpaper WIN32 src_oil/main.cpp src_oil/oil.cpp)
target_compile_definitions(OilWallpaper PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(OilWallpaper PRIVATE /utf-8)
target_link_libraries(OilWallpaper PRIVATE d3d12 dxgi d3dcompiler user32 shell32 ole32 windowscodecs)

# AcidWallpaper — standalone "liquid acid" oil-and-ink proof of concept (src_acid/).
# Separate launcher; shares no sources with the other targets.
add_executable(AcidWallpaper WIN32 src_acid/main.cpp src_acid/acid.cpp)
target_compile_definitions(AcidWallpaper PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(AcidWallpaper PRIVATE /utf-8)
target_link_libraries(AcidWallpaper PRIVATE d3d12 dxgi d3dcompiler user32 shell32 ole32)

# Ink3DWallpaper — standalone 3D "ink in water" simulation (src_ink3d/).
# Separate launcher; shares no sources with the other targets. Milestones
# M0-M2 are HEADLESS: --shot only, no window, no WorkerW, no tray.
# Build it in build3/ so it never fights build/ or build2/.
add_executable(Ink3DWallpaper WIN32 src_ink3d/main.cpp src_ink3d/ink3d.cpp src_ink3d/config.cpp)
target_compile_definitions(Ink3DWallpaper PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(Ink3DWallpaper PRIVATE /utf-8)
target_link_libraries(Ink3DWallpaper PRIVATE d3d12 dxgi d3dcompiler user32 shell32 ole32 windowscodecs)
