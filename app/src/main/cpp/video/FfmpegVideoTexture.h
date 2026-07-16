#pragma once

#include "FfmpegVideoFrame.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

struct FfmpegVideoTextureSet {
    GLuint planes[4] = {0, 0, 0, 0};
    GLuint externalTexture = 0;
    int width = 0;
    int height = 0;
    FfmpegVideoPixelFormat pixelFormat = FfmpegVideoPixelFormat::Unknown;
    FfmpegVideoColorTransfer transfer = FfmpegVideoColorTransfer::Unknown;
    FfmpegVideoColorPrimaries primaries = FfmpegVideoColorPrimaries::Unknown;
    FfmpegVideoColorRange range = FfmpegVideoColorRange::Unknown;
    int dolbyProfile = 0;
    bool dolbyVision = false;
    std::shared_ptr<const DolbyRpuMetadata> dolbyMetadata;
    bool external = false;
    bool valid = false;
};

class FfmpegVideoTexture {
public:
    ~FfmpegVideoTexture();

    bool upload(const FfmpegVideoFrame& frame);
    bool importHardwareBuffer(FfmpegHardwareBufferFrame&& frame);
    const FfmpegVideoTextureSet& textureSet() const { return textureSet_; }
    bool hasFrame() const { return textureSet_.valid; }
    void destroy();

private:
    void ensureTextures();
    void configureTexture(GLuint texture, GLenum targetFormat);
    bool uploadPlane(
        int index,
        GLenum internalFormat,
        GLenum format,
        GLenum type,
        int width,
        int height,
        const std::vector<uint8_t>& data
    );
    void destroyExternalImage();

    FfmpegVideoTextureSet textureSet_{};
    EGLImageKHR externalImage_ = EGL_NO_IMAGE_KHR;
    AImage* externalAImage_ = nullptr;
    int allocatedWidths_[4] = {0, 0, 0, 0};
    int allocatedHeights_[4] = {0, 0, 0, 0};
    GLenum allocatedInternalFormats_[4] = {0, 0, 0, 0};
};
