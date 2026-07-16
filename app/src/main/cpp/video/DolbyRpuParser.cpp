#include "DolbyRpuParser.h"
#include "../util/XrLog.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace {
constexpr int kMaxDmId = 15;

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : data_(data), bitSize_(size * 8) {}

    uint64_t readBits(int count) {
        if (count < 0 || count > 64 || bitPosition_ + static_cast<size_t>(count) > bitSize_) {
            valid_ = false;
            return 0;
        }
        uint64_t value = 0;
        for (int i = 0; i < count; ++i) {
            const size_t byteIndex = bitPosition_ >> 3;
            const int bitIndex = 7 - static_cast<int>(bitPosition_ & 7);
            value = (value << 1) | ((data_[byteIndex] >> bitIndex) & 1U);
            ++bitPosition_;
        }
        return value;
    }

    int64_t readSignedBits(int count) {
        const uint64_t raw = readBits(count);
        if (!valid_ || count <= 0) return 0;
        const uint64_t sign = uint64_t{1} << (count - 1);
        return static_cast<int64_t>((raw ^ sign) - sign);
    }

    uint64_t readUnsignedGolomb() {
        int leadingZeroes = 0;
        while (valid_ && readBits(1) == 0) {
            if (++leadingZeroes >= 63) {
                valid_ = false;
                return 0;
            }
        }
        if (!valid_) return 0;
        const uint64_t suffix = leadingZeroes > 0 ? readBits(leadingZeroes) : 0;
        if (!valid_) return 0;
        return ((uint64_t{1} << leadingZeroes) - 1) + suffix;
    }

    int64_t readSignedGolomb() {
        const uint64_t code = readUnsignedGolomb();
        if (!valid_ || code > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) return 0;
        return (code & 1U)
            ? static_cast<int64_t>((code + 1) >> 1)
            : -static_cast<int64_t>(code >> 1);
    }

    void skipBits(int count) { (void)readBits(count); }
    bool valid() const { return valid_; }

private:
    const uint8_t* data_ = nullptr;
    size_t bitSize_ = 0;
    size_t bitPosition_ = 0;
    bool valid_ = true;
};

std::vector<uint8_t> unescapeRbsp(const uint8_t* data, size_t size) {
    std::vector<uint8_t> rbsp;
    rbsp.reserve(size);
    int zeroCount = 0;
    for (size_t i = 0; i < size; ++i) {
        const uint8_t value = data[i];
        if (zeroCount >= 2 && value == 0x03) {
            zeroCount = 0;
            continue;
        }
        rbsp.push_back(value);
        zeroCount = value == 0 ? zeroCount + 1 : 0;
    }
    return rbsp;
}

size_t findStartCode(const uint8_t* data, size_t size, size_t from, size_t* prefixSize) {
    for (size_t i = from; i + 3 <= size; ++i) {
        if (data[i] != 0 || data[i + 1] != 0) continue;
        if (data[i + 2] == 1) {
            *prefixSize = 3;
            return i;
        }
        if (i + 4 <= size && data[i + 2] == 0 && data[i + 3] == 1) {
            *prefixSize = 4;
            return i;
        }
    }
    return size;
}

int mappingKind(const AVDOVIReshapingCurve& curve) {
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

uint64_t hashBytes(uint64_t hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}
}

struct DolbyRpuParser::Impl {
    struct VdrState {
        AVDOVIDataMapping mapping{};
        AVDOVIColorMetadata color{};
        bool hasMapping = false;
        bool hasColor = false;
    };

    std::array<std::unique_ptr<VdrState>, kMaxDmId + 1> states{};
    AVDOVIRpuDataHeader header{};
    const AVDOVIDataMapping* activeMapping = nullptr;
    const AVDOVIColorMetadata* activeColor = nullptr;
    int profile = 0;
    uint64_t revision = 0;
    uint64_t parsedRpus = 0;
    uint64_t parseFailures = 0;

    void clearState() {
        for (auto& state : states) state.reset();
        header = {};
        activeMapping = nullptr;
        activeColor = nullptr;
    }

