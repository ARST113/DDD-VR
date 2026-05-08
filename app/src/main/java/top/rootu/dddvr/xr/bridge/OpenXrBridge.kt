package top.rootu.dddvr.xr.bridge

import android.content.Context
import android.util.Log
import android.view.Surface
import top.rootu.dddvr.xr.input.OpenXrInputMapper
import top.rootu.dddvr.xr.model.OpenXrPlaybackConfig

class OpenXrBridge(
    context: Context,
    private val callbacks: Callbacks,
    private val config: OpenXrPlaybackConfig
) {
    private val appContext = context.applicationContext
    private var nativeHandle: Long = 0L

    fun start(): Boolean {
        nativeHandle = nativeCreate(appContext, config)
        if (nativeHandle == 0L) {
            Log.e("DDDVR/OpenXR", "nativeCreate returned null handle")
            return false
        }
        nativeStart(nativeHandle)
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

    fun destroy() {
        if (nativeHandle == 0L) return
        nativeDestroy(nativeHandle)
        nativeHandle = 0L
    }

    @Suppress("unused")
    private fun onVideoSurfaceReadyFromNative(surface: Surface) = callbacks.onVideoSurfaceReady(surface)

    @Suppress("unused")
    private fun onInputActionFromNative(actionCode: Int) {
        when (OpenXrInputMapper.fromNativeCode(actionCode)) {
            top.rootu.dddvr.xr.input.OpenXrInputAction.PLAY_PAUSE -> callbacks.onPlayPause()
            top.rootu.dddvr.xr.input.OpenXrInputAction.SEEK_BACK -> callbacks.onSeekBy(-15_000)
            top.rootu.dddvr.xr.input.OpenXrInputAction.SEEK_FORWARD -> callbacks.onSeekBy(15_000)
            top.rootu.dddvr.xr.input.OpenXrInputAction.RECENTER -> callbacks.onRecenter()
            top.rootu.dddvr.xr.input.OpenXrInputAction.EXIT -> callbacks.onExit()
            else -> Unit
        }
    }

    @Suppress("unused")
    private fun onNativeLog(message: String) {
        Log.i("DDDVR/OpenXR", message)
    }

    interface Callbacks {
        fun onVideoSurfaceReady(surface: Surface)
        fun onPlayPause()
        fun onSeekBy(deltaMs: Long)
        fun onRecenter()
        fun onExit()
    }

    private external fun nativeCreate(context: Context, config: OpenXrPlaybackConfig): Long
    private external fun nativeStart(handle: Long)
    private external fun nativeResume(handle: Long)
    private external fun nativePause(handle: Long)
    private external fun nativeDestroy(handle: Long)

    companion object {
        init {
            runCatching { System.loadLibrary("dddvr_openxr") }
                .onFailure { Log.w("DDDVR/OpenXR", "Native OpenXR library missing: ${it.message}") }
        }
    }
}
