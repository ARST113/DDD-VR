#pragma once

#include "../third/imgui/imgui.h"

namespace VrPlayerTheme {
constexpr float MainBarWidth = 1080.0f;
constexpr float MainBarHeight = 146.0f;
constexpr float MainBarRounding = 34.0f;

constexpr float ModalWidth = 560.0f;
constexpr float ModalMinHeight = 300.0f;
constexpr float ModalMaxHeight = 330.0f;
constexpr float ModalRounding = 24.0f;

constexpr float DragHandleWidth = 170.0f;
constexpr float DragHandleHeight = 22.0f;
constexpr float DragHandleRounding = 11.0f;

const ImU32 BarBg = IM_COL32(0, 0, 0, 226);
const ImU32 BarBorder = IM_COL32(245, 248, 255, 44);
const ImU32 ProgressFill = IM_COL32(74, 110, 180, 126);
const ImU32 ProgressEdge = IM_COL32(240, 248, 255, 196);

const ImU32 ModalBg = IM_COL32(0, 0, 0, 236);
const ImU32 RowBg = IM_COL32(10, 10, 12, 184);
const ImU32 RowHover = IM_COL32(36, 48, 72, 220);
const ImU32 TextPrimary = IM_COL32(255, 255, 255, 252);
const ImU32 TextMuted = IM_COL32(232, 238, 248, 196);
const ImU32 AccentBlue = IM_COL32(90, 145, 255, 235);
}