    bool fail(const char* reason) {
        ++parseFailures;
        if (parseFailures <= 5 || parseFailures % 120 == 0) {
            XR_LOGW(
                "DDDVR/DolbyRpu",
                "DOVI_RPU_PARSE_FAILED count=%llu reason=%s",
                static_cast<unsigned long long>(parseFailures),
                reason
            );
        }
        clearState();
        return false;
    }

    static bool inRange(uint64_t value, uint64_t minimum, uint64_t maximum) {
        return value >= minimum && value <= maximum;
    }

    static int guessedProfile(const AVDOVIRpuDataHeader& value) {
        if (value.vdr_rpu_profile == 0 && value.bl_video_full_range_flag) return 5;
        if (value.vdr_rpu_profile == 1) {
            if (value.el_spatial_resampling_filter_flag && !value.disable_residual_flag) {
                return value.vdr_bit_depth == 12 ? 7 : 4;
            }
            return 8;
        }
        return 0;
    }

    static uint64_t readUnsignedCoefficient(BitReader& bits, const AVDOVIRpuDataHeader& value) {
        if (value.coef_data_type == 0) {
            const uint64_t integerPart = bits.readUnsignedGolomb();
            const uint64_t fractionalPart = bits.readBits(value.coef_log2_denom);
            return (integerPart << value.coef_log2_denom) + fractionalPart;
        }
        const uint32_t raw = static_cast<uint32_t>(bits.readBits(32));
        float floating = 0.0f;
        std::memcpy(&floating, &raw, sizeof(floating));
        return static_cast<uint64_t>(floating * static_cast<double>(uint64_t{1} << value.coef_log2_denom));
    }

    static int64_t readSignedCoefficient(BitReader& bits, const AVDOVIRpuDataHeader& value) {
        if (value.coef_data_type == 0) {
            const int64_t integerPart = bits.readSignedGolomb();
            const uint64_t fractionalPart = bits.readBits(value.coef_log2_denom);
            return integerPart * static_cast<int64_t>(uint64_t{1} << value.coef_log2_denom) +
                static_cast<int64_t>(fractionalPart);
        }
        const uint32_t raw = static_cast<uint32_t>(bits.readBits(32));
        float floating = 0.0f;
        std::memcpy(&floating, &raw, sizeof(floating));
        return static_cast<int64_t>(floating * static_cast<double>(uint64_t{1} << value.coef_log2_denom));
    }

