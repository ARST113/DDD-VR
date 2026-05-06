package top.rootu.dddvr.xr.input

object OpenXrInputMapper {
    fun fromNativeCode(code: Int): OpenXrInputAction = when (code) {
        1 -> OpenXrInputAction.PLAY_PAUSE
        2 -> OpenXrInputAction.SEEK_BACK
        3 -> OpenXrInputAction.SEEK_FORWARD
        4 -> OpenXrInputAction.RECENTER
        5 -> OpenXrInputAction.SHOW_MENU
        6 -> OpenXrInputAction.EXIT
        else -> OpenXrInputAction.NONE
    }
}
