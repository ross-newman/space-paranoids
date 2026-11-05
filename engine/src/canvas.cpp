#include "softrender/canvas.h"

#include <algorithm>
#include <cmath>

#include "softrender/parallel.h"

namespace softrender {

Rgb SampleBilinear(const Image& tex, float u, float v) {
  const float fx = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(tex.width() - 1);
  const float fy = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(tex.height() - 1);
  const std::uint32_t x0 = static_cast<std::uint32_t>(fx);
  const std::uint32_t y0 = static_cast<std::uint32_t>(fy);
  const std::uint32_t x1 = std::min(x0 + 1, tex.width() - 1);
  const std::uint32_t y1 = std::min(y0 + 1, tex.height() - 1);
  const float tx = fx - static_cast<float>(x0);
  const float ty = fy - static_cast<float>(y0);
  const auto at = [&tex](std::uint32_t x, std::uint32_t y, std::size_t k) {
    return static_cast<float>(tex.data()[(static_cast<std::size_t>(y) * tex.width() + x) * 3 + k]);
  };
  std::uint8_t out[3];
  for (std::size_t k = 0; k < 3; ++k) {
    const float top = at(x0, y0, k) + (at(x1, y0, k) - at(x0, y0, k)) * tx;
    const float bottom = at(x0, y1, k) + (at(x1, y1, k) - at(x0, y1, k)) * tx;
    out[k] = static_cast<std::uint8_t>(top + (bottom - top) * ty + 0.5f);
  }
  return {out[0], out[1], out[2]};
}

Canvas::Canvas(Image& image)
    : image_(image),
      width_(static_cast<int>(image.width())),
      height_(static_cast<int>(image.height())),
      clip_x0_(0),
      clip_y0_(0),
      clip_x1_(width_),
      clip_y1_(height_),
      depth_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0.0f) {}

void Canvas::Clear(Rgb colour) {
  image_.Clear(colour);
  ResetDepth();
}

void Canvas::ResetDepth() { std::fill(depth_.begin(), depth_.end(), 0.0f); }

void Canvas::SetClip(int x0, int y0, int x1, int y1) {
  clip_x0_ = std::clamp(x0, 0, width_);
  clip_y0_ = std::clamp(y0, 0, height_);
  clip_x1_ = std::clamp(x1, clip_x0_, width_);
  clip_y1_ = std::clamp(y1, clip_y0_, height_);
}

void Canvas::Add(int x, int y, float r, float g, float b) {
  if (!InClip(x, y)) {
    return;
  }
  std::uint8_t* p = image_.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + x) * 3;
  p[0] = static_cast<std::uint8_t>(std::min(255.0f, p[0] + r));
  p[1] = static_cast<std::uint8_t>(std::min(255.0f, p[1] + g));
  p[2] = static_cast<std::uint8_t>(std::min(255.0f, p[2] + b));
}

void Canvas::Rect(int x, int y, int w, int h, Rgb c) {
  for (int j = std::max(y, clip_y0_); j < std::min(y + h, clip_y1_); ++j) {
    for (int i = std::max(x, clip_x0_); i < std::min(x + w, clip_x1_); ++i) {
      image_.Set(i, j, c);
    }
  }
}

void Canvas::RectOutline(int x, int y, int w, int h, Rgb c) {
  const float left = static_cast<float>(x);
  const float top = static_cast<float>(y);
  const float right = static_cast<float>(x + w - 1);
  const float bottom = static_cast<float>(y + h - 1);
  Line(left, top, right, top, c);
  Line(left, bottom, right, bottom, c);
  Line(left, top, left, bottom, c);
  Line(right, top, right, bottom, c);
}

