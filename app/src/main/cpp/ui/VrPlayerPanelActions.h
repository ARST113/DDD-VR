#pragma once

#include <string>

enum class VrPlayerPanelActionType {
    None = 0,

    TogglePlaylist = 100,
    ToggleSettings = 101,
    ToggleVolume = 102,
    ToggleProjectionMenu = 103,
    ToggleEnvironment = 104,
    CloseModal = 105,

    SetSettingsTab = 110,

    SelectAudioTrack = 120,
    SelectSubtitleTrack = 121,
    SelectPlaylistItem = 122,

    SetAspectRatio = 130,
    SetPlaybackSpeed = 131,

    ToggleEnhanceVideo = 140,
    ToggleSpatialAudio = 141,
    ToggleSubtitles = 142
};

struct VrPlayerPanelAction {
    VrPlayerPanelActionType type = VrPlayerPanelActionType::None;
    int intValue = 0;
    float floatValue = 0.f;
    std::string stringValue;
};