    bool parseRpu(const uint8_t* data, size_t size) {
        BitReader bits(data, size);
        const uint64_t nalPrefix = bits.readBits(8);
        if (nalPrefix != 25) return fail("nal_prefix");

        const uint64_t rpuType = bits.readBits(6);
        if (rpuType != 2) return fail("rpu_type");
        header.rpu_type = static_cast<uint8_t>(rpuType);
        header.rpu_format = static_cast<uint16_t>(bits.readBits(11));
        header.vdr_rpu_profile = static_cast<uint8_t>(bits.readBits(4));
        header.vdr_rpu_level = static_cast<uint8_t>(bits.readBits(4));

        const bool sequenceInfoPresent = bits.readBits(1) != 0;
        if (sequenceInfoPresent) {
            header.chroma_resampling_explicit_filter_flag = static_cast<uint8_t>(bits.readBits(1));
            header.coef_data_type = static_cast<uint8_t>(bits.readBits(2));
            if (!inRange(header.coef_data_type, 0, 1)) return fail("coef_data_type");
            if (header.coef_data_type == 0) {
                const uint64_t denominator = bits.readUnsignedGolomb();
                if (!inRange(denominator, 13, 32)) return fail("coef_log2_denom");
                header.coef_log2_denom = static_cast<uint8_t>(denominator);
            } else {
                header.coef_log2_denom = 32;
            }
            header.vdr_rpu_normalized_idc = static_cast<uint8_t>(bits.readBits(2));
            header.bl_video_full_range_flag = static_cast<uint8_t>(bits.readBits(1));
            if ((header.rpu_format & 0x700) == 0) {
                const uint64_t blDepth = bits.readUnsignedGolomb();
                const uint64_t elDepth = bits.readUnsignedGolomb();
                const uint64_t vdrDepth = bits.readUnsignedGolomb();
                if (!inRange(blDepth, 0, 8) || !inRange(elDepth, 0, 8) || !inRange(vdrDepth, 0, 8)) {
                    return fail("bit_depth");
                }
                header.bl_bit_depth = static_cast<uint8_t>(blDepth + 8);
                header.el_bit_depth = static_cast<uint8_t>(elDepth + 8);
                header.vdr_bit_depth = static_cast<uint8_t>(vdrDepth + 8);
                header.spatial_resampling_filter_flag = static_cast<uint8_t>(bits.readBits(1));
                bits.skipBits(3);
                header.el_spatial_resampling_filter_flag = static_cast<uint8_t>(bits.readBits(1));
                header.disable_residual_flag = static_cast<uint8_t>(bits.readBits(1));
            }
        }
        if (!bits.valid() || header.bl_bit_depth == 0) return fail("sequence_info");

        const bool dmMetadataPresent = bits.readBits(1) != 0;
        const bool usePreviousRpu = bits.readBits(1) != 0;
        const bool useNlq = (header.rpu_format & 0x700) == 0 && !header.disable_residual_flag;
        const int activeProfile = profile != 0 ? profile : guessedProfile(header);
        if (activeProfile == 5 && useNlq) return fail("profile5_nlq");

        VdrState* current = nullptr;
        if (usePreviousRpu) {
            const uint64_t previousId = bits.readUnsignedGolomb();
            if (!inRange(previousId, 0, kMaxDmId) || !states[previousId]) return fail("previous_rpu_id");
            current = states[previousId].get();
            activeMapping = current->hasMapping ? &current->mapping : nullptr;
        } else {
            const uint64_t rpuId = bits.readUnsignedGolomb();
            if (!inRange(rpuId, 0, kMaxDmId)) return fail("rpu_id");
            if (!states[rpuId]) states[rpuId] = std::make_unique<VdrState>();
            current = states[rpuId].get();
            current->mapping = {};
            current->mapping.vdr_rpu_id = static_cast<uint8_t>(rpuId);
            current->mapping.mapping_color_space = static_cast<uint8_t>(bits.readUnsignedGolomb());
            current->mapping.mapping_chroma_format_idc = static_cast<uint8_t>(bits.readUnsignedGolomb());

            for (int channel = 0; channel < 3; ++channel) {
                auto& curve = current->mapping.curves[channel];
                const uint64_t pivotsMinusTwo = bits.readUnsignedGolomb();
                if (!inRange(pivotsMinusTwo, 0, AV_DOVI_MAX_PIECES - 1)) return fail("num_pivots");
                curve.num_pivots = static_cast<uint8_t>(pivotsMinusTwo + 2);
                uint32_t pivot = 0;
                for (int i = 0; i < curve.num_pivots; ++i) {
                    pivot += static_cast<uint32_t>(bits.readBits(header.bl_bit_depth));
                    curve.pivots[i] = static_cast<uint16_t>(std::min<uint32_t>(pivot, 65535));
                }
            }

            if (useNlq) {
                const uint64_t method = bits.readBits(3);
                if (method > AV_DOVI_NLQ_LINEAR_DZ) return fail("nlq_method");
                current->mapping.nlq_method_idc = static_cast<AVDOVINLQMethod>(method);
            } else {
                current->mapping.nlq_method_idc = AV_DOVI_NLQ_NONE;
            }
            current->mapping.num_x_partitions = static_cast<uint32_t>(bits.readUnsignedGolomb() + 1);
            current->mapping.num_y_partitions = static_cast<uint32_t>(bits.readUnsignedGolomb() + 1);

            for (int channel = 0; channel < 3; ++channel) {
                auto& curve = current->mapping.curves[channel];
                for (int piece = 0; piece + 1 < curve.num_pivots; ++piece) {
                    const uint64_t method = bits.readUnsignedGolomb();
                    if (method > AV_DOVI_MAPPING_MMR) return fail("mapping_idc");
                    curve.mapping_idc[piece] = static_cast<AVDOVIMappingMethod>(method);
                    if (method == AV_DOVI_MAPPING_POLYNOMIAL) {
                        const uint64_t orderMinusOne = bits.readUnsignedGolomb();
                        if (orderMinusOne > 1) return fail("poly_order");
                        curve.poly_order[piece] = static_cast<uint8_t>(orderMinusOne + 1);
                        if (orderMinusOne == 0 && bits.readBits(1) != 0) return fail("poly_linear_interp");
                        for (int coefficient = 0; coefficient <= curve.poly_order[piece]; ++coefficient) {
                            curve.poly_coef[piece][coefficient] = readSignedCoefficient(bits, header);
                        }
                    } else {
                        const uint64_t orderMinusOne = bits.readBits(2);
                        if (orderMinusOne > 2) return fail("mmr_order");
                        curve.mmr_order[piece] = static_cast<uint8_t>(orderMinusOne + 1);
                        curve.mmr_constant[piece] = readSignedCoefficient(bits, header);
                        for (int order = 0; order < curve.mmr_order[piece]; ++order) {
                            for (int coefficient = 0; coefficient < 7; ++coefficient) {
                                curve.mmr_coef[piece][order][coefficient] = readSignedCoefficient(bits, header);
                            }
                        }
                    }
                }
            }

            if (useNlq) {
                for (int channel = 0; channel < 3; ++channel) {
                    auto& nlq = current->mapping.nlq[channel];
                    nlq.nlq_offset = static_cast<uint16_t>(bits.readBits(header.el_bit_depth));
                    nlq.vdr_in_max = readUnsignedCoefficient(bits, header);
                    nlq.linear_deadzone_slope = readUnsignedCoefficient(bits, header);
                    nlq.linear_deadzone_threshold = readUnsignedCoefficient(bits, header);
                }
            }
            current->hasMapping = bits.valid();
            activeMapping = current->hasMapping ? &current->mapping : nullptr;
        }

        if (dmMetadataPresent) {
            const uint64_t affectedId = bits.readUnsignedGolomb();
            const uint64_t currentId = bits.readUnsignedGolomb();
            if (!inRange(affectedId, 0, kMaxDmId) || !inRange(currentId, 0, kMaxDmId)) {
                return fail("dm_id");
            }
            if (!states[affectedId]) states[affectedId] = std::make_unique<VdrState>();
            if (!states[currentId]) return fail("current_dm_id");
            activeColor = states[currentId]->hasColor ? &states[currentId]->color : nullptr;

            auto& colorState = *states[affectedId];
            auto& color = colorState.color;
            color = {};
            color.dm_metadata_id = static_cast<uint8_t>(affectedId);
            color.scene_refresh_flag = static_cast<uint8_t>(bits.readUnsignedGolomb());
            for (auto& coefficient : color.ycc_to_rgb_matrix) {
                coefficient = AVRational{static_cast<int>(bits.readSignedBits(16)), 1 << 13};
            }
            for (auto& offset : color.ycc_to_rgb_offset) {
                int denominator = activeProfile == 4 ? (1 << 30) : (1 << 28);
                uint64_t numerator = bits.readBits(32);
                if (numerator > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
                    numerator >>= 1;
                    denominator >>= 1;
                }
                offset = AVRational{static_cast<int>(numerator), denominator};
            }
            for (auto& coefficient : color.rgb_to_lms_matrix) {
                coefficient = AVRational{static_cast<int>(bits.readSignedBits(16)), 1 << 14};
            }
            color.signal_eotf = static_cast<uint16_t>(bits.readBits(16));
            color.signal_eotf_param0 = static_cast<uint16_t>(bits.readBits(16));
            color.signal_eotf_param1 = static_cast<uint16_t>(bits.readBits(16));
            color.signal_eotf_param2 = static_cast<uint32_t>(bits.readBits(32));
            color.signal_bit_depth = static_cast<uint8_t>(bits.readBits(5));
            if (!inRange(color.signal_bit_depth, 8, 16)) return fail("signal_bit_depth");
            color.signal_color_space = static_cast<uint8_t>(bits.readBits(2));
            color.signal_chroma_format = static_cast<uint8_t>(bits.readBits(2));
            color.signal_full_range_flag = static_cast<uint8_t>(bits.readBits(2));
            color.source_min_pq = static_cast<uint16_t>(bits.readBits(12));
            color.source_max_pq = static_cast<uint16_t>(bits.readBits(12));
            color.source_diagonal = static_cast<uint16_t>(bits.readBits(10));
            colorState.hasColor = bits.valid();
            activeColor = states[currentId]->hasColor ? &states[currentId]->color : nullptr;
        }

        if (!bits.valid() || activeMapping == nullptr) return fail("truncated_or_no_mapping");
        ++parsedRpus;
        return true;
    }

