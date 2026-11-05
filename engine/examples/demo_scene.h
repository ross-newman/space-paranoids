#ifndef SOFTRENDER_EXAMPLES_DEMO_SCENE_H_
#define SOFTRENDER_EXAMPLES_DEMO_SCENE_H_

// A small world shared by the examples: a glowing floor grid with solid shapes on it, seen from a camera that circles.

#include <cmath>
#include <cstdint>

#include "softrender/softrender.h"

namespace demo {

using namespace softrender;

inline Model MakeBlock(Rgb body, Rgb front) {
  ModelBuilder b("block");
  b.Box(-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, body, front, true);
  return b.Finish(1.0f);
}

inline Model MakePyramid(Rgb colour) {
  ModelBuilder b("pyramid");
  b.Centre(0.0f, -0.4f, 0.0f);
  const std::size_t tip = b.Vertex(0.0f, 1.0f, 0.0f);
  const std::size_t base[4] = {b.Vertex(-1.0f, -1.0f, -1.0f), b.Vertex(1.0f, -1.0f, -1.0f), b.Vertex(1.0f, -1.0f, 1.0f), b.Vertex(-1.0f, -1.0f, 1.0f)};
  for (int i = 0; i < 4; ++i) {
    b.Tri(tip, base[i], base[(i + 1) % 4], colour);
  }
  b.Quad(base[0], base[1], base[2], base[3], Scale(colour, 0.5f));
  return b.Finish(1.0f);
}

inline Image MakeTexture() {
  Image tex(64, 64);
  for (std::uint32_t y = 0; y < 64; ++y) {
    for (std::uint32_t x = 0; x < 64; ++x) {
      tex.Set(x, y, ((x / 8) ^ (y / 8)) & 1U ? Rgb{240, 120, 30} : Rgb{20, 10, 40});
    }
  }
  return tex;
}

/// Draws the world for one frame. `time` is in seconds.
class Scene {
 public:
  Camera CameraFor(const Image& image, double time) const {
    Camera camera = MakeCamera(image.width(), image.height(), 70.0);
    const double angle = time * 0.25;
    camera.position = {std::sin(angle) * 16.0, 4.0, -std::cos(angle) * 16.0};
    camera.orientation = LookAlong(Vec3d{0.0, 1.0, 0.0} - camera.position, {0.0, 1.0, 0.0});
    return camera;
  }

  void Draw(Canvas& canvas, const Camera& camera, double time) const {
    nebula_.Paint(canvas.image(), camera.orientation, camera.focal, camera.cx, camera.cy);
    DrawSkyStars(canvas, camera);

    // The floor: dark, with lines lifted just off it
    const double extent = 40.0;
    FillPolygon(canvas, camera, {{-extent, 0.0, -extent}, {extent, 0.0, -extent}, {extent, 0.0, extent}, {-extent, 0.0, extent}}, {6, 10, 22});
    for (int i = -20; i <= 20; ++i) {
      const double at = i * 2.0;
      DrawLine3D(canvas, camera, {at, 0.02, -extent}, {at, 0.02, extent}, {30, 200, 255});
      DrawLine3D(canvas, camera, {-extent, 0.02, at}, {extent, 0.02, at}, {30, 200, 255});
    }

    Lighting light;
    light.specular = 0.35f;
    for (int i = 0; i < 6; ++i) {
      const double angle = i * 1.0471975512 + time * 0.2;
      Transform t;
      t.position = {std::cos(angle) * 7.0, 1.6, std::sin(angle) * 7.0};
      t.orientation = Turned(Quat{}, 0.0, 0.0, time * 0.9 + i);
      t.scale = 1.6;
      DrawModel(canvas, camera, i % 2 == 0 ? block_ : pyramid_, t, light);
    }

    // A picture standing on the floor, and something too far away to be more than a dot
    DrawTexturedQuad(canvas, camera, {-3.0, 0.0, 0.0}, {3.0, 0.0, 0.0}, {3.0, 6.0, 0.0}, {-3.0, 6.0, 0.0}, texture_);
    Transform far_away;
    far_away.position = {0.0, 200.0, 2000.0};
    DrawModel(canvas, camera, block_, far_away, light, {255, 80, 80});
  }

 private:
  Model block_ = MakeBlock({200, 60, 230}, {255, 240, 120});
  Model pyramid_ = MakePyramid({60, 220, 140});
  Image texture_ = MakeTexture();
  Nebula nebula_{5};
};

}  // namespace demo

#endif  // SOFTRENDER_EXAMPLES_DEMO_SCENE_H_
