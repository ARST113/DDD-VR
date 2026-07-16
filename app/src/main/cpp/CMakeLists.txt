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
set(NATIVE_APP_GLUE_DIR "${ANDROID_NDK}/sources/android/native_app_glue")
add_library(native_app_glue STATIC "${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
target_include_directories(native_app_glue PUBLIC "${NATIVE_APP_GLUE_DIR}")

set(DDDVR_IMGUI_SOURCES
    third/imgui/imgui.cpp
    third/imgui/imgui_draw.cpp
    third/imgui/imgui_tables.cpp
    third/imgui/imgui_widgets.cpp
)

set(DDDVR_OPENXR_UI_SOURCES
    ui/VrPicoGuiBase.cpp
    ui/VrPicoGui.cpp
    ui/VrPlayerPanel.cpp
    ui/VrRayInteractor.cpp
)

set(DDDVR_VIDEO_SOURCES
    video/AndroidImageApi.cpp
    video/DolbyRpuParser.cpp
    video/ExternalOesVideoTexture.cpp
    video/FfmpegAudioOutput.cpp
    video/FfmpegVideoDecoder.cpp
    video/FfmpegVideoTexture.cpp
)

if(NOT DEFINED DDDVR_FFMPEG_ROOT)
    get_filename_component(DDDVR_FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../ffmpeg" ABSOLUTE)
else()
    get_filename_component(DDDVR_FFMPEG_ROOT "${DDDVR_FFMPEG_ROOT}" ABSOLUTE)
endif()
set(DDDVR_HAS_FFMPEG_VIDEO 0)
set(DDDVR_FFMPEG_INCLUDE_DIR "")
set(DDDVR_FFMPEG_LIBS "")
message(STATUS "DDD-VR FFmpeg probe abi=${ANDROID_ABI} root=${DDDVR_FFMPEG_ROOT}")
if(EXISTS "${DDDVR_FFMPEG_ROOT}/include/libavcodec/avcodec.h")
    set(DDDVR_FFMPEG_INCLUDE_DIR "${DDDVR_FFMPEG_ROOT}/include")
    set(DDDVR_FFMPEG_ALL_LIBS_FOUND TRUE)
    foreach(DDDVR_FFMPEG_LIB avformat avcodec avutil swscale swresample)
        set(DDDVR_FFMPEG_LIB_PATH "${DDDVR_FFMPEG_ROOT}/${ANDROID_ABI}/lib${DDDVR_FFMPEG_LIB}.so")
        if(NOT EXISTS "${DDDVR_FFMPEG_LIB_PATH}")
            set(DDDVR_FFMPEG_LIB_PATH "${DDDVR_FFMPEG_ROOT}/lib/${ANDROID_ABI}/lib${DDDVR_FFMPEG_LIB}.so")
        endif()
        if(NOT EXISTS "${DDDVR_FFMPEG_LIB_PATH}")
            set(DDDVR_FFMPEG_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/lib${DDDVR_FFMPEG_LIB}.so")
        endif()
        if(EXISTS "${DDDVR_FFMPEG_LIB_PATH}")
            add_library(dddvr_${DDDVR_FFMPEG_LIB} SHARED IMPORTED)
            set_target_properties(dddvr_${DDDVR_FFMPEG_LIB} PROPERTIES IMPORTED_LOCATION "${DDDVR_FFMPEG_LIB_PATH}")
            list(APPEND DDDVR_FFMPEG_LIBS dddvr_${DDDVR_FFMPEG_LIB})
        else()
            message(WARNING "FFmpeg library not found for ${ANDROID_ABI}: lib${DDDVR_FFMPEG_LIB}.so")
            set(DDDVR_FFMPEG_ALL_LIBS_FOUND FALSE)
        endif()
    endforeach()
    if(DDDVR_FFMPEG_ALL_LIBS_FOUND)
        set(DDDVR_HAS_FFMPEG_VIDEO 1)
        message(STATUS "DDD-VR FFmpeg video backend enabled: ${DDDVR_FFMPEG_ROOT} (${ANDROID_ABI})")
    else()
        set(DDDVR_FFMPEG_LIBS "")
        message(WARNING "DDD-VR FFmpeg video backend disabled because one or more libraries are missing")
    endif()
else()
    message(STATUS "DDD-VR FFmpeg video backend disabled; headers not found at ${DDDVR_FFMPEG_ROOT}/include")
endif()

add_library(dddvr_openxr SHARED
    jni/JniBridge.cpp
    openxr/OpenXrApp.cpp
    openxr/OpenXrLoader.cpp
    openxr/OpenXrSession.cpp
    openxr/OpenXrSwapchain.cpp
    openxr/OpenXrRenderer.cpp
    openxr/OpenXrInput.cpp
    gl/GlProgram.cpp
    gl/CinemaScreenRenderer.cpp
    gl/DolbyMappingTexture.cpp
    ${DDDVR_VIDEO_SOURCES}
    ${DDDVR_IMGUI_SOURCES}
    ${DDDVR_OPENXR_UI_SOURCES}
)

target_compile_features(dddvr_openxr PRIVATE cxx_std_17)
target_compile_definitions(
    dddvr_openxr
    PRIVATE
        DDDVR_HAS_OPENXR=1
        DDDVR_HAS_FFMPEG_VIDEO=${DDDVR_HAS_FFMPEG_VIDEO}
        XR_USE_PLATFORM_ANDROID
        XR_USE_GRAPHICS_API_OPENGL_ES
)
target_include_directories(
    dddvr_openxr
    PRIVATE
        "${OPENXR_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/third/imgui"
        ${DDDVR_FFMPEG_INCLUDE_DIR}
)

target_link_libraries(dddvr_openxr
    android
    dl
    mediandk
    OpenSLES
    log
    EGL
    GLESv3
    openxr_loader
    ${DDDVR_FFMPEG_LIBS}
)

add_library(dddvr_openxr_native_smoke SHARED
    native/OpenXrNativeSmokeMain.cpp
    openxr/OpenXrApp.cpp
    openxr/OpenXrLoader.cpp
    openxr/OpenXrSession.cpp
    openxr/OpenXrSwapchain.cpp
    openxr/OpenXrRenderer.cpp
    openxr/OpenXrInput.cpp
    gl/GlProgram.cpp
    gl/CinemaScreenRenderer.cpp
    gl/DolbyMappingTexture.cpp
    ${DDDVR_VIDEO_SOURCES}
    ${DDDVR_IMGUI_SOURCES}
    ${DDDVR_OPENXR_UI_SOURCES}
)
target_compile_features(dddvr_openxr_native_smoke PRIVATE cxx_std_17)
target_compile_definitions(
    dddvr_openxr_native_smoke
    PRIVATE
        DDDVR_HAS_OPENXR=1
        DDDVR_HAS_FFMPEG_VIDEO=${DDDVR_HAS_FFMPEG_VIDEO}
        XR_USE_PLATFORM_ANDROID
        XR_USE_GRAPHICS_API_OPENGL_ES
)
target_include_directories(
    dddvr_openxr_native_smoke
    PRIVATE
        "${OPENXR_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/third/imgui"
        ${DDDVR_FFMPEG_INCLUDE_DIR}
)
target_link_libraries(dddvr_openxr_native_smoke android dl mediandk OpenSLES log EGL GLESv3 native_app_glue openxr_loader ${DDDVR_FFMPEG_LIBS})
