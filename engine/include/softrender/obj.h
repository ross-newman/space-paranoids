#ifndef SOFTRENDER_OBJ_H_
#define SOFTRENDER_OBJ_H_

#include <string>

#include "softrender/model.h"

namespace softrender {

/// How to read a Wavefront OBJ file into a model
struct ObjOptions {
  /// The name of the model, or empty to use the file name
  std::string name;
  /// OBJ files are usually right handed with the front of the model along -z (the "-Z forward, Y up" export of
  /// Blender and most other tools), while a model here has its front along +z. When set (the default) z is negated and
  /// the winding is turned round to match. Clear it for a file that already faces +z.
  bool flip_z = true;
  /// Turn every face round, for a file whose triangles wind clockwise
  bool flip_winding = false;
  /// The model is centred and scaled to reach this far from its centre, or left at the size in the file if 0
  float radius = 1.0f;
};

/// Read `path` into `*model`, centred and scaled as `options.radius` says. Understands v and f (with
/// any of the v, v/t, v//n and v/t/n forms, negative indices, and polygons that are split into triangles) and the
/// Kd and Ke colours of the material library named by mtllib. Everything else is ignored. The faces point the way
/// their winding says, so the mesh does not have to be convex. On failure returns false and says why in `*error`.
bool LoadObj(const std::string& path, const ObjOptions& options, Model* model, std::string* error);

/// The same for text that is already in memory. `directory` is where the mtllib is looked for, or empty to ignore it.
bool ParseObj(const std::string& text, const std::string& directory, const ObjOptions& options, Model* model,
              std::string* error);

}  // namespace softrender

#endif  // SOFTRENDER_OBJ_H_
