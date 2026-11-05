#ifndef SOFTRENDER_SCENE_H_
#define SOFTRENDER_SCENE_H_

#include <vector>

#include "softrender/camera.h"
#include "softrender/canvas.h"
#include "softrender/math.h"
#include "softrender/model.h"
#include "softrender/types.h"

namespace softrender {

/// Where a model is in the world. A model's vertices are scaled, turned and then moved.
struct Transform {
  Vec3d position;
  Quat orientation;
  double scale = 1.0;
};

/// One distant light and flat shading. Emissive faces ignore it.
struct Lighting {
  /// The direction to the light in the world, which need not be a unit vector
  Vec3d direction{0.4, 0.8, -0.4};
  float ambient = 0.22f;
  float diffuse = 0.78f;
  /// Strength of the highlight, 0 for none
  float specular = 0.0f;
  float shininess = 24.0f;
};

/// Draw a model as solid shaded triangles, outlining the edges its faces mark. Faces turned away from the camera are
/// not drawn and anything nearer than the near plane is cut off. A model too far away to show a shape is drawn as a
/// single dot of `distant` colour.
void DrawModel(Canvas& canvas, const Camera& camera, const Model& model, const Transform& transform, const Lighting& lighting = {},
               Rgb distant = {255, 255, 255});

/// Fill a flat convex polygon with one colour, given in world coordinates. Unlit and seen from both sides. `edges` marks
/// the edges of the triangle fan to outline, for a triangle as in Canvas::Triangle.
void FillPolygon(Canvas& canvas, const Camera& camera, const std::vector<Vec3d>& polygon, Rgb colour, std::uint8_t edges = 0);

/// A line between two points in the world, hidden by anything nearer. It cannot hide anything. A line along a surface
/// should be lifted a little off it, or the surface can hide it.
void DrawLine3D(Canvas& canvas, const Camera& camera, const Vec3d& a, const Vec3d& b, Rgb colour, bool additive = false);

/// A picture on a flat four sided shape in the world, correct in perspective. The corners go round the picture from
/// bottom left: bottom right, top right and then top left. Unlit, and seen from both sides.
void DrawTexturedQuad(Canvas& canvas, const Camera& camera, const Vec3d& a, const Vec3d& b, const Vec3d& c, const Vec3d& d,
                      const Image& texture);

}  // namespace softrender

#endif  // SOFTRENDER_SCENE_H_
