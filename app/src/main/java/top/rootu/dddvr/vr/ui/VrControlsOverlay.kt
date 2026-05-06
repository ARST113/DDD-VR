package top.rootu.dddvr.vr.ui

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.AttributeSet
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
    private val handler = Handler(Looper.getMainLooper())
    private var interactingWithSeek = false

    private val playPause = button("Play/Pause") { callbacks?.onPlayPause() }
    private val seekBack = button("-15s") { callbacks?.onSeekBy(-15_000) }
    private val seekFwd = button("+15s") { callbacks?.onSeekBy(15_000) }
    private val prev = button("Prev") { callbacks?.onPrevious() }
    private val next = button("Next") { callbacks?.onNext() }
    private val mono = button("MONO") { callbacks?.onStereo("MONO") }
    private val sbs = button("SBS") { callbacks?.onStereo("SBS") }
    private val sbsRev = button("SBS_R") { callbacks?.onStereo("SBS_REVERSED") }
    private val ou = button("OU") { callbacks?.onStereo("OU") }
    private val ouRev = button("OU_R") { callbacks?.onStereo("OU_REVERSED") }
    private val swap = button("Swap Eyes") { callbacks?.onToggleSwapEyes() }
    private val flat = button("Flat") { callbacks?.onProjection("FLAT") }
    private val curved = button("Curved") { callbacks?.onProjection("CURVED") }
    private val recenter = button("Recenter") { callbacks?.onRecenter() }
    private val exit = button("Exit") { callbacks?.onExit() }
    private val position = TextView(context)
    private val duration = TextView(context)
    private val seek = SeekBar(context)

    private val tick = object : Runnable {
        override fun run() {
            if (isVisible) callbacks?.onUiTick()
            if (isVisible && !interactingWithSeek) handler.postDelayed(this, 1000)
        }
    }

    init {
        orientation = VERTICAL
        gravity = Gravity.BOTTOM
        setBackgroundColor(0xAA000000.toInt())
        isFocusable = true
        isFocusableInTouchMode = true
        val row1 = row(prev, seekBack, playPause, seekFwd, next)
        val row2 = row(mono, sbs, sbsRev, ou, ouRev, swap)
        val row3 = row(flat, curved, recenter, exit)
        val timeRow = LinearLayout(context).apply { addView(position); addView(duration) }
        addView(row1); addView(timeRow); addView(seek); addView(row2); addView(row3)
        seek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar?, progress: Int, fromUser: Boolean) {
                if (fromUser) callbacks?.onSeekTo(progress.toLong())
            }
            override fun onStartTrackingTouch(sb: SeekBar?) { interactingWithSeek = true }
            override fun onStopTrackingTouch(sb: SeekBar?) { interactingWithSeek = false; callbacks?.onInteraction() }
        })
    }

    fun update(state: VrPlaybackState) {
        playPause.text = if (state.isPlaying) "Pause" else "Play"
        position.text = formatMs(state.positionMs)
        duration.text = " / ${formatMs(state.durationMs)}"
        seek.max = (state.durationMs.coerceAtLeast(1L)).toInt()
        if (!interactingWithSeek) seek.progress = state.positionMs.coerceIn(0L, state.durationMs).toInt()
    }

    override fun setVisibility(visibility: Int) {
        super.setVisibility(visibility)
        handler.removeCallbacks(tick)
        if (visibility == View.VISIBLE) handler.post(tick)
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        callbacks?.onInteraction()
        return super.dispatchKeyEvent(event)
    }

    private fun row(vararg views: View) = LinearLayout(context).apply {
        orientation = HORIZONTAL
        views.forEach { addView(it) }
    }

    private fun button(label: String, click: () -> Unit) = Button(context).apply {
        text = label
        minHeight = 120
        minWidth = 120
        isFocusable = true
        setOnClickListener { callbacks?.onInteraction(); click() }
    }

    private fun formatMs(ms: Long): String {
        val total = (ms / 1000).coerceAtLeast(0)
        val m = total / 60
        val s = total % 60
        return "%02d:%02d".format(m, s)
    }

    interface Callbacks {
        fun onPlayPause(); fun onSeekBy(deltaMs: Long); fun onSeekTo(positionMs: Long)
        fun onPrevious(); fun onNext(); fun onStereo(mode: String); fun onProjection(type: String)
        fun onToggleSwapEyes(); fun onRecenter(); fun onExit(); fun onUiTick(); fun onInteraction()
    }
}
