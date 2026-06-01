#pragma once

#include "../third/imgui/imgui.h"

namespace VrPlayerTheme {
constexpr float MainBarWidth = 1080.0f;
constexpr float MainBarHeight = 140.0f;
constexpr float MainBarRounding = 34.0f;

constexpr float ModalWidth = 560.0f;
constexpr float ModalMinHeight = 300.0f;
constexpr float ModalMaxHeight = 330.0f;
constexpr float ModalRounding = 24.0f;

constexpr float DragHandleWidth = 170.0f;
constexpr float DragHandleHeight = 22.0f;
constexpr float DragHandleRounding = 11.0f;

const ImU32 BarBg = IM_COL32(18, 20, 25, 218);
const ImU32 BarBorder = IM_COL32(230, 240, 255, 38);
const ImU32 ProgressFill = IM_COL32(88, 122, 184, 132);
const ImU32 ProgressEdge = IM_COL32(225, 238, 255, 180);

const ImU32 ModalBg = IM_COL32(35, 35, 38, 234);
const ImU32 RowBg = IM_COL32(58, 58, 62, 170);
const ImU32 RowHover = IM_COL32(76, 84, 105, 210);
const ImU32 TextPrimary = IM_COL32(248, 250, 255, 248);
const ImU32 TextMuted = IM_COL32(221, 228, 242, 184);
const ImU32 AccentBlue = IM_COL32(90, 145, 255, 235);
}
