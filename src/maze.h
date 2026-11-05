#ifndef SPACE_PARANOIDS_MAZE_H_
#define SPACE_PARANOIDS_MAZE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "softrender/camera.h"
#include "softrender/canvas.h"
#include "softrender/math.h"
#include "softrender/scene.h"
#include "softrender/types.h"

// A maze to drive through. The plan is a grid of square tiles in the x, z plane, each solid or open, with the ground at
// y = 0. Tile (tx, tz) covers kTile world units from (tx * kTile, tz * kTile).

namespace maze {

constexpr double kTile = 8.0;

using Tile = std::pair<int, int>;

/// The plan of a maze. Rooms sit on the odd tiles, joined by the odd/even tiles between them
class Plan {
 public:
  /// A maze of `rooms_x` by `rooms_z` rooms, the same for the same seed. Every room can be reached, the outside is
  /// closed, and a few extra openings make loops so that there is more than one way through.
  Plan(int rooms_x, int rooms_z, std::uint32_t seed);

  int tiles_x() const { return tiles_x_; }
  int tiles_z() const { return tiles_z_; }
  std::uint32_t seed() const { return seed_; }

  /// Anything outside the plan is solid
  bool Solid(int tx, int tz) const;
  /// True if a square of half width `radius` centred here touches a solid tile
  bool Blocked(double x, double z, double radius) const;
  /// How tall a solid tile is, or 0 for an open one
  double Height(int tx, int tz) const;
  /// True if a point in space is inside a wall or under the ground. Shots can go over a low wall.
  bool Hits(const softrender::Vec3d& point) const;

  Tile start() const { return start_; }
  /// The room that takes longest to reach from the start
  Tile furthest() const { return furthest_; }
  /// The shortest way between two open tiles, both ends included, or empty if there is none
  std::vector<Tile> Path(Tile from, Tile to) const;

 private:
  std::size_t Index(int tx, int tz) const { return static_cast<std::size_t>(tz) * tiles_x_ + tx; }

  int tiles_x_;
  int tiles_z_;
  std::uint32_t seed_;
  std::vector<std::uint8_t> solid_;
  Tile start_;
  Tile furthest_;
};

inline double TileCentre(int t) { return (t + 0.5) * kTile; }

/// What the driver asks for this moment
struct Controls {
  double throttle = 0.0;  // -1 back to 1 forward
  double turn = 0.0;      // -1 left to 1 right
  double elevate = 0.0;   // -1 guns down to 1 guns up
};

/// How far the tank reaches from its centre, for walls
constexpr double kTankRadius = 1.7;
/// The most it can do: it is heavy, and slow
constexpr double kTankTopSpeed = 5.0;

/// The tank the player drives. Heading 0 looks along +z, and it grows to the right.
struct Tank {
  double x = 0.0;
  double z = 0.0;
  double heading = 0.0;
  double speed = 0.0;
  double elevation = 0.0;  // of the guns, radians above level
};

/// The way the guns point
inline softrender::Vec3d GunDirection(const Tank& tank) {
  const double level = std::cos(tank.elevation);
  return {std::sin(tank.heading) * level, std::sin(tank.elevation), std::cos(tank.heading) * level};
}

/// A tank on the start tile, looking down the open way out of it
Tank StartTank(const Plan& plan);
/// Move a tank on by `dt` seconds. It slides along the walls, and never ends up inside one.
void Step(Tank* tank, const Controls& controls, double dt, const Plan& plan);

/// Where the player sees from: from the tank, close behind it, high behind it above the walls, or straight down on it
enum class ViewMode { kCockpit, kChase, kHigh, kOverhead };
constexpr int kViewCount = 4;

/// How wide the view is, in degrees across the picture
constexpr double kFovDefault = 74.0;
constexpr double kFovLow = 45.0;
constexpr double kFovHigh = 110.0;
constexpr double kFovStep = 5.0;

/// The camera for a view of the tank. The chase camera is drawn in so that a wall never comes between them. `width` and
/// `height` are the picture's.
softrender::Camera TankCamera(const Tank& tank, const Plan& plan, ViewMode mode, double fov, int width, int height);

/// The colours of a maze
struct Theme {
  softrender::Rgb sky;         // and the colour things fade to with distance
  softrender::Rgb wall;
  softrender::Rgb wall_top;
  softrender::Rgb line;        // the thin lines on the walls
  softrender::Rgb recess;      // slots and doorways
  softrender::Rgb floor;
  softrender::Rgb grid;        // the bright lines on the floor
};

/// Charcoal walls, pale lines and a white grid
Theme CharcoalTheme();
/// Blue walls with brighter blue lines
Theme BlueTheme();

/// The solid geometry of a plan, made once and drawn each frame
class View {
 public:
  View(const Plan& plan, const Theme& theme);

  /// Draw everything in reach of the camera. Clear the canvas to `theme.sky` first.
  void Draw(softrender::Canvas& canvas, const softrender::Camera& camera) const;

  /// What the last Draw drew, to see what a frame costs
  std::size_t surfaces_drawn() const { return surfaces_drawn_; }
  std::size_t lines_drawn() const { return lines_drawn_; }

 private:
  struct Surface {
    std::array<softrender::Vec3d, 4> corners;
    softrender::Vec3d normal;
    softrender::Rgb colour;
  };
  struct Edge {
    softrender::Vec3d a;
    softrender::Vec3d b;
    softrender::Vec3d normal;  // an edge shows only from the side its face does
    softrender::Rgb colour;
  };
  /// A line of the floor grid: a bright strip up close, and a plain line further off, where a strip would break up
  struct Bar {
    softrender::Vec3d a;
    softrender::Vec3d b;
    softrender::Vec3d width;  // from the line to the side of the strip
    softrender::Rgb colour;
  };
  /// A block of tiles, the unit of culling
  struct Chunk {
    softrender::Vec3d centre;
    std::vector<Surface> surfaces;
    std::vector<Edge> edges;
    std::vector<Bar> bars;
  };
  class Mason;

  Chunk& ChunkAt(double x, double z);

  Theme theme_;
  int chunks_x_;
  int chunks_z_;
  std::vector<Chunk> chunks_;
  mutable std::size_t surfaces_drawn_ = 0;
  mutable std::size_t lines_drawn_ = 0;
};

}  // namespace maze

#endif  // SPACE_PARANOIDS_MAZE_H_
