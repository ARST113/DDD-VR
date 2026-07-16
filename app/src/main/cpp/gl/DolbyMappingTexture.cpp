#include "DolbyMappingTexture.h"
#include "../video/DolbyRpuParser.h"
#include "../util/XrLog.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr int kLut2DSize = 1024;
constexpr int kLut3DEdge = 16;

constexpr double kBt2020RgbToLmsHpe[9] = {
    0.4407958984375, 0.53533935546875, 0.0238037109375,
    0.1619873046875, 0.7586669921875, 0.079345703125,
    0.0, 0.0257568359375, 0.96877145767211914
};

uint64_t hashBytes(uint64_t hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

double rationalValue(const AVRational& value) {
    return value.den == 0
        ? 0.0
        : static_cast<double>(value.num) / static_cast<double>(value.den);
}

bool inverse3x3(const double input[9], double output[9]) {
    const double determinant =
        input[0] * (input[4] * input[8] - input[5] * input[7]) -
        input[1] * (input[3] * input[8] - input[5] * input[6]) +
        input[2] * (input[3] * input[7] - input[4] * input[6]);
    if (std::abs(determinant) < 1e-12) return false;
    const double inverseDeterminant = 1.0 / determinant;
    output[0] = (input[4] * input[8] - input[5] * input[7]) * inverseDeterminant;
    output[1] = (input[2] * input[7] - input[1] * input[8]) * inverseDeterminant;
    output[2] = (input[1] * input[5] - input[2] * input[4]) * inverseDeterminant;
    output[3] = (input[5] * input[6] - input[3] * input[8]) * inverseDeterminant;
    output[4] = (input[0] * input[8] - input[2] * input[6]) * inverseDeterminant;
    output[5] = (input[2] * input[3] - input[0] * input[5]) * inverseDeterminant;
    output[6] = (input[3] * input[7] - input[4] * input[6]) * inverseDeterminant;
    output[7] = (input[1] * input[6] - input[0] * input[7]) * inverseDeterminant;
    output[8] = (input[0] * input[4] - input[1] * input[3]) * inverseDeterminant;
    return true;
}

void multiply3x3(const double left[9], const double right[9], double output[9]) {
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            double value = 0.0;
            for (int inner = 0; inner < 3; ++inner) {
                value += left[row * 3 + inner] * right[inner * 3 + column];
            }
            output[row * 3 + column] = value;
        }
    }
}

double coefficientScale(int denominator) {
    return std::ldexp(1.0, denominator);
}

void buildIdentity(std::vector<float>& output) {
    output.resize(kLut2DSize);
    for (int i = 0; i < kLut2DSize; ++i) {
        output[i] = static_cast<float>(i) / static_cast<float>(kLut2DSize - 1);
    }
}

int chooseKind(const AVDOVIReshapingCurve& curve) {
    int polynomialPieces = 0;
    int mmrPieces = 0;
    for (int i = 0; i + 1 < curve.num_pivots; ++i) {
        if (curve.mapping_idc[i] == AV_DOVI_MAPPING_MMR) ++mmrPieces;
        if (curve.mapping_idc[i] == AV_DOVI_MAPPING_POLYNOMIAL) ++polynomialPieces;
    }
    if (mmrPieces == 1) return 2;
    if (polynomialPieces > 0) return 1;
    return 0;
}

void buildPolynomial(
    std::vector<float>& output,
    const AVDOVIReshapingCurve& curve,
    int bitDepth,
    int denominator
) {
    buildIdentity(output);
    if (curve.num_pivots <= 1) return;
    const double codeMaximum = static_cast<double>((uint64_t{1} << bitDepth) - 1);
    const double scale = coefficientScale(denominator);
    for (int sample = 0; sample < kLut2DSize; ++sample) {
        const double x = static_cast<double>(sample) / static_cast<double>(kLut2DSize - 1);
        int selectedPiece = -1;
        for (int piece = curve.num_pivots - 2; piece >= 0; --piece) {
            if (x >= static_cast<double>(curve.pivots[piece]) / codeMaximum) {
                selectedPiece = piece;
                break;
            }
        }
        if (selectedPiece < 0) continue;
        const double c0 = static_cast<double>(curve.poly_coef[selectedPiece][0]) / scale;
        const double c1 = static_cast<double>(curve.poly_coef[selectedPiece][1]) / scale;
        const double c2 = curve.poly_order[selectedPiece] == 2
            ? static_cast<double>(curve.poly_coef[selectedPiece][2]) / scale
            : 0.0;
        output[sample] = static_cast<float>(c0 + x * c1 + x * x * c2);
    }
}

double evaluateMmr(
    double x,
    double y,
    double z,
    const AVDOVIReshapingCurve& curve,
    double scale
) {
    const double basis[7] = {x, y, z, x * y, x * z, y * z, x * y * z};
    double result = static_cast<double>(curve.mmr_constant[0]) / scale;
    const int order = std::clamp<int>(curve.mmr_order[0], 0, 3);
    for (int row = 0; row < order; ++row) {
        for (int coefficient = 0; coefficient < 7; ++coefficient) {
            double term = basis[coefficient];
            if (row == 1) term *= term;
            if (row == 2) term = term * term * term;
            result += term * (static_cast<double>(curve.mmr_coef[0][row][coefficient]) / scale);
        }
    }
    return result;
}

