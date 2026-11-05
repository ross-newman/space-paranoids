#include "battle.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "softrender/scene.h"
#include "softrender/text.h"

namespace maze {

namespace {

using softrender::Rgb;
using softrender::Vec3d;

constexpr double kPi = 3.14159265358979323846;
constexpr double kKiteSpeed = 7.0;
constexpr double kKiteTurnRate = 3.0;  // how fast a kite swings round, radians a second
constexpr double kKiteHeight = 2.4;
constexpr double kKiteScale = 2.1;
constexpr double kHitRadius = 2.1;  // a kite is wide
constexpr double kHitHeight = 0.9;  // and flat, so a shot has to come in at its height
constexpr double kShotSpeed = 70.0;
constexpr double kShotLife = 1.2;
constexpr double kShotLength = 3.5;
constexpr double kReload = 0.16;
constexpr double kMuzzleHeight = 2.0;
constexpr double kMuzzleForward = 2.4;
constexpr double kBarrelSide = 0.42;
constexpr double kSparkLife = 0.7;
constexpr int kSparksPerKite = 36;
constexpr double kTankScale = 2.0;
constexpr double kTankHeight = 0.72;

/// A room or passage next to this tile, for a kite to fly on to. It only turns back at a dead end.
Tile NextTile(const Kite& kite, const Plan& plan, std::mt19937& random) {
  constexpr int kSteps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  std::vector<Tile> ways;
  for (const auto& step : kSteps) {
    const Tile next{kite.to.first + step[0], kite.to.second + step[1]};
    if (!plan.Solid(next.first, next.second) && next != kite.from) {
      ways.push_back(next);
    }
  }
  return ways.empty() ? kite.from : ways[random() % ways.size()];
}

}  // namespace

Battle::Battle(const Plan& plan, int kites, std::uint32_t seed) : random_(seed) {
  std::vector<Tile> rooms;
  for (int tz = 1; tz < plan.tiles_z(); tz += 2) {
    for (int tx = 1; tx < plan.tiles_x(); tx += 2) {
      if (plan.Path(plan.start(), {tx, tz}).size() > 8) {
        rooms.push_back({tx, tz});
      }
    }
  }
  std::shuffle(rooms.begin(), rooms.end(), random_);
  for (std::size_t i = 0; i < rooms.size() && i < static_cast<std::size_t>(kites); ++i) {
    Kite kite;
    kite.from = rooms[i];
    kite.to = rooms[i];
    kite.x = TileCentre(rooms[i].first);
    kite.z = TileCentre(rooms[i].second);
    kite.time = static_cast<double>(i);
    kite.to = NextTile(kite, plan, random_);
    kites_.push_back(kite);
  }
}

void Battle::Update(const Tank& tank, bool firing, double dt, const Plan& plan) {
  for (Kite& kite : kites_) {
    kite.time += dt;
    kite.progress += kKiteSpeed * dt / kTile;
    if (kite.progress >= 1.0) {
      kite.progress -= 1.0;
      const Tile reached = kite.to;
      kite.to = NextTile(kite, plan, random_);
      kite.from = reached;
    }
    kite.x = TileCentre(kite.from.first) + (TileCentre(kite.to.first) - TileCentre(kite.from.first)) * kite.progress;
    kite.z = TileCentre(kite.from.second) + (TileCentre(kite.to.second) - TileCentre(kite.from.second)) * kite.progress;
    const double wanted = std::atan2(static_cast<double>(kite.to.first - kite.from.first), static_cast<double>(kite.to.second - kite.from.second));
    const double error = std::remainder(wanted - kite.heading, 2.0 * kPi);
    kite.heading += std::clamp(error, -kKiteTurnRate * dt, kKiteTurnRate * dt);
  }

  reload_ -= dt;
  if (firing && reload_ <= 0.0) {
    reload_ = kReload;
    ++shots_fired_;
    barrel_ = 1 - barrel_;
    const double sx = std::sin(tank.heading);
    const double cz = std::cos(tank.heading);
    const double side = barrel_ == 0 ? -kBarrelSide : kBarrelSide;
    const Vec3d direction = GunDirection(tank);
    const Vec3d muzzle = Vec3d{tank.x + cz * side, kMuzzleHeight, tank.z - sx * side} + direction * kMuzzleForward;
    shots_.push_back({muzzle, direction, 0.0});
  }

  // Shots go in small steps, so that they can't pass through a wall or a kite between frames
  for (Shot& shot : shots_) {
    shot.age += dt;
    const int steps = std::max(1, static_cast<int>(std::ceil(kShotSpeed * dt / 0.5)));
    for (int i = 0; i < steps && shot.age < kShotLife; ++i) {
      shot.position = shot.position + shot.direction * (kShotSpeed * dt / steps);
      if (plan.Hits(shot.position)) {
        shot.age = kShotLife;
        break;
      }
      for (std::size_t k = 0; k < kites_.size(); ++k) {
        if (std::hypot(kites_[k].x - shot.position.x, kites_[k].z - shot.position.z) < kHitRadius && std::fabs(kKiteHeight - shot.position.y) < kHitHeight) {
          const Vec3d at{kites_[k].x, kKiteHeight, kites_[k].z};
          for (int s = 0; s < kSparksPerKite; ++s) {
            std::uniform_real_distribution<double> spread(-1.0, 1.0);
            const Vec3d way = softrender::Normalized(Vec3d{spread(random_), spread(random_) * 0.7, spread(random_)});
            sparks_.push_back({at, way * (6.0 + 9.0 * (spread(random_) + 1.0)), 0.0});
          }
          kites_.erase(kites_.begin() + static_cast<std::ptrdiff_t>(k));
          ++destroyed_;
          shot.age = kShotLife;
          break;
        }
      }
    }
  }
  shots_.erase(std::remove_if(shots_.begin(), shots_.end(), [](const Shot& s) { return s.age >= kShotLife; }), shots_.end());

  for (Spark& spark : sparks_) {
    spark.age += dt;
    spark.velocity.y -= 14.0 * dt;
    spark.position = spark.position + spark.velocity * dt;
  }
  sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(), [](const Spark& s) { return s.age >= kSparkLife; }), sparks_.end());
}

