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
    private external fun nativeDestroy(handle: Long)

    companion object {
        init {
            runCatching { System.loadLibrary("dddvr_openxr") }
                .onFailure { Log.w("DDDVR/OpenXR", "Native OpenXR library missing: ${it.message}") }
        }
    }
}
