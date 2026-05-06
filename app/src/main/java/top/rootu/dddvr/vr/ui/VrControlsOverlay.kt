package top.rootu.dddvr.vr.ui

import android.content.Context
import android.util.AttributeSet
import android.util.Log
import android.util.TypedValue
import android.view.Gravity
import android.view.KeyEvent
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import androidx.core.view.isVisible
import top.rootu.dddvr.vr.player.VrPlaybackState

class VrControlsOverlay @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : LinearLayout(context, attrs) {
    var callbacks: Callbacks? = null
    private var interactingWithSeek = false

    private val playPause = button("Play") { action("play_pause") { it.onPlayPause() } }
    private val seekBack = button("-15s") { action("seek_-15") { it.onSeekBy(-15_000) } }
    private val seekFwd = button("+15s") { action("seek_+15") { it.onSeekBy(15_000) } }
    private val mono = button("MONO") { action("stereo_mono") { it.onStereo("MONO") } }
    private val sbs = button("SBS") { action("stereo_sbs") { it.onStereo("SBS") } }
    private val sbsRev = button("SBS_R") { action("stereo_sbs_r") { it.onStereo("SBS_REVERSED") } }
    private val ou = button("OU") { action("stereo_ou") { it.onStereo("OU") } }
    private val ouRev = button("OU_R") { action("stereo_ou_r") { it.onStereo("OU_REVERSED") } }
    private val swap = button("Swap Eyes") { action("swap_eyes") { it.onToggleSwapEyes() } }
    private val flat = button("Flat") { action("projection_flat") { it.onProjection("FLAT") } }
    private val curved = button("Curved") { action("projection_curved") { it.onProjection("CURVED") } }
    private val recenter = button("Recenter") { action("recenter") { it.onRecenter() } }
    private val exit = button("Exit") { action("exit") { it.onExit() } }
    private val position = TextView(context)
    private val duration = TextView(context)
    private val seek = SeekBar(context)

    init {
        orientation = VERTICAL
        gravity = Gravity.CENTER_HORIZONTAL or Gravity.BOTTOM
        setBackgroundColor(0xB0000000.toInt())
        isClickable = true
        isFocusable = true
        isFocusableInTouchMode = true
        setPadding(dp(12), dp(12), dp(12), dp(12))

        val lp = LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT)
        val row1 = row(seekBack, playPause, seekFwd, exit)
        val row2 = row(mono, sbs, sbsRev, ou, ouRev, swap)
        val row3 = row(flat, curved, recenter)
        val timeRow = LinearLayout(context).apply {
            gravity = Gravity.CENTER_HORIZONTAL
            addView(position)
            addView(duration)
        }

        addView(row1, lp)
        addView(timeRow, lp)
        addView(seek, lp)
        addView(row2, lp)
        addView(row3, lp)

        curved.isEnabled = false
        curved.alpha = 0.5f
        recenter.isEnabled = false
        recenter.alpha = 0.5f

        seek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar?, progress: Int, fromUser: Boolean) {
                if (fromUser) callbacks?.onSeekTo(progress.toLong())
            }

            override fun onStartTrackingTouch(sb: SeekBar?) {
                interactingWithSeek = true
                callbacks?.onControlsInteractionStart()
            }

            override fun onStopTrackingTouch(sb: SeekBar?) {
                interactingWithSeek = false
                callbacks?.onControlsInteractionEnd()
            }
        })
    }

    fun updateState(state: VrPlaybackState, curvedAvailable: Boolean) {
        playPause.text = if (state.isPlaying) "Pause" else "Play"
        position.text = formatMs(state.positionMs)
        duration.text = " / ${formatMs(state.durationMs)}"
        seek.max = state.durationMs.coerceAtLeast(1L).toInt()
        if (!interactingWithSeek) seek.progress = state.positionMs.coerceIn(0L, state.durationMs).toInt()

        curved.isEnabled = curvedAvailable
        curved.alpha = if (curvedAvailable) 1f else 0.5f

        selectStereo(state)
        flat.isSelected = state.projectionType.name == "FLAT"
        curved.isSelected = state.projectionType.name == "CURVED"
        swap.isSelected = state.swapEyes
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        callbacks?.onInteraction()
        return super.dispatchKeyEvent(event)
    }

    private fun selectStereo(state: VrPlaybackState) {
        val mode = state.stereoMode.name
        mono.isSelected = mode == "MONO"
        sbs.isSelected = mode == "SBS"
        sbsRev.isSelected = mode == "SBS_REVERSED"
        ou.isSelected = mode == "OU"
        ouRev.isSelected = mode == "OU_REVERSED"
    }

    private fun action(name: String, block: (Callbacks) -> Unit) {
        callbacks?.let {
            it.onInteraction()
            Log.d("DDDVR/UI", "action=$name")
            block(it)
        }
    }

    private fun row(vararg views: View): LinearLayout = LinearLayout(context).apply {
        orientation = HORIZONTAL
        gravity = Gravity.CENTER_HORIZONTAL
        views.forEach { addView(it) }
    }

    private fun button(label: String, click: () -> Unit): Button = Button(context).apply {
        text = label
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f)
        minHeight = dp(56)
        minWidth = dp(88)
        isFocusable = true
        isClickable = true
        setOnClickListener { click() }
    }

    private fun formatMs(ms: Long): String {
        val total = (ms / 1000).coerceAtLeast(0)
        return "%02d:%02d".format(total / 60, total % 60)
    }

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

    interface Callbacks {
        fun onPlayPause()
        fun onSeekBy(deltaMs: Long)
        fun onSeekTo(positionMs: Long)
        fun onStereo(mode: String)
        fun onProjection(type: String)
        fun onToggleSwapEyes()
        fun onRecenter()
        fun onExit()
        fun onInteraction()
        fun onControlsInteractionStart()
        fun onControlsInteractionEnd()
    }
}
