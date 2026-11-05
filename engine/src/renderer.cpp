#include "softrender/renderer.h"

#include <algorithm>

#include "softrender/post.h"

namespace softrender {

void Renderer::Finish(Canvas& canvas, const Camera& camera, const WorldFn& world) {
  canvas.Clear(options_.background);
  world(canvas, camera);
  if (options_.soften_edges) {
    SoftenEdges(canvas.image());
  }
  if (options_.bloom) {
    Bloom(canvas.image(), options_.bloom_strength, options_.bloom_threshold, options_.bloom_cell * std::clamp(options_.supersample, 1, 4));
  }
}

void Renderer::Render(Image& image, const Camera& camera, const WorldFn& world, const OverlayFn& overlay) {
  const int factor = std::clamp(options_.supersample, 1, 4);
  if (factor == 1) {
    Canvas canvas(image);
    canvas.set_outline(options_.outline);
    Finish(canvas, camera, world);
  } else {
    const std::uint32_t f = static_cast<std::uint32_t>(factor);
    if (big_.width() != image.width() * f || big_.height() != image.height() * f) {
      big_ = Image(image.width() * f, image.height() * f);
    }
    Canvas canvas(big_);
    Outline outline = options_.outline;
    outline.width *= static_cast<float>(factor);
    canvas.set_outline(outline);
    canvas.set_line_width(factor);
    Finish(canvas, camera.Scaled(factor), world);
    Downsample(big_, image, factor);
  }
  if (overlay) {
    overlay(image);
  }
}

}  // namespace softrender
