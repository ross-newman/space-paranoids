// Draw one frame with no window and write it as a PNG: softrender_scene [out.png] [time]

#include <cstdio>
#include <cstdlib>
#include <string>

#include "demo_scene.h"

int main(int argc, char** argv) {
  using namespace softrender;
  const std::string path = argc > 1 ? argv[1] : "softrender_scene.png";
  const double time = argc > 2 ? std::atof(argv[2]) : 1.0;

  Image image(960, 540);
  RenderOptions options;
  options.supersample = 2;
  Renderer renderer(options);
  const demo::Scene scene;
  renderer.Render(
      image, scene.CameraFor(image, time), [&](Canvas& canvas, const Camera& camera) { scene.Draw(canvas, camera, time); },
      [](Image& picture) { DrawText(picture, "softrender", 14, 14, 2.0f * UiScale(picture), {255, 255, 255}); });

  if (!WritePng(image, path)) {
    std::fprintf(stderr, "could not write %s\n", path.c_str());
    return 1;
  }
  std::printf("wrote %s\n", path.c_str());
  return 0;
}
