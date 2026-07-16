#include "AndroidImageApi.h"

#include <dlfcn.h>

namespace dddvr::androidimage {
namespace {

template <typename T>
T load(void* library, const char* name) {
    return library != nullptr
        ? reinterpret_cast<T>(dlsym(library, name))
        : nullptr;
}

struct Api {
    using ReaderNewWithUsage = media_status_t (*)(
        int32_t, int32_t, int32_t, uint64_t, int32_t, AImageReader**
    );
    using ReaderSetBufferRemovedListener = media_status_t (*)(
        AImageReader*, AImageReader_BufferRemovedListener*
    );
    using ReaderGetWindow = media_status_t (*)(AImageReader*, ANativeWindow**);
    using ReaderDelete = void (*)(AImageReader*);
    using ReaderAcquireLatestImage = media_status_t (*)(AImageReader*, AImage**);
    using ImageGetHardwareBuffer = media_status_t (*)(const AImage*, AHardwareBuffer**);
    using ImageGetDimension = media_status_t (*)(const AImage*, int32_t*);
    using ImageGetTimestamp = media_status_t (*)(const AImage*, int64_t*);
    using ImageDelete = void (*)(AImage*);
    using HardwareBufferDescribe = void (*)(const AHardwareBuffer*, AHardwareBuffer_Desc*);

    void* media = dlopen("libmediandk.so", RTLD_NOW | RTLD_LOCAL);
    void* android = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    ReaderNewWithUsage readerNewWithUsage = load<ReaderNewWithUsage>(
        media, "AImageReader_newWithUsage"
    );
    ReaderSetBufferRemovedListener readerSetBufferRemovedListener =
        load<ReaderSetBufferRemovedListener>(
            media, "AImageReader_setBufferRemovedListener"
        );
    ReaderGetWindow readerGetWindow = load<ReaderGetWindow>(
        media, "AImageReader_getWindow"
    );
    ReaderDelete readerDelete = load<ReaderDelete>(media, "AImageReader_delete");
    ReaderAcquireLatestImage readerAcquireLatestImage =
        load<ReaderAcquireLatestImage>(
            media, "AImageReader_acquireLatestImage"
        );
    ImageGetHardwareBuffer imageGetHardwareBuffer = load<ImageGetHardwareBuffer>(
        media, "AImage_getHardwareBuffer"
    );
    ImageGetDimension imageGetWidth = load<ImageGetDimension>(media, "AImage_getWidth");
    ImageGetDimension imageGetHeight = load<ImageGetDimension>(media, "AImage_getHeight");
    ImageGetTimestamp imageGetTimestamp = load<ImageGetTimestamp>(
        media, "AImage_getTimestamp"
    );
    ImageDelete imageDelete = load<ImageDelete>(media, "AImage_delete");
    HardwareBufferDescribe hardwareBufferDescribe = load<HardwareBufferDescribe>(
        android, "AHardwareBuffer_describe"
    );

    bool ready() const {
        return readerNewWithUsage != nullptr &&
            readerSetBufferRemovedListener != nullptr &&
            readerGetWindow != nullptr &&
            readerDelete != nullptr &&
            readerAcquireLatestImage != nullptr &&
            imageGetHardwareBuffer != nullptr &&
            imageGetWidth != nullptr &&
            imageGetHeight != nullptr &&
            imageGetTimestamp != nullptr &&
            imageDelete != nullptr &&
            hardwareBufferDescribe != nullptr;
    }
};

Api& api() {
    static Api instance;
    return instance;
}

media_status_t unsupported() {
    return AMEDIA_ERROR_UNKNOWN;
}

}

bool available() {
    return api().ready();
}

media_status_t readerNewWithUsage(
    int32_t width,
    int32_t height,
    int32_t format,
    uint64_t usage,
    int32_t maxImages,
    AImageReader** reader
) {
    return api().readerNewWithUsage != nullptr
        ? api().readerNewWithUsage(width, height, format, usage, maxImages, reader)
        : unsupported();
}

media_status_t readerSetBufferRemovedListener(
    AImageReader* reader,
    AImageReader_BufferRemovedListener* listener
) {
    return api().readerSetBufferRemovedListener != nullptr
        ? api().readerSetBufferRemovedListener(reader, listener)
        : unsupported();
}

media_status_t readerGetWindow(AImageReader* reader, ANativeWindow** window) {
    return api().readerGetWindow != nullptr
        ? api().readerGetWindow(reader, window)
        : unsupported();
}

void readerDelete(AImageReader* reader) {
    if (api().readerDelete != nullptr) api().readerDelete(reader);
}

media_status_t readerAcquireLatestImage(AImageReader* reader, AImage** image) {
    return api().readerAcquireLatestImage != nullptr
        ? api().readerAcquireLatestImage(reader, image)
        : unsupported();
}

media_status_t imageGetHardwareBuffer(const AImage* image, AHardwareBuffer** buffer) {
    return api().imageGetHardwareBuffer != nullptr
        ? api().imageGetHardwareBuffer(image, buffer)
        : unsupported();
}

media_status_t imageGetWidth(const AImage* image, int32_t* width) {
    return api().imageGetWidth != nullptr
        ? api().imageGetWidth(image, width)
        : unsupported();
}

media_status_t imageGetHeight(const AImage* image, int32_t* height) {
    return api().imageGetHeight != nullptr
        ? api().imageGetHeight(image, height)
        : unsupported();
}

media_status_t imageGetTimestamp(const AImage* image, int64_t* timestampNs) {
    return api().imageGetTimestamp != nullptr
        ? api().imageGetTimestamp(image, timestampNs)
        : unsupported();
}

void imageDelete(AImage* image) {
    if (api().imageDelete != nullptr) api().imageDelete(image);
}

void hardwareBufferDescribe(
    const AHardwareBuffer* buffer,
    AHardwareBuffer_Desc* description
) {
    if (api().hardwareBufferDescribe != nullptr) {
        api().hardwareBufferDescribe(buffer, description);
    }
}

}
