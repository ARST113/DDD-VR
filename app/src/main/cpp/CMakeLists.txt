cmake_minimum_required(VERSION 3.22.1)
project(dddvr_openxr)

include(FetchContent)

set(OPENXR_SDK_SOURCE "${CMAKE_SOURCE_DIR}/../../../../third_party/OpenXR-SDK")
if(EXISTS "${OPENXR_SDK_SOURCE}/CMakeLists.txt")
    set(OPENXR_INCLUDE_DIR "${OPENXR_SDK_SOURCE}/include")
    add_subdirectory("${OPENXR_SDK_SOURCE}" "${CMAKE_BINARY_DIR}/openxr-sdk")
else()
    FetchContent_Declare(
        OpenXRSDK
        GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
        GIT_TAG release-1.1.43
        GIT_SHALLOW TRUE
    )
    set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(OpenXRSDK)
    set(OPENXR_INCLUDE_DIR "${openxrsdk_SOURCE_DIR}/include")
endif()

if(NOT TARGET openxr_loader)
    message(FATAL_ERROR "openxr_loader target is required but was not created from OpenXR SDK")
endif()

add_library(dddvr_openxr SHARED
    jni/JniBridge.cpp
    openxr/OpenXrApp.cpp
    openxr/OpenXrSession.cpp
    openxr/OpenXrSwapchain.cpp
    openxr/OpenXrRenderer.cpp
    openxr/OpenXrInput.cpp
    gl/GlProgram.cpp
    gl/CinemaScreenRenderer.cpp
    video/ExternalOesVideoTexture.cpp
)

target_compile_features(dddvr_openxr PRIVATE cxx_std_17)
target_compile_definitions(
    dddvr_openxr
    PRIVATE
        DDDVR_HAS_OPENXR=1
        XR_USE_PLATFORM_ANDROID
        XR_USE_GRAPHICS_API_OPENGL_ES
)
target_include_directories(dddvr_openxr PRIVATE "${OPENXR_INCLUDE_DIR}")

target_link_libraries(dddvr_openxr
    android
    log
    EGL
    GLESv3
    openxr_loader
)
