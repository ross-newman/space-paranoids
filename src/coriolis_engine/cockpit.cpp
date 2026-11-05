#include "coriolis/cockpit.h"

#include "coriolis/parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>

#include "coriolis/hash.h"
#include "coriolis/renderer.h"
#include "coriolis/steering.h"

namespace coriolis {

namespace {

constexpr double kNear = 0.5;

/// Where the pilot's eye is and which way it looks, with the screen it projects on
struct Camera {
  Quat q;
  Vec3d origin;
  double f = 600.0;
  double cx = 0.0;
  double cy = 0.0;
};

Vec3d ToCamera(const Camera& c, const Vec3d& world) { return Rotate(Conjugate(c.q), world - c.origin); }

bool Project(const Camera& c, const Vec3d& p, float* x, float* y) {
  if (p.z < kNear) {
    return false;
  }
  *x = static_cast<float>(c.cx + c.f * p.x / p.z);
  *y = static_cast<float>(c.cy - c.f * p.y / p.z);
  return true;
}

Rgb Scale(Rgb c, float k) {
  const auto s = [k](std::uint8_t v) { return static_cast<std::uint8_t>(std::clamp(v * k, 0.0f, 255.0f)); };
  return {s(c.r), s(c.g), s(c.b)};
}

Rgb Mix(Rgb a, Rgb b, float t) {
  const auto m = [t](std::uint8_t x, std::uint8_t y) {
    return static_cast<std::uint8_t>(std::clamp(x + (y - x) * t, 0.0f, 255.0f));
  };
  return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b)};
}

