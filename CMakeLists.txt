cmake_minimum_required(VERSION 3.0...3.5)
project(SpaceCadetPinball)

set(CMAKE_CXX_STANDARD 11)


if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/bin)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/CMakeModules")

# On Windows, set paths to SDL-devel packages here
if(WIN32)
   list(APPEND SDL2_PATH "${CMAKE_CURRENT_LIST_DIR}/Libs/SDL2")
   list(APPEND SDL2_MIXER_PATH "${CMAKE_CURRENT_LIST_DIR}/Libs/SDL2_mixer")
endif()

# Link mingw libs static
if(MINGW)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
endif()

if(APPLE)
    set(MACOSX_RPATH)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH true)
    set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.11")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    list(APPEND SDL2_PATH "${CMAKE_CURRENT_LIST_DIR}/Libs")
    list(APPEND SDL2_MIXER_PATH "${CMAKE_CURRENT_LIST_DIR}/Libs")
endif()

# SDL2main is not needed
set(SDL2_BUILDING_LIBRARY ON)

find_package(SDL2 REQUIRED)
FIND_PACKAGE(SDL2_mixer REQUIRED)

include_directories(${SDL2_INCLUDE_DIR} ${SDL2_MIXER_INCLUDE_DIR})
get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
foreach(dir ${dirs})
  message(STATUS "Include dir='${dir}'")
endforeach()



