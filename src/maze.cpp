#include "maze.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <random>

#include "softrender/scene.h"

namespace maze {

namespace {

using softrender::Rgb;
using softrender::Vec3d;

constexpr double kPi = 3.14159265358979323846;
constexpr double kReverseSpeed = 2.5;
constexpr double kAcceleration = 6.0;
constexpr double kElevationRate = 0.55;  // radians a second
constexpr double kElevationLow = -0.12;
constexpr double kElevationHigh = 0.5;
constexpr double kTurnRate = 1.1;     // radians a second
constexpr double kFogEnd = 90.0;      // nothing further than this can be seen
constexpr double kBarStrips = 22.0;   // floor grid lines nearer than this are strips, and further are lines
constexpr int kChunkTiles = 3;

/// A number from 0 up to `n` that depends only on the arguments
std::uint32_t Hash(std::uint32_t seed, int a, int b, int salt, std::uint32_t n) {
  std::uint32_t h = seed * 2654435761u + static_cast<std::uint32_t>(a) * 73856093u + static_cast<std::uint32_t>(b) * 19349663u + static_cast<std::uint32_t>(salt) * 83492791u;
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  h *= 3266489917u;
  h ^= h >> 16;
  return h % n;
}

Rgb Mix(Rgb a, Rgb b, double t) {
  const auto channel = [t](std::uint8_t x, std::uint8_t y) { return static_cast<std::uint8_t>(x + (y - x) * t + 0.5); };
  return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b)};
}

Rgb Scaled(Rgb c, double amount) {
  const auto channel = [amount](std::uint8_t x) { return static_cast<std::uint8_t>(std::min(255.0, x * amount)); };
  return {channel(c.r), channel(c.g), channel(c.b)};
}

/// How far a colour has gone into the distance
Rgb Fogged(Rgb colour, Rgb sky, double distance) {
  const double t = std::min(1.0, distance / kFogEnd);
  return Mix(colour, sky, t * t * (3.0 - 2.0 * t));
}

}  // namespace

Plan::Plan(int rooms_x, int rooms_z, std::uint32_t seed) : tiles_x_(2 * rooms_x + 1), tiles_z_(2 * rooms_z + 1), seed_(seed), solid_(static_cast<std::size_t>(tiles_x_) * tiles_z_, 1) {
  std::mt19937 random(seed);
  const auto below = [&random](std::size_t n) { return static_cast<std::size_t>(random() % n); };
  constexpr int kSteps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

  // Carve from the first room, always walking on to a room not yet visited and backing up when there is none
  start_ = {1, 1};
  solid_[Index(1, 1)] = 0;
  std::vector<Tile> trail = {start_};
  while (!trail.empty()) {
    const auto [tx, tz] = trail.back();
    std::vector<int> choices;
    for (int i = 0; i < 4; ++i) {
      const int nx = tx + 2 * kSteps[i][0];
      const int nz = tz + 2 * kSteps[i][1];
      if (nx > 0 && nz > 0 && nx < tiles_x_ && nz < tiles_z_ && solid_[Index(nx, nz)] != 0) {
        choices.push_back(i);
      }
    }
    if (choices.empty()) {
      trail.pop_back();
      continue;
    }
    const int i = choices[below(choices.size())];
    solid_[Index(tx + kSteps[i][0], tz + kSteps[i][1])] = 0;
    solid_[Index(tx + 2 * kSteps[i][0], tz + 2 * kSteps[i][1])] = 0;
    trail.push_back({tx + 2 * kSteps[i][0], tz + 2 * kSteps[i][1]});
  }

  // Knock through a few of the walls between rooms
  for (int tz = 1; tz + 1 < tiles_z_; ++tz) {
    for (int tx = 1; tx + 1 < tiles_x_; ++tx) {
      const bool between = ((tx % 2 == 0) != (tz % 2 == 0));
      if (between && solid_[Index(tx, tz)] != 0 && below(100) < 7) {
        solid_[Index(tx, tz)] = 0;
      }
    }
  }

  // Find the room that takes longest to reach
  std::vector<int> distance(solid_.size(), -1);
  std::queue<Tile> frontier;
  distance[Index(start_.first, start_.second)] = 0;
  frontier.push(start_);
  furthest_ = start_;
  int furthest = -1;
  while (!frontier.empty()) {
    const auto [tx, tz] = frontier.front();
    frontier.pop();
    const int here = distance[Index(tx, tz)];
    if (tx % 2 == 1 && tz % 2 == 1 && here > furthest) {
      furthest = here;
      furthest_ = {tx, tz};
    }
    for (const auto& step : kSteps) {
      const int nx = tx + step[0];
      const int nz = tz + step[1];
      if (!Solid(nx, nz) && distance[Index(nx, nz)] < 0) {
        distance[Index(nx, nz)] = here + 1;
        frontier.push({nx, nz});
      }
    }
  }
}

