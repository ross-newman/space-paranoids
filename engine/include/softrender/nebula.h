#ifndef SOFTRENDER_NEBULA_H_
#define SOFTRENDER_NEBULA_H_

#include <array>
#include <cstdint>
#include <vector>

#include "softrender/math.h"
#include "softrender/types.h"

namespace softrender {

/// Gas clouds on a sky, made from nothing but a seed. The sky is a cube map of soft coloured light
/// built from warped, layered noise: broad glowing clouds, thin bright filaments, dark dust lanes and a hot core where
/// the gas is thickest. The colours come from a small set of palettes (red and blue emission, teal and violet, dusty
/// orange, pale reflection blue, green and gold). The seed also sets how much of the sky is covered, so some skies
/// have grand clouds and others only a faint haze. The light is dim, so it never fights what is drawn over
/// it. The same seed always gives the same sky.
class Nebula {
 public:
  /// Build the sky for `seed`. `face_size` is the width of each of the six cube faces in texels.
  explicit Nebula(std::uint32_t seed, std::uint32_t face_size = 128);

  std::uint32_t seed() const { return seed_; }

  /// The light coming from a direction (it need not be a unit vector) as red, green and blue from 0 to 255
  std::array<float, 3> Sample(const Vec3d& direction) const;

  /// Add the sky to the image (brighter pixels win over nothing: the light is added to what is there). The camera looks
  /// along the +z of `orientation` with +x to the right and +y up, `focal` is in pixels and (cx, cy) is the pixel the
  /// camera looks through.
  void Paint(Image& image, const Quat& orientation, double focal, double cx, double cy) const;

 private:
  std::uint32_t seed_;
  std::uint32_t face_size_;
  std::vector<float> texels_;  // 6 faces, rows of RGB
};

/// Paint the clouds of `seed` behind a flat scene, such as the title screen. The view turns very slowly as `frame` counts
/// up. The same seed and frame always give the same picture. The sky is kept between calls, so call it every frame.
void DrawNebula(Image& image, std::uint32_t seed, std::uint32_t frame);

}  // namespace softrender

#endif  // SOFTRENDER_NEBULA_H_
