#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <vector>

#include "coriolis/renderer.h"

namespace coriolis {

namespace {

// The font is made of strokes, not pixels: each character is a few polylines on a grid 4 units wide and 6 high. They are
// drawn as smooth, anti-aliased lines with round ends, so the text is clean at any size and is not blocky.

struct Pt {
  float x;
  float y;
};
using Stroke = std::vector<Pt>;
using Glyph = std::vector<Stroke>;

/// Points along an ellipse, with angles in degrees from the +x axis towards +y (down the page)
Stroke Arc(float cx, float cy, float rx, float ry, float from, float to) {
  Stroke s;
  const int steps = std::max(4, static_cast<int>(std::fabs(to - from) / 10.0f));
  for (int i = 0; i <= steps; ++i) {
    const float a = (from + (to - from) * static_cast<float>(i) / static_cast<float>(steps)) * kPi / 180.0f;
    s.push_back({cx + rx * std::cos(a), cy + ry * std::sin(a)});
  }
  return s;
}

/// Join several strokes end to end into one
Stroke Cat(std::initializer_list<Stroke> parts) {
  Stroke s;
  for (const Stroke& p : parts) {
    s.insert(s.end(), p.begin(), p.end());
  }
  return s;
}

/// A dot is a stroke of no length: it draws as a round end
Stroke Dot(float x, float y) { return {{x, y}}; }

std::array<Glyph, 128> BuildFont() {
  std::array<Glyph, 128> g;
  const auto set = [&g](char c, Glyph glyph) { g[static_cast<std::size_t>(c)] = std::move(glyph); };

  set('A', {{{0, 6}, {2, 0}, {4, 6}}, {{0.8f, 3.9f}, {3.2f, 3.9f}}});
  set('B', {{{0, 0}, {0, 6}},
            Cat({{{0, 0}, {2.4f, 0}}, Arc(2.4f, 1.5f, 1.6f, 1.5f, -90, 90), {{0, 3}}}),
            Cat({{{0, 3}, {2.6f, 3}}, Arc(2.6f, 4.5f, 1.6f, 1.5f, -90, 90), {{0, 6}}})});
  set('C', {Arc(2, 3, 2, 3, -45, -315)});
  set('D', {{{0, 0}, {0, 6}}, Cat({{{0, 0}, {1.8f, 0}}, Arc(1.8f, 3, 2.2f, 3, -90, 90), {{0, 6}}})});
  set('E', {{{4, 0}, {0, 0}, {0, 6}, {4, 6}}, {{0, 3}, {3, 3}}});
  set('F', {{{4, 0}, {0, 0}, {0, 6}}, {{0, 3}, {3, 3}}});
  set('G', {Cat({Arc(2, 3, 2, 3, -45, -360), {{2.2f, 3}}})});
  set('H', {{{0, 0}, {0, 6}}, {{4, 0}, {4, 6}}, {{0, 3}, {4, 3}}});
  set('I', {{{1, 0}, {3, 0}}, {{2, 0}, {2, 6}}, {{1, 6}, {3, 6}}});
  set('J', {Cat({{{4, 0}, {4, 4}}, Arc(2, 4, 2, 2, 0, 180)})});
  set('K', {{{0, 0}, {0, 6}}, {{4, 0}, {0.2f, 3.5f}}, {{1.3f, 2.5f}, {4, 6}}});
  set('L', {{{0, 0}, {0, 6}, {4, 6}}});
  set('M', {{{0, 6}, {0, 0}, {2, 3.6f}, {4, 0}, {4, 6}}});
  set('N', {{{0, 6}, {0, 0}, {4, 6}, {4, 0}}});
  set('O', {Arc(2, 3, 2, 3, 0, 360)});
  set('P', {{{0, 0}, {0, 6}}, Cat({{{0, 0}, {2.4f, 0}}, Arc(2.4f, 1.7f, 1.6f, 1.7f, -90, 90), {{0, 3.4f}}})});
  set('Q', {Arc(2, 3, 2, 3, 0, 360), {{2.4f, 4.4f}, {4, 6.2f}}});
  set('R', {{{0, 0}, {0, 6}},
            Cat({{{0, 0}, {2.4f, 0}}, Arc(2.4f, 1.7f, 1.6f, 1.7f, -90, 90), {{0, 3.4f}}}),
            {{1.8f, 3.4f}, {4, 6}}});
  set('S', {Cat({Arc(2, 1.5f, 1.8f, 1.5f, -20, -270), Arc(2, 4.5f, 1.9f, 1.5f, -90, 160)})});
  set('T', {{{0, 0}, {4, 0}}, {{2, 0}, {2, 6}}});
  set('U', {Cat({{{0, 0}, {0, 4}}, Arc(2, 4, 2, 2, 180, 0), {{4, 0}}})});
  set('V', {{{0, 0}, {2, 6}, {4, 0}}});
  set('W', {{{0, 0}, {1, 6}, {2, 2.4f}, {3, 6}, {4, 0}}});
  set('X', {{{0, 0}, {4, 6}}, {{4, 0}, {0, 6}}});
  set('Y', {{{0, 0}, {2, 3}, {4, 0}}, {{2, 3}, {2, 6}}});
  set('Z', {{{0, 0}, {4, 0}, {0, 6}, {4, 6}}});

  set('0', {Arc(2, 3, 1.8f, 3, 0, 360), {{1.2f, 4.6f}, {2.8f, 1.4f}}});
  set('1', {{{0.8f, 1.2f}, {2.2f, 0}, {2.2f, 6}}, {{0.8f, 6}, {3.6f, 6}}});
  set('2', {Cat({Arc(2, 1.7f, 1.9f, 1.7f, -170, 20), {{0, 6}, {4, 6}}})});
  set('3', {Cat({Arc(2, 1.5f, 1.8f, 1.5f, -150, 90), Arc(2, 4.5f, 2, 1.5f, -90, 150)})});
  set('4', {{{3, 6}, {3, 0}, {0, 4.2f}, {4, 4.2f}}});
  set('5', {Cat({{{3.8f, 0}, {0.6f, 0}, {0.4f, 2.7f}}, Arc(2, 4.1f, 2, 1.9f, -125, 150)})});
  set('6', {{{3.2f, 0.2f}, {1.2f, 1.5f}, {0.1f, 3.9f}}, Arc(2, 3.9f, 1.9f, 2.1f, 0, 360)});
  set('7', {{{0, 0}, {4, 0}, {1.5f, 6}}});
  set('8', {Arc(2, 1.5f, 1.7f, 1.5f, 0, 360), Arc(2, 4.5f, 2, 1.5f, 0, 360)});
  set('9', {Arc(2, 2.1f, 1.9f, 2.1f, 0, 360), {{3.9f, 2.1f}, {3.6f, 4.4f}, {2.3f, 5.9f}, {0.8f, 5.8f}}});

  set('.', {Dot(2, 5.8f)});
  set(',', {{{2.2f, 5.5f}, {1.6f, 6.8f}}});
  set(':', {Dot(2, 1.9f), Dot(2, 5.8f)});
  set('!', {{{2, 0}, {2, 4}}, Dot(2, 5.8f)});
  set('(', {Arc(3.6f, 3, 2.4f, 3.4f, 125, 235)});
  set(')', {Arc(0.4f, 3, 2.4f, 3.4f, -55, 55)});
  set('/', {{{4, 0}, {0, 6}}});
  set('%', {{{3.8f, 0.2f}, {0.2f, 5.8f}}, Arc(0.9f, 1.2f, 0.8f, 0.8f, 0, 360), Arc(3.1f, 4.8f, 0.8f, 0.8f, 0, 360)});
  set('+', {{{2, 1.5f}, {2, 4.5f}}, {{0.5f, 3}, {3.5f, 3}}});
  set('?', {Cat({Arc(2, 1.7f, 1.8f, 1.7f, -160, 50), {{2, 3.6f}, {2, 4.3f}}}), Dot(2, 5.8f)});
  set('=', {{{0.3f, 2.2f}, {3.7f, 2.2f}}, {{0.3f, 3.9f}, {3.7f, 3.9f}}});
  set('>', {{{0.5f, 0.8f}, {3.5f, 3}, {0.5f, 5.2f}}});
  set('<', {{{3.5f, 0.8f}, {0.5f, 3}, {3.5f, 5.2f}}});
  set('\'', {{{2, 0}, {2, 1.6f}}});
  set('-', {{{0.5f, 3}, {3.5f, 3}}});
  set('_', {{{0, 6.3f}, {4, 6.3f}}});
  return g;
}

const std::array<Glyph, 128>& Font() {
  static const std::array<Glyph, 128> font = BuildFont();
  return font;
}

/// Distance from a point to the segment from (ax, ay) to (bx, by)
float SegmentDistance(float px, float py, float ax, float ay, float bx, float by) {
  const float dx = bx - ax;
  const float dy = by - ay;
  const float length2 = dx * dx + dy * dy;
  float t = length2 > 1e-9f ? ((px - ax) * dx + (py - ay) * dy) / length2 : 0.0f;
  t = std::clamp(t, 0.0f, 1.0f);
  return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

}  // namespace

float UiScale(const Image& image) {
  const float by_height = static_cast<float>(image.height()) / 240.0f;
  const float by_width = static_cast<float>(image.width()) / 400.0f;
  return std::max(1.0f, std::min(by_height, by_width));
}

void DrawText(Image& image, const std::string& text, std::int64_t x, std::int64_t y, float scale, Rgb colour) {
  const float s = std::max(1.0f, scale);
  const float half = std::max(1.1f, 0.7f * s) / 2.0f;
  const std::int64_t width = static_cast<std::int64_t>(image.width());
  const std::int64_t height = static_cast<std::int64_t>(image.height());
  const std::array<Glyph, 128>& font = Font();

  // The pixels a character can touch: its cell, and a margin of two pixels all round for the thickness of the strokes
  // (one more across, for the fraction of a pixel a character may start at)
  const std::int64_t cell_w = static_cast<std::int64_t>(std::ceil(6.0f * s)) + 5;
  const std::int64_t cell_h = static_cast<std::int64_t>(std::ceil(7.0f * s)) + 4;
  std::vector<float> coverage(static_cast<std::size_t>(cell_w * cell_h));

  // Characters advance by a fractional number of pixels, so a scale that is not a whole number still spaces them evenly
  double pen = static_cast<double>(x);
  for (const char raw : text) {
    const std::int64_t x0 = static_cast<std::int64_t>(std::floor(pen));
    const float shift = static_cast<float>(pen - static_cast<double>(x0));
    pen += 6.0 * static_cast<double>(s);
    const unsigned char upper = static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(raw)));
    const Glyph* glyph = upper < 128 ? &font[upper] : nullptr;
    if (glyph != nullptr && !glyph->empty() && x0 + cell_w > 0 && x0 < width && y + cell_h > 0 && y < height) {
      std::fill(coverage.begin(), coverage.end(), 0.0f);
      // Work in the cell's own pixels: the cell's top left is two pixels out from where the character starts
      for (const Stroke& stroke : *glyph) {
        for (std::size_t i = 0; i < stroke.size(); ++i) {
          const Pt& p0 = stroke[i];
          const Pt& p1 = stroke[std::min(i + 1, stroke.size() - 1)];
          const float ax = 2.0f + shift + (0.5f + p0.x) * s;
          const float ay = 2.0f + (0.5f + p0.y) * s;
          const float bx = 2.0f + shift + (0.5f + p1.x) * s;
          const float by = 2.0f + (0.5f + p1.y) * s;
          const std::int64_t min_x = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(std::min(ax, bx) - half - 1.0f)));
          const std::int64_t max_x = std::min<std::int64_t>(cell_w - 1, static_cast<std::int64_t>(std::ceil(std::max(ax, bx) + half + 1.0f)));
          const std::int64_t min_y = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(std::min(ay, by) - half - 1.0f)));
          const std::int64_t max_y = std::min<std::int64_t>(cell_h - 1, static_cast<std::int64_t>(std::ceil(std::max(ay, by) + half + 1.0f)));
          for (std::int64_t cy = min_y; cy <= max_y; ++cy) {
            for (std::int64_t cx = min_x; cx <= max_x; ++cx) {
              const float d = SegmentDistance(static_cast<float>(cx) + 0.5f, static_cast<float>(cy) + 0.5f, ax, ay, bx, by);
              const float c = std::clamp(half + 0.5f - d, 0.0f, 1.0f);
              float& slot = coverage[static_cast<std::size_t>(cy * cell_w + cx)];
              slot = std::max(slot, c);
            }
          }
        }
      }
      for (std::int64_t cy = 0; cy < cell_h; ++cy) {
        const std::int64_t py = y - 2 + cy;
        if (py < 0 || py >= height) {
          continue;
        }
        for (std::int64_t cx = 0; cx < cell_w; ++cx) {
          const std::int64_t px = x0 - 2 + cx;
          const float c = coverage[static_cast<std::size_t>(cy * cell_w + cx)];
          if (c <= 0.0f || px < 0 || px >= width) {
            continue;
          }
          std::uint8_t* p = image.data() + (static_cast<std::size_t>(py) * static_cast<std::size_t>(width) + static_cast<std::size_t>(px)) * 3;
          p[0] = static_cast<std::uint8_t>(p[0] + (colour.r - p[0]) * c + 0.5f);
          p[1] = static_cast<std::uint8_t>(p[1] + (colour.g - p[1]) * c + 0.5f);
          p[2] = static_cast<std::uint8_t>(p[2] + (colour.b - p[2]) * c + 0.5f);
        }
      }
    }
  }
}

