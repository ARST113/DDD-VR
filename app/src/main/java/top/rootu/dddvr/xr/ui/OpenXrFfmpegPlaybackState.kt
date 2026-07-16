package top.rootu.dddvr.xr.ui

data class OpenXrFfmpegPlaybackState(
    val running: Boolean = false,
    val playing: Boolean = false,
    val buffering: Boolean = true,
    val hdr: Boolean = false,
    val audioActive: Boolean = false,
    val positionMs: Long = 0L,
    val durationMs: Long = 0L,
    val bufferedPositionMs: Long = 0L,
    val width: Int = 0,
    val height: Int = 0,
    val selectedAudioStream: Int = -1
) {
    companion object {
        fun fromNative(values: LongArray?): OpenXrFfmpegPlaybackState {
            if (values == null || values.size < 11) return OpenXrFfmpegPlaybackState()
            return OpenXrFfmpegPlaybackState(
                running = values[0] != 0L,
                playing = values[1] != 0L,
                buffering = values[2] != 0L,
                hdr = values[3] != 0L,
                audioActive = values[4] != 0L,
                positionMs = values[5].coerceAtLeast(0L),
                durationMs = values[6].coerceAtLeast(0L),
                bufferedPositionMs = values[7].coerceAtLeast(0L),
                width = values[8].toInt().coerceAtLeast(0),
                height = values[9].toInt().coerceAtLeast(0),
                selectedAudioStream = values[10].toInt()
            )
        }
    }
}
