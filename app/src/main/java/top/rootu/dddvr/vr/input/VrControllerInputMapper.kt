package top.rootu.dddvr.vr.input

import android.view.KeyEvent

object VrControllerInputMapper {
    fun map(keyCode: Int): VrKeyAction = when (keyCode) {
        KeyEvent.KEYCODE_DPAD_CENTER,
        KeyEvent.KEYCODE_ENTER,
        KeyEvent.KEYCODE_BUTTON_A,
        KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE,
        KeyEvent.KEYCODE_SPACE -> VrKeyAction.PLAY_PAUSE

        KeyEvent.KEYCODE_MEDIA_PLAY -> VrKeyAction.PLAY
        KeyEvent.KEYCODE_MEDIA_PAUSE -> VrKeyAction.PAUSE

        KeyEvent.KEYCODE_DPAD_LEFT,
        KeyEvent.KEYCODE_BUTTON_L1,
        KeyEvent.KEYCODE_MEDIA_REWIND -> VrKeyAction.SEEK_BACK

        KeyEvent.KEYCODE_DPAD_RIGHT,
        KeyEvent.KEYCODE_BUTTON_R1,
        KeyEvent.KEYCODE_MEDIA_FAST_FORWARD -> VrKeyAction.SEEK_FORWARD

        KeyEvent.KEYCODE_MENU,
        KeyEvent.KEYCODE_BUTTON_START -> VrKeyAction.TOGGLE_OVERLAY

        KeyEvent.KEYCODE_BACK,
        KeyEvent.KEYCODE_ESCAPE,
        KeyEvent.KEYCODE_BUTTON_B -> VrKeyAction.HIDE_OR_EXIT

        KeyEvent.KEYCODE_BUTTON_Y -> VrKeyAction.RECENTER
        KeyEvent.KEYCODE_BUTTON_X -> VrKeyAction.TOGGLE_STEREO
        else -> VrKeyAction.NONE
    }
}
