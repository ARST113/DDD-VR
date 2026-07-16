#pragma once

#include <GLES3/gl3.h>
#include <array>
#include <cstdint>
#include <memory>

struct DolbyRpuMetadata;

class DolbyMappingTexture {
public:
    bool update(const std::shared_ptr<const DolbyRpuMetadata>& metadata);
    void bind(
        GLint enabledLocation,
        GLint kindLocation,
        const GLint sampler2D[3],
        const GLint sampler3D[3],
        GLint colorEnabledLocation,
        GLint yccToRgbLocation,
        GLint yccOffsetLocation,
        GLint colorMatrixLocation,
        GLint blFullRangeLocation
    );
    void destroy();

    bool enabled() const { return enabled_; }
    uint64_t mappingHash() const { return mappingHash_; }

private:
    bool upload2D(int channel, const float* values, int count);
    bool upload3D(int channel, const float* values, int edge);
    void updateColor(const std::shared_ptr<const DolbyRpuMetadata>& metadata);

    std::array<GLuint, 3> textures2D_{};
    std::array<GLuint, 3> textures3D_{};
    std::array<int, 3> kinds_{};
    std::array<GLfloat, 9> yccToRgb_{};
    std::array<GLfloat, 3> yccOffset_{};
    std::array<GLfloat, 9> colorMatrix_{};
    uint64_t mappingHash_ = 0;
    uint64_t colorHash_ = 0;
    bool enabled_ = false;
    bool colorEnabled_ = false;
    bool blFullRange_ = false;
};
