#include "vehicles.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "softrender/model_builder.h"

namespace vehicles {

namespace {

using softrender::Model;
using softrender::ModelBuilder;
using softrender::Rgb;
using softrender::Vec3;

constexpr float kTau = 6.28318530718f;

/// A closed convex solid between two loops with the same number of corners, such as a tapered hull
void Loft(ModelBuilder& b, const std::vector<Vec3>& lower, const std::vector<Vec3>& upper, Rgb sides, Rgb cap_top, Rgb cap_bottom) {
  Vec3 c{0.0f, 0.0f, 0.0f};
  for (std::size_t i = 0; i < lower.size(); ++i) {
    c = {c.x + lower[i].x + upper[i].x, c.y + lower[i].y + upper[i].y, c.z + lower[i].z + upper[i].z};
  }
  const float n = 2.0f * static_cast<float>(lower.size());
  b.Centre(c.x / n, c.y / n, c.z / n);
  std::vector<std::size_t> low;
  std::vector<std::size_t> high;
  for (std::size_t i = 0; i < lower.size(); ++i) {
    low.push_back(b.Vertex(lower[i].x, lower[i].y, lower[i].z));
    high.push_back(b.Vertex(upper[i].x, upper[i].y, upper[i].z));
  }
  b.Band(low, high, {sides});
  b.Poly(high, cap_top);
  b.Poly(low, cap_bottom);
}

/// A box given by its corners, with the same colour all round
void Block(ModelBuilder& b, Vec3 low, Vec3 high, Rgb colour) { b.Box(low.x, low.y, low.z, high.x, high.y, high.z, colour, colour, false); }

/// A thin glowing strip between two points, laid on a surface facing up
void Strip(ModelBuilder& b, Vec3 from, Vec3 to, float half_width, Rgb glow) {
  float dx = to.z - from.z;
  float dz = from.x - to.x;
  const float length = std::sqrt(dx * dx + dz * dz);
  dx = dx / length * half_width;
  dz = dz / length * half_width;
  b.Centre((from.x + to.x) / 2.0f, std::min(from.y, to.y) - 1.0f, (from.z + to.z) / 2.0f);
  const std::size_t p0 = b.Vertex(from.x - dx, from.y, from.z - dz);
  const std::size_t p1 = b.Vertex(from.x + dx, from.y, from.z + dz);
  const std::size_t p2 = b.Vertex(to.x + dx, to.y, to.z + dz);
  const std::size_t p3 = b.Vertex(to.x - dx, to.y, to.z - dz);
  b.Tri(p0, p1, p2, glow, true, 0);
  b.Tri(p0, p2, p3, glow, true, 0);
}

/// A hubless wheel in the y-z plane: a ring of solid segments, each its own small convex shape. The inner face glows.
void RingWheel(ModelBuilder& b, Vec3 at, float outer, float inner, float half_width, int sides, Rgb rim, Rgb glow) {
  for (int i = 0; i < sides; ++i) {
    const float a0 = kTau * static_cast<float>(i) / static_cast<float>(sides);
    const float a1 = kTau * static_cast<float>(i + 1) / static_cast<float>(sides);
    const float mid = (a0 + a1) / 2.0f;
    const float middle = (outer + inner) / 2.0f;
    b.Centre(at.x, at.y + middle * std::sin(mid), at.z + middle * std::cos(mid));
    const auto corner = [&](float angle, float radius, float x) {
      return b.Vertex(at.x + x, at.y + radius * std::sin(angle), at.z + radius * std::cos(angle));
    };
    const std::size_t o0l = corner(a0, outer, -half_width);
    const std::size_t o1l = corner(a1, outer, -half_width);
    const std::size_t o1r = corner(a1, outer, half_width);
    const std::size_t o0r = corner(a0, outer, half_width);
    const std::size_t i0l = corner(a0, inner, -half_width);
    const std::size_t i1l = corner(a1, inner, -half_width);
    const std::size_t i1r = corner(a1, inner, half_width);
    const std::size_t i0r = corner(a0, inner, half_width);
    b.Quad(o0l, o1l, o1r, o0r, rim);        // tread
    b.Quad(i0l, i1l, i1r, i0r, glow, true);  // inside of the ring
    b.Quad(o0l, o1l, i1l, i0l, rim);        // sides
    b.Quad(o0r, o1r, i1r, i0r, rim);
  }
}

/// One track pod of the crawler: a long box with sloped ends, toed out by `splay`
void TrackPod(ModelBuilder& b, float side, float z, float splay, const Style& s) {
  const float x = side * 0.95f;
  const float half_length = 0.62f;
  const auto at = [&](float along, float across) { return Vec3{x + across + side * splay * along, 0.0f, z + along}; };
  std::vector<Vec3> lower;
  std::vector<Vec3> upper;
  for (const auto& p : {std::pair<float, float>{-half_length * 0.7f, -0.2f}, {-half_length * 0.7f, 0.2f}, {half_length * 0.7f, 0.2f}, {half_length * 0.7f, -0.2f}}) {
    Vec3 v = at(p.first, p.second);
    v.y = -0.45f;
    lower.push_back(v);
  }
  for (const auto& p : {std::pair<float, float>{-half_length, -0.2f}, {-half_length, 0.2f}, {half_length, 0.2f}, {half_length, -0.2f}}) {
    Vec3 v = at(p.first, p.second);
    v.y = 0.02f;
    upper.push_back(v);
  }
  Loft(b, lower, upper, s.trim, s.hull, s.hull);
  Strip(b, at(-half_length * 0.85f, 0.0f + side * 0.0f), at(half_length * 0.85f, 0.0f), 0.05f, s.glow);
}

}  // namespace

namespace {

/// The crawler, saying where its guns start in the list of faces and which vertex they turn about
Model BuildCrawler(const Style& s, std::size_t* first_gun_face, std::size_t* pivot) {
  ModelBuilder b("crawler");
  // The hull: a low wedge, six sided from above, its point forward
  const std::vector<Vec3> hull_low = {{-0.5f, -0.25f, -1.1f}, {-0.78f, -0.25f, -0.4f}, {-0.6f, -0.25f, 0.55f}, {0.0f, -0.25f, 1.45f}, {0.6f, -0.25f, 0.55f}, {0.78f, -0.25f, -0.4f}, {0.5f, -0.25f, -1.1f}};
  const std::vector<Vec3> hull_high = {{-0.4f, 0.2f, -0.95f}, {-0.58f, 0.2f, -0.35f}, {-0.42f, 0.2f, 0.3f}, {0.0f, 0.2f, 0.75f}, {0.42f, 0.2f, 0.3f}, {0.58f, 0.2f, -0.35f}, {0.4f, 0.2f, -0.95f}};
  Loft(b, hull_low, hull_high, s.hull, s.trim, s.hull);
  // Four separate pods, splayed
  TrackPod(b, -1.0f, 0.75f, 0.28f, s);
  TrackPod(b, 1.0f, 0.75f, 0.28f, s);
  TrackPod(b, -1.0f, -0.75f, -0.28f, s);
  TrackPod(b, 1.0f, -0.75f, -0.28f, s);
  // Arms from the hull to the pods
  for (const float x : {-1.0f, 1.0f}) {
    for (const float z : {-0.75f, 0.75f}) {
      Block(b, {x > 0.0f ? 0.4f : -0.85f, -0.16f, z - 0.1f}, {x > 0.0f ? 0.85f : -0.4f, -0.04f, z + 0.1f}, s.trim);
    }
  }
  // A flat turret set back on the hull
  const std::vector<Vec3> turret_low = {{-0.36f, 0.2f, -0.7f}, {-0.42f, 0.2f, -0.2f}, {0.0f, 0.2f, 0.25f}, {0.42f, 0.2f, -0.2f}, {0.36f, 0.2f, -0.7f}};
  const std::vector<Vec3> turret_high = {{-0.26f, 0.48f, -0.6f}, {-0.3f, 0.48f, -0.22f}, {0.0f, 0.48f, 0.05f}, {0.3f, 0.48f, -0.22f}, {0.26f, 0.48f, -0.6f}};
  Loft(b, turret_low, turret_high, s.trim, s.hull, s.hull);
  Strip(b, {0.0f, 0.485f, -0.5f}, {0.0f, 0.485f, -0.05f}, 0.05f, s.glow);
  // Twin short barrels, which come last so that they can be taken off
  *first_gun_face = b.Build().faces.size();
  *pivot = b.Vertex(0.0f, 0.33f, -0.05f);
  Block(b, {-0.2f, 0.28f, 0.0f}, {-0.1f, 0.38f, 1.0f}, s.trim);
  Block(b, {0.1f, 0.28f, 0.0f}, {0.2f, 0.38f, 1.0f}, s.trim);
  Strip(b, {-0.15f, 0.385f, 0.2f}, {-0.15f, 0.385f, 0.98f}, 0.02f, s.glow);
  Strip(b, {0.15f, 0.385f, 0.2f}, {0.15f, 0.385f, 0.98f}, 0.02f, s.glow);
  return b.Finish(1.0f);
}

}  // namespace

Model MakeCrawler(const Style& s) {
  std::size_t first_gun_face = 0;
  std::size_t pivot = 0;
  return BuildCrawler(s, &first_gun_face, &pivot);
}

CrawlerParts MakeCrawlerParts(const Style& s) {
  std::size_t first_gun_face = 0;
  std::size_t pivot = 0;
  const Model whole = BuildCrawler(s, &first_gun_face, &pivot);
  CrawlerParts parts{whole, whole, whole.vertices[pivot]};
  const auto split = whole.faces.begin() + static_cast<std::ptrdiff_t>(first_gun_face);
  parts.hull.faces.assign(whole.faces.begin(), split);
  parts.gun.faces.assign(split, whole.faces.end());
  const Vec3 p = parts.gun_pivot;
  for (Vec3& v : parts.gun.vertices) {
    v = {v.x - p.x, v.y - p.y, v.z - p.z};
  }
  for (softrender::Face& f : parts.gun.faces) {
    f.centre = {f.centre.x - p.x, f.centre.y - p.y, f.centre.z - p.z};
  }
  return parts;
}

Model MakeKite(const Style& s) {
  ModelBuilder b("kite");
  // Two long hulls side by side, each a thin diamond in section, pointed at both ends
  for (const float side : {-1.0f, 1.0f}) {
    const float x = side * 0.85f;
    const std::vector<Vec3> lower = {{x - 0.1f, 0.0f, -1.2f}, {x - 0.18f, 0.0f, -0.2f}, {x, 0.0f, 1.5f}, {x + 0.18f, 0.0f, -0.2f}, {x + 0.1f, 0.0f, -1.2f}};
    const std::vector<Vec3> upper = {{x - 0.04f, 0.2f, -1.15f}, {x - 0.08f, 0.2f, -0.2f}, {x, 0.1f, 1.1f}, {x + 0.08f, 0.2f, -0.2f}, {x + 0.04f, 0.2f, -1.15f}};
    std::vector<Vec3> keel;
    for (Vec3 v : lower) {
      v.x = x + (v.x - x) * 0.4f;
      v.y = -0.14f;
      keel.push_back(v);
    }
    Loft(b, lower, upper, s.hull, s.trim, s.hull);
    Loft(b, keel, lower, s.hull, s.hull, s.hull);
    // A tall thin fin leaning outward
    Loft(b, {{x - 0.03f, 0.2f, -1.1f}, {x + 0.03f, 0.2f, -1.1f}, {x + 0.03f, 0.2f, -0.45f}, {x - 0.03f, 0.2f, -0.45f}},
         {{x + side * 0.2f - 0.015f, 0.72f, -1.3f}, {x + side * 0.2f + 0.015f, 0.72f, -1.3f}, {x + side * 0.2f + 0.015f, 0.72f, -1.05f}, {x + side * 0.2f - 0.015f, 0.72f, -1.05f}}, s.trim,
         s.hull, s.hull);
    Strip(b, {x, 0.205f, -1.05f}, {x, 0.15f, 0.9f}, 0.025f, s.glow);
  }
  // The forward swept wing that joins them: its outer ends sit further back than its middle
  Loft(b, {{-0.85f, 0.03f, -0.35f}, {-0.85f, 0.03f, 0.1f}, {0.0f, 0.03f, 1.05f}, {0.85f, 0.03f, 0.1f}, {0.85f, 0.03f, -0.35f}, {0.0f, 0.03f, 0.35f}},
       {{-0.85f, 0.1f, -0.3f}, {-0.85f, 0.1f, 0.06f}, {0.0f, 0.1f, 0.95f}, {0.85f, 0.1f, 0.06f}, {0.85f, 0.1f, -0.3f}, {0.0f, 0.1f, 0.4f}}, s.trim, s.hull, s.hull);
  // The pod on the centre line
  Loft(b, {{-0.14f, 0.08f, 0.15f}, {-0.2f, 0.08f, 0.5f}, {0.0f, 0.08f, 1.0f}, {0.2f, 0.08f, 0.5f}, {0.14f, 0.08f, 0.15f}},
       {{-0.08f, 0.3f, 0.25f}, {-0.1f, 0.3f, 0.5f}, {0.0f, 0.3f, 0.72f}, {0.1f, 0.3f, 0.5f}, {0.08f, 0.3f, 0.25f}}, s.trim, s.hull, s.hull);
  Strip(b, {-0.06f, 0.305f, 0.5f}, {0.06f, 0.305f, 0.5f}, 0.12f, s.glow);
  return b.Finish(1.0f);
}

}  // namespace vehicles
