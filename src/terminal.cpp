#include "terminal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include "softrender/parallel.h"

namespace maze {

namespace {

using softrender::Image;
using softrender::Rgb;

struct Line {
  const char* text;
  int indent;          // in characters
  int underline_from;  // the first character to underline, or -1 for none
};

const Line kLines[] = {
    {"MAZENET TERM 7-204  USER 0041973  06:00  STATUS", 0, -1},
    {"COMBAT PROGRAM:  KITE HUNT", 2, -1},
    {"KITES SIGHTED IN ALL CORRIDORS", 2, -1},
    {"TANK UNIT ASSIGNED TO OPERATOR", 2, -1},
    {"THIS MISSION  TOP PRIORITY", 2, 14},
    {"AWAITING OPERATOR", 2, -1},
};
constexpr std::size_t kLineCount = sizeof(kLines) / sizeof(kLines[0]);

constexpr Rgb kGreen{40, 255, 90};
constexpr Rgb kPhosphorAtRest{2, 13, 6};  // the screen is never quite black while it is on
constexpr float kScanlineDark = 0.62f;
constexpr double kPerCharacter = 0.035;
constexpr double kPerLine = 0.35;     // the pause as the carriage returns
constexpr double kFlashPeriod = 0.9;  // of the cursor, on and off
constexpr Rgb kFlash{175, 255, 195};   // a letter still settling is hotter
constexpr Rgb kSpark{20, 150, 60};     // the static round the cursor
constexpr int kSettling = 4;           // how many of the newest letters are still noise
constexpr double kNoiseRate = 28.0;    // how many times a second the noise changes
constexpr int kSparks = 26;

/// A 5 by 7 dot font: a row of a letter is a number, its high bit on the left
struct Glyph {
  char letter;
  std::uint8_t rows[7];
};

const Glyph kFont[] = {
    {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}}, {'C', {14, 17, 16, 16, 16, 17, 14}},
    {'D', {30, 17, 17, 17, 17, 17, 30}}, {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}}, {'I', {14, 4, 4, 4, 4, 4, 14}},
    {'J', {7, 2, 2, 2, 2, 18, 12}},      {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 21, 19, 17, 17, 17}}, {'O', {14, 17, 17, 17, 17, 17, 14}},
    {'P', {30, 17, 17, 30, 16, 16, 16}}, {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},   {'T', {31, 4, 4, 4, 4, 4, 4}},       {'U', {17, 17, 17, 17, 17, 17, 14}},
    {'V', {17, 17, 17, 17, 17, 10, 4}},  {'W', {17, 17, 17, 21, 21, 27, 17}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}},     {'Z', {31, 1, 2, 4, 8, 16, 31}},     {'0', {14, 17, 19, 21, 25, 17, 14}},
    {'1', {4, 12, 4, 4, 4, 4, 14}},      {'2', {14, 17, 1, 2, 4, 8, 31}},     {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},     {'5', {31, 16, 30, 1, 1, 17, 14}},   {'6', {6, 8, 16, 30, 17, 17, 14}},
    {'7', {31, 1, 2, 4, 8, 8, 8}},       {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 2, 12}},
    {':', {0, 12, 12, 0, 12, 12, 0}},    {'-', {0, 0, 0, 31, 0, 0, 0}},
};
constexpr std::uint32_t kGlyphCount = sizeof(kFont) / sizeof(kFont[0]);

const Glyph* FindGlyph(char letter) {
  for (const Glyph& glyph : kFont) {
    if (glyph.letter == letter) {
      return &glyph;
    }
  }
  return nullptr;
}

/// A number that jumps about with its arguments, the same each time
std::uint32_t Scatter(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
  std::uint32_t h = a * 2654435761u ^ (b * 40503u + 7u) * 2246822519u ^ c * 3266489917u;
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  return h;
}

/// How much of each dot's height is the gap between the rows, which is the dark of a scan line
int ScanGap(int pitch) { return std::max(1, pitch / 3); }

/// One dot of a letter. Dots side by side run together and rows stand apart, as a beam draws them line by line.
void Dot(Image& image, int x, int y, int pitch, Rgb colour) {
  for (int j = 0; j < pitch - ScanGap(pitch); ++j) {
    for (int i = 0; i < pitch; ++i) {
      image.Set(x + i, y + j, colour);
    }
  }
}

void DrawGlyph(Image& image, const Glyph& glyph, int x, int y, int pitch, Rgb colour) {
  for (int row = 0; row < 7; ++row) {
    for (int column = 0; column < 5; ++column) {
      if ((glyph.rows[row] >> (4 - column) & 1) != 0) {
        Dot(image, x + column * pitch, y + row * pitch, pitch, colour);
      }
    }
  }
}

/// How much of the text has been typed by this time: whole lines and letters of the line after them. The cursor sits
/// just after the last letter.
void TypedAt(double seconds, std::size_t* line, std::size_t* letters) {
  double left = seconds;
  for (std::size_t i = 0; i < kLineCount; ++i) {
    const std::size_t length = std::string(kLines[i].text).size();
    const double typing = static_cast<double>(length) * kPerCharacter;
    if (left < typing) {
      *line = i;
      *letters = static_cast<std::size_t>(left / kPerCharacter);
      return;
    }
    left -= typing;
    if (i + 1 < kLineCount) {
      left -= kPerLine;
      if (left < 0.0) {
        *line = i;
        *letters = length;
        return;
      }
    }
  }
  *line = kLineCount - 1;
  *letters = std::string(kLines[kLineCount - 1].text).size();
}

}  // namespace