namespace {

/// Lay a colour over a pixel with a coverage from 0 to 1
void Cover(Image& image, std::int64_t x, std::int64_t y, Rgb colour, float c) {
  if (c <= 0.0f || x < 0 || y < 0 || x >= static_cast<std::int64_t>(image.width()) || y >= static_cast<std::int64_t>(image.height())) {
    return;
  }
  std::uint8_t* p = image.data() + (static_cast<std::size_t>(y) * image.width() + static_cast<std::size_t>(x)) * 3;
  p[0] = static_cast<std::uint8_t>(p[0] + (colour.r - p[0]) * c + 0.5f);
  p[1] = static_cast<std::uint8_t>(p[1] + (colour.g - p[1]) * c + 0.5f);
  p[2] = static_cast<std::uint8_t>(p[2] + (colour.b - p[2]) * c + 0.5f);
}

/// Half the width of a stroke at a scale: the same as the strokes of the text
float StrokeHalfWidth(float scale) { return std::max(1.1f, 0.7f * std::max(1.0f, scale)) / 2.0f; }

}  // namespace

void DrawLine(Image& image, float x0, float y0, float x1, float y1, float scale, Rgb colour) {
  const float half = StrokeHalfWidth(scale);
  const std::int64_t min_x = static_cast<std::int64_t>(std::floor(std::min(x0, x1) - half - 1.0f));
  const std::int64_t max_x = static_cast<std::int64_t>(std::ceil(std::max(x0, x1) + half + 1.0f));
  const std::int64_t min_y = static_cast<std::int64_t>(std::floor(std::min(y0, y1) - half - 1.0f));
  const std::int64_t max_y = static_cast<std::int64_t>(std::ceil(std::max(y0, y1) + half + 1.0f));
  for (std::int64_t y = std::max<std::int64_t>(0, min_y); y <= std::min<std::int64_t>(static_cast<std::int64_t>(image.height()) - 1, max_y); ++y) {
    for (std::int64_t x = std::max<std::int64_t>(0, min_x); x <= std::min<std::int64_t>(static_cast<std::int64_t>(image.width()) - 1, max_x); ++x) {
      const float d = SegmentDistance(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, x0, y0, x1, y1);
      Cover(image, x, y, colour, std::clamp(half + 0.5f - d, 0.0f, 1.0f));
    }
  }
}

