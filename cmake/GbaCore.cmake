# Keep third-party settings local to this directory/function; never change the
# original DMG core's compiler flags or dependencies.
function(matcha_add_gba)
    set(LIBMGBA_ONLY ON)
    set(M_CORE_GBA ON)
    set(M_CORE_GB OFF)
    foreach(feature USE_DEBUGGERS USE_GDB_STUB USE_EDITLINE USE_FFMPEG USE_ZLIB
            USE_MINIZIP USE_PNG USE_LIBZIP USE_SQLITE3 USE_ELF USE_LUA USE_LZMA
            USE_DISCORD_RPC USE_EPOXY ENABLE_SCRIPTING BUILD_GL BUILD_GLES2
            BUILD_GLES3 BUILD_QT BUILD_SDL BUILD_PYTHON BUILD_TEST BUILD_SUITE)
        set(${feature} OFF)
    endforeach()
    set(BUILD_LTO OFF)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    add_subdirectory(third_party/mgba EXCLUDE_FROM_ALL)
    if(WIN32 AND NOT MINGW)
        # clang's GNU driver targeting the MSVC ABI uses the CRT for math.
        get_target_property(mgba_links mgba LINK_LIBRARIES)
        list(REMOVE_ITEM mgba_links m)
        set_target_properties(mgba PROPERTIES LINK_LIBRARIES "${mgba_links}"
            INTERFACE_LINK_LIBRARIES "${mgba_links}")
    endif()
endfunction()
matcha_add_gba()
add_library(matcha_gba STATIC src/gba_core.cpp)
target_include_directories(matcha_gba PUBLIC include)
target_include_directories(matcha_gba SYSTEM PRIVATE
    third_party/mgba/include "${CMAKE_CURRENT_BINARY_DIR}/third_party/mgba/include")
target_link_libraries(matcha_gba PRIVATE mgba)
target_compile_definitions(matcha_gba PRIVATE BUILD_STATIC NOMINMAX)
