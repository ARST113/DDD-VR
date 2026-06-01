#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct VrPlayerUiState {
    bool visible = true;
    bool pinned = false;
    bool playing = false;
    bool buffering = false;
    int64_t positionMs = 0;
    int64_t durationMs = 0;
    int64_t bufferedPositionMs = 0;
    std::string title;
    std::string stereoModeLabel;
    std::string audioTrackLabel;
    std::string subtitleTrackLabel;
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

private:
    static const char* formatTime(int64_t timeMs, char* buffer, int bufferSize);
    VrPlayerUiState state_;
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
