#pragma once

#include <android/hardware_buffer.h>
#include <media/NdkImage.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct DolbyRpuMetadata;

enum class FfmpegVideoPixelFormat {
    Unknown = 0,
    Rgba8 = 1,
    Yuv420P8 = 2,
    P010 = 3,
    Yuv420P10 = 4
};

enum class FfmpegVideoColorTransfer {
    Unknown = 0,
    Sdr = 1,
    St2084 = 2,
    Hlg = 3
};

enum class FfmpegVideoColorPrimaries {
    Unknown = 0,
    Bt709 = 1,
    Bt2020 = 2
};

enum class FfmpegVideoColorRange {
    Unknown = 0,
    Limited = 1,
    Full = 2
};

struct FfmpegVideoFrame {
    int width = 0;
    int height = 0;
    int64_t ptsUs = 0;
    FfmpegVideoPixelFormat pixelFormat = FfmpegVideoPixelFormat::Unknown;
    FfmpegVideoColorTransfer transfer = FfmpegVideoColorTransfer::Unknown;
    FfmpegVideoColorPrimaries primaries = FfmpegVideoColorPrimaries::Unknown;
    FfmpegVideoColorRange range = FfmpegVideoColorRange::Unknown;
    int dolbyProfile = 0;
    bool dolbyVision = false;
    std::shared_ptr<const DolbyRpuMetadata> dolbyMetadata;
    std::vector<uint8_t> planes[4];
    const uint8_t* planeViews[4] = {nullptr, nullptr, nullptr, nullptr};
    size_t planeViewSizes[4] = {0, 0, 0, 0};
    int strides[4] = {0, 0, 0, 0};
};

struct FfmpegHardwareBufferFrame {
    AImage* image = nullptr;
    AHardwareBuffer* buffer = nullptr;
    int width = 0;
    int height = 0;
    int64_t ptsUs = 0;
    FfmpegVideoColorTransfer transfer = FfmpegVideoColorTransfer::Unknown;
    FfmpegVideoColorPrimaries primaries = FfmpegVideoColorPrimaries::Unknown;
    FfmpegVideoColorRange range = FfmpegVideoColorRange::Unknown;
    int dolbyProfile = 0;
    bool dolbyVision = false;
    std::shared_ptr<const DolbyRpuMetadata> dolbyMetadata;
};
