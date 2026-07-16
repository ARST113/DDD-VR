# DDD-VR

> Подробное описание реализации HDR10, HLG и Dolby Vision: [docs/HDR_MODE.md](docs/HDR_MODE.md)

**DDD-VR** — Android/OpenXR видеоплеер для запуска видео в VR-режиме.

Проект построен вокруг OpenXR-активити, Media3/ExoPlayer и native OpenXR-рендера. Основной сценарий — открыть видео из внешнего приложения, лаунчера, Lampa/DDDPlayer2-интеграции или через `adb`, передав ссылку на видео через Android Intent.

Пакет приложения:

```text
top.rootu.dddvr
```

Основная activity:

```text
top.rootu.dddvr.xr.activity.OpenXrPlayerActivity
```

---

## Как это работает

DDD-VR получает входной URI через Android Intent, создаёт playback request, передаёт видео в Media3/ExoPlayer, получает Surface от native OpenXR-слоя и выводит видеокадры в VR-сцену.

Общая схема:

```text
External app / ADB / Launcher
        ↓
Intent.ACTION_VIEW
        ↓
OpenXrPlayerActivity
        ↓
VrIntentParser
        ↓
PlayerManager / PlaybackSession
        ↓
Media3 ExoPlayer
        ↓
Surface from OpenXR native layer
        ↓
OpenXR VR rendering
```

Основная OpenXR-активити сохраняет последний переданный URI. Если приложение открывается без нового URI, оно пробует восстановить последний запуск.

---

## Основные возможности

DDD-VR сейчас умеет:

* запускаться как обычное Android-приложение;
* запускаться как Leanback/TV-приложение;
* принимать видео через `Intent.ACTION_VIEW`;
* принимать URI через `intent.data`;
* принимать URI через `Intent.EXTRA_STREAM`;
* запускать воспроизведение с заданной позиции;
* определять или принимать явно тип входного стерео-видео;
* передавать видео в Media3/ExoPlayer;
* подключать ExoPlayer к Surface, созданному OpenXR-слоем;
* выводить видео через native OpenXR bridge;
* управлять воспроизведением из OpenXR input:

  * play/pause;
  * seek назад;
  * seek вперёд;
  * seek по таймлайну;
  * recenter;
  * show/hide menu;
  * exit;
* передавать состояние UI в native OpenXR-слой:

  * видимость меню;
  * прогресс воспроизведения;
  * состояние play/pause;
* передавать размер видео в native OpenXR-слой;
* сохранять последний URI и заголовок видео.

---

## Поддерживаемые входные схемы URI

`OpenXrPlayerActivity` объявлена как обработчик для следующих схем:

```text
http
https
ftp
rtsp
content
file
magnet
acestream
```

Примеры входных URI:

```text
https://example.com/video.mp4
http://192.168.1.10:8090/stream/video.m3u8
rtsp://example.com/live
content://media/external/video/media/123
file:///sdcard/Movies/video.mp4
magnet:?xt=urn:btih:...
acestream://...
```

DDD-VR принимает URI как входной адрес и передаёт его дальше в playback pipeline. Для обычных HTTP/HTTPS/RTSP/file/content-ссылок воспроизведение идёт через Media3/ExoPlayer.

---

## Поддерживаемые параметры Intent

### `Intent.EXTRA_TITLE`

Название видео.

```kotlin
intent.putExtra(Intent.EXTRA_TITLE, "Movie title")
```

### `start_position_ms`

Начальная позиция воспроизведения в миллисекундах.

```kotlin
intent.putExtra("start_position_ms", 60_000L)
```

### `position`

Альтернативный ключ для начальной позиции. Значение также трактуется как миллисекунды.

```kotlin
intent.putExtra("position", 60_000L)
```

### `stereo_mode`

Режим входного стерео-видео.

Поддерживаемые значения:

```text
sbs
sbs_reversed
ou
ou_reversed
```

Пример:

```kotlin
intent.putExtra("stereo_mode", "sbs")
```

### `stereo_layout`

Явное указание раскладки видео.

Поддерживаемые значения:

```text
mono
sbs
ou
```

Пример:

```kotlin
intent.putExtra("stereo_layout", "ou")
```

### `stereo_packing`

Тип упаковки стерео-видео.

Поддерживаемые значения:

```text
full
half
```

Пример:

```kotlin
intent.putExtra("stereo_packing", "full")
```

### `projection`

Тип проекции входного видео.

Поддерживаемые значения:

```text
flat
curved
equirect_180
equirect_360
```

Пример:

```kotlin
intent.putExtra("projection", "flat")
```

### `vr_projection`

VR-режим экрана.

Поддерживаемые значения:

```text
flat_vr_screen
curved
vr180
vr360
```

Пример:

```kotlin
intent.putExtra("vr_projection", "flat_vr_screen")
```

### `swap_eyes`

Меняет левый и правый глаз местами.

```kotlin
intent.putExtra("swap_eyes", true)
```

---

## Запуск через ADB

### Обычный запуск видео

