cmake_minimum_required(VERSION 3.22.1)
project(dddvr_openxr)

add_library(dddvr_openxr SHARED
    jni/JniBridge.cpp
    openxr/OpenXrApp.cpp
    openxr/OpenXrSession.cpp
    openxr/OpenXrSwapchain.cpp
    openxr/OpenXrRenderer.cpp
    openxr/OpenXrInput.cpp
    openxr/OpenXrLoader.cpp
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

find_library(OPENXR_LOADER openxr_loader)
if(OPENXR_LOADER)
  target_link_libraries(dddvr_openxr android log EGL GLESv3 ${OPENXR_LOADER})
else()
  target_link_libraries(dddvr_openxr android log EGL GLESv3)
endif()
