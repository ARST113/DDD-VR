#include "FfmpegVideoDecoder.h"
#include "AndroidImageApi.h"
#include "../util/XrLog.h"

#include <android/native_window_jni.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <utility>

#ifndef DDDVR_HAS_FFMPEG_VIDEO
#define DDDVR_HAS_FFMPEG_VIDEO 0
#endif

#if DDDVR_HAS_FFMPEG_VIDEO
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavcodec/jni.h>
#include <libavcodec/mediacodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#endif

namespace {
constexpr size_t kMaxQueuedFrames = 1;
constexpr int64_t kSeekPrerollToleranceUs = 50000;
constexpr int64_t kVideoLateDropThresholdUs = 120000;
constexpr int kMaxSoftwareFrameWidth = 1920;
constexpr int kMaxSoftwareFrameHeight = 1080;
constexpr int kSoftwareDecoderThreads = 2;
constexpr int kAudioOutputSampleRate = 48000;
constexpr int kAudioOutputChannels = 2;
constexpr size_t kMaxQueuedAudioPackets = 256;
constexpr size_t kMaxQueuedVideoPackets = 240;
constexpr auto kStatsInterval = std::chrono::seconds(5);
constexpr int32_t kMediaCodecColorFormatP010 = 54;

void onP010BufferRemoved(void*, AImageReader*, AHardwareBuffer*) {
}

void sleepBriefly() {
    std::this_thread::sleep_for(std::chrono::milliseconds(6));
}

#if DDDVR_HAS_FFMPEG_VIDEO
const char* hardwareDecoderName(AVCodecID codecId) {
    switch (codecId) {
        case AV_CODEC_ID_H264:
            return "h264_mediacodec";
        case AV_CODEC_ID_HEVC:
            return "hevc_mediacodec";
        case AV_CODEC_ID_VP8:
            return "vp8_mediacodec";
        case AV_CODEC_ID_VP9:
            return "vp9_mediacodec";
#if defined(AV_CODEC_ID_AV1)
        case AV_CODEC_ID_AV1:
            return "av1_mediacodec";
#endif
        case AV_CODEC_ID_MPEG2VIDEO:
            return "mpeg2_mediacodec";
        case AV_CODEC_ID_MPEG4:
            return "mpeg4_mediacodec";
        default:
            return nullptr;
    }
}

const char* mediaCodecMime(AVCodecID codecId) {
    switch (codecId) {
        case AV_CODEC_ID_H264:
            return "video/avc";
        case AV_CODEC_ID_HEVC:
            return "video/hevc";
        case AV_CODEC_ID_VP8:
            return "video/x-vnd.on2.vp8";
        case AV_CODEC_ID_VP9:
            return "video/x-vnd.on2.vp9";
#if defined(AV_CODEC_ID_AV1)
        case AV_CODEC_ID_AV1:
            return "video/av01";
#endif
        case AV_CODEC_ID_MPEG2VIDEO:
            return "video/mpeg2";
        case AV_CODEC_ID_MPEG4:
            return "video/mp4v-es";
        default:
            return nullptr;
    }
}

const char* annexBFilterName(AVCodecID codecId) {
    switch (codecId) {
        case AV_CODEC_ID_H264:
            return "h264_mp4toannexb";
        case AV_CODEC_ID_HEVC:
            return "hevc_mp4toannexb";
        default:
            return nullptr;
    }
}

int mediaCodecColorStandard(AVColorPrimaries primaries, AVColorSpace colorSpace) {
    if (primaries == AVCOL_PRI_BT2020 ||
        colorSpace == AVCOL_SPC_BT2020_NCL ||
        colorSpace == AVCOL_SPC_BT2020_CL) {
        return 6;
    }
    if (primaries == AVCOL_PRI_BT709 || colorSpace == AVCOL_SPC_BT709) return 1;
    if (colorSpace == AVCOL_SPC_BT470BG) return 2;
    if (colorSpace == AVCOL_SPC_SMPTE170M) return 4;
    return 0;
}

int mediaCodecColorTransfer(AVColorTransferCharacteristic transfer) {
    switch (transfer) {
        case AVCOL_TRC_SMPTE2084:
            return 6;
        case AVCOL_TRC_ARIB_STD_B67:
            return 7;
        case AVCOL_TRC_LINEAR:
            return 1;
        case AVCOL_TRC_BT709:
        case AVCOL_TRC_SMPTE170M:
            return 3;
        default:
            return 0;
    }
}

int mediaCodecColorRange(AVColorRange range) {
    if (range == AVCOL_RANGE_JPEG) return 1;
    if (range == AVCOL_RANGE_MPEG) return 2;
    return 0;
}

bool startsWithAnnexB(const uint8_t* data, int size) {
    if (data == nullptr || size < 3) return false;
    return (data[0] == 0 && data[1] == 0 && data[2] == 1) ||
        (size >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1);
}

const char* hdrKind(AVColorTransferCharacteristic transfer, bool dolbyVision) {
    if (dolbyVision) return "dolby_vision";
    if (transfer == AVCOL_TRC_SMPTE2084) return "pq";
    if (transfer == AVCOL_TRC_ARIB_STD_B67) return "hlg";
    return "sdr";
}

bool needsSoftwareScale(const AVFrame* frame) {
    return frame != nullptr &&
        (frame->width > kMaxSoftwareFrameWidth || frame->height > kMaxSoftwareFrameHeight);
}

int evenDimension(float value) {
    const int rounded = std::max(2, static_cast<int>(std::floor(value)));
    return rounded & ~1;
}

void scaledOutputSize(const AVFrame* frame, int* outWidth, int* outHeight) {
    if (frame == nullptr || outWidth == nullptr || outHeight == nullptr) return;
    const float widthScale = static_cast<float>(kMaxSoftwareFrameWidth) / static_cast<float>(frame->width);
    const float heightScale = static_cast<float>(kMaxSoftwareFrameHeight) / static_cast<float>(frame->height);
    const float scale = std::min(1.0f, std::min(widthScale, heightScale));
    *outWidth = evenDimension(static_cast<float>(frame->width) * scale);
    *outHeight = evenDimension(static_cast<float>(frame->height) * scale);
}

FfmpegVideoColorTransfer mapTransfer(AVColorTransferCharacteristic value) {
    switch (value) {
        case AVCOL_TRC_SMPTE2084:
            return FfmpegVideoColorTransfer::St2084;
        case AVCOL_TRC_ARIB_STD_B67:
            return FfmpegVideoColorTransfer::Hlg;
        case AVCOL_TRC_BT709:
        case AVCOL_TRC_SMPTE170M:
        case AVCOL_TRC_GAMMA22:
        case AVCOL_TRC_GAMMA28:
            return FfmpegVideoColorTransfer::Sdr;
        default:
            return FfmpegVideoColorTransfer::Unknown;
    }
}

FfmpegVideoColorPrimaries mapPrimaries(AVColorPrimaries value) {
    switch (value) {
        case AVCOL_PRI_BT709:
            return FfmpegVideoColorPrimaries::Bt709;
        case AVCOL_PRI_BT2020:
            return FfmpegVideoColorPrimaries::Bt2020;
        default:
            return FfmpegVideoColorPrimaries::Unknown;
    }
}

FfmpegVideoColorRange mapRange(AVColorRange value) {
    switch (value) {
        case AVCOL_RANGE_JPEG:
            return FfmpegVideoColorRange::Full;
        case AVCOL_RANGE_MPEG:
            return FfmpegVideoColorRange::Limited;
        default:
            return FfmpegVideoColorRange::Unknown;
    }
}

bool hasDolbyVisionSideData(const AVFrame* frame) {
#if defined(AV_FRAME_DATA_DOVI_METADATA)
    if (frame != nullptr && av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA) != nullptr) {
        return true;
    }
#endif
    return false;
}

int dolbyVisionProfile(const AVCodecParameters* parameters) {
    if (parameters == nullptr) return 0;
    const AVPacketSideData* sideData = av_packet_side_data_get(
        parameters->coded_side_data,
        parameters->nb_coded_side_data,
        AV_PKT_DATA_DOVI_CONF
    );
    if (sideData == nullptr ||
        sideData->data == nullptr ||
        sideData->size < sizeof(AVDOVIDecoderConfigurationRecord)) {
        return 0;
    }
    const auto* configuration =
        reinterpret_cast<const AVDOVIDecoderConfigurationRecord*>(sideData->data);
    return static_cast<int>(configuration->dv_profile);
}

std::string ffError(int err) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buffer, sizeof(buffer));
    return std::string(buffer);
}

std::string dictionaryValue(AVDictionary* metadata, const char* key) {
    const AVDictionaryEntry* entry = av_dict_get(metadata, key, nullptr, 0);
    return entry != nullptr && entry->value != nullptr ? entry->value : "";
}

std::string localizedLanguage(const std::string& language) {
    if (language == "rus" || language == "ru") return "Русский";
    if (language == "ukr" || language == "uk" || language == "ua") return "Украинский";
    if (language == "eng" || language == "en") return "Английский";
    if (language == "deu" || language == "ger" || language == "de") return "Немецкий";
    if (language == "fra" || language == "fre" || language == "fr") return "Французский";
    if (language == "spa" || language == "es") return "Испанский";
    return language;
}

std::string upperAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

std::string audioChannelLabel(const AVCodecParameters* parameters) {
    if (parameters == nullptr) return "";
    const int channels = parameters->ch_layout.nb_channels;
    if (channels == 1) return "1.0";
    if (channels == 2) return "2.0";
    if (channels == 6) return "5.1";
    if (channels == 8) return "7.1";
    return channels > 0 ? std::to_string(channels) + " ch" : "";
}

FfmpegAudioTrackInfo buildAudioTrackInfo(AVStream* stream, int streamIndex, bool selected) {
    FfmpegAudioTrackInfo result{};
    result.streamIndex = streamIndex;
    result.id = "ffmpeg_audio:" + std::to_string(streamIndex);
    result.selected = selected;
    const std::string metadataTitle = dictionaryValue(stream->metadata, "title");
    const std::string language = localizedLanguage(dictionaryValue(stream->metadata, "language"));
    result.title = !metadataTitle.empty()
        ? metadataTitle
        : (!language.empty() ? language : "Аудио " + std::to_string(streamIndex));
    const std::string codec = upperAscii(avcodec_get_name(stream->codecpar->codec_id));
    const std::string channels = audioChannelLabel(stream->codecpar);
    result.subtitle = codec;
    if (!channels.empty()) {
        if (!result.subtitle.empty()) result.subtitle += " ";
        result.subtitle += channels;
    }
    return result;
}

int64_t framePtsUs(const AVFrame* frame, AVRational timeBase) {
    if (frame == nullptr) return 0;
    const int64_t pts = frame->best_effort_timestamp != AV_NOPTS_VALUE
        ? frame->best_effort_timestamp
        : frame->pts;
    if (pts == AV_NOPTS_VALUE) return 0;
    return av_rescale_q(pts, timeBase, AVRational{1, 1000000});
}

void copyPlane(
    std::vector<uint8_t>& out,
    const uint8_t* src,
    int srcStride,
    int rowBytes,
    int rows
) {
    if (src == nullptr || srcStride <= 0 || rowBytes <= 0 || rows <= 0) {
        out.clear();
        return;
    }
    out.resize(static_cast<size_t>(rowBytes) * static_cast<size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        std::memcpy(
            out.data() + static_cast<size_t>(row) * static_cast<size_t>(rowBytes),
            src + static_cast<size_t>(row) * static_cast<size_t>(srcStride),
            static_cast<size_t>(rowBytes)
        );
    }
}