void Battle::Draw(softrender::Canvas& canvas, const softrender::Camera& camera, const softrender::Model& kite_model) const {
  for (const Kite& kite : kites_) {
    softrender::Transform transform;
    transform.position = {kite.x, kKiteHeight + 0.3 * std::sin(kite.time * 1.7), kite.z};
    transform.orientation = softrender::LookAlong({std::sin(kite.heading), 0.0, std::cos(kite.heading)}, {0.0, 1.0, 0.0});
    transform.scale = kKiteScale;
    softrender::Lighting lighting;
    lighting.direction = {0.05, 1.0, 0.05};
    softrender::DrawModel(canvas, camera, kite_model, transform, lighting);
  }
  for (const Shot& shot : shots_) {
    const Vec3d tail = shot.position - shot.direction * kShotLength;
    softrender::DrawLine3D(canvas, camera, tail, shot.position, {255, 240, 150});
    softrender::DrawLine3D(canvas, camera, tail + Vec3d{0.0, 0.05, 0.0}, shot.position + Vec3d{0.0, 0.05, 0.0}, {255, 180, 60});
  }
  for (const Spark& spark : sparks_) {
    const double fade = 1.0 - spark.age / kSparkLife;
    const Rgb colour{255, static_cast<std::uint8_t>(90 + 140 * fade), static_cast<std::uint8_t>(40 * fade)};
    softrender::DrawLine3D(canvas, camera, spark.position, spark.position - spark.velocity * 0.05, colour);
  }
}

void DrawTank(softrender::Canvas& canvas, const softrender::Camera& camera, const vehicles::CrawlerParts& model, const Tank& tank) {
  softrender::Transform transform;
  transform.position = {tank.x, kTankHeight, tank.z};
  transform.orientation = softrender::LookAlong({std::sin(tank.heading), 0.0, std::cos(tank.heading)}, {0.0, 1.0, 0.0});
  transform.scale = kTankScale;
  softrender::Lighting lighting;
  lighting.direction = {0.05, 1.0, 0.05};
  softrender::DrawModel(canvas, camera, model.hull, transform, lighting);

  const Vec3d pivot{model.gun_pivot.x, model.gun_pivot.y, model.gun_pivot.z};
  softrender::Transform gun = transform;
  gun.position = transform.position + softrender::Rotate(transform.orientation, pivot * kTankScale);
  gun.orientation = softrender::LookAlong(GunDirection(tank), {0.0, 1.0, 0.0});
  softrender::DrawModel(canvas, camera, model.gun, gun, lighting);
}