bool Canvas::ClipLine(float x0, float y0, float x1, float y1, float* t0, float* t1) const {
  // Liang-Barsky clipping
  *t0 = 0.0f;
  *t1 = 1.0f;
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float p[4] = {-dx, dx, -dy, dy};
  const float q[4] = {x0 - static_cast<float>(clip_x0_), static_cast<float>(clip_x1_ - 1) - x0, y0 - static_cast<float>(clip_y0_),
                      static_cast<float>(clip_y1_ - 1) - y0};
  for (int i = 0; i < 4; ++i) {
    if (p[i] == 0.0f) {
      if (q[i] < 0.0f) {
        return false;
      }
    } else {
      const float t = q[i] / p[i];
      if (p[i] < 0.0f) {
        *t0 = std::max(*t0, t);
      } else {
        *t1 = std::min(*t1, t);
      }
    }
  }
  return *t0 <= *t1;
}

void Canvas::Stamp(int x, int y, float inv, bool depth_test, Rgb c, bool additive) {
  const int first = -(line_width_ - 1) / 2;
  const float share = 1.0f / static_cast<float>(line_width_);
  for (int j = first; j < first + line_width_; ++j) {
    for (int i = first; i < first + line_width_; ++i) {
      if (!InClip(x + i, y + j) || (depth_test && inv < depth(x + i, y + j))) {
        continue;
      }
      if (additive) {
        Add(x + i, y + j, c.r * share, c.g * share, c.b * share);
      } else {
        image_.Set(x + i, y + j, c);
      }
    }
  }
}

void Canvas::Line(float x0, float y0, float x1, float y1, Rgb c, bool additive) {
  float t0 = 0.0f;
  float t1 = 1.0f;
  if (!ClipLine(x0, y0, x1, y1, &t0, &t1)) {
    return;
  }
  const float ax = x0 + t0 * (x1 - x0);
  const float ay = y0 + t0 * (y1 - y0);
  const float bx = x0 + t1 * (x1 - x0);
  const float by = y0 + t1 * (y1 - y0);
  const int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::fabs(bx - ax), std::fabs(by - ay)))));
  for (int i = 0; i <= steps; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(steps);
    Stamp(static_cast<int>(std::lround(ax + (bx - ax) * t)), static_cast<int>(std::lround(ay + (by - ay) * t)), 0.0f, false, c, additive);
  }
}

void Canvas::DepthLine(float x0, float y0, float inv0, float x1, float y1, float inv1, Rgb c, bool additive) {
  float t0 = 0.0f;
  float t1 = 1.0f;
  if (!ClipLine(x0, y0, x1, y1, &t0, &t1)) {
    return;
  }
  const float ax = x0 + t0 * (x1 - x0);
  const float ay = y0 + t0 * (y1 - y0);
  const float bx = x0 + t1 * (x1 - x0);
  const float by = y0 + t1 * (y1 - y0);
  // One over the depth is linear across the picture
  const float inv_a = inv0 + t0 * (inv1 - inv0);
  const float inv_b = inv0 + t1 * (inv1 - inv0);
  const int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::fabs(bx - ax), std::fabs(by - ay)))));
  for (int i = 0; i <= steps; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(steps);
    Stamp(static_cast<int>(std::lround(ax + (bx - ax) * t)), static_cast<int>(std::lround(ay + (by - ay) * t)), inv_a + (inv_b - inv_a) * t, true, c,
          additive);
  }
}

void Canvas::Ellipse(float cx, float cy, float rx, float ry, Rgb c) {
  const int n = std::max(24, static_cast<int>(std::max(rx, ry)));
  float px = cx + rx;
  float py = cy;
  for (int i = 1; i <= n; ++i) {
    const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(n);
    const float x = cx + rx * std::cos(a);
    const float y = cy + ry * std::sin(a);
    Line(px, py, x, y, c);
    px = x;
    py = y;
  }
}

void Canvas::Disc(float cx, float cy, float r, Rgb c) {
  for (int y = static_cast<int>(std::floor(cy - r)); y <= static_cast<int>(std::ceil(cy + r)); ++y) {
    for (int x = static_cast<int>(std::floor(cx - r)); x <= static_cast<int>(std::ceil(cx + r)); ++x) {
      const float dx = static_cast<float>(x) + 0.5f - cx;
      const float dy = static_cast<float>(y) + 0.5f - cy;
      if (dx * dx + dy * dy <= r * r) {
        Set(x, y, c);
      }
    }
  }
}

