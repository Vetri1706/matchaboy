cmake_minimum_required(VERSION 3.20)
# Do not silently require the build host's macOS release in downloaded apps.
# Explicit toolchain/command-line/environment deployment targets still win.
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET
   AND NOT DEFINED CMAKE_TOOLCHAIN_FILE
   AND "$ENV{CMAKE_TOOLCHAIN_FILE}" STREQUAL ""
   AND "$ENV{MACOSX_DEPLOYMENT_TARGET}" STREQUAL "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum supported macOS version")
endif()
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
if(APPLE OR WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
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
    src/ppu.cpp src/oam.cpp src/apu.cpp src/snapshot.cpp src/linked_pair.cpp)
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
add_library(matcha_friend_transport STATIC src/friend_transport.cpp)
target_link_libraries(matcha_friend_transport PUBLIC matcha_options Threads::Threads)
if(WIN32)
    target_link_libraries(matcha_friend_transport PUBLIC ws2_32)
endif()
matcha_executable(netplay src/netplay_main.cpp)
target_link_libraries(netplay PRIVATE matcha_core matcha_transport)

# Telemetry changes class layout, so every translation unit in these targets
# uses a separate core compiled with the same ENABLE_AUTOPSY definition.
if(BUILD_TESTING OR MATCHA_BUILD_AUTOPSY)
    add_library(matcha_autopsy_core STATIC ${MATCHA_CORE_SOURCES} src/autopsy.cpp)
    target_link_libraries(matcha_autopsy_core PUBLIC matcha_options Threads::Threads)
    target_compile_definitions(matcha_autopsy_core PUBLIC ENABLE_AUTOPSY)
endif()

