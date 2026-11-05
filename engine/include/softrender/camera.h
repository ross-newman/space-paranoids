#ifndef SOFTRENDER_CAMERA_H_
#define SOFTRENDER_CAMERA_H_

#include "softrender/math.h"
#include "softrender/types.h"

namespace softrender {

/// A pinhole camera. It looks along the +z of `orientation` with +x to the right and +y up. Pixels run right and down
/// from the top left of the picture.
struct Camera {
  Quat orientation;
  Vec3d position;
  /// Focal length in pixels
  double focal = 600.0;
  /// The pixel the camera looks through
  double cx = 0.0;
  double cy = 0.0;
  /// Nothing nearer than this is drawn
  double near_plane = 0.5;

  /// A point in the world in the camera's own space
  Vec3d ToCamera(const Vec3d& world) const { return Rotate(Conjugate(orientation), world - position); }

  /// Where a point in camera space falls on the picture. False if it is nearer than the near plane, and the position
  /// is then not set. The position can be off the picture.
  bool Project(const Vec3d& view, float* x, float* y) const {
    if (view.z < near_plane) {
      return false;
    }
    *x = static_cast<float>(cx + focal * view.x / view.z);
    *y = static_cast<float>(cy - focal * view.y / view.z);
    return true;
  }

  /// The same camera for a picture `factor` times bigger
  Camera Scaled(double factor) const {
    Camera out = *this;
    out.focal *= factor;
    out.cx *= factor;
    out.cy *= factor;
    return out;
  }
};

/// A camera looking through the centre of a picture this size, with this horizontal field of view in degrees
inline Camera MakeCamera(std::uint32_t width, std::uint32_t height, double fov_degrees = 70.0) {
  Camera camera;
  camera.focal = static_cast<double>(width) / (2.0 * std::tan(fov_degrees * 3.14159265358979323846 / 360.0));
  camera.cx = static_cast<double>(width) / 2.0;
  camera.cy = static_cast<double>(height) / 2.0;
  return camera;
}

}  // namespace softrender

#endif  // SOFTRENDER_CAMERA_H_
