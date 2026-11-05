// Draw the opening screen at three moments to a PNG, and check that it types on and that the cursor flashes at the end:
//   terminal_preview [out.png]

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "softrender/softrender.h"
#include "terminal.h"

namespace {

using namespace softrender;

std::vector<std::uint8_t> Frame(maze::Terminal& terminal, Renderer& renderer, Image& image, double seconds) {
  renderer.Render(
      image, MakeCamera(image.width(), image.height()), [&](Canvas& canvas, const Camera&) { terminal.Draw(canvas.image(), seconds); },
      [&](Image& picture) { terminal.DrawGlass(picture); });
  return {image.data(), image.data() + image.size()};
}

std::size_t Lit(const std::vector<std::uint8_t>& frame) {
  std::size_t lit = 0;
  for (std::size_t i = 1; i < frame.size(); i += 3) {
    lit += frame[i] > 100 ? 1 : 0;
  }
  return lit;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string path = argc > 1 ? argv[1] : "terminal.png";
  constexpr int kWidth = 960;
  constexpr int kHeight = 540;
  maze::Terminal terminal(kWidth, kHeight);
  Renderer renderer(maze::Terminal::Options(kHeight));
  Image image(kWidth, kHeight);
  const double end = terminal.TypingTime();

  bool ok = true;
  // More of the screen lights up as it types
  std::size_t before = 0;
  for (double t = 0.0; t < end; t += end / 8.0) {
    const std::size_t lit = Lit(Frame(terminal, renderer, image, t));
    ok = ok && lit >= before;
    before = lit;
  }
  // After that the only thing that changes is the cursor, on and off and back to where it was
  const std::vector<std::uint8_t> on = Frame(terminal, renderer, image, end + 0.1);
  const std::vector<std::uint8_t> off = Frame(terminal, renderer, image, end + 0.6);
  const std::vector<std::uint8_t> on_again = Frame(terminal, renderer, image, end + 1.0);
  ok = ok && Lit(off) < Lit(on) && Lit(on_again) > Lit(off);
  std::printf("typing takes %.1f s; %zu lit with the cursor on, %zu off: %s\n", end, Lit(on), Lit(off), ok ? "ok" : "BAD");

  Image sheet(kWidth, kHeight * 2);
  const auto paste = [&sheet](const std::vector<std::uint8_t>& frame, int row) {
    for (int y = 0; y < kHeight; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        const std::uint8_t* p = frame.data() + (static_cast<std::size_t>(y) * kWidth + x) * 3;
        sheet.Set(x, row * kHeight + y, {p[0], p[1], p[2]});
      }
    }
  };
  paste(Frame(terminal, renderer, image, end * 0.55), 0);
  paste(on, 1);
  if (!WritePng(sheet, path)) {
    return 1;
  }
  return ok ? 0 : 1;
}
