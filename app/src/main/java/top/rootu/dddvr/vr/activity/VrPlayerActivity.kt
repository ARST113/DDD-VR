package top.rootu.dddvr.vr.activity

import android.os.Bundle
import android.view.KeyEvent
import androidx.appcompat.app.AppCompatActivity
import androidx.media3.common.Player
import top.rootu.dddvr.core.playback.PlaybackSession
import top.rootu.dddvr.model.MediaItem
import top.rootu.dddvr.player.PlayerManager
import top.rootu.dddvr.vr.player.VrPlayerController
import top.rootu.dddvr.vr.renderer.VrGLSurfaceView
import top.rootu.dddvr.vr.renderer.VrSceneRenderer

class VrPlayerActivity : AppCompatActivity() {
    private lateinit var playerManager: PlayerManager
    private lateinit var playbackSession: PlaybackSession
    private lateinit var renderer: VrSceneRenderer
    private lateinit var controller: VrPlayerController
    private lateinit var surfaceView: VrGLSurfaceView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        playerManager = PlayerManager(this, object : Player.Listener {})
        playbackSession = PlaybackSession(playerManager)
        renderer = VrSceneRenderer { surface ->
            playbackSession.attachSurface(surface)
        }
        surfaceView = VrGLSurfaceView(this, renderer)
        setContentView(surfaceView)

        controller = VrPlayerController(
            activity = this,
            playbackSession = playbackSession,
            projectionManagerProvider = { renderer.projectionManager },
            uiLayer = renderer.uiLayer,
            inputController = renderer.inputController
        )
        controller.enterVrMode()

        val request = VrIntentParser.parse(intent)
        if (request == null) {
            finish()
            return
        }
        renderer.stereoUvMapper.stereoInputMode = request.stereoInputMode
        playerManager.loadPlaylist(
            items = listOf(MediaItem(uri = request.uri, title = request.title, startPositionMs = request.startPositionMs)),
            startIndex = 0,
            startPosMs = request.startPositionMs
        )
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        when (keyCode) {
            KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE, KeyEvent.KEYCODE_SPACE -> {
                controller.togglePlay(); return true
            }
            KeyEvent.KEYCODE_MEDIA_PLAY -> { controller.play(); return true }
            KeyEvent.KEYCODE_MEDIA_PAUSE -> { controller.pause(); return true }
            KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_MEDIA_REWIND -> { controller.seekBy(-10_000); return true }
            KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_MEDIA_FAST_FORWARD -> { controller.seekBy(10_000); return true }
            KeyEvent.KEYCODE_BACK, KeyEvent.KEYCODE_ESCAPE -> { finish(); return true }
        }
        return super.onKeyDown(keyCode, event)
    }

    override fun onDestroy() {
        controller.release()
        super.onDestroy()
    }
}