```bash
adb shell am start \
  -n top.rootu.dddvr/.xr.activity.OpenXrPlayerActivity \
  -a android.intent.action.VIEW \
  -d "https://example.com/video.mp4"
```

### Запуск с названием

```bash
adb shell am start \
  -n top.rootu.dddvr/.xr.activity.OpenXrPlayerActivity \
  -a android.intent.action.VIEW \
  -d "https://example.com/video.mp4" \
  --es android.intent.extra.TITLE "Movie title"
```

### Запуск с позиции

```bash
adb shell am start \
  -n top.rootu.dddvr/.xr.activity.OpenXrPlayerActivity \
  -a android.intent.action.VIEW \
  -d "https://example.com/video.mp4" \
  --el start_position_ms 60000
```

### Запуск SBS-видео

```bash
adb shell am start \
  -n top.rootu.dddvr/.xr.activity.OpenXrPlayerActivity \
  -a android.intent.action.VIEW \
  -d "https://example.com/video_sbs.mp4" \
  --es stereo_mode sbs
```

### Запуск OU-видео

```bash
adb shell am start \
  -n top.rootu.dddvr/.xr.activity.OpenXrPlayerActivity \
  -a android.intent.action.VIEW \
  -d "https://example.com/video_ou.mp4" \
  --es stereo_mode ou
```

### Запуск SBS reversed

```bash
adb shell am start \
  -n top.rootu.dddvr/.xr.activity.OpenXrPlayerActivity \
  -a android.intent.action.VIEW \
  -d "https://example.com/video_sbs.mp4" \
  --es stereo_mode sbs_reversed
```

### Запуск OU reversed

```bash
adb shell am start \
  -n top.rootu.dddvr/.xr.activity.OpenXrPlayerActivity \
  -a android.intent.action.VIEW \
  -d "https://example.com/video_ou.mp4" \
  --es stereo_mode ou_reversed
```

### Запуск со сменой глаз

```bash
adb shell am start \
  -n top.rootu.dddvr/.xr.activity.OpenXrPlayerActivity \
  -a android.intent.action.VIEW \
  -d "https://example.com/video_sbs.mp4" \
  --es stereo_mode sbs \
  --ez swap_eyes true
```

### Запуск на плоском VR-экране

```bash
adb shell am start \
  -n top.rootu.dddvr/.xr.activity.OpenXrPlayerActivity \
  -a android.intent.action.VIEW \
  -d "https://example.com/video.mp4" \
  --es vr_projection flat_vr_screen
```

### Запуск curved VR-экрана

```bash
adb shell am start \
  -n top.rootu.dddvr/.xr.activity.OpenXrPlayerActivity \
  -a android.intent.action.VIEW \
  -d "https://example.com/video.mp4" \
  --es vr_projection curved
```

---

## Интеграция из Android-приложения

Минимальный Kotlin-пример:

```kotlin
val intent = Intent(Intent.ACTION_VIEW).apply {
    setClassName(
        "top.rootu.dddvr",
        "top.rootu.dddvr.xr.activity.OpenXrPlayerActivity"
    )

    setDataAndType(
        Uri.parse("https://example.com/video.mp4"),
        "video/*"
    )

    putExtra(Intent.EXTRA_TITLE, "Movie title")
    putExtra("start_position_ms", 0L)
    putExtra("stereo_mode", "sbs")
    putExtra("vr_projection", "flat_vr_screen")
    putExtra("swap_eyes", false)
}

startActivity(intent)
```

Пример для OU-видео:

```kotlin
val intent = Intent(Intent.ACTION_VIEW).apply {
    setClassName(
        "top.rootu.dddvr",
        "top.rootu.dddvr.xr.activity.OpenXrPlayerActivity"
    )

    data = Uri.parse("https://example.com/video_ou.mp4")

    putExtra(Intent.EXTRA_TITLE, "OU video")
    putExtra("stereo_mode", "ou")
    putExtra("stereo_packing", "full")
}

startActivity(intent)
```

Пример запуска с восстановлением позиции:

```kotlin
val intent = Intent(Intent.ACTION_VIEW).apply {
    setClassName(
        "top.rootu.dddvr",
        "top.rootu.dddvr.xr.activity.OpenXrPlayerActivity"
    )

    data = Uri.parse("https://example.com/video.mp4")

    putExtra(Intent.EXTRA_TITLE, "Movie title")
    putExtra("start_position_ms", 125_000L)
}

startActivity(intent)
```

---

## Автоопределение 3D-режима

Если `stereo_mode` не передан явно, DDD-VR пытается определить тип входного видео по URI через внутренний `StereoTypeDetector`.

Результат автоопределения маппится в один из режимов:

```text
MONO
SBS
OU
```

Для внешней интеграции надёжнее передавать режим явно:

```kotlin
putExtra("stereo_mode", "sbs")
```

или:

```kotlin
putExtra("stereo_mode", "ou")
```

---

## OpenXR input

Native OpenXR-слой передаёт в Kotlin-часть действия управления. Они обрабатываются в `OpenXrBridge` и `OpenXrPlayerActivity`.