bool Plan::Solid(int tx, int tz) const {
  if (tx < 0 || tz < 0 || tx >= tiles_x_ || tz >= tiles_z_) {
    return true;
  }
  return solid_[Index(tx, tz)] != 0;
}

bool Plan::Blocked(double x, double z, double radius) const {
  const int x0 = static_cast<int>(std::floor((x - radius) / kTile));
  const int x1 = static_cast<int>(std::floor((x + radius) / kTile));
  const int z0 = static_cast<int>(std::floor((z - radius) / kTile));
  const int z1 = static_cast<int>(std::floor((z + radius) / kTile));
  for (int tz = z0; tz <= z1; ++tz) {
    for (int tx = x0; tx <= x1; ++tx) {
      if (Solid(tx, tz)) {
        return true;
      }
    }
  }
  return false;
}

double Plan::Height(int tx, int tz) const {
  if (!Solid(tx, tz)) {
    return 0.0;
  }
  // The stepped skyline: a few set heights, so that neighbouring blocks line up
  constexpr double kHeights[5] = {4.5, 4.5, 6.0, 6.0, 9.0};
  return kHeights[Hash(seed_, tx, tz, 1, 5)];
}

bool Plan::Hits(const Vec3d& point) const {
  const int tx = static_cast<int>(std::floor(point.x / kTile));
  const int tz = static_cast<int>(std::floor(point.z / kTile));
  return point.y <= 0.0 || point.y < Height(tx, tz);
}

std::vector<Tile> Plan::Path(Tile from, Tile to) const {
  std::vector<int> came_from(solid_.size(), -1);
  std::queue<Tile> frontier;
  const std::size_t first = Index(from.first, from.second);
  came_from[first] = static_cast<int>(first);
  frontier.push(from);
  constexpr int kSteps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  while (!frontier.empty()) {
    const auto [tx, tz] = frontier.front();
    frontier.pop();
    if (Tile{tx, tz} == to) {
      std::vector<Tile> path;
      int at = static_cast<int>(Index(tx, tz));
      while (true) {
        path.push_back({at % tiles_x_, at / tiles_x_});
        if (came_from[at] == at) {
          break;
        }
        at = came_from[at];
      }
      std::reverse(path.begin(), path.end());
      return path;
    }
    for (const auto& step : kSteps) {
      const int nx = tx + step[0];
      const int nz = tz + step[1];
      if (!Solid(nx, nz) && came_from[Index(nx, nz)] < 0) {
        came_from[Index(nx, nz)] = static_cast<int>(Index(tx, tz));
        frontier.push({nx, nz});
      }
    }
  }
  return {};
}

Tank StartTank(const Plan& plan) {
  Tank tank;
  const auto [tx, tz] = plan.start();
  tank.x = TileCentre(tx);
  tank.z = TileCentre(tz);
  tank.heading = plan.Solid(tx, tz + 1) ? kPi / 2.0 : 0.0;
  return tank;
}

