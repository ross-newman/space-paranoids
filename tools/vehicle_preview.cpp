// Draw the vehicles to a PNG, from the front, side, back and three quarters, with sanity checks on each mesh:
//   vehicle_preview [out.png]

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "softrender/softrender.h"
#include "vehicles.h"

namespace {

using namespace softrender;

/// A model to draw, and how to light it. An overhead light suits the flat upper planes of the crawler and kite, but it
/// only grazes the near vertical sides of a runner, so a runner is lit from above and behind the eye instead.
struct Row {
  Model model;
  bool side_lit;
};

bool Check(const Model& m) {
  bool ok = !m.faces.empty();
  double reach = 0.0;
  for (const Vec3& v : m.vertices) {
    ok = ok && std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    reach = std::max(reach, static_cast<double>(std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z)));
  }
  for (const Face& f : m.faces) {
    ok = ok && f.v[0] < m.vertices.size() && f.v[1] < m.vertices.size() && f.v[2] < m.vertices.size();
  }
  ok = ok && std::fabs(reach - 1.0) < 1e-3;
  std::printf("%-8s %5zu vertices %5zu triangles  reach %.3f  %s\n", m.name.c_str(), m.vertices.size(), m.faces.size(), reach, ok ? "ok" : "BAD");
  return ok;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string path = argc > 1 ? argv[1] : "vehicles.png";
  const std::vector<Row> rows = {{vehicles::MakeCrawler(), false},
                                 {vehicles::MakeRunner(vehicles::RedStyle()), true},
                                 {vehicles::MakeRunner(vehicles::YellowStyle()), true},
                                 {vehicles::MakeRunner(vehicles::OrangeStyle()), true},
                                 {vehicles::MakeKite(), false}};
  const int row_count = static_cast<int>(rows.size());
  bool ok = true;
  for (const Row& row : rows) {
    ok = Check(row.model) && ok;
  }

  constexpr int kCell = 360;
  const double views[4][2] = {{2.45, 0.35}, {3.14159, 0.1}, {1.5708, 0.05}, {0.5, 0.45}};  // yaw and height of the eye
  Image image(kCell * 4, kCell * row_count);
  RenderOptions options;
  options.supersample = 2;
  options.background = {4, 6, 12};
  options.outline = {0.9f, 5.0f};
  Renderer renderer(options);
  const Camera whole = MakeCamera(image.width(), image.height());
  renderer.Render(image, whole, [&](Canvas& canvas, const Camera&) {
    const int cell = canvas.width() / 4;
    for (int row = 0; row < row_count; ++row) {
      for (int col = 0; col < 4; ++col) {
        Camera view = MakeCamera(cell, cell, 30.0);
        view.cx += col * cell;
        view.cy += row * cell;
        const double yaw = views[col][0];
        const double up = views[col][1];
        view.position = {std::sin(yaw) * std::cos(up) * 3.8, std::sin(up) * 3.8, std::cos(yaw) * std::cos(up) * 3.8};
        view.orientation = LookAlong(-view.position, {0.0, 1.0, 0.0});
        canvas.SetClip(col * cell, row * cell, (col + 1) * cell, (row + 1) * cell);
        Lighting lighting;
        const Row& shown = rows[static_cast<std::size_t>(row)];
        lighting.direction = {0.05, 1.0, 0.05};  // overhead, so undersides aren't lost to black
        if (shown.side_lit) {
          lighting.direction = view.position + Vec3d{0.0, 3.8, 0.0};
          lighting.specular = 0.35f;  // the gloss of the paint
        }
        DrawModel(canvas, view, shown.model, Transform{}, lighting);
      }
    }
    canvas.ResetClip();
  });
  for (int row = 0; row < row_count; ++row) {
    DrawText(image, rows[static_cast<std::size_t>(row)].model.name, 12, row * kCell + 10, 2.0f, {255, 255, 255});
  }
  if (!WritePng(image, path)) {
    std::fprintf(stderr, "could not write %s\n", path.c_str());
    return 1;
  }
  return ok ? 0 : 1;
}
