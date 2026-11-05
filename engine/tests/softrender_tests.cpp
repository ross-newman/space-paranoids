// Tests for the renderer. They check what is drawn by counting pixels, so they need no window and no reference pictures.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>

#include "softrender/softrender.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                                 \
  do {                                                                                   \
    if (!(condition)) {                                                                  \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
      ++g_failures;                                                                      \
    }                                                                                    \
  } while (false)

using namespace softrender;

constexpr std::uint32_t kExpectedPictureSum = 0x76B408CCU;

/// Pixels in a block that are not black
std::size_t Lit(const Image& image, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
  std::size_t n = 0;
  for (std::uint32_t y = y0; y < y1; ++y) {
    for (std::uint32_t x = x0; x < x1; ++x) {
      const std::uint8_t* p = image.data() + (static_cast<std::size_t>(y) * image.width() + x) * 3;
      n += (p[0] | p[1] | p[2]) != 0 ? 1U : 0U;
    }
  }
  return n;
}

std::size_t Lit(const Image& image) { return Lit(image, 0, 0, image.width(), image.height()); }

Rgb At(const Image& image, std::uint32_t x, std::uint32_t y) {
  const std::uint8_t* p = image.data() + (static_cast<std::size_t>(y) * image.width() + x) * 3;
  return {p[0], p[1], p[2]};
}

Model Cube(Rgb colour, bool emissive = true) {
  ModelBuilder b("cube");
  b.Box(-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, colour, colour, emissive);
  Model model = b.Build();
  if (emissive) {
    for (Face& f : model.faces) {
      f.emissive = true;
      f.edges = 0;
    }
  }
  return model;
}

Transform At(Vec3d position, double scale = 1.0) {
  Transform t;
  t.position = position;
  t.scale = scale;
  return t;
}

