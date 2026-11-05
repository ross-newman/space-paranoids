# softrender

A small software 3D renderer in C++17, built as a static library (`libsoftrender.a`). It draws into a plain RGB24 picture
in memory and depends on nothing but the C++ standard library and threads, so it runs anywhere and shows up through
whatever you like: an SDL texture, a PNG, a test.

It was taken out of the flight view of the game [Coriolis](../coralis), with everything about that game left behind.

## What it has

| Header | Provides |
| --- | --- |
| `renderer.h` | `Renderer`: makes a whole frame, with supersampling, bloom and edge softening |
| `camera.h` | `Camera`, `MakeCamera`: a pinhole camera in world space |
| `scene.h` | `DrawModel`, `FillPolygon`, `DrawLine3D`, `DrawTexturedQuad`, `Transform`, `Lighting` |
| `canvas.h` | `Canvas`: an image with a depth buffer, a clip rectangle, triangles, lines, glows |
| `model.h`, `model_builder.h` | Meshes, and building them from boxes, rings, quads and fans |
| `obj.h` | Loading Wavefront OBJ with MTL colours |
| `post.h` | `Bloom`, `SoftenEdges`, `Downsample`, `RadialZoom` |
| `stars.h`, `nebula.h` | Procedural skies, fixed to the world or flat |
| `text.h` | A vector font and smooth 2D lines, circles and discs |
| `png.h` | `WritePng` |
| `parallel.h` | `ParallelRows`, the thread pool the big fills use |

`#include "softrender/softrender.h"` brings in all of it.

## A frame

```cpp
#include "softrender/softrender.h"
using namespace softrender;

Image image(960, 540);
Renderer renderer;

ModelBuilder builder("block");
builder.Box(-1, -1, -1, 1, 1, 1, {200, 60, 230}, {255, 240, 120}, true);
const Model block = builder.Finish(1.0f);

Camera camera = MakeCamera(image.width(), image.height(), 70.0);
camera.position = {0, 3, -10};
camera.orientation = LookAlong(Vec3d{0, -3, 10}, {0, 1, 0});

renderer.Render(image, camera,
                [&](Canvas& canvas, const Camera& view) {
                  DrawSkyStars(canvas, view);
                  Transform t;
                  t.position = {0, 1, 0};
                  DrawModel(canvas, view, block, t);
                },
                [](Image& hud) { DrawText(hud, "SCORE 100", 10, 10, 2.0f, {255, 255, 255}); });
```

The world is drawn with the canvas and camera handed to it, which are bigger than the picture while supersampling. To show
the result with SDL2, copy `image.data()` into an `SDL_PIXELFORMAT_RGB24` streaming texture: `examples/window.cpp` is the
whole of it.

- World: +x right, +y up, +z forward. A camera or a model faces along its own +z. It is left handed.
- Faces are shaded flat and need a `centre` inside the mesh to tell outside from in; `ModelBuilder` sets it.
- For a bright line on a dark face, use an `Outline` with `shade` above 1 and dark face colours.
- `Outline` and `Canvas::set_line_width` keep their look across supersampling when you go through `Renderer`.

## Limits

No textures on models (only `DrawTexturedQuad`), no alpha or sprites, no reading of image files, one directional light, and
no shadows. Pictures must be 8 bit RGB.

## Building

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build
cmake --install build --prefix /some/where
```

The library is `build/libsoftrender.a`. Link it with threads (`-pthread`).

To use it from another CMake project, either point at the source:

```cmake
add_subdirectory(engine)
target_link_libraries(my_game PRIVATE softrender::softrender)
```

or use the installed copy:

```cmake
find_package(softrender REQUIRED)
target_link_libraries(my_game PRIVATE softrender::softrender)
```

or plainly, without CMake:

```sh
g++ -std=c++17 -O2 -I/some/where/include my_game.cpp /some/where/lib/libsoftrender.a -pthread
```

`SOFTRENDER_THREADS=1` in the environment turns the thread pool off. The examples and tests are built when this is the top
level project; set `SOFTRENDER_BUILD_EXAMPLES` or `SOFTRENDER_BUILD_TESTS` to have them otherwise. `softrender_scene` draws a
frame to a PNG, and `softrender_window` shows it moving in a window if SDL2 is installed.
