#include "FfmpegVideoTexture.h"
#include "AndroidImageApi.h"
#include "DolbyRpuParser.h"
#include "../util/XrLog.h"
#include <algorithm>
#include <GLES2/gl2ext.h>

namespace {
int chromaSize(int value) {
    return (value + 1) / 2;
}

bool hasPlane(const FfmpegVideoFrame& frame, int index) {
    return index >= 0 && index < 4 && !frame.planes[index].empty();
}
}

FfmpegVideoTexture::~FfmpegVideoTexture() {
    destroy();
}

void FfmpegVideoTexture::ensureTextures() {
    if (textureSet_.planes[0] != 0) return;
    glGenTextures(4, textureSet_.planes);
    for (GLuint texture : textureSet_.planes) {
        if (texture == 0) continue;
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    XR_LOGI(
        "DDDVR/FFmpegVideo",
        "FFMPEG_VIDEO_TEXTURES_CREATED y=%u u=%u v=%u a=%u",
        textureSet_.planes[0],
        textureSet_.planes[1],
        textureSet_.planes[2],
        textureSet_.planes[3]
    );
}

bool FfmpegVideoTexture::uploadPlane(
    int index,
    GLenum internalFormat,
    GLenum format,
    GLenum type,
    int width,
    int height,
    const std::vector<uint8_t>& data
) {
    if (index < 0 || index >= 4 || textureSet_.planes[index] == 0 || width <= 0 || height <= 0 || data.empty()) {
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, textureSet_.planes[index]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const bool allocationChanged =
        allocatedWidths_[index] != width ||
        allocatedHeights_[index] != height ||
        allocatedInternalFormats_[index] != internalFormat;
    if (allocationChanged) {
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            static_cast<GLint>(internalFormat),
            width,
            height,
            0,
            format,
            type,
            nullptr
        );
        allocatedWidths_[index] = width;
        allocatedHeights_[index] = height;
        allocatedInternalFormats_[index] = internalFormat;
    }
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        width,
        height,
        format,
        type,
        data.data()
    );
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        XR_LOGE(
            "DDDVR/FFmpegVideo",
            "FFMPEG_VIDEO_TEXTURE_UPLOAD_FAILED plane=%d width=%d height=%d glErr=0x%x",
            index,
            width,
            height,
            err
        );
        return false;
    }
    return true;
}

bool FfmpegVideoTexture::upload(const FfmpegVideoFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0) return false;
    if (textureSet_.external) {
        destroyExternalImage();
        textureSet_.external = false;
        textureSet_.externalTexture = 0;
    }
    ensureTextures();

    const int width = frame.width;
    const int height = frame.height;
    const int chromaWidth = chromaSize(width);
    const int chromaHeight = chromaSize(height);
    bool ok = false;

    switch (frame.pixelFormat) {
        case FfmpegVideoPixelFormat::Yuv420P8:
            ok =
                hasPlane(frame, 0) &&
                hasPlane(frame, 1) &&
                hasPlane(frame, 2) &&
                uploadPlane(0, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, width, height, frame.planes[0]) &&
                uploadPlane(1, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, chromaWidth, chromaHeight, frame.planes[1]) &&
                uploadPlane(2, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, chromaWidth, chromaHeight, frame.planes[2]);
            break;
        case FfmpegVideoPixelFormat::Yuv420P10:
            ok =
                hasPlane(frame, 0) &&
                hasPlane(frame, 1) &&
                hasPlane(frame, 2) &&
                uploadPlane(0, GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, width, height, frame.planes[0]) &&
                uploadPlane(1, GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, chromaWidth, chromaHeight, frame.planes[1]) &&
                uploadPlane(2, GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, chromaWidth, chromaHeight, frame.planes[2]);
            break;
        case FfmpegVideoPixelFormat::P010:
            ok =
                hasPlane(frame, 0) &&
                hasPlane(frame, 1) &&
                uploadPlane(0, GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, width, height, frame.planes[0]) &&
                uploadPlane(1, GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT, chromaWidth, chromaHeight, frame.planes[1]);
            break;
        default:
            ok = false;
            break;
    }

    if (!ok) return false;

    const bool sizeChanged =
        textureSet_.width != frame.width ||
        textureSet_.height != frame.height ||
        textureSet_.pixelFormat != frame.pixelFormat;
    textureSet_.width = frame.width;
    textureSet_.height = frame.height;
    textureSet_.pixelFormat = frame.pixelFormat;
    textureSet_.transfer = frame.transfer;
    textureSet_.primaries = frame.primaries;
    textureSet_.range = frame.range == FfmpegVideoColorRange::Unknown
        ? FfmpegVideoColorRange::Limited
        : frame.range;
    textureSet_.dolbyProfile = frame.dolbyProfile;
    textureSet_.dolbyVision = frame.dolbyVision;
    textureSet_.dolbyMetadata = frame.dolbyMetadata;
    textureSet_.valid = true;

    static uint32_t uploadCount = 0;
    uploadCount += 1;
    if (sizeChanged || uploadCount <= 3 || uploadCount % 120 == 0) {
        XR_LOGI(
            "DDDVR/FFmpegVideo",
            "FFMPEG_VIDEO_FRAME_UPLOADED count=%u width=%d height=%d fmt=%d transfer=%d primaries=%d range=%d dovi=%d doviProfile=%d",
            uploadCount,
            frame.width,
            frame.height,
            static_cast<int>(frame.pixelFormat),
            static_cast<int>(frame.transfer),
            static_cast<int>(frame.primaries),
            static_cast<int>(textureSet_.range),
            frame.dolbyVision ? 1 : 0,
            frame.dolbyProfile
        );
    }
    return true;
}

