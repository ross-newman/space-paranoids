#ifndef SOFTRENDER_TYPES_H_
#define SOFTRENDER_TYPES_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace softrender {

constexpr float kPi = 3.14159265358979f;

/// A point or direction in 3D. In a model the front points along +z, +y is up and +x is to the right.
struct Vec3 {
  float x;
  float y;
  float z;
};

/// An 8 bit per channel colour
struct Rgb {
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
};

/// A picture of RGB24 pixels, row by row from the top left
class Image {
 public:
  Image(std::uint32_t width, std::uint32_t height)
      : width_(width), height_(height), pixels_(static_cast<std::size_t>(width) * height * 3, 0) {}

  std::uint32_t width() const { return width_; }
  std::uint32_t height() const { return height_; }

  /// The pixels as RGB24, 3 bytes per pixel
  std::uint8_t* data() { return pixels_.data(); }
  const std::uint8_t* data() const { return pixels_.data(); }
  std::size_t size() const { return pixels_.size(); }

  /// Set every pixel
  void Clear(Rgb colour) {
    for (std::size_t i = 0; i < pixels_.size(); i += 3) {
      pixels_[i] = colour.r;
      pixels_[i + 1] = colour.g;
      pixels_[i + 2] = colour.b;
    }
  }

  /// Set a pixel, ignoring positions outside the picture
  void Set(std::int64_t x, std::int64_t y, Rgb colour) {
    if (x < 0 || y < 0 || x >= static_cast<std::int64_t>(width_) || y >= static_cast<std::int64_t>(height_)) {
      return;
    }
    const std::size_t i = (static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(x)) * 3;
    pixels_[i] = colour.r;
    pixels_[i + 1] = colour.g;
    pixels_[i + 2] = colour.b;
  }

  bool operator==(const Image& other) const {
    return width_ == other.width_ && height_ == other.height_ && pixels_ == other.pixels_;
  }

 private:
  std::uint32_t width_;
  std::uint32_t height_;
  std::vector<std::uint8_t> pixels_;
};

/// A colour multiplied by k, stopping at white
inline Rgb Scale(Rgb c, float k) {
  const auto s = [k](std::uint8_t v) { return static_cast<std::uint8_t>(std::clamp(v * k, 0.0f, 255.0f)); };
  return {s(c.r), s(c.g), s(c.b)};
}

/// The colour `t` of the way from a to b
inline Rgb Mix(Rgb a, Rgb b, float t) {
  const auto m = [t](std::uint8_t x, std::uint8_t y) {
    return static_cast<std::uint8_t>(std::clamp(x + (y - x) * t, 0.0f, 255.0f));
  };
  return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b)};
}

}  // namespace softrender

#endif  // SOFTRENDER_TYPES_H_
