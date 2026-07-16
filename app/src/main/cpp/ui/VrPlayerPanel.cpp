#include "VrPlayerPanel.h"

#include "VrPlayerTheme.h"
#include "../third/imgui/imgui.h"
#include "../third/imgui/imgui_internal.h"
#include "../util/XrLog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
enum class IconKind {
    Playlist,
    Volume,
    Muted,
    Previous,
    Play,
    Pause,
    Next,
    Environment,
    Microphone,
    More,
    Close,
    Refresh
};

float clamp01(float value) {
    return std::clamp(value, 0.f, 1.f);
}

bool nearlyEqual(float a, float b) {
    return std::fabs(a - b) < 0.01f;
}

ImRect centeredRect(float centerX, float centerY, float width, float height) {
    return ImRect(
        ImVec2(centerX - width * 0.5f, centerY - height * 0.5f),
        ImVec2(centerX + width * 0.5f, centerY + height * 0.5f)
    );
}

void drawCenteredText(ImDrawList* draw, const ImRect& rect, const char* text, ImU32 color) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    draw->AddText(
        ImVec2(rect.Min.x + (rect.GetWidth() - size.x) * 0.5f,
               rect.Min.y + (rect.GetHeight() - size.y) * 0.5f),
        color,
        text
    );
}

void drawClippedText(ImDrawList* draw, const ImRect& rect, const char* text, ImU32 color) {
    draw->PushClipRect(rect.Min, rect.Max, true);
    draw->AddText(ImVec2(rect.Min.x, rect.Min.y + (rect.GetHeight() - ImGui::GetTextLineHeight()) * 0.5f), color, text);
    draw->PopClipRect();
}

void drawTextScaled(ImDrawList* draw, const ImVec2& pos, const char* text, ImU32 color, float scale) {
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, pos, color, text);
}

std::string speedLabel(float speed) {
    if (nearlyEqual(speed, 1.0f)) return "1x";
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%.2gx", static_cast<double>(speed));
    return buffer;
}

