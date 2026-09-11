cmake_minimum_required(VERSION 3.20)
project(Matchaboy VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# A downloaded Windows build must not need a separate Visual C++ runtime.
# CMake maps this to the correct flags for both clang++ and clang-cl/MSVC.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()
set(MATCHA_AUTOPSY_DEFAULT OFF)
if(APPLE OR WIN32)
    set(MATCHA_AUTOPSY_DEFAULT ON)
endif()
option(MATCHA_BUILD_AUTOPSY "Build the native hardware dashboard" ${MATCHA_AUTOPSY_DEFAULT})
include(CTest)
find_package(Threads REQUIRED)

add_library(matcha_options INTERFACE)
target_include_directories(matcha_options INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" OR
   CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(matcha_options INTERFACE /W4 /WX /permissive- /EHsc
        $<$<CONFIG:Release>:/O2>)
else()
    target_compile_options(matcha_options INTERFACE -Wall -Wextra -Werror -Wpedantic
        $<$<CONFIG:Release>:-O3>)
endif()
if(WIN32)
    target_compile_definitions(matcha_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
endif()

set(MATCHA_CORE_SOURCES
    src/cpu.cpp src/mmu.cpp src/timer.cpp src/mbc.cpp
    src/ppu.cpp src/oam.cpp src/apu.cpp src/snapshot.cpp)
add_library(matcha_core STATIC ${MATCHA_CORE_SOURCES})
target_link_libraries(matcha_core PUBLIC matcha_options)
set_target_properties(matcha_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

function(matcha_executable target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE matcha_options)
    # Snapshot tests and the diagnostic runner hold complete machine states on
    # the stack. Match the usual Unix stack budget rather than Windows' 1 MiB.
    if(WIN32)
        if(MINGW)
            target_link_options(${target} PRIVATE "LINKER:--stack,16777216")
        else()
            target_link_options(${target} PRIVATE "LINKER:/STACK:16777216")
        endif()
    endif()
endfunction()

matcha_executable(dmg src/harness.cpp)
target_link_libraries(dmg PRIVATE matcha_core)

add_library(matcha SHARED src/gym.cpp)
target_link_libraries(matcha PRIVATE matcha_core Threads::Threads)
target_compile_definitions(matcha PRIVATE MATCHA_BUILD_SHARED INTERFACE MATCHA_USE_SHARED)
# Keep a single predictable basename for the ctypes adapter on all three OSes.
set_target_properties(matcha PROPERTIES PREFIX "lib" OUTPUT_NAME "matcha")

# Native benchmark/tests use the C++ API directly and do not depend on a DLL's
# C++ ABI or export implementation details from the C-only shared library.
add_library(matcha_gym_engine STATIC src/gym.cpp)
target_link_libraries(matcha_gym_engine PUBLIC matcha_core Threads::Threads)
matcha_executable(gym_benchmark src/gym_benchmark.cpp)
target_link_libraries(gym_benchmark PRIVATE matcha_gym_engine)
if(WIN32)
    target_link_libraries(gym_benchmark PRIVATE psapi)
endif()

add_library(matcha_transport STATIC src/netplay.cpp)
target_link_libraries(matcha_transport PUBLIC matcha_options Threads::Threads)
if(WIN32)
    target_link_libraries(matcha_transport PUBLIC ws2_32)
endif()
matcha_executable(netplay src/netplay_main.cpp)
target_link_libraries(netplay PRIVATE matcha_core matcha_transport)

# Telemetry changes class layout, so every translation unit in these targets
# uses a separate core compiled with the same ENABLE_AUTOPSY definition.
if(BUILD_TESTING OR ((APPLE OR WIN32) AND MATCHA_BUILD_AUTOPSY))
    add_library(matcha_autopsy_core STATIC ${MATCHA_CORE_SOURCES} src/autopsy.cpp)
    target_link_libraries(matcha_autopsy_core PUBLIC matcha_options Threads::Threads)
    target_compile_definitions(matcha_autopsy_core PUBLIC ENABLE_AUTOPSY)
endif()

if(BUILD_TESTING)
    foreach(test IN ITEMS unit_tests oam_tests ppu_edge_tests apu_tests snapshot_tests)
        matcha_executable(${test} tests/${test}.cpp)
        target_link_libraries(${test} PRIVATE matcha_core)
        add_test(NAME ${test} COMMAND ${test})
    endforeach()
    matcha_executable(gym_tests tests/gym_tests.cpp)
    target_link_libraries(gym_tests PRIVATE matcha_gym_engine)
    add_test(NAME gym_tests COMMAND gym_tests)
    matcha_executable(netplay_tests tests/netplay_tests.cpp)
    target_link_libraries(netplay_tests PRIVATE matcha_transport)
    add_test(NAME netplay_tests COMMAND netplay_tests)
    matcha_executable(autopsy_tests tests/autopsy_tests.cpp)
    target_link_libraries(autopsy_tests PRIVATE matcha_autopsy_core)
    add_test(NAME autopsy_tests COMMAND autopsy_tests)
    set_tests_properties(unit_tests oam_tests ppu_edge_tests apu_tests snapshot_tests
        gym_tests netplay_tests autopsy_tests PROPERTIES TIMEOUT 180)
endif()

if(APPLE AND MATCHA_BUILD_AUTOPSY)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
    matcha_executable(autopsy src/autopsy_main.mm)
    target_link_libraries(autopsy PRIVATE matcha_autopsy_core
        "-framework AppKit" "-framework OpenGL" "-framework CoreGraphics"
        "-framework CoreText" "-framework ImageIO")
    # Reuse the native application's checked-in metadata, with the executable
    # name CMake places inside this bundle rather than Makefile's plain binary.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/tools/AutopsyInfo.plist" MATCHA_PLIST)
    string(REPLACE "<string>autopsy</string>" "<string>MatchaAutopsy</string>"
        MATCHA_PLIST "${MATCHA_PLIST}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/AutopsyInfo.plist" "${MATCHA_PLIST}")
    set_target_properties(autopsy PROPERTIES MACOSX_BUNDLE TRUE
        OUTPUT_NAME MatchaAutopsy
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/AutopsyInfo.plist")
    install(TARGETS autopsy BUNDLE DESTINATION .)
endif()

if(WIN32 AND MATCHA_BUILD_AUTOPSY)
    include(cmake/GbaCore.cmake)
    enable_language(RC)
    configure_file(assets/arcade.rc.in "${CMAKE_CURRENT_BINARY_DIR}/arcade.rc" @ONLY)
    file(GLOB MATCHA_ARCADE_ROMS "${PROJECT_SOURCE_DIR}/games/gb/roms/*.gb" "${PROJECT_SOURCE_DIR}/games/gba/roms/*.gba")
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/arcade.rc" PROPERTIES OBJECT_DEPENDS "${MATCHA_ARCADE_ROMS}")
    matcha_executable(autopsy src/autopsy_windows.cpp src/windows_audio.cpp src/arcade_library.cpp
        assets/matchaboy.rc "${CMAKE_CURRENT_BINARY_DIR}/arcade.rc")
    target_compile_definitions(autopsy PRIVATE UNICODE _UNICODE)
    target_link_libraries(autopsy PRIVATE matcha_autopsy_core matcha_gba gdiplus opengl32 shell32 ole32 user32 gdi32 comdlg32 winmm dxgi dxguid advapi32 avrt)
    set_target_properties(autopsy PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME Matchaboy)
    install(TARGETS autopsy RUNTIME DESTINATION .)
    if(BUILD_TESTING)
        matcha_executable(gba_arcade_probe tests/gba_arcade_probe.cpp)
        target_link_libraries(gba_arcade_probe PRIVATE matcha_gba)
    endif()
endif()

install(TARGETS dmg netplay gym_benchmark matcha
    RUNTIME DESTINATION . LIBRARY DESTINATION . ARCHIVE DESTINATION lib)
install(FILES LICENSE THIRD_PARTY.md matcha_gym.py README.md DOWNLOADS.md PLATFORM.md PLATFORM_VERIFICATION.md
    VERIFICATION.md MOONEYE.md MEALYBUG.md ARTIFACTS.md DESTINATION .)
install(FILES include/dmg/gym.h DESTINATION include/dmg)
