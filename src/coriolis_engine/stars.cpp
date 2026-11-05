#include <algorithm>
#include <array>

#include "coriolis/hash.h"
#include "coriolis/renderer.h"

namespace coriolis {

void DrawStars(Image& image, std::uint32_t frame) {
  const std::uint32_t width = image.width();
  const std::uint32_t height = image.height();
  if (width == 0 || height == 0) {
    return;
  }
  const std::uint32_t count = std::max<std::uint32_t>(80, (width * height) / 2000);
  const bool crosses = std::min(width, height) >= 240;
  const std::array<float, 3> layer_brightness{{0.45f, 0.70f, 1.0f}};
  const std::array<std::uint32_t, 3> layer_speed{{1, 2, 4}};  // quarter pixels per frame

  auto plot = [&](std::int64_t x, std::int64_t y, const std::array<float, 3>& colour, float level) {
    if (x < 0 || y < 0 || x >= static_cast<std::int64_t>(width) || y >= static_cast<std::int64_t>(height)) {
      return;
    }
    std::uint8_t* pixel = image.data() + (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 3;
    for (std::size_t c = 0; c < 3; ++c) {
      const auto value = static_cast<std::uint8_t>(std::min(255.0f, colour[c] * level));
      pixel[c] = std::max(pixel[c], value);
    }
  };

  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint32_t h = Hash32(i * 2654435761U + 0x9E3779B9U);
    const std::uint32_t layer = h % 3;
    const std::int64_t x0 = (h >> 8) % width;
    const std::int64_t y = Hash32(h ^ 0x85ebca6bU) % height;
    const std::int64_t drift = static_cast<std::int64_t>((static_cast<std::uint64_t>(frame) * layer_speed[layer]) / 4);
    const std::int64_t x = ((x0 - drift) % static_cast<std::int64_t>(width) + static_cast<std::int64_t>(width)) %
                           static_cast<std::int64_t>(width);

    std::array<float, 3> colour{255.0f, 255.0f, 255.0f};
    switch ((h >> 4) % 6) {
      case 0:
        colour = {190.0f, 210.0f, 255.0f};  // blue white
        break;
      case 1:
        colour = {255.0f, 236.0f, 200.0f};  // warm
        break;
      default:
        break;
    }

    // A gentle twinkle
    float level = layer_brightness[layer];
    if (((frame / 6) + i) % 9 == 0) {
      level *= 0.6f;
    }
    plot(x, y, colour, level);
    if (crosses && layer == 2) {
      plot(x - 1, y, colour, level * 0.45f);
      plot(x + 1, y, colour, level * 0.45f);
      plot(x, y - 1, colour, level * 0.45f);
      plot(x, y + 1, colour, level * 0.45f);
    }
  }
}

}  // namespace coriolis
