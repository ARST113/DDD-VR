package top.rootu.dddvr.vr.activity

import android.content.pm.ActivityInfo
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
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
import top.rootu.dddvr.vr.input.VrControllerInputMapper
import top.rootu.dddvr.vr.input.VrInputController
import top.rootu.dddvr.vr.input.VrKeyAction
import top.rootu.dddvr.vr.model.ProjectionMode
import top.rootu.dddvr.vr.model.StereoLayout
import top.rootu.dddvr.vr.pose.AndroidRotationVectorPoseProvider
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
    private lateinit var poseProvider: AndroidRotationVectorPoseProvider
    private val handler = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        applyImmersiveMode()
        Log.i("DDDVR/Activity", "fullscreen setup done")

        playerManager = PlayerManager(this, object : Player.Listener {})
        playbackSession = PlaybackSession(playerManager)
        poseProvider = AndroidRotationVectorPoseProvider(this)
        renderer = VrSceneRenderer(onSurfaceReady = { surface -> playbackSession.attachSurface(surface) }, poseProvider = poseProvider)

        val root = FrameLayout(this)
        val surfaceView = VrGLSurfaceView(this, renderer)
        val controls = VrControlsOverlay(this)
        val loading = ProgressBar(this)
        val errorText = TextView(this)

        root.addView(surfaceView, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        root.addView(controls, FrameLayout.LayoutParams((resources.displayMetrics.widthPixels * 0.86f).toInt(), ViewGroup.LayoutParams.WRAP_CONTENT).apply { gravity = android.view.Gravity.BOTTOM or android.view.Gravity.CENTER_HORIZONTAL })
        root.addView(loading)
        root.addView(errorText)
        setContentView(root)

        uiLayer = VrUiLayer(controls, loading, errorText)
        inputController = VrInputController(autoHideDelayMs = 8_000L, onShowControls = { uiLayer.show() }, onHideControls = { uiLayer.hide() }, isOverlayVisible = { uiLayer.isVisible() })
        controller = VrPlayerController(this, playbackSession, { renderer.projectionManager }, renderer.stereoUvMapper, recenterHeadPose = { poseProvider.recenter() })

        controls.callbacks = object : VrControlsOverlay.Callbacks {
            override fun onPlayPause() = controller.togglePlay()
            override fun onSeekBy(deltaMs: Long) = controller.seekBy(deltaMs)
            override fun onSeekTo(positionMs: Long) = controller.seekTo(positionMs)
            override fun onStereo(mode: String) = controller.setStereoMode(StereoInputMode.valueOf(mode))
            override fun onProjection(type: String) = controller.setProjection(ProjectionType.valueOf(type))
            override fun onToggleSwapEyes() = controller.toggleSwapEyes()
            override fun onRecenter() = Unit
            override fun onExit() = controller.exitVrMode()
            override fun onInteraction() = inputController.notifyInteraction()
            override fun onControlsInteractionStart() = inputController.notifyControlsInteractionStart()
            override fun onControlsInteractionEnd() = inputController.notifyControlsInteractionEnd()
        }

        val request = VrIntentParser.parse(intent) ?: run {
            uiLayer.setBlockingError("Invalid intent")
            return
        }
        val config = request.vrConfig
        val stereoMode = when (config.stereoLayout) {
            StereoLayout.SBS -> StereoInputMode.SBS
            StereoLayout.OU -> StereoInputMode.OU
            StereoLayout.MONO -> request.stereoInputMode
        }
        val projection = mapProjectionMode(config.projectionMode, request.projectionType)

        controller.setStereoMode(stereoMode)
        controller.setProjection(projection)
        controller.swapEyes(config.swapEyes)
        playerManager.loadPlaylist(listOf(MediaItem(uri = request.uri, title = request.title, startPositionMs = request.startPositionMs)), 0, request.startPositionMs)
        inputController.enable()
        uiLayer.show()
        startUiLoop()
    }

    private fun startUiLoop() {
        handler.post(object : Runnable {
            override fun run() {
                val curvedAvailable = runCatching { renderer.projectionManager.hasProjection(ProjectionType.CURVED) }.getOrDefault(false)
                uiLayer.update(controller.getState(), curvedAvailable)
                if (inputController.shouldAutoHide(android.os.SystemClock.uptimeMillis())) inputController.hideControls()
                handler.postDelayed(this, 500)
            }
        })
    }

    override fun onResume() {
        super.onResume()
        poseProvider.start()
        applyImmersiveMode()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) applyImmersiveMode()
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.action != KeyEvent.ACTION_DOWN) return super.dispatchKeyEvent(event)

        val action = VrControllerInputMapper.map(event.keyCode)
        if (action == VrKeyAction.NONE) return super.dispatchKeyEvent(event)

        inputController.notifyInteraction()
        Log.d("DDDVR/Input", "key=${event.keyCode} action=$action overlay=${uiLayer.isVisible()}")

        return when (action) {
            VrKeyAction.PLAY_PAUSE -> { controller.togglePlay(); true }
            VrKeyAction.PLAY -> { controller.play(); true }
            VrKeyAction.PAUSE -> { controller.pause(); true }
            VrKeyAction.SEEK_BACK -> { controller.seekBy(-15_000); true }
            VrKeyAction.SEEK_FORWARD -> { controller.seekBy(15_000); true }
            VrKeyAction.TOGGLE_OVERLAY -> { inputController.toggleControls(); true }
            VrKeyAction.HIDE_OR_EXIT -> {
                if (uiLayer.isVisible()) inputController.hideControls() else finish()
                true
            }
            VrKeyAction.RECENTER -> { controller.recenter(); true }
            VrKeyAction.TOGGLE_STEREO -> {
                controller.toggleStereoMode()
                inputController.showControls()
                true
            }
            VrKeyAction.NONE -> false
        }
    }

    override fun onPause() {
        poseProvider.stop()
        super.onPause()
    }

    override fun onDestroy() {
        handler.removeCallbacksAndMessages(null)
        inputController.disable()
        controller.release()
        super.onDestroy()
    }

    internal fun mapProjectionMode(mode: ProjectionMode, fallback: ProjectionType): ProjectionType = when (mode) {
        ProjectionMode.VR180 -> ProjectionType.EQUIRECT_180
        ProjectionMode.VR360 -> ProjectionType.EQUIRECT_360
        ProjectionMode.VR_CURVED_SCREEN -> ProjectionType.CURVED
        ProjectionMode.VR_FLAT_SCREEN -> fallback
    }

    private fun applyImmersiveMode() {
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.statusBars() or WindowInsetsCompat.Type.navigationBars() or WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }
}