    std::shared_ptr<const DolbyRpuMetadata> parseAnnexB(
        const uint8_t* data,
        size_t size,
        int64_t ptsUs
    ) {
        std::shared_ptr<const DolbyRpuMetadata> result;
        size_t cursor = 0;
        while (cursor < size) {
            size_t prefixSize = 0;
            const size_t startCode = findStartCode(data, size, cursor, &prefixSize);
            if (startCode == size) break;
            const size_t nalStart = startCode + prefixSize;
            size_t nextPrefixSize = 0;
            const size_t nextStart = findStartCode(data, size, nalStart, &nextPrefixSize);
            const size_t nalEnd = nextStart == size ? size : nextStart;
            cursor = nalEnd;
            if (nalEnd <= nalStart + 2) continue;
            const int nalType = (data[nalStart] >> 1) & 0x3f;
            if (nalType != 62) continue;

            const auto rbsp = unescapeRbsp(data + nalStart + 2, nalEnd - nalStart - 2);
            if (rbsp.empty() || !parseRpu(rbsp.data(), rbsp.size())) continue;

            auto metadata = std::make_shared<DolbyRpuMetadata>();
            metadata->header = header;
            metadata->mapping = *activeMapping;
            metadata->hasMapping = true;
            if (activeColor != nullptr) {
                metadata->color = *activeColor;
                metadata->hasColor = true;
            }
            metadata->ptsUs = ptsUs;
            metadata->revision = ++revision;
            uint64_t mappingHash = 1469598103934665603ULL;
            mappingHash = hashBytes(
                mappingHash,
                &metadata->header.bl_bit_depth,
                sizeof(metadata->header.bl_bit_depth)
            );
            mappingHash = hashBytes(
                mappingHash,
                &metadata->header.coef_log2_denom,
                sizeof(metadata->header.coef_log2_denom)
            );
            mappingHash = hashBytes(mappingHash, &metadata->mapping, sizeof(metadata->mapping));
            metadata->mappingHash = mappingHash;
            result = metadata;

            if (metadata->revision <= 4 || metadata->revision % 120 == 0) {
                XR_LOGI(
                    "DDDVR/DolbyRpu",
                    "DOVI_RPU_MAPPING revision=%llu ptsUs=%lld profile=%d blDepth=%d denom=%d color=%d curves=%d/%d/%d pivots=%d/%d/%d sourcePq=%d..%d",
                    static_cast<unsigned long long>(metadata->revision),
                    static_cast<long long>(ptsUs),
                    profile,
                    metadata->header.bl_bit_depth,
                    metadata->header.coef_log2_denom,
                    metadata->hasColor ? 1 : 0,
                    mappingKind(metadata->mapping.curves[0]),
                    mappingKind(metadata->mapping.curves[1]),
                    mappingKind(metadata->mapping.curves[2]),
                    metadata->mapping.curves[0].num_pivots,
                    metadata->mapping.curves[1].num_pivots,
                    metadata->mapping.curves[2].num_pivots,
                    metadata->hasColor ? metadata->color.source_min_pq : 0,
                    metadata->hasColor ? metadata->color.source_max_pq : 0
                );
            }
        }
        return result;
    }
};

DolbyRpuParser::DolbyRpuParser() : impl_(std::make_unique<Impl>()) {}
DolbyRpuParser::~DolbyRpuParser() = default;

void DolbyRpuParser::reset(int dolbyProfile) {
    impl_->clearState();
    impl_->profile = dolbyProfile;
    impl_->revision = 0;
    impl_->parsedRpus = 0;
    impl_->parseFailures = 0;
}

std::shared_ptr<const DolbyRpuMetadata> DolbyRpuParser::parseAnnexB(
    const uint8_t* data,
    size_t size,
    int64_t ptsUs
) {
    if (data == nullptr || size == 0) return nullptr;
    return impl_->parseAnnexB(data, size, ptsUs);
}
