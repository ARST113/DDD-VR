package top.rootu.dddvr.vr.activity

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import androidx.media3.common.Player
import top.rootu.dddplayer.player.PlayerManager
import top.rootu.dddvr.core.playback.PlaybackSession
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
    }

    override fun onDestroy() {
        controller.release()
        super.onDestroy()
    }
}