Сейчас используются следующие действия:

| Действие       | Поведение                           |
| -------------- | ----------------------------------- |
| `PLAY_PAUSE`   | Переключает play/pause              |
| `SEEK_BACK`    | Перемотка на 15 секунд назад        |
| `SEEK_FORWARD` | Перемотка на 15 секунд вперёд       |
| `RECENTER`     | Запрос recenter                     |
| `SHOW_MENU`    | Показать или скрыть OpenXR UI       |
| `EXIT`         | Закрыть плеер                       |
| timeline seek  | Перемотка по прогрессу от 0 до 1000 |

---

## Воспроизведение

Воспроизведение строится на Media3/ExoPlayer.

Внутри `OpenXrPlayerActivity` создаётся один `MediaItem`:

```kotlin
MediaItem(
    uri = request.uri,
    title = request.title,
    startPositionMs = request.startPositionMs
)
```

Дальше он передаётся в `PlayerManager.loadPlaylist(...)`.

Когда native OpenXR-слой создаёт Surface, activity получает его через callback и подключает к `PlaybackSession`:

```kotlin
playbackSession.attachSurface(surface)
```

При изменении видеоформата ширина и высота видео передаются обратно в OpenXR bridge:

```kotlin
bridge.setVideoSize(width, height)
```

Состояние интерфейса передаётся в native-слой через:

```kotlin
bridge.setUiState(
    visible = openXrUiVisible,
    progressPermille = progress,
    playing = playing
)
```

---

## Восстановление последнего запуска

При запуске с новым URI DDD-VR сохраняет:

```text
last_uri
last_title
```

Хранилище:

```text
SharedPreferences: openxr_player
```

Если приложение открыто без URI, оно пробует восстановить последний сохранённый URI и запустить его повторно.

---

## Сборка

Требования для сборки:

```text
JDK 17
Android SDK Platform 36
Android Build Tools 36.0.0
CMake 3.22.1
Android NDK 27.0.12077973
```

Сборка debug APK:

```bash
./gradlew assembleDebug
```

Запуск тестов:

```bash
./gradlew test
```

APK после сборки:

```text
app/build/outputs/apk/debug/
```

Установка на устройство:

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

---

## GitHub Actions

В репозитории настроена автоматическая сборка Android APK.

Workflow делает:

```text
checkout
setup JDK 17
setup Android SDK
install platform-tools / android-36 / build-tools / cmake / ndk
./gradlew assembleDebug
проверку libdddvr_openxr.so внутри APK
./gradlew test
upload debug APK artifact
```

Имя artifact:

```text
dddvr-debug-apk
```

---

## Технический стек

Основные компоненты:

```text
Kotlin
AndroidX
Media3 / ExoPlayer
OpenXR
CMake
C++17
Room
OkHttp
Gson
Coil
Leanback
```

Параметры Android-сборки:

```text
namespace: top.rootu.dddvr
applicationId: top.rootu.dddvr
minSdk: 23
targetSdk: 34
compileSdk: 36
```

Native library:

```text
libdddvr_openxr.so
```

---

## Структура проекта

Основной Android-код:

```text
app/src/main/java/top/rootu/dddvr/
```

Ключевые части:

```text
xr/activity/OpenXrPlayerActivity.kt
xr/bridge/OpenXrBridge.kt
xr/model/OpenXrPlaybackConfig.kt
vr/activity/VrIntentParser.kt
vr/activity/VrPlaybackRequest.kt
player/PlayerManager.kt
core/playback/PlaybackSession.kt
model/MediaItem.kt
```

Native/OpenXR-код:

```text
app/src/main/cpp/
```

Сборочный файл native-части:

```text
app/src/main/cpp/CMakeLists.txt
```

Manifest:

```text
app/src/main/AndroidManifest.xml
```

---

## Интеграция с Lampa / DDDPlayer2

Для интеграции с Lampa или DDDPlayer2 внешняя сторона должна открыть DDD-VR через explicit intent и передать URI видео.

Базовый контракт:

```text
package: top.rootu.dddvr
activity: top.rootu.dddvr.xr.activity.OpenXrPlayerActivity
action: android.intent.action.VIEW
data: video URI
```

Рекомендуемые extras:

```text
Intent.EXTRA_TITLE
start_position_ms
stereo_mode
vr_projection
swap_eyes
```

Пример:

```kotlin
val intent = Intent(Intent.ACTION_VIEW).apply {
    setClassName(
        "top.rootu.dddvr",
        "top.rootu.dddvr.xr.activity.OpenXrPlayerActivity"
    )

    data = Uri.parse(videoUrl)

    putExtra(Intent.EXTRA_TITLE, title)
    putExtra("start_position_ms", positionMs)
    putExtra("stereo_mode", "sbs")
    putExtra("vr_projection", "flat_vr_screen")
    putExtra("swap_eyes", false)
}

context.startActivity(intent)
```

---

## Лицензия

Проект распространяется под лицензией GPL-3.0.

См. файл:

```text
LICENSE
```