set(SOURCE_FILES
        SpaceCadetPinball/control.cpp
        SpaceCadetPinball/control.h
        SpaceCadetPinball/EmbeddedData.cpp
        SpaceCadetPinball/EmbeddedData.h
        SpaceCadetPinball/font_selection.cpp
        SpaceCadetPinball/font_selection.h
        SpaceCadetPinball/fullscrn.cpp
        SpaceCadetPinball/fullscrn.h
        SpaceCadetPinball/gdrv.cpp
        SpaceCadetPinball/gdrv.h
        SpaceCadetPinball/GroupData.cpp
        SpaceCadetPinball/GroupData.h
        SpaceCadetPinball/high_score.cpp
        SpaceCadetPinball/high_score.h
        SpaceCadetPinball/loader.cpp
        SpaceCadetPinball/loader.h
        SpaceCadetPinball/maths.cpp
        SpaceCadetPinball/maths.h
        SpaceCadetPinball/midi.cpp
        SpaceCadetPinball/midi.h
        SpaceCadetPinball/nudge.cpp
        SpaceCadetPinball/nudge.h
        SpaceCadetPinball/options.cpp
        SpaceCadetPinball/options.h
        SpaceCadetPinball/partman.cpp
        SpaceCadetPinball/partman.h
        SpaceCadetPinball/pb.cpp
        SpaceCadetPinball/pb.h
        SpaceCadetPinball/pch.h
        SpaceCadetPinball/proj.cpp
        SpaceCadetPinball/proj.h
        SpaceCadetPinball/render.cpp
        SpaceCadetPinball/render.h
        SpaceCadetPinball/score.cpp
        SpaceCadetPinball/score.h
        SpaceCadetPinball/Sound.cpp
        SpaceCadetPinball/Sound.h
        SpaceCadetPinball/SpaceCadetPinball.cpp
        SpaceCadetPinball/TBall.cpp
        SpaceCadetPinball/TBall.h
        SpaceCadetPinball/TBlocker.cpp
        SpaceCadetPinball/TBlocker.h
        SpaceCadetPinball/TBumper.cpp
        SpaceCadetPinball/TBumper.h
        SpaceCadetPinball/TCircle.cpp
        SpaceCadetPinball/TCircle.h
        SpaceCadetPinball/TCollisionComponent.cpp
        SpaceCadetPinball/TCollisionComponent.h
        SpaceCadetPinball/TComponentGroup.cpp
        SpaceCadetPinball/TComponentGroup.h
        SpaceCadetPinball/TDemo.cpp
        SpaceCadetPinball/TDemo.h
        SpaceCadetPinball/TDrain.cpp
        SpaceCadetPinball/TDrain.h
        SpaceCadetPinball/TEdgeBox.h
        SpaceCadetPinball/TEdgeManager.cpp
        SpaceCadetPinball/TEdgeManager.h
        SpaceCadetPinball/TEdgeSegment.cpp
        SpaceCadetPinball/TEdgeSegment.h
        SpaceCadetPinball/TFlagSpinner.cpp
        SpaceCadetPinball/TFlagSpinner.h
        SpaceCadetPinball/TFlipper.cpp
        SpaceCadetPinball/TFlipper.h
        SpaceCadetPinball/TFlipperEdge.cpp
        SpaceCadetPinball/TFlipperEdge.h
        SpaceCadetPinball/TGate.cpp
        SpaceCadetPinball/TGate.h
        SpaceCadetPinball/THole.cpp
        SpaceCadetPinball/THole.h
        SpaceCadetPinball/timer.cpp
        SpaceCadetPinball/timer.h
        SpaceCadetPinball/TKickback.cpp
        SpaceCadetPinball/TKickback.h
        SpaceCadetPinball/TKickout.cpp
        SpaceCadetPinball/TKickout.h
        SpaceCadetPinball/TLight.cpp
        SpaceCadetPinball/TLight.h
        SpaceCadetPinball/TLightBargraph.cpp
        SpaceCadetPinball/TLightBargraph.h
        SpaceCadetPinball/TLightGroup.cpp
        SpaceCadetPinball/TLightGroup.h
        SpaceCadetPinball/TLightRollover.cpp
        SpaceCadetPinball/TLightRollover.h
        SpaceCadetPinball/TLine.cpp
        SpaceCadetPinball/TLine.h
        SpaceCadetPinball/TOneway.cpp
        SpaceCadetPinball/TOneway.h
        SpaceCadetPinball/TPinballComponent.cpp
        SpaceCadetPinball/TPinballComponent.h
        SpaceCadetPinball/TPinballTable.cpp
        SpaceCadetPinball/TPinballTable.h
        SpaceCadetPinball/TPlunger.cpp
        SpaceCadetPinball/TPlunger.h
        SpaceCadetPinball/TPopupTarget.cpp
        SpaceCadetPinball/TPopupTarget.h
        SpaceCadetPinball/TRamp.cpp
        SpaceCadetPinball/TRamp.h
        SpaceCadetPinball/translations.cpp
        SpaceCadetPinball/translations.h
        SpaceCadetPinball/TRollover.cpp
        SpaceCadetPinball/TRollover.h
        SpaceCadetPinball/TSink.cpp
        SpaceCadetPinball/TSink.h
        SpaceCadetPinball/TSoloTarget.cpp
        SpaceCadetPinball/TSoloTarget.h
        SpaceCadetPinball/TSound.cpp
        SpaceCadetPinball/TSound.h
        SpaceCadetPinball/TTableLayer.cpp
        SpaceCadetPinball/TTableLayer.h
        SpaceCadetPinball/TTextBox.cpp
        SpaceCadetPinball/TTextBox.h
        SpaceCadetPinball/TTextBoxMessage.cpp
        SpaceCadetPinball/TTextBoxMessage.h
        SpaceCadetPinball/TTimer.cpp
        SpaceCadetPinball/TTimer.h
        SpaceCadetPinball/TTripwire.cpp
        SpaceCadetPinball/TTripwire.h
        SpaceCadetPinball/TWall.cpp
        SpaceCadetPinball/TWall.h
        SpaceCadetPinball/winmain.cpp
        SpaceCadetPinball/winmain.h
        SpaceCadetPinball/zdrv.cpp
        SpaceCadetPinball/zdrv.h
        SpaceCadetPinball/imconfig.h
        SpaceCadetPinball/imgui_internal.h
        SpaceCadetPinball/imgui.cpp
        SpaceCadetPinball/imgui.h
        SpaceCadetPinball/imgui_sdl.cpp
        SpaceCadetPinball/imgui_sdl.h
        SpaceCadetPinball/imgui_draw.cpp
        SpaceCadetPinball/imgui_widgets.cpp
        SpaceCadetPinball/imgui_tables.cpp
        SpaceCadetPinball/imgui_demo.cpp
        SpaceCadetPinball/imgui_impl_sdl.cpp
        SpaceCadetPinball/imgui_impl_sdl.h
        SpaceCadetPinball/imgui_impl_sdlrenderer.h
        SpaceCadetPinball/imgui_impl_sdlrenderer.cpp
        SpaceCadetPinball/imstb_textedit.h
        SpaceCadetPinball/imstb_rectpack.h
        SpaceCadetPinball/imstb_truetype.h
        SpaceCadetPinball/DebugOverlay.cpp
        SpaceCadetPinball/DebugOverlay.h
        )