bool copyMediaCodecP010(
    const uint8_t* data,
    size_t dataSize,
    int width,
    int height,
    int strideBytes,
    int sliceHeight,
    int64_t ptsUs,
    const AVCodecParameters* parameters,
    int dolbyProfile,
    FfmpegVideoFrame* out
) {
    if (data == nullptr || out == nullptr || parameters == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    strideBytes = std::max(width * 2, strideBytes);
    sliceHeight = std::max(height, sliceHeight);
    const int sourceStrideBytes = strideBytes;
    const int yRowBytes = width * 2;
    const int chromaRows = (height + 1) / 2;
    const size_t yStorageBytes =
        static_cast<size_t>(sourceStrideBytes) * static_cast<size_t>(sliceHeight);
    const size_t requiredBytes = yStorageBytes +
        static_cast<size_t>(sourceStrideBytes) * static_cast<size_t>(chromaRows);
    if (dataSize < requiredBytes) {
        XR_LOGW(
            "DDDVR/FFmpegVideo",
            "FFMPEG_VIDEO_P010_BUFFER_TOO_SMALL size=%zu required=%zu width=%d height=%d stride=%d slice=%d",
            dataSize,
            requiredBytes,
            width,
            height,
            strideBytes,
            sliceHeight
        );
        return false;
    }

    FfmpegVideoFrame result{};
    result.width = width;
    result.height = height;
    result.ptsUs = ptsUs;
    result.pixelFormat = FfmpegVideoPixelFormat::P010;
    result.transfer = mapTransfer(parameters->color_trc);
    result.primaries = mapPrimaries(parameters->color_primaries);
    result.range = mapRange(parameters->color_range);
    result.dolbyProfile = dolbyProfile;
    result.dolbyVision = dolbyProfile > 0;
    result.strides[0] = yRowBytes;
    result.strides[1] = yRowBytes;
    copyPlane(result.planes[0], data, sourceStrideBytes, yRowBytes, height);
    copyPlane(
        result.planes[1],
        data + yStorageBytes,
        sourceStrideBytes,
        yRowBytes,
        chromaRows
    );
    *out = std::move(result);
    return true;
}

bool copySupportedFrame(
    const AVFrame* frame,
    AVRational timeBase,
    FfmpegVideoFrame* out
) {
    if (frame == nullptr || out == nullptr || frame->width <= 0 || frame->height <= 0) return false;

    const int width = frame->width;
    const int height = frame->height;
    const int chromaWidth = (width + 1) / 2;
    const int chromaHeight = (height + 1) / 2;
    FfmpegVideoFrame result{};
    result.width = width;
    result.height = height;
    result.ptsUs = framePtsUs(frame, timeBase);
    result.transfer = mapTransfer(frame->color_trc);
    result.primaries = mapPrimaries(frame->color_primaries);
    result.range = mapRange(frame->color_range);
    result.dolbyVision = hasDolbyVisionSideData(frame);

    switch (static_cast<AVPixelFormat>(frame->format)) {
        case AV_PIX_FMT_YUV420P:
            result.pixelFormat = FfmpegVideoPixelFormat::Yuv420P8;
            result.strides[0] = width;
            result.strides[1] = chromaWidth;
            result.strides[2] = chromaWidth;
            copyPlane(result.planes[0], frame->data[0], frame->linesize[0], result.strides[0], height);
            copyPlane(result.planes[1], frame->data[1], frame->linesize[1], result.strides[1], chromaHeight);
            copyPlane(result.planes[2], frame->data[2], frame->linesize[2], result.strides[2], chromaHeight);
            break;
        case AV_PIX_FMT_YUV420P10LE:
        case AV_PIX_FMT_YUV420P12LE:
            result.pixelFormat = FfmpegVideoPixelFormat::Yuv420P10;
            result.strides[0] = width * 2;
            result.strides[1] = chromaWidth * 2;
            result.strides[2] = chromaWidth * 2;
            copyPlane(result.planes[0], frame->data[0], frame->linesize[0], result.strides[0], height);
            copyPlane(result.planes[1], frame->data[1], frame->linesize[1], result.strides[1], chromaHeight);
            copyPlane(result.planes[2], frame->data[2], frame->linesize[2], result.strides[2], chromaHeight);
            break;
        case AV_PIX_FMT_P010LE:
        case AV_PIX_FMT_P016LE:
            result.pixelFormat = FfmpegVideoPixelFormat::P010;
            result.strides[0] = width * 2;
            result.strides[1] = chromaWidth * 4;
            copyPlane(result.planes[0], frame->data[0], frame->linesize[0], result.strides[0], height);
            copyPlane(result.planes[1], frame->data[1], frame->linesize[1], result.strides[1], chromaHeight);
            break;
        default:
            return false;
    }

    *out = std::move(result);
    return true;
}

bool convertAndCopyFrame(
    const AVFrame* frame,
    AVRational timeBase,
    SwsContext** sws,
    FfmpegVideoFrame* out
) {
    if (!needsSoftwareScale(frame) && copySupportedFrame(frame, timeBase, out)) return true;
    if (frame == nullptr || out == nullptr || sws == nullptr) return false;

    const bool hdr =
        frame->color_trc == AVCOL_TRC_SMPTE2084 ||
        frame->color_trc == AVCOL_TRC_ARIB_STD_B67 ||
        hasDolbyVisionSideData(frame);
    const AVPixelFormat dstFormat = hdr ? AV_PIX_FMT_P010LE : AV_PIX_FMT_YUV420P;
    int dstWidth = frame->width;
    int dstHeight = frame->height;
    scaledOutputSize(frame, &dstWidth, &dstHeight);
    AVFrame* converted = av_frame_alloc();
    if (converted == nullptr) return false;
    converted->format = dstFormat;
    converted->width = dstWidth;
    converted->height = dstHeight;
    converted->pts = frame->pts;
    converted->best_effort_timestamp = frame->best_effort_timestamp;
    converted->color_trc = frame->color_trc;
    converted->color_primaries = frame->color_primaries;
    converted->color_range = frame->color_range;
    int err = av_frame_get_buffer(converted, 64);
    if (err < 0) {
        av_frame_free(&converted);
        return false;
    }

    *sws = sws_getCachedContext(
        *sws,
        frame->width,
        frame->height,
        static_cast<AVPixelFormat>(frame->format),
        dstWidth,
        dstHeight,
        dstFormat,
        needsSoftwareScale(frame) ? SWS_FAST_BILINEAR : SWS_BICUBIC,
        nullptr,
        nullptr,
        nullptr
    );
    if (*sws == nullptr) {
        av_frame_free(&converted);
        return false;
    }
    err = sws_scale(
        *sws,
        frame->data,
        frame->linesize,
        0,
        frame->height,
        converted->data,
        converted->linesize
    );
    if (err <= 0) {
        av_frame_free(&converted);
        return false;
    }
    const bool ok = copySupportedFrame(converted, timeBase, out);
    av_frame_free(&converted);
    return ok;
}
#endif
}

FfmpegVideoDecoder::~FfmpegVideoDecoder() {
    stop();
}

bool FfmpegVideoDecoder::linked() const {
#if DDDVR_HAS_FFMPEG_VIDEO
    return true;
#else
    return false;
#endif
}

bool FfmpegVideoDecoder::start(
    const std::string& uri,
    int64_t startPositionMs,
    JavaVM* javaVm,
    jobject outputSurface
) {
    stop();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        frames_.clear();
        audioTracks_.clear();
        lastError_.clear();
    }
#if DDDVR_HAS_FFMPEG_VIDEO
    if (uri.empty()) {
        setError("empty uri");
        return false;
    }
    running_.store(true);
    playing_.store(true);
    seekInFlight_.store(false);
    hardwareSurfaceActive_.store(false);
    buffering_.store(true);
    hdr_.store(false);
    durationMs_.store(0);
    videoWidth_.store(0);
    videoHeight_.store(0);
    selectedAudioStream_.store(-1);
    requestedAudioStream_.store(-1);
    javaVm_ = javaVm;
    outputSurface_ = outputSurface;
    requestedPositionMs_.store(std::max<int64_t>(0, startPositionMs));
    playbackStateUpdateNs_.store(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
    lastPresentedPositionMs_.store(-1);
    hardwareTransfer_.store(0);
    hardwarePrimaries_.store(0);
    hardwareRange_.store(0);
    hardwareDolbyProfile_.store(0);
    seekRequested_.store(false);
    thread_ = std::thread(&FfmpegVideoDecoder::decodeLoop, this, uri, std::max<int64_t>(0, startPositionMs));
    XR_LOGI(
        "DDDVR/FFmpegVideo",
        "FFMPEG_VIDEO_START uri=%s startMs=%lld javaVm=%p surface=%p",
        uri.c_str(),
        (long long)startPositionMs,
        javaVm_,
        outputSurface_
    );
    return true;
#else
    (void)uri;
    (void)startPositionMs;
    setError("FFmpeg video backend is not linked. Put Android libav* prebuilts under app/src/main/ffmpeg and rebuild.");
    XR_LOGE("DDDVR/FFmpegVideo", "CURRENT_BLOCKER FFMPEG_VIDEO_NOT_LINKED");
    return false;
#endif
}

void FfmpegVideoDecoder::stop() {
    running_.store(false);
    audioOutput_.stop();
    if (thread_.joinable()) {
        thread_.join();
    }
    destroyP010ImageReader();
    std::lock_guard<std::mutex> lock(mutex_);
    frames_.clear();
    audioTracks_.clear();
    seekRequested_.store(false);
    seekInFlight_.store(false);
    playbackStateUpdateNs_.store(0);
    lastPresentedPositionMs_.store(-1);
    hardwareSurfaceActive_.store(false);
    buffering_.store(false);
    durationMs_.store(0);
    videoWidth_.store(0);
    videoHeight_.store(0);
    selectedAudioStream_.store(-1);
    requestedAudioStream_.store(-1);
    javaVm_ = nullptr;
    outputSurface_ = nullptr;
}

void FfmpegVideoDecoder::setPlaybackState(bool playing, int64_t positionMs, bool forceSeek) {
    const int64_t safePosition = std::max<int64_t>(0, positionMs);
    requestedPositionMs_.store(safePosition);
    playbackStateUpdateNs_.store(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
    playing_.store(playing);
    audioOutput_.setPlaying(playing);
    const int64_t presented = lastPresentedPositionMs_.load();
    if (forceSeek) {
        seekInFlight_.store(true);
        seekRequested_.store(true);
        XR_LOGI(
            "DDDVR/FFmpegVideo",
            "FFMPEG_VIDEO_SYNC_SEEK_REQUEST reason=command_jump requestedMs=%lld presentedMs=%lld driftMs=%lld",
            (long long)safePosition,
            (long long)presented,
            presented >= 0 ? (long long)(safePosition - presented) : 0LL
        );
    }
}

void FfmpegVideoDecoder::setMuted(bool muted) {
    muted_.store(muted);
    audioOutput_.setMuted(muted);
}

bool FfmpegVideoDecoder::selectAudioTrack(const std::string& trackId) {
    static constexpr const char* kPrefix = "ffmpeg_audio:";
    if (trackId.rfind(kPrefix, 0) != 0) return false;
    const char* value = trackId.c_str() + std::strlen(kPrefix);
    char* end = nullptr;
    const long streamIndex = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || streamIndex < 0) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool found = std::any_of(audioTracks_.begin(), audioTracks_.end(), [&](const auto& track) {
            return track.streamIndex == streamIndex;
        });
        if (!found) return false;
    }
    requestedAudioStream_.store(static_cast<int32_t>(streamIndex));
    XR_LOGI("DDDVR/FFmpegAudio", "FFMPEG_AUDIO_TRACK_REQUEST id=%s stream=%ld", trackId.c_str(), streamIndex);
    return true;
}

