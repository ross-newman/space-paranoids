#ifndef SOFTRENDER_TEXT_H_
#define SOFTRENDER_TEXT_H_

#include <cstdint>
#include <string>

#include "softrender/types.h"

namespace softrender {

/// Draw text in a vector stroke font at pixel position (x, y). The font is drawn as smooth anti-aliased lines, so it is
/// clean at any size: a character cell is 6*scale pixels wide and its capitals are 7*scale high. It has capitals,
/// lower case, digits and the usual punctuation; any other character is drawn as a space. The scale need not be a
/// whole number, so text can be sized to the picture exactly.
void DrawText(Image& image, const std::string& text, std::int64_t x, std::int64_t y, float scale, Rgb colour);

/// The text scale for screens and menus on a picture this size: the same share of the picture at any resolution. A
/// screen is laid out as 240 units high and 400 wide (a row of text is 10 units), so the scale follows whichever of the
/// height and the width is the tighter fit. Never below 1.
float UiScale(const Image& image);

/// Smooth anti-aliased lines, circles and discs in the same weight as the text, so graphics and text sit together: a
/// line or ring at `scale` is as thick as the strokes of text at that scale. Positions are in pixels, and anything off
/// the picture is clipped.
void DrawLine(Image& image, float x0, float y0, float x1, float y1, float scale, Rgb colour);
void DrawCircle(Image& image, float cx, float cy, float radius, float scale, Rgb colour);
void DrawDisc(Image& image, float cx, float cy, float radius, Rgb colour);

/// Draw text in the bottom left corner, sized to the height of the image
void DrawCaption(Image& image, const std::string& text, Rgb colour = {120, 255, 160});

}  // namespace softrender

#endif  // SOFTRENDER_TEXT_H_
