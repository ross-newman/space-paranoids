#include "softrender/post.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "softrender/parallel.h"

namespace softrender {

void RadialZoom(Image& img, double amount, double cx, double cy) {
  const int w = static_cast<int>(img.width());
  const int h = static_cast<int>(img.height());
  const Image source = img;
  const std::uint8_t* src = source.data();
  std::uint8_t* dst = img.data();
  constexpr int kSamples = 24;
  const double zoom = 1.0 + 7.0 * amount;
  const double white = amount * amount * amount * 0.85;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int peak[3] = {0, 0, 0};
      for (int i = 0; i < kSamples; ++i) {
        // Each sample looks at the same ray a little further out, so the image stretches away from the centre
        const double k = std::pow(zoom, static_cast<double>(i) / (kSamples - 1));
        const int sx = static_cast<int>(cx + (x - cx) / k);
        const int sy = static_cast<int>(cy + (y - cy) / k);
        const std::size_t at = (static_cast<std::size_t>(std::clamp(sy, 0, h - 1)) * static_cast<std::size_t>(w) +
                                static_cast<std::size_t>(std::clamp(sx, 0, w - 1))) * 3;
        for (int c = 0; c < 3; ++c) {
          peak[c] = std::max(peak[c], static_cast<int>(src[at + static_cast<std::size_t>(c)]));
        }
      }
      const std::size_t out = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3;
      for (int c = 0; c < 3; ++c) {
        // The brightest thing along the ray wins, so a star becomes a bright streak
        const double v = peak[c];
        dst[out + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(std::clamp(v + (255.0 - v) * white, 0.0, 255.0));
      }
    }
  }
}