void Step(Tank* tank, const Controls& controls, double dt, const Plan& plan) {
  tank->heading += controls.turn * kTurnRate * dt;
  tank->elevation = std::clamp(tank->elevation + controls.elevate * kElevationRate * dt, kElevationLow, kElevationHigh);
  const double wanted = controls.throttle > 0.0 ? controls.throttle * kTankTopSpeed : controls.throttle * kReverseSpeed;
  const double change = kAcceleration * dt;
  tank->speed += std::clamp(wanted - tank->speed, -change, change);

  // One axis at a time, so that a tank running into a wall at an angle slides along it
  const double dx = std::sin(tank->heading) * tank->speed * dt;
  const double dz = std::cos(tank->heading) * tank->speed * dt;
  bool stopped = false;
  if (!plan.Blocked(tank->x + dx, tank->z, kTankRadius)) {
    tank->x += dx;
  } else {
    stopped = true;
  }
  if (!plan.Blocked(tank->x, tank->z + dz, kTankRadius)) {
    tank->z += dz;
  } else {
    stopped = true;
  }
  if (stopped) {
    tank->speed *= std::max(0.0, 1.0 - 4.0 * dt);  // scraping a wall loses speed
  }
}

softrender::Camera TankCamera(const Tank& tank, const Plan& plan, ViewMode mode, double fov, int width, int height) {
  const double sx = std::sin(tank.heading);
  const double cz = std::cos(tank.heading);
  softrender::Camera camera = softrender::MakeCamera(width, height, fov);
  camera.near_plane = 0.1;
  if (mode == ViewMode::kCockpit) {
    camera.position = {tank.x + sx * 0.8, 2.1, tank.z + cz * 0.8};
    camera.orientation = softrender::LookAlong(GunDirection(tank), {0.0, 1.0, 0.0});
    return camera;
  }
  if (mode == ViewMode::kHigh) {
    // Above the tallest walls, looking down the way the tank is heading
    camera.position = {tank.x - sx * 14.0, 15.0, tank.z - cz * 14.0};
    const Vec3d ahead{tank.x + sx * 9.0, 0.0, tank.z + cz * 9.0};
    camera.orientation = softrender::LookAlong(ahead - camera.position, {0.0, 1.0, 0.0});
    return camera;
  }
  if (mode == ViewMode::kOverhead) {
    camera.position = {tank.x, 34.0, tank.z};
    camera.orientation = softrender::LookAlong({0.0, -1.0, 0.0}, {sx, 0.0, cz});
    return camera;
  }
  constexpr double kBehind = 8.5;
  constexpr double kMargin = 0.4;
  double back = 0.0;
  while (back < kBehind && !plan.Blocked(tank.x - sx * (back + 0.1), tank.z - cz * (back + 0.1), kMargin)) {
    back += 0.1;
  }
  camera.position = {tank.x - sx * back, 2.2 + 1.6 * back / kBehind, tank.z - cz * back};
  const Vec3d target{tank.x + sx * 6.0, 1.4, tank.z + cz * 6.0};
  camera.orientation = softrender::LookAlong(target - camera.position, {0.0, 1.0, 0.0});
  return camera;
}

Theme CharcoalTheme() { return {{9, 6, 12}, {50, 47, 66}, {34, 32, 46}, {165, 162, 190}, {6, 5, 9}, {20, 17, 26}, {235, 235, 245}}; }

Theme BlueTheme() { return {{3, 8, 16}, {40, 74, 132}, {24, 44, 84}, {120, 165, 255}, {2, 4, 10}, {30, 54, 100}, {120, 170, 255}}; }

/// Lays out the faces of the walls and floor. A face is worked on in its own flat coordinates: u along it, v up it.
class View::Mason {
 public:
  Mason(View* view, const Theme& theme) : view_(view), theme_(theme) {}

  void Place(Vec3d origin, Vec3d along, Vec3d normal, double shade) {
    origin_ = origin;
    along_ = along;
    normal_ = normal;
    shade_ = shade;
  }

  /// A flat part of the face, `depth` behind it
  void Panel(double u0, double v0, double u1, double v1, Rgb colour, double depth = 0.0) {
    Add({Local(u0, v0, depth), Local(u1, v0, depth), Local(u1, v1, depth), Local(u0, v1, depth)}, normal_, Scaled(colour, shade_));
  }