namespace {

constexpr Rgb kHudGreen{70, 255, 110};

void DrawCockpitFrame(softrender::Image& image, float w, float h, float margin, float weight) {
  const float cx = w / 2.0f;
  const float cy = h / 2.0f;
  const auto stroke = [&](float x0, float y0, float x1, float y1) { softrender::DrawLine(image, x0, y0, x1, y1, weight, kHudGreen); };
  const float gap_x = 0.055f * h;  // the openings in the middle of each side
  const float gap_y = 0.05f * h;
  const float tick = 0.045f * h;
  for (const float sx : {-1.0f, 1.0f}) {
    for (const float sy : {-1.0f, 1.0f}) {
      // A bracket round each corner of the picture
      const float corner_x = cx + sx * (w / 2.0f - margin);
      const float corner_y = cy + sy * (h / 2.0f - margin);
      stroke(corner_x, corner_y, cx + sx * gap_x, corner_y);
      stroke(cx + sx * gap_x, corner_y, cx + sx * gap_x, corner_y - sy * tick);
      stroke(corner_x, corner_y, corner_x, cy + sy * gap_y);
      stroke(corner_x, cy + sy * gap_y, corner_x - sx * tick, cy + sy * gap_y);
      // The long sight lines, each turning out to the side at its foot
      stroke(cx + sx * 0.09f * h, cy + sy * 0.40f * h, cx + sx * 0.40f * h, cy + sy * 0.09f * h);
      stroke(cx + sx * 0.40f * h, cy + sy * 0.09f * h, cx + sx * 0.52f * h, cy + sy * 0.09f * h);
      // The chevrons round the middle
      stroke(cx + sx * 0.035f * h, cy + sy * 0.19f * h, cx + sx * 0.165f * h, cy + sy * 0.06f * h);
      stroke(cx + sx * 0.165f * h, cy + sy * 0.06f * h, cx + sx * 0.07f * h, cy + sy * 0.06f * h);
    }
  }
  // The hash at the point of aim
  const float bar = 0.05f * h;
  const float apart = 0.017f * h;
  for (const float side : {-apart, apart}) {
    stroke(cx + side, cy - bar, cx + side, cy + bar);
    stroke(cx - bar, cy + side, cx + bar, cy + side);
  }
}

/// Cut a line down to the part inside a circle round the origin. False if none of it is.
bool ClipToCircle(float radius, float* x0, float* y0, float* x1, float* y1) {
  const float dx = *x1 - *x0;
  const float dy = *y1 - *y0;
  const float a = dx * dx + dy * dy;
  const float b = 2.0f * (*x0 * dx + *y0 * dy);
  const float c = *x0 * *x0 + *y0 * *y0 - radius * radius;
  const float disc = b * b - 4.0f * a * c;
  if (a <= 0.0f || disc <= 0.0f) {
    return false;
  }
  const float root = std::sqrt(disc);
  const float from = std::max(0.0f, (-b - root) / (2.0f * a));
  const float to = std::min(1.0f, (-b + root) / (2.0f * a));
  if (from >= to) {
    return false;
  }
  const float start_x = *x0 + dx * from;
  const float start_y = *y0 + dy * from;
  *x1 = *x0 + dx * to;
  *y1 = *y0 + dy * to;
  *x0 = start_x;
  *y0 = start_y;
  return true;
}

/// A round map of what is near the tank, with the way it is heading always up. The walls are lines, drawn wherever a
/// solid tile meets an open one.
void DrawRadar(softrender::Image& image, const Battle& battle, const Plan& plan, const Tank& tank, float cx, float cy, float radius, float weight) {
  constexpr double kRange = 42.0;  // world units from the middle to the rim
  const double scale = kRange / radius;
  const double forward_x = std::sin(tank.heading);
  const double forward_z = std::cos(tank.heading);
  const double right_x = forward_z;
  const double right_z = -forward_x;
  const int width = static_cast<int>(image.width());
  const int height = static_cast<int>(image.height());
  const int r = static_cast<int>(radius);
  for (int y = std::max(0, static_cast<int>(cy) - r); y <= std::min(height - 1, static_cast<int>(cy) + r); ++y) {
    for (int x = std::max(0, static_cast<int>(cx) - r); x <= std::min(width - 1, static_cast<int>(cx) + r); ++x) {
      const float across = static_cast<float>(x) - cx;
      const float up = cy - static_cast<float>(y);
      if (across * across + up * up > radius * radius) {
        continue;
      }
      // Dark glass with a little of the view coming through, and a lighter green over the walls
      constexpr Rgb kGlass{2, 24, 11};
      constexpr Rgb kWallGlass{16, 92, 44};
      const double wx = tank.x + (right_x * across + forward_x * up) * scale;
      const double wz = tank.z + (right_z * across + forward_z * up) * scale;
      const bool over_wall = plan.Solid(static_cast<int>(std::floor(wx / kTile)), static_cast<int>(std::floor(wz / kTile)));
      const Rgb tint = over_wall ? kWallGlass : kGlass;
      const float strength = over_wall ? 0.66f : 0.72f;
      std::uint8_t* pixel = image.data() + (static_cast<std::size_t>(y) * width + x) * 3;
      pixel[0] = static_cast<std::uint8_t>(pixel[0] + (tint.r - pixel[0]) * strength);
      pixel[1] = static_cast<std::uint8_t>(pixel[1] + (tint.g - pixel[1]) * strength);
      pixel[2] = static_cast<std::uint8_t>(pixel[2] + (tint.b - pixel[2]) * strength);
    }
  }

  const auto wall = [&](double x0, double z0, double x1, double z1) {
    const auto across = [&](double x, double z) { return static_cast<float>(((x - tank.x) * right_x + (z - tank.z) * right_z) / scale); };
    const auto up = [&](double x, double z) { return static_cast<float>(((x - tank.x) * forward_x + (z - tank.z) * forward_z) / scale); };
    float ax = across(x0, z0);
    float ay = up(x0, z0);
    float bx = across(x1, z1);
    float by = up(x1, z1);
    if (ClipToCircle(radius, &ax, &ay, &bx, &by)) {
      softrender::DrawLine(image, cx + ax, cy - ay, cx + bx, cy - by, weight * 0.42f, kHudGreen);
    }
  };
  const int reach = static_cast<int>(kRange / kTile) + 2;
  const int home_x = static_cast<int>(std::floor(tank.x / kTile));
  const int home_z = static_cast<int>(std::floor(tank.z / kTile));
  for (int tz = home_z - reach; tz <= home_z + reach; ++tz) {
    for (int tx = home_x - reach; tx <= home_x + reach; ++tx) {
      if (plan.Solid(tx, tz) && tx >= 0 && tz >= 0 && tx < plan.tiles_x() && tz < plan.tiles_z()) {
        const double x0 = tx * kTile;
        const double z0 = tz * kTile;
        const double x1 = x0 + kTile;
        const double z1 = z0 + kTile;
        if (!plan.Solid(tx - 1, tz)) wall(x0, z0, x0, z1);
        if (!plan.Solid(tx + 1, tz)) wall(x1, z0, x1, z1);
        if (!plan.Solid(tx, tz - 1)) wall(x0, z0, x1, z0);
        if (!plan.Solid(tx, tz + 1)) wall(x0, z1, x1, z1);
      }
    }
  }
  softrender::DrawCircle(image, cx, cy, radius, weight * 1.3f, kHudGreen);
  for (int i = 0; i < 8; ++i) {
    const float angle = static_cast<float>(i) * static_cast<float>(kPi) / 4.0f;
    const float sx = std::sin(angle);
    const float sy = std::cos(angle);
    softrender::DrawLine(image, cx + sx * radius * 0.92f, cy - sy * radius * 0.92f, cx + sx * radius * 1.08f, cy - sy * radius * 1.08f, weight, kHudGreen);
  }
  for (const Kite& kite : battle.kites()) {
    const double dx = kite.x - tank.x;
    const double dz = kite.z - tank.z;
    const float across = static_cast<float>((dx * right_x + dz * right_z) / scale);
    const float up = static_cast<float>((dx * forward_x + dz * forward_z) / scale);
    if (across * across + up * up < radius * radius * 0.8f) {
      softrender::DrawDisc(image, cx + across, cy - up, radius * 0.055f, {255, 60, 40});
    }
  }
  // The tank, pointing up
  const float size = radius * 0.11f;
  softrender::DrawLine(image, cx, cy - size * 1.3f, cx - size, cy + size, weight, {235, 255, 235});
  softrender::DrawLine(image, cx, cy - size * 1.3f, cx + size, cy + size, weight, {235, 255, 235});
  softrender::DrawLine(image, cx - size, cy + size, cx + size, cy + size, weight, {235, 255, 235});
}

}  // namespace

