// The demo scene in a window: how to show a softrender picture with SDL2. The picture is RGB24, so it goes straight into
// a streaming texture. Escape quits.

#include <SDL.h>

#include <cstdio>

#include "demo_scene.h"

int main(int, char**) {
  using namespace softrender;
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
    return 1;
  }
  const int width = 960;
  const int height = 540;
  SDL_Window* window = SDL_CreateWindow("softrender", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_RESIZABLE);
  SDL_Renderer* output = window != nullptr ? SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC) : nullptr;
  SDL_Texture* texture = output != nullptr ? SDL_CreateTexture(output, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height) : nullptr;
  if (texture == nullptr) {
    std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_RenderSetLogicalSize(output, width, height);

  Image image(width, height);
  Renderer renderer;
  const demo::Scene scene;
  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
        running = false;
      }
    }
    const double time = SDL_GetTicks() / 1000.0;
    renderer.Render(image, scene.CameraFor(image, time), [&](Canvas& canvas, const Camera& camera) { scene.Draw(canvas, camera, time); });
    SDL_UpdateTexture(texture, nullptr, image.data(), width * 3);
    SDL_RenderClear(output);
    SDL_RenderCopy(output, texture, nullptr, nullptr);
    SDL_RenderPresent(output);
  }

  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(output);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
