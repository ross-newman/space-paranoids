#ifndef SOFTRENDER_CANVAS_H_
#define SOFTRENDER_CANVAS_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "softrender/types.h"

namespace softrender {

/// A colour from a picture, smoothly blended between its pixels. u and v run from 0 to 1 and stop at the edges.
Rgb SampleBilinear(const Image& image, float u, float v);

/// How the marked edges of a triangle are drawn
struct Outline {
  /// In pixels
  float width = 0.9f;
  /// The edge is the colour of the face times this. Below 1 it is a dark line on a solid; above 1 it is a bright line
  /// on a dark face.
  float shade = 0.35f;
};

/// A drawing surface: an image and a depth buffer, with drawing helpers in pixel coordinates. It keeps a reference to
/// the image, which must outlive it. Everything it draws stays inside the clip rectangle, the whole image at first.
class Canvas {
 public:
  explicit Canvas(Image& image);

  Image& image() { return image_; }
  int width() const { return width_; }
  int height() const { return height_; }

  /// Fill the whole image with a colour, whatever the clip, and forget all depth, to start a frame
  void Clear(Rgb colour);
  /// Forget what is in front of what, so that something can be drawn over everything
  void ResetDepth();

  /// Limit drawing to x in [x0, x1) and y in [y0, y1). The rectangle is cut down to the image. With a camera centred on
  /// the same rectangle it makes a viewport.
  void SetClip(int x0, int y0, int x1, int y1);
  void ResetClip() { SetClip(0, 0, width_, height_); }
  bool InClip(int x, int y) const { return x >= clip_x0_ && y >= clip_y0_ && x < clip_x1_ && y < clip_y1_; }

  void set_outline(Outline outline) { outline_ = outline; }
  Outline outline() const { return outline_; }

  /// How wide lines and stars are, in whole pixels (default 1). The Renderer raises it while supersampling, or thin lines
  /// would fade away as the picture shrinks.
  void set_line_width(int pixels) { line_width_ = pixels < 1 ? 1 : pixels; }
  int line_width() const { return line_width_; }

  /// The depth buffer holds one over the distance, 0 where nothing has been drawn. The position must be on the image.
  float& depth(int x, int y) { return depth_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + x]; }

  void Set(int x, int y, Rgb c) {
    if (InClip(x, y)) {
      image_.Set(x, y, c);
    }
  }

  /// Add to the pixel, stopping at white
  void Add(int x, int y, float r, float g, float b);

  /// The pixel, which must be on the image
  Rgb Get(int x, int y) const {
    const std::uint8_t* p = image_.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + x) * 3;
    return {p[0], p[1], p[2]};
  }

  void Rect(int x, int y, int w, int h, Rgb c);
  void RectOutline(int x, int y, int w, int h, Rgb c);

  /// A line, `line_width()` pixels wide. Additive lines add their colour to what is there, which makes glows.
  void Line(float x0, float y0, float x1, float y1, Rgb c, bool additive = false);

  /// A line that is hidden by anything nearer. `inv0` and `inv1` are one over the depth at its ends. It leaves
  /// the depth buffer as it is, so it can be drawn over a surface it lies on only if it is nearer than the surface.
  void DepthLine(float x0, float y0, float inv0, float x1, float y1, float inv1, Rgb c, bool additive = false);

  void Ellipse(float cx, float cy, float rx, float ry, Rgb c);
  void Disc(float cx, float cy, float r, Rgb c);

  /// A soft glow: brightest in the middle, falling to nothing at the radius
  void Glow(float cx, float cy, float r, float r_colour, float g_colour, float b_colour, float strength);

  /// A triangle with a depth test. `inv` is one over the depth at each corner. `edges` marks the edges to outline: bit 0
  /// is corner 0 to 1, bit 1 is 1 to 2 and bit 2 is 2 to 0.
  void Triangle(const float (&p)[3][2], const float (&inv)[3], Rgb colour, std::uint8_t edges);

  /// A triangle with a picture on it, correct in perspective. `uv` are the picture's coordinates at the corners. When the
  /// picture is bigger than the triangle on screen (`minify` above 1) several samples are averaged, so it stays smooth.
  void TexturedTriangle(const float (&p)[3][2], const float (&inv)[3], const float (&uv)[3][2], const Image& texture, float minify);

 private:
  /// Find the part of a line inside the clip rectangle as a range of its length. False if none of it is.
  bool ClipLine(float x0, float y0, float x1, float y1, float* t0, float* t1) const;
  /// Draw a block of `line_width()` pixels round a point on a line. A wide additive line is shared out over the
  /// overlapping blocks, so it stays as bright as a thin one.
  void Stamp(int x, int y, float inv, bool depth_test, Rgb c, bool additive);
  /// The pixels a triangle can touch, inside the clip rectangle. False if there are none.
  bool Bounds(const float (&p)[3][2], int* min_x, int* max_x, int* min_y, int* max_y) const;

  Image& image_;
  int width_;
  int height_;
  int clip_x0_;
  int clip_y0_;
  int clip_x1_;
  int clip_y1_;
  Outline outline_;
  int line_width_ = 1;
  std::vector<float> depth_;
};

}  // namespace softrender

#endif  // SOFTRENDER_CANVAS_H_
