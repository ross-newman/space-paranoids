#ifndef SOFTRENDER_MODEL_BUILDER_H_
#define SOFTRENDER_MODEL_BUILDER_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "softrender/model.h"
#include "softrender/types.h"

namespace softrender {

/// Builds a model from vertices and polygons
///
/// The front points along +z, +y is up and +x is to the right of the model. Faces are oriented outwards by
/// comparing their position with the centre of the mesh they belong to, so each mesh has to be convex about the
/// centre set with Centre(). Finish() recentres the model and scales it to a chosen size; Build() leaves it as it is.
class ModelBuilder {
 public:
  explicit ModelBuilder(const std::string& name) { model_.name = name; }

  std::size_t Vertex(float x, float y, float z) {
    model_.vertices.push_back({x, y, z});
    return model_.vertices.size() - 1;
  }

  /// Where a vertex made earlier is
  Vec3 Position(std::size_t index) const { return model_.vertices[index]; }

  /// The centre of the mesh being built
  void Centre(float x, float y, float z) { centre_ = {x, y, z}; }

  void Tri(std::size_t a, std::size_t b, std::size_t c, Rgb colour, bool emissive = false, std::uint8_t edges = 0x7) {
    model_.faces.push_back({{a, b, c}, colour, emissive, edges, centre_});
  }

  /// A convex polygon, outlined along its boundary only
  void Poly(const std::vector<std::size_t>& v, Rgb colour, bool emissive = false) {
    const std::size_t n = v.size();
    for (std::size_t i = 1; i + 1 < n; ++i) {
      std::uint8_t edges = 0x2;
      if (i == 1) {
        edges |= 0x1;
      }
      if (i + 2 == n) {
        edges |= 0x4;
      }
      Tri(v[0], v[i], v[i + 1], colour, emissive, edges);
    }
  }

  void Quad(std::size_t a, std::size_t b, std::size_t c, std::size_t d, Rgb colour, bool emissive = false) {
    Poly({a, b, c, d}, colour, emissive);
  }

  /// A ring of vertices in a plane of constant z
  std::vector<std::size_t> Ring(int sides, float rx, float ry, float z, float phase = 0.0f, float cx = 0.0f,
                                float cy = 0.0f) {
    std::vector<std::size_t> ring;
    for (int i = 0; i < sides; ++i) {
      const float angle = phase + 2.0f * kPi * static_cast<float>(i) / static_cast<float>(sides);
      ring.push_back(Vertex(cx + rx * std::cos(angle), cy + ry * std::sin(angle), z));
    }
    return ring;
  }

  /// Quads between two rings with the same number of vertices, the colours repeating around the ring
  void Band(const std::vector<std::size_t>& a, const std::vector<std::size_t>& b, const std::vector<Rgb>& colours,
            bool emissive = false) {
    const std::size_t n = a.size();
    for (std::size_t i = 0; i < n; ++i) {
      const std::size_t j = (i + 1) % n;
      Quad(a[i], a[j], b[j], b[i], colours[i % colours.size()], emissive);
    }
  }

  /// Triangles from a point to a ring
  void Fan(std::size_t tip, const std::vector<std::size_t>& ring, const std::vector<Rgb>& colours) {
    const std::size_t n = ring.size();
    for (std::size_t i = 0; i < n; ++i) {
      Tri(tip, ring[i], ring[(i + 1) % n], colours[i % colours.size()]);
    }
  }

  /// A closed box, the face at z0 (the rear) in its own colour
  void Box(float x0, float y0, float z0, float x1, float y1, float z1, Rgb colour, Rgb rear, bool rear_emissive) {
    Centre((x0 + x1) / 2.0f, (y0 + y1) / 2.0f, (z0 + z1) / 2.0f);
    const std::size_t p000 = Vertex(x0, y0, z0);
    const std::size_t p100 = Vertex(x1, y0, z0);
    const std::size_t p110 = Vertex(x1, y1, z0);
    const std::size_t p010 = Vertex(x0, y1, z0);
    const std::size_t p001 = Vertex(x0, y0, z1);
    const std::size_t p101 = Vertex(x1, y0, z1);
    const std::size_t p111 = Vertex(x1, y1, z1);
    const std::size_t p011 = Vertex(x0, y1, z1);
    Quad(p000, p100, p110, p010, rear, rear_emissive);
    Quad(p001, p011, p111, p101, colour);
    Quad(p010, p110, p111, p011, colour);
    Quad(p000, p001, p101, p100, colour);
    Quad(p000, p010, p011, p001, colour);
    Quad(p100, p101, p111, p110, colour);
  }

  /// The model exactly as it was built
  Model Build() const { return model_; }

  /// Centre the model on the origin and scale it so that it reaches `radius` from there
  Model Finish(float radius) {
    std::array<float, 3> low{1e9f, 1e9f, 1e9f};
    std::array<float, 3> high{-1e9f, -1e9f, -1e9f};
    for (const Vec3& v : model_.vertices) {
      low = {std::min(low[0], v.x), std::min(low[1], v.y), std::min(low[2], v.z)};
      high = {std::max(high[0], v.x), std::max(high[1], v.y), std::max(high[2], v.z)};
    }
    const Vec3 shift{(low[0] + high[0]) / 2.0f, (low[1] + high[1]) / 2.0f, (low[2] + high[2]) / 2.0f};
    float largest = 1e-3f;
    for (Vec3& v : model_.vertices) {
      v = {v.x - shift.x, v.y - shift.y, v.z - shift.z};
      largest = std::max(largest, std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z));
    }
    const float scale = radius / largest;
    for (Vec3& v : model_.vertices) {
      v = {v.x * scale, v.y * scale, v.z * scale};
    }
    for (Face& f : model_.faces) {
      f.centre = {(f.centre.x - shift.x) * scale, (f.centre.y - shift.y) * scale, (f.centre.z - shift.z) * scale};
    }
    return model_;
  }

 private:
  Model model_;
  Vec3 centre_{0.0f, 0.0f, 0.0f};
};

}  // namespace softrender

#endif  // SOFTRENDER_MODEL_BUILDER_H_