softrender::RenderOptions SceneOptions(const Theme& theme, int height) {
  softrender::RenderOptions options;
  options.background = theme.sky;
  options.bloom_threshold = 170.0f;
  options.bloom_cell = std::max(4, height / 135);  // the glow keeps its size on the picture
  options.outline = {0.9f * static_cast<float>(height) / 540.0f, 5.0f};  // the vehicles are dark, and lined in light
  return options;
}

void DrawHud(softrender::Image& image, const Battle& battle, const Plan& plan, const Tank& tank, ViewMode mode, double fov, int score) {
  const float w = static_cast<float>(image.width());
  const float h = static_cast<float>(image.height());
  const float weight = h / 150.0f;  // 2.5 pixels thick on a 540 line picture: a stroke is 0.7 of its scale
  const float margin = 0.035f * h;
  const auto text_width = [](const std::string& text, float scale) { return 6.0f * scale * static_cast<float>(text.size()); };
  if (mode == ViewMode::kCockpit) {
    DrawCockpitFrame(image, w, h, margin, weight);
  }
  const float text_scale = h / 270.0f;
  const std::string kites = "KITES " + std::to_string(battle.kites().size());
  softrender::DrawText(image, kites, static_cast<std::int64_t>(margin * 1.7f), static_cast<std::int64_t>(margin * 1.7f), text_scale, kHudGreen);
  const std::string points = std::to_string(score);
  softrender::DrawText(image, points, static_cast<std::int64_t>((w - text_width(points, text_scale)) / 2.0f), static_cast<std::int64_t>(mode == ViewMode::kCockpit ? h * 0.875f : margin * 1.7f), text_scale, kHudGreen);
  const std::string view = "CAM " + std::to_string(static_cast<int>(mode) + 1) + "  FOV " + std::to_string(static_cast<int>(fov));
  softrender::DrawText(image, view, static_cast<std::int64_t>(w - margin * 1.7f - text_width(view, text_scale)), static_cast<std::int64_t>(margin * 1.7f), text_scale, kHudGreen);
  DrawRadar(image, battle, plan, tank, margin * 2.0f + 0.13f * h, h - margin * 2.0f - 0.13f * h, 0.13f * h, weight);
}

