#ifndef SOFTRENDER_MODEL_H_
#define SOFTRENDER_MODEL_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "softrender/types.h"

namespace softrender {

/// One triangle of a model
struct Face {
  /// Indices of the three vertices
  std::array<std::size_t, 3> v;
  /// Surface colour
  Rgb colour;
  /// Emissive surfaces (engine glow, lights) ignore the lighting
  bool emissive;
  /// Edges to outline: bit 0 is v0-v1, bit 1 is v1-v2 and bit 2 is v2-v0
  std::uint8_t edges;
  /// The centre of the mesh the face belongs to, used to point the normal outwards
  Vec3 centre;
};

/// A mesh: vertices and triangles
struct Model {
  std::string name;
  std::vector<Vec3> vertices;
  std::vector<Face> faces;
};

}  // namespace softrender

#endif  // SOFTRENDER_MODEL_H_
