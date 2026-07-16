# DDD-VR FFmpeg Video Backend

This folder is the native FFmpeg video backend input.

Expected layout:

```text
app/src/main/ffmpeg/include/libavcodec/avcodec.h
app/src/main/ffmpeg/include/libavformat/avformat.h
app/src/main/ffmpeg/include/libavutil/...
app/src/main/ffmpeg/include/libswscale/swscale.h

app/src/main/ffmpeg/arm64-v8a/libavformat.so
app/src/main/ffmpeg/arm64-v8a/libavcodec.so
app/src/main/ffmpeg/arm64-v8a/libavutil.so
app/src/main/ffmpeg/arm64-v8a/libswscale.so
app/src/main/ffmpeg/arm64-v8a/libswresample.so
```

`app/src/main/jniLibs/<abi>/lib*.so` is also accepted for the libraries.

When all headers and libraries are present, CMake defines
`DDDVR_HAS_FFMPEG_VIDEO=1`. Without them the app still builds, logs
`FFMPEG_VIDEO_NOT_LINKED`, and falls back to the existing ExoPlayer/OES path.

