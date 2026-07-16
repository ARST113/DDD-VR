# HDR-режим в dd Vr

## Текущее состояние

HDR реализован в native-пайплайне FFmpeg/OpenXR и включается автоматически.
Отдельного переключателя HDR в интерфейсе нет: режим выбирается по метаданным
видеопотока и сигнализации Dolby Vision.

Текущий OpenXR-плеер запускает через FFmpeg как HDR-, так и SDR-видео. Демультиплексирование,
тайминг видео, выбор звуковой дорожки и цветовые метаданные остаются внутри одного
пайплайна, а не разделяются между FFmpeg и отдельным видеотрактом ExoPlayer.

## Определение HDR

Native-декодер считает поток HDR, когда выполняется хотя бы одно условие:

- transfer characteristic равен SMPTE ST 2084 (PQ/HDR10);
- transfer characteristic равен ARIB STD-B67 (HLG);
- в конфигурации HEVC обнаружен профиль Dolby Vision.

До получения метаданных потока маркеры `HDR`, `HDR10`, `Dolby`, `DoVi`, `DV` и
`2160p` в URI или названии используются только как ранняя подсказка для запуска.
Окончательный режим определяется по метаданным потока и кадров FFmpeg.

Вместе с кадром сохраняются:

- transfer: SDR, ST 2084 или HLG;
- primaries: BT.709 или BT.2020;
- range: limited или full;
- флаг и профиль Dolby Vision;
- разобранные RPU mapping/color metadata Dolby Vision, если они присутствуют.

## Пайплайн декодирования

```text
HTTP/file/content URI
        |
        v
FFmpeg demuxer (libavformat)
        |
        +--> FFmpeg audio decode --> OpenSL ES output
        |
        v
HEVC/H.264 Annex-B bitstream filter при необходимости
        |
        v
Android NDK MediaCodec
        |
        +--> HEVC HDR: P010 AImageReader/AHardwareBuffer (приоритет)
        +--> HEVC HDR: P010 byte-buffer (fallback)
        +--> обычный MediaCodec Surface (fallback)
        +--> software decode FFmpeg + загрузка YUV-текстур (последний fallback)
        |
        v
OpenGL ES: преобразование цвета и HDR mapping
        |
        v
OpenXR stereo swapchain
```

Для HEVC HDR приоритетный путь называется
`ndk_mediacodec_p010_ahardwarebuffer`. MediaCodec записывает 10-битный P010-кадр
в `AImageReader`, после чего его `AHardwareBuffer` импортируется в OpenGL ES и
читается VR-рендерером. Это позволяет не превращать 10-битный HDR-кадр в
8-битное CPU-изображение перед отрисовкой.

Если этот путь недоступен, dd Vr пробует прямой P010 output, затем обычный
MediaCodec Surface. Последний fallback - программный декодер FFmpeg с загрузкой
планарного YUV. Программное декодирование 4K функционально, но по производительности
не обязано соответствовать аппаратному декодеру Pico.

FFmpeg-библиотеки в репозитории собраны для `arm64-v8a`, то есть для целевого
runtime Pico. APK можно собирать и для других Android ABI, но native FFmpeg backend
включается только при наличии библиотек для соответствующей архитектуры.

## HDR10 и HLG

HDR обрабатывается шейдерами `CinemaScreenRenderer`:

1. YUV/P010 преобразуется с учётом range и цветовых метаданных кадра.
2. PQ-сигнал линеаризуется обратной функцией SMPTE ST 2084.
3. Яркие области сжимаются filmic-кривой типа Hable.
4. Применяются gamma, saturation и smoothing настроенного HDR-профиля.
5. Значения ограничиваются диапазоном вывода только на финальном этапе шейдера.

Для HLG используется отдельная ветка обратного преобразования ARIB STD-B67 и
собственный профиль насыщенности/сглаживания. SDR-кадры не проходят через
HDR-кривую и используют обычные параметры brightness/contrast/saturation/gamma.

Базовый HDR-профиль согласован с настройкой связанного исследовательского проекта
4XVR:

- HDR brightness: `0.5`;
- gamma power: `0.35`;
- smoothing: `0.3996094`;
- saturation power: `0.85` для HDR10/HLG и `0.73` для Dolby Vision.

Это параметры рендера, а не mastering-display metadata файла. Один профиль
используется для mono и стереоскопического контента, поэтому 2D, SBS и OU не
получают различную цветокоррекцию.

## Dolby Vision

Dolby Vision определяется по конфигурации HEVC. Для Annex-B HEVC класс
`DolbyRpuParser` также читает RPU из битстрима и связывает метаданные с PTS кадра.

Когда RPU доступен, рендерер загружает:

- нелинейные mapping tables для intensity, chroma-T и chroma-P;
- коэффициенты и offsets преобразования Dolby YCC-to-RGB;
- данные range и цветовой матрицы base layer.

