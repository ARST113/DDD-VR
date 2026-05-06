package top.rootu.dddvr.vr.activity

import android.content.pm.ActivityInfo
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.KeyEvent
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.ProgressBar
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.media3.common.Player
import top.rootu.dddvr.core.playback.PlaybackSession
import top.rootu.dddvr.model.MediaItem
import top.rootu.dddvr.player.PlayerManager
import top.rootu.dddvr.vr.input.VrInputController
import top.rootu.dddvr.vr.player.VrPlayerController
import top.rootu.dddvr.vr.projection.ProjectionType
import top.rootu.dddvr.vr.renderer.VrGLSurfaceView
import top.rootu.dddvr.vr.renderer.VrSceneRenderer
import top.rootu.dddvr.vr.stereo.StereoInputMode
import top.rootu.dddvr.vr.ui.VrControlsOverlay
import top.rootu.dddvr.vr.ui.VrUiLayer

class VrPlayerActivity : AppCompatActivity() {
    private lateinit var playerManager: PlayerManager
    private lateinit var playbackSession: PlaybackSession
    private lateinit var renderer: VrSceneRenderer
    private lateinit var controller: VrPlayerController
    private lateinit var uiLayer: VrUiLayer
    private lateinit var inputController: VrInputController
    private val handler = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        WindowCompat.setDecorFitsSystemWindows(window, false)

        playerManager = PlayerManager(this, object : Player.Listener {})
        playbackSession = PlaybackSession(playerManager)
        renderer = VrSceneRenderer { surface -> playbackSession.attachSurface(surface) }

        val root = FrameLayout(this)
        val surfaceView = VrGLSurfaceView(this, renderer)
        val controls = VrControlsOverlay(this)
        val loading = ProgressBar(this)
        val errorText = TextView(this)
        root.addView(surfaceView, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        root.addView(controls, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        root.addView(loading)
        root.addView(errorText)
        setContentView(root)

        uiLayer = VrUiLayer(controls, loading, errorText)
        inputController = VrInputController(onShowControls = { uiLayer.show() }, onHideControls = { uiLayer.hide() }, isOverlayVisible = { uiLayer.isVisible() })
        controller = VrPlayerController(this, playbackSession, { renderer.projectionManager }, renderer.stereoUvMapper)

        controls.callbacks = object : VrControlsOverlay.Callbacks {
            override fun onPlayPause() = controller.togglePlay()
            override fun onSeekBy(deltaMs: Long) = controller.seekBy(deltaMs)
            override fun onSeekTo(positionMs: Long) = controller.seekTo(positionMs)
            override fun onPrevious() = controller.previousItem()
            override fun onNext() = controller.nextItem()
            override fun onStereo(mode: String) = controller.setStereoMode(StereoInputMode.valueOf(mode))
            override fun onProjection(type: String) = controller.setProjection(ProjectionType.valueOf(type))
            override fun onToggleSwapEyes() = controller.toggleSwapEyes()
            override fun onRecenter() = controller.recenter()
            override fun onExit() = controller.exitVrMode()
            override fun onUiTick() = uiLayer.update(controller.getState())
            override fun onInteraction() = inputController.notifyInteraction()
        }

        val request = VrIntentParser.parse(intent) ?: run { uiLayer.setBlockingError("Invalid intent"); return }
        controller.setStereoMode(request.stereoInputMode)
        playerManager.loadPlaylist(listOf(MediaItem(uri = request.uri, title = request.title, startPositionMs = request.startPositionMs)),0,request.startPositionMs)
        inputController.enable()
        uiLayer.show()
        startUiLoop()
    }

    private fun startUiLoop() {
        handler.post(object : Runnable {
            override fun run() {
                uiLayer.update(controller.getState())
                if (inputController.shouldAutoHide(android.os.SystemClock.uptimeMillis())) inputController.hideControls()
                handler.postDelayed(this, 500)
            }
        })
    }

    override fun onResume() {
        super.onResume()
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        inputController.notifyInteraction()
        when (keyCode) {
            KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE, KeyEvent.KEYCODE_SPACE, KeyEvent.KEYCODE_BUTTON_A -> { controller.togglePlay(); return true }
            KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_MEDIA_REWIND, KeyEvent.KEYCODE_BUTTON_L1 -> { controller.seekBy(-15_000); return true }
            KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_MEDIA_FAST_FORWARD, KeyEvent.KEYCODE_BUTTON_R1 -> { controller.seekBy(15_000); return true }
            KeyEvent.KEYCODE_DPAD_UP -> { inputController.showControls(); return true }
            KeyEvent.KEYCODE_DPAD_DOWN -> { inputController.hideControls(); return true }
            KeyEvent.KEYCODE_MENU -> { inputController.toggleControls(); return true }
            KeyEvent.KEYCODE_BACK, KeyEvent.KEYCODE_ESCAPE, KeyEvent.KEYCODE_BUTTON_B -> {
                if (uiLayer.isVisible()) inputController.hideControls() else finish()
                return true
            }
        }
        return super.onKeyDown(keyCode, event)
    }

    override fun onDestroy() {
        handler.removeCallbacksAndMessages(null)
        inputController.disable()
        controller.release()
        super.onDestroy()
    }
}
