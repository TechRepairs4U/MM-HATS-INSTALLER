#pragma once
#include <algorithm>

namespace sphaira::ui {

struct OptionBoxLayout {
    static constexpr float ButtonHeight = 75;
    static constexpr float Padding = 32;
    static constexpr float ScrollHintHeight = 28;
    float height;
    float text_area_height;
    bool scrollable;
};

inline OptionBoxLayout CalculateOptionBoxLayout(float measured_text_height, bool image) {
    const auto content_height = std::max(measured_text_height, image ? 150.f : 0.f);
    const auto height = std::clamp(content_height + OptionBoxLayout::Padding * 2 +
        OptionBoxLayout::ButtonHeight, 295.f, 600.f);
    auto text_area = height - OptionBoxLayout::ButtonHeight - OptionBoxLayout::Padding * 2;
    const bool scrollable = measured_text_height > text_area;
    if (scrollable) text_area -= OptionBoxLayout::ScrollHintHeight;
    return {height, text_area, scrollable};
}

} // namespace sphaira::ui