bool FfmpegVideoTexture::importHardwareBuffer(FfmpegHardwareBufferFrame&& frame) {
    if (frame.image == nullptr || frame.buffer == nullptr ||
        frame.width <= 0 || frame.height <= 0) {
        if (frame.image != nullptr) dddvr::androidimage::imageDelete(frame.image);
        return false;
    }

    const EGLDisplay display = eglGetCurrentDisplay();
    const auto getNativeClientBuffer =
        reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(
            eglGetProcAddress("eglGetNativeClientBufferANDROID")
        );
    const auto createImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
        eglGetProcAddress("eglCreateImageKHR")
    );
    const auto imageTargetTexture =
        reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
            eglGetProcAddress("glEGLImageTargetTexture2DOES")
        );
    if (display == EGL_NO_DISPLAY || getNativeClientBuffer == nullptr ||
        createImage == nullptr || imageTargetTexture == nullptr) {
        XR_LOGE(
            "DDDVR/FFmpegVideo",
            "FFMPEG_VIDEO_AHB_IMPORT_UNSUPPORTED display=%p native=%p create=%p target=%p",
            display,
            getNativeClientBuffer,
            createImage,
            imageTargetTexture
        );
        dddvr::androidimage::imageDelete(frame.image);
        return false;
    }

    glFinish();
    destroyExternalImage();

    const EGLClientBuffer clientBuffer = getNativeClientBuffer(frame.buffer);
    const EGLint attributes[] = {
        EGL_IMAGE_PRESERVED_KHR,
        EGL_TRUE,
        EGL_NONE
    };
    const EGLImageKHR image = createImage(
        display,
        EGL_NO_CONTEXT,
        EGL_NATIVE_BUFFER_ANDROID,
        clientBuffer,
        attributes
    );
    if (image == EGL_NO_IMAGE_KHR) {
        XR_LOGE(
            "DDDVR/FFmpegVideo",
            "FFMPEG_VIDEO_AHB_EGLIMAGE_FAILED eglErr=0x%x",
            eglGetError()
        );
        dddvr::androidimage::imageDelete(frame.image);
        return false;
    }

    if (textureSet_.externalTexture == 0) {
        glGenTextures(1, &textureSet_.externalTexture);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureSet_.externalTexture);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureSet_.externalTexture);
    }
    imageTargetTexture(GL_TEXTURE_EXTERNAL_OES, image);
    const GLenum glError = glGetError();
    if (glError != GL_NO_ERROR) {
        const auto destroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
            eglGetProcAddress("eglDestroyImageKHR")
        );
        if (destroyImage != nullptr) destroyImage(display, image);
        dddvr::androidimage::imageDelete(frame.image);
        XR_LOGE(
            "DDDVR/FFmpegVideo",
            "FFMPEG_VIDEO_AHB_TEXTURE_BIND_FAILED glErr=0x%x",
            glError
        );
        return false;
    }

    AHardwareBuffer_Desc description{};
    dddvr::androidimage::hardwareBufferDescribe(frame.buffer, &description);
    externalImage_ = image;
    externalAImage_ = frame.image;
    frame.image = nullptr;
    frame.buffer = nullptr;

    const bool firstExternalFrame = !textureSet_.external;
    textureSet_.width = frame.width;
    textureSet_.height = frame.height;
    textureSet_.pixelFormat = FfmpegVideoPixelFormat::P010;
    textureSet_.transfer = frame.transfer;
    textureSet_.primaries = frame.primaries;
    textureSet_.range = frame.range == FfmpegVideoColorRange::Unknown
        ? FfmpegVideoColorRange::Limited
        : frame.range;
    textureSet_.dolbyProfile = frame.dolbyProfile;
    textureSet_.dolbyVision = frame.dolbyVision;
    textureSet_.dolbyMetadata = frame.dolbyMetadata;
    textureSet_.external = true;
    textureSet_.valid = true;

    static uint64_t importedFrames = 0;
    ++importedFrames;
    if (firstExternalFrame || importedFrames <= 3 || importedFrames % 120 == 0) {
        XR_LOGI(
            "DDDVR/FFmpegVideo",
            "FFMPEG_VIDEO_AHB_IMPORTED count=%llu size=%dx%d ahb=%ux%u stride=%u format=0x%x usage=0x%llx texture=%u doviMetadata=%d doviRevision=%llu doviHash=%llu",
            (unsigned long long)importedFrames,
            frame.width,
            frame.height,
            description.width,
            description.height,
            description.stride,
            description.format,
            (unsigned long long)description.usage,
            textureSet_.externalTexture,
            textureSet_.dolbyMetadata ? 1 : 0,
            textureSet_.dolbyMetadata
                ? static_cast<unsigned long long>(textureSet_.dolbyMetadata->revision)
                : 0ULL,
            textureSet_.dolbyMetadata
                ? static_cast<unsigned long long>(textureSet_.dolbyMetadata->mappingHash)
                : 0ULL
        );
    }
    return true;
}