Таблицы загружаются как 2D- или 3D-текстуры OpenGL и применяются перед PQ tone
mapping. Для Profile 8 настроен отдельный профиль `strong`; это основной
проверенный сценарий Dolby Vision в текущей версии.

Реализация независимая и не лицензированная. Это не сертификация Dolby Vision и
не обещание побитового совпадения с сертифицированным телевизионным трактом.
Dual-layer потоки и профили, которым требуется недоступная реконструкция
enhancement layer, могут использовать только base layer и требуют отдельной
проверки.

## Цветовое пространство OpenXR

При активном HDR dd Vr через `XR_FB_color_space` запрашивает первое поддерживаемое
runtime цветовое пространство в таком порядке:

```text
REC2020 -> P3 -> REC709 -> UNMANAGED
```

Для SDR порядок другой:

```text
REC709 -> QUEST -> P3 -> UNMANAGED
```

Фактически доступное пространство определяет runtime Pico. Выбранное значение
записывается в лог `XR_COLOR_SPACE_SET`.

OpenXR swapchain предпочитает `GL_SRGB8_ALPHA8`, затем `GL_RGBA16F`,
`GL_RGB10_A2` и `GL_RGBA8`. При наличии `GL_EXT_sRGB_write_control` автоматическое
sRGB-преобразование framebuffer отключается во время вывода уже обработанного
сигнала. Это предотвращает повторное кодирование, которое давало бы выцветшие
светлые области и неправильный контраст на Pico.

Итоговый режим является GPU tone-mapped HDR presentation через OpenXR compositor,
а не необработанным HDMI-подобным HDR passthrough.

## Диагностика на Pico

Команда для основных HDR-логов:

```bash
adb logcat -v time DDDVR/FFmpegVideo:I DDDVR/DolbyRpu:I DDDVR/OpenXRColor:I '*:S'
```

При нормальном запуске 4K HDR/Dolby Vision должны появиться маркеры:

```text
FFMPEG_VIDEO_COLOR_METADATA ...
FFMPEG_VIDEO_READY ... width=3840 height=2160 hdr=1 hdrKind=... doviProfile=...
FFMPEG_VIDEO_FIRST_AHB_FRAME ...
XR_HDR_PROFILE ...
XR_COLOR_SPACE_SET ... reason=hdr
XR_SRGB_WRITE_CONTROL extension=1 ... after=0 ...
FFMPEG_VIDEO_STATS ... decodedFps=... presentedFps=... hdr=1 ...
```

Предпочтительный путь декодирования:

```text
path=ndk_mediacodec_p010_ahardwarebuffer
```

Fallback-маркеры не всегда означают ошибку, но объясняют возможную разницу в
производительности или цвете:

```text
path=ndk_mediacodec_p010
path=ndk_mediacodec_surface
path=software_yuv_upload
FFMPEG_VIDEO_P010_OUTPUT_UNAVAILABLE
FFMPEG_VIDEO_MEDIACODEC_FALLBACK
```

Для плавного воспроизведения `presentedFps` должен оставаться близким к FPS
источника, глубина очередей не должна бесконечно расти, а `lagMs` не должен
постоянно увеличиваться.

## Ограничения

- Качество HDR зависит от правильных transfer/primaries/range метаданных файла.
- Определение по имени является только стартовой подсказкой и не исправляет
  ошибочные метаданные потока.
- Profile 8 - основной проверенный путь Dolby Vision; другие профили требуют
  проверки на конкретных источниках.
- Runtime VR может не предоставить REC2020 и выбрать P3, REC709 или UNMANAGED.
- Software decode 4K может быть слишком медленным для стабильного VR.
- HDR проходит tone mapping через OpenXR compositor, а не native display passthrough.
- Ручного выбора HDR-профиля в интерфейсе пока нет.

## Основные файлы реализации

- `app/src/main/cpp/video/FfmpegVideoDecoder.cpp` - demux, decode, P010 и
  определение метаданных.
- `app/src/main/cpp/video/DolbyRpuParser.cpp` - разбор Dolby Vision RPU.
- `app/src/main/cpp/gl/DolbyMappingTexture.cpp` - RPU mapping/color textures.
- `app/src/main/cpp/gl/CinemaScreenRenderer.cpp` - YUV conversion, PQ/HLG и
  Dolby Vision shader mapping.
- `app/src/main/cpp/openxr/OpenXrSession.cpp` - выбор OpenXR color space.
- `app/src/main/cpp/openxr/OpenXrSwapchain.cpp` - приоритет формата swapchain.
- `app/src/main/java/top/rootu/dddvr/xr/activity/OpenXrPlayerActivity.kt` -
  FFmpeg-only playback lifecycle и состояние UI.
