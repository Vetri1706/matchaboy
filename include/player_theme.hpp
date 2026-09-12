#pragma once
#include <array>

// Shared player identity. Native drawing backends adapt these normalized
// RGB values without choosing independent platform palettes.
namespace matcha::theme {
struct Color { double r, g, b; };
inline constexpr Color foreground{0.85, 0.90, 0.94}, muted{0.48, 0.60, 0.67};
inline constexpr Color cyan{0.25, 0.85, 0.94}, green{0.45, 0.90, 0.60}, orange{1.0, 0.65, 0.32};
inline constexpr std::array<Color, 4> shades{{{0.80,0.87,0.61},{0.53,0.65,0.40},{0.29,0.43,0.31},{0.10,0.23,0.22}}};
inline constexpr std::array<Color, 4> grayscale{{{1,1,1},{2.0/3,2.0/3,2.0/3},{1.0/3,1.0/3,1.0/3},{0,0,0}}};
}
