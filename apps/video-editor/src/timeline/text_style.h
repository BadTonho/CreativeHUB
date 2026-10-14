#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace timeline {
enum class TextAlignment { Left, Center, Right };
struct TextStyle {
    std::string content = "Text";
    std::string font_family = "Sans Serif";
    double font_size_pixels = 48.0;
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};
    TextAlignment alignment = TextAlignment::Center;
    friend bool operator==(const TextStyle&, const TextStyle&) = default;
};
inline bool isValidTextStyle(const TextStyle& text) noexcept {
    return !text.font_family.empty() && std::isfinite(text.font_size_pixels) &&
        text.font_size_pixels > 0 && text.font_size_pixels <= 512 &&
        (text.alignment == TextAlignment::Left || text.alignment == TextAlignment::Center || text.alignment == TextAlignment::Right);
}
} // namespace timeline