void DrawCircle(Image& image, float cx, float cy, float radius, float scale, Rgb colour) {
  const float half = StrokeHalfWidth(scale);
  const float reach = radius + half + 1.0f;
  for (std::int64_t y = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(cy - reach)));
       y <= std::min<std::int64_t>(static_cast<std::int64_t>(image.height()) - 1, static_cast<std::int64_t>(std::ceil(cy + reach))); ++y) {
    for (std::int64_t x = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(cx - reach)));
         x <= std::min<std::int64_t>(static_cast<std::int64_t>(image.width()) - 1, static_cast<std::int64_t>(std::ceil(cx + reach))); ++x) {
      const float d = std::fabs(std::hypot(static_cast<float>(x) + 0.5f - cx, static_cast<float>(y) + 0.5f - cy) - radius);
      Cover(image, x, y, colour, std::clamp(half + 0.5f - d, 0.0f, 1.0f));
    }
  }
}

void DrawDisc(Image& image, float cx, float cy, float radius, Rgb colour) {
  const float reach = radius + 1.0f;
  for (std::int64_t y = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(cy - reach)));
       y <= std::min<std::int64_t>(static_cast<std::int64_t>(image.height()) - 1, static_cast<std::int64_t>(std::ceil(cy + reach))); ++y) {
    for (std::int64_t x = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(cx - reach)));
         x <= std::min<std::int64_t>(static_cast<std::int64_t>(image.width()) - 1, static_cast<std::int64_t>(std::ceil(cx + reach))); ++x) {
      const float d = std::hypot(static_cast<float>(x) + 0.5f - cx, static_cast<float>(y) + 0.5f - cy);
      Cover(image, x, y, colour, std::clamp(radius + 0.5f - d, 0.0f, 1.0f));
    }
  }
}

void DrawCaption(Image& image, const std::string& text, Rgb colour) {
  const std::uint32_t scale = std::max<std::uint32_t>(1, image.height() / 180);
  const std::int64_t margin = 4 * scale;
  DrawText(image, text, margin, static_cast<std::int64_t>(image.height()) - 7 * scale - margin, scale, colour);
}

}  // namespace coriolis