FfmpegPlaybackSnapshot FfmpegVideoDecoder::playbackSnapshot() const {
    FfmpegPlaybackSnapshot snapshot{};
    snapshot.running = running_.load();
    snapshot.playing = playing_.load();
    snapshot.buffering = buffering_.load() || audioOutput_.buffering();
    snapshot.hdr = hdr_.load();
    snapshot.audioActive = audioOutput_.active();
    const int64_t audioPositionUs = audioOutput_.positionUs();
    snapshot.positionMs = audioPositionUs >= 0
        ? audioPositionUs / 1000
        : std::max<int64_t>(0, lastPresentedPositionMs_.load());
    snapshot.durationMs = durationMs_.load();
    snapshot.bufferedPositionMs = snapshot.positionMs;
    snapshot.width = videoWidth_.load();
    snapshot.height = videoHeight_.load();
    snapshot.selectedAudioStream = selectedAudioStream_.load();
    return snapshot;
}

std::vector<FfmpegAudioTrackInfo> FfmpegVideoDecoder::audioTracks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return audioTracks_;
}

bool FfmpegVideoDecoder::pollFrame(FfmpegVideoFrame* outFrame) {
    if (outFrame == nullptr) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (frames_.empty()) return false;
    *outFrame = std::move(frames_.back());
    frames_.clear();
    return true;
}

bool FfmpegVideoDecoder::createP010ImageReader(
    int width,
    int height,
    ANativeWindow** outWindow
) {
    if (outWindow == nullptr || width <= 0 || height <= 0) return false;
    *outWindow = nullptr;
    destroyP010ImageReader();

    AImageReader* reader = nullptr;
    if (!dddvr::androidimage::available()) {
        XR_LOGW("DDDVR/FFmpegVideo", "FFMPEG_VIDEO_P010_IMAGE_API_UNAVAILABLE");
        return false;
    }
    const media_status_t createStatus = dddvr::androidimage::readerNewWithUsage(
        width,
        height,
        AHARDWAREBUFFER_FORMAT_YCbCr_P010,
        AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
        4,
        &reader
    );
    if (createStatus != AMEDIA_OK || reader == nullptr) {
        XR_LOGW(
            "DDDVR/FFmpegVideo",
            "FFMPEG_VIDEO_P010_IMAGEREADER_CREATE_FAILED status=%d size=%dx%d",
            static_cast<int>(createStatus),
            width,
            height
        );
        return false;
    }

    AImageReader_BufferRemovedListener removedListener{};
    removedListener.context = this;
    removedListener.onBufferRemoved = onP010BufferRemoved;
    dddvr::androidimage::readerSetBufferRemovedListener(reader, &removedListener);

    ANativeWindow* window = nullptr;
    const media_status_t windowStatus = dddvr::androidimage::readerGetWindow(
        reader,
        &window
    );
    if (windowStatus != AMEDIA_OK || window == nullptr) {
        XR_LOGW(
            "DDDVR/FFmpegVideo",
            "FFMPEG_VIDEO_P010_IMAGEREADER_WINDOW_FAILED status=%d",
            static_cast<int>(windowStatus)
        );
        dddvr::androidimage::readerDelete(reader);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(p010ImageReaderMutex_);
        p010ImageReader_ = reader;
    }
    *outWindow = window;
    XR_LOGI(
        "DDDVR/FFmpegVideo",
        "FFMPEG_VIDEO_P010_IMAGEREADER_READY size=%dx%d format=0x%x usage=0x%llx window=%p",
        width,
        height,
        AHARDWAREBUFFER_FORMAT_YCbCr_P010,
        (unsigned long long)AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
        window
    );
    return true;
}

void FfmpegVideoDecoder::destroyP010ImageReader() {
    AImageReader* reader = nullptr;
    {
        std::lock_guard<std::mutex> lock(p010ImageReaderMutex_);
        reader = p010ImageReader_;
        p010ImageReader_ = nullptr;
    }
    if (reader != nullptr) {
        dddvr::androidimage::readerDelete(reader);
        XR_LOGI("DDDVR/FFmpegVideo", "FFMPEG_VIDEO_P010_IMAGEREADER_DESTROYED");
    }
}

bool FfmpegVideoDecoder::pollHardwareBufferFrame(FfmpegHardwareBufferFrame* outFrame) {
    if (outFrame == nullptr) return false;
    *outFrame = {};

    std::lock_guard<std::mutex> lock(p010ImageReaderMutex_);
    if (p010ImageReader_ == nullptr) return false;

    AImage* image = nullptr;
    const media_status_t acquireStatus = dddvr::androidimage::readerAcquireLatestImage(
        p010ImageReader_,
        &image
    );
    if (acquireStatus == AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE) return false;
    if (acquireStatus != AMEDIA_OK || image == nullptr) {
        XR_LOGW(
            "DDDVR/FFmpegVideo",
            "FFMPEG_VIDEO_P010_IMAGE_ACQUIRE_FAILED status=%d",
            static_cast<int>(acquireStatus)
        );
        return false;
    }

    AHardwareBuffer* buffer = nullptr;
    int32_t width = 0;
    int32_t height = 0;
    int64_t timestampNs = 0;
    const bool valid =
        dddvr::androidimage::imageGetHardwareBuffer(image, &buffer) == AMEDIA_OK &&
        buffer != nullptr &&
        dddvr::androidimage::imageGetWidth(image, &width) == AMEDIA_OK &&
        dddvr::androidimage::imageGetHeight(image, &height) == AMEDIA_OK;
    dddvr::androidimage::imageGetTimestamp(image, &timestampNs);
    if (!valid) {
        dddvr::androidimage::imageDelete(image);
        XR_LOGW("DDDVR/FFmpegVideo", "FFMPEG_VIDEO_P010_IMAGE_INVALID");
        return false;
    }

    outFrame->image = image;
    outFrame->buffer = buffer;
    outFrame->width = width;
    outFrame->height = height;
    outFrame->ptsUs = timestampNs > 0 ? timestampNs / 1000 : 0;
    outFrame->transfer = static_cast<FfmpegVideoColorTransfer>(hardwareTransfer_.load());
    outFrame->primaries = static_cast<FfmpegVideoColorPrimaries>(hardwarePrimaries_.load());
    outFrame->range = static_cast<FfmpegVideoColorRange>(hardwareRange_.load());
    outFrame->dolbyProfile = hardwareDolbyProfile_.load();
    outFrame->dolbyVision = outFrame->dolbyProfile > 0;
    outFrame->dolbyMetadata = dolbyMetadataForPts(outFrame->ptsUs);
    return true;
}

void FfmpegVideoDecoder::storeDolbyMetadata(
    std::shared_ptr<const DolbyRpuMetadata> metadata
) {
    if (!metadata) return;
    std::lock_guard<std::mutex> lock(dolbyMetadataMutex_);
    dolbyMetadataByPts_[metadata->ptsUs] = std::move(metadata);
    while (dolbyMetadataByPts_.size() > 240) {
        dolbyMetadataByPts_.erase(dolbyMetadataByPts_.begin());
    }
}

std::shared_ptr<const DolbyRpuMetadata> FfmpegVideoDecoder::dolbyMetadataForPts(int64_t ptsUs) {
    std::lock_guard<std::mutex> lock(dolbyMetadataMutex_);
    if (dolbyMetadataByPts_.empty()) return nullptr;

    auto after = dolbyMetadataByPts_.upper_bound(ptsUs + 2000);
    auto selected = after;
    if (selected != dolbyMetadataByPts_.begin()) {
        --selected;
    } else {
        selected = dolbyMetadataByPts_.begin();
    }
    const auto result = selected->second;

    const int64_t pruneBeforeUs = ptsUs - 5000000;
    auto pruneEnd = dolbyMetadataByPts_.lower_bound(pruneBeforeUs);
    dolbyMetadataByPts_.erase(dolbyMetadataByPts_.begin(), pruneEnd);
    return result;
}

void FfmpegVideoDecoder::clearDolbyMetadata(int dolbyProfile) {
    dolbyRpuParser_.reset(dolbyProfile);
    std::lock_guard<std::mutex> lock(dolbyMetadataMutex_);
    dolbyMetadataByPts_.clear();
}

std::string FfmpegVideoDecoder::lastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

void FfmpegVideoDecoder::pushFrame(FfmpegVideoFrame&& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    frames_.push_back(std::move(frame));
    while (frames_.size() > kMaxQueuedFrames) {
        frames_.pop_front();
    }
}

void FfmpegVideoDecoder::setError(const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastError_ = error;
}