void buildMmr(
    std::vector<float>& output,
    const AVDOVIReshapingCurve& curve,
    int denominator
) {
    output.clear();
    output.reserve(kLut3DEdge * kLut3DEdge * kLut3DEdge);
    const double scale = coefficientScale(denominator);
    for (int z = 0; z < kLut3DEdge; ++z) {
        for (int y = 0; y < kLut3DEdge; ++y) {
            for (int x = 0; x < kLut3DEdge; ++x) {
                output.push_back(static_cast<float>(evaluateMmr(
                    static_cast<double>(x) / (kLut3DEdge - 1),
                    static_cast<double>(y) / (kLut3DEdge - 1),
                    static_cast<double>(z) / (kLut3DEdge - 1),
                    curve,
                    scale
                )));
            }
        }
    }
}
}

void DolbyMappingTexture::updateColor(
    const std::shared_ptr<const DolbyRpuMetadata>& metadata
) {
    if (!metadata || !metadata->hasColor) {
        colorEnabled_ = false;
        colorHash_ = 0;
        blFullRange_ = false;
        return;
    }

    uint64_t hash = 1469598103934665603ULL;
    for (const AVRational& value : metadata->color.ycc_to_rgb_matrix) {
        hash = hashBytes(hash, &value.num, sizeof(value.num));
        hash = hashBytes(hash, &value.den, sizeof(value.den));
    }
    for (const AVRational& value : metadata->color.ycc_to_rgb_offset) {
        hash = hashBytes(hash, &value.num, sizeof(value.num));
        hash = hashBytes(hash, &value.den, sizeof(value.den));
    }
    for (const AVRational& value : metadata->color.rgb_to_lms_matrix) {
        hash = hashBytes(hash, &value.num, sizeof(value.num));
        hash = hashBytes(hash, &value.den, sizeof(value.den));
    }
    hash = hashBytes(
        hash,
        &metadata->header.bl_video_full_range_flag,
        sizeof(metadata->header.bl_video_full_range_flag)
    );
    if (colorEnabled_ && hash == colorHash_) return;

    double rgbToLms[9]{};
    double lmsToBt2020[9]{};
    double colorMatrix[9]{};
    for (int i = 0; i < 9; ++i) {
        yccToRgb_[i] = static_cast<GLfloat>(rationalValue(metadata->color.ycc_to_rgb_matrix[i]));
        rgbToLms[i] = rationalValue(metadata->color.rgb_to_lms_matrix[i]);
    }
    for (int i = 0; i < 3; ++i) {
        yccOffset_[i] = static_cast<GLfloat>(rationalValue(metadata->color.ycc_to_rgb_offset[i]));
    }
    if (!inverse3x3(kBt2020RgbToLmsHpe, lmsToBt2020)) {
        colorEnabled_ = false;
        return;
    }
    multiply3x3(rgbToLms, lmsToBt2020, colorMatrix);
    for (int i = 0; i < 9; ++i) {
        colorMatrix_[i] = static_cast<GLfloat>(colorMatrix[i]);
    }

    blFullRange_ = metadata->header.bl_video_full_range_flag != 0;
    colorEnabled_ = true;
    colorHash_ = hash;
    XR_LOGI(
        "DDDVR/FFmpegVideo",
        "XR_DOVI_COLOR_MATRIX revision=%llu hash=%llu fullRange=%d offset=(%.7f,%.7f,%.7f) ycc=(%.7f,%.7f,%.7f;%.7f,%.7f,%.7f;%.7f,%.7f,%.7f) linear=(%.7f,%.7f,%.7f;%.7f,%.7f,%.7f;%.7f,%.7f,%.7f)",
        static_cast<unsigned long long>(metadata->revision),
        static_cast<unsigned long long>(colorHash_),
        blFullRange_ ? 1 : 0,
        yccOffset_[0], yccOffset_[1], yccOffset_[2],
        yccToRgb_[0], yccToRgb_[1], yccToRgb_[2],
        yccToRgb_[3], yccToRgb_[4], yccToRgb_[5],
        yccToRgb_[6], yccToRgb_[7], yccToRgb_[8],
        colorMatrix_[0], colorMatrix_[1], colorMatrix_[2],
        colorMatrix_[3], colorMatrix_[4], colorMatrix_[5],
        colorMatrix_[6], colorMatrix_[7], colorMatrix_[8]
    );
}

bool DolbyMappingTexture::upload2D(int channel, const float* values, int count) {
    if (textures2D_[channel] == 0) glGenTextures(1, &textures2D_[channel]);
    glBindTexture(GL_TEXTURE_2D, textures2D_[channel]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, count, 1, 0, GL_RED, GL_FLOAT, values);
    return glGetError() == GL_NO_ERROR;
}

