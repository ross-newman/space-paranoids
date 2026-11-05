// Check the maze game and draw it to a PNG. A bot plays each maze with the real controls until every kite is down, so a
// pass means the game can be won. Also times the frames:
//   maze_preview [out.png [action.png]]
// With a second name it also draws one big moment of a fight, for the README.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "battle.h"
#include "maze.h"
#include "softrender/softrender.h"
#include "vehicles.h"

namespace {

using namespace softrender;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDt = 1.0 / 60.0;
constexpr int kRooms = 10;
constexpr int kKites = 8;
constexpr std::size_t kActionKill = 2;  // which kill makes the best picture

/// Every room of a maze can be reached, and the outside is closed
bool CheckPlan(const maze::Plan& plan) {
  bool ok = true;
  for (int tz = 1; tz < plan.tiles_z(); tz += 2) {
    for (int tx = 1; tx < plan.tiles_x(); tx += 2) {
      ok = ok && !plan.Solid(tx, tz) && !plan.Path(plan.start(), {tx, tz}).empty();
    }
  }
  for (int tx = 0; tx < plan.tiles_x(); ++tx) {
    ok = ok && plan.Solid(tx, 0) && plan.Solid(tx, plan.tiles_z() - 1);
  }
  for (int tz = 0; tz < plan.tiles_z(); ++tz) {
    ok = ok && plan.Solid(0, tz) && plan.Solid(plan.tiles_x() - 1, tz);
  }
  return ok;
}

maze::Tile TileOf(double x, double z) { return {static_cast<int>(std::floor(x / maze::kTile)), static_cast<int>(std::floor(z / maze::kTile))}; }

bool InSight(const maze::Plan& plan, double x0, double z0, double x1, double z1) {
  const double length = std::hypot(x1 - x0, z1 - z0);
  for (double t = 0.0; t < length; t += 0.5) {
    if (plan.Blocked(x0 + (x1 - x0) * t / length, z0 + (z1 - z0) * t / length, 0.05)) {
      return false;
    }
  }
  return true;
}

/// Goes after the nearest kite and shoots whatever it has in its sights. Calls on_frame after every step.
template <typename OnFrame>
std::optional<double> Hunt(const maze::Plan& plan, maze::Battle& battle, OnFrame on_frame, double elevate = 0.0, int seconds = 900) {
  maze::Tank tank = maze::StartTank(plan);
  for (int frame = 0; frame < 60 * seconds; ++frame) {
    if (battle.cleared()) {
      return frame * kDt;
    }
    const maze::Kite* target = nullptr;
    double nearest = 1e18;
    for (const maze::Kite& kite : battle.kites()) {
      const double distance = plan.Path(TileOf(tank.x, tank.z), TileOf(kite.x, kite.z)).size();
      if (distance < nearest) {
        nearest = distance;
        target = &kite;
      }
    }
    const bool sighted = InSight(plan, tank.x, tank.z, target->x, target->z);
    double aim_x = target->x;
    double aim_z = target->z;
    if (!sighted) {
      const std::vector<maze::Tile> path = plan.Path(TileOf(tank.x, tank.z), TileOf(target->x, target->z));
      const maze::Tile next = path.size() > 1 ? path[1] : path[0];
      aim_x = maze::TileCentre(next.first);
      aim_z = maze::TileCentre(next.second);
    }
    const double error = std::remainder(std::atan2(aim_x - tank.x, aim_z - tank.z) - tank.heading, 2.0 * kPi);
    maze::Controls controls;
    controls.turn = std::clamp(error * 4.0, -1.0, 1.0);
    controls.elevate = elevate;
    const double range = std::hypot(target->x - tank.x, target->z - tank.z);
    controls.throttle = sighted && range < 12.0 ? 0.0 : (std::fabs(error) < 0.5 ? 1.0 : 0.15);
    maze::Step(&tank, controls, kDt, plan);
    const bool firing = sighted && std::fabs(error) < 0.1;
    battle.Update(tank, firing, kDt, plan);
    if (plan.Blocked(tank.x, tank.z, maze::kTankRadius - 0.01)) {
      std::printf("tank went into a wall at %.2f, %.2f\n", tank.x, tank.z);
      return std::nullopt;
    }
    on_frame(tank, battle, sighted && range < 34.0 && range > 12.0 && firing);
  }
  return std::nullopt;
}

struct Moment {
  maze::Tank tank;
  maze::Battle battle;
};

}  // namespace

