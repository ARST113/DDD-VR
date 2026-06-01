#include "VrPlayerPanel.h"

#include "../third/imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
bool pointInRect(const ImVec2& point, const ImVec2& min, const ImVec2& max) {
    return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
}
}

void VrPlayerPanel::setState(const VrPlayerUiState& state) {
    state_ = state;
}

void VrPlayerPanel::draw() {
    if (!state_.visible) return;

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 canvas(io.DisplaySize.x, io.DisplaySize.y);
    ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
    ImGui::SetNextWindowSize(canvas);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("DDDVR_VR_PLAYER_PANEL", nullptr, flags);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    char position[32]{};
    char durationText[32]{};
    formatTime(state_.positionMs, position, sizeof(position));
    formatTime(state_.durationMs, durationText, sizeof(durationText));
    const int64_t durationMs = std::max<int64_t>(state_.durationMs, 1);
    const float duration = static_cast<float>(durationMs);
    const float progress = std::clamp(static_cast<float>(state_.positionMs) / duration, 0.f, 1.f);
    const float buffered = std::clamp(static_cast<float>(state_.bufferedPositionMs) / duration, 0.f, 1.f);
    const ImVec2 mouse = io.MousePos;

    const ImVec2 panelMin(100.f, 132.f);
    const ImVec2 panelMax(1500.f, 470.f);
    const float panelWidth = panelMax.x - panelMin.x;
    const float centerX = (panelMin.x + panelMax.x) * 0.5f;
    const float panelRounding = 28.f;
    const ImVec2 timelineRowMin(panelMin.x + 34.f, panelMin.y + 18.f);
    const ImVec2 timelineRowMax(panelMax.x - 34.f, panelMin.y + 140.f);
    const ImVec2 controlsRowMin(panelMin.x + 34.f, panelMin.y + 158.f);
    const ImVec2 controlsRowMax(panelMax.x - 34.f, panelMax.y - 28.f);
    const float timelineTrackLeft = timelineRowMin.x + 190.f;
    const float timelineTrackRight = timelineRowMax.x - 190.f;
    const float timelineTrackWidth = timelineTrackRight - timelineTrackLeft;
    const float timelineY = timelineRowMin.y + 62.f;
    const bool pointerInsidePanel = pointInRect(mouse, panelMin, panelMax);
    const bool scrubModeActive = panelMode_ != VrPlayerPanelMode::Normal;
    const double nowSeconds = ImGui::GetTime();

    const ImVec2 currentTimeHitMin(timelineRowMin.x, timelineRowMin.y + 18.f);
    const ImVec2 currentTimeHitMax(timelineRowMin.x + 156.f, timelineRowMax.y - 18.f);
    const ImVec2 durationHitMin(timelineRowMax.x - 156.f, timelineRowMin.y + 18.f);
    const ImVec2 durationHitMax(timelineRowMax.x, timelineRowMax.y - 18.f);
    const ImVec2 timelineHitMin(timelineTrackLeft - 42.f, timelineRowMin.y);
    const ImVec2 timelineHitMax(timelineTrackRight + 42.f, timelineRowMax.y);

    auto progressFromPanelPointer = [&]() {
        return std::clamp((mouse.x - panelMin.x) / panelWidth, 0.f, 1.f);
    };
    auto progressFromTimelinePointer = [&]() {
        return std::clamp((mouse.x - timelineTrackLeft) / timelineTrackWidth, 0.f, 1.f);
    };
    auto enterScrubPreview = [&]() {
        panelMode_ = VrPlayerPanelMode::FullPanelScrubPreview;
        scrubPreviewProgress_ = pointerInsidePanel ? progressFromPanelPointer() : progress;
        lastScrubActivitySeconds_ = nowSeconds;
    };

    bool activatedScrubThisFrame = false;
    if (!scrubModeActive) {
        ImGui::SetCursorScreenPos(currentTimeHitMin);
        ImGui::InvisibleButton("DDDVR_TIME_CURRENT", ImVec2(currentTimeHitMax.x - currentTimeHitMin.x, currentTimeHitMax.y - currentTimeHitMin.y));
        if (ImGui::IsItemClicked(0)) {
            enterScrubPreview();
            activatedScrubThisFrame = true;
        }
        ImGui::SetCursorScreenPos(durationHitMin);
        ImGui::InvisibleButton("DDDVR_TIME_DURATION", ImVec2(durationHitMax.x - durationHitMin.x, durationHitMax.y - durationHitMin.y));
        if (ImGui::IsItemClicked(0)) {
            enterScrubPreview();
            activatedScrubThisFrame = true;
        }
    }

    const bool timelineHovered = !scrubModeActive && pointInRect(mouse, timelineHitMin, timelineHitMax);
    if (timelineHovered && ImGui::IsMouseClicked(0)) {
        timelineDragging_ = true;
        timelinePreviewProgress_ = progressFromTimelinePointer();
    }
    if (timelineDragging_ && ImGui::IsMouseDown(0)) {
        timelinePreviewProgress_ = progressFromTimelinePointer();
    }
    const bool timelineActive = !scrubModeActive && timelineDragging_;
    const float timelinePreviewProgress = (timelineHovered || timelineActive)
        ? (timelineActive ? timelinePreviewProgress_ : progressFromTimelinePointer())
        : progress;
    if (timelineDragging_ && ImGui::IsMouseReleased(0)) {
        timelinePreviewProgress_ = progressFromTimelinePointer();
        timelineSeekRequested_ = true;
        requestedTimelinePositionMs_ = static_cast<int64_t>(1000.f * timelinePreviewProgress_);
        timelineDragging_ = false;
    }

    if (panelMode_ != VrPlayerPanelMode::Normal) {
        if (pointerInsidePanel) {
            scrubPreviewProgress_ = progressFromPanelPointer();
            lastScrubActivitySeconds_ = nowSeconds;
        }
        if (panelMode_ == VrPlayerPanelMode::FullPanelScrubPreview) {
            if (!activatedScrubThisFrame && ImGui::IsMouseClicked(0)) {
                if (pointerInsidePanel) {
                    panelMode_ = VrPlayerPanelMode::FullPanelScrubDragging;
                    scrubPreviewProgress_ = progressFromPanelPointer();
                    lastScrubActivitySeconds_ = nowSeconds;
                } else {
                    panelMode_ = VrPlayerPanelMode::Normal;
                }
            }
        } else if (panelMode_ == VrPlayerPanelMode::FullPanelScrubDragging) {
            if (ImGui::IsMouseDown(0)) {
                scrubPreviewProgress_ = progressFromPanelPointer();
                lastScrubActivitySeconds_ = nowSeconds;
            }
            if (ImGui::IsMouseReleased(0)) {
                timelineSeekRequested_ = true;
                requestedTimelinePositionMs_ = static_cast<int64_t>(1000.f * scrubPreviewProgress_);
                panelMode_ = VrPlayerPanelMode::Normal;
            }
        }
        if (panelMode_ != VrPlayerPanelMode::Normal && nowSeconds - lastScrubActivitySeconds_ > 5.0) {
            panelMode_ = VrPlayerPanelMode::Normal;
        }
    }

    draw->AddRectFilled(panelMin, panelMax, IM_COL32(10, 12, 18, 236), panelRounding);
    draw->AddRectFilled(panelMin, ImVec2(panelMax.x, panelMin.y + 96.f), IM_COL32(255, 255, 255, 16), panelRounding, ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersTopRight);
    if (panelMode_ != VrPlayerPanelMode::Normal) {
        const float fillRight = panelMin.x + panelWidth * scrubPreviewProgress_;
        draw->AddRectFilled(panelMin, ImVec2(fillRight, panelMax.y), IM_COL32(45, 110, 220, 158), panelRounding);
        draw->AddLine(ImVec2(fillRight, panelMin.y + 12.f), ImVec2(fillRight, panelMax.y - 12.f), IM_COL32(235, 245, 255, 210), 2.4f);
    } else if (timelineHovered || timelineActive) {
        draw->AddRectFilled(timelineHitMin, timelineHitMax, IM_COL32(45, 95, 180, 58), 20.f);
    }
    draw->AddLine(ImVec2(panelMin.x + 20.f, controlsRowMin.y - 18.f), ImVec2(panelMax.x - 20.f, controlsRowMin.y - 18.f), IM_COL32(220, 235, 255, 26), 1.4f);
    draw->AddRect(panelMin, panelMax, IM_COL32(220, 235, 255, 48), panelRounding, 0, 1.0f);

    const bool timeHovered = !scrubModeActive && pointInRect(mouse, currentTimeHitMin, currentTimeHitMax);
    const bool durationHovered = !scrubModeActive && pointInRect(mouse, durationHitMin, durationHitMax);
    if (timeHovered) draw->AddRectFilled(currentTimeHitMin, currentTimeHitMax, IM_COL32(70, 110, 185, 92), 15.f);
    if (durationHovered) draw->AddRectFilled(durationHitMin, durationHitMax, IM_COL32(70, 110, 185, 92), 15.f);
    draw->AddText(ImVec2(timelineRowMin.x + 18.f, timelineY - 15.f), IM_COL32(245, 247, 255, 255), position);
    const ImVec2 durationSize = ImGui::CalcTextSize(durationText);
    draw->AddText(ImVec2(timelineRowMax.x - durationSize.x - 18.f, timelineY - 15.f), IM_COL32(245, 247, 255, 255), durationText);

    draw->AddRectFilled(ImVec2(timelineTrackLeft, timelineY - 5.f), ImVec2(timelineTrackRight, timelineY + 5.f), IM_COL32(76, 82, 96, 226), 5.f);
    draw->AddRectFilled(ImVec2(timelineTrackLeft, timelineY - 5.f), ImVec2(timelineTrackLeft + timelineTrackWidth * buffered, timelineY + 5.f), IM_COL32(100, 116, 146, 220), 5.f);
    draw->AddRectFilled(ImVec2(timelineTrackLeft, timelineY - 5.f), ImVec2(timelineTrackLeft + timelineTrackWidth * progress, timelineY + 5.f), IM_COL32(172, 212, 255, 255), 5.f);
    const float knobProgress = (timelineHovered || timelineActive) ? timelinePreviewProgress : progress;
    const float knobX = timelineTrackLeft + timelineTrackWidth * knobProgress;
    draw->AddCircleFilled(ImVec2(knobX, timelineY), timelineHovered || timelineActive ? 13.f : 9.f, IM_COL32(246, 248, 255, 255));

    const std::string title = state_.title.empty() ? "DDD-VR OpenXR Player" : state_.title;
    const ImVec2 titleMin(controlsRowMin.x + 104.f, controlsRowMin.y + 60.f);
    const ImVec2 titleMax(centerX - 250.f, controlsRowMin.y + 116.f);
    draw->PushClipRect(titleMin, titleMax, true);
    draw->AddText(titleMin, IM_COL32(218, 226, 238, 240), title.c_str());
    draw->PopClipRect();

    auto drawIconButton = [&](const char* id, const ImVec2& min, const ImVec2& max, int kind) {
        bool hovered = false;
        bool active = false;
        bool clicked = false;
        if (panelMode_ == VrPlayerPanelMode::Normal) {
            ImGui::SetCursorScreenPos(min);
            ImGui::InvisibleButton(id, ImVec2(max.x - min.x, max.y - min.y));
            hovered = ImGui::IsItemHovered();
            active = ImGui::IsItemActive();
            clicked = ImGui::IsItemClicked(0);
        }
        const ImU32 buttonBg = kind == 1
            ? IM_COL32(236, 242, 255, panelMode_ == VrPlayerPanelMode::Normal ? 42 : 18)
            : IM_COL32(24, 27, 35, panelMode_ == VrPlayerPanelMode::Normal ? 172 : 72);
        draw->AddRectFilled(min, max, active ? IM_COL32(90, 135, 220, 212) : (hovered ? IM_COL32(70, 105, 180, 205) : buttonBg), 16.f);
        draw->AddRect(min, max, IM_COL32(230, 240, 255, hovered || active ? 92 : 34), 16.f, 0, 1.f);
        const ImU32 iconColor = IM_COL32(246, 248, 255, panelMode_ == VrPlayerPanelMode::Normal ? 245 : 140);
        const float cx = (min.x + max.x) * 0.5f;
        const float cy = (min.y + max.y) * 0.5f;
        if (kind == 0 || kind == 2) {
            const char* label = kind == 0 ? "-15" : "+15";
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            draw->AddText(ImVec2(cx - textSize.x * 0.5f, cy - textSize.y * 0.5f), iconColor, label);
        } else if (state_.playing) {
            draw->AddRectFilled(ImVec2(cx - 16.f, cy - 24.f), ImVec2(cx - 5.f, cy + 24.f), iconColor, 2.f);
            draw->AddRectFilled(ImVec2(cx + 5.f, cy - 24.f), ImVec2(cx + 16.f, cy + 24.f), iconColor, 2.f);
        } else {
            draw->AddTriangleFilled(ImVec2(cx - 16.f, cy - 27.f), ImVec2(cx - 16.f, cy + 27.f), ImVec2(cx + 26.f, cy), iconColor);
        }
        return clicked;
    };

    const float buttonY = controlsRowMin.y + 42.f;
    const ImVec2 menuMin(controlsRowMin.x + 22.f, buttonY + 4.f);
    const ImVec2 menuMax(menuMin.x + 74.f, buttonY + 74.f);
    draw->AddRectFilled(menuMin, menuMax, IM_COL32(24, 27, 35, 158), 14.f);
    draw->AddLine(ImVec2(menuMin.x + 20.f, menuMin.y + 23.f), ImVec2(menuMin.x + 54.f, menuMin.y + 23.f), IM_COL32(235, 242, 255, 230), 3.4f);
    draw->AddLine(ImVec2(menuMin.x + 20.f, menuMin.y + 37.f), ImVec2(menuMin.x + 48.f, menuMin.y + 37.f), IM_COL32(235, 242, 255, 185), 3.4f);
    draw->AddLine(ImVec2(menuMin.x + 20.f, menuMin.y + 51.f), ImVec2(menuMin.x + 42.f, menuMin.y + 51.f), IM_COL32(235, 242, 255, 150), 3.4f);

    const ImVec2 backMin(centerX - 198.f, buttonY + 0.f);
    const ImVec2 backMax(backMin.x + 108.f, buttonY + 82.f);
    const ImVec2 playMin(centerX - 64.f, buttonY - 8.f);
    const ImVec2 playMax(playMin.x + 128.f, buttonY + 94.f);
    const ImVec2 forwardMin(centerX + 90.f, buttonY + 0.f);
    const ImVec2 forwardMax(forwardMin.x + 108.f, buttonY + 82.f);
    if (drawIconButton("DDDVR_BTN_BACK", backMin, backMax, 0)) seekBackRequested_ = true;
    if (drawIconButton("DDDVR_BTN_PLAY", playMin, playMax, 1)) playPauseRequested_ = true;
    if (drawIconButton("DDDVR_BTN_FORWARD", forwardMin, forwardMax, 2)) seekForwardRequested_ = true;

    auto drawChip = [&](const char* id, const char* label, const ImVec2& min, const ImVec2& max) {
        bool hovered = false;
        bool clicked = false;
        if (panelMode_ == VrPlayerPanelMode::Normal) {
            ImGui::SetCursorScreenPos(min);
            ImGui::InvisibleButton(id, ImVec2(max.x - min.x, max.y - min.y));
            hovered = ImGui::IsItemHovered();
            clicked = ImGui::IsItemClicked(0);
        }
        draw->AddRectFilled(min, max, hovered ? IM_COL32(74, 132, 198, 190) : IM_COL32(24, 27, 34, panelMode_ == VrPlayerPanelMode::Normal ? 152 : 82), 13.f);
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        draw->AddText(ImVec2((min.x + max.x - textSize.x) * 0.5f, (min.y + max.y - textSize.y) * 0.5f), IM_COL32(222, 234, 250, panelMode_ == VrPlayerPanelMode::Normal ? 232 : 145), label);
        return clicked;
    };
    const ImVec2 audioMin(panelMax.x - 388.f, buttonY + 5.f);
    const ImVec2 audioMax(audioMin.x + 126.f, buttonY + 75.f);
    if (drawChip("DDDVR_CHIP_AUDIO", "AUDIO", audioMin, audioMax)) {
        audioPopupOpen_ = !audioPopupOpen_;
    }
    drawChip("DDDVR_CHIP_STEREO", state_.stereoModeLabel.empty() ? "2D" : state_.stereoModeLabel.c_str(), ImVec2(panelMax.x - 244.f, buttonY + 5.f), ImVec2(panelMax.x - 140.f, buttonY + 75.f));
    drawChip("DDDVR_CHIP_MORE", "...", ImVec2(panelMax.x - 118.f, buttonY + 5.f), ImVec2(panelMax.x - 42.f, buttonY + 75.f));

    if (panelMode_ != VrPlayerPanelMode::Normal || timelineHovered || timelineActive) {
        const float previewProgress = panelMode_ != VrPlayerPanelMode::Normal ? scrubPreviewProgress_ : timelinePreviewProgress;
        const float previewX = panelMode_ != VrPlayerPanelMode::Normal
            ? panelMin.x + panelWidth * previewProgress
            : timelineTrackLeft + timelineTrackWidth * previewProgress;
        const int64_t previewMs = static_cast<int64_t>(duration * previewProgress);
        char previewText[32]{};
        formatTime(previewMs, previewText, sizeof(previewText));
        const ImVec2 textSize = ImGui::CalcTextSize(previewText);
        const float bubbleHalfWidth = textSize.x * 0.5f + 18.f;
        const float targetTooltipX = std::clamp(previewX, panelMin.x + bubbleHalfWidth, panelMax.x - bubbleHalfWidth);
        if (tooltipX_ < 0.f) tooltipX_ = targetTooltipX;
        const float tooltipK = 1.f - std::exp(-std::max(io.DeltaTime, 0.f) * 20.f);
        tooltipX_ += (targetTooltipX - tooltipX_) * tooltipK;
        const ImVec2 bubbleMin(tooltipX_ - bubbleHalfWidth, timelineRowMin.y - 34.f);
        const ImVec2 bubbleMax(tooltipX_ + bubbleHalfWidth, timelineRowMin.y + 4.f);
        draw->AddLine(ImVec2(previewX, bubbleMax.y), ImVec2(previewX, panelMode_ != VrPlayerPanelMode::Normal ? panelMax.y - 14.f : timelineY - 9.f), IM_COL32(235, 245, 255, 190), 2.f);
        draw->AddRectFilled(bubbleMin, bubbleMax, IM_COL32(12, 14, 18, 238), 10.f);
        draw->AddRect(bubbleMin, bubbleMax, IM_COL32(220, 235, 255, 55), 10.f, 0, 1.f);
        draw->AddText(ImVec2(tooltipX_ - textSize.x * 0.5f, bubbleMin.y + 8.f), IM_COL32(240, 246, 255, 255), previewText);
    } else {
        tooltipX_ = -1.f;
    }

    if (audioPopupOpen_) {
        const ImVec2 popupMin(panelMin.x + 28.f, 22.f);
        const ImVec2 popupMax(popupMin.x + 520.f, popupMin.y + 102.f + 48.f * static_cast<float>(std::max<size_t>(state_.audioTrackLabels.size(), 1)));
        draw->AddRectFilled(popupMin, popupMax, IM_COL32(10, 12, 18, 238), 22.f);
        draw->AddRect(popupMin, popupMax, IM_COL32(220, 235, 255, 58), 22.f, 0, 1.1f);
        draw->AddText(ImVec2(popupMin.x + 24.f, popupMin.y + 20.f), IM_COL32(245, 247, 255, 255), "Audio tracks");
        const int rowCount = static_cast<int>(std::max<size_t>(state_.audioTrackLabels.size(), 1));
        for (int i = 0; i < rowCount; ++i) {
            const ImVec2 rowMin(popupMin.x + 18.f, popupMin.y + 64.f + i * 48.f);
            const ImVec2 rowMax(popupMax.x - 18.f, rowMin.y + 40.f);
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(rowMin);
            ImGui::InvisibleButton("DDDVR_AUDIO_ROW", ImVec2(rowMax.x - rowMin.x, rowMax.y - rowMin.y));
            const bool hovered = ImGui::IsItemHovered();
            const bool selected = i == state_.selectedAudioTrackIndex;
            if (hovered || selected) {
                draw->AddRectFilled(rowMin, rowMax, selected ? IM_COL32(220, 230, 245, 220) : IM_COL32(74, 132, 198, 170), 10.f);
            }
            const std::string rowLabel = state_.audioTrackLabels.empty()
                ? std::string("No audio tracks")
                : state_.audioTrackLabels[static_cast<size_t>(i)];
            draw->PushClipRect(ImVec2(rowMin.x + 14.f, rowMin.y), ImVec2(rowMax.x - 10.f, rowMax.y), true);
            draw->AddText(ImVec2(rowMin.x + 14.f, rowMin.y + 9.f), selected ? IM_COL32(12, 16, 24, 255) : IM_COL32(226, 236, 250, 245), rowLabel.c_str());
            draw->PopClipRect();
            if (ImGui::IsItemClicked(0)) {
                if (!state_.audioTrackLabels.empty()) {
                    audioTrackSelected_ = true;
                    requestedAudioTrackIndex_ = i;
                }
                audioPopupOpen_ = false;
            }
            ImGui::PopID();
        }
        if (ImGui::IsMouseClicked(0) &&
            !pointInRect(mouse, popupMin, popupMax) &&
            !pointInRect(mouse, audioMin, audioMax)) {
            audioPopupOpen_ = false;
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

bool VrPlayerPanel::consumePlayPauseRequested() {
    const bool value = playPauseRequested_;
    playPauseRequested_ = false;
    return value;
}

bool VrPlayerPanel::consumeSeekBackRequested() {
    const bool value = seekBackRequested_;
    seekBackRequested_ = false;
    return value;
}

bool VrPlayerPanel::consumeSeekForwardRequested() {
    const bool value = seekForwardRequested_;
    seekForwardRequested_ = false;
    return value;
}

bool VrPlayerPanel::consumeExitRequested() {
    const bool value = exitRequested_;
    exitRequested_ = false;
    return value;
}

bool VrPlayerPanel::consumeRecenterRequested() {
    const bool value = recenterRequested_;
    recenterRequested_ = false;
    return value;
}

bool VrPlayerPanel::consumeTimelineSeekRequested(int64_t* outPositionMs) {
    const bool value = timelineSeekRequested_;
    if (value && outPositionMs != nullptr) {
        *outPositionMs = requestedTimelinePositionMs_;
    }
    timelineSeekRequested_ = false;
    return value;
}

bool VrPlayerPanel::consumeAudioTrackSelected(int* outTrackIndex) {
    const bool value = audioTrackSelected_;
    if (value && outTrackIndex != nullptr) {
        *outTrackIndex = requestedAudioTrackIndex_;
    }
    audioTrackSelected_ = false;
    requestedAudioTrackIndex_ = -1;
    return value;
}

const char* VrPlayerPanel::formatTime(int64_t timeMs, char* buffer, int bufferSize) {
    if (buffer == nullptr || bufferSize <= 0) return "";
    if (timeMs < 0) timeMs = 0;
    const int64_t totalSeconds = timeMs / 1000;
    const int64_t hours = totalSeconds / 3600;
    const int64_t minutes = (totalSeconds / 60) % 60;
    const int64_t seconds = totalSeconds % 60;
    if (hours > 0) {
        std::snprintf(buffer, static_cast<size_t>(bufferSize), "%lld:%02lld:%02lld",
                      static_cast<long long>(hours),
                      static_cast<long long>(minutes),
                      static_cast<long long>(seconds));
    } else {
        std::snprintf(buffer, static_cast<size_t>(bufferSize), "%02lld:%02lld",
                      static_cast<long long>(minutes),
                      static_cast<long long>(seconds));
    }
    return buffer;
}
