cmake_minimum_required(VERSION 3.16)
project(game-fps VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

configure_file(res/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)

add_executable(game-fps WIN32
  src/main.cpp
  src/etw.cpp
  src/hud.cpp
  src/settings.cpp
  src/autostart.cpp
  src/memory.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/app.rc
)

target_compile_definitions(game-fps PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  _WIN32_WINNT=0x0A00 WINVER=0x0A00
  APP_VERSION="${PROJECT_VERSION}"
)
target_include_directories(game-fps PRIVATE src)
target_link_libraries(game-fps PRIVATE advapi32 user32 gdi32 shell32 dwmapi ole32)

if(MSVC)
  # Static CRT (no VC++ redistributable needed), our own manifest is embedded via the .rc
  set_property(TARGET game-fps PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
  target_compile_options(game-fps PRIVATE /utf-8 /W4 /EHsc /GL /Gy /GS-)
  target_link_options(game-fps PRIVATE /MANIFEST:NO /LTCG /OPT:REF /OPT:ICF)
else()
  target_compile_options(game-fps PRIVATE -Wall -Wextra -Os -ffunction-sections -fdata-sections)
  target_link_options(game-fps PRIVATE -static -s -Wl,--gc-sections -mwindows)
endif()