Terminal::Terminal(int width, int height) : shade_(static_cast<std::size_t>(width) * height) {
  // The picture tube is dimmer towards its corners
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float dx = (static_cast<float>(x) - width / 2.0f) / (width / 2.0f);
      const float dy = (static_cast<float>(y) - height / 2.0f) / (height / 2.0f);
      const float d = dx * dx + dy * dy;
      shade_[static_cast<std::size_t>(y) * width + x] = std::max(0.3f, 1.0f - 0.2f * d * d - 0.02f * d);
    }
  }
}

double Terminal::TypingTime() const {
  double total = kPerLine * static_cast<double>(kLineCount - 1);
  for (const Line& line : kLines) {
    total += static_cast<double>(std::string(line.text).size()) * kPerCharacter;
  }
  return total;
}

std::size_t Terminal::LettersTyped(double seconds) const {
  std::size_t line = 0;
  std::size_t letters = 0;
  TypedAt(seconds, &line, &letters);
  for (std::size_t i = 0; i < line; ++i) {
    letters += std::string(kLines[i].text).size();
  }
  return letters;
}

void Terminal::Draw(Image& image, double seconds) const {
  const int width = static_cast<int>(image.width());
  const int height = static_cast<int>(image.height());
  const int pitch = std::max(2, width / 320);  // of the dots
  const int cell = 6 * pitch;
  const int row = 12 * pitch;
  // On whole dots, so that the rows of dots fall in step with the scan lines
  const int left = width * 7 / 100 / pitch * pitch;
  const int top = height / 5 / pitch * pitch;

  image.Clear(kPhosphorAtRest);
  std::size_t typed_line = 0;
  std::size_t typed_letters = 0;
  TypedAt(seconds, &typed_line, &typed_letters);
  const bool typing = seconds < TypingTime();
  const std::uint32_t tick = static_cast<std::uint32_t>(seconds * kNoiseRate);

  for (std::size_t i = 0; i <= typed_line; ++i) {
    const Line& line = kLines[i];
    const std::string text(line.text);
    const std::size_t shown = i < typed_line ? text.size() : typed_letters;
    const int x = left + cell * line.indent;
    const int y = top + row * static_cast<int>(i);
    for (std::size_t c = 0; c < shown; ++c) {
      if (text[c] == ' ') {
        continue;
      }
      const int at = x + cell * static_cast<int>(c);
      if (typing && i == typed_line && c + kSettling >= shown) {
        // Not settled yet: noise that changes faster than it can be read
        const Glyph& noise = kFont[Scatter(static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(c), tick) % kGlyphCount];
        DrawGlyph(image, noise, at, y, pitch, kFlash);
      } else if (const Glyph* glyph = FindGlyph(text[c])) {
        DrawGlyph(image, *glyph, at, y, pitch, kGreen);
      }
    }
    if (line.underline_from >= 0) {
      for (std::size_t c = static_cast<std::size_t>(line.underline_from); c < shown; ++c) {
        for (int d = 0; d < 6; ++d) {
          Dot(image, x + cell * static_cast<int>(c) + d * pitch, y + 8 * pitch, pitch, kGreen);
        }
      }
    }
  }

  const int cursor_x = left + cell * (kLines[typed_line].indent + static_cast<int>(typed_letters));
  const int cursor_y = top + row * static_cast<int>(typed_line);
  const double waiting = seconds - TypingTime();
  if (waiting < 0.0 || std::fmod(waiting, kFlashPeriod) < kFlashPeriod / 2.0) {
    for (int dot_y = 0; dot_y < 7; ++dot_y) {
      for (int dot_x = 0; dot_x < 5; ++dot_x) {
        Dot(image, cursor_x + dot_x * pitch, cursor_y + dot_y * pitch, pitch, kGreen);
      }
    }
  }

  // Static where the data is coming in
  if (typing) {
    for (int s = 0; s < kSparks; ++s) {
      const std::uint32_t scatter = Scatter(static_cast<std::uint32_t>(s), tick, 11u);
      const int dx = static_cast<int>(scatter % 16u) - 4;
      const int dy = static_cast<int>((scatter >> 8) % 13u) - 3;
      Dot(image, cursor_x + dx * pitch * 2, cursor_y + dy * pitch, pitch, kSpark);
    }
  }
}

void Terminal::DrawGlass(Image& image) const {
  const int width = static_cast<int>(image.width());
  const int height = static_cast<int>(image.height());
  // The scan lines are as far apart as the rows of dots, and fall in the gaps between them
  const int pitch = std::max(2, width / 320);
  const int gap = ScanGap(pitch);
  softrender::ParallelRows(0, height, [&](int begin, int end) {
    for (int y = begin; y < end; ++y) {
      const float line = y % pitch >= pitch - gap ? kScanlineDark : 1.0f;
      const float* edge = shade_.data() + static_cast<std::size_t>(y) * width;
      std::uint8_t* pixel = image.data() + static_cast<std::size_t>(y) * width * 3;
      for (int x = 0; x < width; ++x, pixel += 3) {
        const float amount = line * edge[x];
        pixel[0] = static_cast<std::uint8_t>(pixel[0] * amount);
        pixel[1] = static_cast<std::uint8_t>(pixel[1] * amount);
        pixel[2] = static_cast<std::uint8_t>(pixel[2] * amount);
      }
    }
  });
}

softrender::RenderOptions Terminal::Options(int height) {
  softrender::RenderOptions options;
  options.background = {0, 0, 0};
  options.bloom_threshold = 60.0f;
  options.bloom_cell = std::max(4, height / 135);  // the glow keeps its size on the picture
  options.bloom_strength = 1.3f;
  return options;
}

}  // namespace maze
