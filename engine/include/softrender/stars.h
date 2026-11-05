#ifndef SOFTRENDER_STARS_H_
#define SOFTRENDER_STARS_H_

#include <cstdint>

#include "softrender/camera.h"
#include "softrender/canvas.h"
#include "softrender/types.h"

namespace softrender {

/// Paint a flat star field in three layers that drift at different speeds for parallax, behind a 2D screen. `frame`
/// counts frames; the same frame always gives the same stars. Brighter pixels win over what is in the image.
void DrawStars(Image& image, std::uint32_t frame);

/// Stars fixed on the sky, infinitely far away: they turn with the camera and never move with it. Draw them first, as
/// they ignore depth. The same seed always gives the same sky.
void DrawSkyStars(Canvas& canvas, const Camera& camera, std::uint32_t count = 500, std::uint32_t seed = 17);

}  // namespace softrender

#endif  // SOFTRENDER_STARS_H_
