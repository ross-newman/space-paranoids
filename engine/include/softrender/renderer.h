#ifndef SOFTRENDER_RENDERER_H_
#define SOFTRENDER_RENDERER_H_

#include <functional>

#include "softrender/camera.h"
#include "softrender/canvas.h"
#include "softrender/types.h"

namespace softrender {

/// How a frame is made
struct RenderOptions {
  /// What the picture starts as
  Rgb background{0, 0, 0};
  /// Draw this many times bigger and shrink to the picture, for smooth edges (1 to 4)
  int supersample = 1;
  /// A soft glow round the bright parts
  bool bloom = true;
  float bloom_strength = 0.45f;
  float bloom_threshold = 150.0f;
  /// How wide the glow is, as the size in pixels of the cells it is worked out on, measured on the finished picture
  int bloom_cell = 4;
  /// Soften the stair steps along edges. A supersampled picture does not need it.
  bool soften_edges = false;
  /// The edges of models, measured on the finished picture
  Outline outline;
};

/// Makes whole frames: it starts the picture, has the world drawn, and finishes it. The world draws with the canvas and
/// camera it is handed, which are bigger than the picture when supersampling, so nothing needs to know about that.
class Renderer {
 public:
  using WorldFn = std::function<void(Canvas&, const Camera&)>;
  using OverlayFn = std::function<void(Image&)>;

  explicit Renderer(RenderOptions options = {}) : options_(options) {}

  const RenderOptions& options() const { return options_; }
  void set_options(const RenderOptions& options) { options_ = options; }

  /// Make a frame in `image`. `camera` is for that image. `overlay`, if any, draws on the finished picture, so it stays
  /// crisp and does not glow: this is the place for a HUD. To have text glow, draw it on canvas.image() in `world`.
  void Render(Image& image, const Camera& camera, const WorldFn& world, const OverlayFn& overlay = nullptr);

 private:
  void Finish(Canvas& canvas, const Camera& camera, const WorldFn& world);

  RenderOptions options_;
  Image big_{0, 0};  // kept between frames, while supersampling
};

}  // namespace softrender

#endif  // SOFTRENDER_RENDERER_H_
