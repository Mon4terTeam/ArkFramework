#pragma once

#include <algorithm>

#include <imgui.h>

namespace ark::ui {

// Sets the next window's size and position, shrunk and clamped so the window stays
// inside the current display with a small margin.
inline void fitting_window_layout(ImVec2 size, ImVec2 position,
                                  ImGuiCond condition = ImGuiCond_FirstUseEver) {
    constexpr float kSizeMargin = 32.f;
    constexpr float kEdgeMargin = 16.f;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    size.x = (std::min)(size.x, (std::max)(1.f, display.x - kSizeMargin));
    size.y = (std::min)(size.y, (std::max)(1.f, display.y - kSizeMargin));
    position.x = std::clamp(position.x, 0.f, (std::max)(0.f, display.x - size.x - kEdgeMargin));
    position.y = std::clamp(position.y, 0.f, (std::max)(0.f, display.y - size.y - kEdgeMargin));
    ImGui::SetNextWindowSize(size, condition);
    ImGui::SetNextWindowPos(position, condition);
}

} // namespace ark::ui