void FfmpegVideoDecoder::decodeLoop(std::string uri, int64_t startPositionMs) {
#if DDDVR_HAS_FFMPEG_VIDEO
    avformat_network_init();
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVCodecContext* audioCodec = nullptr;
    AVPacket* packet = nullptr;
    AVPacket* filteredPacket = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* audioFrame = nullptr;
    SwsContext* sws = nullptr;
    SwrContext* swr = nullptr;
    AVDictionary* options = nullptr;
    AVBSFContext* bitstreamFilter = nullptr;
    AMediaCodec* ndkMediaCodec = nullptr;
    AMediaFormat* ndkMediaFormat = nullptr;
    ANativeWindow* ndkNativeWindow = nullptr;
    JNIEnv* ndkEnv = nullptr;
    bool ndkThreadAttached = false;
    bool ndkCodecStarted = false;
    const AVCodec* decoder = nullptr;
    bool mediaCodecContextInitialized = false;
    bool useHardwareSurface = false;
    int audioStream = -1;
    std::mutex audioPacketMutex;
    std::condition_variable audioPacketReady;
    std::condition_variable audioPacketSpace;
    std::deque<AVPacket*> audioPackets;
    std::atomic<bool> audioWorkerRunning{false};
    std::thread audioWorker;
    std::mutex videoPacketMutex;
    std::condition_variable videoPacketReady;
    std::condition_variable videoPacketSpace;
    std::deque<AVPacket*> videoPackets;
    std::atomic<bool> demuxWorkerRunning{false};
    std::atomic<bool> demuxReachedEof{false};
    std::thread demuxWorker;

    auto stopDemuxWorker = [&]() {
        demuxWorkerRunning.store(false);
        videoPacketReady.notify_all();
        videoPacketSpace.notify_all();
        audioPacketReady.notify_all();
        audioPacketSpace.notify_all();
        if (demuxWorker.joinable()) demuxWorker.join();
        std::lock_guard<std::mutex> lock(videoPacketMutex);
        while (!videoPackets.empty()) {
            AVPacket* queued = videoPackets.front();
            videoPackets.pop_front();
            av_packet_free(&queued);
        }
        demuxReachedEof.store(false);
    };

    auto stopAudioWorker = [&]() {
        audioWorkerRunning.store(false);
        audioOutput_.stop();
        audioPacketReady.notify_all();
        audioPacketSpace.notify_all();
        if (audioWorker.joinable()) audioWorker.join();
        std::lock_guard<std::mutex> lock(audioPacketMutex);
        while (!audioPackets.empty()) {
            AVPacket* queued = audioPackets.front();
            audioPackets.pop_front();
            av_packet_free(&queued);
        }
    };

    auto closeCodec = [&]() {
        if (codec == nullptr) return;
        if (avcodec_is_open(codec)) {
            avcodec_close(codec);
        }
        if (mediaCodecContextInitialized) {
            av_mediacodec_default_free(codec);
            mediaCodecContextInitialized = false;
        }
        avcodec_free_context(&codec);
    };

    auto closeNdkMediaCodec = [&]() {
        if (bitstreamFilter != nullptr) {
            av_bsf_free(&bitstreamFilter);
        }
        if (ndkMediaCodec != nullptr) {
            if (ndkCodecStarted) {
                AMediaCodec_stop(ndkMediaCodec);
                ndkCodecStarted = false;
            }
            AMediaCodec_delete(ndkMediaCodec);
            ndkMediaCodec = nullptr;
        }
        if (ndkMediaFormat != nullptr) {
            AMediaFormat_delete(ndkMediaFormat);
            ndkMediaFormat = nullptr;
        }
        if (ndkNativeWindow != nullptr) {
            ANativeWindow_release(ndkNativeWindow);
            ndkNativeWindow = nullptr;
        }
        if (ndkThreadAttached && javaVm_ != nullptr) {
            javaVm_->DetachCurrentThread();
            ndkThreadAttached = false;
        }
        ndkEnv = nullptr;
    };

    auto closeAudioCodec = [&]() {
        stopAudioWorker();
        if (swr != nullptr) {
            swr_free(&swr);
        }
        if (audioFrame != nullptr) {
            av_frame_free(&audioFrame);
        }
        if (audioCodec != nullptr) {
            avcodec_free_context(&audioCodec);
        }
        selectedAudioStream_.store(-1);
        audioStream = -1;
    };

    auto cleanup = [&]() {
        stopDemuxWorker();
        if (sws != nullptr) sws_freeContext(sws);
        if (frame != nullptr) av_frame_free(&frame);
        if (filteredPacket != nullptr) av_packet_free(&filteredPacket);
        if (packet != nullptr) av_packet_free(&packet);
        closeCodec();
        closeNdkMediaCodec();
        closeAudioCodec();
        if (format != nullptr) avformat_close_input(&format);
        if (options != nullptr) av_dict_free(&options);
        seekRequested_.store(false);
        seekInFlight_.store(false);
        hardwareSurfaceActive_.store(false);
        buffering_.store(false);
        running_.store(false);
        XR_LOGI("DDDVR/FFmpegVideo", "FFMPEG_VIDEO_STOPPED");
    };

    av_dict_set(&options, "user_agent", "DDDVR/FFmpeg", 0);
    av_dict_set(&options, "timeout", "30000000", 0);
    av_dict_set(&options, "rw_timeout", "30000000", 0);
    av_dict_set(&options, "reconnect", "1", 0);
    av_dict_set(&options, "reconnect_at_eof", "1", 0);
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "4", 0);
    int err = avformat_open_input(&format, uri.c_str(), nullptr, &options);
    if (err < 0) {
        setError("avformat_open_input failed: " + ffError(err));
        XR_LOGE("DDDVR/FFmpegVideo", "CURRENT_BLOCKER FFMPEG_OPEN_FAILED err=%s uri=%s", ffError(err).c_str(), uri.c_str());
        cleanup();
        return;
    }
    err = avformat_find_stream_info(format, nullptr);
    if (err < 0) {
        setError("avformat_find_stream_info failed: " + ffError(err));
        XR_LOGE("DDDVR/FFmpegVideo", "CURRENT_BLOCKER FFMPEG_STREAM_INFO_FAILED err=%s", ffError(err).c_str());
        cleanup();
        return;
    }

    const int videoStream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStream < 0) {
        setError("no video stream");
        XR_LOGE("DDDVR/FFmpegVideo", "CURRENT_BLOCKER FFMPEG_NO_VIDEO_STREAM");
        cleanup();
        return;
    }
    AVStream* stream = format->streams[videoStream];
    const int streamDolbyProfile = dolbyVisionProfile(stream->codecpar);
    clearDolbyMetadata(streamDolbyProfile);
    videoWidth_.store(stream->codecpar->width);
    videoHeight_.store(stream->codecpar->height);
    const bool streamHdr = stream->codecpar->color_trc == AVCOL_TRC_SMPTE2084 ||
        stream->codecpar->color_trc == AVCOL_TRC_ARIB_STD_B67 ||
        streamDolbyProfile > 0;
    hdr_.store(streamHdr);
    hardwareTransfer_.store(static_cast<int32_t>(mapTransfer(stream->codecpar->color_trc)));
    hardwarePrimaries_.store(static_cast<int32_t>(mapPrimaries(stream->codecpar->color_primaries)));
    hardwareRange_.store(static_cast<int32_t>(mapRange(stream->codecpar->color_range)));
    hardwareDolbyProfile_.store(streamDolbyProfile);
    XR_LOGI(
        "DDDVR/FFmpegVideo",
        "FFMPEG_VIDEO_COLOR_METADATA transfer=%d primaries=%d range=%d doviProfile=%d dovi=%d",
        stream->codecpar->color_trc,
        stream->codecpar->color_primaries,
        stream->codecpar->color_range,
        streamDolbyProfile,
        streamDolbyProfile > 0 ? 1 : 0
    );
    durationMs_.store(
        format->duration != AV_NOPTS_VALUE
            ? std::max<int64_t>(0, format->duration / (AV_TIME_BASE / 1000))
            : 0
    );

    const int bestAudioStream = av_find_best_stream(
        format,
        AVMEDIA_TYPE_AUDIO,
        -1,
        videoStream,
        nullptr,
        0
    );
    int firstAudioStream = -1;
    int defaultAudioStream = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        audioTracks_.clear();
        for (unsigned int index = 0; index < format->nb_streams; ++index) {
            AVStream* candidate = format->streams[index];
            if (candidate == nullptr || candidate->codecpar == nullptr ||
                candidate->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
                continue;
            }
            if (firstAudioStream < 0) firstAudioStream = static_cast<int>(index);
            if (defaultAudioStream < 0 &&
                (candidate->disposition & AV_DISPOSITION_DEFAULT) != 0) {
                defaultAudioStream = static_cast<int>(index);
            }
            audioTracks_.push_back(buildAudioTrackInfo(
                candidate,
                static_cast<int>(index),
                false
            ));
        }
    }
    const int initialAudioStream = defaultAudioStream >= 0
        ? defaultAudioStream
        : (firstAudioStream >= 0 ? firstAudioStream : bestAudioStream);

    int64_t audioDiscardUntilPtsUs = startPositionMs > 0 ? startPositionMs * 1000 : -1;
    auto openAudioStream = [&](int streamIndex, int64_t clockPositionUs) -> bool {
        if (streamIndex < 0 || streamIndex >= static_cast<int>(format->nb_streams)) return false;
        AVStream* audio = format->streams[streamIndex];
        if (audio == nullptr || audio->codecpar == nullptr ||
            audio->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
            return false;
        }

        closeAudioCodec();
        const AVCodec* audioDecoder = avcodec_find_decoder(audio->codecpar->codec_id);
        if (audioDecoder == nullptr) {
            XR_LOGE(
                "DDDVR/FFmpegAudio",
                "FFMPEG_AUDIO_DECODER_MISSING stream=%d codec=%s",
                streamIndex,
                avcodec_get_name(audio->codecpar->codec_id)
            );
            return false;
        }
        audioCodec = avcodec_alloc_context3(audioDecoder);
        if (audioCodec == nullptr) return false;
        int audioErr = avcodec_parameters_to_context(audioCodec, audio->codecpar);
        if (audioErr >= 0) {
            audioCodec->pkt_timebase = audio->time_base;
            audioCodec->thread_count = 2;
            audioErr = avcodec_open2(audioCodec, audioDecoder, nullptr);
        }
        if (audioErr < 0) {
            XR_LOGE(
                "DDDVR/FFmpegAudio",
                "FFMPEG_AUDIO_CODEC_OPEN_FAILED stream=%d codec=%s err=%s",
                streamIndex,
                audioDecoder->name,
                ffError(audioErr).c_str()
            );
            closeAudioCodec();
            return false;
        }
        audioFrame = av_frame_alloc();
        if (audioFrame == nullptr) {
            closeAudioCodec();
            return false;
        }

        if (audioCodec->ch_layout.nb_channels <= 0) {
            av_channel_layout_default(&audioCodec->ch_layout, 2);
        }
        const AVChannelLayout outputLayout = AV_CHANNEL_LAYOUT_STEREO;
        audioErr = swr_alloc_set_opts2(
            &swr,
            &outputLayout,
            AV_SAMPLE_FMT_S16,
            kAudioOutputSampleRate,
            &audioCodec->ch_layout,
            audioCodec->sample_fmt,
            std::max(1, audioCodec->sample_rate),
            0,
            nullptr
        );
        if (audioErr >= 0) audioErr = swr_init(swr);
        if (audioErr < 0 || swr == nullptr) {
            XR_LOGE(
                "DDDVR/FFmpegAudio",
                "FFMPEG_AUDIO_RESAMPLER_FAILED stream=%d err=%s",
                streamIndex,
                ffError(audioErr).c_str()
            );
            closeAudioCodec();
            return false;
        }
        if (!audioOutput_.start(kAudioOutputSampleRate, kAudioOutputChannels)) {
            XR_LOGE("DDDVR/FFmpegAudio", "CURRENT_BLOCKER FFMPEG_AUDIO_OUTPUT_FAILED stream=%d", streamIndex);
            closeAudioCodec();
            return false;
        }
        audioOutput_.setMuted(muted_.load());
        audioOutput_.flush(std::max<int64_t>(0, clockPositionUs));
        audioOutput_.setPlaying(playing_.load());
        audioStream = streamIndex;
        selectedAudioStream_.store(streamIndex);
        requestedAudioStream_.store(streamIndex);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& track : audioTracks_) {
                track.selected = track.streamIndex == streamIndex;
            }
        }
        char layout[128] = {};
        av_channel_layout_describe(&audioCodec->ch_layout, layout, sizeof(layout));
        XR_LOGI(
            "DDDVR/FFmpegAudio",
            "FFMPEG_AUDIO_READY stream=%d decoder=%s inputRate=%d inputLayout=%s outputRate=%d outputChannels=%d",
            streamIndex,
            audioDecoder->name,
            audioCodec->sample_rate,
            layout,
            kAudioOutputSampleRate,
            kAudioOutputChannels
        );
        return true;
    };

    auto decodeAudioPacket = [&](const AVPacket* audioPacket) -> bool {
        if (audioCodec == nullptr || audioFrame == nullptr || swr == nullptr ||
            audioPacket == nullptr || audioPacket->stream_index != audioStream) {
            return false;
        }
        int audioErr = avcodec_send_packet(audioCodec, audioPacket);
        if (audioErr < 0 && audioErr != AVERROR(EAGAIN)) {
            XR_LOGW("DDDVR/FFmpegAudio", "FFMPEG_AUDIO_SEND_FAILED err=%s", ffError(audioErr).c_str());
            return false;
        }
        bool produced = false;
        while (running_.load()) {
            audioErr = avcodec_receive_frame(audioCodec, audioFrame);
            if (audioErr == AVERROR(EAGAIN) || audioErr == AVERROR_EOF) break;
            if (audioErr < 0) {
                XR_LOGW("DDDVR/FFmpegAudio", "FFMPEG_AUDIO_RECEIVE_FAILED err=%s", ffError(audioErr).c_str());
                break;
            }
            AVStream* audio = format->streams[audioStream];
            int64_t ptsUs = framePtsUs(audioFrame, audio->time_base);
            const int inputRate = std::max(1, audioCodec->sample_rate);
            const int outputCapacity = static_cast<int>(av_rescale_rnd(
                swr_get_delay(swr, inputRate) + audioFrame->nb_samples,
                kAudioOutputSampleRate,
                inputRate,
                AV_ROUND_UP
            ));
            std::vector<int16_t> pcm(
                static_cast<size_t>(std::max(1, outputCapacity)) * kAudioOutputChannels
            );
            uint8_t* output[] = {reinterpret_cast<uint8_t*>(pcm.data())};
            const int converted = swr_convert(
                swr,
                output,
                outputCapacity,
                const_cast<const uint8_t**>(audioFrame->extended_data),
                audioFrame->nb_samples
            );
            const int64_t frameEndUs = ptsUs +
                (static_cast<int64_t>(std::max(0, converted)) * 1000000LL) /
                    kAudioOutputSampleRate;
            const bool discard = audioDiscardUntilPtsUs >= 0 &&
                frameEndUs + kSeekPrerollToleranceUs < audioDiscardUntilPtsUs;
            if (converted > 0 && !discard) {
                if (audioOutput_.enqueue(pcm.data(), converted, ptsUs)) {
                    produced = true;
                    buffering_.store(false);
                    if (audioDiscardUntilPtsUs >= 0) audioDiscardUntilPtsUs = -1;
                }
            }
            av_frame_unref(audioFrame);
        }
        return produced;
    };

    auto startAudioWorker = [&]() {
        if (audioCodec == nullptr || audioStream < 0 || audioWorker.joinable()) return;
        audioWorkerRunning.store(true);
        audioWorker = std::thread([&]() {
            XR_LOGI(
                "DDDVR/FFmpegAudio",
                "FFMPEG_AUDIO_WORKER_START stream=%d maxPackets=%zu",
                audioStream,
                kMaxQueuedAudioPackets
            );
            while (running_.load() && audioWorkerRunning.load()) {
                AVPacket* queued = nullptr;
                {
                    std::unique_lock<std::mutex> lock(audioPacketMutex);
                    audioPacketReady.wait(lock, [&]() {
                        return !running_.load() || !audioWorkerRunning.load() ||
                            !audioPackets.empty();
                    });
                    if (!running_.load() || !audioWorkerRunning.load()) break;
                    queued = audioPackets.front();
                    audioPackets.pop_front();
                }
                audioPacketSpace.notify_all();
                decodeAudioPacket(queued);
                av_packet_free(&queued);
            }
            XR_LOGI("DDDVR/FFmpegAudio", "FFMPEG_AUDIO_WORKER_STOP");
        });
    };

    auto queueAudioPacket = [&](const AVPacket* source) -> bool {
        if (source == nullptr || !audioWorkerRunning.load() ||
            !demuxWorkerRunning.load()) return false;
        AVPacket* queued = av_packet_clone(source);
        if (queued == nullptr) return false;
        std::unique_lock<std::mutex> lock(audioPacketMutex);
        audioPacketSpace.wait(lock, [&]() {
            return !running_.load() || !audioWorkerRunning.load() ||
                !demuxWorkerRunning.load() ||
                audioPackets.size() < kMaxQueuedAudioPackets;
        });
        if (!running_.load() || !audioWorkerRunning.load() ||
            !demuxWorkerRunning.load()) {
            lock.unlock();
            av_packet_free(&queued);
            return false;
        }
        audioPackets.push_back(queued);
        lock.unlock();
        audioPacketReady.notify_one();
        return true;
    };

    auto startDemuxWorker = [&]() {
        if (format == nullptr || demuxWorker.joinable()) return;
        demuxReachedEof.store(false);
        demuxWorkerRunning.store(true);
        demuxWorker = std::thread([&]() {
            XR_LOGI(
                "DDDVR/FFmpegVideo",
                "FFMPEG_DEMUX_WORKER_START videoPackets=%zu audioPackets=%zu",
                kMaxQueuedVideoPackets,
                kMaxQueuedAudioPackets
            );
            while (running_.load() && demuxWorkerRunning.load()) {
                AVPacket* readPacket = av_packet_alloc();
                if (readPacket == nullptr) break;
                const int readErr = av_read_frame(format, readPacket);
                if (readErr == AVERROR_EOF) {
                    av_packet_free(&readPacket);
                    demuxReachedEof.store(true);
                    videoPacketReady.notify_all();
                    break;
                }
                if (readErr < 0) {
                    av_packet_free(&readPacket);
                    buffering_.store(true);
                    XR_LOGW(
                        "DDDVR/FFmpegVideo",
                        "FFMPEG_DEMUX_READ_FAILED err=%s",
                        ffError(readErr).c_str()
                    );
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    continue;
                }
                if (readPacket->stream_index == videoStream) {
                    std::unique_lock<std::mutex> lock(videoPacketMutex);
                    videoPacketSpace.wait(lock, [&]() {
                        return !running_.load() || !demuxWorkerRunning.load() ||
                            videoPackets.size() < kMaxQueuedVideoPackets;
                    });
                    if (!running_.load() || !demuxWorkerRunning.load()) {
                        lock.unlock();
                        av_packet_free(&readPacket);
                        break;
                    }
                    videoPackets.push_back(readPacket);
                    lock.unlock();
                    videoPacketReady.notify_one();
                    continue;
                }
                if (readPacket->stream_index == selectedAudioStream_.load()) {
                    queueAudioPacket(readPacket);
                }
                av_packet_free(&readPacket);
            }
            videoPacketReady.notify_all();
            audioPacketReady.notify_all();
            XR_LOGI("DDDVR/FFmpegVideo", "FFMPEG_DEMUX_WORKER_STOP");
        });
    };

    auto takeVideoPacket = [&](AVPacket* destination) -> bool {
        if (destination == nullptr) return false;
        std::unique_lock<std::mutex> lock(videoPacketMutex);
        videoPacketReady.wait_for(lock, std::chrono::milliseconds(20), [&]() {
            return !running_.load() || !videoPackets.empty() ||
                demuxReachedEof.load() || !demuxWorkerRunning.load();
        });
        if (videoPackets.empty()) return false;
        AVPacket* queued = videoPackets.front();
        videoPackets.pop_front();
        av_packet_move_ref(destination, queued);
        av_packet_free(&queued);
        lock.unlock();
        videoPacketSpace.notify_one();
        return true;
    };

    auto packetQueueDepths = [&]() -> std::pair<size_t, size_t> {
        size_t videoDepth = 0;
        size_t audioDepth = 0;
        {
            std::lock_guard<std::mutex> lock(videoPacketMutex);
            videoDepth = videoPackets.size();
        }
        {
            std::lock_guard<std::mutex> lock(audioPacketMutex);
            audioDepth = audioPackets.size();
        }
        return {videoDepth, audioDepth};
    };

    if (initialAudioStream >= 0) {
        if (openAudioStream(initialAudioStream, startPositionMs * 1000)) {
            startAudioWorker();
        } else {
            XR_LOGW(
                "DDDVR/FFmpegAudio",
                "FFMPEG_AUDIO_DISABLED initialStream=%d bestStream=%d",
                initialAudioStream,
                bestAudioStream
            );
        }
    } else {
        XR_LOGW("DDDVR/FFmpegAudio", "FFMPEG_AUDIO_NO_STREAM");
    }
    startDemuxWorker();

    const char* ndkMime = mediaCodecMime(stream->codecpar->codec_id);
    const char* bsfName = annexBFilterName(stream->codecpar->codec_id);
    bool useNdkMediaCodecSurface = false;
    bool useNdkMediaCodecP010 = false;
    bool useNdkMediaCodecP010ImageReader = false;
    ANativeWindow* p010ImageReaderWindow = nullptr;
    const bool requestP010 = streamHdr && stream->codecpar->codec_id == AV_CODEC_ID_HEVC;

    XR_LOGI(
        "DDDVR/FFmpegVideo",
        "FFMPEG_VIDEO_NDK_MEDIACODEC_PROBE codecId=%d mime=%s javaVm=%p surface=%p",
        stream->codecpar->codec_id,
        ndkMime != nullptr ? ndkMime : "none",
        javaVm_,
        outputSurface_
    );

    if (ndkMime != nullptr && javaVm_ != nullptr && outputSurface_ != nullptr) {
        const jint envStatus = javaVm_->GetEnv(reinterpret_cast<void**>(&ndkEnv), JNI_VERSION_1_6);
        if (envStatus == JNI_EDETACHED && javaVm_->AttachCurrentThread(&ndkEnv, nullptr) == JNI_OK) {
            ndkThreadAttached = true;
        }

        if (ndkEnv != nullptr) {
            ndkNativeWindow = ANativeWindow_fromSurface(ndkEnv, outputSurface_);
        }

        if (bsfName != nullptr) {
            const AVBitStreamFilter* filter = av_bsf_get_by_name(bsfName);
            if (filter != nullptr && av_bsf_alloc(filter, &bitstreamFilter) >= 0) {
                err = avcodec_parameters_copy(bitstreamFilter->par_in, stream->codecpar);
                if (err >= 0) {
                    bitstreamFilter->time_base_in = stream->time_base;
                    err = av_bsf_init(bitstreamFilter);
                }
                if (err < 0) {
                    XR_LOGW(
                        "DDDVR/FFmpegVideo",
                        "FFMPEG_VIDEO_NDK_BSF_INIT_FAILED filter=%s err=%s",
                        bsfName,
                        ffError(err).c_str()
                    );
                    av_bsf_free(&bitstreamFilter);
                }
            }
        }

        const bool bsfReady = bsfName == nullptr || bitstreamFilter != nullptr;
        auto configureNdkCodec = [&, this](
            bool p010,
            ANativeWindow* outputWindow,
            const char* path
        ) -> bool {
            if (ndkMediaCodec != nullptr) {
                if (ndkCodecStarted) {
                    AMediaCodec_stop(ndkMediaCodec);
                    ndkCodecStarted = false;
                }
                AMediaCodec_delete(ndkMediaCodec);
                ndkMediaCodec = nullptr;
            }
            if (ndkMediaFormat != nullptr) {
                AMediaFormat_delete(ndkMediaFormat);
                ndkMediaFormat = nullptr;
            }
            ndkMediaCodec = AMediaCodec_createDecoderByType(ndkMime);
            ndkMediaFormat = AMediaFormat_new();
            if (ndkMediaCodec == nullptr || ndkMediaFormat == nullptr) return false;
            AMediaFormat_setString(ndkMediaFormat, "mime", ndkMime);
            AMediaFormat_setInt32(ndkMediaFormat, "width", stream->codecpar->width);
            AMediaFormat_setInt32(ndkMediaFormat, "height", stream->codecpar->height);
            AMediaFormat_setInt32(ndkMediaFormat, "max-input-size", 8 * 1024 * 1024);
            AMediaFormat_setInt32(ndkMediaFormat, "priority", 0);
            if (p010) {
                AMediaFormat_setInt32(
                    ndkMediaFormat,
                    "color-format",
                    kMediaCodecColorFormatP010
                );
            }

            const int colorStandard = mediaCodecColorStandard(
                stream->codecpar->color_primaries,
                stream->codecpar->color_space
            );
            const int colorTransfer = mediaCodecColorTransfer(stream->codecpar->color_trc);
            const int colorRange = mediaCodecColorRange(stream->codecpar->color_range);
            if (colorStandard != 0) AMediaFormat_setInt32(ndkMediaFormat, "color-standard", colorStandard);
            if (colorTransfer != 0) AMediaFormat_setInt32(ndkMediaFormat, "color-transfer", colorTransfer);
            if (colorRange != 0) AMediaFormat_setInt32(ndkMediaFormat, "color-range", colorRange);

            const AVCodecParameters* csdParameters = bitstreamFilter != nullptr
                ? bitstreamFilter->par_out
                : stream->codecpar;
            if (csdParameters != nullptr &&
                startsWithAnnexB(csdParameters->extradata, csdParameters->extradata_size)) {
                AMediaFormat_setBuffer(
                    ndkMediaFormat,
                    "csd-0",
                    csdParameters->extradata,
                    static_cast<size_t>(csdParameters->extradata_size)
                );
            }

            XR_LOGI(
                "DDDVR/FFmpegVideo",
                "FFMPEG_VIDEO_NDK_CONFIG path=%s format=%s bsf=%s csd=%d hdr=%d transfer=%d primaries=%d range=%d",
                path,
                AMediaFormat_toString(ndkMediaFormat),
                bsfName != nullptr ? bsfName : "none",
                csdParameters != nullptr && startsWithAnnexB(
                    csdParameters->extradata,
                    csdParameters->extradata_size
                ) ? 1 : 0,
                colorTransfer == 6 || colorTransfer == 7 ? 1 : 0,
                stream->codecpar->color_trc,
                stream->codecpar->color_primaries,
                stream->codecpar->color_range
            );

            media_status_t mediaStatus = AMediaCodec_configure(
                ndkMediaCodec,
                ndkMediaFormat,
                outputWindow,
                nullptr,
                0
            );
            if (mediaStatus == AMEDIA_OK) {
                mediaStatus = AMediaCodec_start(ndkMediaCodec);
            }
            if (mediaStatus == AMEDIA_OK) {
                ndkCodecStarted = true;
                XR_LOGI(
                    "DDDVR/FFmpegVideo",
                    "FFMPEG_VIDEO_NDK_MEDIACODEC_ACTIVE path=%s mime=%s surface=%p",
                    path,
                    ndkMime,
                    outputWindow
                );
                return true;
            } else {
                XR_LOGW(
                    "DDDVR/FFmpegVideo",
                    "FFMPEG_VIDEO_NDK_MEDIACODEC_CONFIG_FAILED path=%s mime=%s status=%d",
                    path,
                    ndkMime,
                    static_cast<int>(mediaStatus)
                );
            }
            return false;
        };

        if (bsfReady && requestP010) {
            if (createP010ImageReader(
                    stream->codecpar->width,
                    stream->codecpar->height,
                    &p010ImageReaderWindow
                )) {
                useNdkMediaCodecP010ImageReader = configureNdkCodec(
                    true,
                    p010ImageReaderWindow,
                    "p010_ahardwarebuffer"
                );
                if (!useNdkMediaCodecP010ImageReader) {
                    destroyP010ImageReader();
                    p010ImageReaderWindow = nullptr;
                }
            }
            if (!useNdkMediaCodecP010ImageReader) {
                useNdkMediaCodecP010 = configureNdkCodec(
                    true,
                    nullptr,
                    "p010_buffer"
                );
            }
        }
        if (bsfReady && !useNdkMediaCodecP010ImageReader &&
            !useNdkMediaCodecP010 && ndkNativeWindow != nullptr) {
            useNdkMediaCodecSurface = configureNdkCodec(
                false,
                ndkNativeWindow,
                "surface"
            );
        }
        hardwareSurfaceActive_.store(useNdkMediaCodecSurface);
    }

    if (useNdkMediaCodecSurface || useNdkMediaCodecP010 ||
        useNdkMediaCodecP010ImageReader) {
        packet = av_packet_alloc();
        filteredPacket = av_packet_alloc();
        if (packet == nullptr || filteredPacket == nullptr) {
            setError("NDK MediaCodec packet allocation failed");
            cleanup();
            return;
        }

        int64_t discardUntilPtsUs = -1;
        auto statsStart = std::chrono::steady_clock::now();
        uint64_t inputPackets = 0;
        uint64_t decodedFrames = 0;
        uint64_t presentedFrames = 0;
        uint64_t discardedPrerollFrames = 0;
        uint64_t discardedLateFrames = 0;
        uint64_t inputFailures = 0;
        int outputWidth = stream->codecpar->width;
        int outputHeight = stream->codecpar->height;
        int outputStride = outputWidth * 2;
        int outputSliceHeight = outputHeight;
        int outputColorFormat = useNdkMediaCodecP010 ? kMediaCodecColorFormatP010 : 0;
        const bool hdr = stream->codecpar->color_trc == AVCOL_TRC_SMPTE2084 ||
            stream->codecpar->color_trc == AVCOL_TRC_ARIB_STD_B67;

        auto masterPositionUs = [&]() -> int64_t {
            const int64_t audioPositionUs = audioOutput_.positionUs();
            if (audioOutput_.active() && audioPositionUs >= 0) return audioPositionUs;
            const int64_t baseUs = requestedPositionMs_.load() * 1000;
            if (!playing_.load()) return baseUs;
            const int64_t updatedNs = playbackStateUpdateNs_.load();
            if (updatedNs <= 0) return baseUs;
            const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
            return baseUs + std::max<int64_t>(0, nowNs - updatedNs) / 1000;
        };

        auto waitForPresentation = [&](int64_t ptsUs) {
            while (running_.load() && playing_.load()) {
                if (ptsUs <= masterPositionUs() + 20000) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        };

        auto logNdkStatsIfDue = [&]() {
            const auto now = std::chrono::steady_clock::now();
            if (now - statsStart < kStatsInterval) return;
            const double seconds = std::chrono::duration<double>(now - statsStart).count();
            const auto [videoPacketDepth, audioPacketDepth] = packetQueueDepths();
            XR_LOGI(
                "DDDVR/FFmpegVideo",
                "FFMPEG_VIDEO_STATS path=%s decoder=%s inputFps=%.2f decodedFps=%.2f presentedFps=%.2f input=%llu decoded=%llu presented=%llu prerollDropped=%llu lateDropped=%llu inputFailures=%llu lastPtsMs=%lld masterMs=%lld lagMs=%lld hdr=%d hdrKind=%s transfer=%d primaries=%d range=%d audioActive=%d audioStream=%d audioUnderflows=%llu videoPackets=%zu audioPackets=%zu",
                useNdkMediaCodecP010ImageReader
                    ? "ndk_mediacodec_p010_ahardwarebuffer"
                    : (useNdkMediaCodecP010
                        ? "ndk_mediacodec_p010"
                        : "ndk_mediacodec_surface"),
                ndkMime,
                seconds > 0.0 ? static_cast<double>(inputPackets) / seconds : 0.0,
                seconds > 0.0 ? static_cast<double>(decodedFrames) / seconds : 0.0,
                seconds > 0.0 ? static_cast<double>(presentedFrames) / seconds : 0.0,
                (unsigned long long)inputPackets,
                (unsigned long long)decodedFrames,
                (unsigned long long)presentedFrames,
                (unsigned long long)discardedPrerollFrames,
                (unsigned long long)discardedLateFrames,
                (unsigned long long)inputFailures,
                (long long)lastPresentedPositionMs_.load(),
                (long long)(masterPositionUs() / 1000),
                (long long)(masterPositionUs() / 1000 - lastPresentedPositionMs_.load()),
                hdr ? 1 : 0,
                hdrKind(stream->codecpar->color_trc, false),
                stream->codecpar->color_trc,
                stream->codecpar->color_primaries,
                stream->codecpar->color_range,
                audioOutput_.active() ? 1 : 0,
                selectedAudioStream_.load(),
                (unsigned long long)audioOutput_.underflowCount(),
                videoPacketDepth,
                audioPacketDepth
            );
            statsStart = now;
            inputPackets = 0;
            decodedFrames = 0;
            presentedFrames = 0;
            discardedPrerollFrames = 0;
            discardedLateFrames = 0;
            inputFailures = 0;
        };

        auto drainOutput = [&](int64_t initialTimeoutUs) {
            int64_t timeoutUs = initialTimeoutUs;
            while (running_.load()) {
                AMediaCodecBufferInfo info{};
                const ssize_t outputIndex = AMediaCodec_dequeueOutputBuffer(
                    ndkMediaCodec,
                    &info,
                    timeoutUs
                );
                timeoutUs = 0;
                if (outputIndex >= 0) {
                    ++decodedFrames;
                    const bool codecConfig =
                        (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;
                    const bool discardPreroll =
                        !codecConfig && discardUntilPtsUs >= 0 &&
                        info.presentationTimeUs + kSeekPrerollToleranceUs < discardUntilPtsUs;
                    const int64_t masterUs = masterPositionUs();
                    const bool discardLate =
                        !codecConfig && !discardPreroll && audioOutput_.active() &&
                        info.presentationTimeUs + kVideoLateDropThresholdUs < masterUs;
                    const bool render = !codecConfig && !discardPreroll && !discardLate;
                    if (discardPreroll) {
                        ++discardedPrerollFrames;
                    }
                    if (discardLate) {
                        ++discardedLateFrames;
                    }
                    if (render) {
                        waitForPresentation(info.presentationTimeUs);
                    }
                    bool frameReady = render;
                    if (render && useNdkMediaCodecP010) {
                        size_t outputCapacity = 0;
                        uint8_t* outputBuffer = AMediaCodec_getOutputBuffer(
                            ndkMediaCodec,
                            static_cast<size_t>(outputIndex),
                            &outputCapacity
                        );
                        const size_t offset = info.offset >= 0
                            ? static_cast<size_t>(info.offset)
                            : 0;
                        const size_t available = offset < outputCapacity
                            ? outputCapacity - offset
                            : 0;
                        const size_t payloadSize = info.size > 0
                            ? std::min(static_cast<size_t>(info.size), available)
                            : available;
                        FfmpegVideoFrame p010Frame{};
                        frameReady =
                            outputColorFormat == kMediaCodecColorFormatP010 &&
                            copyMediaCodecP010(
                                outputBuffer != nullptr && offset < outputCapacity
                                    ? outputBuffer + offset
                                    : nullptr,
                                payloadSize,
                                outputWidth,
                                outputHeight,
                                outputStride,
                                outputSliceHeight,
                                info.presentationTimeUs,
                                stream->codecpar,
                                streamDolbyProfile,
                                &p010Frame
                            );
                        if (frameReady) {
                            pushFrame(std::move(p010Frame));
                        } else {
                            ++inputFailures;
                            XR_LOGW(
                                "DDDVR/FFmpegVideo",
                                "FFMPEG_VIDEO_P010_OUTPUT_UNAVAILABLE format=%d capacity=%zu offset=%zu size=%zu stride=%d slice=%d",
                                outputColorFormat,
                                outputCapacity,
                                offset,
                                payloadSize,
                                outputStride,
                                outputSliceHeight
                            );
                        }
                    }
                    const media_status_t releaseStatus = AMediaCodec_releaseOutputBuffer(
                        ndkMediaCodec,
                        static_cast<size_t>(outputIndex),
                        render && (useNdkMediaCodecSurface ||
                            useNdkMediaCodecP010ImageReader)
                    );
                    if (releaseStatus == AMEDIA_OK && render && frameReady) {
                        ++presentedFrames;
                        lastPresentedPositionMs_.store(
                            std::max<int64_t>(0, info.presentationTimeUs / 1000)
                        );
                        if (discardUntilPtsUs >= 0) {
                            XR_LOGI(
                                "DDDVR/FFmpegVideo",
                                "FFMPEG_VIDEO_SYNC_SEEK_COMPLETE targetMs=%lld presentedMs=%lld prerollDropped=%llu",
                                (long long)(discardUntilPtsUs / 1000),
                                (long long)(info.presentationTimeUs / 1000),
                                (unsigned long long)discardedPrerollFrames
                            );
                            discardUntilPtsUs = -1;
                            seekInFlight_.store(false);
                        }
                    }
                    logNdkStatsIfDue();
                    continue;
                }
                if (outputIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                    AMediaFormat* outputFormat = AMediaCodec_getOutputFormat(ndkMediaCodec);
                    if (outputFormat != nullptr && useNdkMediaCodecP010) {
                        AMediaFormat_getInt32(outputFormat, "width", &outputWidth);
                        AMediaFormat_getInt32(outputFormat, "height", &outputHeight);
                        if (!AMediaFormat_getInt32(outputFormat, "stride", &outputStride)) {
                            outputStride = outputWidth * 2;
                        }
                        if (!AMediaFormat_getInt32(outputFormat, "slice-height", &outputSliceHeight)) {
                            outputSliceHeight = outputHeight;
                        }
                        AMediaFormat_getInt32(outputFormat, "color-format", &outputColorFormat);
                    }
                    XR_LOGI(
                        "DDDVR/FFmpegVideo",
                        "FFMPEG_VIDEO_NDK_OUTPUT_FORMAT %s",
                        outputFormat != nullptr ? AMediaFormat_toString(outputFormat) : "null"
                    );
                    if (outputFormat != nullptr) AMediaFormat_delete(outputFormat);
                    continue;
                }
                break;
            }
        };

        auto queueMediaPacket = [&](const AVPacket* mediaPacket) -> bool {
            if (mediaPacket == nullptr || mediaPacket->data == nullptr || mediaPacket->size <= 0) {
                return true;
            }
            const int64_t packetPts = mediaPacket->pts != AV_NOPTS_VALUE
                ? mediaPacket->pts
                : mediaPacket->dts;
            const int64_t ptsUs = packetPts != AV_NOPTS_VALUE
                ? av_rescale_q(packetPts, stream->time_base, AVRational{1, 1000000})
                : 0;
            if (streamDolbyProfile > 0 && stream->codecpar->codec_id == AV_CODEC_ID_HEVC) {
                storeDolbyMetadata(dolbyRpuParser_.parseAnnexB(
                    mediaPacket->data,
                    static_cast<size_t>(mediaPacket->size),
                    ptsUs
                ));
            }
            while (running_.load()) {
                const ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(ndkMediaCodec, 10000);
                if (inputIndex >= 0) {
                    size_t capacity = 0;
                    uint8_t* inputBuffer = AMediaCodec_getInputBuffer(
                        ndkMediaCodec,
                        static_cast<size_t>(inputIndex),
                        &capacity
                    );
                    if (inputBuffer == nullptr || static_cast<size_t>(mediaPacket->size) > capacity) {
                        ++inputFailures;
                        XR_LOGE(
                            "DDDVR/FFmpegVideo",
                            "FFMPEG_VIDEO_NDK_INPUT_TOO_LARGE size=%d capacity=%zu",
                            mediaPacket->size,
                            capacity
                        );
                        AMediaCodec_queueInputBuffer(
                            ndkMediaCodec,
                            static_cast<size_t>(inputIndex),
                            0,
                            0,
                            0,
                            0
                        );
                        return false;
                    }
                    std::memcpy(inputBuffer, mediaPacket->data, static_cast<size_t>(mediaPacket->size));
                    const media_status_t queueStatus = AMediaCodec_queueInputBuffer(
                        ndkMediaCodec,
                        static_cast<size_t>(inputIndex),
                        0,
                        static_cast<size_t>(mediaPacket->size),
                        static_cast<uint64_t>(std::max<int64_t>(0, ptsUs)),
                        0
                    );
                    if (queueStatus == AMEDIA_OK) {
                        ++inputPackets;
                        buffering_.store(false);
                        drainOutput(0);
                        return true;
                    }
                    ++inputFailures;
                    return false;
                }
                drainOutput(0);
            }
            return false;
        };

        auto seekNdkToMs = [&](int64_t positionMs) {
            const int64_t targetUs = std::max<int64_t>(0, positionMs) * 1000;
            const int64_t streamTs = av_rescale_q(targetUs, AVRational{1, 1000000}, stream->time_base);
            const int retainedAudioStream = audioStream;
            stopDemuxWorker();
            const int seekErr = av_seek_frame(format, videoStream, streamTs, AVSEEK_FLAG_BACKWARD);
            if (seekErr >= 0) {
                AMediaCodec_flush(ndkMediaCodec);
                if (bitstreamFilter != nullptr) av_bsf_flush(bitstreamFilter);
                clearDolbyMetadata(streamDolbyProfile);
                if (retainedAudioStream >= 0 && openAudioStream(retainedAudioStream, targetUs)) {
                    startAudioWorker();
                }
                audioDiscardUntilPtsUs = std::max<int64_t>(0, targetUs - kSeekPrerollToleranceUs);
                discardUntilPtsUs = std::max<int64_t>(0, targetUs - kSeekPrerollToleranceUs);
                seekInFlight_.store(true);
                buffering_.store(true);
                XR_LOGI("DDDVR/FFmpegVideo", "FFMPEG_VIDEO_NDK_SEEK positionMs=%lld", (long long)positionMs);
            } else {
                seekInFlight_.store(false);
                XR_LOGW(
                    "DDDVR/FFmpegVideo",
                    "FFMPEG_VIDEO_NDK_SEEK_FAILED positionMs=%lld err=%s",
                    (long long)positionMs,
                    ffError(seekErr).c_str()
                );
            }
            startDemuxWorker();
        };

        if (startPositionMs > 0) seekNdkToMs(startPositionMs);
        XR_LOGI(
            "DDDVR/FFmpegVideo",
            "FFMPEG_VIDEO_READY stream=%d path=%s decoder=%s width=%d height=%d hdr=%d hdrKind=%s transfer=%d primaries=%d range=%d doviProfile=%d",
            videoStream,
            useNdkMediaCodecP010ImageReader
                ? "ndk_mediacodec_p010_ahardwarebuffer"
                : (useNdkMediaCodecP010
                    ? "ndk_mediacodec_p010"
                    : "ndk_mediacodec_surface"),
            ndkMime,
            stream->codecpar->width,
            stream->codecpar->height,
            hdr ? 1 : 0,
            hdrKind(stream->codecpar->color_trc, streamDolbyProfile > 0),
            stream->codecpar->color_trc,
            stream->codecpar->color_primaries,
            stream->codecpar->color_range,
            streamDolbyProfile
        );

        while (running_.load()) {
            if (!playing_.load()) {
                sleepBriefly();
                continue;
            }
            const int requestedAudio = requestedAudioStream_.load();
            if (requestedAudio >= 0 && requestedAudio != audioStream) {
                const int64_t switchPositionUs = masterPositionUs();
                stopDemuxWorker();
                if (openAudioStream(requestedAudio, switchPositionUs)) {
                    startAudioWorker();
                    audioDiscardUntilPtsUs = -1;
                }
                startDemuxWorker();
            }
            if (seekRequested_.exchange(false)) {
                seekNdkToMs(requestedPositionMs_.load());
            }

            if (!takeVideoPacket(packet)) {
                buffering_.store(true);
                drainOutput(10000);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }

            if (bitstreamFilter != nullptr) {
                err = av_bsf_send_packet(bitstreamFilter, packet);
                av_packet_unref(packet);
                if (err < 0) {
                    ++inputFailures;
                    XR_LOGW(
                        "DDDVR/FFmpegVideo",
                        "FFMPEG_VIDEO_NDK_BSF_SEND_FAILED err=%s",
                        ffError(err).c_str()
                    );
                    continue;
                }
                while (running_.load()) {
                    err = av_bsf_receive_packet(bitstreamFilter, filteredPacket);
                    if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) break;
                    if (err < 0) {
                        ++inputFailures;
                        break;
                    }
                    queueMediaPacket(filteredPacket);
                    av_packet_unref(filteredPacket);
                }
            } else {
                queueMediaPacket(packet);
                av_packet_unref(packet);
            }
            drainOutput(0);
        }

        cleanup();
        return;
    }

    closeNdkMediaCodec();
    const AVCodec* softwareDecoder = avcodec_find_decoder(stream->codecpar->codec_id);
    const char* mediaCodecName = hardwareDecoderName(stream->codecpar->codec_id);
    const AVCodec* mediaCodecDecoder = mediaCodecName != nullptr
        ? avcodec_find_decoder_by_name(mediaCodecName)
        : nullptr;
    const bool hardwareCandidate =
        mediaCodecDecoder != nullptr && javaVm_ != nullptr && outputSurface_ != nullptr;
    XR_LOGI(
        "DDDVR/FFmpegVideo",
        "FFMPEG_VIDEO_DECODER_PROBE codecId=%d software=%s hardware=%s hardwareAvailable=%d javaVm=%p surface=%p",
        stream->codecpar->codec_id,
        softwareDecoder != nullptr ? softwareDecoder->name : "none",
        mediaCodecName != nullptr ? mediaCodecName : "none",
        mediaCodecDecoder != nullptr ? 1 : 0,
        javaVm_,
        outputSurface_
    );
    if (softwareDecoder == nullptr && !hardwareCandidate) {
        setError("no decoder for codec");
        XR_LOGE("DDDVR/FFmpegVideo", "CURRENT_BLOCKER FFMPEG_NO_DECODER codec=%d", stream->codecpar->codec_id);
        cleanup();
        return;
    }

    auto openDecoder = [&](const AVCodec* candidate, bool hardware) -> int {
        closeCodec();
        decoder = candidate;
        codec = avcodec_alloc_context3(candidate);
        if (codec == nullptr) return AVERROR(ENOMEM);
        int openErr = avcodec_parameters_to_context(codec, stream->codecpar);
        if (openErr < 0) return openErr;
        codec->pkt_timebase = stream->time_base;

        if (hardware) {
            if (av_jni_set_java_vm(javaVm_, nullptr) < 0) {
                return AVERROR_EXTERNAL;
            }
            AVMediaCodecContext* mediaContext = av_mediacodec_alloc_context();
            if (mediaContext == nullptr) return AVERROR(ENOMEM);
            openErr = av_mediacodec_default_init(codec, mediaContext, outputSurface_);
            if (openErr < 0) {
                av_free(mediaContext);
                return openErr;
            }
            mediaCodecContextInitialized = true;
            av_opt_set_int(codec->priv_data, "ndk_codec", 0, 0);
            codec->thread_count = 1;
        } else {
            codec->thread_count = kSoftwareDecoderThreads;
            codec->thread_type = FF_THREAD_FRAME;
        }
        return avcodec_open2(codec, candidate, nullptr);
    };

    if (hardwareCandidate) {
        err = openDecoder(mediaCodecDecoder, true);
        if (err >= 0) {
            useHardwareSurface = true;
            hardwareSurfaceActive_.store(true);
            XR_LOGI(
                "DDDVR/FFmpegVideo",
                "FFMPEG_VIDEO_MEDIACODEC_ACTIVE decoder=%s surface=%p",
                decoder->name,
                outputSurface_
            );
        } else {
            XR_LOGW(
                "DDDVR/FFmpegVideo",
                "FFMPEG_VIDEO_MEDIACODEC_FALLBACK decoder=%s err=%s",
                mediaCodecName,
                ffError(err).c_str()
            );
        }
    }

    if (!useHardwareSurface) {
        if (softwareDecoder == nullptr) {
            setError("hardware decoder failed and no software decoder is available");
            cleanup();
            return;
        }
        err = openDecoder(softwareDecoder, false);
        if (err < 0) {
            setError("avcodec_open2 failed: " + ffError(err));
            XR_LOGE(
                "DDDVR/FFmpegVideo",
                "CURRENT_BLOCKER FFMPEG_CODEC_OPEN_FAILED decoder=%s err=%s",
                decoder->name,
                ffError(err).c_str()
            );
            cleanup();
            return;
        }
    }

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (packet == nullptr || frame == nullptr) {
        setError("packet/frame alloc failed");
        cleanup();
        return;
    }

    int64_t discardUntilPtsUs = -1;
    uint64_t discardedPrerollFrames = 0;

    auto seekToMs = [&](int64_t positionMs) {
        const int64_t targetUs = std::max<int64_t>(0, positionMs) * 1000;
        const int64_t streamTs = av_rescale_q(targetUs, AVRational{1, 1000000}, stream->time_base);
        const int retainedAudioStream = audioStream;
        stopDemuxWorker();
        const int seekErr = av_seek_frame(format, videoStream, streamTs, AVSEEK_FLAG_BACKWARD);
        if (seekErr >= 0) {
            avcodec_flush_buffers(codec);
            if (retainedAudioStream >= 0 && openAudioStream(retainedAudioStream, targetUs)) {
                startAudioWorker();
            }
            audioDiscardUntilPtsUs = std::max<int64_t>(0, targetUs - kSeekPrerollToleranceUs);
            discardUntilPtsUs = std::max<int64_t>(0, targetUs - kSeekPrerollToleranceUs);
            seekInFlight_.store(true);
            buffering_.store(true);
            std::lock_guard<std::mutex> lock(mutex_);
            frames_.clear();
            XR_LOGI("DDDVR/FFmpegVideo", "FFMPEG_VIDEO_SEEK positionMs=%lld", (long long)positionMs);
        } else {
            seekInFlight_.store(false);
            XR_LOGW("DDDVR/FFmpegVideo", "FFMPEG_VIDEO_SEEK_FAILED positionMs=%lld err=%s", (long long)positionMs, ffError(seekErr).c_str());
        }
        startDemuxWorker();
    };

    if (startPositionMs > 0) {
        seekToMs(startPositionMs);
    }

    auto statsStart = std::chrono::steady_clock::now();
    uint64_t decodedFrames = 0;
    uint64_t presentedFrames = 0;
    uint64_t conversionFailures = 0;
    AVColorTransferCharacteristic lastTransfer = codec->color_trc;
    AVColorPrimaries lastPrimaries = codec->color_primaries;
    AVColorRange lastRange = codec->color_range;
    bool lastDolbyVision = false;

    auto masterPositionUs = [&]() -> int64_t {
        const int64_t audioPositionUs = audioOutput_.positionUs();
        if (audioOutput_.active() && audioPositionUs >= 0) return audioPositionUs;
        const int64_t baseUs = requestedPositionMs_.load() * 1000;
        if (!playing_.load()) return baseUs;
        const int64_t updatedNs = playbackStateUpdateNs_.load();
        if (updatedNs <= 0) return baseUs;
        const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        return baseUs + std::max<int64_t>(0, nowNs - updatedNs) / 1000;
    };

    auto waitForPresentation = [&](int64_t ptsUs) {
        while (running_.load() && playing_.load()) {
            if (ptsUs <= masterPositionUs() + 20000) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };

    auto logStatsIfDue = [&]() {
        const auto now = std::chrono::steady_clock::now();
        if (now - statsStart < kStatsInterval) return;
        const double seconds = std::chrono::duration<double>(now - statsStart).count();
        const bool hdr = lastDolbyVision ||
            lastTransfer == AVCOL_TRC_SMPTE2084 ||
            lastTransfer == AVCOL_TRC_ARIB_STD_B67;
        const auto [videoPacketDepth, audioPacketDepth] = packetQueueDepths();
        XR_LOGI(
            "DDDVR/FFmpegVideo",
            "FFMPEG_VIDEO_STATS path=%s decoder=%s decodedFps=%.2f presentedFps=%.2f decoded=%llu presented=%llu prerollDropped=%llu conversionFailures=%llu lastPtsMs=%lld masterMs=%lld lagMs=%lld hdr=%d hdrKind=%s transfer=%d primaries=%d range=%d audioActive=%d audioStream=%d audioUnderflows=%llu videoPackets=%zu audioPackets=%zu",
            useHardwareSurface ? "mediacodec_surface" : "software_yuv_upload",
            decoder != nullptr ? decoder->name : "none",
            seconds > 0.0 ? static_cast<double>(decodedFrames) / seconds : 0.0,
            seconds > 0.0 ? static_cast<double>(presentedFrames) / seconds : 0.0,
            (unsigned long long)decodedFrames,
            (unsigned long long)presentedFrames,
            (unsigned long long)discardedPrerollFrames,
            (unsigned long long)conversionFailures,
            (long long)lastPresentedPositionMs_.load(),
            (long long)(masterPositionUs() / 1000),
            (long long)(masterPositionUs() / 1000 - lastPresentedPositionMs_.load()),
            hdr ? 1 : 0,
            hdrKind(lastTransfer, lastDolbyVision),
            lastTransfer,
            lastPrimaries,
            lastRange,
            audioOutput_.active() ? 1 : 0,
            selectedAudioStream_.load(),
            (unsigned long long)audioOutput_.underflowCount(),
            videoPacketDepth,
            audioPacketDepth
        );
        statsStart = now;
        decodedFrames = 0;
        presentedFrames = 0;
        discardedPrerollFrames = 0;
        conversionFailures = 0;
    };

    const bool initialHdr = codec->color_trc == AVCOL_TRC_SMPTE2084 ||
        codec->color_trc == AVCOL_TRC_ARIB_STD_B67;
    XR_LOGI(
        "DDDVR/FFmpegVideo",
        "FFMPEG_VIDEO_READY stream=%d path=%s decoder=%s width=%d height=%d pixFmt=%d hdr=%d hdrKind=%s transfer=%d primaries=%d range=%d",
        videoStream,
        useHardwareSurface ? "mediacodec_surface" : "software_yuv_upload",
        decoder->name,
        codec->width,
        codec->height,
        codec->pix_fmt,
        initialHdr ? 1 : 0,
        hdrKind(codec->color_trc, false),
        codec->color_trc,
        codec->color_primaries,
        codec->color_range
    );
    if (!useHardwareSurface &&
        (codec->width > kMaxSoftwareFrameWidth || codec->height > kMaxSoftwareFrameHeight)) {
        XR_LOGW(
            "DDDVR/FFmpegVideo",
            "FFMPEG_VIDEO_SOFTWARE_PERF_MODE input=%dx%d outputMax=%dx%d queue=%zu decoderThreads=%d",
            codec->width,
            codec->height,
            kMaxSoftwareFrameWidth,
            kMaxSoftwareFrameHeight,
            kMaxQueuedFrames,
            kSoftwareDecoderThreads
        );
    }

    while (running_.load()) {
        if (!playing_.load()) {
            sleepBriefly();
            continue;
        }

        const int requestedAudio = requestedAudioStream_.load();
        if (requestedAudio >= 0 && requestedAudio != audioStream) {
            const int64_t switchPositionUs = masterPositionUs();
            stopDemuxWorker();
            if (openAudioStream(requestedAudio, switchPositionUs)) {
                startAudioWorker();
                audioDiscardUntilPtsUs = -1;
            }
            startDemuxWorker();
        }

        if (seekRequested_.exchange(false)) {
            seekToMs(requestedPositionMs_.load());
        }

        if (!takeVideoPacket(packet)) {
            buffering_.store(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        err = avcodec_send_packet(codec, packet);
        av_packet_unref(packet);
        if (err < 0 && err != AVERROR(EAGAIN)) {
            XR_LOGW("DDDVR/FFmpegVideo", "FFMPEG_SEND_PACKET_FAILED err=%s", ffError(err).c_str());
            continue;
        }

        while (running_.load()) {
            err = avcodec_receive_frame(codec, frame);
            if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) break;
            if (err < 0) {
                XR_LOGW("DDDVR/FFmpegVideo", "FFMPEG_RECEIVE_FRAME_FAILED err=%s", ffError(err).c_str());
                break;
            }

            ++decodedFrames;
            lastTransfer = frame->color_trc != AVCOL_TRC_UNSPECIFIED
                ? frame->color_trc
                : codec->color_trc;
            lastPrimaries = frame->color_primaries != AVCOL_PRI_UNSPECIFIED
                ? frame->color_primaries
                : codec->color_primaries;
            lastRange = frame->color_range != AVCOL_RANGE_UNSPECIFIED
                ? frame->color_range
                : codec->color_range;
            lastDolbyVision = hasDolbyVisionSideData(frame);
            const int64_t decodedPtsUs = framePtsUs(frame, stream->time_base);
            const bool discardPreroll =
                discardUntilPtsUs >= 0 &&
                decodedPtsUs + kSeekPrerollToleranceUs < discardUntilPtsUs;

            if (discardPreroll) {
                if (useHardwareSurface && frame->format == AV_PIX_FMT_MEDIACODEC && frame->data[3] != nullptr) {
                    auto* mediaBuffer = reinterpret_cast<AVMediaCodecBuffer*>(frame->data[3]);
                    av_mediacodec_release_buffer(mediaBuffer, 0);
                }
                ++discardedPrerollFrames;
                logStatsIfDue();
                av_frame_unref(frame);
                continue;
            }

            bool framePresented = false;
            if (useHardwareSurface) {
                if (frame->format == AV_PIX_FMT_MEDIACODEC && frame->data[3] != nullptr) {
                    waitForPresentation(decodedPtsUs);
                    auto* mediaBuffer = reinterpret_cast<AVMediaCodecBuffer*>(frame->data[3]);
                    const int renderErr = av_mediacodec_release_buffer(mediaBuffer, 1);
                    if (renderErr >= 0) {
                        ++presentedFrames;
                        buffering_.store(false);
                        lastPresentedPositionMs_.store(std::max<int64_t>(0, decodedPtsUs / 1000));
                        framePresented = true;
                    } else {
                        XR_LOGW(
                            "DDDVR/FFmpegVideo",
                            "FFMPEG_VIDEO_MEDIACODEC_RENDER_FAILED err=%s",
                            ffError(renderErr).c_str()
                        );
                    }
                } else {
                    ++conversionFailures;
                    XR_LOGW(
                        "DDDVR/FFmpegVideo",
                        "FFMPEG_VIDEO_MEDIACODEC_UNEXPECTED_FRAME format=%d data3=%p",
                        frame->format,
                        frame->data[3]
                    );
                }
            } else {
                FfmpegVideoFrame decoded{};
                if (convertAndCopyFrame(frame, stream->time_base, &sws, &decoded)) {
                    if (decoded.transfer == FfmpegVideoColorTransfer::Unknown) decoded.transfer = mapTransfer(codec->color_trc);
                    if (decoded.primaries == FfmpegVideoColorPrimaries::Unknown) decoded.primaries = mapPrimaries(codec->color_primaries);
                    if (decoded.range == FfmpegVideoColorRange::Unknown) decoded.range = mapRange(codec->color_range);
                    decoded.dolbyProfile = streamDolbyProfile;
                    decoded.dolbyVision = decoded.dolbyVision || streamDolbyProfile > 0;
                    waitForPresentation(decoded.ptsUs);
                    const int64_t presentedPtsUs = decoded.ptsUs;
                    pushFrame(std::move(decoded));
                    ++presentedFrames;
                    buffering_.store(false);
                    lastPresentedPositionMs_.store(std::max<int64_t>(0, presentedPtsUs / 1000));
                    framePresented = true;
                } else {
                    ++conversionFailures;
                }
            }
            if (discardUntilPtsUs >= 0 && framePresented) {
                XR_LOGI(
                    "DDDVR/FFmpegVideo",
                    "FFMPEG_VIDEO_SYNC_SEEK_COMPLETE targetMs=%lld presentedMs=%lld prerollDropped=%llu",
                    (long long)(discardUntilPtsUs / 1000),
                    (long long)lastPresentedPositionMs_.load(),
                    (unsigned long long)discardedPrerollFrames
                );
                discardUntilPtsUs = -1;
                seekInFlight_.store(false);
            }
            logStatsIfDue();
            av_frame_unref(frame);
        }
    }

    cleanup();
#else
    (void)uri;
    (void)startPositionMs;
#endif
}
