#!/usr/bin/env bash
set -euo pipefail

NDK_ROOT="/f/Codex/android-sdk/ndk/27.0.12077973"
TOOLCHAIN="$NDK_ROOT/toolchains/llvm/prebuilt/windows-x86_64"
MAKE_BIN="$NDK_ROOT/prebuilt/windows-x86_64/bin"
PREFIX="/f/Codex/DDD-VR/app/src/main/ffmpeg"
SOURCE="/f/Codex/ffmpeg-android-src"

export PATH="$TOOLCHAIN/bin:$MAKE_BIN:$PATH"
cd "$SOURCE"

make distclean >/dev/null 2>&1 || true

./configure \
  --prefix="$PREFIX" \
  --libdir="$PREFIX/arm64-v8a" \
  --incdir="$PREFIX/include" \
  --target-os=android \
  --arch=aarch64 \
  --cpu=armv8-a \
  --enable-cross-compile \
  --cross-prefix=aarch64-linux-android- \
  --cc=aarch64-linux-android24-clang \
  --cxx=aarch64-linux-android24-clang++ \
  --ar=llvm-ar \
  --ranlib=llvm-ranlib \
  --strip=llvm-strip \
  --nm=llvm-nm \
  --enable-shared \
  --disable-static \
  --disable-programs \
  --disable-doc \
  --disable-avdevice \
  --disable-avfilter \
  --disable-postproc \
  --disable-encoders \
  --disable-muxers \
  --disable-everything \
  --enable-avcodec \
  --enable-avformat \
  --enable-avutil \
  --enable-swscale \
  --enable-swresample \
  --enable-network \
  --enable-pic \
  --disable-debug \
  --enable-decoder=h264 \
  --enable-decoder=hevc \
  --enable-decoder=mpeg4 \
  --enable-decoder=mpeg1video \
  --enable-decoder=mpeg2video \
  --enable-decoder=vp8 \
  --enable-decoder=vp9 \
  --enable-decoder=av1 \
  --enable-decoder=rawvideo \
  --enable-decoder=wrapped_avframe \
  --enable-decoder=aac \
  --enable-decoder=ac3 \
  --enable-decoder=eac3 \
  --enable-decoder=dca \
  --enable-decoder=mlp \
  --enable-decoder=truehd \
  --enable-decoder=flac \
  --enable-decoder=alac \
  --enable-decoder=mp3 \
  --enable-decoder=opus \
  --enable-decoder=vorbis \
  --enable-decoder=pcm_s16le \
  --enable-decoder=pcm_s16be \
  --enable-decoder=pcm_s24le \
  --enable-decoder=pcm_s24be \
  --enable-decoder=pcm_s32le \
  --enable-decoder=pcm_f32le \
  --enable-decoder=pcm_f64le \
  --enable-parser=h264 \
  --enable-parser=hevc \
  --enable-parser=mpeg4video \
  --enable-parser=mpegvideo \
  --enable-parser=vp8 \
  --enable-parser=vp9 \
  --enable-parser=av1 \
  --enable-parser=aac \
  --enable-parser=ac3 \
  --enable-parser=dca \
  --enable-parser=flac \
  --enable-parser=mlp \
  --enable-parser=opus \
  --enable-parser=mpegaudio \
  --enable-demuxer=matroska \
  --enable-demuxer=mov \
  --enable-demuxer=mpegts \
  --enable-demuxer=mpegps \
  --enable-demuxer=mpegvideo \
  --enable-demuxer=avi \
  --enable-demuxer=flv \
  --enable-demuxer=h264 \
  --enable-demuxer=hevc \
  --enable-demuxer=m4v \
  --enable-demuxer=hls \
  --enable-demuxer=rawvideo \
  --enable-protocol=file \
  --enable-protocol=http \
  --enable-protocol=tcp \
  --enable-protocol=udp \
  --enable-protocol=pipe \
  --enable-protocol=cache \
  --enable-protocol=crypto \
  --enable-protocol=data \
  --enable-bsf=h264_mp4toannexb \
  --enable-bsf=hevc_mp4toannexb \
  --enable-bsf=extract_extradata \
  --enable-bsf=null \
  --enable-bsf=av1_frame_merge \
  --enable-bsf=av1_frame_split \
  --enable-bsf=vp9_superframe \
  --enable-bsf=vp9_superframe_split \
  --extra-cflags=-O3

make -j8
make install
