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
# You need to provide the MemProcFS SDK manually.
# Download:  https://github.com/ufrisk/MemProcFS/releases
#   → MemProcFS_files_and_binaries-win_x64-latest.zip
#
# 1. Build-time files (compile + link):
#    Copy these into ./sdk/memprocfs/:
#       vmmdll.h        (header)
#       vmm.lib         (import lib for vmm.dll — older releases shipped
#                        this as vmmdll.lib; modern ones ship vmm.lib)
#       leechcore.h     (optional; only if you call leechcore directly)
#       leechcore.lib   (optional; same)
#
# 2. Runtime files (copied next to the exe at build time):
#    Set MEMPROCFS_RUNTIME_DIR to the extracted MemProcFS folder; the
#    POST_BUILD step below copies vmm.dll, leechcore.dll, info.db, and
#    related deps into $<TARGET_FILE_DIR>. Skipped (with a warning) if
#    you don't set the variable — you can also copy by hand.
#       cmake .. -DMEMPROCFS_RUNTIME_DIR="C:/path/to/MemProcFS_files..."
set(MEMPROCFS_SDK "${CMAKE_SOURCE_DIR}/sdk/memprocfs")
set(MEMPROCFS_RUNTIME_DIR "" CACHE PATH
    "Path to the extracted MemProcFS_files_and_binaries folder (for runtime DLLs)")

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

# Pick whichever import-lib name is actually present in sdk/memprocfs/.
# Modern MemProcFS releases ship vmm.lib; very old ones shipped vmmdll.lib.
if(EXISTS "${MEMPROCFS_SDK}/vmm.lib")
  set(MEMPROCFS_IMPORT_LIB "${MEMPROCFS_SDK}/vmm.lib")
elseif(EXISTS "${MEMPROCFS_SDK}/vmmdll.lib")
  set(MEMPROCFS_IMPORT_LIB "${MEMPROCFS_SDK}/vmmdll.lib")
else()
  message(FATAL_ERROR
    "MemProcFS import lib not found. Drop vmm.lib (or vmmdll.lib) into "
    "${MEMPROCFS_SDK}. See top-of-file comment for download link.")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
  nlohmann_json::nlohmann_json
  httplib::httplib
  d3d11
  dxgi
  d3dcompiler
  ws2_32
  ${MEMPROCFS_IMPORT_LIB}
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

# ── Copy MemProcFS runtime files next to the exe ──────────────
# These DLLs and info.db must be in the same directory as bf6_dma.exe
# at runtime. If you set -DMEMPROCFS_RUNTIME_DIR, CMake handles it for
# you; otherwise copy them in manually.
if(MEMPROCFS_RUNTIME_DIR AND EXISTS "${MEMPROCFS_RUNTIME_DIR}/vmm.dll")
  set(MEMPROCFS_RUNTIME_FILES
    vmm.dll leechcore.dll leechcore_driver.dll
    FTD3XX.dll FTD3XXWU.dll
    dbghelp.dll symsrv.dll vcruntime140.dll
    vmmyara.dll tinylz4.dll
    info.db
  )
  foreach(F ${MEMPROCFS_RUNTIME_FILES})
    if(EXISTS "${MEMPROCFS_RUNTIME_DIR}/${F}")
      add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${MEMPROCFS_RUNTIME_DIR}/${F}"
          "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${F}"
      )
    endif()
  endforeach()
  message(STATUS "MemProcFS runtime: ${MEMPROCFS_RUNTIME_DIR} (will be copied)")
else()
  message(WARNING
    "MEMPROCFS_RUNTIME_DIR not set — you'll need to copy vmm.dll, "
    "leechcore.dll, info.db, etc. next to bf6_dma.exe by hand.")
endif()

message(STATUS "Build type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "MemProcFS SDK: ${MEMPROCFS_SDK}")