void Canvas::Glow(float cx, float cy, float r, float r_colour, float g_colour, float b_colour, float strength) {
  for (int y = static_cast<int>(std::floor(cy - r)); y <= static_cast<int>(std::ceil(cy + r)); ++y) {
    for (int x = static_cast<int>(std::floor(cx - r)); x <= static_cast<int>(std::ceil(cx + r)); ++x) {
      const float d = std::hypot(static_cast<float>(x) + 0.5f - cx, static_cast<float>(y) + 0.5f - cy) / r;
      if (d < 1.0f) {
        const float k = (1.0f - d) * (1.0f - d) * strength;
        Add(x, y, r_colour * k, g_colour * k, b_colour * k);
      }
    }
  }
}

bool Canvas::Bounds(const float (&p)[3][2], int* min_x, int* max_x, int* min_y, int* max_y) const {
  // A corner close to the near plane can project far off the picture, further than an int reaches, so the limits are
  // applied before converting
  const float low_x = std::floor(std::min({p[0][0], p[1][0], p[2][0]}));
  const float high_x = std::ceil(std::max({p[0][0], p[1][0], p[2][0]}));
  const float low_y = std::floor(std::min({p[0][1], p[1][1], p[2][1]}));
  const float high_y = std::ceil(std::max({p[0][1], p[1][1], p[2][1]}));
  if (clip_x0_ >= clip_x1_ || clip_y0_ >= clip_y1_ || high_x < static_cast<float>(clip_x0_) || low_x > static_cast<float>(clip_x1_ - 1) ||
      high_y < static_cast<float>(clip_y0_) || low_y > static_cast<float>(clip_y1_ - 1)) {
    return false;
  }
  *min_x = low_x > static_cast<float>(clip_x0_) ? static_cast<int>(low_x) : clip_x0_;
  *max_x = high_x < static_cast<float>(clip_x1_ - 1) ? static_cast<int>(high_x) : clip_x1_ - 1;
  *min_y = low_y > static_cast<float>(clip_y0_) ? static_cast<int>(low_y) : clip_y0_;
  *max_y = high_y < static_cast<float>(clip_y1_ - 1) ? static_cast<int>(high_y) : clip_y1_ - 1;
  return true;
}

void Canvas::Triangle(const float (&p)[3][2], const float (&inv)[3], Rgb colour, std::uint8_t edges) {
  const float area = (p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[1][1] - p[0][1]) * (p[2][0] - p[0][0]);
  if (std::fabs(area) < 1e-4f) {
    return;
  }
  int min_x = 0;
  int max_x = 0;
  int min_y = 0;
  int max_y = 0;
  if (!Bounds(p, &min_x, &max_x, &min_y, &max_y)) {
    return;
  }
  const float opposite[3] = {std::hypot(p[2][0] - p[1][0], p[2][1] - p[1][1]), std::hypot(p[0][0] - p[2][0], p[0][1] - p[2][1]),
                             std::hypot(p[1][0] - p[0][0], p[1][1] - p[0][1])};
  const Rgb edge = Scale(colour, outline_.shade);
  const float edge_width = outline_.width;
  const auto fill_rows = [&](int row_begin, int row_end) {
    for (int y = row_begin; y < row_end; ++y) {
      for (int x = min_x; x <= max_x; ++x) {
        const float px = static_cast<float>(x) + 0.5f;
        const float py = static_cast<float>(y) + 0.5f;
        const float l0 = ((p[1][0] - px) * (p[2][1] - py) - (p[1][1] - py) * (p[2][0] - px)) / area;
        const float l1 = ((p[2][0] - px) * (p[0][1] - py) - (p[2][1] - py) * (p[0][0] - px)) / area;
        const float l2 = 1.0f - l0 - l1;
        if (l0 < 0.0f || l1 < 0.0f || l2 < 0.0f) {
          continue;
        }
        const float z = l0 * inv[0] + l1 * inv[1] + l2 * inv[2];
        float& stored = depth(x, y);
        if (z <= stored) {
          continue;
        }
        stored = z;
        Rgb out = colour;
        if (edges != 0) {
          const float abs_area = std::fabs(area);
          const float d[3] = {l0 * abs_area / opposite[0], l1 * abs_area / opposite[1], l2 * abs_area / opposite[2]};
          for (std::size_t j = 0; j < 3; ++j) {
            if (((edges >> j) & 1U) != 0U && d[(j + 2) % 3] < edge_width) {
              out = edge;
            }
          }
        }
        image_.Set(x, y, out);
      }
    }
  };
  // A big triangle is filled by several threads, each with its own rows; a small one is not worth the wake-up
  if (static_cast<long long>(max_x - min_x + 1) * (max_y - min_y + 1) >= 60000) {
    ParallelRows(min_y, max_y + 1, fill_rows, 24);
  } else {
    fill_rows(min_y, max_y + 1);
  }
}