  void Line(double u0, double v0, double u1, double v1, double depth = 0.0) { AddEdge(Local(u0, v0, depth), Local(u1, v1, depth), normal_); }

  /// A wall of a recess, joining the face to `depth` behind it along a line up or across the face
  void Jamb(double u0, double v0, double u1, double v1, double depth, Vec3d normal) {
    Add({Local(u0, v0, 0.0), Local(u1, v1, 0.0), Local(u1, v1, depth), Local(u0, v0, depth)}, normal, theme_.recess);
  }

  void Add(std::array<Vec3d, 4> corners, Vec3d normal, Rgb colour) {
    const Vec3d& c = corners[0];
    view_->ChunkAt(c.x, c.z).surfaces.push_back({corners, normal, colour});
  }

  void AddEdge(Vec3d a, Vec3d b, Vec3d normal) { view_->ChunkAt(a.x, a.z).edges.push_back({a, b, normal, theme_.line}); }

 private:
  Vec3d Local(double u, double v, double depth) const { return origin_ + along_ * u + Vec3d{0.0, v, 0.0} - normal_ * depth; }

  View* view_;
  const Theme& theme_;
  Vec3d origin_;
  Vec3d along_;
  Vec3d normal_;
  double shade_ = 1.0;
};

View::Chunk& View::ChunkAt(double x, double z) {
  const double size = kChunkTiles * kTile;
  const int cx = std::clamp(static_cast<int>(std::floor(x / size)), 0, chunks_x_ - 1);
  const int cz = std::clamp(static_cast<int>(std::floor(z / size)), 0, chunks_z_ - 1);
  return chunks_[static_cast<std::size_t>(cz) * chunks_x_ + cx];
}

