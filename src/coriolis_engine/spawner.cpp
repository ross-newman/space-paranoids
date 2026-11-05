#include "coriolis/spawner.h"

#include <algorithm>
#include <cmath>

namespace coriolis {

namespace {

constexpr std::size_t kMaxShips = 16;

const char* const kPirates[] = {"sidewinder", "gecko", "krait",        "cobra_mk1",
                                "mamba",      "boa",   "fer_de_lance", "cobra_mk3_bounty_hunter"};
const char* const kTraders[] = {"shuttle", "cobra_mk3_trader", "adder", "transporter", "moray", "python", "anaconda"};
const char* const kPolice[] = {"viper"};
const char* const kHunters[] = {"cobra_mk3_bounty_hunter", "asp_mk2"};
const char* const kAliens[] = {"thargoid"};
const char* const kElite[] = {"constrictor", "cougar"};

Vec3d RandomUnit(Random& rng) {
  Vec3d v;
  do {
    v = {rng.Range(-1.0, 1.0), rng.Range(-1.0, 1.0), rng.Range(-1.0, 1.0)};
  } while (Dot(v, v) > 1.0 || Dot(v, v) < 0.01);
  return Normalized(v);
}

}  // namespace

void Spawner::PopulateRocks(World& world, const StarSystem& system) {
  const Entity* player = world.Find(world.player());
  if (player == nullptr) {
    return;
  }
  // The field is worked out from the system, so it is the same each visit
  Random rng(static_cast<std::uint64_t>(system.seed) ^ 0x524F434B53ULL);
  const Vec3d centre = player->position + Forward(player->orientation) * 25000.0;
  const int count = 5 + static_cast<int>(rng.Below(8));

  struct Placed {
    Vec3d position;
    double radius;
  };
  std::vector<Placed> placed;
  // Add a rock at a place near `around` that is clear of the rocks already there, or give up after a few tries
  const auto add = [&](const Blueprint& bp, const Vec3d& around, double min_range, double max_range,
                       const Vec3d& drift) -> const Entity* {
    const double radius = BoundingRadius(bp);
    for (int attempt = 0; attempt < 12; ++attempt) {
      const Vec3d where = around + RandomUnit(rng) * rng.Range(min_range, max_range);
      bool clear = true;
      for (const Placed& other : placed) {
        clear = clear && Distance(where, other.position) > (radius + other.radius) * 1.15;
      }
      if (!clear) {
        continue;
      }
      const EntityId id = world.SpawnShip(bp, Team::kNeutral, where, Quat{});
      Entity* e = world.Find(id);
      e->velocity = drift + RandomUnit(rng) * rng.Range(0.0, 3.0);
      placed.push_back({where, radius});
      return e;
    }
    return nullptr;
  };

  bool giant = false;
  for (int i = 0; i < count; ++i) {
    RockClass size_class = PickRockClass(rng);
    if (size_class == RockClass::kGiant) {
      // One giant to a field is plenty
      size_class = giant ? RockClass::kLarge : size_class;
      giant = true;
    }
    const Blueprint& bp = rocks_.Get(static_cast<std::uint32_t>(rng.Next()), size_class);
    const Vec3d drift = RandomUnit(rng) * rng.Range(0.0, 12.0);
    const Entity* rock = add(bp, centre, 1000.0, 14000.0, drift);
    if (rock == nullptr || size_class < RockClass::kAsteroid) {
      continue;
    }
    // Debris round the big ones, drifting with them
    const Vec3d parent = rock->position;
    const double parent_radius = rock->radius;
    const Vec3d parent_velocity = rock->velocity;
    const int pieces = 1 + static_cast<int>(rng.Below(4));
    for (int piece = 0; piece < pieces; ++piece) {
      const RockClass small = rng.Chance(0.6) ? RockClass::kSplinter : RockClass::kBoulder;
      add(rocks_.Get(static_cast<std::uint32_t>(rng.Next()), small), parent, parent_radius * 1.5, parent_radius * 3.2,
          parent_velocity);
    }
  }
}

Vec3d Spawner::PlaceAround(World& world, const Vec3d& player_position, double min_range, double max_range,
                           bool outside_safe_zone) {
  const Entity* station = world.FindKind(EntityKind::kStation);
  Vec3d p = player_position;
  for (int attempt = 0; attempt < 8; ++attempt) {
    p = player_position + RandomUnit(world.rng()) * world.rng().Range(min_range, max_range);
    if (!outside_safe_zone || station == nullptr || Distance(p, station->position) > kSafeZone) {
      break;
    }
  }
  return p;
}

void Spawner::SpawnPack(World& world, const Vec3d& player_position, const char* const* pool, int pool_size, Team team,
                        int count) {
  const Vec3d centre =
      PlaceAround(world, player_position, 9000.0, 16000.0, team == Team::kPirate || team == Team::kAlien);
  const int kind = static_cast<int>(world.rng().Below(static_cast<std::uint32_t>(pool_size)));
  const Blueprint* bp = FindBuiltInBlueprint(pool[kind]);
  if (bp == nullptr) {
    return;
  }
  const Vec3d toward = Normalized(player_position - centre);
  for (int i = 0; i < count; ++i) {
    const Vec3d where = centre + RandomUnit(world.rng()) * (bp->stats.target_radius * 6.0);
    const EntityId id = world.SpawnShip(*bp, team, where, LookAlong(toward, {0.0, 1.0, 0.0}));
    if (Entity* e = world.Find(id)) {
      e->velocity = toward * (bp->stats.max_speed * 0.5);
      e->controls.throttle = 0.5;
      e->group = tick_;
    }
  }
}

void Spawner::Update(World& world, const StarSystem& system, double dt, double kills, bool wanted, bool peaceful) {
  const Entity* player = world.Find(world.player());
  if (player == nullptr || !player->alive || world.CountShips() >= kMaxShips) {
    return;
  }
  // Spawning can move the entities about in memory, so keep a copy of what is needed, not the pointer
  const Vec3d player_position = player->position;
  Random& rng = world.rng();
  ++tick_;

  // Rates are events per second
  const double lawless = 1.0 + (7.0 - system.government) * 0.35;
  const double trade = 0.5 + system.tech_level / 15.0;
  const double order = 0.4 + system.government * 0.3;

  if (rng.Chance(dt / 45.0 * trade)) {
    // Traders come in from the dark and fly for the station
    const Blueprint* bp = FindBuiltInBlueprint(
        kTraders[rng.Below(std::min<std::uint32_t>(7, 3 + static_cast<std::uint32_t>(system.tech_level / 3)))]);
    const Entity* station = world.FindKind(EntityKind::kStation);
    if (bp != nullptr && station != nullptr) {
      const Vec3d where = PlaceAround(world, player_position, 9000.0, 16000.0, false);
      world.SpawnShip(*bp, Team::kTrader, where, LookAlong(station->position - where, {0.0, 1.0, 0.0}));
    }
  }
  if (!peaceful && rng.Chance(dt / 160.0 * lawless)) {
    // The better the pilot, the nastier the pirates
    const int tier = kills >= 512 ? 8 : (kills >= 128 ? 7 : (kills >= 32 ? 6 : (kills >= 8 ? 5 : 3)));
    const int pack = 1 + static_cast<int>(rng.Below(kills >= 32 ? 3 : 2));
    SpawnPack(world, player_position, kPirates, tier, Team::kPirate, pack);
  }
  if (rng.Chance(dt * (wanted ? 1.0 / 12.0 : 1.0 / 150.0 * order))) {
    SpawnPack(world, player_position, kPolice, 1, Team::kPolice, wanted ? 2 : 1);
  }
  if (!peaceful && rng.Chance(dt / 260.0)) {
    SpawnPack(world, player_position, kHunters, 2, Team::kHunter, 1);
  }
  if (!peaceful && kills >= 64 && rng.Chance(dt / 1200.0)) {
    SpawnPack(world, player_position, kElite, 2, Team::kPirate, 1);
  }
  bool aliens = false;
  for (const Entity& e : world.entities()) {
    aliens = aliens || (e.alive && e.team == Team::kAlien);
  }
  if (!peaceful && !aliens && rng.Chance(dt / 1500.0)) {
    SpawnPack(world, player_position, kAliens, 1, Team::kAlien, 1);
  }
}

}  // namespace coriolis