/// A colour from a picture, smoothly blended between its pixels. u and v run from 0 to 1.
Rgb Bilinear(const Image& tex, float u, float v) {
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

/// Smooth noise in 3D, from a hash on the lattice
float Lattice(int x, int y, int z) {
  const std::uint32_t h = Hash32(static_cast<std::uint32_t>(x) * 73856093U ^ static_cast<std::uint32_t>(y) * 19349663U ^
                                 static_cast<std::uint32_t>(z) * 83492791U);
  return static_cast<float>(h & 0xFFFFU) / 65535.0f;
}

float Noise3(float x, float y, float z) {
  const float fx = std::floor(x);
  const float fy = std::floor(y);
  const float fz = std::floor(z);
  const int ix = static_cast<int>(fx);
  const int iy = static_cast<int>(fy);
  const int iz = static_cast<int>(fz);
  const auto smooth = [](float t) { return t * t * (3.0f - 2.0f * t); };
  const float tx = smooth(x - fx);
  const float ty = smooth(y - fy);
  const float tz = smooth(z - fz);
  const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
  const float c00 = lerp(Lattice(ix, iy, iz), Lattice(ix + 1, iy, iz), tx);
  const float c10 = lerp(Lattice(ix, iy + 1, iz), Lattice(ix + 1, iy + 1, iz), tx);
  const float c01 = lerp(Lattice(ix, iy, iz + 1), Lattice(ix + 1, iy, iz + 1), tx);
  const float c11 = lerp(Lattice(ix, iy + 1, iz + 1), Lattice(ix + 1, iy + 1, iz + 1), tx);
  return lerp(lerp(c00, c10, ty), lerp(c01, c11, ty), tz);
}

Rgb TeamColour(const Entity& e) {
  switch (e.kind) {
    case EntityKind::kStation:
      return {232, 232, 244};
    case EntityKind::kMissile:
      return {255, 80, 255};
    case EntityKind::kCanister:
      return {255, 170, 40};
    case EntityKind::kAsteroid:
      return {140, 128, 112};
    case EntityKind::kEscapePod:
      return {190, 200, 255};
    default:
      break;
  }
  switch (e.team) {
    case Team::kPirate:
      return {255, 70, 60};
    case Team::kAlien:
      return {120, 255, 90};
    case Team::kPolice:
      return {80, 140, 255};
    case Team::kHunter:
      return {255, 140, 60};
    case Team::kTrader:
      return {255, 230, 90};
    default:
      return {210, 210, 210};
  }
}

/// Is it attacking the player? A missile that is hunting the player, or a ship whose pilot has the player as its target
/// and is closing in, shooting or breaking away from a run. Traders, police that are not after the player and the rest
/// do not count.
bool IsThreat(const Entity& e, EntityId player) {
  if (e.kind == EntityKind::kMissile) {
    return e.target == player && e.owner != player;
  }
  if (e.kind != EntityKind::kShip) {
    return false;
  }
  return e.ai.target == player &&
         (e.ai.mode == AiMode::kApproach || e.ai.mode == AiMode::kAttack || e.ai.mode == AiMode::kBreak);
}

/// A drawing surface: the image, a depth buffer for the 3D view, and drawing helpers
class Canvas {
 public:
  /// `with_depth` is false for flat drawing (the console screens), which never tests depth: no buffer is made
  Canvas(Image& image, int view_height, bool with_depth = true)
      : image_(image),
        width_(static_cast<int>(image.width())),
        height_(static_cast<int>(image.height())),
        view_height_(view_height),
        clip_(view_height),
        depth_(with_depth ? static_cast<std::size_t>(width_) * static_cast<std::size_t>(std::max(view_height, 0)) : 0, 0.0f) {}

  int width() const { return width_; }
  int height() const { return height_; }
  int view_height() const { return view_height_; }
  /// Drawing is limited to rows above this
  void set_clip(int rows) { clip_ = std::min(rows, height_); }

  float& depth(int x, int y) { return depth_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + x]; }

  void Set(int x, int y, Rgb c) {
    if (x >= 0 && y >= 0 && x < width_ && y < clip_) {
      image_.Set(x, y, c);
    }
  }

  void Add(int x, int y, float r, float g, float b) {
    if (x < 0 || y < 0 || x >= width_ || y >= clip_) {
      return;
    }
    std::uint8_t* p = image_.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + x) * 3;
    p[0] = static_cast<std::uint8_t>(std::min(255.0f, p[0] + r));
    p[1] = static_cast<std::uint8_t>(std::min(255.0f, p[1] + g));
    p[2] = static_cast<std::uint8_t>(std::min(255.0f, p[2] + b));
  }

  Rgb Get(int x, int y) const {
    const std::uint8_t* p = image_.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + x) * 3;
    return {p[0], p[1], p[2]};
  }

  void Rect(int x, int y, int w, int h, Rgb c) {
    for (int j = y; j < y + h; ++j) {
      for (int i = x; i < x + w; ++i) {
        Set(i, j, c);
      }
    }
  }

  void RectOutline(int x, int y, int w, int h, Rgb c) {
    Line(static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + w - 1), static_cast<float>(y), c);
    Line(static_cast<float>(x), static_cast<float>(y + h - 1), static_cast<float>(x + w - 1), static_cast<float>(y + h - 1), c);
    Line(static_cast<float>(x), static_cast<float>(y), static_cast<float>(x), static_cast<float>(y + h - 1), c);
    Line(static_cast<float>(x + w - 1), static_cast<float>(y), static_cast<float>(x + w - 1), static_cast<float>(y + h - 1), c);
  }

  /// A line, clipped to the drawing area. Additive lines add their colour to what is there, which makes glows.
  void Line(float x0, float y0, float x1, float y1, Rgb c, bool additive = false) {
    // Liang-Barsky clipping
    float t0 = 0.0f;
    float t1 = 1.0f;
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float p[4] = {-dx, dx, -dy, dy};
    const float q[4] = {x0, static_cast<float>(width_ - 1) - x0, y0, static_cast<float>(clip_ - 1) - y0};
    for (int i = 0; i < 4; ++i) {
      if (p[i] == 0.0f) {
        if (q[i] < 0.0f) {
          return;
        }
      } else {
        const float t = q[i] / p[i];
        if (p[i] < 0.0f) {
          t0 = std::max(t0, t);
        } else {
          t1 = std::min(t1, t);
        }
      }
    }
    if (t0 > t1) {
      return;
    }
    const float ax = x0 + t0 * dx;
    const float ay = y0 + t0 * dy;
    const float bx = x0 + t1 * dx;
    const float by = y0 + t1 * dy;
    const int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::fabs(bx - ax), std::fabs(by - ay)))));
    for (int i = 0; i <= steps; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(steps);
      const int x = static_cast<int>(std::lround(ax + (bx - ax) * t));
      const int y = static_cast<int>(std::lround(ay + (by - ay) * t));
      if (additive) {
        Add(x, y, c.r, c.g, c.b);
      } else {
        Set(x, y, c);
      }
    }
  }

  void Ellipse(float cx, float cy, float rx, float ry, Rgb c) {
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

  void Disc(float cx, float cy, float r, Rgb c) {
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

  /// A soft glow: brightest in the middle, falling to nothing at the radius
  void Glow(float cx, float cy, float r, float r_colour, float g_colour, float b_colour, float strength) {
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

  /// A triangle with a depth test. `inv` is one over the depth at each corner. Marked edges are outlined.
  void Triangle(const float (&p)[3][2], const float (&inv)[3], Rgb colour, std::uint8_t edges) {
    const float area = (p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[1][1] - p[0][1]) * (p[2][0] - p[0][0]);
    if (std::fabs(area) < 1e-4f) {
      return;
    }
    const float opposite[3] = {std::hypot(p[2][0] - p[1][0], p[2][1] - p[1][1]), std::hypot(p[0][0] - p[2][0], p[0][1] - p[2][1]),
                               std::hypot(p[1][0] - p[0][0], p[1][1] - p[0][1])};
    const int min_x = std::max(0, static_cast<int>(std::floor(std::min({p[0][0], p[1][0], p[2][0]}))));
    const int max_x = std::min(width_ - 1, static_cast<int>(std::ceil(std::max({p[0][0], p[1][0], p[2][0]}))));
    const int min_y = std::max(0, static_cast<int>(std::floor(std::min({p[0][1], p[1][1], p[2][1]}))));
    const int max_y = std::min(view_height_ - 1, static_cast<int>(std::ceil(std::max({p[0][1], p[1][1], p[2][1]}))));
    const Rgb edge = Scale(colour, 0.35f);
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
            if (((edges >> j) & 1U) != 0U && d[(j + 2) % 3] < 0.9f) {
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

  /// Forget what is in front of what, so something can be drawn over everything (the cockpit)
  void ResetDepth() { std::fill(depth_.begin(), depth_.end(), 0.0f); }

  /// A triangle with a picture on it, correct in perspective. `uv` are the picture's coordinates at the corners. When the
  /// picture is bigger than the triangle on screen (`minify` above 1) several samples are averaged, so it stays smooth.
  void TexturedTriangle(const float (&p)[3][2], const float (&inv)[3], const float (&uv)[3][2], const Image& tex, float minify) {
    const float area = (p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[1][1] - p[0][1]) * (p[2][0] - p[0][0]);
    if (std::fabs(area) < 1e-4f) {
      return;
    }
    const int min_x = std::max(0, static_cast<int>(std::floor(std::min({p[0][0], p[1][0], p[2][0]}))));
    const int max_x = std::min(width_ - 1, static_cast<int>(std::ceil(std::max({p[0][0], p[1][0], p[2][0]}))));
    const int min_y = std::max(0, static_cast<int>(std::floor(std::min({p[0][1], p[1][1], p[2][1]}))));
    const int max_y = std::min(view_height_ - 1, static_cast<int>(std::ceil(std::max({p[0][1], p[1][1], p[2][1]}))));
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
          const Rgb s0 = Bilinear(tex, u - du, v - dv);
          const Rgb s1 = Bilinear(tex, u + du, v - dv);
          const Rgb s2 = Bilinear(tex, u - du, v + dv);
          const Rgb s3 = Bilinear(tex, u + du, v + dv);
          c = {static_cast<std::uint8_t>((s0.r + s1.r + s2.r + s3.r) / 4), static_cast<std::uint8_t>((s0.g + s1.g + s2.g + s3.g) / 4),
               static_cast<std::uint8_t>((s0.b + s1.b + s2.b + s3.b) / 4)};
        } else {
          c = Bilinear(tex, u, v);
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

 private:
  Image& image_;
  int width_;
  int height_;
  int view_height_;
  int clip_;
  std::vector<float> depth_;
};

/// Fill a convex polygon given in camera space, cutting it off at the near plane
void FillPolygon(Canvas& canvas, const Camera& cam, std::vector<Vec3d> poly, Rgb colour, std::uint8_t edges) {
  bool clipped = false;
  std::vector<Vec3d> out;
  for (std::size_t i = 0; i < poly.size(); ++i) {
    const Vec3d& a = poly[i];
    const Vec3d& b = poly[(i + 1) % poly.size()];
    const bool a_in = a.z >= kNear;
    const bool b_in = b.z >= kNear;
    if (a_in) {
      out.push_back(a);
    }
    if (a_in != b_in) {
      const double t = (kNear - a.z) / (b.z - a.z);
      out.push_back(a + (b - a) * t);
      clipped = true;
    }
  }
  if (out.size() < 3) {
    return;
  }
  for (std::size_t i = 1; i + 1 < out.size(); ++i) {
    float p[3][2];
    float inv[3];
    const Vec3d* corner[3] = {&out[0], &out[i], &out[i + 1]};
    bool ok = true;
    for (int k = 0; k < 3; ++k) {
      ok = ok && Project(cam, *corner[k], &p[k][0], &p[k][1]);
      inv[k] = static_cast<float>(1.0 / corner[k]->z);
    }
    if (ok) {
      canvas.Triangle(p, inv, colour, clipped ? 0 : edges);
    }
  }
}

enum class PlanetKind { kWorld, kLava, kBanded };

/// What a planet looks like, all of it chosen from the system seed
struct PlanetStyle {
  PlanetKind kind;
  Rgb land;
  Rgb high;  ///< Mountains and, on a gas giant, the second band colour
  Rgb sea;
  Rgb ice;
  Rgb atmosphere;
  float sea_level;       ///< Height below which there is sea, 0 to 1
  float terrain;         ///< Size of the features: bigger is busier
  float ice_latitude;    ///< |y| above which there is ice, above 1 for none
  float glow_thickness;  ///< Of the atmosphere, as a fraction of the radius; 0 for none
  float glow_strength;
  float ring_inner = 0.0f;  ///< Inner and outer edge of the rings in planet radii; 0 for no rings
  float ring_outer = 0.0f;
  float ring_gap = 0.5f;  ///< Where the main gap lies, from 0 at the inner edge to 1 at the outer
  Rgb ring_colour{200, 185, 160};
  Vec3d ring_axis{0.0, 1.0, 0.0};  ///< Normal of the ring plane, in world space
  const char* name = "";           ///< The kind of world, for captions
};

/// A colour nudged a little in each channel, so no two planets of a type match exactly
Rgb Jitter(Rgb c, std::uint32_t h, float amount) {
  const auto channel = [&](std::uint8_t v, int shift) {
    const float f = 1.0f + amount * (static_cast<float>((h >> shift) & 255U) / 127.5f - 1.0f);
    return static_cast<std::uint8_t>(std::clamp(v * f, 0.0f, 255.0f));
  };
  return {channel(c.r, 0), channel(c.g, 8), channel(c.b, 16)};
}

PlanetStyle StyleFor(std::uint32_t seed) {
  struct Type {
    PlanetKind kind;
    Rgb land, high, sea, ice, atmosphere;
    float sea_level, terrain, ice_latitude;
  };
  static const Type kTypes[] = {
      // Temperate, green and blue
      {PlanetKind::kWorld, {96, 150, 70}, {150, 140, 110}, {30, 70, 150}, {235, 240, 250}, {120, 170, 255}, 0.52f, 3.0f, 0.9f},
      // Desert
      {PlanetKind::kWorld, {196, 150, 90}, {228, 192, 132}, {120, 90, 60}, {240, 230, 215}, {255, 190, 120}, 0.42f, 3.0f, 1.5f},
      // Grey and cratered, like a moon
      {PlanetKind::kWorld, {150, 160, 170}, {196, 200, 206}, {80, 86, 98}, {235, 240, 250}, {170, 190, 220}, 0.50f, 4.0f, 1.5f},
      // Rust red
      {PlanetKind::kWorld, {170, 90, 70}, {212, 142, 102}, {110, 50, 40}, {240, 225, 220}, {232, 150, 110}, 0.48f, 3.0f, 0.93f},
      // Ocean world of islands
      {PlanetKind::kWorld, {70, 130, 90}, {200, 210, 190}, {20, 80, 170}, {240, 245, 255}, {110, 190, 255}, 0.68f, 2.5f, 0.85f},
      // Frozen
      {PlanetKind::kWorld, {196, 212, 230}, {246, 250, 255}, {106, 148, 190}, {250, 252, 255}, {170, 210, 255}, 0.40f, 3.5f, 0.7f},
      // Violet alien jungle
      {PlanetKind::kWorld, {110, 70, 150}, {192, 122, 192}, {40, 30, 96}, {235, 220, 250}, {200, 130, 255}, 0.50f, 3.0f, 0.92f},
      // Toxic yellow-green
      {PlanetKind::kWorld, {170, 180, 60}, {212, 202, 100}, {70, 100, 40}, {235, 240, 200}, {190, 230, 90}, 0.50f, 3.0f, 1.5f},
      // Coral pink
      {PlanetKind::kWorld, {200, 120, 150}, {240, 190, 200}, {90, 50, 110}, {250, 235, 245}, {255, 160, 200}, 0.50f, 3.0f, 0.9f},
      // Dark teal seas under a green-gold land
      {PlanetKind::kWorld, {150, 160, 80}, {190, 175, 120}, {16, 90, 100}, {235, 245, 245}, {120, 230, 210}, 0.55f, 3.2f, 0.88f},
      // Volcanic, glowing rifts in dark rock
      {PlanetKind::kLava, {50, 40, 40}, {96, 76, 64}, {255, 90, 20}, {200, 190, 180}, {255, 110, 50}, 0.46f, 3.5f, 1.5f},
      // Gas giants: orange, teal and cream
      {PlanetKind::kBanded, {214, 164, 104}, {150, 90, 60}, {120, 70, 50}, {255, 255, 255}, {255, 200, 140}, 0.5f, 3.0f, 1.5f},
      {PlanetKind::kBanded, {120, 176, 214}, {52, 92, 152}, {40, 70, 120}, {255, 255, 255}, {140, 210, 255}, 0.5f, 3.0f, 1.5f},
      {PlanetKind::kBanded, {232, 216, 172}, {172, 140, 108}, {140, 110, 90}, {255, 255, 255}, {240, 220, 170}, 0.5f, 3.0f, 1.5f},
  };
  constexpr std::uint32_t kTypeCount = sizeof(kTypes) / sizeof(kTypes[0]);
  // In the order of kTypes
  static const char* const kNames[] = {"Temperate world", "Desert world", "Cratered moon",   "Rust world",
                                       "Ocean world",     "Frozen world", "Violet jungle",   "Toxic world",
                                       "Coral world",     "Teal seas",    "Volcanic world",  "Amber gas giant",
                                       "Blue gas giant",  "Cream gas giant"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == kTypeCount, "a name for each type of planet");
  const std::uint32_t h = Hash32(seed);
  const Type& t = kTypes[h % kTypeCount];
  const std::uint32_t j = Hash32(h ^ 0x9E3779B9U);
  PlanetStyle s{t.kind,         Jitter(t.land, j, 0.18f), Jitter(t.high, j >> 3, 0.15f), Jitter(t.sea, j >> 5, 0.2f), t.ice,
                t.atmosphere,   t.sea_level,              t.terrain,                     t.ice_latitude,              0.0f,
                0.0f};
  s.sea_level += (static_cast<float>(j & 255U) / 255.0f - 0.5f) * 0.1f;
  s.terrain *= 0.8f + 0.4f * static_cast<float>((j >> 8) & 255U) / 255.0f;
  // Half the time keep the atmosphere that goes with the world, otherwise tint it from a short list of others
  static const Rgb kHazes[] = {{120, 170, 255}, {150, 230, 240}, {255, 190, 120}, {170, 240, 130},
                               {210, 150, 255}, {255, 150, 190}, {255, 120, 90},  {230, 235, 245}};
  const std::uint32_t a = Hash32(j ^ 0x85EBCA6BU);
  if ((a & 1U) != 0U) {
    s.atmosphere = Jitter(kHazes[(a >> 4) % 8U], a >> 8, 0.1f);
  }
  // How thick: none, thin, medium or thick
  const std::uint32_t roll = (a >> 12) % 20U;
  int grade = roll < 3 ? 0 : roll < 10 ? 1 : roll < 17 ? 2 : 3;
  if (t.kind == PlanetKind::kBanded) {
    grade = std::max(grade, 1);
  } else if (t.kind == PlanetKind::kLava) {
    grade = std::max(grade, 2);
  }
  static const float kThickness[] = {0.0f, 0.05f, 0.10f, 0.18f};
  static const float kStrength[] = {0.0f, 0.5f, 0.8f, 1.0f};
  s.glow_thickness = kThickness[grade];
  s.glow_strength = kStrength[grade];
  // Rings: most gas giants have them, a few other worlds too, tilted to the way the planet turns
  const std::uint32_t r = Hash32(a ^ 0xC0FFEE11U);
  if (r % 100U < (t.kind == PlanetKind::kBanded ? 75U : 18U)) {
    const float u1 = static_cast<float>((r >> 8) & 255U) / 255.0f;
    const float u2 = static_cast<float>((r >> 16) & 255U) / 255.0f;
    const float u3 = static_cast<float>((r >> 24) & 255U) / 255.0f;
    s.ring_inner = 1.25f + 0.25f * u1;
    s.ring_outer = s.ring_inner + 0.55f + 0.5f * u2;
    s.ring_gap = 0.55f + 0.2f * u3;
    s.ring_colour = t.kind == PlanetKind::kBanded ? Mix(s.land, {236, 226, 200}, 0.55f) : Jitter({196, 182, 160}, r, 0.12f);
    const double tilt = 0.25 + 0.5 * u1;
    const double turn = 6.283185307179586 * u3;
    s.ring_axis = {std::sin(tilt) * std::cos(turn), std::cos(tilt), std::sin(tilt) * std::sin(turn)};
  }
  s.name = kNames[h % kTypeCount];
  return s;
}

/// A planet or a sun as a shaded sphere, with depth. `light` is the direction to the sun in camera space.
void DrawBody(Canvas& canvas, const Camera& cam, const Entity& body, const Vec3d& centre, const Vec3d& light,
              std::uint32_t style_seed) {
  const double d = Length(centre);
  const double radius = body.radius;
  const bool sun = body.kind == EntityKind::kSun;
  const PlanetStyle style = StyleFor(style_seed);
  const bool ringed = !sun && style.ring_outer > 0.0f;
  // Everything of the body lies inside a sphere this big
  const double reach_radius = ringed ? radius * style.ring_outer : radius;
  if (d <= radius || (centre.z < -reach_radius && d > 3.0 * reach_radius)) {
    return;
  }
  int x0 = 0;
  int x1 = canvas.width() - 1;
  int y0 = 0;
  int y1 = canvas.view_height() - 1;
  double screen_radius = 1e9;
  if (centre.z > 1.0) {
    screen_radius = cam.f * radius / std::sqrt(std::max(1.0, d * d - radius * radius));
    const double glow = sun ? screen_radius * 1.4 : screen_radius * style.glow_thickness * 2.0 + 2.0;
    // A sphere off the view axis projects to a stretched ellipse, not a circle, so bound it by the
    // tangent planes through the camera: one pair for x (in the x-z plane) and one for y (in the y-z plane).
    const auto extent = [&](double a, double& lo, double& hi) {
      const double dist = std::hypot(a, centre.z);
      if (dist <= reach_radius) {
        return false;
      }
      const double theta = std::atan2(a, centre.z);
      const double alpha = std::asin(reach_radius / dist);
      if (theta + alpha >= 1.5 || theta - alpha <= -1.5) {
        return false;  // reaches to the side of the camera: unbounded on this axis
      }
      lo = cam.f * std::tan(theta - alpha);
      hi = cam.f * std::tan(theta + alpha);
      return true;
    };
    double lo = 0.0;
    double hi = 0.0;
    if (extent(centre.x, lo, hi)) {
      x0 = std::max(0, static_cast<int>(std::floor(cam.cx + lo - glow - 1.0)));
      x1 = std::min(canvas.width() - 1, static_cast<int>(std::ceil(cam.cx + hi + glow + 1.0)));
    }
    if (extent(centre.y, lo, hi)) {
      y0 = std::max(0, static_cast<int>(std::floor(cam.cy - hi - glow - 1.0)));
      y1 = std::min(canvas.view_height() - 1, static_cast<int>(std::ceil(cam.cy - lo + glow + 1.0)));
    }
  }
  const double cc = Dot(centre, centre) - radius * radius;
  ParallelRows(y0, y1 + 1, [&](int row_begin, int row_end) {
  for (int y = row_begin; y < row_end; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const Vec3d ray{(x + 0.5 - cam.cx) / cam.f, -(y + 0.5 - cam.cy) / cam.f, 1.0};
      const double rr = Dot(ray, ray);
      const double b = Dot(ray, centre);
      const double disc = b * b - rr * cc;
      if (disc < 0.0) {
        if (sun && screen_radius < 1e8) {
          // The glow of the sun round its edge
          const double sx = cam.cx + cam.f * centre.x / centre.z;
          const double sy = cam.cy - cam.f * centre.y / centre.z;
          const double dd = std::hypot(x + 0.5 - sx, y + 0.5 - sy);
          const double t = (dd - screen_radius) / (screen_radius * 1.4);
          if (t > 0.0 && t < 1.0 && canvas.depth(x, y) <= 0.0f) {
            const float k = static_cast<float>((1.0 - t) * (1.0 - t));
            canvas.Add(x, y, 255.0f * k * 0.8f, 200.0f * k * 0.7f, 90.0f * k * 0.5f);
          }
        } else if (!sun && style.glow_thickness > 0.0f && b > 0.0) {
          // The glow of the atmosphere beyond the edge, brightest on the side the sun lights
          const Vec3d q = ray * (b / rr);
          const Vec3d off = q - centre;
          const double h = Length(off) / radius;
          const double t = (h - 1.0) / style.glow_thickness;
          if (t > 0.0 && t < 1.0 && canvas.depth(x, y) <= 0.0f) {
            const double lit = std::clamp(Dot(off * (1.0 / (h * radius)), light) + 0.25, 0.0, 1.0);
            const float k = static_cast<float>(std::pow(1.0 - t, 2.5) * lit * style.glow_strength);
            canvas.Add(x, y, style.atmosphere.r * k * 0.9f, style.atmosphere.g * k * 0.9f, style.atmosphere.b * k * 0.9f);
          }
        }
        continue;
      }
      const double t = (b - std::sqrt(disc)) / rr;
      if (t <= 0.0) {
        continue;
      }
      const Vec3d p = ray * t;
      const float inv = static_cast<float>(1.0 / p.z);
      if (inv <= canvas.depth(x, y)) {
        continue;
      }
      canvas.depth(x, y) = inv;
      const Vec3d n = (p - centre) * (1.0 / radius);
      Rgb colour;
      if (sun) {
        const double facing = std::clamp(-Dot(n, Normalized(ray)), 0.0, 1.0);
        const float k = static_cast<float>(0.55 + 0.45 * std::sqrt(facing));
        colour = Scale({255, 226, 140}, k);
      } else {
        const Vec3d nw = Rotate(cam.q, n);
        // The seed also offsets the noise, so two planets of one type have different continents
        const float off = static_cast<float>(Hash32(style_seed ^ 0xA5A5A5A5U) & 1023U) * 0.37f;
        const float scale = style.terrain;
        const float px = static_cast<float>(nw.x) * scale + off;
        const float py = static_cast<float>(nw.y) * scale + off * 0.7f;
        const float pz = static_cast<float>(nw.z) * scale - off * 0.3f;
        const float h = 0.6f * Noise3(px, py, pz) + 0.3f * Noise3(px * 2.3f, py * 2.3f, pz * 2.3f) +
                        0.1f * Noise3(px * 6.1f, py * 6.1f, pz * 6.1f);
        Rgb glow_rock{0, 0, 0};
        if (style.kind == PlanetKind::kBanded) {
          // Bands of cloud along the lines of latitude, stirred by the noise
          const float v = static_cast<float>(nw.y) * 3.0f * style.terrain + 2.4f * (h - 0.5f) + off;
          const float band = 0.5f + 0.5f * std::sin(v * 2.0f);
          const float fine = 0.5f + 0.5f * std::sin(v * 7.0f + 3.0f * h);
          colour = Mix(Mix(style.land, style.high, band), Scale(style.land, 0.85f), 0.3f * fine);
        } else {
          const float shore = style.sea_level;
          if (h > shore) {
            const float rise = std::clamp((h - shore) * 4.0f, 0.0f, 1.0f);
            colour = Scale(Mix(style.land, style.high, rise * rise), 0.75f + 0.5f * h);
          } else if (style.kind == PlanetKind::kLava) {
            // Molten rifts: dark crust at the edges, glowing in the deep
            colour = Scale(style.land, 0.7f);
            const float deep = std::clamp((shore - h) * 6.0f, 0.0f, 1.0f);
            glow_rock = Scale(style.sea, deep * (0.7f + 0.5f * Noise3(px * 9.0f, py * 9.0f, pz * 9.0f)));
          } else {
            // Shallow water by the coast, deep water far from it
            colour = Mix(Scale(style.sea, 1.5f), Scale(style.sea, 0.8f), std::clamp((shore - h) * 5.0f, 0.0f, 1.0f));
          }
          if (std::fabs(nw.y) > style.ice_latitude) {
            colour = Mix(colour, style.ice, static_cast<float>(std::min(1.0, (std::fabs(nw.y) - style.ice_latitude) * 12.0)));
            glow_rock = {0, 0, 0};
          }
        }
        const float lit = static_cast<float>(std::clamp(Dot(n, light) * 1.25 + 0.08, 0.04, 1.0));
        colour = Scale(colour, lit);
        if (glow_rock.r + glow_rock.g + glow_rock.b > 0) {
          // Lava shines whether or not the sun reaches it
          colour = {static_cast<std::uint8_t>(std::min(255, colour.r + glow_rock.r)),
                    static_cast<std::uint8_t>(std::min(255, colour.g + glow_rock.g)),
                    static_cast<std::uint8_t>(std::min(255, colour.b + glow_rock.b))};
        }
        // The atmosphere seen at the edge of the disc, on the lit side: thicker air gives a wider and brighter rim
        if (style.glow_thickness > 0.0f) {
          const double facing = std::clamp(-Dot(n, Normalized(ray)), 0.0, 1.0);
          const float power = 3.0f - 6.0f * style.glow_thickness;
          const float rim = std::min(
              1.0f, static_cast<float>(std::pow(1.0 - facing, power)) * 0.8f * style.glow_strength * (0.25f + lit));
          colour = Mix(colour, style.atmosphere, rim);
        }
      }
      canvas.Set(x, y, colour);
    }
  }
  });
  if (!ringed) {
    return;
  }

  // The rings: a flat disc with a hole, seen by crossing each pixel's ray with its plane. The depth buffer puts
  // the near half in front of the planet and the far half behind it.
  const Vec3d axis = Rotate(Conjugate(cam.q), style.ring_axis);
  const double axis_centre = Dot(axis, centre);
  const double light_side = Dot(axis, light);
  // The sun shines on the side of the rings the ship sees, or on the far side, which is dim
  const float side = (light_side > 0.0) == (axis_centre < 0.0) ? 1.0f : 0.35f;
  const float offset = static_cast<float>(Hash32(style_seed ^ 0x5EED1234U) & 1023U) * 0.53f;
  const double inner = style.ring_inner;
  const double outer = style.ring_outer;
  ParallelRows(y0, y1 + 1, [&](int row_begin, int row_end) {
  for (int y = row_begin; y < row_end; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const Vec3d ray{(x + 0.5 - cam.cx) / cam.f, -(y + 0.5 - cam.cy) / cam.f, 1.0};
      const double slope = Dot(axis, ray);
      if (std::fabs(slope) < 1e-9) {
        continue;
      }
      const double t = axis_centre / slope;
      if (t <= 0.0) {
        continue;
      }
      const Vec3d p = ray * t;
      const Vec3d off = p - centre;
      const double rd = Length(off) / radius;
      if (rd < inner || rd > outer) {
        continue;
      }
      const float inv = static_cast<float>(1.0 / t);
      if (inv <= canvas.depth(x, y)) {
        continue;
      }
      // Fine bands and one wide gap, thinning out at both edges
      const float u = static_cast<float>((rd - inner) / (outer - inner));
      const float coarse = Noise3(u * 9.0f + offset, 1.7f, 3.1f);
      const float fine = Noise3(u * 41.0f + offset, 5.3f, 0.7f);
      float density = 0.25f + 0.9f * (0.55f * coarse + 0.45f * fine);
      density *= 0.08f + 0.92f * std::clamp(std::fabs(u - style.ring_gap) / 0.05f, 0.0f, 1.0f);
      density *= std::clamp(u / 0.05f, 0.0f, 1.0f) * std::clamp((1.0f - u) / 0.05f, 0.0f, 1.0f);
      const float alpha = std::clamp((density - 0.3f) * 1.8f, 0.0f, 0.92f);
      if (alpha < 0.03f) {
        continue;
      }
      // The planet casts its shadow on the rings
      float lit = side * (0.55f + 0.45f * fine);
      const double toward = Dot(off, light);
      if (toward < 0.0 && Dot(off, off) - toward * toward < radius * radius) {
        lit *= 0.15f;
      }
      const Rgb colour = Scale(Mix(style.ring_colour, Scale(style.ring_colour, 0.6f), fine), lit);
      canvas.Set(x, y, Mix(canvas.Get(x, y), colour, alpha));
      if (alpha > 0.5f) {
        canvas.depth(x, y) = inv;
      }
    }
  }
  });
}

/// Draw one entity's model from the cockpit
void DrawEntity(Canvas& canvas, const Camera& cam, const Entity& e, const Model& model, const Vec3d& light) {
  const Vec3d centre = ToCamera(cam, e.position);
  if (centre.z < -e.radius) {
    return;
  }
  const double dist = std::max(Length(centre), 1.0);
  const double screen_radius = cam.f * e.radius / dist;
  float sx = 0.0f;
  float sy = 0.0f;
  if (screen_radius < 1.6) {
    // Too far to see as a shape: a dot, brighter for a bigger thing
    if (Project(cam, centre, &sx, &sy)) {
      const int ix = static_cast<int>(sx);
      const int iy = static_cast<int>(sy);
      if (ix >= 0 && iy >= 0 && ix < canvas.width() && iy < canvas.view_height() && canvas.depth(ix, iy) < static_cast<float>(1.0 / dist)) {
        canvas.Set(ix, iy, TeamColour(e));
      }
    }
    return;
  }
  const double scale = e.blueprint->scale;
  const Quat r = Conjugate(cam.q) * e.orientation;
  std::vector<Vec3d> verts(model.vertices.size());
  for (std::size_t i = 0; i < verts.size(); ++i) {
    const Vec3& v = model.vertices[i];
    verts[i] = Rotate(r, Vec3d{v.x, v.y, v.z} * scale) + centre;
  }
  for (const Face& face : model.faces) {
    const Vec3d& a = verts[face.v[0]];
    const Vec3d& b = verts[face.v[1]];
    const Vec3d& c = verts[face.v[2]];
    Vec3d n = Cross(b - a, c - a);
    const double length = Length(n);
    if (length < 1e-9) {
      continue;
    }
    n = n * (1.0 / length);
    const Vec3d mid = (a + b + c) * (1.0 / 3.0);
    const Vec3d face_centre = Rotate(r, Vec3d{face.centre.x, face.centre.y, face.centre.z} * scale) + centre;
    if (Dot(n, mid - face_centre) < 0.0) {
      n = -n;
    }
    // Faces turned away from the eye are not drawn
    if (Dot(n, mid) >= 0.0) {
      continue;
    }
    Rgb colour = face.colour;
    if (!face.emissive) {
      const float lit = 0.22f + 0.78f * static_cast<float>(std::max(0.0, Dot(n, light)));
      colour = Scale(colour, lit);
    }
    FillPolygon(canvas, cam, {a, b, c}, colour, face.edges);
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// The cockpit: a 3D interior drawn in front of the view, with computer screens on the console. The paint comes from the
// ship's own colours, so each ship has a cockpit to match its design.

struct Palette {
  /// The main colour of the hull and the colour of its markings
  Rgb hull;
  Rgb accent;
};

/// The inside of the docking bay, drawn while the ship is in it: a tunnel of panels with strips of light, running back
/// into the station from the mouth. The model of the station has no hollow of its own, so the tunnel is drawn here.
void DrawBay(Canvas& canvas, const Camera& cam, const Bay& bay, double beam = -1.0) {
  const auto P = [&](double x, double y, double z) { return ToCamera(cam, bay.mouth + bay.right * x + bay.up * y + bay.axis * z); };
  const auto quad = [&](const Vec3d& a, const Vec3d& b, const Vec3d& c, const Vec3d& d, Rgb colour) { FillPolygon(canvas, cam, {a, b, c, d}, colour, 0); };
  const double w = bay.half_width;
  const double h = bay.half_height;
  const double inset = 0.6;
  const int segments = 20;
  const double step = bay.depth / segments;
  for (int k = 0; k < segments; ++k) {
    const double z0 = -bay.depth + k * step;
    const double z1 = z0 + step;
    // Brighter towards the mouth, where daylight spills in; every other panel a little darker
    const float depth_light = 0.55f + 0.45f * static_cast<float>(k) / static_cast<float>(segments - 1);
    const float shade = depth_light * (k % 2 == 0 ? 1.0f : 0.82f);
    quad(P(-w, -h, z0), P(w, -h, z0), P(w, -h, z1), P(-w, -h, z1), Scale({70, 76, 90}, shade));
    quad(P(-w, h, z0), P(w, h, z0), P(w, h, z1), P(-w, h, z1), Scale({84, 92, 108}, shade));
    quad(P(-w, -h, z0), P(-w, h, z0), P(-w, h, z1), P(-w, -h, z1), Scale({92, 100, 116}, shade));
    quad(P(w, -h, z0), P(w, h, z0), P(w, h, z1), P(w, -h, z1), Scale({92, 100, 116}, shade));
    // Strips of light along the walls and panels of light in the roof
    const double gap = step * 0.06;
    if (k % 3 != 2) {
      const Rgb amber{255, 196, 84};
      quad(P(-w + inset, 0.35 * h, z0 + gap), P(-w + inset, 0.5 * h, z0 + gap), P(-w + inset, 0.5 * h, z1 - gap), P(-w + inset, 0.35 * h, z1 - gap), amber);
      quad(P(w - inset, 0.35 * h, z0 + gap), P(w - inset, 0.5 * h, z0 + gap), P(w - inset, 0.5 * h, z1 - gap), P(w - inset, 0.35 * h, z1 - gap), amber);
    }
    if (k % 2 == 0) {
      const Rgb white{225, 238, 255};
      quad(P(-0.3 * w, h - inset, z0 + 2 * gap), P(0.3 * w, h - inset, z0 + 2 * gap), P(0.3 * w, h - inset, z1 - 2 * gap), P(-0.3 * w, h - inset, z1 - 2 * gap), white);
      // A dashed guide line down the middle of the floor
      const Rgb guide{236, 206, 76};
      quad(P(-0.04 * w, -h + inset, z0 + 3 * gap), P(0.04 * w, -h + inset, z0 + 3 * gap), P(0.04 * w, -h + inset, z1 - 3 * gap), P(-0.04 * w, -h + inset, z1 - 3 * gap), guide);
    }
  }
  // The back wall, with two lit panels
  quad(P(-w, -h, -bay.depth), P(w, -h, -bay.depth), P(w, h, -bay.depth), P(-w, h, -bay.depth), {38, 42, 52});
  quad(P(-0.8 * w, -0.5 * h, -bay.depth + inset), P(-0.2 * w, -0.5 * h, -bay.depth + inset), P(-0.2 * w, 0.5 * h, -bay.depth + inset),
       P(-0.8 * w, 0.5 * h, -bay.depth + inset), {160, 60, 48});
  quad(P(0.2 * w, -0.5 * h, -bay.depth + inset), P(0.8 * w, -0.5 * h, -bay.depth + inset), P(0.8 * w, 0.5 * h, -bay.depth + inset),
       P(0.2 * w, 0.5 * h, -bay.depth + inset), {160, 60, 48});
  // The rim of the mouth, bright all round
  const Rgb rim{255, 214, 90};
  const double r = 0.04 * w;
  quad(P(-w, -h, -4.0), P(-w + r, -h, -4.0), P(-w + r, h, -4.0), P(-w, h, -4.0), rim);
  quad(P(w - r, -h, -4.0), P(w, -h, -4.0), P(w, h, -4.0), P(w - r, h, -4.0), rim);
  quad(P(-w, h - r, -4.0), P(w, h - r, -4.0), P(w, h, -4.0), P(-w, h, -4.0), rim);
  quad(P(-w, -h, -4.0), P(w, -h, -4.0), P(w, -h + r, -4.0), P(-w, -h + r, -4.0), rim);
  // The tractor beam: rings of light run down the bay, fading as they go, while the ship is being landed
  if (beam >= 0.0) {
    const int rings = 6;
    for (int i = 0; i < rings; ++i) {
      const double f = (i + beam) / rings;
      const double z = -f * bay.depth * 0.95;
      const float level = static_cast<float>(0.9 * (1.0 - f));
      const Rgb c = Scale({70, 200, 255}, level);
      const Vec3d corners[4] = {P(-0.9 * w, -0.9 * h, z), P(0.9 * w, -0.9 * h, z), P(0.9 * w, 0.9 * h, z), P(-0.9 * w, 0.9 * h, z)};
      for (int k = 0; k < 4; ++k) {
        float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
        if (Project(cam, corners[k], &ax, &ay) && Project(cam, corners[(k + 1) % 4], &bx, &by)) {
          canvas.Line(ax, ay, bx, by, c, true);
        }
      }
    }
  }
}

Palette PaletteFor(const Model& model) {
  std::map<std::uint32_t, std::pair<int, Rgb>> colours;
  for (const Face& f : model.faces) {
    if (f.emissive) {
      continue;
    }
    const std::uint32_t key = (static_cast<std::uint32_t>(f.colour.r >> 4) << 8) | (static_cast<std::uint32_t>(f.colour.g >> 4) << 4) |
                              static_cast<std::uint32_t>(f.colour.b >> 4);
    auto& entry = colours[key];
    ++entry.first;
    entry.second = f.colour;
  }
  Palette p{{120, 124, 132}, {60, 200, 200}};
  int most = 0;
  for (const auto& kv : colours) {
    if (kv.second.first > most) {
      most = kv.second.first;
      p.hull = kv.second.second;
    }
  }
  // The accent is the most colourful of the other colours that are used a fair amount
  int best = 40;
  for (const auto& kv : colours) {
    const Rgb c = kv.second.second;
    const int saturation = std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b});
    const int apart = std::abs(c.r - p.hull.r) + std::abs(c.g - p.hull.g) + std::abs(c.b - p.hull.b);
    if (kv.second.first >= 2 && apart > 90 && saturation > best) {
      best = saturation;
      p.accent = c;
    }
  }
  return p;
}

/// The colour brought up to a bright, glowing version of itself
Rgb Glowing(Rgb c) {
  const int m = std::max({static_cast<int>(c.r), static_cast<int>(c.g), static_cast<int>(c.b), 1});
  return Scale(c, std::max(1.0f, 235.0f / static_cast<float>(m)));
}

int TextWidth(const std::string& s, int ts) { return static_cast<int>(s.size()) * 6 * ts; }

std::string Fmt(const char* format, double a) {
  char buffer[64];
  std::snprintf(buffer, sizeof buffer, format, a);
  return buffer;
}

/// The dark glass of a screen with a faint grid, a title and corner marks
void ScreenBase(Image& tex, const std::string& title, Rgb accent, int ts) {
  Canvas c(tex, static_cast<int>(tex.height()), false);
  const int w = static_cast<int>(tex.width());
  const int h = static_cast<int>(tex.height());
  for (int y = 0; y < h; ++y) {
    const float t = static_cast<float>(y) / static_cast<float>(h);
    for (int x = 0; x < w; ++x) {
      const float edge = std::pow(std::fabs(2.0f * static_cast<float>(x) / static_cast<float>(w) - 1.0f), 3.0f);
      const float k = 1.0f - 0.35f * edge;
      c.Set(x, y, {static_cast<std::uint8_t>((4.0f + 4.0f * t) * k), static_cast<std::uint8_t>((9.0f + 9.0f * t) * k),
                   static_cast<std::uint8_t>((14.0f + 12.0f * t) * k)});
    }
  }
  const Rgb grid = Scale(accent, 0.10f);
  for (int x = 0; x < w; x += 28) {
    c.Line(static_cast<float>(x), 0.0f, static_cast<float>(x), static_cast<float>(h), grid, true);
  }
  for (int y = 0; y < h; y += 28) {
    c.Line(0.0f, static_cast<float>(y), static_cast<float>(w), static_cast<float>(y), grid, true);
  }
  DrawText(tex, title, 12, 8, static_cast<std::uint32_t>(ts), Glowing(accent));
  c.Line(10.0f, static_cast<float>(8 + 9 * ts), static_cast<float>(w - 10), static_cast<float>(8 + 9 * ts), Scale(accent, 0.7f));
  const float arm = 12.0f;
  const Rgb mark = Scale(accent, 0.8f);
  for (const int sx : {0, 1}) {
    for (const int sy : {0, 1}) {
      const float x = sx == 0 ? 3.0f : static_cast<float>(w - 4);
      const float y = sy == 0 ? 3.0f : static_cast<float>(h - 4);
      c.Line(x, y, x + (sx == 0 ? arm : -arm), y, mark);
      c.Line(x, y, x, y + (sy == 0 ? arm : -arm), mark);
    }
  }
}

/// Scan lines, as on a real display
void ScreenFinish(Image& tex) {
  const int w = static_cast<int>(tex.width());
  const int h = static_cast<int>(tex.height());
  for (int y = 1; y < h; y += 2) {
    std::uint8_t* row = tex.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 3;
    for (int i = 0; i < w * 3; ++i) {
      row[i] = static_cast<std::uint8_t>(row[i] * 0.86f);
    }
  }
}

/// A segmented gauge with a label and a value
void Meter(Image& tex, int x, int y, int w, double fraction, Rgb colour, const std::string& label, const std::string& value, int ts) {
  Canvas c(tex, static_cast<int>(tex.height()), false);
  DrawText(tex, label, x, y, static_cast<std::uint32_t>(ts), {150, 178, 192});
  DrawText(tex, value, x + w - TextWidth(value, ts), y, static_cast<std::uint32_t>(ts), {235, 246, 250});
  const int by = y + 10 * ts;
  const int segments = std::max(8, w / (3 * ts + 1));
  const int segment_w = std::max(2, w / segments);
  const int filled = static_cast<int>(std::lround(std::clamp(fraction, 0.0, 1.0) * segments));
  for (int i = 0; i < segments; ++i) {
    c.Rect(x + i * segment_w, by, segment_w - 1, 4 * ts, i < filled ? colour : Rgb{18, 30, 38});
  }
}

void DrawSystemsScreen(Image& tex, const Game& game, const Entity& me, const Palette& pal) {
  const int h = static_cast<int>(tex.height());
  const int w = static_cast<int>(tex.width());
  const int ts = std::max(1, h / 90);
  const Rgb accent = Glowing(pal.accent);
  ScreenBase(tex, "SHIP SYSTEMS", accent, ts);
  Canvas c(tex, h, false);
  const int x = 14;
  const int gap = 16;
  const int mw = (w - 28 - gap) / 2;
  const int x2 = x + mw + gap;
  int y = 12 + 10 * ts;
  const int row = 20 * ts;
  const auto percent = [](double f) { return Fmt("%.0f%%", f * 100.0); };
  const double shield = me.shield_max > 0.0 ? me.shield / me.shield_max : 0.0;
  Meter(tex, x, y, mw, shield, {70, 160, 255}, "SHIELDS", percent(shield), ts);
  const double hull = me.hull / me.hull_max;
  Meter(tex, x2, y, mw, hull, hull < 0.3 ? Rgb{255, 70, 50} : Rgb{255, 176, 48}, "HULL", percent(hull), ts);
  y += row;
  const double energy = me.energy / me.energy_max;
  Meter(tex, x, y, mw, energy, {80, 235, 130}, "ENERGY", percent(energy), ts);
  Meter(tex, x2, y, mw, game.commander().fuel / kMaxFuel, {120, 210, 255}, "FUEL", Fmt("%.1f LY", game.commander().fuel), ts);
  y += row + 2 * ts;

  // The parts of the ship that can be damaged, side by side
  const struct {
    const char* part;
    const char* label;
  } parts[] = {{"engines", "ENGINES"}, {"weapons", "WEAPONS"}, {"reactor", "REACTOR"}};
  int px = x;
  for (const auto& part : parts) {
    const int index = me.SubsystemIndex(part.part);
    if (index < 0) {
      continue;
    }
    const double full = me.blueprint->subsystems[static_cast<std::size_t>(index)].health;
    const double f = full > 0.0 ? me.subsystem_health[static_cast<std::size_t>(index)] / full : 0.0;
    const char* status = f > 0.5 ? "OK" : (f > 0.0 ? "DAMAGED" : "OFFLINE");
    const Rgb colour = f > 0.5 ? Rgb{90, 235, 130} : (f > 0.0 ? Rgb{255, 190, 60} : Rgb{255, 70, 60});
    c.Disc(static_cast<float>(px + 3 * ts), static_cast<float>(y + 3 * ts), 2.2f * static_cast<float>(ts), colour);
    DrawText(tex, part.label, px + 9 * ts, y, static_cast<std::uint32_t>(ts), {170, 196, 208});
    DrawText(tex, status, px + 9 * ts + TextWidth(part.label, ts) + 5 * ts, y, static_cast<std::uint32_t>(ts), colour);
    px += (w - 28) / 3;
  }
  y += row;

  // Weapons
  DrawText(tex, "MISSILES", x, y, static_cast<std::uint32_t>(ts), {170, 196, 208});
  for (int m = 0; m < 4; ++m) {
    const Rgb col = m < me.missiles ? (game.missile_lock() != kNoEntity ? Rgb{255, 80, 70} : Rgb{90, 235, 130}) : Rgb{24, 36, 44};
    c.Rect(x + TextWidth("MISSILES ", ts) + m * 9 * ts, y, 7 * ts, 6 * ts, col);
  }
  const char* ecm = !me.has_ecm ? "ECM NOT FITTED" : (me.energy >= 20.0 ? "ECM READY" : "ECM CHARGING");
  DrawText(tex, ecm, x2, y, static_cast<std::uint32_t>(ts), me.has_ecm && me.energy >= 20.0 ? Rgb{90, 235, 130} : Rgb{150, 160, 168});
  ScreenFinish(tex);
}

void DrawScannerScreen(Image& tex, const Game& game, const Entity& me, const Palette& pal, std::uint32_t frame) {
  const int h = static_cast<int>(tex.height());
  const int w = static_cast<int>(tex.width());
  const int ts = std::max(1, h / 90);
  const Rgb accent = Glowing(pal.accent);
  ScreenBase(tex, "TACTICAL SCANNER", accent, ts);
  Canvas c(tex, h, false);
  const float cx = static_cast<float>(w) / 2.0f;
  const float cy = static_cast<float>(h) * 0.61f;
  const float rx = static_cast<float>(w) * 0.32f;
  const float ry = static_cast<float>(h) * 0.33f;
  const Rgb ring = Scale(accent, 0.55f);

  // The glass of the scanner: brighter towards the middle, with range rings and bearing lines
  for (int y = static_cast<int>(cy - ry); y <= static_cast<int>(cy + ry); ++y) {
    for (int x = static_cast<int>(cx - rx); x <= static_cast<int>(cx + rx); ++x) {
      const float ex = (static_cast<float>(x) - cx) / rx;
      const float ey = (static_cast<float>(y) - cy) / ry;
      const float r2 = ex * ex + ey * ey;
      if (r2 < 1.0f) {
        const float k = 0.10f * (1.0f - r2);
        c.Add(x, y, accent.r * k, accent.g * k, accent.b * k);
      }
    }
  }
  for (const float f : {1.0f, 0.6667f, 0.3333f}) {
    c.Ellipse(cx, cy, rx * f, ry * f, f == 1.0f ? ring : Scale(ring, 0.6f));
  }
  for (int i = 0; i < 12; ++i) {
    const float a = static_cast<float>(i) * kPi / 6.0f;
    c.Line(cx + std::cos(a) * rx * 0.08f, cy + std::sin(a) * ry * 0.08f, cx + std::cos(a) * rx, cy + std::sin(a) * ry, Scale(ring, 0.4f));
  }
  // The sweep
  const float sweep = static_cast<float>(frame) * 0.05f;
  for (int k = 0; k < 24; ++k) {
    const float a = sweep - static_cast<float>(k) * 0.025f;
    const float fade = std::pow(1.0f - static_cast<float>(k) / 24.0f, 2.0f) * 0.5f;
    c.Line(cx, cy, cx + std::cos(a) * rx, cy + std::sin(a) * ry, Scale(accent, fade), true);
  }
  const double range = 14000.0;
  DrawText(tex, "14 KM", static_cast<std::int64_t>(cx + rx * 0.72f), static_cast<std::int64_t>(cy + ry * 0.74f), static_cast<std::uint32_t>(ts), Scale(accent, 0.8f));
  DrawText(tex, "FWD", static_cast<std::int64_t>(cx - 9 * ts), static_cast<std::int64_t>(cy - ry - 9 * ts), static_cast<std::uint32_t>(ts), Scale(accent, 0.8f));

  // The contacts: a stalk shows how far above or below the ship each one is
  for (const Entity& e : game.world().entities()) {
    if (!e.alive || e.id == me.id || e.kind == EntityKind::kPlanet || e.kind == EntityKind::kSun) {
      continue;
    }
    Vec3d local = Rotate(Conjugate(me.orientation), e.position - me.position) * (1.0 / range);
    const double flat = std::hypot(local.x, local.z);
    const bool beyond = flat > 1.0;
    if (beyond) {
      local = local * (1.0 / flat);
    }
    const float bx = cx + static_cast<float>(local.x) * rx;
    const float by = cy - static_cast<float>(local.z) * ry;
    const float tip = by - static_cast<float>(std::clamp(local.y, -1.0, 1.0)) * ry * 0.8f;
    const Rgb col = TeamColour(e);
    c.Line(bx, by, bx, tip, Scale(col, 0.55f));
    c.Ellipse(bx, by, 3.0f * static_cast<float>(ts), 1.5f * static_cast<float>(ts), Scale(col, 0.5f));
    const float s = 3.0f * static_cast<float>(ts) * (beyond ? 0.7f : 1.0f);
    c.Glow(bx, tip, s * 2.6f, col.r, col.g, col.b, 0.55f);
    const bool hostile = e.team == Team::kPirate || e.team == Team::kAlien;
    if (e.kind == EntityKind::kStation) {
      c.RectOutline(static_cast<int>(bx - s), static_cast<int>(tip - s), static_cast<int>(2 * s), static_cast<int>(2 * s), col);
    } else if (e.kind == EntityKind::kMissile) {
      c.Line(bx - s, tip, bx, tip - s, col);
      c.Line(bx, tip - s, bx + s, tip, col);
      c.Line(bx + s, tip, bx, tip + s, col);
      c.Line(bx, tip + s, bx - s, tip, col);
    } else if (hostile) {
      c.Line(bx, tip - s, bx + s, tip + s, col);
      c.Line(bx + s, tip + s, bx - s, tip + s, col);
      c.Line(bx - s, tip + s, bx, tip - s, col);
    } else {
      c.Disc(bx, tip, s * 0.7f, col);
    }
  }
  // The ship in the middle
  c.Line(cx, cy - 5.0f * static_cast<float>(ts), cx + 4.0f * static_cast<float>(ts), cy + 4.0f * static_cast<float>(ts), {255, 255, 255});
  c.Line(cx + 4.0f * static_cast<float>(ts), cy + 4.0f * static_cast<float>(ts), cx - 4.0f * static_cast<float>(ts), cy + 4.0f * static_cast<float>(ts), {255, 255, 255});
  c.Line(cx - 4.0f * static_cast<float>(ts), cy + 4.0f * static_cast<float>(ts), cx, cy - 5.0f * static_cast<float>(ts), {255, 255, 255});

  // The key, down the left side
  const struct {
    const char* name;
    Rgb colour;
  } key[] = {{"HOSTILE", {255, 70, 60}}, {"POLICE", {80, 140, 255}}, {"TRADE", {255, 230, 90}}, {"STATION", {232, 232, 244}}};
  int ky = 12 + 10 * ts;
  for (const auto& k : key) {
    c.Disc(static_cast<float>(16 + 2 * ts), static_cast<float>(ky + 3 * ts), 2.0f * static_cast<float>(ts), k.colour);
    DrawText(tex, k.name, 16 + 7 * ts, ky, static_cast<std::uint32_t>(ts), {150, 178, 192});
    ky += 11 * ts;
  }
  ScreenFinish(tex);
}

void DrawNavScreen(Image& tex, const Game& game, const Entity& me, const Palette& pal) {
  const int h = static_cast<int>(tex.height());
  const int w = static_cast<int>(tex.width());
  const int ts = std::max(1, h / 90);
  const Rgb accent = Glowing(pal.accent);
  ScreenBase(tex, "NAVIGATION", accent, ts);
  Canvas c(tex, h, false);
  const int x = 14;
  const int top = 12 + 10 * ts;
  const Rgb dim{150, 178, 192};

  // Speed, large, then the throttle and the flight mode
  const std::string speed = Fmt("%.0f", me.Speed());
  DrawText(tex, speed, x, top, static_cast<std::uint32_t>(ts * 3), {240, 250, 255});
  DrawText(tex, "M/S", x + TextWidth(speed, ts * 3) + 4 * ts, top + 14 * ts, static_cast<std::uint32_t>(ts), dim);
  Meter(tex, x, top + 25 * ts, 150, game.throttle(), {150, 160, 255}, "THROTTLE", Fmt("%.0f%%", game.throttle() * 100.0), ts);
  const char* mode = game.flight_mode() == FlightMode::kClassic ? "CLASSIC" : (game.assist() ? "NEWTONIAN ASSIST" : "NEWTONIAN");
  DrawText(tex, mode, x, top + 44 * ts, static_cast<std::uint32_t>(ts), accent);

  // The station and the planet, with a dial that points to the station
  const Entity* station = game.world().FindKind(EntityKind::kStation);
  const Entity* planet = game.world().FindKind(EntityKind::kPlanet);
  const float dcx = 208.0f;
  const float dcy = static_cast<float>(top) + 24.0f * static_cast<float>(ts);
  const float dr = 14.0f * static_cast<float>(ts);
  c.Ellipse(dcx, dcy, dr, dr, Scale(accent, 0.7f));
  c.Ellipse(dcx, dcy, dr * 0.5f, dr * 0.5f, Scale(accent, 0.35f));
  c.Line(dcx - dr, dcy, dcx + dr, dcy, Scale(accent, 0.3f));
  c.Line(dcx, dcy - dr, dcx, dcy + dr, Scale(accent, 0.3f));
  if (station != nullptr) {
    const Vec3d l = Normalized(Rotate(Conjugate(me.orientation), station->position - me.position));
    const float px = dcx + static_cast<float>(l.x) * dr * 0.9f;
    const float py = dcy - static_cast<float>(l.y) * dr * 0.9f;
    const Rgb col = l.z > 0.0 ? Rgb{255, 235, 90} : Rgb{150, 130, 50};
    c.Line(dcx, dcy, px, py, Scale(col, 0.7f));
    if (l.z > 0.0) {
      c.Disc(px, py, 2.5f * static_cast<float>(ts), col);
    } else {
      c.Ellipse(px, py, 2.5f * static_cast<float>(ts), 2.5f * static_cast<float>(ts), col);
    }
  }
  const int bx = 252;
  DrawText(tex, "STATION", bx, top, static_cast<std::uint32_t>(ts), dim);
  if (station != nullptr) {
    const double d = Distance(me.position, station->position);
    DrawText(tex, d >= 1000.0 ? Fmt("%.1f KM", d / 1000.0) : Fmt("%.0f M", d), bx, top + 9 * ts, static_cast<std::uint32_t>(ts * 2), {240, 250, 255});
  } else {
    DrawText(tex, "NONE", bx, top + 9 * ts, static_cast<std::uint32_t>(ts * 2), dim);
  }
  DrawText(tex, "ALTITUDE", bx, top + 30 * ts, static_cast<std::uint32_t>(ts), dim);
  if (planet != nullptr) {
    const double alt = Distance(me.position, planet->position) - planet->radius;
    DrawText(tex, alt >= 1000.0 ? Fmt("%.1f KM", alt / 1000.0) : Fmt("%.0f M", alt), bx, top + 39 * ts, static_cast<std::uint32_t>(ts * 2), {240, 250, 255});
  }
  c.Line(static_cast<float>(bx - 10), static_cast<float>(top), static_cast<float>(bx - 10), static_cast<float>(h - 14 * ts), Scale(accent, 0.3f));
  c.Line(static_cast<float>(bx + 178), static_cast<float>(top), static_cast<float>(bx + 178), static_cast<float>(h - 14 * ts), Scale(accent, 0.3f));

  // The target
  const int tx = bx + 192;
  const int tw = w - 14 - tx;
  int y = top;
  const Entity* target = game.world().Find(game.target());
  if (target != nullptr && target->alive && target->blueprint != nullptr) {
    const char* side = "NEUTRAL";
    Rgb side_colour = {200, 210, 215};
    switch (target->team) {
      case Team::kPirate:
      case Team::kAlien:
        side = "HOSTILE";
        side_colour = {255, 80, 70};
        break;
      case Team::kPolice:
      case Team::kHunter:
        side = "LAW";
        side_colour = {90, 150, 255};
        break;
      case Team::kTrader:
        side = "TRADER";
        side_colour = {255, 230, 90};
        break;
      default:
        break;
    }
    DrawText(tex, "TARGET", tx, y, static_cast<std::uint32_t>(ts), dim);
    DrawText(tex, side, tx + tw - TextWidth(side, ts), y, static_cast<std::uint32_t>(ts), side_colour);
    y += 9 * ts;
    DrawText(tex, target->blueprint->name, tx, y, static_cast<std::uint32_t>(ts), {240, 250, 255});
    y += 9 * ts;
    const double d = Distance(me.position, target->position);
    const std::string dist = d >= 1000.0 ? Fmt("%.1f KM", d / 1000.0) : Fmt("%.0f M", d);
    DrawText(tex, dist, tx, y, static_cast<std::uint32_t>(ts), {240, 250, 255});
    y += 11 * ts;
    Meter(tex, tx, y, tw, target->shield_max > 0.0 ? target->shield / target->shield_max : 0.0, {70, 160, 255}, "SHIELD", "", ts);
    y += 18 * ts;
    Meter(tex, tx, y, tw, target->hull / target->hull_max, {255, 176, 48}, "HULL", "", ts);
  } else {
    DrawText(tex, "TARGET", tx, y, static_cast<std::uint32_t>(ts), dim);
    DrawText(tex, "NONE", tx, y + 9 * ts, static_cast<std::uint32_t>(ts * 2), dim);
  }

  // Warnings along the bottom
  int fx = 14;
  const int fy = h - 12 * ts;
  const auto badge = [&](const std::string& text, Rgb colour) {
    DrawText(tex, text, fx, fy, static_cast<std::uint32_t>(ts), colour);
    fx += TextWidth(text, ts) + 8 * ts;
  };
  if (game.MassLocked()) {
    badge("MASS LOCK", {255, 160, 70});
  }
  if (game.autopilot()) {
    badge("DOCKING COMPUTER", {255, 235, 90});
  }
  if (game.jump_countdown() > 0.0) {
    badge(Fmt("JUMP %.0f", std::ceil(game.jump_countdown())), {255, 255, 255});
  }
  ScreenFinish(tex);
}

/// Text laid over the view in the vector font: which camera, the latest message, money and rank, and what the ship is
/// doing. There is no frame round the view; this is all that is drawn in the upper part of the picture.
void DrawHud(Image& image, const Game& game, const std::string& message, bool message_fresh, const Palette& pal, CockpitView view, std::uint32_t frame,
             int ts) {
  const int width = static_cast<int>(image.width());
  const std::uint32_t scale = static_cast<std::uint32_t>(ts);
  const Rgb accent = Scale(Glowing(pal.accent), 0.85f);
  const int margin = 8 * ts;
  const char* names[] = {"FRONT VIEW", "REAR VIEW", "LEFT VIEW", "RIGHT VIEW"};
  DrawText(image, names[static_cast<int>(view)], margin, margin, scale, accent);
  char right[96];
  std::snprintf(right, sizeof right, "%.1f CR  %s", static_cast<double>(game.commander().credits) / 10.0, RankName(game.commander().kills));
  DrawText(image, right, width - margin - TextWidth(right, ts), margin, scale, {200, 225, 232});
  if (message_fresh && !message.empty()) {
    // Along the bottom of the view, just above the console, where nothing else is: along the top it ran into the camera
    // name and the money
    const int y = static_cast<int>(image.height()) * 4 / 5 - 22 * ts;
    DrawText(image, message, std::max(margin, (width - TextWidth(message, ts)) / 2), y, scale, {255, 232, 130});
  }
  int y = margin + 12 * ts;
  if (game.launching()) {
    DrawText(image, "LAUNCHING", margin, y, scale, {255, 214, 90});
    y += 10 * ts;
  }
  if (game.landing()) {
    DrawText(image, "TRACTOR BEAM: LANDING", margin, y, scale, {110, 225, 255});
    y += 10 * ts;
  }
  if (game.MassLocked()) {
    DrawText(image, "MASS LOCKED", margin, y, scale, {255, 150, 80});
    y += 10 * ts;
  }
  if (game.autopilot() && (frame / 20) % 2 == 0) {
    DrawText(image, "DOCKING COMPUTER ON", margin, y, scale, {255, 235, 90});
  }
}

/// A small indicator in the top right of the view that shows which way the pilot is looking: the ship seen from above
/// in the middle with its nose up, and a wedge that points to the front, rear, left or right and shows how much of the
/// sky the view takes in. There are no contacts on it; the scanner has those.
void DrawViewPpi(Image& image, CockpitView view, const Palette& pal, int ts) {
  // Half the width of the view, from the focal length of the camera: atan(0.5 / 0.7)
  constexpr double kHalfView = 0.6202;
  const int width = static_cast<int>(image.width());
  const int height = static_cast<int>(image.height());
  const float unit = static_cast<float>(ts);
  const float radius = 17.0f * unit;
  const float cx = static_cast<float>(width) - 8.0f * unit - radius;
  const float cy = 8.0f * unit + 12.0f * unit + radius;
  // The same colour, weight and font as the rest of the text over the view
  const Rgb accent = Scale(Glowing(pal.accent), 0.85f);
  const Rgb ring = Scale(accent, 0.55f);
  const std::uint32_t stroke = static_cast<std::uint32_t>(ts);

  // Which way the pilot is looking, as a turn clockwise from the nose
  double look = 0.0;
  switch (view) {
    case CockpitView::kFront:
      look = 0.0;
      break;
    case CockpitView::kRight:
      look = 1.5707963267948966;
      break;
    case CockpitView::kRear:
      look = 3.141592653589793;
      break;
    case CockpitView::kLeft:
      look = -1.5707963267948966;
      break;
  }
  const auto turn_between = [](double a, double b) {
    double d = a - b;
    while (d > 3.141592653589793) {
      d -= 6.283185307179586;
    }
    while (d < -3.141592653589793) {
      d += 6.283185307179586;
    }
    return d;
  };

  // The glass, and the wedge of the view lit up on it, both with soft edges
  for (int y = std::max(0, static_cast<int>(cy - radius) - 1); y <= std::min(height - 1, static_cast<int>(cy + radius) + 1); ++y) {
    for (int x = std::max(0, static_cast<int>(cx - radius) - 1); x <= std::min(width - 1, static_cast<int>(cx + radius) + 1); ++x) {
      const float dx = static_cast<float>(x) + 0.5f - cx;
      const float dy = static_cast<float>(y) + 0.5f - cy;
      const float dist = std::hypot(dx, dy);
      const float glass = std::clamp(radius - dist + 0.5f, 0.0f, 1.0f);
      if (glass <= 0.0f) {
        continue;
      }
      // How many pixels inside the edge of the wedge this one is
      const double off = kHalfView - std::fabs(turn_between(std::atan2(static_cast<double>(dx), -static_cast<double>(dy)), look));
      const float wedge = std::clamp(0.5f + static_cast<float>(off) * dist, 0.0f, 1.0f);
      const float k = glass * (0.05f + wedge * 0.13f * (1.0f - 0.5f * dist / radius));
      std::uint8_t* p = image.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 3;
      p[0] = static_cast<std::uint8_t>(std::min(255.0f, p[0] + accent.r * k));
      p[1] = static_cast<std::uint8_t>(std::min(255.0f, p[1] + accent.g * k));
      p[2] = static_cast<std::uint8_t>(std::min(255.0f, p[2] + accent.b * k));
    }
  }
  DrawCircle(image, cx, cy, radius, stroke, accent);
  DrawCircle(image, cx, cy, radius * 0.5f, stroke, ring);
  for (const double edge : {look - kHalfView, look + kHalfView}) {
    DrawLine(image, cx, cy, cx + static_cast<float>(std::sin(edge)) * radius, cy - static_cast<float>(std::cos(edge)) * radius, stroke, ring);
  }
  // The four directions inside the rim, in the font of the other text but smaller (half the size, or the smallest there is)
  const std::uint32_t small = static_cast<std::uint32_t>(std::max(1, ts / 2));
  const float sw = 6.0f * static_cast<float>(small);  // the width of a character cell
  const float sh = 7.0f * static_cast<float>(small);  // the height of a capital
  DrawText(image, "F", static_cast<std::int64_t>(cx - sw / 2), static_cast<std::int64_t>(cy - radius + 2.0f * unit), small, accent);
  DrawText(image, "A", static_cast<std::int64_t>(cx - sw / 2), static_cast<std::int64_t>(cy + radius - 2.0f * unit - sh), small, accent);
  DrawText(image, "L", static_cast<std::int64_t>(cx - radius + 2.0f * unit), static_cast<std::int64_t>(cy - sh / 2), small, accent);
  DrawText(image, "R", static_cast<std::int64_t>(cx + radius - 2.0f * unit - sw), static_cast<std::int64_t>(cy - sh / 2), small, accent);

  // The ship, in the middle, nose up
  const Rgb white{255, 255, 255};
  DrawLine(image, cx, cy - 3.0f * unit, cx + 2.5f * unit, cy + 2.5f * unit, stroke, white);
  DrawLine(image, cx + 2.5f * unit, cy + 2.5f * unit, cx - 2.5f * unit, cy + 2.5f * unit, stroke, white);
  DrawLine(image, cx - 2.5f * unit, cy + 2.5f * unit, cx, cy - 3.0f * unit, stroke, white);
}

/// The hyperspace transition: the view rushes towards the centre as if the ship plunged into space, smearing the stars
/// into streaks, and washes out to white at the peak. `amount` is 0 (nothing) to 1 (full zoom).
void WarpZoom(Image& img, double amount, double cx, double cy) {
  const int w = static_cast<int>(img.width());
  const int h = static_cast<int>(img.height());
  const Image source = img;
  const std::uint8_t* src = source.data();
  std::uint8_t* dst = img.data();
  constexpr int kSamples = 24;
  const double zoom = 1.0 + 7.0 * amount;
  const double white = amount * amount * amount * 0.85;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int peak[3] = {0, 0, 0};
      for (int i = 0; i < kSamples; ++i) {
        // Each sample looks at the same ray a little further out, so the image stretches away from the centre
        const double k = std::pow(zoom, static_cast<double>(i) / (kSamples - 1));
        const int sx = static_cast<int>(cx + (x - cx) / k);
        const int sy = static_cast<int>(cy + (y - cy) / k);
        const std::size_t at = (static_cast<std::size_t>(std::clamp(sy, 0, h - 1)) * static_cast<std::size_t>(w) +
                                static_cast<std::size_t>(std::clamp(sx, 0, w - 1))) * 3;
        for (int c = 0; c < 3; ++c) {
          peak[c] = std::max(peak[c], static_cast<int>(src[at + static_cast<std::size_t>(c)]));
        }
      }
      const std::size_t out = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3;
      for (int c = 0; c < 3; ++c) {
        // The brightest thing along the ray wins, so a star becomes a bright streak
        const double v = peak[c];
        dst[out + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(std::clamp(v + (255.0 - v) * white, 0.0, 255.0));
      }
    }
  }
}

/// Bright parts bleed light into the dark round them, as on a camera sensor or the eye: screens, engines, the sun
void Bloom(Image& img, float strength) {
  const int w = static_cast<int>(img.width());
  const int h = static_cast<int>(img.height());
  const int bw = std::max(1, w / 4);
  const int bh = std::max(1, h / 4);
  std::vector<float> a(static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh) * 3, 0.0f);
  std::vector<float> b(a.size(), 0.0f);
  ParallelRows(0, bh, [&](int by_begin, int by_end) {
  for (int by = by_begin; by < by_end; ++by) {
    for (int bx = 0; bx < bw; ++bx) {
      float sum[3] = {0, 0, 0};
      int n = 0;
      for (int y = by * 4; y < std::min(h, by * 4 + 4); ++y) {
        for (int x = bx * 4; x < std::min(w, bx * 4 + 4); ++x) {
          const std::uint8_t* p = img.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3;
          for (int k = 0; k < 3; ++k) {
            sum[k] += std::max(0.0f, static_cast<float>(p[k]) - 150.0f);
          }
          ++n;
        }
      }
      for (int k = 0; k < 3; ++k) {
        a[(static_cast<std::size_t>(by) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(bx)) * 3 + static_cast<std::size_t>(k)] = sum[k] / static_cast<float>(std::max(n, 1));
      }
    }
  }
  });
  const float weights[7] = {1, 2, 3, 4, 3, 2, 1};
  for (int pass = 0; pass < 2; ++pass) {
    ParallelRows(0, bh, [&](int ya, int yb) {
    for (int y = ya; y < yb; ++y) {
      for (int x = 0; x < bw; ++x) {
        for (int k = 0; k < 3; ++k) {
          float s = 0.0f;
          for (int i = -3; i <= 3; ++i) {
            const int xx = std::clamp(x + i, 0, bw - 1);
            s += weights[i + 3] * a[(static_cast<std::size_t>(y) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(xx)) * 3 + static_cast<std::size_t>(k)];
          }
          b[(static_cast<std::size_t>(y) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(k)] = s / 16.0f;
        }
      }
    }
    });
    ParallelRows(0, bh, [&](int ya, int yb) {
    for (int y = ya; y < yb; ++y) {
      for (int x = 0; x < bw; ++x) {
        for (int k = 0; k < 3; ++k) {
          float s = 0.0f;
          for (int i = -3; i <= 3; ++i) {
            const int yy = std::clamp(y + i, 0, bh - 1);
            s += weights[i + 3] * b[(static_cast<std::size_t>(yy) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(k)];
          }
          a[(static_cast<std::size_t>(y) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(k)] = s / 16.0f;
        }
      }
    }
    });
  }
  ParallelRows(0, h, [&](int row_begin, int row_end) {
  for (int y = row_begin; y < row_end; ++y) {
    const float fy = std::clamp(static_cast<float>(y) / 4.0f - 0.5f, 0.0f, static_cast<float>(bh - 1));
    const int y0 = static_cast<int>(fy);
    const int y1 = std::min(y0 + 1, bh - 1);
    const float ty = fy - static_cast<float>(y0);
    for (int x = 0; x < w; ++x) {
      const float fx = std::clamp(static_cast<float>(x) / 4.0f - 0.5f, 0.0f, static_cast<float>(bw - 1));
      const int x0 = static_cast<int>(fx);
      const int x1 = std::min(x0 + 1, bw - 1);
      const float tx = fx - static_cast<float>(x0);
      std::uint8_t* p = img.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3;
      for (int k = 0; k < 3; ++k) {
        const auto at = [&](int xx, int yy) {
          return a[(static_cast<std::size_t>(yy) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(xx)) * 3 + static_cast<std::size_t>(k)];
        };
        const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
        const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
        const float glow = (top + (bottom - top) * ty) * strength;
        p[k] = static_cast<std::uint8_t>(std::min(255.0f, static_cast<float>(p[k]) + glow));
      }
    }
  }
  });
}

/// Soften the stair steps along edges (in the way of FXAA): a pixel whose neighbours differ a lot in brightness is mixed
/// with the pixel across the edge it is on and the ones along it, so a staircase becomes a smooth ramp. Flat areas are
/// left alone, and so are lone bright dots (stars), which would only be dimmed. Only the first `rows` rows are done.
void SoftenEdges(Image& img, int rows) {
  const int w = static_cast<int>(img.width());
  rows = std::min(rows, static_cast<int>(img.height()));
  if (w < 3 || rows < 3) {
    return;
  }
  const std::vector<std::uint8_t> src(img.data(), img.data() + static_cast<std::size_t>(w) * static_cast<std::size_t>(rows) * 3);
  const auto at = [&](int x, int y) { return &src[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3]; };
  const auto luma = [&](int x, int y) {
    const std::uint8_t* p = at(x, y);
    return 77 * static_cast<int>(p[0]) + 150 * static_cast<int>(p[1]) + 29 * static_cast<int>(p[2]);
  };
  ParallelRows(1, rows - 1, [&](int y_begin, int y_end) {
    for (int y = y_begin; y < y_end; ++y) {
      for (int x = 1; x < w - 1; ++x) {
        const int c = luma(x, y);
        const int n = luma(x, y - 1);
        const int s = luma(x, y + 1);
        const int e = luma(x + 1, y);
        const int wl = luma(x - 1, y);
        const int hi = std::max({c, n, s, e, wl});
        const int lo = std::min({c, n, s, e, wl});
        const int range = hi - lo;
        if (range < std::max(14 * 256, hi / 8)) {
          continue;
        }
        if (c == hi && c - std::max(std::max(n, s), std::max(e, wl)) > range * 6 / 10) {
          continue;  // a lone bright dot
        }
        const int nw = luma(x - 1, y - 1);
        const int ne = luma(x + 1, y - 1);
        const int sw = luma(x - 1, y + 1);
        const int se = luma(x + 1, y + 1);
        const int horizontal = std::abs(nw + sw - 2 * wl) + 2 * std::abs(n + s - 2 * c) + std::abs(ne + se - 2 * e);
        const int vertical = std::abs(nw + ne - 2 * n) + 2 * std::abs(wl + e - 2 * c) + std::abs(sw + se - 2 * s);
        // The pixel across the edge, and the two along it
        int ox = 0;
        int oy = 0;
        int ax = 0;
        int ay = 0;
        if (horizontal >= vertical) {
          oy = std::abs(n - c) >= std::abs(s - c) ? -1 : 1;
          ax = 1;
        } else {
          ox = std::abs(wl - c) >= std::abs(e - c) ? -1 : 1;
          ay = 1;
        }
        const std::uint8_t* pc = at(x, y);
        const std::uint8_t* po = at(x + ox, y + oy);
        const std::uint8_t* pa = at(x + ax, y + ay);
        const std::uint8_t* pb = at(x - ax, y - ay);
        std::uint8_t* out = img.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3;
        for (int k = 0; k < 3; ++k) {
          out[k] = static_cast<std::uint8_t>((4 * pc[k] + 2 * po[k] + pa[k] + pb[k] + 4) / 8);
        }
      }
    }
  });
}

/// Average blocks of pixels down to a smaller picture: supersampling, for smooth edges
void Downsample(const Image& big, Image& out, int factor) {
  const std::uint32_t f = static_cast<std::uint32_t>(factor);
  for (std::uint32_t y = 0; y < out.height(); ++y) {
    for (std::uint32_t x = 0; x < out.width(); ++x) {
      std::uint32_t sum[3] = {0, 0, 0};
      for (std::uint32_t j = 0; j < f; ++j) {
        for (std::uint32_t i = 0; i < f; ++i) {
          const std::uint8_t* p = big.data() + (static_cast<std::size_t>(y * f + j) * big.width() + (x * f + i)) * 3;
          sum[0] += p[0];
          sum[1] += p[1];
          sum[2] += p[2];
        }
      }
      out.Set(x, y, {static_cast<std::uint8_t>(sum[0] / (f * f)), static_cast<std::uint8_t>(sum[1] / (f * f)), static_cast<std::uint8_t>(sum[2] / (f * f))});
    }
  }
}

/// How the inside of the cockpit is lit and where it sits (a little off centre as the ship turns)
struct Interior {
  Vec3d offset;
  Vec3d lamp;
  Vec3d sun;
  Palette palette;
};

/// Move a point of the interior a little nearer, so a part laid over another wins the depth test
Vec3d Layer(Vec3d v, int layer) {
  v.z -= 0.004 * layer;
  return v;
}

/// A panel of the interior, shaded by the lamp over the pilot's head and the sun through the windows
void Panel(Canvas& canvas, const Camera& cam, const Interior& in, Vec3d a, Vec3d b, Vec3d c, Vec3d d, Rgb base, int layer = 0, float gloss = 0.25f,
           bool lit = true) {
  a = Layer(a, layer) + in.offset;
  b = Layer(b, layer) + in.offset;
  c = Layer(c, layer) + in.offset;
  d = Layer(d, layer) + in.offset;
  Rgb colour = base;
  if (lit) {
    Vec3d n = Normalized(Cross(b - a, c - a));
    const Vec3d mid = (a + b + c + d) * 0.25;
    if (Dot(n, mid) > 0.0) {
      n = -n;
    }
    const double light = 0.30 + 0.45 * std::max(0.0, Dot(n, in.lamp)) + 0.5 * std::max(0.0, Dot(n, in.sun));
    const double spec = gloss * std::pow(std::max(0.0, Dot(n, Normalized(in.lamp - Normalized(mid)))), 24.0);
    colour = Scale(base, static_cast<float>(light));
    colour = {static_cast<std::uint8_t>(std::min(255.0, colour.r + 255.0 * spec)), static_cast<std::uint8_t>(std::min(255.0, colour.g + 255.0 * spec)),
              static_cast<std::uint8_t>(std::min(255.0, colour.b + 255.0 * spec))};
  }
  FillPolygon(canvas, cam, {a, b, c, d}, colour, 0);
}

/// A screen: its picture mapped onto a quadrilateral of the console
void ScreenQuad(Canvas& canvas, const Camera& cam, const Interior& in, Vec3d a, Vec3d b, Vec3d c, Vec3d d, const Image& tex, int layer) {
  const Vec3d corner[4] = {Layer(a, layer) + in.offset, Layer(b, layer) + in.offset, Layer(c, layer) + in.offset, Layer(d, layer) + in.offset};
  float p[4][2];
  float inv[4];
  for (int k = 0; k < 4; ++k) {
    if (!Project(cam, corner[k], &p[k][0], &p[k][1])) {
      return;
    }
    inv[k] = static_cast<float>(1.0 / corner[k].z);
  }
  const float screen_width = std::hypot(p[1][0] - p[0][0], p[1][1] - p[0][1]);
  const float minify = static_cast<float>(tex.width()) / std::max(1.0f, screen_width);
  const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
  const int first[3] = {0, 1, 2};
  const int second[3] = {0, 2, 3};
  for (const int* tri : {first, second}) {
    const float q[3][2] = {{p[tri[0]][0], p[tri[0]][1]}, {p[tri[1]][0], p[tri[1]][1]}, {p[tri[2]][0], p[tri[2]][1]}};
    const float qi[3] = {inv[tri[0]], inv[tri[1]], inv[tri[2]]};
    const float quv[3][2] = {{uv[tri[0]][0], uv[tri[0]][1]}, {uv[tri[1]][0], uv[tri[1]][1]}, {uv[tri[2]][0], uv[tri[2]][1]}};
    canvas.TexturedTriangle(q, qi, quv, tex, minify);
  }
}

/// The cockpit itself: a low console along the bottom fifth of the picture with three screens and a row of warning
/// lights. Everything above it is left clear, as the view out of the ship.
/// Everything is in the pilot's own space: x right, y up, z forward, the eye at the origin.
void DrawInterior(Canvas& canvas, const Camera& cam, const Interior& in, const Game& game, const Entity& me, const Image& left, const Image& centre,
                  const Image& right, std::uint32_t frame) {
  // The console is laid out for a 16:9 picture, where its screens run down to the bottom edge. Its height is scaled by
  // the shape of the picture, so on a wider or taller window it still sits at the same rows (and the screens keep their
  // proportions) and there is no bare panel under the screens.
  const double vscale = 3.1111 * cam.cy / cam.f;
  const auto V = [vscale](double x, double y, double z) { return Vec3d{x, y * vscale, z}; };
  const Rgb hull = in.palette.hull;
  const Rgb accent = Glowing(in.palette.accent);
  // The paint is the ship's own colour, darkened: panels, and a lighter shade for the trim
  const Rgb panel = Mix(Scale(hull, 0.30f), {12, 14, 18}, 0.40f);
  const Rgb panel_light = Mix(Scale(hull, 0.55f), {20, 22, 26}, 0.30f);
  const Rgb dark{12, 14, 18};
  const Rgb trim = Scale(accent, 0.9f);

  // Only the console is drawn: a low desk along the bottom fifth of the picture. Above it the view is clear, with no
  // frame or pillars. Row 0.8 of the picture is y / z = -0.3214 for a camera centred at 0.4 of the height (and `V`
  // scales that to the shape of the picture).
  // The rim along the top edge, facing the pilot
  Panel(canvas, cam, in, V(-3, -0.3214, 1.0), V(3, -0.3214, 1.0), V(3, -0.3333, 1.0), V(-3, -0.3333, 1.0), panel_light, 0, 0.6f);
  // The face of the desk, sloping up towards the pilot
  Panel(canvas, cam, in, V(-3, -0.3333, 1.0), V(3, -0.3333, 1.0), V(3, -0.5233, 0.85), V(-3, -0.5233, 0.85), panel, 0, 0.35f);
  // A thin lit line where the rim meets the face
  Panel(canvas, cam, in, V(-3, -0.3333, 1.0), V(3, -0.3333, 1.0), V(3, -0.3355, 0.9975), V(-3, -0.3355, 0.9975), trim, 2, 0.0f, false);

  // Three screens, each in a bezel, lying in the face of the desk
  const Image* pictures[3] = {&left, &centre, &right};
  const double centres[3] = {-0.44, 0.0, 0.44};
  const double width = 0.36;
  const auto on_face = [](double s) { return Vec3d{0.0, -0.3333 - 0.19 * s, 1.0 - 0.15 * s}; };
  for (int i = 0; i < 3; ++i) {
    const double x0 = centres[i] - width / 2.0;
    const double x1 = centres[i] + width / 2.0;
    const Vec3d lo = on_face(0.07);
    const Vec3d hi = on_face(0.55);
    const Vec3d blo = on_face(0.035);
    const Vec3d bhi = on_face(0.575);
    Panel(canvas, cam, in, V(x0 - 0.015, blo.y, blo.z), V(x1 + 0.015, blo.y, blo.z), V(x1 + 0.015, bhi.y, bhi.z), V(x0 - 0.015, bhi.y, bhi.z), dark, 1, 0.6f);
    ScreenQuad(canvas, cam, in, V(x0, hi.y, hi.z), V(x1, hi.y, hi.z), V(x1, lo.y, lo.z), V(x0, lo.y, lo.z), *pictures[i], 2);
  }

  // Rows of square red and white buttons: a narrow strip between the screens, and a block out beyond each of the outer
  // screens that runs off the edge of the picture. The ones in the strips and on the inner edge of the blocks show
  // something about the ship (a feature fitted or switched on, a warning) and stay lit while it is true. The rest do
  // nothing and are there for the look: they light up for a while now and then, at random.
  {
    const Equipment& fitted = game.commander().equipment;
    struct Status {
      bool on;
      bool red;
    };
    const std::array<Status, 3> left_strip = {{
        {game.autopilot(), false},                                                       // docking computer flying
        {game.flight_mode() == FlightMode::kNewtonian && game.assist(), false},          // flight assist on
        {game.flight_mode() == FlightMode::kNewtonian, true},                            // Newtonian flight
    }};
    const std::array<Status, 3> right_strip = {{
        {game.missile_lock() != kNoEntity, true},                                        // a missile is locked on
        {game.target() != kNoEntity, false},                                             // a target is chosen
        {game.MassLocked(), true},                                                       // the hyperdrive is blocked
    }};
    const std::array<Status, 3> left_edge = {{
        {fitted.ecm, false},                                                             // ECM fitted
        {fitted.fuel_scoop, false},                                                      // fuel scoop fitted
        {fitted.escape_pod, false},                                                      // escape pod fitted
    }};
    const std::array<Status, 3> right_edge = {{
        {me.shield_max > 0.0 && me.shield > 0.5 * me.shield_max, false},                 // shields up
        {fitted.energy_bomb, true},                                                      // energy bomb fitted
        {me.hull < 0.6 * me.hull_max, true},                                             // the hull is damaged
    }};

    const double bw = 0.025;  // the width of a button
    // The height of a button along the face of the desk. The face slopes away from the pilot, and a step down it moves
    // the picture by 0.24 / z^2 where a step across moves it by 1 / z, so this makes the button square on the screen.
    const auto along = [&](double s0, double across) { return across * (1.0 - 0.15 * s0) / (0.24 * vscale); };
    const double pitch = 0.155;  // from one row to the next, along the face
    const Rgb red_off{70, 14, 14};
    const Rgb red_on{255, 64, 52};
    const Rgb white_off{105, 110, 118};
    const Rgb white_on{255, 255, 255};
    // The plates the buttons are set in
    const Vec3d plate_top = on_face(0.02);
    const Vec3d plate_bottom = on_face(0.58);
    for (const double side : {-1.0, 1.0}) {
      for (const std::array<double, 2> span : {std::array<double, 2>{0.20, 0.24}, std::array<double, 2>{0.64, 0.95}}) {
        const double x0 = side * span[0];
        const double x1 = side * span[1];
        Panel(canvas, cam, in, V(x0, plate_top.y, plate_top.z), V(x1, plate_top.y, plate_top.z), V(x1, plate_bottom.y, plate_bottom.z),
              V(x0, plate_bottom.y, plate_bottom.z), panel_light, 1, 0.3f);
      }
    }
    int index = 0;
    // A button at across-position x in a row. `status` is what it shows, or null for one that lights at random.
    const auto button = [&](double x, int row, const Status* status) {
      const std::uint32_t h = Hash32(static_cast<std::uint32_t>(index++) * 0x9E3779B1U + 77U);
      bool red = (h & 2U) != 0;
      bool lit = false;
      if (status != nullptr) {
        red = status->red;
        lit = status->on;
      } else {
        // Each button has its own slow beat, and at each beat it is lit by chance, but not often
        const std::uint32_t period = 120 + (h >> 4) % 300;
        const std::uint32_t phase = (h >> 12) % period;
        lit = Hash32(h ^ (((frame + phase) / period) * 0x85EBCA6BU)) % 100 < 14;
      }
      const double s0 = 0.06 + row * pitch;
      const double ds = along(s0, bw);
      const Vec3d a = on_face(s0);
      const Vec3d b = on_face(s0 + ds);
      const double e = 0.0035;  // the dark rim round the button
      const Vec3d ra = on_face(s0 - along(s0, e));
      const Vec3d rb = on_face(s0 + ds + along(s0, e));
      Panel(canvas, cam, in, V(x - bw / 2 - e, ra.y, ra.z), V(x + bw / 2 + e, ra.y, ra.z), V(x + bw / 2 + e, rb.y, rb.z),
            V(x - bw / 2 - e, rb.y, rb.z), dark, 2, 0.0f, false);
      const Rgb colour = red ? (lit ? red_on : red_off) : (lit ? white_on : white_off);
      Panel(canvas, cam, in, V(x - bw / 2, a.y, a.z), V(x + bw / 2, a.y, a.z), V(x + bw / 2, b.y, b.z), V(x - bw / 2, b.y, b.z), colour, 3, 0.0f, false);
      if (lit) {
        const Vec3d mid = V(x, 0.5 * (a.y + b.y), 0.5 * (a.z + b.z)) + in.offset;
        float sx = 0.0f;
        float sy = 0.0f;
        if (Project(cam, mid, &sx, &sy)) {
          canvas.Glow(sx, sy, static_cast<float>(0.05 * cam.f), colour.r, colour.g, colour.b, 0.35f);
        }
      }
    };
    for (int row = 0; row < 3; ++row) {
      const std::size_t r = static_cast<std::size_t>(row);
      button(-0.22, row, &left_strip[r]);
      button(0.22, row, &right_strip[r]);
      button(-0.655, row, &left_edge[r]);
      button(0.655, row, &right_edge[r]);
      for (int column = 1; column < 6; ++column) {
        button(-(0.655 + 0.04 * column), row, nullptr);
        button(0.655 + 0.04 * column, row, nullptr);
      }
    }
  }

  // A row of lights along the rim, each telling the state of something
  const bool hostile_near = [&] {
    for (const Entity& e : game.world().entities()) {
      if (e.alive && e.kind == EntityKind::kShip && (e.team == Team::kPirate || e.team == Team::kAlien) && Distance(e.position, me.position) < 6000.0) {
        return true;
      }
    }
    return false;
  }();
  const bool blink = (frame / 15) % 2 == 0;
  const Rgb off{22, 28, 32};
  const Rgb states[10] = {
      !me.has_ecm ? off : (me.energy >= 20.0 ? Rgb{80, 235, 120} : Rgb{255, 190, 60}),
      me.missiles > 0 ? Rgb{80, 235, 120} : off,
      game.missile_lock() != kNoEntity ? Rgb{255, 70, 60} : off,
      game.autopilot() ? Rgb{255, 235, 90} : off,
      (game.flight_mode() == FlightMode::kNewtonian && game.assist()) ? Rgb{90, 200, 255} : off,
      game.MassLocked() ? Rgb{255, 150, 60} : off,
      hostile_near ? (blink ? Rgb{255, 60, 50} : Rgb{90, 25, 20}) : off,
      me.shield_max > 0.0 && me.shield < 0.3 * me.shield_max ? Rgb{80, 150, 255} : off,
      me.hull < 0.4 * me.hull_max ? (blink ? Rgb{255, 60, 50} : Rgb{90, 25, 20}) : off,
      game.jump_countdown() > 0.0 ? Rgb{255, 255, 255} : off,
  };
  for (int i = 0; i < 10; ++i) {
    const double x = -0.9 + 0.2 * i;
    Panel(canvas, cam, in, V(x, -0.3315, 1.0), V(x + 0.07, -0.3315, 1.0), V(x + 0.07, -0.3232, 1.0), V(x, -0.3232, 1.0), states[i], 2, 0.0f, false);
  }
}

}  // namespace

const Model& Cockpit::ModelFor(const Blueprint& blueprint) {
  auto it = models_.find(&blueprint);
  if (it == models_.end()) {
    it = models_.emplace(&blueprint, BlueprintToModel(blueprint)).first;
  }
  return it->second;
}

void Cockpit::Notice(const Game& game, const std::vector<Event>& events, const std::vector<std::string>& messages) {
  const EntityId me = game.world().player();
  for (const Event& e : events) {
    if (e.kind == EventKind::kDestroyed || e.kind == EventKind::kMissileDetonated) {
      Flash f;
      f.position = e.position;
      f.size = e.blueprint != nullptr ? std::max(8.0, 3.2 * e.blueprint->scale) : 20.0;
      if (e.kind == EventKind::kMissileDetonated) {
        f.size = 40.0;
      }
      f.seed = static_cast<std::uint32_t>(e.a) * 7919U + static_cast<std::uint32_t>(flashes_.size());
      flashes_.push_back(f);
    } else if (e.kind == EventKind::kShieldHit && e.a == me) {
      shield_flash_ = 0.25;
    } else if (e.kind == EventKind::kHullHit && e.a == me) {
      hull_flash_ = 0.35;
    }
  }
  if (!messages.empty()) {
    message_ = messages.back();
    message_age_ = 0.0;
  }
}

void Cockpit::Advance(double seconds) {
  for (Flash& f : flashes_) {
    f.age += seconds;
  }
  flashes_.erase(std::remove_if(flashes_.begin(), flashes_.end(), [](const Flash& f) { return f.age > 1.4; }), flashes_.end());
  message_age_ += seconds;
  shield_flash_ = std::max(0.0, shield_flash_ - seconds);
  hull_flash_ = std::max(0.0, hull_flash_ - seconds);
}

void Cockpit::Draw(Image& image, const Game& game, std::uint32_t frame) {
  const int ss = std::clamp(supersample_, 1, 4);
  if (ss == 1 || image.width() < 160 || image.height() < 120) {
    Render(image, game, frame);
    return;
  }
  // Draw bigger and shrink: the edges of everything come out smooth
  Image big(image.width() * static_cast<std::uint32_t>(ss), image.height() * static_cast<std::uint32_t>(ss));
  Render(big, game, frame);
  Downsample(big, image, ss);
}

void Cockpit::Render(Image& image, const Game& game, std::uint32_t frame) {
  image.Clear({0, 0, 0});
  const int width = static_cast<int>(image.width());
  const int height = static_cast<int>(image.height());
  const Entity* me = game.player();
  if (me == nullptr || game.mode() != GameMode::kFlight || width < 160 || height < 120) {
    return;
  }
  // The whole picture is the view out of the windows; the cockpit is drawn over it
  const int view_height = height;
  Canvas canvas(image, view_height);
  const int ts = std::max(1, height / 270);

  // The cockpit shifts a little as the ship turns, as if the pilot is pressed in the seat
  const Vec3d sway_target{(-me->spin.z * 0.020 + me->spin.y * 0.010), me->spin.x * 0.016, 0.0};
  sway_ = sway_ + (sway_target - sway_) * 0.15;

  // The camera: the ship's own orientation, turned for the side and rear views
  double yaw = 0.0;
  switch (view_) {
    case CockpitView::kFront:
      yaw = 0.0;
      break;
    case CockpitView::kRear:
      yaw = 3.141592653589793;
      break;
    case CockpitView::kLeft:
      yaw = -1.5707963267948966;
      break;
    case CockpitView::kRight:
      yaw = 1.5707963267948966;
      break;
  }
  Camera cam;
  cam.q = Normalized(me->orientation * FromAxisAngle({0.0, 1.0, 0.0}, yaw));
  cam.origin = me->position;
  cam.f = 0.7 * width;
  cam.cx = width / 2.0;
  cam.cy = view_height * 0.4;

  // The direction to the sun lights everything
  Vec3d light_world{0.4, 0.8, -0.4};
  for (const Entity& e : game.world().entities()) {
    if (e.kind == EntityKind::kSun) {
      light_world = Normalized(e.position - me->position);
    }
  }
  const Vec3d light = Rotate(Conjugate(cam.q), light_world);

  // Gas clouds on the sky of this system, behind the stars
  if (nebula_on_) {
    if (!nebula_ || nebula_->seed() != game.system().seed) {
      nebula_.emplace(game.system().seed);
    }
    nebula_->Paint(image, cam.q, cam.f, cam.cx, cam.cy);
  }

  // Fixed stars
  for (std::uint32_t i = 0; i < 500; ++i) {
    const std::uint32_t h = Hash32(i * 2654435761U + 17U);
    const double z = (static_cast<double>(h & 0xFFFFU) / 32767.5) - 1.0;
    const double phi = static_cast<double>((h >> 16) & 0xFFFFU) / 65535.0 * 6.283185307179586;
    const double s = std::sqrt(std::max(0.0, 1.0 - z * z));
    const Vec3d p = Rotate(Conjugate(cam.q), Vec3d{s * std::cos(phi), z, s * std::sin(phi)});
    float sx = 0.0f;
    float sy = 0.0f;
    if (Project(cam, p * 1000.0, &sx, &sy)) {
      const float level = 0.35f + 0.65f * static_cast<float>(Hash32(h) & 255U) / 255.0f;
      const Rgb c = Scale((h >> 8) % 5 == 0 ? Rgb{190, 210, 255} : Rgb{255, 255, 255}, level);
      canvas.Set(static_cast<int>(sx), static_cast<int>(sy), c);
      if (level > 0.85f) {
        canvas.Set(static_cast<int>(sx) + 1, static_cast<int>(sy), Scale(c, 0.5f));
        canvas.Set(static_cast<int>(sx), static_cast<int>(sy) + 1, Scale(c, 0.5f));
      }
    }
  }

  // The planet and the sun
  for (const Entity& e : game.world().entities()) {
    if (e.kind == EntityKind::kPlanet || e.kind == EntityKind::kSun) {
      DrawBody(canvas, cam, e, ToCamera(cam, e.position), light, game.system().seed);
    }
  }

  // The docking bay, while the ship is inside it
  if (const Entity* station = game.world().FindKind(EntityKind::kStation)) {
    const Bay bay = BayOf(*station);
    const Vec3d rel = me->position - bay.mouth;
    const double along = Dot(rel, bay.axis);
    if (along < 30.0 && along > -bay.depth - 50.0 && std::fabs(Dot(rel, bay.right)) < bay.half_width * 1.05 &&
        std::fabs(Dot(rel, bay.up)) < bay.half_height * 1.05) {
      DrawBay(canvas, cam, bay, game.landing() ? std::fmod(game.landing_progress() * 7.0, 1.0) : -1.0);
    }
  }

  // Ships, stations, rocks, canisters and missiles
  for (const Entity& e : game.world().entities()) {
    if (!e.alive || e.blueprint == nullptr || e.id == me->id) {
      continue;
    }
    DrawEntity(canvas, cam, e, ModelFor(*e.blueprint), light);
  }

  // Laser bolts
  for (const Projectile& p : game.world().projectiles()) {
    const Vec3d a = ToCamera(cam, p.position);
    const Vec3d b = ToCamera(cam, p.position - p.velocity * 0.05);
    float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
    if (Project(cam, a, &ax, &ay) && Project(cam, b, &bx, &by)) {
      const bool mine = p.owner == me->id;
      const Rgb c = mine ? Rgb{60, 255, 120} : Rgb{255, 70, 50};
      canvas.Line(ax, ay, bx, by, c);
      canvas.Line(ax + 1, ay, bx + 1, by, Scale(c, 0.6f), true);
    }
  }

  // Dust streaming past shows speed and direction
  {
    constexpr double kTile = 4000.0;
    const std::uint32_t dust_count = 160;
    for (std::uint32_t i = 0; i < dust_count; ++i) {
      const std::uint32_t h = Hash32(i * 40503U + 99U);
      const Vec3d offset{static_cast<double>(h & 0xFFFU) / 4095.0 * kTile, static_cast<double>((h >> 12) & 0xFFFU) / 4095.0 * kTile,
                         static_cast<double>(Hash32(h) & 0xFFFU) / 4095.0 * kTile};
      Vec3d rel = offset - me->position;
      rel.x -= kTile * std::floor(rel.x / kTile) + kTile / 2.0;
      rel.y -= kTile * std::floor(rel.y / kTile) + kTile / 2.0;
      rel.z -= kTile * std::floor(rel.z / kTile) + kTile / 2.0;
      const Vec3d now = Rotate(Conjugate(cam.q), rel);
      const Vec3d before = Rotate(Conjugate(cam.q), rel + me->velocity * 0.05);
      float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
      if (Project(cam, now, &ax, &ay) && Project(cam, before, &bx, &by)) {
        const int ix = static_cast<int>(ax);
        const int iy = static_cast<int>(ay);
        if (ix >= 0 && iy >= 0 && ix < width && iy < view_height && canvas.depth(ix, iy) < static_cast<float>(1.0 / now.z)) {
          canvas.Line(ax, ay, bx, by, {150, 160, 175});
        }
      }
    }
  }

  // Explosions: a flash that grows and fades, with sparks
  for (const Flash& f : flashes_) {
    const Vec3d c = ToCamera(cam, f.position);
    if (c.z < kNear) {
      continue;
    }
    const double t = f.age / 1.4;
    const double radius = f.size * (0.4 + 3.0 * t);
    const float sr = static_cast<float>(cam.f * radius / c.z);
    float sx = 0.0f;
    float sy = 0.0f;
    Project(cam, c, &sx, &sy);
    const float fade = static_cast<float>(std::max(0.0, 1.0 - t));
    canvas.Glow(sx, sy, std::max(2.0f, sr), 255.0f, 170.0f, 80.0f, 1.2f * fade);
    for (int k = 0; k < 18; ++k) {
      const std::uint32_t h = Hash32(f.seed * 31U + static_cast<std::uint32_t>(k));
      const float angle = static_cast<float>(h & 0xFFFFU) / 65535.0f * 6.2831853f;
      const float length = sr * (0.8f + 1.8f * static_cast<float>((h >> 16) & 255U) / 255.0f) * static_cast<float>(0.3 + t);
      canvas.Line(sx + std::cos(angle) * sr * 0.2f, sy + std::sin(angle) * sr * 0.2f, sx + std::cos(angle) * length,
                  sy + std::sin(angle) * length, {static_cast<std::uint8_t>(255 * fade), static_cast<std::uint8_t>(190 * fade), static_cast<std::uint8_t>(90 * fade)},
                  true);
    }
  }

  // Hyperspace: zoom into space on the way out, and back out of it on arrival
  if (game.hyperspace_warp() > 0.0) {
    WarpZoom(image, game.hyperspace_warp(), cam.cx, cam.cy);
  }

  // Smooth the edges of everything drawn so far (the world and its ships); the brackets, the HUD and the console come after,
  // so their lines stay crisp. A supersampled picture is smoothed by the shrinking instead.
  if (false && antialias_ && supersample_ <= 1) {
    SoftenEdges(image, view_height);
  }

  // Target brackets and the point to aim at
  const Entity* target = game.world().Find(game.target());
  if (target != nullptr && target->alive) {
    const Vec3d c = ToCamera(cam, target->position);
    float sx = 0.0f;
    float sy = 0.0f;
    if (Project(cam, c, &sx, &sy)) {
      const float r = std::max(8.0f, static_cast<float>(cam.f * target->radius / c.z));
      const bool locked = game.missile_lock() == target->id;
      const Rgb bc = locked ? Rgb{255, 80, 80} : Rgb{80, 220, 255};
      const float arm = r * 0.4f;
      for (const int sxn : {-1, 1}) {
        for (const int syn : {-1, 1}) {
          const float x = sx + static_cast<float>(sxn) * r;
          const float y = sy + static_cast<float>(syn) * r;
          canvas.Line(x, y, x - static_cast<float>(sxn) * arm, y, bc);
          canvas.Line(x, y, x, y - static_cast<float>(syn) * arm, bc);
        }
      }
      char text[96];
      const double d = Distance(me->position, target->position);
      if (d >= 1000.0) {
        std::snprintf(text, sizeof text, "%s %.1f KM", target->blueprint->name.c_str(), d / 1000.0);
      } else {
        std::snprintf(text, sizeof text, "%s %.0f M", target->blueprint->name.c_str(), d);
      }
      DrawText(image, text, static_cast<std::int64_t>(sx - r), static_cast<std::int64_t>(sy + r + 4), static_cast<std::uint32_t>(ts), bc);
      // Where to point the lasers to hit it
      const Vec3d lead = LeadDirection(me->position, me->velocity, target->position, target->velocity, kBoltSpeed);
      const Vec3d lead_cam = Rotate(Conjugate(cam.q), lead * Distance(me->position, target->position));
      float lx = 0.0f;
      float ly = 0.0f;
      if (Project(cam, lead_cam, &lx, &ly)) {
        canvas.Ellipse(lx, ly, 6.0f, 6.0f, {255, 220, 60});
        canvas.Line(lx - 9, ly, lx + 9, ly, {255, 220, 60});
        canvas.Line(lx, ly - 9, lx, ly + 9, {255, 220, 60});
      }
    }
  }

  // Threat markers: only what is attacking the ship gets one. A yellow diamond goes round each attacker in view, so it is
  // easy to pick out against the stars even when it is only a dot; one that is off the screen or behind gets a yellow
  // arrow at the edge of the view pointing the way to it. Traders, police that are not after you, stations, rocks and
  // canisters get nothing. The chosen target has its own bigger brackets below. An attacker hidden behind the planet or
  // something nearer gets no diamond.
  {
    const std::uint32_t stroke = static_cast<std::uint32_t>(std::max(1, ts));
    const float unit = static_cast<float>(ts);
    const Rgb yellow{255, 220, 60};
    for (const Entity& e : game.world().entities()) {
      if (!e.alive || e.blueprint == nullptr || e.id == me->id || !IsThreat(e, me->id)) {
        continue;
      }
      const Vec3d c = ToCamera(cam, e.position);
      float sx = 0.0f;
      float sy = 0.0f;
      const bool projected = Project(cam, c, &sx, &sy);
      const bool in_view = projected && sx >= 0.0f && sy >= 0.0f && sx < static_cast<float>(width) &&
                           sy < static_cast<float>(view_height) * 0.8f;
      if (in_view) {
        if (e.id == game.target()) {
          continue;  // it has the target brackets
        }
        const int ix = static_cast<int>(sx);
        const int iy = static_cast<int>(sy);
        if (canvas.depth(ix, iy) > static_cast<float>(1.0 / std::max(1.0, c.z - 1.5 * e.radius))) {
          continue;
        }
        const float on_screen = static_cast<float>(cam.f * e.radius / std::max(1.0, Length(c)));
        const float r = std::max(5.5f * unit, on_screen * 1.25f + 2.0f * unit);
        const float px[4] = {sx, sx + r, sx, sx - r};
        const float py[4] = {sy - r, sy, sy + r, sy};
        for (int i = 0; i < 4; ++i) {
          const int j = (i + 1) % 4;
          // Each corner of the diamond, with a short arm along both of its edges
          DrawLine(image, px[i], py[i], px[i] + (px[j] - px[i]) * 0.35f, py[i] + (py[j] - py[i]) * 0.35f, stroke, yellow);
          DrawLine(image, px[j], py[j], px[j] + (px[i] - px[j]) * 0.35f, py[j] + (py[i] - py[j]) * 0.35f, stroke, yellow);
        }
      } else {
        double dx = c.x;
        double dy = -c.y;
        const double length = std::hypot(dx, dy);
        if (length < 1e-6) {
          dx = 0.0;
          dy = -1.0;
        } else {
          dx /= length;
          dy /= length;
        }
        const float cy = static_cast<float>(view_height) * 0.4f;
        const float ax = static_cast<float>(cam.cx) + static_cast<float>(dx) * static_cast<float>(width) * 0.42f;
        const float ay = cy + static_cast<float>(dy) * static_cast<float>(view_height) * 0.34f;
        const float fx = static_cast<float>(dx);
        const float fy = static_cast<float>(dy);
        const float len = 11.0f * unit;
        const float half = 6.0f * unit;
        const float bx = ax - fx * len;
        const float by = ay - fy * len;
        DrawLine(image, ax, ay, bx - fy * half, by + fx * half, stroke, yellow);
        DrawLine(image, ax, ay, bx + fy * half, by - fx * half, stroke, yellow);
        DrawLine(image, bx - fy * half, by + fx * half, bx + fy * half, by - fx * half, stroke, yellow);
      }
    }
  }

  // Crosshair
  {
    const float x = static_cast<float>(cam.cx);
    const float y = static_cast<float>(cam.cy);
    const Rgb c{90, 255, 150};
    canvas.Line(x - 22, y, x - 7, y, c);
    canvas.Line(x + 7, y, x + 22, y, c);
    canvas.Line(x, y - 22, x, y - 7, c);
    canvas.Line(x, y + 7, x, y + 22, c);
  }

  // A tint at the edge of the view when the ship is hit
  const auto tint = [&](double amount, Rgb c) {
    if (amount <= 0.0) {
      return;
    }
    const int band = std::max(4, height / 40);
    for (int i = 0; i < band; ++i) {
      const float k = static_cast<float>(amount) * (1.0f - static_cast<float>(i) / static_cast<float>(band));
      const float r = c.r * k;
      const float g = c.g * k;
      const float b = c.b * k;
      for (int x = 0; x < width; ++x) {
        canvas.Add(x, i, r, g, b);
        canvas.Add(x, view_height - 1 - i, r, g, b);
      }
      for (int y = 0; y < view_height; ++y) {
        canvas.Add(i, y, r, g, b);
        canvas.Add(width - 1 - i, y, r, g, b);
      }
    }
  };
  tint(shield_flash_ * 3.0, {60, 120, 255});
  tint(hull_flash_ * 3.0, {255, 50, 30});

  // ---- The cockpit ----
  // It is drawn over the view, so it needs a depth buffer of its own. The ship's own colours give the paint.
  canvas.ResetDepth();
  const Palette palette = PaletteFor(ModelFor(*me->blueprint));
  const bool fresh = message_age_ < 6.0;
  Image left_screen(640, 196);
  Image centre_screen(640, 196);
  Image right_screen(640, 196);
  DrawSystemsScreen(left_screen, game, *me, palette);
  DrawScannerScreen(centre_screen, game, *me, palette, frame);
  DrawNavScreen(right_screen, game, *me, palette);
  Interior interior;
  interior.offset = sway_;
  interior.lamp = Normalized(Vec3d{0.15, 1.0, -0.35});
  interior.sun = light * 0.8;
  interior.palette = palette;
  DrawInterior(canvas, cam, interior, game, *me, left_screen, centre_screen, right_screen, frame);

  // Text over the view: this and the countdown to a jump are all there is above the console
  DrawHud(image, game, message_, fresh, palette, view_, frame, ts);
  DrawViewPpi(image, view_, palette, ts);
  if (game.jump_countdown() > 0.0) {
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "HYPERSPACE %.0f", std::ceil(game.jump_countdown()));
    const std::uint32_t big = static_cast<std::uint32_t>(ts * 3);
    const std::int64_t w = static_cast<std::int64_t>(std::strlen(buffer)) * 6 * big;
    DrawText(image, buffer, (width - w) / 2, view_height / 5, big, {255, 255, 255});
  }

  // The finish: a soft glow from the bright parts
  if (bloom_) {
    Bloom(image, 0.45f);
  }
}

PlanetPortraitInfo DescribePlanet(std::uint32_t system_seed) {
  const PlanetStyle style = StyleFor(system_seed);
  return {style.name, style.ring_outer > 0.0f, style.glow_thickness > 0.0f};
}

void DrawPlanetPortrait(Image& image, std::uint32_t system_seed, double turn) {
  image.Clear({0, 0, 0});
  const int width = static_cast<int>(image.width());
  const int height = static_cast<int>(image.height());
  if (width < 16 || height < 16) {
    return;
  }
  DrawNebula(image, system_seed, 0);
  DrawStars(image, 0);
  const PlanetStyle style = StyleFor(system_seed);
  Canvas canvas(image, height);
  Camera cam;
  // Look down on the rings a little, and turn the world to show a different face
  cam.q = FromAxisAngle({0.0, 1.0, 0.0}, turn) * FromAxisAngle({1.0, 0.0, 0.0}, -0.35);
  cam.origin = {0.0, 0.0, 0.0};
  cam.f = 0.7 * width;
  cam.cx = width / 2.0;
  cam.cy = height / 2.0;
  Entity body;
  body.kind = EntityKind::kPlanet;
  body.radius = 1000.0;
  // Fit the planet, with its rings, in the picture
  const double reach = style.ring_outer > 0.0f ? body.radius * style.ring_outer : body.radius;
  const double wanted = (style.ring_outer > 0.0f ? 0.46 : 0.40) * std::min(width, height);
  const double distance = reach * std::sqrt(1.0 + (cam.f / wanted) * (cam.f / wanted));
  const Vec3d light = Normalized(Vec3d{-0.7, 0.35, -0.55});
  DrawBody(canvas, cam, body, {0.0, 0.0, distance}, light, system_seed);
}

}  // namespace coriolis
