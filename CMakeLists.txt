cmake_minimum_required(VERSION 3.20)
project(bf6_dma CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Release by default ────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ── Third-party deps (fetch if not present) ───────────────────
include(FetchContent)

# nlohmann/json (header-only)
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# cpp-httplib (header-only)
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.15.3
)
FetchContent_MakeAvailable(httplib)

# ── PCILeech / MemProcFS ──────────────────────────────────────
# You need to provide the MemProcFS SDK manually:
#   Place vmmdll.h + vmmdll.lib in ./sdk/memprocfs/
# Download: https://github.com/ufrisk/MemProcFS/releases
set(MEMPROCFS_SDK "${CMAKE_SOURCE_DIR}/sdk/memprocfs")

# ── Sources ───────────────────────────────────────────────────
set(SOURCES
  src/main.cpp
  src/dma/dma_handler.cpp
  src/game/entity.cpp
  src/game/offset_resolver.cpp
  src/game/offset_discovery.cpp
  src/features/aimbot.cpp
  src/features/antirecoil.cpp
  src/features/esp.cpp
  src/input/kmbox.cpp
  src/input/serial_device.cpp
  src/input/input_monitor.cpp
  src/input/hotkey_manager.cpp
  src/config/config.cpp
  src/web/web_server.cpp
  src/diagnostics.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${MEMPROCFS_SDK}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  nlohmann_json::nlohmann_json
  httplib::httplib
  d3d11
  dxgi
  d3dcompiler
  ws2_32
  ${MEMPROCFS_SDK}/vmmdll.lib
)

# ── Compiler flags ────────────────────────────────────────────
if(MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE
    /O2 /W3 /MP
    /D_CRT_SECURE_NO_WARNINGS
  )
else()
  target_compile_options(${PROJECT_NAME} PRIVATE -O2 -Wall)
endif()

# ── Copy web_menu next to exe ─────────────────────────────────
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/web_menu"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/web_menu"
  COMMENT "Copying web_menu to output dir"
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/configs"
)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "MemProcFS SDK: ${MEMPROCFS_SDK}")
