#ifndef SPACE_PARANOIDS_BATTLE_H_
#define SPACE_PARANOIDS_BATTLE_H_

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "maze.h"
#include "softrender/canvas.h"
#include "softrender/camera.h"
#include "softrender/model.h"
#include "softrender/renderer.h"
#include "vehicles.h"

// The fight in a maze: kites flying round the corridors, and the tank shooting them down.

namespace maze {

/// A kite, flying from the middle of one tile to the middle of the next
struct Kite {
  double x = 0.0;
  double z = 0.0;
  double heading = 0.0;
  double progress = 0.0;  // from 0 at `from` to 1 at `to`
  double time = 0.0;      // what it bobs to
  Tile from;
  Tile to;
};

struct Shot {
  softrender::Vec3d position;
  softrender::Vec3d direction;
  double age = 0.0;
};

/// A fleck thrown out by a kite as it blows up
struct Spark {
  softrender::Vec3d position;
  softrender::Vec3d velocity;
  double age = 0.0;
};

class Battle {
 public:
  /// `kites` kites in rooms well away from the start, the same for the same seed
  Battle(const Plan& plan, int kites, std::uint32_t seed);

  /// Move everything on by `dt` seconds. While `firing` the tank shoots as fast as it can reload.
  void Update(const Tank& tank, bool firing, double dt, const Plan& plan);

  void Draw(softrender::Canvas& canvas, const softrender::Camera& camera, const softrender::Model& kite_model) const;

  const std::vector<Kite>& kites() const { return kites_; }
  int destroyed() const { return destroyed_; }
  int shots_fired() const { return shots_fired_; }
  bool cleared() const { return kites_.empty(); }
  bool exploding() const { return !sparks_.empty(); }
  bool shots_flying() const { return !shots_.empty(); }

 private:
  std::vector<Kite> kites_;
  std::vector<Shot> shots_;
  std::vector<Spark> sparks_;
  std::mt19937 random_;
  double reload_ = 0.0;
  int barrel_ = 0;
  int destroyed_ = 0;
  int shots_fired_ = 0;
};

/// The model of the tank, at the tank, with its guns raised as they are
void DrawTank(softrender::Canvas& canvas, const softrender::Camera& camera, const vehicles::CrawlerParts& model, const Tank& tank);

/// How the renderer should be set up for a theme, on a picture this many pixels high
softrender::RenderOptions SceneOptions(const Theme& theme, int height);

/// What a kite is worth
constexpr int kKiteScore = 100;

/// Draw the tank's instruments over a finished picture: the score, how many kites are left, and a round radar with the
/// tank at its middle and its heading up, with the walls in outline. In the cockpit they sit in the green frame and gun
/// sight. The camera and field of view are at the top right.
void DrawHud(softrender::Image& image, const Battle& battle, const Plan& plan, const Tank& tank, ViewMode mode, double fov, int score);

/// Dim a finished picture and list the controls on it
void DrawPauseScreen(softrender::Image& image);

}  // namespace maze

#endif  // SPACE_PARANOIDS_BATTLE_H_