void drawIcon(ImDrawList* draw, const ImRect& rect, IconKind kind, ImU32 color) {
    const float cx = rect.GetCenter().x;
    const float cy = rect.GetCenter().y;
    const float s = std::min(rect.GetWidth(), rect.GetHeight());
    const float r = s * 0.24f;
    const float stroke = std::max(2.0f, s * 0.045f);

    switch (kind) {
        case IconKind::Playlist:
            for (int i = 0; i < 3; ++i) {
                const float y = cy - r + i * r;
                draw->AddCircleFilled(ImVec2(cx - r * 1.15f, y), stroke * 0.55f, color);
                draw->AddLine(ImVec2(cx - r * 0.55f, y), ImVec2(cx + r * 1.25f, y), color, stroke);
            }
            break;
        case IconKind::Volume:
        case IconKind::Muted:
            draw->AddRectFilled(ImVec2(cx - r * 1.4f, cy - r * 0.55f), ImVec2(cx - r * 0.75f, cy + r * 0.55f), color, 2.f);
            draw->AddTriangleFilled(
                ImVec2(cx - r * 0.75f, cy - r * 0.9f),
                ImVec2(cx - r * 0.75f, cy + r * 0.9f),
                ImVec2(cx + r * 0.25f, cy + r * 0.45f),
                color
            );
            if (kind == IconKind::Muted) {
                draw->AddLine(ImVec2(cx + r * 0.55f, cy - r), ImVec2(cx + r * 1.35f, cy + r), color, stroke);
                draw->AddLine(ImVec2(cx + r * 1.35f, cy - r), ImVec2(cx + r * 0.55f, cy + r), color, stroke);
            } else {
                draw->AddBezierCubic(
                    ImVec2(cx + r * 0.35f, cy - r * 0.75f),
                    ImVec2(cx + r * 1.1f, cy - r * 0.35f),
                    ImVec2(cx + r * 1.1f, cy + r * 0.35f),
                    ImVec2(cx + r * 0.35f, cy + r * 0.75f),
                    color,
                    stroke
                );
            }
            break;
        case IconKind::Previous:
            draw->AddRectFilled(ImVec2(cx - r * 1.15f, cy - r), ImVec2(cx - r * 0.9f, cy + r), color, 1.5f);
            draw->AddTriangleFilled(ImVec2(cx + r, cy - r), ImVec2(cx + r, cy + r), ImVec2(cx - r * 0.65f, cy), color);
            break;
        case IconKind::Play:
            draw->AddTriangleFilled(ImVec2(cx - r * 0.55f, cy - r), ImVec2(cx - r * 0.55f, cy + r), ImVec2(cx + r * 0.9f, cy), color);
            break;
        case IconKind::Pause:
            draw->AddRectFilled(ImVec2(cx - r * 0.65f, cy - r), ImVec2(cx - r * 0.18f, cy + r), color, 2.f);
            draw->AddRectFilled(ImVec2(cx + r * 0.18f, cy - r), ImVec2(cx + r * 0.65f, cy + r), color, 2.f);
            break;
        case IconKind::Next:
            draw->AddRectFilled(ImVec2(cx + r * 0.9f, cy - r), ImVec2(cx + r * 1.15f, cy + r), color, 1.5f);
            draw->AddTriangleFilled(ImVec2(cx - r, cy - r), ImVec2(cx - r, cy + r), ImVec2(cx + r * 0.65f, cy), color);
            break;
        case IconKind::Environment:
            draw->AddCircle(ImVec2(cx, cy), r * 1.05f, color, 32, stroke);
            draw->AddLine(ImVec2(cx - r * 1.05f, cy), ImVec2(cx + r * 1.05f, cy), color, stroke);
            draw->AddBezierCubic(
                ImVec2(cx, cy - r * 1.05f),
                ImVec2(cx - r * 0.65f, cy - r * 0.35f),
                ImVec2(cx - r * 0.65f, cy + r * 0.35f),
                ImVec2(cx, cy + r * 1.05f),
                color,
                stroke
            );
            draw->AddBezierCubic(
                ImVec2(cx, cy - r * 1.05f),
                ImVec2(cx + r * 0.65f, cy - r * 0.35f),
                ImVec2(cx + r * 0.65f, cy + r * 0.35f),
                ImVec2(cx, cy + r * 1.05f),
                color,
                stroke
            );
            break;
        case IconKind::Microphone:
            draw->AddRectFilled(ImVec2(cx - r * 0.48f, cy - r * 1.18f), ImVec2(cx + r * 0.48f, cy + r * 0.2f), color, r * 0.48f);
            draw->AddLine(ImVec2(cx - r * 0.95f, cy - r * 0.12f), ImVec2(cx - r * 0.95f, cy + r * 0.25f), color, stroke);
            draw->AddLine(ImVec2(cx + r * 0.95f, cy - r * 0.12f), ImVec2(cx + r * 0.95f, cy + r * 0.25f), color, stroke);
            draw->PathArcTo(ImVec2(cx, cy + r * 0.18f), r * 0.95f, 0.0f, 3.14159f, 18);
            draw->PathStroke(color, 0, stroke);
            draw->AddLine(ImVec2(cx, cy + r * 0.48f), ImVec2(cx, cy + r * 1.1f), color, stroke);
            draw->AddLine(ImVec2(cx - r * 0.62f, cy + r * 1.1f), ImVec2(cx + r * 0.62f, cy + r * 1.1f), color, stroke);
            break;
        case IconKind::More:
            for (int i = 0; i < 3; ++i) {
                draw->AddCircleFilled(ImVec2(cx - r + i * r, cy), stroke * 0.8f, color);
            }
            break;
        case IconKind::Close:
            draw->AddLine(ImVec2(cx - r, cy - r), ImVec2(cx + r, cy + r), color, stroke);
            draw->AddLine(ImVec2(cx + r, cy - r), ImVec2(cx - r, cy + r), color, stroke);
            break;
        case IconKind::Refresh:
            draw->PathArcTo(ImVec2(cx, cy), r, -2.3f, 1.1f, 24);
            draw->PathStroke(color, 0, stroke);
            draw->AddTriangleFilled(
                ImVec2(cx + r * 0.9f, cy - r * 0.08f),
                ImVec2(cx + r * 1.35f, cy - r * 0.15f),
                ImVec2(cx + r * 1.04f, cy + r * 0.35f),
                color
            );
            break;
    }
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
    ImGui::SetWindowFontScale(1.10f);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float centerX = canvas.x * 0.5f;
    const bool modalOpen = state_.activeModal != VrPlayerModal::None;
    const float barWidth = std::min(VrPlayerTheme::MainBarWidth, canvas.x - 120.f);
    const float barTop = modalOpen
        ? canvas.y - VrPlayerTheme::MainBarHeight - 34.f
        : canvas.y - 190.f;
    const ImRect bar(
        ImVec2(centerX - barWidth * 0.5f, barTop),
        ImVec2(centerX + barWidth * 0.5f, barTop + VrPlayerTheme::MainBarHeight)
    );
    const float dragHandleHeight = modalOpen ? 20.f : VrPlayerTheme::DragHandleHeight;
    const float dragHandleTop = std::min(
        bar.Max.y + (modalOpen ? 6.f : 14.f),
        canvas.y - dragHandleHeight - 8.f
    );
    const ImRect dragHandle(
        ImVec2(centerX - VrPlayerTheme::DragHandleWidth * 0.5f, dragHandleTop),
        ImVec2(centerX + VrPlayerTheme::DragHandleWidth * 0.5f, dragHandleTop + dragHandleHeight)
    );

    char position[32]{};
    char durationText[32]{};
    formatTime(state_.positionMs, position, sizeof(position));
    formatTime(state_.durationMs, durationText, sizeof(durationText));
    const int64_t durationMs = std::max<int64_t>(state_.durationMs, 1);
    const float progress = clamp01(static_cast<float>(state_.positionMs) / static_cast<float>(durationMs));
    const float buffered = clamp01(static_cast<float>(state_.bufferedPositionMs) / static_cast<float>(durationMs));

    std::string tooltip;
    ImVec2 tooltipAnchor(0.f, 0.f);
    auto setTooltip = [&](const char* value, const ImRect& rect) {
        tooltip = value;
        tooltipAnchor = ImVec2(rect.GetCenter().x, rect.Min.y);
    };

    auto pushAction = [&](VrPlayerPanelActionType type, int intValue = 0, float floatValue = 0.f, std::string stringValue = {}) {
        VrPlayerPanelAction action{};
        action.type = type;
        action.intValue = intValue;
        action.floatValue = floatValue;
        action.stringValue = std::move(stringValue);
        pendingActions_.push_back(std::move(action));
    };

    auto drawSmallIconButton = [&](const char* id, const ImRect& rect, IconKind icon, const char* tooltipText) {
        ImGui::SetCursorScreenPos(rect.Min);
        const bool clicked = ImGui::InvisibleButton(id, rect.GetSize());
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (hovered) setTooltip(tooltipText, rect);

        const ImU32 bg = active ? IM_COL32(98, 139, 215, 210) :
            hovered ? IM_COL32(82, 100, 132, 185) :
            IM_COL32(255, 255, 255, 0);
        if (hovered || active) {
            draw->AddRectFilled(rect.Min, rect.Max, bg, 15.f);
        }
        drawIcon(draw, rect, icon, hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(250, 252, 255, 238));
        return clicked;
    };

    auto drawProjectionButton = [&](const ImRect& rect) {
        ImGui::SetCursorScreenPos(rect.Min);
        const bool clicked = ImGui::InvisibleButton("DDDVR_BTN_PROJECTION", rect.GetSize());
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (hovered) setTooltip("Режим", rect);
        draw->AddRectFilled(
            rect.Min,
            rect.Max,
            active ? IM_COL32(98, 139, 215, 210) : (hovered ? IM_COL32(82, 100, 132, 185) : IM_COL32(255, 255, 255, 0)),
            15.f
        );
        const std::string label = state_.projectionModeLabel.empty() ? "2D" : state_.projectionModeLabel;
        drawCenteredText(draw, rect, label.c_str(), hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(250, 252, 255, 238));
        return clicked;
    };

    auto drawToggle = [&](const char* id, const ImRect& rect, bool enabled, const char* tooltipText) {
        ImGui::SetCursorScreenPos(rect.Min);
        const bool clicked = ImGui::InvisibleButton(id, rect.GetSize());
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) setTooltip(tooltipText, rect);
        draw->AddRectFilled(rect.Min, rect.Max, enabled ? IM_COL32(60, 118, 235, 235) : IM_COL32(88, 91, 98, 205), rect.GetHeight() * 0.5f);
        const float knobRadius = rect.GetHeight() * 0.36f;
        const float knobX = enabled ? rect.Max.x - rect.GetHeight() * 0.5f : rect.Min.x + rect.GetHeight() * 0.5f;
        draw->AddCircleFilled(ImVec2(knobX, rect.GetCenter().y), knobRadius, IM_COL32(245, 248, 255, 245));
        return clicked;
    };

    auto drawModalFrame = [&](const ImRect& modal, const char* title, bool playlistModal) {
        draw->AddRectFilled(modal.Min, modal.Max, VrPlayerTheme::ModalBg, VrPlayerTheme::ModalRounding);
        draw->AddRect(modal.Min, modal.Max, IM_COL32(235, 242, 255, 38), VrPlayerTheme::ModalRounding, 0, 1.0f);
        const ImVec2 titleSize = ImGui::CalcTextSize(title);
        draw->AddText(
            ImVec2(modal.GetCenter().x - titleSize.x * 0.5f, modal.Min.y + 24.f),
            VrPlayerTheme::TextPrimary,
            title
        );
        const ImRect closeHitRect(ImVec2(modal.Max.x - 66.f, modal.Min.y + 6.f), ImVec2(modal.Max.x - 10.f, modal.Min.y + 62.f));
        const ImRect closeRect(ImVec2(modal.Max.x - 58.f, modal.Min.y + 12.f), ImVec2(modal.Max.x - 18.f, modal.Min.y + 52.f));
        ImGui::SetCursorScreenPos(closeHitRect.Min);
        const bool closeClicked = ImGui::InvisibleButton(
            playlistModal ? "DDDVR_PLAYLIST_CLOSE" : "DDDVR_SETTINGS_CLOSE",
            closeHitRect.GetSize()
        );
        const bool closeHovered = ImGui::IsItemHovered();
        const bool closeActive = ImGui::IsItemActive();
        draw->AddRectFilled(
            closeRect.Min,
            closeRect.Max,
            closeActive ? IM_COL32(98, 139, 215, 220) : (closeHovered ? IM_COL32(82, 100, 132, 190) : IM_COL32(0, 0, 0, 116)),
            12.f
        );
        draw->AddRect(closeRect.Min, closeRect.Max, IM_COL32(255, 255, 255, 42), 12.f, 0, 1.f);
        const ImRect closeIcon(ImVec2(closeRect.Min.x + 12.f, closeRect.Min.y + 12.f), ImVec2(closeRect.Max.x - 12.f, closeRect.Max.y - 12.f));
        drawIcon(draw, closeIcon, IconKind::Close, closeHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(245, 248, 255, 242));
        if (closeClicked) {
            exitRequested_ = true;
            XR_LOGI("DDDVR/OpenXRUi", "XR_UI_ACTION modal_close_exit");
        }
    };

    auto drawSegment = [&](const char* id, const ImRect& rect, const char* text, bool selected) {
        ImGui::SetCursorScreenPos(rect.Min);
        const bool clicked = ImGui::InvisibleButton(id, rect.GetSize());
        const bool hovered = ImGui::IsItemHovered();
        draw->AddRectFilled(
            rect.Min,
            rect.Max,
            selected ? IM_COL32(80, 104, 146, 220) : (hovered ? VrPlayerTheme::RowHover : IM_COL32(48, 49, 54, 172)),
            9.f
        );
        drawCenteredText(draw, rect, text, selected ? VrPlayerTheme::TextPrimary : VrPlayerTheme::TextMuted);
        return clicked;
    };

    auto drawRowBase = [&](const ImRect& row, bool selected, bool enabled, const char* id) {
        ImGui::SetCursorScreenPos(row.Min);
        const bool clicked = ImGui::InvisibleButton(id, row.GetSize());
        const bool hovered = ImGui::IsItemHovered();
        draw->AddRectFilled(
            row.Min,
            row.Max,
            selected ? IM_COL32(72, 88, 116, 222) : (hovered ? VrPlayerTheme::RowHover : VrPlayerTheme::RowBg),
            10.f
        );
        if (!enabled) {
            draw->AddRectFilled(row.Min, row.Max, IM_COL32(0, 0, 0, 76), 10.f);
        }
        return clicked;
    };

    auto drawTrackRows = [&](const std::vector<VrTrackRow>& sourceRows, const ImRect& content, bool audioRows) {
        std::vector<VrTrackRow> rows = sourceRows;
        if (audioRows && rows.empty() && !state_.audioTrackLabels.empty()) {
            rows.reserve(state_.audioTrackLabels.size());
            for (size_t i = 0; i < state_.audioTrackLabels.size(); ++i) {
                VrTrackRow row{};
                row.id = "legacy_audio:" + std::to_string(i);
                row.title = state_.audioTrackLabels[i];
                row.selected = static_cast<int>(i) == state_.selectedAudioTrackIndex;
                rows.push_back(std::move(row));
            }
        }
        if (rows.empty()) {
            draw->AddText(ImVec2(content.Min.x + 16.f, content.Min.y + 18.f), VrPlayerTheme::TextMuted, audioRows ? "Нет аудиодорожек" : "Нет субтитров");
            return;
        }
        const float rowHeight = audioRows ? 52.f : 54.f;
        const float gap = audioRows ? 7.f : 6.f;
        const int visibleRows = std::min<int>(
            static_cast<int>(rows.size()),
            std::max(0, static_cast<int>((content.GetHeight() + gap) / (rowHeight + gap)))
        );
        for (int i = 0; i < visibleRows; ++i) {
            const VrTrackRow& row = rows[static_cast<size_t>(i)];
            const ImRect rowRect(
                ImVec2(content.Min.x, content.Min.y + i * (rowHeight + gap)),
                ImVec2(content.Max.x, content.Min.y + i * (rowHeight + gap) + rowHeight)
            );
            ImGui::PushID(audioRows ? 4000 + i : 5000 + i);
            const bool clicked = drawRowBase(rowRect, row.selected, row.enabled, audioRows ? "DDDVR_AUDIO_ROW" : "DDDVR_SUB_ROW");
            if (audioRows) {
                const ImRect iconRect(
                    ImVec2(rowRect.Min.x + 16.f, rowRect.Min.y + 9.f),
                    ImVec2(rowRect.Min.x + 50.f, rowRect.Min.y + 43.f)
                );
                draw->AddCircleFilled(iconRect.GetCenter(), 17.f, row.selected ? IM_COL32(74, 116, 205, 198) : IM_COL32(255, 255, 255, 24));
                drawIcon(draw, iconRect, IconKind::Microphone, row.enabled ? IM_COL32(255, 255, 255, 244) : VrPlayerTheme::TextMuted);
            }
            const float textStartX = audioRows ? rowRect.Min.x + 62.f : rowRect.Min.x + 18.f;
            const ImRect textRect(ImVec2(textStartX, rowRect.Min.y + 5.f), ImVec2(rowRect.Max.x - 62.f, rowRect.Max.y - 4.f));
            const std::string singleLineTitle =
                audioRows && !row.subtitle.empty() ? row.title + " - " + row.subtitle : row.title;
            const char* titleText = singleLineTitle.c_str();
            const float titleScale = audioRows ? 1.05f : 1.0f;
            const float titleFontSize = ImGui::GetFontSize() * titleScale;
            const ImVec2 titleSize = ImGui::GetFont()->CalcTextSizeA(titleFontSize, 100000.f, 0.0f, titleText);
            const bool textOverflow = titleSize.x > textRect.GetWidth() || titleSize.y > textRect.GetHeight();
            if (textOverflow) {
                draw->AddRect(rowRect.Min, rowRect.Max, IM_COL32(255, 44, 44, 230), 10.f, 0, 2.0f);
                static std::string lastOverflowKey;
                const std::string overflowKey = row.id + ":" + singleLineTitle;
                if (overflowKey != lastOverflowKey) {
                    XR_LOGW(
                        "DDDVR/OpenXRUi",
                        "XR_AUDIO_ROW_TEXT_OVERFLOW id=%s textWidth=%.1f rectWidth=%.1f text=%s",
                        row.id.c_str(),
                        titleSize.x,
                        textRect.GetWidth(),
                        titleText
                    );
                    lastOverflowKey = overflowKey;
                }
            }
            draw->PushClipRect(textRect.Min, textRect.Max, true);
            drawTextScaled(
                draw,
                ImVec2(textRect.Min.x, rowRect.Min.y + (audioRows ? 15.f : 9.f)),
                titleText,
                row.enabled ? VrPlayerTheme::TextPrimary : VrPlayerTheme::TextMuted,
                titleScale
            );
            if (!audioRows && !row.subtitle.empty()) {
                drawTextScaled(
                    draw,
                    ImVec2(textRect.Min.x, rowRect.Min.y + 31.f),
                    row.subtitle.c_str(),
                    VrPlayerTheme::TextMuted,
                    0.84f
                );
            }
            draw->PopClipRect();
            if (row.selected) {
                const ImVec2 checkCenter(rowRect.Max.x - 34.f, rowRect.GetCenter().y);
                draw->AddCircleFilled(checkCenter, 15.f, IM_COL32(74, 116, 205, 230));
                draw->AddLine(ImVec2(checkCenter.x - 7.f, checkCenter.y), ImVec2(checkCenter.x - 2.f, checkCenter.y + 6.f), IM_COL32(255, 255, 255, 255), 2.7f);
                draw->AddLine(ImVec2(checkCenter.x - 2.f, checkCenter.y + 6.f), ImVec2(checkCenter.x + 8.f, checkCenter.y - 7.f), IM_COL32(255, 255, 255, 255), 2.7f);
            }
            if (clicked && row.enabled) {
                if (audioRows) {
                    if (row.id.rfind("legacy_audio:", 0) == 0) {
                        requestedAudioTrackIndex_ = std::atoi(row.id.c_str() + 13);
                    } else {
                        requestedAudioTrackIndex_ = i + 1;
                    }
                    audioTrackSelected_ = requestedAudioTrackIndex_ >= 0;
                    pushAction(VrPlayerPanelActionType::SelectAudioTrack, 0, 0.f, row.id);
                } else {
                    pushAction(VrPlayerPanelActionType::SelectSubtitleTrack, 0, 0.f, row.id);
                }
            }
            ImGui::PopID();
        }
    };

    if (modalOpen) {
        const float modalGap = 18.f;
        const float modalTopMargin = 18.f;
        const bool audioOnlyModal = state_.activeModal == VrPlayerModal::Settings && state_.activeSettingsTab == VrSettingsTab::Audio;
        const float modalWidth = audioOnlyModal ? 880.f : VrPlayerTheme::ModalWidth;
        const float desiredModalHeight = state_.activeModal == VrPlayerModal::Playlist ? 292.f : (audioOnlyModal ? 470.f : 312.f);
        const float availableModalHeight = std::max(220.f, bar.Min.y - modalGap - modalTopMargin);
        const float modalHeight = std::min(desiredModalHeight, availableModalHeight);
        const ImRect modal(
            ImVec2(centerX - modalWidth * 0.5f, bar.Min.y - modalGap - modalHeight),
            ImVec2(centerX + modalWidth * 0.5f, bar.Min.y - modalGap)
        );

        if (state_.activeModal == VrPlayerModal::Playlist) {
            drawModalFrame(modal, "Плейлист", true);
            const ImRect refreshRect(ImVec2(modal.Max.x - 96.f, modal.Min.y + 16.f), ImVec2(modal.Max.x - 60.f, modal.Min.y + 52.f));
            drawSmallIconButton("DDDVR_PLAYLIST_REFRESH", refreshRect, IconKind::Refresh, "Обновить");
            const ImRect rowsRect(ImVec2(modal.Min.x + 22.f, modal.Min.y + 70.f), ImVec2(modal.Max.x - 22.f, modal.Max.y - 18.f));
            const float rowHeight = 48.f;
            const float rowGap = 6.f;
            const int rowCount = std::min<int>(
                static_cast<int>(state_.playlistRows.size()),
                std::max(0, static_cast<int>((rowsRect.GetHeight() + rowGap) / (rowHeight + rowGap)))
            );
            if (rowCount == 0) {
                draw->AddText(ImVec2(rowsRect.Min.x + 12.f, rowsRect.Min.y + 14.f), VrPlayerTheme::TextMuted, "Плейлист пуст");
            }
            for (int i = 0; i < rowCount; ++i) {
                const VrPlaylistRow& row = state_.playlistRows[static_cast<size_t>(i)];
                const ImRect rowRect(
                    ImVec2(rowsRect.Min.x, rowsRect.Min.y + i * (rowHeight + rowGap)),
                    ImVec2(rowsRect.Max.x, rowsRect.Min.y + i * (rowHeight + rowGap) + rowHeight)
                );
                ImGui::PushID(3000 + i);
                const bool clicked = drawRowBase(rowRect, row.selected, true, "DDDVR_PLAYLIST_ROW");
                draw->AddText(ImVec2(rowRect.Min.x + 18.f, rowRect.Min.y + 12.f), row.selected ? VrPlayerTheme::AccentBlue : VrPlayerTheme::TextPrimary, "▥");
                const ImRect textRect(ImVec2(rowRect.Min.x + 48.f, rowRect.Min.y + 6.f), ImVec2(rowRect.Max.x - 18.f, rowRect.Max.y - 4.f));
                draw->PushClipRect(textRect.Min, textRect.Max, true);
                draw->AddText(ImVec2(textRect.Min.x, rowRect.Min.y + 8.f), VrPlayerTheme::TextPrimary, row.title.c_str());
                if (!row.subtitle.empty()) {
                    draw->AddText(ImVec2(textRect.Min.x, rowRect.Min.y + 28.f), VrPlayerTheme::TextMuted, row.subtitle.c_str());
                }
                draw->PopClipRect();
                if (clicked) {
                    pushAction(VrPlayerPanelActionType::SelectPlaylistItem, 0, 0.f, row.id);
                }
                ImGui::PopID();
            }
        } else {
            const bool audioOnlyModal = state_.activeSettingsTab == VrSettingsTab::Audio;
            drawModalFrame(modal, audioOnlyModal ? "Аудио" : "", false);
            if (!audioOnlyModal) {
                const float tabY = modal.Min.y + 14.f;
                const float tabWidth = 132.f;
                const float tabHeight = 36.f;
                const ImRect displayTab(ImVec2(modal.Min.x + 38.f, tabY), ImVec2(modal.Min.x + 38.f + tabWidth, tabY + tabHeight));
                const ImRect subTab(ImVec2(displayTab.Max.x + 8.f, tabY), ImVec2(displayTab.Max.x + 8.f + tabWidth, tabY + tabHeight));
                const ImRect audioTab(ImVec2(subTab.Max.x + 8.f, tabY), ImVec2(subTab.Max.x + 8.f + tabWidth, tabY + tabHeight));
                if (drawSegment("DDDVR_TAB_DISPLAY", displayTab, "Дисплей", state_.activeSettingsTab == VrSettingsTab::Display)) {
                    pushAction(VrPlayerPanelActionType::SetSettingsTab, static_cast<int>(VrSettingsTab::Display));
                }
                if (drawSegment("DDDVR_TAB_SUBTITLES", subTab, "Субтитры", state_.activeSettingsTab == VrSettingsTab::Subtitles)) {
                    pushAction(VrPlayerPanelActionType::SetSettingsTab, static_cast<int>(VrSettingsTab::Subtitles));
                }
                if (drawSegment("DDDVR_TAB_AUDIO", audioTab, "Аудио", state_.activeSettingsTab == VrSettingsTab::Audio)) {
                    pushAction(VrPlayerPanelActionType::SetSettingsTab, static_cast<int>(VrSettingsTab::Audio));
                }
            }

            const ImRect content(ImVec2(modal.Min.x + 28.f, modal.Min.y + 64.f), ImVec2(modal.Max.x - 28.f, modal.Max.y - 18.f));
            draw->PushClipRect(content.Min, content.Max, true);
            if (state_.activeSettingsTab == VrSettingsTab::Display) {
                draw->AddText(ImVec2(content.Min.x, content.Min.y), VrPlayerTheme::TextMuted, "Соотношение сторон");
                const char* aspects[] = {"Оригинал", "4:3", "16:9", "16:10", "1:1", "21:9"};
                for (int i = 0; i < 6; ++i) {
                    const int col = i % 3;
                    const int row = i / 3;
                    const ImRect chip(
                        ImVec2(content.Min.x + col * 154.f, content.Min.y + 22.f + row * 36.f),
                        ImVec2(content.Min.x + col * 154.f + 142.f, content.Min.y + 54.f + row * 36.f)
                    );
                    ImGui::PushID(100 + i);
                    if (drawSegment("DDDVR_ASPECT", chip, aspects[i], state_.display.aspectRatio == aspects[i])) {
                        pushAction(VrPlayerPanelActionType::SetAspectRatio, 0, 0.f, aspects[i]);
                    }
                    ImGui::PopID();
                }
                draw->AddText(ImVec2(content.Min.x, content.Min.y + 100.f), VrPlayerTheme::TextMuted, "Скорость");
                const float speeds[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
                for (int i = 0; i < 6; ++i) {
                    const ImRect chip(
                        ImVec2(content.Min.x + i * 76.f, content.Min.y + 122.f),
                        ImVec2(content.Min.x + i * 76.f + 66.f, content.Min.y + 154.f)
                    );
                    const std::string label = speedLabel(speeds[i]);
                    ImGui::PushID(200 + i);
                    if (drawSegment("DDDVR_SPEED", chip, label.c_str(), nearlyEqual(state_.display.playbackSpeed, speeds[i]))) {
                        pushAction(VrPlayerPanelActionType::SetPlaybackSpeed, 0, speeds[i]);
                    }
                    ImGui::PopID();
                }
                const ImRect enhanceRow(ImVec2(content.Min.x, content.Min.y + 170.f), ImVec2(content.Max.x, content.Min.y + 212.f));
                draw->AddRectFilled(enhanceRow.Min, enhanceRow.Max, VrPlayerTheme::RowBg, 10.f);
                draw->AddText(ImVec2(enhanceRow.Min.x + 18.f, enhanceRow.Min.y + 12.f), VrPlayerTheme::TextPrimary, "Повысить качество видео");
                if (drawToggle("DDDVR_ENHANCE_TOGGLE", ImRect(ImVec2(enhanceRow.Max.x - 72.f, enhanceRow.Min.y + 7.f), ImVec2(enhanceRow.Max.x - 18.f, enhanceRow.Min.y + 35.f)), state_.display.enhanceVideo, "Качество")) {
                    pushAction(VrPlayerPanelActionType::ToggleEnhanceVideo);
                }
                if (content.GetHeight() > 226.f) {
                    const ImRect slider(ImVec2(content.Min.x, content.Min.y + 220.f), ImVec2(content.Max.x, content.Min.y + 232.f));
                    draw->AddRectFilled(slider.Min, slider.Max, IM_COL32(52, 54, 61, 190), 6.f);
                    const float bright = clamp01(state_.display.brightness);
                    draw->AddRectFilled(slider.Min, ImVec2(slider.Min.x + slider.GetWidth() * bright, slider.Max.y), IM_COL32(92, 125, 180, 176), 6.f);
                    draw->AddCircleFilled(ImVec2(slider.Min.x + slider.GetWidth() * bright, slider.GetCenter().y), 7.f, IM_COL32(242, 246, 255, 240));
                }
            } else if (state_.activeSettingsTab == VrSettingsTab::Audio) {
                drawTrackRows(state_.audioTracks, content, true);
            } else {
                const ImRect enabledRow(ImVec2(content.Min.x, content.Min.y), ImVec2(content.Max.x, content.Min.y + 42.f));
                draw->AddRectFilled(enabledRow.Min, enabledRow.Max, VrPlayerTheme::RowBg, 10.f);
                draw->AddText(ImVec2(enabledRow.Min.x + 18.f, enabledRow.Min.y + 12.f), VrPlayerTheme::TextPrimary, "Субтитры");
                if (drawToggle("DDDVR_SUBTITLE_TOGGLE", ImRect(ImVec2(enabledRow.Max.x - 72.f, enabledRow.Min.y + 7.f), ImVec2(enabledRow.Max.x - 18.f, enabledRow.Min.y + 35.f)), state_.subtitles.enabled, "Субтитры")) {
                    pushAction(VrPlayerPanelActionType::ToggleSubtitles);
                }
                draw->AddText(ImVec2(content.Min.x, content.Min.y + 52.f), VrPlayerTheme::TextMuted, "Размер");
                const char* sizes[] = {"Мал.", "Сред.", "Круп."};
                for (int i = 0; i < 3; ++i) {
                    const ImRect chip(ImVec2(content.Min.x + i * 118.f, content.Min.y + 74.f), ImVec2(content.Min.x + i * 118.f + 108.f, content.Min.y + 106.f));
                    ImGui::PushID(600 + i);
                    drawSegment("DDDVR_SUB_SIZE", chip, sizes[i], state_.subtitles.sizeLabel == sizes[i] || (i == 1 && state_.subtitles.sizeLabel == "Средний"));
                    ImGui::PopID();
                }
                draw->AddText(ImVec2(content.Min.x, content.Min.y + 116.f), VrPlayerTheme::TextMuted, "Положение");
                const char* positions[] = {"Ниже", "Центр", "Выше"};
                for (int i = 0; i < 3; ++i) {
                    const ImRect chip(ImVec2(content.Min.x + i * 118.f, content.Min.y + 138.f), ImVec2(content.Min.x + i * 118.f + 108.f, content.Min.y + 170.f));
                    ImGui::PushID(700 + i);
                    drawSegment("DDDVR_SUB_POS", chip, positions[i], state_.subtitles.positionLabel == positions[i]);
                    ImGui::PopID();
                }
                drawTrackRows(state_.subtitleTracks, ImRect(ImVec2(content.Min.x, content.Min.y + 184.f), content.Max), false);
            }
            draw->PopClipRect();
        }
    }

    draw->AddRectFilled(bar.Min, bar.Max, VrPlayerTheme::BarBg, VrPlayerTheme::MainBarRounding);
    draw->AddRectFilled(bar.Min, ImVec2(bar.Max.x, bar.Min.y + 50.f), IM_COL32(255, 255, 255, 8), VrPlayerTheme::MainBarRounding, ImDrawFlags_RoundCornersTop);
    draw->PushClipRect(bar.Min, bar.Max, true);
    if (buffered > progress && bar.GetWidth() * buffered > 30.f) {
        ImRect bufferedRect = bar;
        bufferedRect.Max.x = bar.Min.x + bar.GetWidth() * buffered;
        draw->AddRectFilled(bufferedRect.Min, bufferedRect.Max, IM_COL32(135, 150, 178, 48), VrPlayerTheme::MainBarRounding, ImDrawFlags_RoundCornersLeft);
    }
    ImRect progressRect = bar;
    progressRect.Max.x = bar.Min.x + bar.GetWidth() * progress;
    const float progressWidth = progressRect.GetWidth();
    if (progressWidth > 30.f) {
        draw->AddRectFilled(progressRect.Min, progressRect.Max, VrPlayerTheme::ProgressFill, VrPlayerTheme::MainBarRounding, progress > 0.985f ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft);
        if (progressWidth > 42.f) {
            draw->AddLine(
                ImVec2(progressRect.Max.x, bar.Min.y + 9.f),
                ImVec2(progressRect.Max.x, bar.Max.y - 9.f),
                VrPlayerTheme::ProgressEdge,
                2.0f
            );
        }
    }
    draw->PopClipRect();
    const bool pointerOverBar =
        io.MousePos.x >= bar.Min.x && io.MousePos.x <= bar.Max.x &&
        io.MousePos.y >= bar.Min.y && io.MousePos.y <= bar.Max.y;
    if (pointerOverBar && timelineDragging_) {
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 8.0f);
        const float markerX = std::clamp(io.MousePos.x, bar.Min.x + 16.f, bar.Max.x - 16.f);
        const ImU32 markerGlow = IM_COL32(150, 225, 255, static_cast<int>(96 + pulse * 56));
        const ImU32 markerCore = IM_COL32(236, 250, 255, static_cast<int>(190 + pulse * 48));
        draw->AddLine(
            ImVec2(markerX, bar.Min.y + 8.f),
            ImVec2(markerX, bar.Max.y - 8.f),
            markerGlow,
            5.0f
        );
        draw->AddLine(
            ImVec2(markerX, bar.Min.y + 10.f),
            ImVec2(markerX, bar.Max.y - 10.f),
            markerCore,
            1.8f
        );
        const ImRect tip(
            ImVec2(markerX - 5.f, bar.Min.y - 7.f),
            ImVec2(markerX + 5.f, bar.Min.y + 3.f)
        );
        draw->AddRectFilled(tip.Min, tip.Max, markerCore, 2.f);
    }
    draw->AddRect(bar.Min, bar.Max, VrPlayerTheme::BarBorder, VrPlayerTheme::MainBarRounding, 0, 1.0f);

    const ImRect timelineHit(ImVec2(bar.Min.x, bar.Min.y), ImVec2(bar.Max.x, bar.Min.y + 56.f));
    ImGui::SetCursorScreenPos(timelineHit.Min);
    ImGui::InvisibleButton("DDDVR_BAR_TIMELINE", timelineHit.GetSize());
    const bool timelineHovered = ImGui::IsItemHovered();
    if (timelineHovered) setTooltip("Перемотка", timelineHit);
    auto progressFromMouse = [&]() {
        return clamp01((io.MousePos.x - bar.Min.x) / bar.GetWidth());
    };
    if (timelineHovered && ImGui::IsMouseClicked(0)) {
        timelineDragging_ = true;
        timelinePreviewProgress_ = progressFromMouse();
    }
    if (timelineDragging_ && ImGui::IsMouseDown(0)) {
        timelinePreviewProgress_ = progressFromMouse();
    }
    if (timelineDragging_ && ImGui::IsMouseReleased(0)) {
        timelinePreviewProgress_ = progressFromMouse();
        timelineSeekRequested_ = true;
        requestedTimelineProgressPermille_ = static_cast<int>(std::lround(timelinePreviewProgress_ * 1000.f));
        requestedTimelineProgressPermille_ = std::clamp(requestedTimelineProgressPermille_, 0, 1000);
        timelineDragging_ = false;
    }

    const float topRowMinY = bar.Min.y + 9.f;
    const float topRowMaxY = bar.Min.y + 42.f;
    const ImRect leftTime(ImVec2(bar.Min.x + 28.f, topRowMinY), ImVec2(bar.Min.x + 150.f, topRowMaxY));
    const ImRect rightTime(ImVec2(bar.Max.x - 150.f, topRowMinY), ImVec2(bar.Max.x - 28.f, topRowMaxY));
    drawClippedText(draw, leftTime, position, VrPlayerTheme::TextMuted);
    const ImVec2 durationSize = ImGui::CalcTextSize(durationText);
    draw->AddText(
        ImVec2(
            std::floor(rightTime.Max.x - durationSize.x),
            std::floor(rightTime.GetCenter().y - durationSize.y * 0.5f)
        ),
        VrPlayerTheme::TextMuted,
        durationText
    );

    const std::string title = state_.title.empty() ? "DDD-VR OpenXR Player" : state_.title;
    const ImRect titleRect(ImVec2(bar.Min.x + 165.f, topRowMinY), ImVec2(bar.Max.x - 165.f, topRowMaxY));
    const float titleFontSize = ImGui::GetFontSize() * 0.98f;
    const ImVec2 titleSize = ImGui::GetFont()->CalcTextSizeA(titleFontSize, 100000.f, 0.0f, title.c_str());
    draw->PushClipRect(titleRect.Min, titleRect.Max, true);
    draw->AddText(
        ImGui::GetFont(),
        titleFontSize,
        ImVec2(
            std::floor(titleRect.GetCenter().x - titleSize.x * 0.5f),
            std::floor(titleRect.GetCenter().y - titleSize.y * 0.5f)
        ),
        IM_COL32(248, 250, 255, 224),
        title.c_str()
    );
    draw->PopClipRect();

    const float buttonCenterY = bar.Min.y + 114.f;
    const ImRect playlistButton = centeredRect(bar.Min.x + 76.f, buttonCenterY, 66.f, 54.f);
    const ImRect volumeButton = centeredRect(bar.Min.x + 154.f, buttonCenterY, 66.f, 54.f);
    const ImRect prevButton = centeredRect(centerX - 122.f, buttonCenterY, 70.f, 56.f);
    const ImRect playButton = centeredRect(centerX, buttonCenterY, 82.f, 60.f);
    const ImRect nextButton = centeredRect(centerX + 122.f, buttonCenterY, 70.f, 56.f);
    const ImRect audioButton = centeredRect(bar.Max.x - 320.f, buttonCenterY, 70.f, 56.f);
    const ImRect envButton = centeredRect(bar.Max.x - 240.f, buttonCenterY, 66.f, 54.f);
    const ImRect projectionButton = centeredRect(bar.Max.x - 158.f, buttonCenterY, 72.f, 54.f);
    const ImRect moreButton = centeredRect(bar.Max.x - 76.f, buttonCenterY, 66.f, 54.f);

    if (drawSmallIconButton("DDDVR_BTN_PLAYLIST", playlistButton, IconKind::Playlist, "Плейлист")) {
        pushAction(VrPlayerPanelActionType::TogglePlaylist);
    }
    if (drawSmallIconButton("DDDVR_BTN_VOLUME", volumeButton, state_.muted ? IconKind::Muted : IconKind::Volume, state_.muted ? "Включить звук" : "Звук")) {
        pushAction(VrPlayerPanelActionType::ToggleVolume);
    }
    if (drawSmallIconButton("DDDVR_BTN_AUDIO_SWITCHER", audioButton, IconKind::Microphone, "Озвучка")) {
        pushAction(VrPlayerPanelActionType::SetSettingsTab, static_cast<int>(VrSettingsTab::Audio));
    }
    if (drawSmallIconButton("DDDVR_BTN_PREV", prevButton, IconKind::Previous, "Назад")) {
        seekBackRequested_ = true;
    }
    if (drawSmallIconButton("DDDVR_BTN_PLAY", playButton, state_.playing ? IconKind::Pause : IconKind::Play, state_.playing ? "Пауза" : "Играть")) {
        playPauseRequested_ = true;
    }
    if (drawSmallIconButton("DDDVR_BTN_NEXT", nextButton, IconKind::Next, "Вперёд")) {
        seekForwardRequested_ = true;
    }
    if (drawSmallIconButton("DDDVR_BTN_ENV", envButton, IconKind::Environment, "Среда")) {
        pushAction(VrPlayerPanelActionType::ToggleEnvironment);
    }
    if (drawProjectionButton(projectionButton)) {
        pushAction(VrPlayerPanelActionType::ToggleProjectionMenu);
    }
    if (drawSmallIconButton("DDDVR_BTN_MORE", moreButton, IconKind::More, "Ещё")) {
        pushAction(VrPlayerPanelActionType::ToggleSettings);
    }

    if (timelineHovered || timelineDragging_) {
        const float preview = timelineDragging_ ? timelinePreviewProgress_ : progressFromMouse();
        const float previewX = bar.Min.x + bar.GetWidth() * preview;
        const int64_t previewMs = static_cast<int64_t>(static_cast<float>(durationMs) * preview);
        char previewText[32]{};
        formatTime(previewMs, previewText, sizeof(previewText));
        const ImVec2 previewSize = ImGui::CalcTextSize(previewText);
        const float bubbleHalfWidth = previewSize.x * 0.5f + 16.f;
        const float bubbleX = std::clamp(previewX, bar.Min.x + bubbleHalfWidth, bar.Max.x - bubbleHalfWidth);
        const ImRect bubble(
            ImVec2(bubbleX - bubbleHalfWidth, bar.Min.y - 42.f),
            ImVec2(bubbleX + bubbleHalfWidth, bar.Min.y - 8.f)
        );
        draw->AddLine(ImVec2(previewX, bubble.Max.y), ImVec2(previewX, bar.Max.y - 10.f), IM_COL32(235, 245, 255, 150), 1.8f);
        draw->AddRectFilled(bubble.Min, bubble.Max, IM_COL32(20, 22, 28, 238), 9.f);
        drawCenteredText(draw, bubble, previewText, VrPlayerTheme::TextPrimary);
    }

    draw->AddRectFilled(dragHandle.Min, dragHandle.Max, IM_COL32(40, 42, 48, 182), VrPlayerTheme::DragHandleRounding);
    draw->AddRect(dragHandle.Min, dragHandle.Max, IM_COL32(230, 240, 255, 34), VrPlayerTheme::DragHandleRounding, 0, 1.f);
    for (int i = 0; i < 3; ++i) {
        draw->AddCircleFilled(
            ImVec2(dragHandle.GetCenter().x - 22.f + i * 22.f, dragHandle.GetCenter().y),
            3.4f,
            IM_COL32(215, 224, 240, 112)
        );
    }

    if (!tooltip.empty() && !timelineHovered && !timelineDragging_) {
        const ImVec2 textSize = ImGui::CalcTextSize(tooltip.c_str());
        const float padX = 14.f;
        const ImRect bubble(
            ImVec2(tooltipAnchor.x - textSize.x * 0.5f - padX, tooltipAnchor.y - 42.f),
            ImVec2(tooltipAnchor.x + textSize.x * 0.5f + padX, tooltipAnchor.y - 10.f)
        );
        draw->AddRectFilled(bubble.Min, bubble.Max, IM_COL32(18, 20, 25, 235), 9.f);
        draw->AddRect(bubble.Min, bubble.Max, IM_COL32(230, 240, 255, 32), 9.f, 0, 1.f);
        drawCenteredText(draw, bubble, tooltip.c_str(), VrPlayerTheme::TextPrimary);
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

bool VrPlayerPanel::consumeTimelineSeekRequested(int* outProgressPermille) {
    const bool value = timelineSeekRequested_;
    if (value && outProgressPermille != nullptr) {
        *outProgressPermille = requestedTimelineProgressPermille_;
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

bool VrPlayerPanel::consumeAction(VrPlayerPanelAction* outAction) {
    if (outAction == nullptr || pendingActions_.empty()) return false;
    *outAction = pendingActions_.front();
    pendingActions_.erase(pendingActions_.begin());
    return true;
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