View::View(const Plan& plan, const Theme& theme) : theme_(theme), chunks_x_(plan.tiles_x() / kChunkTiles + 1), chunks_z_(plan.tiles_z() / kChunkTiles + 1) {
  chunks_.resize(static_cast<std::size_t>(chunks_x_) * chunks_z_);
  for (int cz = 0; cz < chunks_z_; ++cz) {
    for (int cx = 0; cx < chunks_x_; ++cx) {
      chunks_[static_cast<std::size_t>(cz) * chunks_x_ + cx].centre = {(cx + 0.5) * kChunkTiles * kTile, 0.0, (cz + 0.5) * kChunkTiles * kTile};
    }
  }

  Mason mason(this, theme_);
  constexpr int kGridPerTile = 5;
  constexpr double kGridHalfWidth = 0.04;
  const Vec3d up{0.0, 1.0, 0.0};
  const std::uint32_t seed = plan.seed();

  for (int tz = 0; tz < plan.tiles_z(); ++tz) {
    for (int tx = 0; tx < plan.tiles_x(); ++tx) {
      const double x0 = tx * kTile;
      const double z0 = tz * kTile;
      const double x1 = x0 + kTile;
      const double z1 = z0 + kTile;

      if (!plan.Solid(tx, tz)) {
        mason.Add({Vec3d{x0, 0.0, z0}, Vec3d{x0, 0.0, z1}, Vec3d{x1, 0.0, z1}, Vec3d{x1, 0.0, z0}}, up, theme_.floor);
        constexpr double kRaise = 0.02;
        // The far edges of a tile belong to the next one, unless that is a wall
        const int last_x = plan.Solid(tx + 1, tz) ? kGridPerTile : kGridPerTile - 1;
        const int last_z = plan.Solid(tx, tz + 1) ? kGridPerTile : kGridPerTile - 1;
        Chunk& chunk = ChunkAt(x0, z0);
        for (int i = 0; i <= last_x; ++i) {
          const double x = x0 + kTile * i / kGridPerTile;
          chunk.bars.push_back({{x, kRaise, z0}, {x, kRaise, z1}, {kGridHalfWidth, 0.0, 0.0}, theme_.grid});
        }
        for (int i = 0; i <= last_z; ++i) {
          const double z = z0 + kTile * i / kGridPerTile;
          chunk.bars.push_back({{x0, kRaise, z}, {x1, kRaise, z}, {0.0, 0.0, kGridHalfWidth}, theme_.grid});
        }
        continue;
      }

      const double height = plan.Height(tx, tz);
      // The roof of the block
      mason.Add({Vec3d{x0, height, z0}, Vec3d{x0, height, z1}, Vec3d{x1, height, z1}, Vec3d{x1, height, z0}}, up, theme_.wall_top);

      // Each side, wherever it stands higher than what is next to it
      struct Side {
        int dx, dz;
        Vec3d origin, along, normal;
        double shade;
      };
      const Side sides[4] = {
          {1, 0, {x1, 0.0, z0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}, 0.82},
          {-1, 0, {x0, 0.0, z1}, {0.0, 0.0, -1.0}, {-1.0, 0.0, 0.0}, 0.82},
          {0, 1, {x1, 0.0, z1}, {-1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 1.0},
          {0, -1, {x0, 0.0, z0}, {1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, 1.0},
      };
      for (int s = 0; s < 4; ++s) {
        const Side& side = sides[s];
        const double next_height = plan.Height(tx + side.dx, tz + side.dz);
        if (height <= next_height) {
          continue;
        }
        mason.Place(side.origin, side.along, side.normal, side.shade);
        const double base = next_height;
        const double course = 2.6;
        const double w = kTile;
        mason.Line(0.0, height, w, height);

        if (base > 0.0) {
          // Above a lower neighbour, seen from far off: plain
          mason.Panel(0.0, base, w, height, theme_.wall);
          continue;
        }

        mason.Line(0.0, 0.0, 0.0, height);
        mason.Line(0.0, course, w, course);
        mason.Line(0.0, 0.0, w, 0.0);
        const std::uint32_t kind = Hash(seed, tx, tz, 10 + s, 8);
        if (kind == 0) {
          // A recessed doorway
          constexpr double kDoorU0 = kTile / 2.0 - 1.1;
          constexpr double kDoorU1 = kTile / 2.0 + 1.1;
          constexpr double kDoorTop = 3.7;
          constexpr double kDepth = 0.5;
          mason.Panel(0.0, 0.0, kDoorU0, height, theme_.wall);
          mason.Panel(kDoorU1, 0.0, w, height, theme_.wall);
          mason.Panel(kDoorU0, kDoorTop, kDoorU1, height, theme_.wall);
          mason.Panel(kDoorU0, 0.0, kDoorU1, kDoorTop, theme_.wall, kDepth);
          mason.Jamb(kDoorU0, 0.0, kDoorU0, kDoorTop, kDepth, side.along);
          mason.Jamb(kDoorU1, 0.0, kDoorU1, kDoorTop, kDepth, side.along * -1.0);
          mason.Jamb(kDoorU0, kDoorTop, kDoorU1, kDoorTop, kDepth, Vec3d{0.0, -1.0, 0.0});
          mason.Line(kDoorU0, 0.0, kDoorU0, kDoorTop);
          mason.Line(kDoorU0, kDoorTop, kDoorU1, kDoorTop);
          mason.Line(kDoorU1, 0.0, kDoorU1, kDoorTop);
        } else if (kind <= 2) {
          // A black slot, the height of the lower course
          const double u = 1.0 + (w - 2.0) * Hash(seed, tx, tz, 20 + s, 100) / 100.0;
          constexpr double kSlot = 0.22;
          mason.Panel(0.0, 0.0, u, height, theme_.wall);
          mason.Panel(u + kSlot, 0.0, w, height, theme_.wall);
          mason.Panel(u, course, u + kSlot, height, theme_.wall);
          mason.Panel(u, 0.0, u + kSlot, course, theme_.recess);
          mason.Line(u, 0.0, u, course);
          mason.Line(u + kSlot, 0.0, u + kSlot, course);
        } else if (kind <= 4) {
          // Slanted lines, rising along the wall
          mason.Panel(0.0, 0.0, w, height, theme_.wall);
          const double lean = kind == 3 ? 1.0 : -1.0;
          const double foot = lean > 0.0 ? 0.6 : w - 0.6;
          mason.Line(foot, 0.0, foot + lean * course * 0.9, course);
          mason.Line(foot + lean * 1.1, 0.0, foot + lean * (1.1 + course * 0.9), course);
          mason.Line(foot + lean * course * 0.9, course, foot + lean * course * 0.9, height);
        } else {
          // Plain blocks, the joint falling somewhere different on each
          mason.Panel(0.0, 0.0, w, height, theme_.wall);
          const double u = 1.5 + (w - 3.0) * Hash(seed, tx, tz, 30 + s, 100) / 100.0;
          mason.Line(u, course, u, height);
          mason.Line(w - u, 0.0, w - u, course);
        }
      }
    }
  }
}

void View::Draw(softrender::Canvas& canvas, const softrender::Camera& camera) const {
  canvas.set_line_width(std::max(canvas.line_width(), canvas.height() / 540));  // lines stay the same size on the picture
  const Vec3d eye = camera.position;
  const Vec3d forward = softrender::Rotate(camera.orientation, Vec3d{0.0, 0.0, 1.0});
  const double reach = kChunkTiles * kTile;  // more than half the diagonal of a chunk

  // Nearest first, so that the depth buffer turns away as much as it can
  std::vector<std::pair<double, const Chunk*>> shown;
  for (const Chunk& chunk : chunks_) {
    const Vec3d to = chunk.centre - eye;
    const double distance = std::hypot(to.x, to.z);
    if (distance < kFogEnd + reach && softrender::Dot(to, forward) > -reach) {
      shown.push_back({distance, &chunk});
    }
  }
  std::sort(shown.begin(), shown.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

  surfaces_drawn_ = 0;
  lines_drawn_ = 0;
  for (const auto& [distance, chunk] : shown) {
    for (const Surface& s : chunk->surfaces) {
      if (softrender::Dot(s.normal, eye - s.corners[0]) <= 0.0) {
        continue;
      }
      const Vec3d middle = (s.corners[0] + s.corners[2]) * 0.5;
      const double far = softrender::Length(middle - eye);
      if (far > kFogEnd) {
        continue;
      }
      softrender::FillPolygon(canvas, camera, {s.corners[0], s.corners[1], s.corners[2], s.corners[3]}, Fogged(s.colour, theme_.sky, far), 0);
      ++surfaces_drawn_;
    }
  }
  for (const auto& [distance, chunk] : shown) {
    for (const Bar& bar : chunk->bars) {
      const Vec3d middle = (bar.a + bar.b) * 0.5;
      const double far = softrender::Length(middle - eye);
      if (far > kFogEnd) {
        continue;
      }
      const Rgb colour = Fogged(bar.colour, theme_.sky, far);
      if (far < kBarStrips) {
        softrender::FillPolygon(canvas, camera, {bar.a - bar.width, bar.b - bar.width, bar.b + bar.width, bar.a + bar.width}, colour, 0);
      } else {
        softrender::DrawLine3D(canvas, camera, bar.a, bar.b, colour);
      }
      ++lines_drawn_;
    }
  }
  // After all the surfaces, each pulled a little towards the eye so that it wins against the face it lies on
  for (const auto& [distance, chunk] : shown) {
    for (const Edge& e : chunk->edges) {
      if (softrender::Dot(e.normal, eye - e.a) <= 0.0) {
        continue;
      }
      const Vec3d middle = (e.a + e.b) * 0.5;
      const double far = softrender::Length(middle - eye);
      if (far > kFogEnd) {
        continue;
      }
      // A face seen nearly edge on changes depth quickly from one pixel to the next, so its lines need more pull
      const auto pulled = [&](const Vec3d& p) {
        const Vec3d to_eye = eye - p;
        const double length = softrender::Length(to_eye);
        const double facing = std::max(0.08, softrender::Dot(e.normal, to_eye) / length);
        return p + to_eye * ((0.006 + 0.0012 * length) / facing / length);
      };
      softrender::DrawLine3D(canvas, camera, pulled(e.a), pulled(e.b), Fogged(e.colour, theme_.sky, far));
      ++lines_drawn_;
    }
  }
}

}  // namespace maze
