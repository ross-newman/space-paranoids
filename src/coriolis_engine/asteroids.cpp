#include "coriolis/asteroids.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "coriolis/hash.h"
#include "coriolis/model_builder.h"

namespace coriolis {

namespace {

struct ClassInfo {
  const char* name;
  float min_radius;
  float max_radius;
  /// The most a rock of the class is stretched along its long axis
  float max_elongation;
};

constexpr std::array<ClassInfo, kRockClassCount> kClasses{{
    {"SPLINTER", 8.0f, 20.0f, 1.4f},
    {"BOULDER", 20.0f, 55.0f, 0.7f},
    {"ASTEROID", 55.0f, 160.0f, 0.6f},
    {"LARGE ASTEROID", 160.0f, 400.0f, 0.5f},
    {"GIANT ASTEROID", 400.0f, 900.0f, 0.4f},
}};

struct TypeInfo {
  const char* prefix;
  Rgb colour;
  /// Kilograms per cubic metre
  float density;
};

constexpr std::array<TypeInfo, 4> kTypes{{
    {"", {138, 120, 100}, 2600.0f},
    {"METALLIC ", {150, 138, 130}, 5200.0f},
    {"ICY ", {188, 208, 224}, 950.0f},
    {"DARK ", {72, 68, 68}, 1900.0f},
}};

constexpr float kFourThirdsPi = 4.18879f;

const ClassInfo& Info(RockClass c) { return kClasses[static_cast<std::size_t>(c)]; }

float Fraction(std::uint32_t h) { return static_cast<float>(h % 1000U) / 999.0f; }

Vec3 RandomUnit(Random& rng) {
  for (;;) {
    const float x = static_cast<float>(rng.Range(-1.0, 1.0));
    const float y = static_cast<float>(rng.Range(-1.0, 1.0));
    const float z = static_cast<float>(rng.Range(-1.0, 1.0));
    const float d = x * x + y * y + z * z;
    if (d > 0.01f && d <= 1.0f) {
      const float inverse = 1.0f / std::sqrt(d);
      return {x * inverse, y * inverse, z * inverse};
    }
  }
}

float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

std::uint8_t Channel(float v) { return static_cast<std::uint8_t>(std::lround(std::fmin(255.0f, std::fmax(0.0f, v)))); }

struct Lump {
  Vec3 centre;
  float height;
  float sharpness;
};

}  // namespace

float RockClassMinRadius(RockClass size_class) { return Info(size_class).min_radius; }

float RockClassMaxRadius(RockClass size_class) { return Info(size_class).max_radius; }

const char* RockClassName(RockClass size_class) { return Info(size_class).name; }

RockClass PickRockClass(Random& rng) {
  const std::uint32_t pick = rng.Below(100);
  if (pick < 30) {
    return RockClass::kSplinter;
  }
  if (pick < 60) {
    return RockClass::kBoulder;
  }
  if (pick < 84) {
    return RockClass::kAsteroid;
  }
  return pick < 96 ? RockClass::kLarge : RockClass::kGiant;
}

AsteroidParams MakeAsteroidParams(std::uint32_t seed, RockClass size_class) {
  Random rng(static_cast<std::uint64_t>(seed) * 0x9E3779B1ULL + 0x51ED27ULL + static_cast<std::uint64_t>(size_class));
  const ClassInfo& info = Info(size_class);
  AsteroidParams p;
  p.seed = seed;
  p.size_class = size_class;
  // Sizes are spread evenly on a log scale, so a class has more small rocks than big ones
  p.radius_m = info.min_radius * std::pow(info.max_radius / info.min_radius, static_cast<float>(rng.Uniform()));
  const std::uint32_t type = rng.Below(100);
  p.type = type < 45 ? RockType::kRocky : (type < 65 ? RockType::kMetallic : (type < 80 ? RockType::kIcy : RockType::kCarbon));
  p.elongation = static_cast<float>(rng.Range(0.0, info.max_elongation));
  p.flatness = rng.Chance(0.4) ? static_cast<float>(rng.Range(0.6, 0.95)) : 1.0f;
  p.lumps = 3 + static_cast<int>(rng.Below(5));
  p.craters = size_class == RockClass::kSplinter ? static_cast<int>(rng.Below(2)) : static_cast<int>(rng.Below(4));
  p.roughness = static_cast<float>(rng.Range(0.03, 0.12));
  switch (size_class) {
    case RockClass::kSplinter:
      p.detail = static_cast<int>(rng.Below(2));
      break;
    case RockClass::kBoulder:
      p.detail = 1;
      break;
    case RockClass::kAsteroid:
      p.detail = 1 + static_cast<int>(rng.Below(2));
      break;
    default:
      p.detail = 2;
      break;
  }
  return p;
}

Model GenerateAsteroidModel(const AsteroidParams& p) {
  Random rng((static_cast<std::uint64_t>(p.seed) << 8) ^ 0xA57E401DULL ^
             ((static_cast<std::uint64_t>(p.size_class) + 1) * 0x9E3779B97F4A7C15ULL));

  // The 20 sided starting shape on a unit sphere, split as often as asked
  const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
  std::vector<Vec3> unit = {{-1, t, 0},  {1, t, 0},  {-1, -t, 0}, {1, -t, 0}, {0, -1, t},  {0, 1, t},
                            {0, -1, -t}, {0, 1, -t}, {t, 0, -1},  {t, 0, 1},  {-t, 0, -1}, {-t, 0, 1}};
  std::vector<std::array<std::size_t, 3>> faces = {{0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11},
                                                   {1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                                                   {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},
                                                   {4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1}};
  const auto normalise = [](const Vec3& v) {
    const float inverse = 1.0f / std::sqrt(Dot(v, v));
    return Vec3{v.x * inverse, v.y * inverse, v.z * inverse};
  };
  for (Vec3& v : unit) {
    v = normalise(v);
  }
  const auto cross = [](const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3 w{c.x - a.x, c.y - a.y, c.z - a.z};
    return Vec3{u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x};
  };
  // Wind every face counter-clockwise seen from outside, whatever way round the list above was written
  for (auto& f : faces) {
    const Vec3 middle{unit[f[0]].x + unit[f[1]].x + unit[f[2]].x, unit[f[0]].y + unit[f[1]].y + unit[f[2]].y,
                      unit[f[0]].z + unit[f[1]].z + unit[f[2]].z};
    if (Dot(cross(unit[f[0]], unit[f[1]], unit[f[2]]), middle) < 0.0f) {
      std::swap(f[1], f[2]);
    }
  }
  for (int level = 0; level < std::clamp(p.detail, 0, 2); ++level) {
    std::vector<std::array<std::size_t, 3>> split;
    std::vector<std::pair<std::pair<std::size_t, std::size_t>, std::size_t>> middles;
    const auto middle = [&](std::size_t a, std::size_t c) {
      const auto key = std::make_pair(std::min(a, c), std::max(a, c));
      for (const auto& m : middles) {
        if (m.first == key) {
          return m.second;
        }
      }
      unit.push_back(normalise({unit[a].x + unit[c].x, unit[a].y + unit[c].y, unit[a].z + unit[c].z}));
      middles.push_back({key, unit.size() - 1});
      return unit.size() - 1;
    };
    for (const auto& f : faces) {
      const std::size_t ab = middle(f[0], f[1]);
      const std::size_t bc = middle(f[1], f[2]);
      const std::size_t ca = middle(f[2], f[0]);
      split.push_back({f[0], ab, ca});
      split.push_back({f[1], bc, ab});
      split.push_back({f[2], ca, bc});
      split.push_back({ab, bc, ca});
    }
    faces = std::move(split);
  }

  // The shape: bulges and hollows over the whole rock, round dents, and a bump of its own at every vertex
  std::vector<Lump> lumps;
  for (int i = 0; i < p.lumps; ++i) {
    lumps.push_back({RandomUnit(rng), static_cast<float>(rng.Range(0.08, 0.28)) * (rng.Chance(0.5) ? 1.0f : -1.0f),
                     static_cast<float>(rng.Range(1.5, 6.0))});
  }
  std::vector<Lump> craters;
  for (int i = 0; i < p.craters; ++i) {
    craters.push_back({RandomUnit(rng), static_cast<float>(rng.Range(0.08, 0.2)), static_cast<float>(rng.Range(20.0, 60.0))});
  }
  // One axis is longer than the others, and one of the others may be flattened
  const std::size_t long_axis = rng.Below(3);
  const std::size_t flat_axis = (long_axis + 1 + rng.Below(2)) % 3;
  std::array<float, 3> stretch{1.0f, 1.0f, 1.0f};
  stretch[long_axis] = 1.0f + p.elongation;
  stretch[flat_axis] = p.flatness;

  std::vector<float> radius(unit.size());
  std::vector<Vec3> position(unit.size());
  for (std::size_t i = 0; i < unit.size(); ++i) {
    const Vec3& d = unit[i];
    float r = 1.0f;
    for (const Lump& l : lumps) {
      r += l.height * std::exp(-l.sharpness * (1.0f - Dot(d, l.centre)));
    }
    for (const Lump& c : craters) {
      r -= c.height * std::exp(-c.sharpness * (1.0f - Dot(d, c.centre)));
    }
    const std::uint32_t h = Hash32(static_cast<std::uint32_t>(i) * 977U + 13U + p.seed);
    r += p.roughness * (2.0f * Fraction(h) - 1.0f);
    radius[i] = std::max(0.45f, r);
    position[i] = {d.x * radius[i] * stretch[0], d.y * radius[i] * stretch[1], d.z * radius[i] * stretch[2]};
  }

  const TypeInfo& type = kTypes[static_cast<std::size_t>(p.type)];
  std::string name = std::string(type.prefix) + Info(p.size_class).name;
  ModelBuilder builder(name);
  for (const Vec3& v : position) {
    builder.Vertex(v.x, v.y, v.z);
  }
  // Each rock is tinted a little differently from the next of its type
  const float tint_r = static_cast<float>(rng.Range(-10.0, 10.0));
  const float tint_g = static_cast<float>(rng.Range(-10.0, 10.0));
  const float tint_b = static_cast<float>(rng.Range(-10.0, 10.0));
  for (std::size_t i = 0; i < faces.size(); ++i) {
    const auto& f = faces[i];
    const Vec3& a = position[f[0]];
    const Vec3& b = position[f[1]];
    const Vec3& c = position[f[2]];
    const Vec3 normal = cross(a, b, c);
    const float length = std::sqrt(Dot(normal, normal));
    if (length < 1e-9f) {
      continue;
    }
    // The renderer turns a normal away from the face's centre, so put the centre just behind the face along the normal
    // the winding gives: the rock does not have to be convex
    const Vec3 middle{(a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f, (a.z + b.z + c.z) / 3.0f};
    builder.Centre(middle.x - normal.x / length, middle.y - normal.y / length, middle.z - normal.z / length);

    // Darker in the hollows, lighter on the high ground, each face a little different
    const std::uint32_t h = Hash32(static_cast<std::uint32_t>(i) * 31U + 7U + p.seed * 3U);
    const float height = (radius[f[0]] + radius[f[1]] + radius[f[2]]) / 3.0f - 1.0f;
    float shade = 0.80f + 0.40f * Fraction(h) + 0.9f * height;
    float r = type.colour.r + tint_r;
    float g = type.colour.g + tint_g;
    float bl = type.colour.b + tint_b;
    const float speck = Fraction(Hash32(h + 99U));
    if (p.type == RockType::kMetallic && speck > 0.92f) {
      shade *= 1.35f;  // a bright metal face
    } else if (p.type == RockType::kIcy && speck > 0.9f) {
      r *= 0.85f;
      g *= 0.95f;
    } else if (p.type == RockType::kRocky && speck > 0.94f) {
      r *= 1.15f;  // a rusty patch
      bl *= 0.8f;
    }
    shade = std::clamp(shade, 0.35f, 1.5f);
    builder.Tri(f[0], f[1], f[2], {Channel(r * shade), Channel(g * shade), Channel(bl * shade)});
  }
  return builder.Finish(3.2f);
}

Blueprint GenerateAsteroidBlueprint(const AsteroidParams& p) {
  char id[40];
  std::snprintf(id, sizeof id, "rock_%08x_%d", static_cast<unsigned>(p.seed), static_cast<int>(p.size_class));
  Blueprint b = BlueprintFromModel(GenerateAsteroidModel(p), id);
  b.kind = HullKind::kAsteroid;
  // The model is scaled to a radius of 3.2 mesh units
  b.scale = p.radius_m / 3.2f;

  const float r = p.radius_m;
  const TypeInfo& type = kTypes[static_cast<std::size_t>(p.type)];
  BlueprintStats& s = b.stats;
  // Mass of a sphere of the rock's radius, a little under that for the odd shape
  s.mass_kg = kFourThirdsPi * r * r * r * type.density * 0.55f;
  s.max_speed = 30.0f;
  s.acceleration = 0.0f;
  s.max_pitch_rate = s.max_roll_rate = s.max_yaw_rate = 0.05f;
  s.hull = 10.0f + 2.5f * r;
  s.shield = 0.0f;
  s.energy = 0.0f;
  s.energy_recharge = 0.0f;
  s.target_radius = 0.8f * r;
  s.visibility_distance = std::clamp(250.0f * r, 6000.0f, 150000.0f);
  s.bounty = 0.0f;
  s.kill_points = 0.02f + r / 20000.0f;
  const int rich = (p.type == RockType::kMetallic && r > 20.0f) ? 1 : 0;
  s.max_cargo_drop = static_cast<std::uint32_t>(std::clamp(static_cast<int>(r / 35.0f) + rich, 0, 6));
  s.missiles = 0;
  return b;
}

const Blueprint& AsteroidCatalog::Get(std::uint32_t seed, RockClass size_class) {
  const std::uint64_t key = (static_cast<std::uint64_t>(seed) << 8) | static_cast<std::uint64_t>(size_class);
  auto found = rocks_.find(key);
  if (found == rocks_.end()) {
    found = rocks_
                .emplace(key, std::make_unique<Blueprint>(GenerateAsteroidBlueprint(MakeAsteroidParams(seed, size_class))))
                .first;
  }
  return *found->second;
}

}  // namespace coriolis
