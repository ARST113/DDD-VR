package top.rootu.dddvr.xr.ui

data class OpenXrPlayerUiState(
    val visible: Boolean,
    val pinned: Boolean,
    val playing: Boolean,
    val buffering: Boolean,
    val muted: Boolean,

    val positionMs: Long,
    val durationMs: Long,
    val bufferedPositionMs: Long,

    val title: String,
    val projectionModeLabel: String,
    val audioTrackLabel: String,
    val subtitleTrackLabel: String,

    val activeModal: Int,
    val activeSettingsTab: Int,
    val hoverTarget: Int,

    val display: OpenXrDisplayUiState,
    val subtitles: OpenXrSubtitlesUiState,
    val audio: OpenXrAudioUiState,

    val playlistRows: List<OpenXrPlaylistRow>,
    val audioTracks: List<OpenXrTrackRow>,
    val subtitleTracks: List<OpenXrTrackRow>
)

data class OpenXrTrackRow(
    val id: String,
    val title: String,
    val subtitle: String,
    val selected: Boolean,
    val enabled: Boolean
)

data class OpenXrPlaylistRow(
    val id: String,
    val title: String,
    val subtitle: String,
    val selected: Boolean
)

data class OpenXrDisplayUiState(
    val aspectRatio: String,
    val playbackSpeed: Float,
    val enhanceVideo: Boolean,
    val hdrVideo: Boolean,
    val brightness: Float
)

data class OpenXrSubtitlesUiState(
    val enabled: Boolean,
    val delayMs: Int,
    val sizeLabel: String,
    val positionLabel: String
)

data class OpenXrAudioUiState(
    val delayMs: Int,
    val spatialAudio: Boolean
)
