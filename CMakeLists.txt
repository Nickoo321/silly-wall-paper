cmake_minimum_required(VERSION 3.20)
project(FluidWallpaper CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# static CRT: the exe runs on machines without the VC++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(FluidWallpaper WIN32 src/main.cpp src/fluid.cpp src/settings.cpp src/analyzer.cpp src/scenes.cpp src/moods.cpp src/journey.cpp src/app.rc)
target_compile_definitions(FluidWallpaper PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(FluidWallpaper PRIVATE /utf-8)   # source files are UTF-8 (em dashes in UI strings)
target_link_libraries(FluidWallpaper PRIVATE d3d12 dxgi d3dcompiler user32 shell32 ole32 windowscodecs)

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