int main(int argc, char** argv) {
  const std::string path = argc > 1 ? argv[1] : "maze.png";
  bool ok = true;
  double longest = 0.0;
  for (std::uint32_t seed = 1; seed <= 40; ++seed) {
    const maze::Plan plan(kRooms, kRooms, seed);
    ok = CheckPlan(plan) && ok;
    maze::Battle battle(plan, kKites, seed);
    const std::optional<double> time = Hunt(plan, battle, [](const maze::Tank&, const maze::Battle&, bool) {});
    ok = time.has_value() && battle.destroyed() == kKites && ok;
    longest = std::max(longest, time.value_or(0.0));
  }
  std::printf("40 mazes cleared of %d kites by the bot, slowest %.0f s: %s\n", kKites, longest, ok ? "ok" : "BAD");

  // With its guns held right up the same bot should do much worse: a shot has to be aimed at the height of a kite
  int level_kills = 0;
  int raised_kills = 0;
  for (std::uint32_t seed = 1; seed <= 6; ++seed) {
    const maze::Plan plan(kRooms, kRooms, seed);
    maze::Battle level(plan, kKites, seed);
    maze::Battle raised(plan, kKites, seed);
    Hunt(plan, level, [](const maze::Tank&, const maze::Battle&, bool) {}, 0.0, 150);
    Hunt(plan, raised, [](const maze::Tank&, const maze::Battle&, bool) {}, 1.0, 150);
    level_kills += level.destroyed();
    raised_kills += raised.destroyed();
  }
  const bool aiming = raised_kills * 3 < level_kills;
  std::printf("kites down in 150 s with guns level %d, guns right up %d: %s\n", level_kills, raised_kills, aiming ? "ok" : "BAD");
  ok = ok && aiming;

  const maze::Plan plan(kRooms, kRooms, 3);
  const std::array<maze::Theme, 2> themes = {maze::CharcoalTheme(), maze::BlueTheme()};
  const vehicles::CrawlerParts tank_model = vehicles::MakeCrawlerParts();
  const Model kite_model = vehicles::MakeKite({{20, 8, 10}, {70, 14, 18}, {255, 40, 30}});

  // Pick out a kite in the sights, and one blowing up
  std::optional<Moment> sighting;
  std::optional<Moment> explosion;
  std::vector<Moment> kills;  // a moment after each kite goes down, with the blast opening out
  std::vector<int> kill_frames;
  int frame_now = 0;
  std::vector<maze::Tank> all;
  {
    maze::Battle battle(plan, kKites, 3);
    int down = 0;
    Hunt(plan, battle, [&](const maze::Tank& tank, const maze::Battle& now, bool lined_up) {
      all.push_back(tank);
      ++frame_now;
      if (static_cast<int>(kill_frames.size()) < now.destroyed()) {
        kill_frames.push_back(frame_now);
      }
      if (!kill_frames.empty() && frame_now == kill_frames.back() + 9) {
        kills.push_back({tank, now});
      }
      if (lined_up && now.shots_flying() && !sighting) {
        sighting = Moment{tank, now};
      }
      if (now.destroyed() > down) {
        down = now.destroyed();
        if (down == 3) {
          explosion = Moment{tank, now};
        }
      }
    });
  }
  if (!sighting || !explosion) {
    std::printf("no moment to show\n");
    return 1;
  }
  explosion->tank.elevation = 0.4;  // to show the guns raised
  for (int i = 0; i < 6; ++i) {  // let the blast open out
    explosion->battle.Update(explosion->tank, false, kDt, plan);
  }

  constexpr int kWidth = 640;
  constexpr int kHeight = 360;
  Image sheet(kWidth * 3, kHeight * 2);
  const struct {
    const Moment* moment;
    maze::ViewMode mode;
    std::size_t theme;
    double fov;
    bool paused;
  } panels[6] = {{&*sighting, maze::ViewMode::kCockpit, 0, maze::kFovDefault, false},
                 {&*sighting, maze::ViewMode::kChase, 0, maze::kFovDefault, false},
                 {&*explosion, maze::ViewMode::kHigh, 0, maze::kFovDefault, false},
                 {&*sighting, maze::ViewMode::kOverhead, 0, maze::kFovDefault, false},
                 {&*sighting, maze::ViewMode::kCockpit, 1, maze::kFovHigh, false},
                 {&*sighting, maze::ViewMode::kChase, 0, maze::kFovDefault, true}};
  const std::array<maze::View, 2> views = {maze::View(plan, themes[0]), maze::View(plan, themes[1])};
  for (int panel = 0; panel < 6; ++panel) {
    const auto& p = panels[panel];
    RenderOptions options = maze::SceneOptions(themes[p.theme], kHeight);
    options.supersample = 2;
    Renderer renderer(options);
    Image picture(kWidth, kHeight);
    renderer.Render(
        picture, maze::TankCamera(p.moment->tank, plan, p.mode, p.fov, kWidth, kHeight),
        [&](Canvas& canvas, const Camera& camera) {
          views[p.theme].Draw(canvas, camera);
          p.moment->battle.Draw(canvas, camera, kite_model);
          if (p.mode != maze::ViewMode::kCockpit) {
            maze::DrawTank(canvas, camera, tank_model, p.moment->tank);
          }
        },
        [&](Image& image) {
          maze::DrawHud(image, p.moment->battle, plan, p.moment->tank, p.mode, p.fov, p.moment->battle.destroyed() * maze::kKiteScore);
          if (p.paused) {
            maze::DrawPauseScreen(image);
          }
        });
    for (int y = 0; y < kHeight; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        const std::uint8_t* q = picture.data() + (static_cast<std::size_t>(y) * kWidth + x) * 3;
        sheet.Set((panel % 3) * kWidth + x, (panel / 3) * kHeight + y, {q[0], q[1], q[2]});
      }
    }
  }
  if (!WritePng(sheet, path)) {
    std::fprintf(stderr, "could not write %s\n", path.c_str());
    return 1;
  }

  if (argc > 2) {
    constexpr int kActionWidth = 1600;
    constexpr int kActionHeight = 900;
    const Moment& moment = kills.at(kActionKill);
    RenderOptions options = maze::SceneOptions(themes[0], kActionHeight);
    options.supersample = 2;
    Renderer renderer(options);
    Image action(kActionWidth, kActionHeight);
    renderer.Render(
        action, maze::TankCamera(moment.tank, plan, maze::ViewMode::kChase, maze::kFovDefault, kActionWidth, kActionHeight),
        [&](Canvas& canvas, const Camera& camera) {
          views[0].Draw(canvas, camera);
          moment.battle.Draw(canvas, camera, kite_model);
          maze::DrawTank(canvas, camera, tank_model, moment.tank);
        },
        [&](Image& image) { maze::DrawHud(image, moment.battle, plan, moment.tank, maze::ViewMode::kChase, maze::kFovDefault, moment.battle.destroyed() * maze::kKiteScore); });
    if (!WritePng(action, argv[2])) {
      std::fprintf(stderr, "could not write %s\n", argv[2]);
      return 1;
    }
  }

  // What a frame costs at sizes a screen might be
  const maze::View view(plan, themes[0]);
  const maze::Battle battle(plan, kKites, 3);
  const int sizes[][3] = {{960, 540, 0}, {1920, 1080, 0}, {1920, 1200, 0}, {1920, 1200, 1}, {3840, 2400, 0}};
  for (const auto& size : sizes) {
    Image picture(static_cast<std::uint32_t>(size[0]), static_cast<std::uint32_t>(size[1]));
    RenderOptions sized = maze::SceneOptions(themes[0], size[1]);
    sized.soften_edges = size[2] != 0;
    Renderer renderer(sized);
    const auto begin = std::chrono::steady_clock::now();
    int frames = 0;
    double worst = 0.0;
    for (std::size_t i = 0; i < all.size(); i += 40) {
      const auto before = std::chrono::steady_clock::now();
      renderer.Render(
          picture, maze::TankCamera(all[i], plan, maze::ViewMode::kChase, maze::kFovDefault, size[0], size[1]),
          [&](Canvas& canvas, const Camera& camera) {
            view.Draw(canvas, camera);
            battle.Draw(canvas, camera, kite_model);
            maze::DrawTank(canvas, camera, tank_model, all[i]);
          },
          [&](Image& image) { maze::DrawHud(image, battle, plan, all[i], maze::ViewMode::kChase, maze::kFovDefault, 0); });
      worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count());
      ++frames;
    }
    const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    std::printf("%dx%d%s: %.1f ms a frame on average, %.1f at worst\n", size[0], size[1], size[2] != 0 ? " softened" : "", total / frames, worst);
  }
  return ok ? 0 : 1;
}
