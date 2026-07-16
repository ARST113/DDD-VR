#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

extern "C" {
#include <libavutil/dovi_meta.h>
}

struct DolbyRpuMetadata {
    AVDOVIRpuDataHeader header{};
    AVDOVIDataMapping mapping{};
    AVDOVIColorMetadata color{};
    int64_t ptsUs = 0;
    uint64_t revision = 0;
    uint64_t mappingHash = 0;
    bool hasMapping = false;
    bool hasColor = false;
};

class DolbyRpuParser {
public:
    DolbyRpuParser();
    ~DolbyRpuParser();

    DolbyRpuParser(const DolbyRpuParser&) = delete;
    DolbyRpuParser& operator=(const DolbyRpuParser&) = delete;

    void reset(int dolbyProfile);
    std::shared_ptr<const DolbyRpuMetadata> parseAnnexB(
        const uint8_t* data,
        size_t size,
        int64_t ptsUs
    );

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