# ---------------------------------------------------------------------------
# VR (OpenXR + Vulkan) support, for the Steam Frame and other SteamVR / OpenXR headsets.
# Start the game with "-vr" to play in VR. The executable does not link against Vulkan
# (it is loaded at run time) and the OpenXR loader is only needed at run time in VR mode.
option(SCP_VR "Build with OpenXR VR support (Steam Frame, SteamVR, Monado)" ON)
option(SCP_VR_FETCH_OPENXR "Download and build the OpenXR loader if it is not found" ON)
option(SCP_PORTABLE "Packaged build: only search \$ORIGIN/lib and \$ORIGIN for bundled libraries" OFF)
if(SCP_VR)
    list(APPEND SOURCE_FILES
        SpaceCadetPinball/vr/VrRuntime.cpp
        SpaceCadetPinball/vr/VrRuntime.h
        SpaceCadetPinball/vr/VrScene.cpp
        SpaceCadetPinball/vr/VrScene.h
        SpaceCadetPinball/vr/VrFeedback.cpp
        SpaceCadetPinball/vr/VrFeedback.h
        SpaceCadetPinball/vr/vk_loader.cpp
        SpaceCadetPinball/vr/vk_loader.h
        SpaceCadetPinball/vr/vr_math.h
        SpaceCadetPinball/vr/vr_shaders.h
    )
endif()

# On Windows, include resource file with the icon
if(WIN32)
    set_source_files_properties(SpaceCadetPinball/SpaceCadetPinball.rc LANGUAGE RC)
    list(APPEND SOURCE_FILES SpaceCadetPinball/SpaceCadetPinball.rc)
endif(WIN32)

add_executable(SpaceCadetPinball ${SOURCE_FILES})

# Skip pch on foreign code
set_source_files_properties(
        SpaceCadetPinball/imgui.cpp
        SpaceCadetPinball/imgui_sdl.cpp
        SpaceCadetPinball/imgui_draw.cpp
        SpaceCadetPinball/imgui_widgets.cpp
        SpaceCadetPinball/imgui_tables.cpp
        SpaceCadetPinball/imgui_demo.cpp
        SpaceCadetPinball/imgui_impl_sdl.cpp
        SpaceCadetPinball/imgui_impl_sdlrenderer.cpp
        PROPERTIES SKIP_PRECOMPILE_HEADERS 1
)

if(${CMAKE_VERSION} VERSION_GREATER "3.16.0" OR ${CMAKE_VERSION} VERSION_EQUAL "3.16.0")
    target_precompile_headers(SpaceCadetPinball
            PUBLIC
            SpaceCadetPinball/pch.h
            )
endif()

target_link_libraries(SpaceCadetPinball ${SDL2_LIBRARY} ${SDL2_MIXER_LIBRARY})