void Bloom(Image& img, float strength, float threshold, int cell_size) {
  const int w = static_cast<int>(img.width());
  const int h = static_cast<int>(img.height());
  const int cell = std::max(1, cell_size);
  const int bw = std::max(1, w / cell);
  const int bh = std::max(1, h / cell);
  std::vector<float> a(static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh) * 3, 0.0f);
  std::vector<float> b(a.size(), 0.0f);
  ParallelRows(0, bh, [&](int by_begin, int by_end) {
  for (int by = by_begin; by < by_end; ++by) {
    for (int bx = 0; bx < bw; ++bx) {
      float sum[3] = {0, 0, 0};
      int n = 0;
      for (int y = by * cell; y < std::min(h, by * cell + cell); ++y) {
        for (int x = bx * cell; x < std::min(w, bx * cell + cell); ++x) {
          const std::uint8_t* p = img.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3;
          for (int k = 0; k < 3; ++k) {
            sum[k] += std::max(0.0f, static_cast<float>(p[k]) - threshold);
          }
          ++n;
        }
      }
      for (int k = 0; k < 3; ++k) {
        a[(static_cast<std::size_t>(by) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(bx)) * 3 + static_cast<std::size_t>(k)] = sum[k] / static_cast<float>(std::max(n, 1));
      }
    }
  }
  });
  const float weights[7] = {1, 2, 3, 4, 3, 2, 1};
  for (int pass = 0; pass < 2; ++pass) {
    ParallelRows(0, bh, [&](int ya, int yb) {
    for (int y = ya; y < yb; ++y) {
      for (int x = 0; x < bw; ++x) {
        for (int k = 0; k < 3; ++k) {
          float s = 0.0f;
          for (int i = -3; i <= 3; ++i) {
            const int xx = std::clamp(x + i, 0, bw - 1);
            s += weights[i + 3] * a[(static_cast<std::size_t>(y) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(xx)) * 3 + static_cast<std::size_t>(k)];
          }
          b[(static_cast<std::size_t>(y) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(k)] = s / 16.0f;
        }
      }
    }
    });
    ParallelRows(0, bh, [&](int ya, int yb) {
    for (int y = ya; y < yb; ++y) {
      for (int x = 0; x < bw; ++x) {
        for (int k = 0; k < 3; ++k) {
          float s = 0.0f;
          for (int i = -3; i <= 3; ++i) {
            const int yy = std::clamp(y + i, 0, bh - 1);
            s += weights[i + 3] * b[(static_cast<std::size_t>(yy) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(k)];
          }
          a[(static_cast<std::size_t>(y) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(k)] = s / 16.0f;
        }
      }
    }
    });
  }
  ParallelRows(0, h, [&](int row_begin, int row_end) {
  for (int y = row_begin; y < row_end; ++y) {
    const float fy = std::clamp(static_cast<float>(y) / static_cast<float>(cell) - 0.5f, 0.0f, static_cast<float>(bh - 1));
    const int y0 = static_cast<int>(fy);
    const int y1 = std::min(y0 + 1, bh - 1);
    const float ty = fy - static_cast<float>(y0);
    for (int x = 0; x < w; ++x) {
      const float fx = std::clamp(static_cast<float>(x) / static_cast<float>(cell) - 0.5f, 0.0f, static_cast<float>(bw - 1));
      const int x0 = static_cast<int>(fx);
      const int x1 = std::min(x0 + 1, bw - 1);
      const float tx = fx - static_cast<float>(x0);
      std::uint8_t* p = img.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3;
      for (int k = 0; k < 3; ++k) {
        const auto at = [&](int xx, int yy) {
          return a[(static_cast<std::size_t>(yy) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(xx)) * 3 + static_cast<std::size_t>(k)];
        };
        const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
        const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
        const float glow = (top + (bottom - top) * ty) * strength;
        p[k] = static_cast<std::uint8_t>(std::min(255.0f, static_cast<float>(p[k]) + glow));
      }
    }
  }
  });
}

void SoftenEdges(Image& img, int rows) {
  const int w = static_cast<int>(img.width());
  rows = std::min(rows, static_cast<int>(img.height()));
  if (w < 3 || rows < 3) {
    return;
  }
  const std::vector<std::uint8_t> src(img.data(), img.data() + static_cast<std::size_t>(w) * static_cast<std::size_t>(rows) * 3);
  const auto at = [&](int x, int y) { return &src[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3]; };
  const auto luma = [&](int x, int y) {
    const std::uint8_t* p = at(x, y);
    return 77 * static_cast<int>(p[0]) + 150 * static_cast<int>(p[1]) + 29 * static_cast<int>(p[2]);
  };
  ParallelRows(1, rows - 1, [&](int y_begin, int y_end) {
    for (int y = y_begin; y < y_end; ++y) {
      for (int x = 1; x < w - 1; ++x) {
        const int c = luma(x, y);
        const int n = luma(x, y - 1);
        const int s = luma(x, y + 1);
        const int e = luma(x + 1, y);
        const int wl = luma(x - 1, y);
        const int hi = std::max({c, n, s, e, wl});
        const int lo = std::min({c, n, s, e, wl});
        const int range = hi - lo;
        if (range < std::max(14 * 256, hi / 8)) {
          continue;
        }
        if (c == hi && c - std::max(std::max(n, s), std::max(e, wl)) > range * 6 / 10) {
          continue;  // a lone bright dot
        }
        const int nw = luma(x - 1, y - 1);
        const int ne = luma(x + 1, y - 1);
        const int sw = luma(x - 1, y + 1);
        const int se = luma(x + 1, y + 1);
        const int horizontal = std::abs(nw + sw - 2 * wl) + 2 * std::abs(n + s - 2 * c) + std::abs(ne + se - 2 * e);
        const int vertical = std::abs(nw + ne - 2 * n) + 2 * std::abs(wl + e - 2 * c) + std::abs(sw + se - 2 * s);
        // The pixel across the edge, and the two along it
        int ox = 0;
        int oy = 0;
        int ax = 0;
        int ay = 0;
        if (horizontal >= vertical) {
          oy = std::abs(n - c) >= std::abs(s - c) ? -1 : 1;
          ax = 1;
        } else {
          ox = std::abs(wl - c) >= std::abs(e - c) ? -1 : 1;
          ay = 1;
        }
        const std::uint8_t* pc = at(x, y);
        const std::uint8_t* po = at(x + ox, y + oy);
        const std::uint8_t* pa = at(x + ax, y + ay);
        const std::uint8_t* pb = at(x - ax, y - ay);
        std::uint8_t* out = img.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3;
        for (int k = 0; k < 3; ++k) {
          out[k] = static_cast<std::uint8_t>((4 * pc[k] + 2 * po[k] + pa[k] + pb[k] + 4) / 8);
        }
      }
    }
  });
}

void Downsample(const Image& big, Image& out, int factor) {
  const std::uint32_t f = static_cast<std::uint32_t>(factor);
  for (std::uint32_t y = 0; y < out.height(); ++y) {
    for (std::uint32_t x = 0; x < out.width(); ++x) {
      std::uint32_t sum[3] = {0, 0, 0};
      for (std::uint32_t j = 0; j < f; ++j) {
        for (std::uint32_t i = 0; i < f; ++i) {
          const std::uint8_t* p = big.data() + (static_cast<std::size_t>(y * f + j) * big.width() + (x * f + i)) * 3;
          sum[0] += p[0];
          sum[1] += p[1];
          sum[2] += p[2];
        }
      }
      out.Set(x, y, {static_cast<std::uint8_t>(sum[0] / (f * f)), static_cast<std::uint8_t>(sum[1] / (f * f)), static_cast<std::uint8_t>(sum[2] / (f * f))});
    }
  }
}

}  // namespace softrender