if(BUILD_TESTING)
    matcha_executable(keyboard_mapping_tests tests/keyboard_mapping_tests.cpp)
    add_test(NAME keyboard_mapping_tests COMMAND keyboard_mapping_tests)
    if(APPLE)
        matcha_executable(macos_keyboard_tests tests/macos_keyboard_tests.cpp)
        add_test(NAME macos_keyboard_tests COMMAND macos_keyboard_tests)
        enable_language(OBJCXX)
        matcha_executable(macos_keyboard_presenter_tests
            tests/macos_keyboard_presenter_tests.mm src/macos_keyboard_settings.mm)
        set_target_properties(macos_keyboard_presenter_tests PROPERTIES
            OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
        target_link_libraries(macos_keyboard_presenter_tests PRIVATE "-framework AppKit")
        option(MATCHA_RUN_NATIVE_GUI_TESTS
            "Register process-local AppKit tests (requires a logged-in WindowServer session)" OFF)
        if(MATCHA_RUN_NATIVE_GUI_TESTS)
            add_test(NAME macos_keyboard_presenter_tests COMMAND macos_keyboard_presenter_tests)
            set_tests_properties(macos_keyboard_presenter_tests PROPERTIES LABELS gui TIMEOUT 60)
        endif()
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        matcha_executable(linux_keyboard_tests tests/linux_keyboard_tests.cpp)
        add_test(NAME linux_keyboard_tests COMMAND linux_keyboard_tests)
    endif()
    if(WIN32)
        matcha_executable(windows_keyboard_presenter_tests
            tests/windows_keyboard_presenter_tests.cpp src/windows_keyboard_settings.cpp)
        target_link_libraries(windows_keyboard_presenter_tests PRIVATE user32 gdi32 advapi32)
        add_test(NAME windows_keyboard_presenter_tests COMMAND windows_keyboard_presenter_tests)
        set_tests_properties(windows_keyboard_presenter_tests PROPERTIES TIMEOUT 60 LABELS gui)
    endif()
    foreach(test IN ITEMS unit_tests oam_tests ppu_edge_tests apu_tests snapshot_tests linked_pair_tests)
        matcha_executable(${test} tests/${test}.cpp)
        target_link_libraries(${test} PRIVATE matcha_core)
        add_test(NAME ${test} COMMAND ${test})
    endforeach()
    matcha_executable(friend_transport_tests tests/friend_transport_tests.cpp)
    target_link_libraries(friend_transport_tests PRIVATE matcha_friend_transport)
    add_test(NAME friend_transport_tests COMMAND friend_transport_tests)
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

if((APPLE OR WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux") AND MATCHA_BUILD_AUTOPSY)
    include(cmake/GbaCore.cmake)
    add_library(matcha_friend_session STATIC src/friend_session.cpp src/gameboy_save.cpp)
    target_link_libraries(matcha_friend_session PUBLIC matcha_autopsy_core matcha_gba matcha_friend_transport)
    if(BUILD_TESTING)
        matcha_executable(gba_link_tests tests/gba_link_tests.cpp)
        target_link_libraries(gba_link_tests PRIVATE matcha_gba)
        add_test(NAME gba_link_tests COMMAND gba_link_tests)
        matcha_executable(gba_friend_probe tests/gba_friend_probe.cpp)
        target_link_libraries(gba_friend_probe PRIVATE matcha_gba)
        matcha_executable(friend_session_tests tests/friend_session_tests.cpp)
        target_link_libraries(friend_session_tests PRIVATE matcha_friend_session)
        add_test(NAME friend_session_tests COMMAND friend_session_tests)
        matcha_executable(friend_session_edge_tests tests/friend_session_edge_tests.cpp)
        target_link_libraries(friend_session_edge_tests PRIVATE matcha_friend_session)
        add_test(NAME friend_session_edge_tests COMMAND friend_session_edge_tests)
        matcha_executable(fifa_network_probe tests/fifa_network_probe.cpp)
        target_link_libraries(fifa_network_probe PRIVATE matcha_friend_session)
        matcha_executable(gba_arcade_probe tests/gba_arcade_probe.cpp)
        target_link_libraries(gba_arcade_probe PRIVATE matcha_gba)
        if(APPLE)
            # Explicit device probe, deliberately excluded from unattended
            # CTest: it requires a usable macOS speaker/output device.
            matcha_executable(macos_audio_probe tests/macos_audio_probe.cpp src/macos_audio.cpp)
            target_link_libraries(macos_audio_probe PRIVATE matcha_core matcha_gba "-framework AudioToolbox")
        endif()
    endif()
endif()

include(cmake/Homebrew.cmake)

if(APPLE AND MATCHA_BUILD_AUTOPSY)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
    set(MATCHA_MAC_ARCADE_ROMS ${MATCHA_ARCADE_ROMS})
    set_source_files_properties(${MATCHA_MAC_ARCADE_ROMS} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources/Arcade HEADER_FILE_ONLY TRUE)
    set_source_files_properties(assets/matchaboy.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources HEADER_FILE_ONLY TRUE)
    set_source_files_properties(games/homebrew/CREDITS.txt PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources HEADER_FILE_ONLY TRUE)
    matcha_executable(autopsy src/autopsy_main.mm src/arcade_macos.mm src/macos_audio.cpp src/macos_keyboard_settings.mm
        ${MATCHA_MAC_ARCADE_ROMS} assets/matchaboy.icns games/homebrew/CREDITS.txt)
    target_link_libraries(autopsy PRIVATE matcha_autopsy_core matcha_gba matcha_friend_session
        "-framework AppKit" "-framework OpenGL" "-framework CoreGraphics"
        "-framework CoreText" "-framework ImageIO" "-framework AudioToolbox")
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
    enable_language(RC)
    configure_file(assets/arcade.rc.in "${CMAKE_CURRENT_BINARY_DIR}/arcade.rc" @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/arcade.rc" PROPERTIES
        OBJECT_DEPENDS "${MATCHA_ARCADE_ROMS};${PROJECT_SOURCE_DIR}/games/homebrew/CREDITS.txt")
    set_source_files_properties(assets/matchaboy.rc PROPERTIES
        OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/assets/matchaboy.ico")
    matcha_executable(autopsy src/autopsy_windows.cpp src/windows_audio.cpp src/arcade_library.cpp src/windows_keyboard_settings.cpp
        assets/matchaboy.rc "${CMAKE_CURRENT_BINARY_DIR}/arcade.rc")
    target_compile_definitions(autopsy PRIVATE UNICODE _UNICODE)
    target_link_libraries(autopsy PRIVATE matcha_autopsy_core matcha_gba matcha_friend_session gdiplus opengl32 shell32 ole32 user32 gdi32 comdlg32 winmm dxgi dxguid advapi32 avrt)
    set_target_properties(autopsy PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME Matchaboy)
    if(MINGW)
        target_link_options(autopsy PRIVATE -municode)
    endif()
    install(TARGETS autopsy RUNTIME DESTINATION .)
endif()


if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND MATCHA_BUILD_AUTOPSY)
    find_package(X11 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XFT REQUIRED IMPORTED_TARGET xft)
    find_package(ALSA QUIET)
    matcha_executable(autopsy src/player_linux.cpp src/arcade_linux.cpp src/linux_audio.cpp)
    target_link_libraries(autopsy PRIVATE matcha_autopsy_core matcha_gba matcha_friend_session X11::X11 PkgConfig::XFT)
    if(ALSA_FOUND)
        target_link_libraries(autopsy PRIVATE ALSA::ALSA)
        target_compile_definitions(autopsy PRIVATE MATCHA_LINUX_ALSA)
    endif()
    set_target_properties(autopsy PROPERTIES OUTPUT_NAME Matchaboy)
    set_property(TARGET autopsy APPEND PROPERTY LINK_DEPENDS
        ${MATCHA_ARCADE_ROMS} "${PROJECT_SOURCE_DIR}/games/homebrew/CREDITS.txt")
    add_custom_command(TARGET autopsy POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "$<TARGET_FILE_DIR:autopsy>/assets/roms"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/games/homebrew/roms" "$<TARGET_FILE_DIR:autopsy>/assets/roms"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:autopsy>/assets/licenses"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PROJECT_SOURCE_DIR}/games/homebrew/CREDITS.txt" "$<TARGET_FILE_DIR:autopsy>/assets/licenses/CREDITS.txt")
    install(TARGETS autopsy RUNTIME DESTINATION .)
    install(FILES ${MATCHA_ARCADE_ROMS} DESTINATION assets/roms)
    install(FILES games/homebrew/CREDITS.txt DESTINATION assets/licenses)
endif()

install(TARGETS dmg netplay gym_benchmark matcha
    RUNTIME DESTINATION . LIBRARY DESTINATION . ARCHIVE DESTINATION lib)
install(FILES LICENSE THIRD_PARTY.md matcha_gym.py README.md DOWNLOADS.md MACOS.md WINDOWS.md LINUX.md NETPLAY.md HOMEBREW.md DESIGN.md PLATFORM.md PLATFORM_VERIFICATION.md
    VERIFICATION.md MOONEYE.md MEALYBUG.md ARTIFACTS.md DESTINATION .)
install(FILES include/dmg/gym.h DESTINATION include/dmg)
