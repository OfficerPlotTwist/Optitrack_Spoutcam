cmake_minimum_required(VERSION 3.15)
project(optitrack_spout LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Spout sender (SpoutDX) — compiled directly from the vendored submodule.
# This is upstream's exact SpoutDX_static source set, which avoids pulling in
# SpoutGL / SpoutDX9 / SpoutDX12 and their extra dependencies.
# ---------------------------------------------------------------------------
set(SPOUTDX_DIR ${CMAKE_SOURCE_DIR}/external/Spout2/SPOUTSDK/SpoutDirectX/SpoutDX)
set(SPOUTGL_DIR ${CMAKE_SOURCE_DIR}/external/Spout2/SPOUTSDK/SpoutGL)

if(NOT EXISTS ${SPOUTDX_DIR}/SpoutDX.cpp)
    message(FATAL_ERROR
        "Spout2 submodule missing. Run: git submodule update --init --recursive")
endif()

add_library(spout_grayscale_sender STATIC
    src/spout_grayscale_sender.cpp
    src/spout_grayscale_sender.h
    ${SPOUTDX_DIR}/SpoutDX.cpp
    ${SPOUTGL_DIR}/SpoutCopy.cpp
    ${SPOUTGL_DIR}/SpoutDirectX.cpp
    ${SPOUTGL_DIR}/SpoutFrameCount.cpp
    ${SPOUTGL_DIR}/SpoutSenderNames.cpp
    ${SPOUTGL_DIR}/SpoutSharedMemory.cpp
    ${SPOUTGL_DIR}/SpoutUtils.cpp
)
target_compile_definitions(spout_grayscale_sender PRIVATE SPOUT_BUILD_STATIC)
target_include_directories(spout_grayscale_sender PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${SPOUTDX_DIR}
    ${SPOUTGL_DIR}
)
target_link_libraries(spout_grayscale_sender PUBLIC d3d11 dxgi)

# ---------------------------------------------------------------------------
# Test-pattern sender — builds WITHOUT the Camera SDK. Use to validate the
# Spout pipeline into TouchDesigner before the camera app is available.
# ---------------------------------------------------------------------------
add_executable(spout_testpattern src/main_testpattern.cpp)
target_link_libraries(spout_testpattern PRIVATE spout_grayscale_sender)

# ---------------------------------------------------------------------------
# Real OptiTrack capture app — needs the Camera SDK (version-matched to Motive).
# Point CMake at it with -DCAMERA_SDK_DIR=<root> (contains include/ and lib/),
# or set the OPTITRACK_CAMERA_SDK environment variable.
# ---------------------------------------------------------------------------
set(CAMERA_SDK_DIR "" CACHE PATH "OptiTrack Camera SDK root (contains include/ and lib/)")
if(NOT CAMERA_SDK_DIR AND DEFINED ENV{OPTITRACK_CAMERA_SDK})
    set(CAMERA_SDK_DIR "$ENV{OPTITRACK_CAMERA_SDK}")
endif()

find_path(CAMERA_SDK_INCLUDE
    NAMES cameralibrary.h
    HINTS "${CAMERA_SDK_DIR}/include"
          "C:/Program Files (x86)/OptiTrack/CameraSDK/include"
          "C:/Program Files/OptiTrack/Camera SDK/include")

if(CAMERA_SDK_INCLUDE)
    # lib/ sits alongside include/
    get_filename_component(_sdk_root "${CAMERA_SDK_INCLUDE}" DIRECTORY)
    set(_sdk_lib "${_sdk_root}/lib")
    # DLL-based SDK: pick release (…x64S) and debug (…x64D) import libs.
    file(GLOB _cam_lib_release "${_sdk_lib}/CameraLibrary*x64S.lib")
    file(GLOB _cam_lib_debug   "${_sdk_lib}/CameraLibrary*x64D.lib")
endif()

if(CAMERA_SDK_INCLUDE AND _cam_lib_release)
    message(STATUS "Camera SDK include:    ${CAMERA_SDK_INCLUDE}")
    message(STATUS "Camera SDK lib (rel):  ${_cam_lib_release}")
    message(STATUS "Camera SDK lib (dbg):  ${_cam_lib_debug}")

    add_executable(optitrack_spout src/main_camera.cpp)
    target_include_directories(optitrack_spout PRIVATE "${CAMERA_SDK_INCLUDE}")

    # Link debug import lib in Debug, release otherwise (fall back to release).
    if(_cam_lib_debug)
        target_link_libraries(optitrack_spout PRIVATE spout_grayscale_sender
            $<$<CONFIG:Debug>:${_cam_lib_debug}>
            $<$<NOT:$<CONFIG:Debug>>:${_cam_lib_release}>)
    else()
        target_link_libraries(optitrack_spout PRIVATE spout_grayscale_sender ${_cam_lib_release})
    endif()

    # Copy the matching CameraLibrary DLL next to the exe (it is DLL-linked).
    get_filename_component(_cam_dll_rel_name "${_cam_lib_release}" NAME_WE)
    set(_cam_dll_release "${_sdk_lib}/${_cam_dll_rel_name}.dll")
    set(_cam_dll_copy "${_cam_dll_release}")
    if(_cam_lib_debug)
        get_filename_component(_cam_dll_dbg_name "${_cam_lib_debug}" NAME_WE)
        set(_cam_dll_debug "${_sdk_lib}/${_cam_dll_dbg_name}.dll")
        set(_cam_dll_copy "$<IF:$<CONFIG:Debug>,${_cam_dll_debug},${_cam_dll_release}>")
    endif()
    add_custom_command(TARGET optitrack_spout POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_cam_dll_copy}" "$<TARGET_FILE_DIR:optitrack_spout>"
        COMMENT "Copying CameraLibrary DLL next to optitrack_spout.exe")
else()
    message(WARNING
        "Camera SDK not found — only 'spout_testpattern' will build. "
        "Install the Camera SDK (matching Motive) and reconfigure with "
        "-DCAMERA_SDK_DIR=<sdk root>.")
endif()
