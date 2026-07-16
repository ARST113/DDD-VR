package top.rootu.dddvr.xr.bridge

import android.app.Activity
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Surface
import top.rootu.dddvr.xr.input.OpenXrInputMapper
import top.rootu.dddvr.xr.model.OpenXrPlaybackConfig
import top.rootu.dddvr.xr.ui.OpenXrPlayerUiAction
import top.rootu.dddvr.xr.ui.OpenXrPlayerUiState
import top.rootu.dddvr.xr.ui.OpenXrFfmpegPlaybackState
import top.rootu.dddvr.xr.ui.OpenXrTrackRow

class OpenXrBridge(
    private val activity: Activity,
    private val callbacks: Callbacks,
    private val config: OpenXrPlaybackConfig
) {
    private var nativeHandle: Long = 0L
    private val mainHandler = Handler(Looper.getMainLooper())

    fun start(): Boolean {
        nativeHandle = nativeCreate(activity, activity.applicationContext, config)
        if (nativeHandle == 0L) {
            Log.e("DDDVR/OpenXR", "nativeCreate returned null handle")
            return false
        }
        val started = nativeStart(nativeHandle)
        if (!started) {
            nativeDestroy(nativeHandle)
            nativeHandle = 0L
            return false
        }
        return true
    }

    fun onResume() {
        if (nativeHandle == 0L) return
        nativeResume(nativeHandle)
    }

    fun onPause() {
        if (nativeHandle == 0L) return
        nativePause(nativeHandle)
    }

    fun setVideoSize(width: Int, height: Int) {
        if (nativeHandle == 0L || width <= 0 || height <= 0) return
        nativeSetVideoSize(nativeHandle, width, height)
    }

    fun setUiState(
        visible: Boolean,
        playing: Boolean,
        buffering: Boolean,
        positionMs: Long,
        durationMs: Long,
        bufferedPositionMs: Long,
        title: String,
        stereoModeLabel: String,
        audioTrackLabel: String,
        audioTrackLabels: Array<String>,
        selectedAudioTrackIndex: Int
    ) {
        if (nativeHandle == 0L) return
        nativeSetUiState(
            nativeHandle,
            visible,
            playing,
            buffering,
            positionMs.coerceAtLeast(0L),
            durationMs.coerceAtLeast(0L),
            bufferedPositionMs.coerceAtLeast(0L),
            title,
            stereoModeLabel,
            audioTrackLabel,
            audioTrackLabels,
            selectedAudioTrackIndex
        )
    }

    fun setPlayerUiState(state: OpenXrPlayerUiState) {
        if (nativeHandle == 0L) return
        nativeSetPlayerUiState(nativeHandle, state)
    }

    fun startFfmpegVideoSource(uri: String, startPositionMs: Long) {
        if (nativeHandle == 0L || uri.isBlank()) return
        nativeStartFfmpegVideoSource(nativeHandle, uri, startPositionMs.coerceAtLeast(0L))
    }

    fun stopFfmpegVideoSource() {
        if (nativeHandle == 0L) return
        nativeStopFfmpegVideoSource(nativeHandle)
    }

    fun setFfmpegPlaybackState(playing: Boolean, positionMs: Long, forceSeek: Boolean = false) {
        if (nativeHandle == 0L) return
        nativeSetFfmpegPlaybackState(nativeHandle, playing, positionMs.coerceAtLeast(0L), forceSeek)
    }

    fun getFfmpegPlaybackState(): OpenXrFfmpegPlaybackState {
        if (nativeHandle == 0L) return OpenXrFfmpegPlaybackState()
        return OpenXrFfmpegPlaybackState.fromNative(nativeGetFfmpegPlaybackState(nativeHandle))
    }

    fun getFfmpegAudioTracks(): List<OpenXrTrackRow> {
        if (nativeHandle == 0L) return emptyList()
        val ids = nativeGetFfmpegAudioTrackIds(nativeHandle)
        val titles = nativeGetFfmpegAudioTrackTitles(nativeHandle)
        val subtitles = nativeGetFfmpegAudioTrackSubtitles(nativeHandle)
        val selectedStream = getFfmpegPlaybackState().selectedAudioStream
        return ids.indices.map { index ->
            OpenXrTrackRow(
                id = ids[index],
                title = titles.getOrNull(index).orEmpty(),
                subtitle = subtitles.getOrNull(index).orEmpty(),
                selected = ids[index] == "ffmpeg_audio:$selectedStream",
                enabled = true
            )
        }
    }

    fun selectFfmpegAudioTrack(trackId: String): Boolean {
        if (nativeHandle == 0L || trackId.isBlank()) return false
        return nativeSelectFfmpegAudioTrack(nativeHandle, trackId)
    }

    fun setFfmpegMuted(muted: Boolean) {
        if (nativeHandle != 0L) nativeSetFfmpegMuted(nativeHandle, muted)
    }

    fun destroy() {
        if (nativeHandle == 0L) return
        nativeDestroy(nativeHandle)
        nativeHandle = 0L
    }

    @Suppress("unused")
    private fun onVideoSurfaceReadyFromNative(surface: Surface) {
        mainHandler.post { callbacks.onVideoSurfaceReady(surface) }
    }

    @Suppress("unused")
    private fun onInputActionFromNative(actionCode: Int) {
        val action = OpenXrInputMapper.fromNativeCode(actionCode)
        mainHandler.post {
            when (action) {
                top.rootu.dddvr.xr.input.OpenXrInputAction.PLAY_PAUSE -> callbacks.onPlayPause()
                top.rootu.dddvr.xr.input.OpenXrInputAction.SEEK_BACK -> callbacks.onSeekBy(-15_000)
                top.rootu.dddvr.xr.input.OpenXrInputAction.SEEK_FORWARD -> callbacks.onSeekBy(15_000)
                top.rootu.dddvr.xr.input.OpenXrInputAction.RECENTER -> callbacks.onRecenter()
                top.rootu.dddvr.xr.input.OpenXrInputAction.SHOW_MENU -> callbacks.onShowMenu()
                top.rootu.dddvr.xr.input.OpenXrInputAction.EXIT -> callbacks.onExit()
                else -> Unit
            }
        }
    }

    @Suppress("unused")
    private fun onTimelineSeekFromNative(progressPermille: Int) {
        mainHandler.post { callbacks.onSeekToProgress(progressPermille.coerceIn(0, 1000)) }
    }

    @Suppress("unused")
    private fun onAudioTrackSelectedFromNative(trackIndex: Int) {
        mainHandler.post { callbacks.onSelectAudioTrack(trackIndex) }
    }

    @Suppress("unused")
    private fun onPlayerUiActionFromNative(
        actionType: Int,
        intValue: Int,
        floatValue: Float,
        stringValue: String?
    ) {
        Log.i(
            "DDDVR/OpenXR",
            "XR_PLAYER_UI_ACTION_RECEIVED type=$actionType int=$intValue float=$floatValue text=${stringValue.orEmpty()}"
        )
        val action = OpenXrPlayerUiAction.fromNative(actionType, intValue, floatValue, stringValue)
        mainHandler.post { callbacks.onPlayerUiAction(action) }
    }

    @Suppress("unused")
    private fun onNativeLog(message: String) {
        Log.i("DDDVR/OpenXR", message)
    }

    interface Callbacks {
        fun onVideoSurfaceReady(surface: Surface)
        fun onPlayPause()
        fun onSeekBy(deltaMs: Long)
        fun onSeekToProgress(progressPermille: Int)
        fun onSelectAudioTrack(trackIndex: Int)
        fun onPlayerUiAction(action: OpenXrPlayerUiAction)
        fun onRecenter()
        fun onShowMenu()
        fun onExit()
    }

    private external fun nativeCreate(activity: Activity, appContext: Context, config: OpenXrPlaybackConfig): Long
    private external fun nativeStart(handle: Long): Boolean
    private external fun nativeResume(handle: Long)
    private external fun nativePause(handle: Long)
    private external fun nativeSetVideoSize(handle: Long, width: Int, height: Int)
    private external fun nativeSetUiState(
        handle: Long,
        visible: Boolean,
        playing: Boolean,
        buffering: Boolean,
        positionMs: Long,
        durationMs: Long,
        bufferedPositionMs: Long,
        title: String,
        stereoModeLabel: String,
        audioTrackLabel: String,
        audioTrackLabels: Array<String>,
        selectedAudioTrackIndex: Int
    )
    private external fun nativeSetPlayerUiState(handle: Long, state: OpenXrPlayerUiState)
    private external fun nativeStartFfmpegVideoSource(handle: Long, uri: String, startPositionMs: Long)
    private external fun nativeStopFfmpegVideoSource(handle: Long)
    private external fun nativeSetFfmpegPlaybackState(
        handle: Long,
        playing: Boolean,
        positionMs: Long,
        forceSeek: Boolean
    )
    private external fun nativeGetFfmpegPlaybackState(handle: Long): LongArray
    private external fun nativeGetFfmpegAudioTrackIds(handle: Long): Array<String>
    private external fun nativeGetFfmpegAudioTrackTitles(handle: Long): Array<String>
    private external fun nativeGetFfmpegAudioTrackSubtitles(handle: Long): Array<String>
    private external fun nativeSelectFfmpegAudioTrack(handle: Long, trackId: String): Boolean
    private external fun nativeSetFfmpegMuted(handle: Long, muted: Boolean)
    private external fun nativeDestroy(handle: Long)

    companion object {
        init {
            runCatching { System.loadLibrary("dddvr_openxr") }
                .onFailure { Log.w("DDDVR/OpenXR", "Native OpenXR library missing: ${it.message}") }
        }
    }
}