void FfmpegVideoTexture::destroyExternalImage() {
    if (externalImage_ != EGL_NO_IMAGE_KHR) {
        const EGLDisplay display = eglGetCurrentDisplay();
        const auto destroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
            eglGetProcAddress("eglDestroyImageKHR")
        );
        if (display != EGL_NO_DISPLAY && destroyImage != nullptr) {
            destroyImage(display, externalImage_);
        }
        externalImage_ = EGL_NO_IMAGE_KHR;
    }
    if (externalAImage_ != nullptr) {
        dddvr::androidimage::imageDelete(externalAImage_);
        externalAImage_ = nullptr;
    }
}

void FfmpegVideoTexture::destroy() {
    if (externalImage_ != EGL_NO_IMAGE_KHR || externalAImage_ != nullptr) {
        glFinish();
        destroyExternalImage();
    }
    if (textureSet_.externalTexture != 0) {
        glDeleteTextures(1, &textureSet_.externalTexture);
    }
    if (textureSet_.planes[0] != 0 ||
        textureSet_.planes[1] != 0 ||
        textureSet_.planes[2] != 0 ||
        textureSet_.planes[3] != 0) {
        glDeleteTextures(4, textureSet_.planes);
    }
    textureSet_ = {};
    std::fill_n(allocatedWidths_, 4, 0);
    std::fill_n(allocatedHeights_, 4, 0);
    std::fill_n(allocatedInternalFormats_, 4, 0);
}
