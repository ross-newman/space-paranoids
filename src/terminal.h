#ifndef SPACE_PARANOIDS_TERMINAL_H_
#define SPACE_PARANOIDS_TERMINAL_H_

#include <cstddef>
#include <vector>

#include "softrender/renderer.h"
#include "softrender/types.h"

// The opening screen: green text on a glowing CRT terminal, typed out a letter at a time, with a cursor left flashing at
// the end. What it says is the table at the top of terminal.cpp.

namespace maze {

class Terminal {
 public:
  /// A screen for pictures this size
  Terminal(int width, int height);

  /// How many letters have been typed by this time
  std::size_t LettersTyped(double seconds) const;

  /// How long the typing takes, in seconds
  double TypingTime() const;

  /// Draw the screen as it is this long after it started. Draw it in a renderer's world, so that the letters glow.
  void Draw(softrender::Image& image, double seconds) const;

  /// Draw the scan lines and the dimming at the corners. Do it in a renderer's overlay, or the glow would blur them.
  void DrawGlass(softrender::Image& image) const;

  /// How a renderer should be set up to draw it, for the glow of the letters
  static softrender::RenderOptions Options(int height);

 private:
  std::vector<float> shade_;  // how bright the tube is at each pixel
};

}  // namespace maze

#endif  // SPACE_PARANOIDS_TERMINAL_H_
