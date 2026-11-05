#ifndef SOFTRENDER_POST_H_
#define SOFTRENDER_POST_H_

#include <limits>

#include "softrender/types.h"

namespace softrender {

/// Bright parts bleed light into the dark round them, as on a camera sensor or the eye. Only what is brighter than
/// `threshold` (0 to 255) bleeds, and `strength` sets how much.
/// The glow is worked out on a copy of the picture made of cells this many pixels across, and spreads a few cells, so a
/// bigger cell makes a wider glow.
void Bloom(Image& image, float strength = 0.45f, float threshold = 150.0f, int cell_size = 4);

/// Soften the stair steps along edges (in the way of FXAA): a pixel whose neighbours differ a lot in brightness is mixed
/// with the pixel across the edge it is on and the ones along it, so a staircase becomes a smooth ramp. Flat areas are
/// left alone, and so are lone bright dots, which would only be dimmed. Only the first `rows` rows are done.
void SoftenEdges(Image& image, int rows = std::numeric_limits<int>::max());

/// Average blocks of `factor` by `factor` pixels down to a smaller picture: supersampling, for smooth edges. `big` must
/// be exactly `factor` times the size of `out`.
void Downsample(const Image& big, Image& out, int factor);

/// Stretch the picture away from (cx, cy) with the brightest thing along each ray winning, so bright dots become
/// streaks, fading to white. `amount` runs from 0 (a little) to 1 (all the way).
void RadialZoom(Image& image, double amount, double cx, double cy);

}  // namespace softrender

#endif  // SOFTRENDER_POST_H_