bool DolbyMappingTexture::upload3D(int channel, const float* values, int edge) {
    if (textures3D_[channel] == 0) glGenTextures(1, &textures3D_[channel]);
    glBindTexture(GL_TEXTURE_3D, textures3D_[channel]);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, edge, edge, edge, 0, GL_RED, GL_FLOAT, values);
    return glGetError() == GL_NO_ERROR;
}

bool DolbyMappingTexture::update(const std::shared_ptr<const DolbyRpuMetadata>& metadata) {
    updateColor(metadata);
    if (!metadata || !metadata->hasMapping) {
        enabled_ = false;
        return false;
    }
    if (enabled_ && metadata->mappingHash == mappingHash_) return true;

    std::vector<float> values;
    std::array<float, 3> minimums{};
    std::array<float, 3> centers{};
    std::array<float, 3> maximums{};
    bool success = true;
    for (int channel = 0; channel < 3; ++channel) {
        const auto& curve = metadata->mapping.curves[channel];
        kinds_[channel] = chooseKind(curve);
        if (kinds_[channel] == 2) {
            buildMmr(values, curve, metadata->header.coef_log2_denom);
            success = upload3D(channel, values.data(), kLut3DEdge) && success;
        } else {
            if (kinds_[channel] == 1) {
                buildPolynomial(
                    values,
                    curve,
                    metadata->header.bl_bit_depth,
                    metadata->header.coef_log2_denom
                );
            } else {
                buildIdentity(values);
            }
            success = upload2D(channel, values.data(), static_cast<int>(values.size())) && success;
        }
        const auto bounds = std::minmax_element(values.begin(), values.end());
        minimums[channel] = bounds.first != values.end() ? *bounds.first : 0.0f;
        maximums[channel] = bounds.second != values.end() ? *bounds.second : 0.0f;
        if (kinds_[channel] == 2) {
            const int midpoint = kLut3DEdge / 2;
            const size_t midpointIndex = static_cast<size_t>(
                (midpoint * kLut3DEdge + midpoint) * kLut3DEdge + midpoint
            );
            centers[channel] = midpointIndex < values.size() ? values[midpointIndex] : 0.0f;
        } else {
            centers[channel] = values.empty() ? 0.0f : values[values.size() / 2];
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindTexture(GL_TEXTURE_3D, 0);
    enabled_ = success;
    mappingHash_ = metadata->mappingHash;
    XR_LOGI(
        "DDDVR/FFmpegVideo",
        "XR_DOVI_LUT_UPDATE revision=%llu hash=%llu enabled=%d kinds=%d/%d/%d blDepth=%d denom=%d min=(%.7f,%.7f,%.7f) center=(%.7f,%.7f,%.7f) max=(%.7f,%.7f,%.7f)",
        static_cast<unsigned long long>(metadata->revision),
        static_cast<unsigned long long>(mappingHash_),
        enabled_ ? 1 : 0,
        kinds_[0],
        kinds_[1],
        kinds_[2],
        metadata->header.bl_bit_depth,
        metadata->header.coef_log2_denom,
        minimums[0], minimums[1], minimums[2],
        centers[0], centers[1], centers[2],
        maximums[0], maximums[1], maximums[2]
    );
    return enabled_;
}

void DolbyMappingTexture::bind(
    GLint enabledLocation,
    GLint kindLocation,
    const GLint sampler2D[3],
    const GLint sampler3D[3],
    GLint colorEnabledLocation,
    GLint yccToRgbLocation,
    GLint yccOffsetLocation,
    GLint colorMatrixLocation,
    GLint blFullRangeLocation
) {
    glUniform1i(enabledLocation, enabled_ ? 1 : 0);
    glUniform3i(kindLocation, kinds_[0], kinds_[1], kinds_[2]);
    glUniform1i(colorEnabledLocation, colorEnabled_ ? 1 : 0);
    glUniformMatrix3fv(yccToRgbLocation, 1, GL_FALSE, yccToRgb_.data());
    glUniform3fv(yccOffsetLocation, 1, yccOffset_.data());
    glUniformMatrix3fv(colorMatrixLocation, 1, GL_FALSE, colorMatrix_.data());
    glUniform1i(blFullRangeLocation, blFullRange_ ? 1 : 0);
    for (int channel = 0; channel < 3; ++channel) {
        glActiveTexture(GL_TEXTURE3 + channel);
        glBindTexture(GL_TEXTURE_2D, textures2D_[channel]);
        glUniform1i(sampler2D[channel], 3 + channel);
        glActiveTexture(GL_TEXTURE6 + channel);
        glBindTexture(GL_TEXTURE_3D, textures3D_[channel]);
        glUniform1i(sampler3D[channel], 6 + channel);
    }
}

void DolbyMappingTexture::destroy() {
    glDeleteTextures(3, textures2D_.data());
    glDeleteTextures(3, textures3D_.data());
    textures2D_ = {};
    textures3D_ = {};
    kinds_ = {};
    yccToRgb_ = {};
    yccOffset_ = {};
    colorMatrix_ = {};
    mappingHash_ = 0;
    colorHash_ = 0;
    enabled_ = false;
    colorEnabled_ = false;
    blFullRange_ = false;
}
