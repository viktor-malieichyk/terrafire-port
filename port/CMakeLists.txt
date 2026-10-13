cmake_minimum_required(VERSION 3.24)
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
project(terrafire VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use an installed SDL3 (Homebrew on macOS, vcpkg/SDK on Windows); otherwise
# build it from source so a fresh Windows checkout works with no setup.
option(TF_VENDOR_SDL "Always build SDL3 from source" OFF)
if(NOT TF_VENDOR_SDL)
    find_package(SDL3 CONFIG QUIET)
endif()
if(NOT SDL3_FOUND)
    message(STATUS "SDL3 not found; fetching and building it")
    include(FetchContent)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.18
        GIT_SHALLOW TRUE)
    # macOS links it in, so the app needs nothing beside it; Windows ships
    # SDL3.dll next to the exe.
    if(APPLE)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    else()
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    endif()
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
endif()

add_executable(terrafire
    src/main.c
    src/platform_sdl.c
    src/pak.c
    src/assets.c
    src/viewer.c
    src/kbd.c
    src/timing.c
    src/demo.c
    src/palette.c
    src/state.c
    src/session.c
    src/missions_data.c
    src/unported.c
    src/res.c
    src/levels.c
    src/fatal.c
    src/objects.c
    src/anims_data.c
    src/callbacks_data.c
    src/render.c
    src/frame.c
    src/spawn.c
    src/collision.c
    src/tables_data.c
    src/player.c
    src/hits.c
    src/effects.c
    src/pickups.c
    src/weapons.c
    src/enemies.c
    src/hud.c
    src/tow.c
    src/sound.c
    src/bonus.c
    src/menu.c
    src/save.c
    src/screens.c
    src/joystick.c
)
if(TARGET SDL3::SDL3)
    target_link_libraries(terrafire PRIVATE SDL3::SDL3)
else()
    target_link_libraries(terrafire PRIVATE SDL3::SDL3-static)
endif()

# macOS: -DTF_MACOS_BUNDLE=ON builds TerraFire.app (for releases), which
# finds DATA.PAK in the folder it is in.
option(TF_MACOS_BUNDLE "macOS: build TerraFire.app" OFF)
if(APPLE AND TF_MACOS_BUNDLE)
    target_sources(terrafire PRIVATE res/terrafire.icns)
    set_source_files_properties(res/terrafire.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(terrafire PROPERTIES
        OUTPUT_NAME TerraFire
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/res/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME TerraFire
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.viktor-malieichyk.terrafire
        MACOSX_BUNDLE_ICON_FILE terrafire.icns
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
endif()

# Windows: a GUI program (no console window) with the game's icon. Logs then
# go nowhere; configure with -DTF_WINDOWS_CONSOLE=ON to keep the console.
option(TF_WINDOWS_CONSOLE "Windows: build as a console program" OFF)
if(WIN32)
    target_sources(terrafire PRIVATE res/terrafire.rc)
    if(NOT TF_WINDOWS_CONSOLE)
        if(MSVC)
            target_link_options(terrafire PRIVATE /SUBSYSTEM:WINDOWS)   # SDL_main.h supplies WinMain
        else()
            target_link_options(terrafire PRIVATE -mwindows)
        endif()
    endif()
    if(MINGW)
        # Link the C runtime statically: only SDL3.dll ships with the exe.
        target_link_options(terrafire PRIVATE -static-libgcc)
    endif()
endif()

# The original's x87 code runs at 53-bit precision, which IEEE double
# reproduces only without fused multiply-add contraction.
if(MSVC)
    target_compile_options(terrafire PRIVATE /W4 /D_CRT_SECURE_NO_WARNINGS /fp:precise)
else()
    target_compile_options(terrafire PRIVATE -Wall -Wextra -Wpedantic -Wshadow -ffp-contract=off)
endif()

# Windows: put SDL3.dll next to the executable so it runs from the build dir.
if(WIN32)
    add_custom_command(TARGET terrafire POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:terrafire>)
endif()