void DrawPauseScreen(softrender::Image& image) {
  std::uint8_t* pixel = image.data();
  for (std::size_t i = 0; i < image.size(); ++i) {
    pixel[i] = static_cast<std::uint8_t>(pixel[i] / 4);
  }
  const float h = static_cast<float>(image.height());
  const float w = static_cast<float>(image.width());
  const float title = h / 110.0f;
  const float text = h / 215.0f;
  const float row = 11.5f * text;
  const auto put = [&](const std::string& s, float x, float y, float scale, Rgb colour) { softrender::DrawText(image, s, static_cast<std::int64_t>(x), static_cast<std::int64_t>(y), scale, colour); };
  put("PAUSED", (w - 36.0f * title) / 2.0f, 0.05f * h, title, kHudGreen);

  const float keys_x = 0.08f * w;
  const float does_x = 0.35f * w;
  const float pad_x = 0.62f * w;
  float y = 0.2f * h;
  constexpr Rgb kDim{120, 200, 145};
  put("KEY", keys_x, y, text, kDim);
  put("ACTION", does_x, y, text, kDim);
  put("CONTROLLER", pad_x, y, text, kDim);
  y += row * 1.4f;
  const struct {
    const char* key;
    const char* action;
    const char* pad;
  } rows[] = {
      {"UP DOWN", "DRIVE", "LEFT STICK OR D-PAD"},
      {"LEFT RIGHT  A D", "TURN", "LEFT STICK OR D-PAD"},
      {"W S", "GUNS UP DOWN", "RIGHT STICK"},
      {"SPACE", "FIRE", "A OR RIGHT TRIGGER"},
      {"1 2 3 4", "CAMERA", ""},
      {"V", "NEXT CAMERA", "Y"},
      {"- =", "FIELD OF VIEW", "SHOULDER BUTTONS"},
      {"T", "COLOURS", "X"},
      {"R", "NEW MAZE", "BACK"},
      {"N", "SOUND", ""},
      {"ALT ENTER", "FULLSCREEN", ""},
      {"ESC", "RESUME", "START"},
      {"Q", "QUIT", ""},
  };
  for (const auto& entry : rows) {
    put(entry.key, keys_x, y, text, kHudGreen);
    put(entry.action, does_x, y, text, {235, 255, 235});
    put(entry.pad, pad_x, y, text, kHudGreen);
    y += row;
  }
}

}  // namespace maze