if(SCP_VR)
    set_source_files_properties(
        SpaceCadetPinball/vr/VrRuntime.cpp
        SpaceCadetPinball/vr/VrScene.cpp
        SpaceCadetPinball/vr/VrFeedback.cpp
        SpaceCadetPinball/vr/vk_loader.cpp
        PROPERTIES SKIP_PRECOMPILE_HEADERS 1
    )
    target_compile_definitions(SpaceCadetPinball PRIVATE SCP_VR=1)
    if(NOT CMAKE_CXX_STANDARD OR CMAKE_CXX_STANDARD LESS 14)
        set_property(TARGET SpaceCadetPinball PROPERTY CXX_STANDARD 14)
    endif()

    # Vulkan headers only (no import library needed).
    find_path(VULKAN_HEADERS_DIR vulkan/vulkan.h HINTS $ENV{VULKAN_SDK}/include)
    if(NOT VULKAN_HEADERS_DIR)
        message(FATAL_ERROR "Vulkan headers not found. Install vulkan-headers / libvulkan-dev, set VULKAN_HEADERS_DIR, or configure with -DSCP_VR=OFF.")
    endif()
    target_include_directories(SpaceCadetPinball PRIVATE ${VULKAN_HEADERS_DIR})

    # OpenXR loader: user supplied, system package, or downloaded.
    if(OPENXR_INCLUDE_DIR AND OPENXR_LIBRARY)
        target_include_directories(SpaceCadetPinball PRIVATE ${OPENXR_INCLUDE_DIR})
        target_link_libraries(SpaceCadetPinball ${OPENXR_LIBRARY})
    else()
        find_package(OpenXR CONFIG QUIET)
        if(OpenXR_FOUND)
            target_link_libraries(SpaceCadetPinball OpenXR::headers OpenXR::openxr_loader)
        elseif(SCP_VR_FETCH_OPENXR)
            include(FetchContent)
            set(BUILD_TESTS OFF CACHE INTERNAL "")
            set(BUILD_API_LAYERS OFF CACHE INTERNAL "")
            set(DYNAMIC_LOADER ON CACHE INTERNAL "")
            # Bundle jsoncpp into the loader so the shipped libopenxr_loader.so.1 has no extra dependencies.
            set(BUILD_WITH_SYSTEM_JSONCPP OFF CACHE INTERNAL "")
            FetchContent_Declare(openxr_sdk
                URL https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.65.tar.gz)
            FetchContent_MakeAvailable(openxr_sdk)
            target_link_libraries(SpaceCadetPinball openxr_loader)
            target_include_directories(SpaceCadetPinball PRIVATE ${openxr_sdk_SOURCE_DIR}/include ${openxr_sdk_BINARY_DIR}/include)
        else()
            message(FATAL_ERROR "OpenXR loader not found. Set OPENXR_INCLUDE_DIR and OPENXR_LIBRARY, enable SCP_VR_FETCH_OPENXR, or configure with -DSCP_VR=OFF.")
        endif()
    endif()

    if(UNIX)
        target_link_libraries(SpaceCadetPinball ${CMAKE_DL_LIBS})
        # Look for bundled libraries (OpenXR loader, SDL2) next to the executable first.
        if(NOT APPLE)
            set_target_properties(SpaceCadetPinball PROPERTIES
                BUILD_RPATH "\$ORIGIN/lib;\$ORIGIN"
                INSTALL_RPATH "\$ORIGIN/lib;\$ORIGIN")
            if(CMAKE_CROSSCOMPILING OR SCP_PORTABLE)
                # Cross/packaged builds are packaged with their libraries; don't leak host paths.
                set_target_properties(SpaceCadetPinball PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE)
            endif()
        endif()
    endif()

    # Self-test that renders a synthetic table without game data (handy on a new headset/runtime).
    add_executable(SpaceCadetPinballVRTest
        SpaceCadetPinball/vr/vr_selftest.cpp
        SpaceCadetPinball/vr/VrRuntime.cpp
        SpaceCadetPinball/vr/VrScene.cpp
        SpaceCadetPinball/vr/vk_loader.cpp)
    set_property(TARGET SpaceCadetPinballVRTest PROPERTY CXX_STANDARD 14)
    target_include_directories(SpaceCadetPinballVRTest PRIVATE ${VULKAN_HEADERS_DIR})
    get_target_property(_scp_inc SpaceCadetPinball INCLUDE_DIRECTORIES)
    get_target_property(_scp_libs SpaceCadetPinball LINK_LIBRARIES)
    target_include_directories(SpaceCadetPinballVRTest PRIVATE ${_scp_inc})
    target_link_libraries(SpaceCadetPinballVRTest ${_scp_libs})
    if(UNIX AND NOT APPLE)
        set_target_properties(SpaceCadetPinballVRTest PROPERTIES BUILD_RPATH "\$ORIGIN/lib;\$ORIGIN"
            INSTALL_RPATH "\$ORIGIN/lib;\$ORIGIN")
        if(CMAKE_CROSSCOMPILING OR SCP_PORTABLE)
            set_target_properties(SpaceCadetPinballVRTest PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE)
        endif()
    endif()
endif()

# On Windows, copy DLL to output
if(WIN32)
    list(GET SDL2_LIBRARY -1 SDL2_DLL_PATH)
    list(GET SDL2_MIXER_LIBRARY -1 SDL2_MIXER_DLL_PATH)
    get_filename_component(SDL2_DLL_PATH ${SDL2_DLL_PATH} DIRECTORY)
    get_filename_component(SDL2_MIXER_DLL_PATH ${SDL2_MIXER_DLL_PATH} DIRECTORY)
    if(MINGW)
        string(REGEX REPLACE "lib$" "bin" SDL2_DLL_PATH ${SDL2_DLL_PATH})
        string(REGEX REPLACE "lib$" "bin" SDL2_MIXER_DLL_PATH ${SDL2_MIXER_DLL_PATH})
    endif()
    message(STATUS "copy paths='${SDL2_DLL_PATH}' '${SDL2_MIXER_DLL_PATH}'") 
    add_custom_command(TARGET SpaceCadetPinball POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL2_DLL_PATH}/SDL2.dll" $<TARGET_FILE_DIR:SpaceCadetPinball>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL2_MIXER_DLL_PATH}/SDL2_mixer.dll" $<TARGET_FILE_DIR:SpaceCadetPinball>
        )
endif()

if(UNIX AND NOT APPLE)
	include(GNUInstallDirs)
	install(TARGETS "${PROJECT_NAME}" RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
	install(FILES "${CMAKE_SOURCE_DIR}/Platform/Linux/${PROJECT_NAME}.desktop" DESTINATION "share/applications")
	install(FILES "${CMAKE_SOURCE_DIR}/Platform/Linux/${PROJECT_NAME}.metainfo.xml" DESTINATION "share/metainfo")
	foreach(S 16 32 48 128 192)
		install(FILES "${CMAKE_SOURCE_DIR}/${PROJECT_NAME}/Icon_${S}x${S}.png" DESTINATION
			"share/icons/hicolor/${S}x${S}/apps" RENAME "${PROJECT_NAME}.png")
	endforeach(S)
endif()
