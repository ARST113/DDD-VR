package top.rootu.dddvr.xr.ui

sealed class OpenXrPlayerUiAction {
    object TogglePlaylist : OpenXrPlayerUiAction()
    object ToggleSettings : OpenXrPlayerUiAction()
    object ToggleVolume : OpenXrPlayerUiAction()
    object ToggleProjectionMenu : OpenXrPlayerUiAction()
    object ToggleEnvironment : OpenXrPlayerUiAction()
    object CloseModal : OpenXrPlayerUiAction()

    data class SetSettingsTab(val tab: Int) : OpenXrPlayerUiAction()
    data class SelectAudioTrack(val id: String) : OpenXrPlayerUiAction()
    data class SelectSubtitleTrack(val id: String) : OpenXrPlayerUiAction()
    data class SelectPlaylistItem(val id: String) : OpenXrPlayerUiAction()

    data class SetAspectRatio(val value: String) : OpenXrPlayerUiAction()
    data class SetPlaybackSpeed(val value: Float) : OpenXrPlayerUiAction()

    object ToggleEnhanceVideo : OpenXrPlayerUiAction()
    object ToggleSpatialAudio : OpenXrPlayerUiAction()
    object ToggleSubtitles : OpenXrPlayerUiAction()

    data class Unknown(val actionType: Int) : OpenXrPlayerUiAction()

    companion object {
        fun fromNative(
            actionType: Int,
            intValue: Int,
            floatValue: Float,
            stringValue: String?
        ): OpenXrPlayerUiAction {
            return when (actionType) {
                100 -> TogglePlaylist
                101 -> ToggleSettings
                102 -> ToggleVolume
                103 -> ToggleProjectionMenu
                104 -> ToggleEnvironment
                105 -> CloseModal
                110 -> SetSettingsTab(intValue)
                120 -> SelectAudioTrack(stringValue.orEmpty())
                121 -> SelectSubtitleTrack(stringValue.orEmpty())
                122 -> SelectPlaylistItem(stringValue.orEmpty())
                130 -> SetAspectRatio(stringValue.orEmpty())
                131 -> SetPlaybackSpeed(floatValue)
                140 -> ToggleEnhanceVideo
                141 -> ToggleSpatialAudio
                142 -> ToggleSubtitles
                else -> Unknown(actionType)
            }
        }
    }
}
