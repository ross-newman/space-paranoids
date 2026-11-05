#include "coriolis/nebula.h"
#include "coriolis/parallel.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "coriolis/hash.h"

namespace coriolis {

namespace {

using Colour = std::array<float, 3>;

float Lattice(int x, int y, int z) {
  const std::uint32_t h = Hash32(static_cast<std::uint32_t>(x) * 73856093U ^ static_cast<std::uint32_t>(y) * 19349663U ^
                                 static_cast<std::uint32_t>(z) * 83492791U);
  return static_cast<float>(h & 0xFFFFU) / 65535.0f;
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

float SmoothStep(float low, float high, float x) {
  const float t = std::clamp((x - low) / (high - low), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

/// Smooth value noise from 0 to 1
float Noise3(float x, float y, float z) {
  const float fx = std::floor(x);
  const float fy = std::floor(y);
  const float fz = std::floor(z);
  const int ix = static_cast<int>(fx);
  const int iy = static_cast<int>(fy);
  const int iz = static_cast<int>(fz);
  const auto smooth = [](float t) { return t * t * (3.0f - 2.0f * t); };
  const float tx = smooth(x - fx);
  const float ty = smooth(y - fy);
  const float tz = smooth(z - fz);
  const float c00 = Lerp(Lattice(ix, iy, iz), Lattice(ix + 1, iy, iz), tx);
  const float c10 = Lerp(Lattice(ix, iy + 1, iz), Lattice(ix + 1, iy + 1, iz), tx);
  const float c01 = Lerp(Lattice(ix, iy, iz + 1), Lattice(ix + 1, iy, iz + 1), tx);
  const float c11 = Lerp(Lattice(ix, iy + 1, iz + 1), Lattice(ix + 1, iy + 1, iz + 1), tx);
  return Lerp(Lerp(c00, c10, ty), Lerp(c01, c11, ty), tz);
}

/// Layers of noise, each twice as fine and half as strong. Averages 0.5; the spread is stretched so that it uses more of
/// the range from 0 to 1.
float Fbm(float x, float y, float z, int octaves) {
  float sum = 0.0f;
  float amplitude = 0.5f;
  float norm = 0.0f;
  for (int i = 0; i < octaves; ++i) {
    sum += amplitude * Noise3(x, y, z);
    norm += amplitude;
    // Shift and turn a little between layers so the lattice does not show
    const float nx = 2.03f * x + 0.6f * z + 11.7f;
    const float ny = 2.03f * y - 0.4f * x + 5.3f;
    const float nz = 2.03f * z + 0.5f * y + 17.1f;
    x = nx;
    y = ny;
    z = nz;
    amplitude *= 0.5f;
  }
  return std::clamp((sum / norm - 0.5f) * 1.9f + 0.5f, 0.0f, 1.0f);
}

/// The look of one sky, chosen from the seed
struct Style {
  Colour a;     // the main colour of the gas
  Colour b;     // the second colour, where the gas is another kind
  Colour core;  // the hot colour of the thickest gas
  float scale;       // the size of the clouds: bigger means smaller clouds
  float threshold;   // where the clouds start: lower means more of the sky is covered
  float brightness;  // 0 to 1
  float warp;        // how much the clouds are twisted
  float offset[4][3];
  int blobs;           // how many separate clouds the sky has (0 to 2)
  float centre[2][3];  // where they are, as unit vectors
  float radius[2];     // how big they are, in radians of sky
};

float Unit(std::uint32_t base, std::uint32_t k) {
  return static_cast<float>(Hash32(base + k * 0x9E3779B9U) >> 8) / 16777216.0f;
}

Style StyleFor(std::uint32_t seed) {
  static const Colour kPalettes[][2] = {
      {{0.25f, 0.40f, 1.00f}, {0.45f, 0.30f, 0.95f}},  // blue and indigo
      {{0.30f, 0.20f, 0.85f}, {0.65f, 0.25f, 0.95f}},  // indigo and purple
      {{0.55f, 0.20f, 0.85f}, {1.00f, 0.40f, 0.75f}},  // purple and pink
      {{0.20f, 0.45f, 1.00f}, {0.95f, 0.45f, 0.80f}},  // blue and pink
      {{0.35f, 0.15f, 0.75f}, {0.95f, 0.30f, 0.65f}},  // deep indigo and magenta pink
      {{0.22f, 0.50f, 1.00f}, {0.60f, 0.30f, 0.95f}},  // blue and purple
  };
  const std::uint32_t base = Hash32(seed ^ 0xC10D5EEDU);
  Style s{};
  const auto& palette = kPalettes[Hash32(base) % (sizeof(kPalettes) / sizeof(kPalettes[0]))];
  for (std::size_t c = 0; c < 3; ++c) {
    const float jitter = (Unit(base, 1 + static_cast<std::uint32_t>(c)) - 0.5f) * 0.08f;
    s.a[c] = std::clamp(palette[0][c] + jitter, 0.0f, 1.0f);
    s.b[c] = std::clamp(palette[1][c] - jitter, 0.0f, 1.0f);
    // The core is the main colour washed out towards white
    s.core[c] = Lerp(s.a[c], 1.0f, 0.65f);
  }
  s.scale = 4.5f + 3.5f * Unit(base, 10);
  s.threshold = 0.50f + 0.08f * Unit(base, 11);
  s.brightness = 0.55f + 0.25f * Unit(base, 12);
  s.warp = 1.2f + 1.4f * Unit(base, 13);
  for (std::uint32_t k = 0; k < 4; ++k) {
    for (std::uint32_t c = 0; c < 3; ++c) {
      s.offset[k][c] = 40.0f * Unit(base, 20 + k * 3 + c);
    }
  }
  // Each system has one cloud, a distant patch of sky. It is never more than about 15 degrees from its centre, which
  // is a quarter of the view at the very most (the view is about 70 by 45 degrees).
  s.blobs = 1;
  for (std::uint32_t k = 0; k < 2; ++k) {
    const float z = 2.0f * Unit(base, 40 + k * 3) - 1.0f;
    const float phi = 6.2831853f * Unit(base, 41 + k * 3);
    const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    s.centre[k][0] = r * std::cos(phi);
    s.centre[k][1] = z;
    s.centre[k][2] = r * std::sin(phi);
    s.radius[k] = 0.20f + 0.07f * Unit(base, 42 + k * 3);  // out to 11.5 to 15.5 degrees from the centre at the most
  }
  return s;
}

/// The light from a unit direction, 0 to 255 per channel
Colour Evaluate(const Style& s, float dx, float dy, float dz) {
  const float px = dx * s.scale + s.offset[0][0];
  const float py = dy * s.scale + s.offset[0][1];
  const float pz = dz * s.scale + s.offset[0][2];

  // Twist the space so that the clouds curl and billow instead of looking like plain noise
  const float wx = Fbm(px * 0.8f + s.offset[1][0], py * 0.8f, pz * 0.8f, 3) - 0.5f;
  const float wy = Fbm(px * 0.8f, py * 0.8f + s.offset[1][1], pz * 0.8f, 3) - 0.5f;
  const float wz = Fbm(px * 0.8f, py * 0.8f, pz * 0.8f + s.offset[1][2], 3) - 0.5f;
  const float qx = px + wx * s.warp;
  const float qy = py + wy * s.warp;
  const float qz = pz + wz * s.warp;

  // The gas lives in a few small patches of the sky: a smooth falloff round each centre, bent by the same twist as the
  // gas so the edges are ragged, and also held inside a hard limit so no cloud can spread far
  float patch = 0.0f;
  {
    float tx = dx + wx * 0.35f;
    float ty = dy + wy * 0.35f;
    float tz = dz + wz * 0.35f;
    const float tl = std::sqrt(tx * tx + ty * ty + tz * tz);
    tx /= tl;
    ty /= tl;
    tz /= tl;
    for (int k = 0; k < s.blobs; ++k) {
      const float* c = s.centre[k];
      const float plain = std::acos(std::clamp(dx * c[0] + dy * c[1] + dz * c[2], -1.0f, 1.0f));
      const float bent = std::acos(std::clamp(tx * c[0] + ty * c[1] + tz * c[2], -1.0f, 1.0f));
      const float soft = 1.0f - SmoothStep(0.45f, 1.0f, bent / (0.9f * s.radius[k]));
      const float limit = 1.0f - SmoothStep(0.85f, 1.0f, plain / s.radius[k]);
      patch = std::max(patch, soft * limit);
    }
  }
  const float body = Fbm(qx, qy, qz, 5);
  // The patch gives the cloud its outline and the noise gives it body: thick and thin gas inside the outline
  const float density = SmoothStep(0.0f, 0.5f, patch) * (0.45f + 0.55f * SmoothStep(s.threshold - 0.2f, s.threshold + 0.15f, body));

  // Thin bright filaments, where the gas is folded
  const float ridge = 1.0f - std::fabs(2.0f * Fbm(qx * 2.1f, qy * 2.1f, qz * 2.1f, 3) - 1.0f);
  const float filament = ridge * ridge * ridge;

  // Dark dust lanes that cut across the glow
  const float dust = SmoothStep(0.52f, 0.78f, Fbm(qx * 1.7f + s.offset[3][0], qy * 1.7f + s.offset[3][1], qz * 1.7f + s.offset[3][2], 3));

  // Which kind of gas it is
  const float kind = SmoothStep(0.30f, 0.70f, Fbm(qx * 0.6f + s.offset[3][2], qy * 0.6f, qz * 0.6f + s.offset[3][0], 3));

  float intensity = density * (0.55f + 0.6f * filament);
  intensity *= 1.0f - 0.5f * dust * density;
  const float core = density * density * density * (1.0f - dust) * 0.55f;
  // Faint haze left out: very dim gradients only show as bands in 8 bit colour
  const float haze = 0.0f;

  Colour out{};
  for (std::size_t c = 0; c < 3; ++c) {
    const float gas = Lerp(s.a[c], s.b[c], kind);
    const float v = (gas * (intensity + haze) + s.core[c] * core) * s.brightness;
    // Soft limit, so the thickest gas stays dimmer than the stars
    out[c] = 0.50f * (1.0f - std::exp(-2.0f * std::max(v, 0.0f))) * 255.0f;
  }
  return out;
}

/// A direction on a cube face: `a` and `b` run from -1 to 1 across the face
Vec3d FaceDirection(std::uint32_t face, double a, double b) {
  switch (face) {
    case 0:
      return {1.0, b, a};
    case 1:
      return {-1.0, b, a};
    case 2:
      return {a, 1.0, b};
    case 3:
      return {a, -1.0, b};
    case 4:
      return {a, b, 1.0};
    default:
      return {a, b, -1.0};
  }
}

/// The face a direction hits and where on it (a and b from -1 to 1): the inverse of FaceDirection
std::uint32_t FaceOf(const Vec3d& d, double* a, double* b) {
  const double ax = std::fabs(d.x);
  const double ay = std::fabs(d.y);
  const double az = std::fabs(d.z);
  if (ax >= ay && ax >= az) {
    *a = d.z / ax;
    *b = d.y / ax;
    return d.x >= 0.0 ? 0 : 1;
  }
  if (ay >= az) {
    *a = d.x / ay;
    *b = d.z / ay;
    return d.y >= 0.0 ? 2 : 3;
  }
  *a = d.x / az;
  *b = d.y / az;
  return d.z >= 0.0 ? 4 : 5;
}

}  // namespace

Nebula::Nebula(std::uint32_t seed, std::uint32_t face_size)
    : seed_(seed),
      face_size_(std::max<std::uint32_t>(2, face_size)),
      texels_(static_cast<std::size_t>(6) * face_size_ * face_size_ * 3, 0.0f) {
  const Style style = StyleFor(seed);
  const std::size_t n = face_size_;
  for (std::uint32_t face = 0; face < 6; ++face) {
    for (std::size_t j = 0; j < n; ++j) {
      for (std::size_t i = 0; i < n; ++i) {
        const double a = (static_cast<double>(i) + 0.5) / static_cast<double>(n) * 2.0 - 1.0;
        const double b = (static_cast<double>(j) + 0.5) / static_cast<double>(n) * 2.0 - 1.0;
        const Vec3d d = Normalized(FaceDirection(face, a, b));
        const Colour c = Evaluate(style, static_cast<float>(d.x), static_cast<float>(d.y), static_cast<float>(d.z));
        float* out = &texels_[((face * n + j) * n + i) * 3];
        out[0] = c[0];
        out[1] = c[1];
        out[2] = c[2];
      }
    }
  }
}

std::array<float, 3> Nebula::Sample(const Vec3d& direction) const {
  double a = 0.0;
  double b = 0.0;
  const std::uint32_t face = FaceOf(direction, &a, &b);
  const std::size_t n = face_size_;
  const double max_index = static_cast<double>(n - 1);
  const double fx = std::clamp((a + 1.0) * 0.5 * static_cast<double>(n) - 0.5, 0.0, max_index);
  const double fy = std::clamp((b + 1.0) * 0.5 * static_cast<double>(n) - 0.5, 0.0, max_index);
  const std::size_t x0 = static_cast<std::size_t>(fx);
  const std::size_t y0 = static_cast<std::size_t>(fy);
  const std::size_t x1 = std::min(x0 + 1, n - 1);
  const std::size_t y1 = std::min(y0 + 1, n - 1);
  const float tx = static_cast<float>(fx - static_cast<double>(x0));
  const float ty = static_cast<float>(fy - static_cast<double>(y0));
  const float* base = &texels_[static_cast<std::size_t>(face) * n * n * 3];
  std::array<float, 3> out{};
  for (std::size_t c = 0; c < 3; ++c) {
    const float top = Lerp(base[(y0 * n + x0) * 3 + c], base[(y0 * n + x1) * 3 + c], tx);
    const float bottom = Lerp(base[(y1 * n + x0) * 3 + c], base[(y1 * n + x1) * 3 + c], tx);
    out[c] = Lerp(top, bottom, ty);
  }
  return out;
}

void Nebula::Paint(Image& image, const Quat& orientation, double focal, double cx, double cy) const {
  const std::uint32_t width = image.width();
  const std::uint32_t height = image.height();
  if (width == 0 || height == 0 || focal <= 0.0) {
    return;
  }
  const Vec3d right = Rotate(orientation, {1.0, 0.0, 0.0});
  const Vec3d up = Rotate(orientation, {0.0, 1.0, 0.0});
  const Vec3d forward = Rotate(orientation, {0.0, 0.0, 1.0});

  // The light changes slowly across the screen, so work it out on a coarse grid and blend between the grid points
  const std::uint32_t step = std::max<std::uint32_t>(1, (width + 399) / 400);
  const std::uint32_t grid_w = (width + step - 1) / step + 2;
  const std::uint32_t grid_h = (height + step - 1) / step + 2;
  std::vector<float> grid(static_cast<std::size_t>(grid_w) * grid_h * 3);
  ParallelRows(0, static_cast<int>(grid_h), [&](int gy_begin, int gy_end) {
  for (std::uint32_t gy = static_cast<std::uint32_t>(gy_begin); gy < static_cast<std::uint32_t>(gy_end); ++gy) {
    const double v = -(static_cast<double>(gy * step) - cy) / focal;
    for (std::uint32_t gx = 0; gx < grid_w; ++gx) {
      const double u = (static_cast<double>(gx * step) - cx) / focal;
      const std::array<float, 3> c = Sample(forward + right * u + up * v);
      float* out = &grid[(static_cast<std::size_t>(gy) * grid_w + gx) * 3];
      out[0] = c[0];
      out[1] = c[1];
      out[2] = c[2];
    }
  }
  });

  const float inv_step = 1.0f / static_cast<float>(step);
  ParallelRows(0, static_cast<int>(height), [&](int row_begin, int row_end) {
  for (std::uint32_t y = static_cast<std::uint32_t>(row_begin); y < static_cast<std::uint32_t>(row_end); ++y) {
    const float fy = (static_cast<float>(y) + 0.5f) * inv_step;
    const std::uint32_t gy = static_cast<std::uint32_t>(fy);
    const float ty = fy - static_cast<float>(gy);
    std::uint8_t* row = image.data() + static_cast<std::size_t>(y) * width * 3;
    for (std::uint32_t x = 0; x < width; ++x) {
      const float fx = (static_cast<float>(x) + 0.5f) * inv_step;
      const std::uint32_t gx = static_cast<std::uint32_t>(fx);
      const float tx = fx - static_cast<float>(gx);
      const float* p00 = &grid[(static_cast<std::size_t>(gy) * grid_w + gx) * 3];
      const float* p10 = p00 + 3;
      const float* p01 = p00 + static_cast<std::size_t>(grid_w) * 3;
      const float* p11 = p01 + 3;
      for (std::size_t c = 0; c < 3; ++c) {
        const float light = Lerp(Lerp(p00[c], p10[c], tx), Lerp(p01[c], p11[c], tx), ty);
        const float sum = static_cast<float>(row[x * 3 + c]) + light;
        row[x * 3 + c] = static_cast<std::uint8_t>(std::min(255.0f, sum + 0.5f));
      }
    }
  }
  });
}

void DrawNebula(Image& image, std::uint32_t seed, std::uint32_t frame) {
  static thread_local std::optional<Nebula> cached;
  if (!cached || cached->seed() != seed) {
    cached.emplace(seed);
  }
  const double t = static_cast<double>(frame);
  const Quat orientation = Normalized(FromAxisAngle({0.0, 1.0, 0.0}, t * 0.0005 + 0.4) *
                                      FromAxisAngle({1.0, 0.0, 0.0}, 0.15 * std::sin(t * 0.0007)));
  const double w = static_cast<double>(image.width());
  const double h = static_cast<double>(image.height());
  cached->Paint(image, orientation, 0.8 * w, w / 2.0, h / 2.0);
}

}  // namespace coriolis
