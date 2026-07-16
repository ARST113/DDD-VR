#pragma once

#include <android/hardware_buffer.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>

namespace dddvr::androidimage {

bool available();
media_status_t readerNewWithUsage(
    int32_t width,
    int32_t height,
    int32_t format,
    uint64_t usage,
    int32_t maxImages,
    AImageReader** reader
);
media_status_t readerSetBufferRemovedListener(
    AImageReader* reader,
    AImageReader_BufferRemovedListener* listener
);
media_status_t readerGetWindow(AImageReader* reader, ANativeWindow** window);
void readerDelete(AImageReader* reader);
media_status_t readerAcquireLatestImage(AImageReader* reader, AImage** image);
media_status_t imageGetHardwareBuffer(const AImage* image, AHardwareBuffer** buffer);
media_status_t imageGetWidth(const AImage* image, int32_t* width);
media_status_t imageGetHeight(const AImage* image, int32_t* height);
media_status_t imageGetTimestamp(const AImage* image, int64_t* timestampNs);
void imageDelete(AImage* image);
void hardwareBufferDescribe(const AHardwareBuffer* buffer, AHardwareBuffer_Desc* description);

}
