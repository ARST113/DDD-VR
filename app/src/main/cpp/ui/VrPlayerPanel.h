#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "VrPlayerPanelActions.h"

enum class VrPlayerModal {
    None = 0,
    Playlist = 1,
    Settings = 2
};

enum class VrSettingsTab {
    Display = 0,
    Subtitles = 1,
    Audio = 2
};

enum class VrHoverTarget {
    None = 0,

    MainBar,
    PlaylistButton,
    VolumeButton,
    PreviousButton,
    PlayPauseButton,
    NextButton,
    EnvironmentButton,
    ProjectionModeButton,
    MoreButton,
    DragHandle,
    Timeline,

    SettingsTabDisplay,
    SettingsTabSubtitles,
    SettingsTabAudio,

    ModalClose,
    ModalRefresh,
    ModalRow,
    ModalToggle,
    ModalSegmentButton
};

struct VrTrackRow {
    std::string id;
    std::string title;
    std::string subtitle;
    bool selected = false;
    bool enabled = true;
};

struct VrPlaylistRow {
    std::string id;
    std::string title;
    std::string subtitle;
    bool selected = false;
};

struct VrDisplayState {
    std::string aspectRatio = "Оригинал";
    float playbackSpeed = 1.0f;
    bool enhanceVideo = false;
    float brightness = 1.0f;
};

struct VrSubtitleState {
    bool enabled = false;
    int delayMs = 0;
    std::string sizeLabel = "Средний";
    std::string positionLabel = "Ниже";
};

struct VrAudioState {
    int delayMs = 0;
    bool spatialAudio = false;
};

struct VrPlayerUiState {
    bool visible = true;
    bool pinned = false;
    bool playing = false;
    bool buffering = false;
    bool muted = false;
    int64_t positionMs = 0;
    int64_t durationMs = 0;
    int64_t bufferedPositionMs = 0;
    std::string title;
    std::string projectionModeLabel = "2D";
    std::string audioTrackLabel;
    std::string subtitleTrackLabel;

    VrPlayerModal activeModal = VrPlayerModal::None;
    VrSettingsTab activeSettingsTab = VrSettingsTab::Display;
    VrHoverTarget hoverTarget = VrHoverTarget::None;

    VrDisplayState display;
    VrSubtitleState subtitles;
    VrAudioState audio;

    std::vector<VrPlaylistRow> playlistRows;
    std::vector<VrTrackRow> audioTracks;
    std::vector<VrTrackRow> subtitleTracks;

    // Legacy fields kept until callers fully migrate to audioTracks.
    std::vector<std::string> audioTrackLabels;
    int selectedAudioTrackIndex = 0;
};

enum class VrPlayerPanelMode {
    Normal,
    FullPanelScrubPreview,
    FullPanelScrubDragging
};

class VrPlayerPanel {
public:
    void setState(const VrPlayerUiState& state);
    void draw();

    bool consumePlayPauseRequested();
    bool consumeSeekBackRequested();
    bool consumeSeekForwardRequested();
    bool consumeExitRequested();
    bool consumeRecenterRequested();
    bool consumeTimelineSeekRequested(int64_t* outPositionMs);
    bool consumeAudioTrackSelected(int* outTrackIndex);
    bool consumeAction(VrPlayerPanelAction* outAction);

private:
    static const char* formatTime(int64_t timeMs, char* buffer, int bufferSize);
    VrPlayerUiState state_;
    std::vector<VrPlayerPanelAction> pendingActions_;
    bool playPauseRequested_ = false;
    bool seekBackRequested_ = false;
    bool seekForwardRequested_ = false;
    bool exitRequested_ = false;
    bool recenterRequested_ = false;
    bool timelineSeekRequested_ = false;
    bool audioTrackSelected_ = false;
    bool timelineDragging_ = false;
    bool audioPopupOpen_ = false;
    VrPlayerPanelMode panelMode_ = VrPlayerPanelMode::Normal;
    float timelinePreviewProgress_ = 0.f;
    float scrubPreviewProgress_ = 0.f;
    float tooltipX_ = -1.f;
    double lastScrubActivitySeconds_ = 0.0;
    int64_t requestedTimelinePositionMs_ = 0;
    int requestedAudioTrackIndex_ = -1;
};