void TestCameraProjects() {
  Camera c = MakeCamera(200, 100, 90.0);
  CHECK(std::fabs(c.focal - 100.0) < 1e-9);
  float x = 0.0f;
  float y = 0.0f;
  CHECK(c.Project({0.0, 0.0, 10.0}, &x, &y) && x == 100.0f && y == 50.0f);
  CHECK(c.Project({10.0, 10.0, 10.0}, &x, &y) && x == 200.0f && y == -50.0f);  // up is towards the top of the picture
  CHECK(!c.Project({0.0, 0.0, 0.4}, &x, &y));
  CHECK(!c.Project({0.0, 0.0, -5.0}, &x, &y));

  c.position = {1.0, 2.0, 3.0};
  c.orientation = LookAlong({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
  const Vec3d view = c.ToCamera({6.0, 2.0, 3.0});
  CHECK(std::fabs(view.x) < 1e-9 && std::fabs(view.y) < 1e-9 && std::fabs(view.z - 5.0) < 1e-9);

  const Camera big = MakeCamera(200, 100, 90.0).Scaled(2.0);
  CHECK(std::fabs(big.focal - 200.0) < 1e-9 && big.cx == 200.0 && big.cy == 100.0);
}

void TestNearerHidesFurther() {
  Image image(100, 100);
  Canvas canvas(image);
  const Camera camera = MakeCamera(100, 100);
  const Model red = Cube({255, 0, 0});
  const Model green = Cube({0, 255, 0});
  // Either order must come out the same
  DrawModel(canvas, camera, red, At({0.0, 0.0, 10.0}));
  DrawModel(canvas, camera, green, At({0.0, 0.0, 20.0}));
  CHECK(At(image, 50, 50).r == 255 && At(image, 50, 50).g == 0);
  canvas.Clear({0, 0, 0});
  CHECK(Lit(image) == 0);
  DrawModel(canvas, camera, green, At({0.0, 0.0, 20.0}));
  DrawModel(canvas, camera, red, At({0.0, 0.0, 10.0}));
  CHECK(At(image, 50, 50).r == 255 && At(image, 50, 50).g == 0);
}

void TestModelBehindTheCameraDrawsNothing() {
  Image image(100, 100);
  Canvas canvas(image);
  const Camera camera = MakeCamera(100, 100);
  DrawModel(canvas, camera, Cube({255, 255, 255}), At({0.0, 0.0, -10.0}));
  CHECK(Lit(image) == 0);
}

void TestModelAcrossTheNearPlaneIsCut() {
  // The eye is inside a huge box: faces pass the near plane and project far beyond the range of an int
  Image image(100, 100);
  Canvas canvas(image);
  Camera camera = MakeCamera(100, 100);
  ModelBuilder b("slab");
  b.Centre(0.0f, -1.0f, 0.0f);
  const std::size_t p[4] = {b.Vertex(-1e7f, 0.0f, -1e7f), b.Vertex(1e7f, 0.0f, -1e7f), b.Vertex(1e7f, 0.0f, 1e7f), b.Vertex(-1e7f, 0.0f, 1e7f)};
  b.Quad(p[0], p[1], p[2], p[3], {255, 255, 255}, true);
  const Model slab = b.Build();
  Transform t = At({0.0, -2.0, 0.0});
  camera.focal = 1e4;
  DrawModel(canvas, camera, slab, t);
  CHECK(Lit(image, 0, 60, 100, 100) == 40 * 100);  // the floor fills the picture below the horizon
  CHECK(Lit(image, 0, 0, 100, 40) == 0);
}

void TestFarModelIsADot() {
  Image image(100, 100);
  Canvas canvas(image);
  const Camera camera = MakeCamera(100, 100);
  DrawModel(canvas, camera, Cube({255, 255, 255}), At({0.0, 0.0, 100000.0}), {}, {10, 20, 30});
  CHECK(Lit(image) == 1);
  CHECK(At(image, 50, 50).b == 30);
}

void TestLightAndEmissive() {
  const Camera camera = MakeCamera(100, 100);
  Lighting towards;
  towards.direction = {0.0, 0.0, -1.0};  // behind the camera, so it lights what the camera sees
  Lighting away;
  away.direction = {0.0, 0.0, 1.0};
  const Model solid = Cube({200, 200, 200}, false);
  Image bright(100, 100);
  Image dim(100, 100);
  Canvas a(bright);
  Canvas b(dim);
  DrawModel(a, camera, solid, At({0.0, 0.0, 6.0}), towards);
  DrawModel(b, camera, solid, At({0.0, 0.0, 6.0}), away);
  CHECK(At(bright, 50, 50).r == 200);
  CHECK(At(dim, 50, 50).r == 44);  // ambient only
  // The direction need not be a unit vector
  Image same(100, 100);
  Canvas c(same);
  towards.direction = {0.0, 0.0, -50.0};
  DrawModel(c, camera, solid, At({0.0, 0.0, 6.0}), towards);
  CHECK(same == bright);
  // Emissive faces do not care
  Image glow(100, 100);
  Canvas d(glow);
  DrawModel(d, camera, Cube({200, 10, 10}), At({0.0, 0.0, 6.0}), away);
  CHECK(At(glow, 50, 50).r == 200);
}

void TestSpecularBrightens() {
  const Camera camera = MakeCamera(100, 100);
  Lighting light;
  light.direction = {0.0, 0.0, -1.0};
  light.ambient = 0.2f;
  light.diffuse = 0.2f;
  Image plain(100, 100);
  Canvas a(plain);
  DrawModel(a, camera, Cube({100, 100, 100}, false), At({0.0, 0.0, 6.0}), light);
  light.specular = 0.5f;
  Image shiny(100, 100);
  Canvas b(shiny);
  DrawModel(b, camera, Cube({100, 100, 100}, false), At({0.0, 0.0, 6.0}), light);
  CHECK(At(plain, 50, 50).r == 40);
  CHECK(At(shiny, 50, 50).r > 150);
}

void TestClipRectangle() {
  Image image(100, 100);
  Canvas canvas(image);
  const Camera camera = MakeCamera(100, 100);
  canvas.SetClip(10, 20, 40, 60);
  DrawModel(canvas, camera, Cube({255, 255, 255}), At({0.0, 0.0, 2.0}));
  canvas.Line(-50.0f, 30.0f, 150.0f, 30.0f, {255, 255, 255});
  canvas.Rect(-10, -10, 200, 200, {255, 255, 255});
  canvas.Glow(20.0f, 30.0f, 200.0f, 255.0f, 255.0f, 255.0f, 1.0f);
  CHECK(Lit(image) == 30 * 40);
  CHECK(Lit(image, 10, 20, 40, 60) == 30 * 40);
  canvas.ResetClip();
  canvas.Set(0, 0, {1, 1, 1});
  CHECK(Lit(image) == 30 * 40 + 1);
  // Out of range limits are cut down to the image, and an empty rectangle draws nothing
  Image other(50, 50);
  Canvas small(other);
  small.SetClip(-100, -100, 1000, 1000);
  small.Clear({0, 0, 0});
  small.Rect(0, 0, 50, 50, {1, 2, 3});
  CHECK(Lit(other) == 2500);
  Image empty(50, 50);
  Canvas none(empty);
  none.SetClip(20, 20, 10, 10);
  DrawModel(none, MakeCamera(50, 50), Cube({255, 255, 255}), At({0.0, 0.0, 2.0}));
  none.Line(0.0f, 0.0f, 49.0f, 49.0f, {255, 255, 255});
  CHECK(Lit(empty) == 0);
}

void TestLines3D() {
  Image image(100, 100);
  Canvas canvas(image);
  const Camera camera = MakeCamera(100, 100);
  // Across the view, then hidden by a nearer model and not by a further one
  DrawLine3D(canvas, camera, {-5.0, 0.0, 10.0}, {5.0, 0.0, 10.0}, {0, 255, 0});
  CHECK(At(image, 50, 50).g == 255);
  canvas.Clear({0, 0, 0});
  DrawModel(canvas, camera, Cube({255, 0, 0}), At({0.0, 0.0, 5.0}));
  DrawLine3D(canvas, camera, {-5.0, 0.0, 10.0}, {5.0, 0.0, 10.0}, {0, 255, 0});
  CHECK(At(image, 50, 50).g == 0);
  canvas.Clear({0, 0, 0});
  DrawModel(canvas, camera, Cube({255, 0, 0}), At({0.0, 0.0, 20.0}));
  DrawLine3D(canvas, camera, {-5.0, 0.0, 10.0}, {5.0, 0.0, 10.0}, {0, 255, 0});
  CHECK(At(image, 50, 50).g == 255);
  // One end behind the eye: the part in front is drawn, ending at the near plane
  canvas.Clear({0, 0, 0});
  DrawLine3D(canvas, camera, {0.0, -1.0, -50.0}, {0.0, -1.0, 100.0}, {255, 255, 255});
  CHECK(Lit(image) > 0);
  // All of it behind
  canvas.Clear({0, 0, 0});
  DrawLine3D(canvas, camera, {0.0, 0.0, -5.0}, {3.0, 0.0, -50.0}, {255, 255, 255});
  CHECK(Lit(image) == 0);
}

void TestWideLines() {
  Image thin(50, 50);
  Canvas a(thin);
  a.Line(10.0f, 25.0f, 40.0f, 25.0f, {255, 255, 255});
  CHECK(Lit(thin) == 31);
  Image wide(50, 50);
  Canvas b(wide);
  b.set_line_width(3);
  b.Line(10.0f, 25.0f, 40.0f, 25.0f, {255, 255, 255});
  CHECK(Lit(wide, 0, 24, 50, 27) >= 3 * 31);
  // A wide glow is no brighter than a thin one where the blocks overlap, give or take rounding
  Image glow(50, 50);
  Canvas c(glow);
  c.set_line_width(3);
  c.Line(10.0f, 25.0f, 40.0f, 25.0f, {30, 30, 30}, true);
  CHECK(At(glow, 25, 25).r >= 27 && At(glow, 25, 25).r <= 30);
}

void TestTexturedQuad() {
  Image texture(2, 2);
  texture.Set(0, 0, {255, 0, 0});  // top left
  texture.Set(1, 0, {255, 0, 0});
  texture.Set(0, 1, {0, 0, 255});  // bottom
  texture.Set(1, 1, {0, 0, 255});
  Image image(100, 100);
  Canvas canvas(image);
  const Camera camera = MakeCamera(100, 100);
  DrawTexturedQuad(canvas, camera, {-2.0, -2.0, 4.0}, {2.0, -2.0, 4.0}, {2.0, 2.0, 4.0}, {-2.0, 2.0, 4.0}, texture);
  CHECK(At(image, 50, 30).r > At(image, 50, 30).b);  // right way up
  CHECK(At(image, 50, 70).b > At(image, 50, 70).r);
  // Reaching past the near plane it is cut, not lost, and the part on show keeps its place on the picture
  canvas.Clear({0, 0, 0});
  DrawTexturedQuad(canvas, camera, {-2.0, -3.0, -4.0}, {2.0, -3.0, -4.0}, {2.0, -3.0, 40.0}, {-2.0, -3.0, 40.0}, texture);
  CHECK(Lit(image) > 100);
  canvas.Clear({0, 0, 0});
  DrawTexturedQuad(canvas, camera, {-2.0, -2.0, -4.0}, {2.0, -2.0, -4.0}, {2.0, 2.0, -4.0}, {-2.0, 2.0, -4.0}, texture);
  CHECK(Lit(image) == 0);
  // A picture with nothing in it is ignored
  canvas.Clear({0, 0, 0});
  DrawTexturedQuad(canvas, camera, {-2.0, -2.0, 4.0}, {2.0, -2.0, 4.0}, {2.0, 2.0, 4.0}, {-2.0, 2.0, 4.0}, Image(0, 0));
  CHECK(Lit(image) == 0);
}

void TestFillPolygon() {
  Image image(100, 100);
  Canvas canvas(image);
  const Camera camera = MakeCamera(100, 100);
  FillPolygon(canvas, camera, {{-1.0, -1.0, 4.0}, {-1.0, 1.0, 4.0}, {1.0, 1.0, 4.0}, {1.0, -1.0, 4.0}}, {9, 9, 9});
  CHECK(At(image, 50, 50).r == 9);
  // Seen from either side
  canvas.Clear({0, 0, 0});
  FillPolygon(canvas, camera, {{-1.0, -1.0, 4.0}, {1.0, -1.0, 4.0}, {1.0, 1.0, 4.0}, {-1.0, 1.0, 4.0}}, {9, 9, 9});
  CHECK(At(image, 50, 50).r == 9);
  // Too few corners
  canvas.Clear({0, 0, 0});
  FillPolygon(canvas, camera, {}, {9, 9, 9});
  FillPolygon(canvas, camera, {{0.0, 0.0, 4.0}, {1.0, 0.0, 4.0}}, {9, 9, 9});
  CHECK(Lit(image) == 0);
}

void TestOutline() {
  const Camera camera = MakeCamera(100, 100);
  Model face;
  ModelBuilder b("face");
  b.Centre(0.0f, 0.0f, 1.0f);
  const std::size_t p[4] = {b.Vertex(-1.0f, -1.0f, 0.0f), b.Vertex(1.0f, -1.0f, 0.0f), b.Vertex(1.0f, 1.0f, 0.0f), b.Vertex(-1.0f, 1.0f, 0.0f)};
  b.Quad(p[0], p[1], p[2], p[3], {40, 40, 40}, true);
  face = b.Build();
  Image image(100, 100);
  Canvas canvas(image);
  canvas.set_outline({1.5f, 5.0f});  // a bright line on a dark face
  DrawModel(canvas, camera, face, At({0.0, 0.0, 4.0}));
  CHECK(At(image, 50, 50).r == 40);
  std::size_t bright = 0;
  for (std::uint32_t y = 0; y < 100; ++y) {
    for (std::uint32_t x = 0; x < 100; ++x) {
      bright += At(image, x, y).r == 200 ? 1U : 0U;
    }
  }
  CHECK(bright > 40);
}

void TestRendererFrame() {
  const Camera camera = MakeCamera(160, 120);
  const Model cube = Cube({255, 255, 255});
  const auto world = [&](Canvas& canvas, const Camera& view) { DrawModel(canvas, view, cube, At({0.0, 0.0, 6.0})); };

  RenderOptions options;
  options.bloom = false;
  options.background = {5, 6, 7};
  Renderer plain(options);
  Image one(160, 120);
  plain.Render(one, camera, world);
  CHECK(At(one, 0, 0).b == 7);
  CHECK(At(one, 80, 60).r == 255);

  // A stale frame leaves no trace
  plain.Render(one, camera, [](Canvas&, const Camera&) {});
  CHECK(At(one, 80, 60).r == 5);

  // Supersampling hands over a bigger canvas and a camera to match, and the shape stays the same size
  options.supersample = 3;
  Renderer smooth(options);
  Image three(160, 120);
  int seen_width = 0;
  smooth.Render(three, camera, [&](Canvas& canvas, const Camera& view) {
    seen_width = canvas.width();
    CHECK(canvas.line_width() == 3);
    CHECK(view.focal == camera.focal * 3.0);
    world(canvas, view);
  });
  CHECK(seen_width == 480);
  CHECK(At(three, 80, 60).r == 255);
  CHECK(At(three, 0, 0).b == 7);
  plain.Render(one, camera, world);
  std::size_t differ = 0;
  for (std::uint32_t y = 0; y < 120; ++y) {
    for (std::uint32_t x = 0; x < 160; ++x) {
      differ += (At(one, x, y).r > 128) != (At(three, x, y).r > 128) ? 1U : 0U;
    }
  }
  CHECK(differ < 40);

  // The picture can change size between frames
  Image other(80, 60);
  smooth.Render(other, MakeCamera(80, 60), world);
  CHECK(At(other, 40, 30).r == 255);

  // The overlay goes on the finished picture and is not bloomed
  options.supersample = 1;
  options.bloom = true;
  Renderer finish(options);
  finish.Render(one, camera, [](Canvas&, const Camera&) {}, [](Image& picture) { picture.Set(10, 10, {255, 255, 255}); });
  CHECK(At(one, 11, 10).r == 5);
}

void TestPost() {
  // Bloom leaves a dark picture alone and spreads a bright one
  Image dark(64, 64);
  dark.Clear({100, 100, 100});
  const Image before = dark;
  Bloom(dark);
  CHECK(dark == before);
  Image bright(64, 64);
  for (std::uint32_t y = 28; y < 36; ++y) {
    for (std::uint32_t x = 28; x < 36; ++x) {
      bright.Set(x, y, {255, 255, 255});
    }
  }
  Bloom(bright);
  CHECK(At(bright, 24, 32).r > 0);
  CHECK(At(bright, 0, 0).r == 0);
  // The threshold decides
  Image raised(64, 64);
  raised.Clear({100, 100, 100});
  Bloom(raised, 0.45f, 50.0f);
  CHECK(!(raised == before));

  Image big(8, 8);
  for (std::uint32_t y = 0; y < 8; ++y) {
    for (std::uint32_t x = 0; x < 4; ++x) {
      big.Set(x, y, {200, 100, 40});
    }
  }
  Image small(4, 4);
  Downsample(big, small, 2);
  CHECK(At(small, 0, 0).r == 200 && At(small, 1, 3).g == 100 && At(small, 2, 0).r == 0);

  // A hard edge is softened, a flat area is not. The whole picture is done by default.
  Image edge(32, 32);
  for (std::uint32_t y = 0; y < 32; ++y) {
    for (std::uint32_t x = 0; x < y; ++x) {
      edge.Set(x, y, {255, 255, 255});
    }
  }
  const Image hard = edge;
  SoftenEdges(edge);
  CHECK(!(edge == hard));
  Image flat(32, 32);
  flat.Clear({50, 50, 50});
  const Image still = flat;
  SoftenEdges(flat);
  CHECK(flat == still);

  // Streaks grow from a dot, away from the centre
  Image dot(64, 64);
  dot.Set(48, 32, {255, 255, 255});
  RadialZoom(dot, 0.3, 32.0, 32.0);
  CHECK(Lit(dot, 49, 30, 64, 34) > 2);

  // Tiny pictures do not upset them
  for (std::uint32_t size : {0U, 1U, 2U, 3U}) {
    Image tiny(size, size);
    Bloom(tiny);
    SoftenEdges(tiny);
    RadialZoom(tiny, 0.5, 0.0, 0.0);
  }
}

void TestSky() {
  const Camera camera = MakeCamera(200, 100);
  Image a(200, 100);
  Image b(200, 100);
  Canvas ca(a);
  Canvas cb(b);
  DrawSkyStars(ca, camera);
  DrawSkyStars(cb, camera);
  CHECK(Lit(a) > 0);
  CHECK(a == b);
  // Moving the camera does not move the stars; turning it does
  Camera moved = camera;
  moved.position = {500.0, 0.0, 0.0};
  Image c(200, 100);
  Canvas cc(c);
  DrawSkyStars(cc, moved);
  CHECK(a == c);
  Camera turned = camera;
  turned.orientation = FromAxisAngle({0.0, 1.0, 0.0}, 1.0);
  Image d(200, 100);
  Canvas cd(d);
  DrawSkyStars(cd, turned);
  CHECK(!(a == d));

  Image flat(200, 100);
  DrawStars(flat, 3);
  CHECK(Lit(flat) > 0);

  Image sky(64, 64);
  const Nebula nebula(12345, 32);
  nebula.Paint(sky, Quat{}, 40.0, 32.0, 32.0);
  Image again(64, 64);
  Nebula(12345, 32).Paint(again, Quat{}, 40.0, 32.0, 32.0);
  CHECK(sky == again);
}

void TestText() {
  Image image(200, 60);
  DrawText(image, "Grid 42", 5, 5, 2.0f, {255, 255, 255});
  CHECK(Lit(image) > 50);
  Image blank(200, 60);
  DrawText(blank, "  ", 5, 5, 2.0f, {255, 255, 255});
  DrawText(blank, "OFF", -5000, -5000, 2.0f, {255, 255, 255});
  CHECK(Lit(blank) == 0);
  CHECK(UiScale(Image(400, 240)) == 1.0f);
  CHECK(UiScale(Image(800, 480)) == 2.0f);
}

void TestObj() {
  const std::string text =
      "v 0 0 0\nv 2 0 0\nv 2 2 0\nv 0 2 0\n"
      "f 1 2 3 4\n";
  Model model;
  std::string error;
  CHECK(ParseObj(text, "", ObjOptions{}, &model, &error));
  CHECK(model.faces.size() == 2);
  CHECK(model.vertices.size() == 4);
  double furthest = 0.0;
  for (const Vec3& v : model.vertices) {
    furthest = std::max(furthest, static_cast<double>(std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z)));
  }
  CHECK(std::fabs(furthest - 1.0) < 1e-5);  // scaled to radius 1

  ObjOptions as_is;
  as_is.radius = 0.0f;
  CHECK(ParseObj(text, "", as_is, &model, &error));
  // The sizes in the file are kept, and only the corners that faces use are kept, in the order the faces reach them
  bool found_far_corner = false;
  for (const Vec3& v : model.vertices) {
    found_far_corner = found_far_corner || (v.x == 2.0f && v.y == 2.0f && v.z == 0.0f);
  }
  CHECK(found_far_corner);

  CHECK(!ParseObj("v 1 2\nf bad\n", "", ObjOptions{}, &model, &error));
  CHECK(!error.empty());
  CHECK(!LoadObj("/no/such/file.obj", ObjOptions{}, &model, &error));
}

void TestPng() {
  Image image(4, 3);
  image.Clear({1, 2, 3});
  const char* path = "softrender_test.png";
  CHECK(WritePng(image, path));
  std::ifstream in(path, std::ios::binary);
  char signature[8] = {};
  in.read(signature, 8);
  CHECK(std::string(signature + 1, 3) == "PNG");
  in.close();
  std::remove(path);
  CHECK(!WritePng(image, "/no/such/directory/x.png"));
}

void TestSameEveryTime() {
  // Big triangles go through the thread pool, and rows must not disturb each other. The tests are also run with one
  // thread by CTest, and this picture must come out to the same sum.
  const auto draw = [] {
    Image image(800, 800);
    Canvas canvas(image);
    Transform t = At({0.0, 0.0, 3.0});
    t.orientation = Turned(Quat{}, 0.4, 0.3, 0.7);
    DrawModel(canvas, MakeCamera(800, 800), Cube({255, 255, 255}, false), t);
    Bloom(image);
    return image;
  };
  const Image image = draw();
  CHECK(Lit(image) > 100000);
  CHECK(image == draw());
  std::uint32_t sum = 0;
  for (std::size_t i = 0; i < image.size(); ++i) {
    sum = Hash32(sum ^ image.data()[i]);
  }
  CHECK(sum == kExpectedPictureSum);
  CHECK(ParallelThreads() >= 1);
}

}  // namespace

int main() {
  TestCameraProjects();
  TestNearerHidesFurther();
  TestModelBehindTheCameraDrawsNothing();
  TestModelAcrossTheNearPlaneIsCut();
  TestFarModelIsADot();
  TestLightAndEmissive();
  TestSpecularBrightens();
  TestClipRectangle();
  TestLines3D();
  TestWideLines();
  TestTexturedQuad();
  TestFillPolygon();
  TestOutline();
  TestRendererFrame();
  TestPost();
  TestSky();
  TestText();
  TestObj();
  TestPng();
  TestSameEveryTime();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d checks failed\n", g_failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}
