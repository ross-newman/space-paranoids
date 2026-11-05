#include "coriolis/renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace coriolis {

void Renderer::DrawShip(Image& image, const Model& model, float angle) const {
  const std::uint32_t width = image.width();
  const std::uint32_t height = image.height();
  if (width == 0 || height == 0) {
    return;
  }

  // Render supersampled on smaller pictures to smooth the edges
  std::uint32_t ss = options_.supersample;
  if (ss == 0) {
    ss = (static_cast<std::uint64_t>(width) * height <= 1024U * 768U) ? 2 : 1;
  }
  const std::int32_t rw = static_cast<std::int32_t>(width * ss);
  const std::int32_t rh = static_cast<std::int32_t>(height * ss);
  std::vector<std::uint8_t> ship(static_cast<std::size_t>(rw) * static_cast<std::size_t>(rh) * 3, 0);
  std::vector<float> depth_buffer(static_cast<std::size_t>(rw) * static_cast<std::size_t>(rh), 0.0f);

  // Orientation: a steady spin with a slow nod and roll. A rolling ship spins about its own axis instead, showing
  // its front to the viewer.
  const bool roll_spin = model.spin == Spin::kRoll;
  const bool sway = model.spin == Spin::kSway;
  const float yaw_view = roll_spin ? kPi + 0.5f : (sway ? kPi + 0.7f * std::sin(angle * 0.9f) : angle);
  const float pitch = roll_spin ? 0.25f : (sway ? 0.1f * std::sin(angle * 0.5f) : 0.42f + 0.12f * std::sin(angle * 0.5f));
  const float roll = (roll_spin || sway) ? 0.0f : 0.16f * std::sin(angle * 0.7f + 1.0f);
  const float cos_y = std::cos(yaw_view);
  const float sin_y = std::sin(yaw_view);
  const float cos_p = std::cos(pitch);
  const float sin_p = std::sin(pitch);
  const float cos_r = std::cos(roll);
  const float sin_r = std::sin(roll);
  const float cos_s = std::cos(angle);
  const float sin_s = std::sin(angle);
  auto rotate = [&](const Vec3& v) {
    // The spin about the ship's own z axis
    const Vec3 p = roll_spin ? Vec3{v.x * cos_s - v.y * sin_s, v.x * sin_s + v.y * cos_s, v.z} : v;
    const float x1 = p.x * cos_y + p.z * sin_y;
    const float z1 = -p.x * sin_y + p.z * cos_y;
    const float y2 = p.y * cos_p + z1 * sin_p;
    const float z2 = -p.y * sin_p + z1 * cos_p;
    return Vec3{x1 * cos_r - y2 * sin_r, x1 * sin_r + y2 * cos_r, z2};
  };

  // Perspective camera: +z is away from the viewer
  const float camera_distance = options_.camera_distance;
  const float focal = options_.focal * static_cast<float>(std::min(rw, rh));
  const float centre_x = static_cast<float>(rw) / 2.0f;
  const float centre_y = static_cast<float>(rh) / 2.0f;

  std::vector<Vec3> view(model.vertices.size());
  std::vector<std::array<float, 2>> screen(model.vertices.size());
  std::vector<float> inverse_depth(model.vertices.size());
  for (std::size_t i = 0; i < model.vertices.size(); ++i) {
    view[i] = rotate(model.vertices[i]);
    const float depth = camera_distance + view[i].z;
    inverse_depth[i] = 1.0f / depth;
    screen[i] = {centre_x + focal * view[i].x * inverse_depth[i], centre_y - focal * view[i].y * inverse_depth[i]};
  }

  // Light from above left, on the viewer's side
  const float light_length = std::sqrt(0.45f * 0.45f + 0.75f * 0.75f + 0.55f * 0.55f);
  const Vec3 light{-0.45f / light_length, 0.75f / light_length, -0.55f / light_length};
  const Vec3 half{light.x, light.y, light.z - 1.0f};
  const float half_length = std::sqrt(half.x * half.x + half.y * half.y + half.z * half.z);

  const float edge_width = 0.9f * static_cast<float>(ss);
  for (const Face& face : model.faces) {
    const Vec3& a = view[face.v[0]];
    const Vec3& b = view[face.v[1]];
    const Vec3& c = view[face.v[2]];

    // Face normal, pointing out of the mesh the face belongs to
    Vec3 n{(b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y), (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
           (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)};
    const Vec3 centre = rotate(face.centre);
    const Vec3 middle{(a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f, (a.z + b.z + c.z) / 3.0f};
    if (n.x * (middle.x - centre.x) + n.y * (middle.y - centre.y) + n.z * (middle.z - centre.z) < 0.0f) {
      n = {-n.x, -n.y, -n.z};
    }
    const float n_length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (n_length < 1e-6f) {
      continue;
    }
    n = {n.x / n_length, n.y / n_length, n.z / n_length};

    // Skip faces turned away from the camera
    if (n.x * middle.x + n.y * middle.y + n.z * (middle.z + camera_distance) >= 0.0f) {
      continue;
    }

    // Flat shading
    std::array<float, 3> shaded{static_cast<float>(face.colour.r), static_cast<float>(face.colour.g),
                                static_cast<float>(face.colour.b)};
    if (!face.emissive) {
      const float diffuse = std::max(0.0f, n.x * light.x + n.y * light.y + n.z * light.z);
      const float spec_base = std::max(0.0f, (n.x * half.x + n.y * half.y + n.z * half.z) / half_length);
      const float specular = 0.30f * std::pow(spec_base, 24.0f);
      const float lit = 0.28f + 0.72f * diffuse;
      for (float& channel : shaded) {
        channel = std::min(255.0f, channel * lit + 255.0f * specular);
      }
    }

    // Rasterise with a depth buffer
    const std::array<float, 2>& p0 = screen[face.v[0]];
    const std::array<float, 2>& p1 = screen[face.v[1]];
    const std::array<float, 2>& p2 = screen[face.v[2]];
    const float area = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p1[1] - p0[1]) * (p2[0] - p0[0]);
    if (std::fabs(area) < 1e-4f) {
      continue;
    }
    const std::array<float, 3> w{inverse_depth[face.v[0]], inverse_depth[face.v[1]], inverse_depth[face.v[2]]};
    // Length of the edge opposite each vertex, to measure the distance from the pixel to the edge
    const std::array<float, 3> opposite{std::hypot(p2[0] - p1[0], p2[1] - p1[1]),
                                        std::hypot(p0[0] - p2[0], p0[1] - p2[1]),
                                        std::hypot(p1[0] - p0[0], p1[1] - p0[1])};

    const std::int32_t min_x = std::max(0, static_cast<std::int32_t>(std::floor(std::min({p0[0], p1[0], p2[0]}))));
    const std::int32_t max_x = std::min(rw - 1, static_cast<std::int32_t>(std::ceil(std::max({p0[0], p1[0], p2[0]}))));
    const std::int32_t min_y = std::max(0, static_cast<std::int32_t>(std::floor(std::min({p0[1], p1[1], p2[1]}))));
    const std::int32_t max_y = std::min(rh - 1, static_cast<std::int32_t>(std::ceil(std::max({p0[1], p1[1], p2[1]}))));

    for (std::int32_t y = min_y; y <= max_y; ++y) {
      for (std::int32_t x = min_x; x <= max_x; ++x) {
        const float px = static_cast<float>(x) + 0.5f;
        const float py = static_cast<float>(y) + 0.5f;
        const float l0 = ((p1[0] - px) * (p2[1] - py) - (p1[1] - py) * (p2[0] - px)) / area;
        const float l1 = ((p2[0] - px) * (p0[1] - py) - (p2[1] - py) * (p0[0] - px)) / area;
        const float l2 = 1.0f - l0 - l1;
        if (l0 < 0.0f || l1 < 0.0f || l2 < 0.0f) {
          continue;
        }
        const float inv = l0 * w[0] + l1 * w[1] + l2 * w[2];
        const std::size_t pixel =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(rw) + static_cast<std::size_t>(x);
        if (inv <= depth_buffer[pixel]) {
          continue;
        }
        depth_buffer[pixel] = inv;

        // Outline the edges that are marked
        float shade = 1.0f;
        const float abs_area = std::fabs(area);
        const std::array<float, 3> distance{l0 * abs_area / opposite[0], l1 * abs_area / opposite[1],
                                            l2 * abs_area / opposite[2]};
        // Edge j runs from vertex j to vertex j+1, opposite vertex j+2
        for (std::size_t j = 0; j < 3; ++j) {
          if (((face.edges >> j) & 1U) != 0U && distance[(j + 2) % 3] < edge_width) {
            shade = 0.30f;
          }
        }
        for (std::size_t k = 0; k < 3; ++k) {
          ship[pixel * 3 + k] = static_cast<std::uint8_t>(shaded[k] * shade);
        }
      }
    }
  }

  // Composite the ship over what is in the image
  const std::uint32_t samples = ss * ss;
  std::uint8_t* out = image.data();
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      std::array<std::uint32_t, 3> sum{0, 0, 0};
      std::uint32_t covered = 0;
      for (std::uint32_t sy = 0; sy < ss; ++sy) {
        for (std::uint32_t sx = 0; sx < ss; ++sx) {
          const std::size_t pixel =
              static_cast<std::size_t>(y * ss + sy) * static_cast<std::size_t>(rw) + (x * ss + sx);
          if (depth_buffer[pixel] > 0.0f) {
            ++covered;
            for (std::size_t k = 0; k < 3; ++k) {
              sum[k] += ship[pixel * 3 + k];
            }
          }
        }
      }
      if (covered == 0) {
        continue;
      }
      const std::size_t at = (static_cast<std::size_t>(y) * width + x) * 3;
      for (std::size_t k = 0; k < 3; ++k) {
        out[at + k] = static_cast<std::uint8_t>((sum[k] + (samples - covered) * out[at + k]) / samples);
      }
    }
  }
}

}  // namespace coriolis