void Canvas::TexturedTriangle(const float (&p)[3][2], const float (&inv)[3], const float (&uv)[3][2], const Image& tex, float minify) {
  const float area = (p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[1][1] - p[0][1]) * (p[2][0] - p[0][0]);
  if (std::fabs(area) < 1e-4f || tex.width() == 0 || tex.height() == 0) {
    return;
  }
  int min_x = 0;
  int max_x = 0;
  int min_y = 0;
  int max_y = 0;
  if (!Bounds(p, &min_x, &max_x, &min_y, &max_y)) {
    return;
  }
  const float du = 0.35f * std::max(1.0f, minify) / static_cast<float>(tex.width());
  const float dv = 0.35f * std::max(1.0f, minify) / static_cast<float>(tex.height());
  const auto fill_rows = [&](int row_begin, int row_end) {
    for (int y = row_begin; y < row_end; ++y) {
      for (int x = min_x; x <= max_x; ++x) {
        const float px = static_cast<float>(x) + 0.5f;
        const float py = static_cast<float>(y) + 0.5f;
        const float l0 = ((p[1][0] - px) * (p[2][1] - py) - (p[1][1] - py) * (p[2][0] - px)) / area;
        const float l1 = ((p[2][0] - px) * (p[0][1] - py) - (p[2][1] - py) * (p[0][0] - px)) / area;
        const float l2 = 1.0f - l0 - l1;
        if (l0 < 0.0f || l1 < 0.0f || l2 < 0.0f) {
          continue;
        }
        const float z = l0 * inv[0] + l1 * inv[1] + l2 * inv[2];
        float& stored = depth(x, y);
        if (z <= stored) {
          continue;
        }
        stored = z;
        const float u = (l0 * uv[0][0] * inv[0] + l1 * uv[1][0] * inv[1] + l2 * uv[2][0] * inv[2]) / z;
        const float v = (l0 * uv[0][1] * inv[0] + l1 * uv[1][1] * inv[1] + l2 * uv[2][1] * inv[2]) / z;
        Rgb c;
        if (minify > 1.4f) {
          const Rgb s0 = SampleBilinear(tex, u - du, v - dv);
          const Rgb s1 = SampleBilinear(tex, u + du, v - dv);
          const Rgb s2 = SampleBilinear(tex, u - du, v + dv);
          const Rgb s3 = SampleBilinear(tex, u + du, v + dv);
          c = {static_cast<std::uint8_t>((s0.r + s1.r + s2.r + s3.r) / 4), static_cast<std::uint8_t>((s0.g + s1.g + s2.g + s3.g) / 4),
               static_cast<std::uint8_t>((s0.b + s1.b + s2.b + s3.b) / 4)};
        } else {
          c = SampleBilinear(tex, u, v);
        }
        image_.Set(x, y, c);
      }
    }
  };
  if (static_cast<long long>(max_x - min_x + 1) * (max_y - min_y + 1) >= 30000) {
    ParallelRows(min_y, max_y + 1, fill_rows, 24);
  } else {
    fill_rows(min_y, max_y + 1);
  }
}

}  // namespace softrender
