#include "coriolis/world.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "coriolis/ai.h"
#include "coriolis/market.h"
#include "coriolis/steering.h"

namespace coriolis {

namespace {

Vec3d ToVec(const Vec3& v) { return {v.x, v.y, v.z}; }

bool IsSolid(const Entity& e) { return e.alive && e.kind != EntityKind::kPlanet && e.kind != EntityKind::kSun; }

const Hardpoint* FindHardpoint(const Entity& e, HardpointKind kind) {
  if (e.blueprint == nullptr) {
    return nullptr;
  }
  for (const Hardpoint& h : e.blueprint->hardpoints) {
    if (h.kind == kind) {
      return &h;
    }
  }
  return nullptr;
}

/// The radius used for ramming and being rammed
double CollisionRadius(const Entity& e) { return e.kind == EntityKind::kStation ? e.hit_radius : e.radius * 0.8; }

double Mass(const Entity& e) {
  return e.blueprint != nullptr ? std::max(1.0, static_cast<double>(e.stats().mass_kg)) : 1e12;
}

/// Where a segment first enters a sphere, as a fraction 0 to 1 of the segment, or a negative number if it does not
double SegmentSphere(const Vec3d& from, const Vec3d& to, const Vec3d& centre, double radius) {
  const Vec3d d = to - from;
  const Vec3d f = from - centre;
  const double a = Dot(d, d);
  const double c = Dot(f, f) - radius * radius;
  if (c <= 0.0) {
    return 0.0;
  }
  if (a < 1e-12) {
    return -1.0;
  }
  const double b = 2.0 * Dot(f, d);
  const double disc = b * b - 4.0 * a * c;
  if (disc < 0.0) {
    return -1.0;
  }
  const double t = (-b - std::sqrt(disc)) / (2.0 * a);
  return (t >= 0.0 && t <= 1.0) ? t : -1.0;
}

Vec3d RandomDirection(Random& rng) {
  Vec3d v;
  do {
    v = {rng.Range(-1.0, 1.0), rng.Range(-1.0, 1.0), rng.Range(-1.0, 1.0)};
  } while (Dot(v, v) > 1.0 || Dot(v, v) < 0.01);
  return Normalized(v);
}

}  // namespace

double BoundingRadius(const Blueprint& blueprint) {
  double r2 = 0.0;
  for (const BlueprintVertex& v : blueprint.vertices) {
    r2 = std::max(r2, static_cast<double>(v.position.x) * v.position.x +
                          static_cast<double>(v.position.y) * v.position.y +
                          static_cast<double>(v.position.z) * v.position.z);
  }
  return std::sqrt(r2) * blueprint.scale;
}

int Entity::SubsystemIndex(const std::string& name) const {
  if (blueprint == nullptr) {
    return -1;
  }
  for (std::size_t i = 0; i < blueprint->subsystems.size(); ++i) {
    if (blueprint->subsystems[i].name == name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool Entity::SubsystemDown(const std::string& name) const {
  const int i = SubsystemIndex(name);
  return i >= 0 && static_cast<std::size_t>(i) < subsystem_health.size() &&
         subsystem_health[static_cast<std::size_t>(i)] <= 0.0;
}

Vec3d DockPoint(const Entity& station) {
  const Hardpoint* h = FindHardpoint(station, HardpointKind::kDock);
  if (h == nullptr) {
    return station.position;
  }
  return station.position +
         Rotate(station.orientation, ToVec(h->position) * static_cast<double>(station.blueprint->scale));
}

Vec3d DockAxis(const Entity& station) {
  const Hardpoint* h = FindHardpoint(station, HardpointKind::kDock);
  return h == nullptr ? Forward(station.orientation) : Rotate(station.orientation, ToVec(h->direction));
}

double DockRadius(const Entity& station) { return Clamp(0.2 * station.radius, 60.0, 150.0); }

Bay BayOf(const Entity& station) {
  Bay bay;
  bay.mouth = DockPoint(station);
  bay.axis = DockAxis(station);
  bay.up = Up(station.orientation);
  bay.right = Normalized(Cross(bay.up, bay.axis));
  // The same size as the opening on the front of the station, but no bigger than a bay built for ships: the wheel
  // station is far bigger than its bay
  bay.half_width = std::min(0.24 * station.radius, 480.0);
  bay.half_height = std::min(0.09 * station.radius, 175.0);
  bay.depth = std::min(0.8 * station.radius, 850.0);
  return bay;
}

bool CanDock(const Entity& ship, const Entity& station, bool strict) {
  if (station.hostile || !station.alive) {
    return false;
  }
  if (Distance(ship.position, DockPoint(station)) > DockRadius(station)) {
    return false;
  }
  if (!strict) {
    return true;
  }
  if (AngleBetween(Forward(ship.orientation), -DockAxis(station)) > 0.45) {
    return false;
  }
  if (AngleBetween(Up(ship.orientation), Up(station.orientation)) > 0.6) {
    return false;
  }
  return Length(ship.velocity - station.velocity) < 250.0;
}

World::World(std::uint64_t seed) : rng_(seed) {}

EntityId World::Add(Entity e) {
  e.id = next_id_++;
  const EntityId id = e.id;
  (stepping_ ? pending_ : entities_).push_back(std::move(e));
  return id;
}

EntityId World::SpawnShip(const Blueprint& bp, Team team, const Vec3d& position, const Quat& orientation) {
  Entity e;
  switch (bp.kind) {
    case HullKind::kShip:
      e.kind = EntityKind::kShip;
      break;
    case HullKind::kStation:
      e.kind = EntityKind::kStation;
      break;
    case HullKind::kMissile:
      e.kind = EntityKind::kMissile;
      break;
    case HullKind::kCanister:
      e.kind = EntityKind::kCanister;
      break;
    case HullKind::kAsteroid:
      e.kind = EntityKind::kAsteroid;
      break;
    case HullKind::kEscapePod:
      e.kind = EntityKind::kEscapePod;
      break;
  }
  e.team = e.kind == EntityKind::kStation ? Team::kStation : team;
  e.blueprint = &bp;
  e.position = position;
  e.orientation = Normalized(orientation);
  e.radius = BoundingRadius(bp);
  e.hit_radius = std::max(static_cast<double>(bp.stats.target_radius), 0.6 * e.radius);
  e.hull = e.hull_max = bp.stats.hull;
  e.shield = e.shield_max = bp.stats.shield;
  e.energy = e.energy_max = bp.stats.energy;
  for (const Subsystem& s : bp.subsystems) {
    e.subsystem_health.push_back(s.health);
  }
  e.missiles = static_cast<int>(bp.stats.missiles);
  switch (e.kind) {
    case EntityKind::kStation: {
      const Hardpoint* h = FindHardpoint(e, HardpointKind::kDock);
      e.hit_radius = h != nullptr ? 0.9 * Length(ToVec(h->position)) * bp.scale : 0.7 * e.radius;
      e.spin = {0.0, bp.stats.max_roll_rate, 0.0};
      break;
    }
    case EntityKind::kMissile:
      e.life = 25.0;
      e.hit_radius = std::max(e.hit_radius, 6.0);
      break;
    case EntityKind::kCanister:
      e.life = 900.0;
      e.spin = {rng_.Range(-0.5, 0.5), rng_.Range(-0.5, 0.5), rng_.Range(-0.5, 0.5)};
      break;
    case EntityKind::kAsteroid:
      e.hit_radius = 0.9 * e.radius;
      e.spin = {rng_.Range(-0.1, 0.1), rng_.Range(-0.1, 0.1), rng_.Range(-0.1, 0.1)};
      break;
    case EntityKind::kEscapePod:
      e.life = 300.0;
      break;
    default:
      break;
  }
  return Add(std::move(e));
}

EntityId World::SpawnBody(EntityKind kind, double radius, const Vec3d& position) {
  Entity e;
  e.kind = kind;
  e.team = Team::kNeutral;
  e.position = position;
  e.radius = e.hit_radius = radius;
  return Add(std::move(e));
}

Entity* World::Find(EntityId id) {
  if (id == kNoEntity) {
    return nullptr;
  }
  for (Entity& e : entities_) {
    if (e.id == id) {
      return &e;
    }
  }
  for (Entity& e : pending_) {
    if (e.id == id) {
      return &e;
    }
  }
  return nullptr;
}

const Entity* World::Find(EntityId id) const { return const_cast<World*>(this)->Find(id); }

Entity* World::FindKind(EntityKind kind) {
  for (Entity& e : entities_) {
    if (e.kind == kind && e.alive) {
      return &e;
    }
  }
  return nullptr;
}

const Entity* World::FindKind(EntityKind kind) const { return const_cast<World*>(this)->FindKind(kind); }

void World::SetPlayer(EntityId id) {
  player_ = id;
  if (Entity* e = Find(id)) {
    e->is_player = true;
    e->team = Team::kPlayer;
  }
}

std::size_t World::CountShips() const {
  std::size_t n = 0;
  for (const Entity& e : entities_) {
    n += (e.alive && e.kind == EntityKind::kShip) ? 1 : 0;
  }
  return n;
}

void World::Clear() {
  entities_.clear();
  pending_.clear();
  projectiles_.clear();
  events_.clear();
  player_ = kNoEntity;
}

std::vector<Event> World::TakeEvents() {
  std::vector<Event> out;
  out.swap(events_);
  return out;
}

void World::Cull(double distance) {
  const Entity* player = Find(player_);
  if (player == nullptr) {
    return;
  }
  for (Entity& e : entities_) {
    if (e.kind == EntityKind::kStation || e.kind == EntityKind::kPlanet || e.kind == EntityKind::kSun || e.is_player) {
      continue;
    }
    if (Distance(e.position, player->position) > distance) {
      e.alive = false;
      e.silent_removal = true;
    }
  }
}

void World::ApplyDamage(Entity& victim, double amount, EntityId attacker, const Vec3d& hit_point) {
  if (!victim.alive || amount <= 0.0 || victim.blueprint == nullptr) {
    return;
  }
  victim.last_attacker = attacker;
  if (victim.kind == EntityKind::kStation && attacker != kNoEntity && attacker == player_) {
    victim.hostile = true;
  }
  double remaining = amount;
  if (victim.invulnerable) {
    return;
  }
  if (victim.shield > 0.0) {
    const double absorbed = std::min(victim.shield, remaining);
    victim.shield -= absorbed;
    remaining -= absorbed;
    Emit({EventKind::kShieldHit, victim.id, attacker, hit_point, absorbed, victim.blueprint, victim.team, victim.kind});
  }
  if (remaining <= 0.0) {
    return;
  }
  victim.hull -= remaining;
  Emit({EventKind::kHullHit, victim.id, attacker, hit_point, remaining, victim.blueprint, victim.team, victim.kind});

  // The part nearest the hit takes some of it
  const Blueprint& bp = *victim.blueprint;
  if (!bp.subsystems.empty() && bp.scale > 0.0f) {
    const Vec3d local = Rotate(Conjugate(victim.orientation), hit_point - victim.position) * (1.0 / bp.scale);
    int best = -1;
    double best_gap = 1e30;
    for (std::size_t i = 0; i < bp.subsystems.size(); ++i) {
      const double gap = Distance(local, ToVec(bp.subsystems[i].centre)) - bp.subsystems[i].radius;
      if (gap < best_gap) {
        best_gap = gap;
        best = static_cast<int>(i);
      }
    }
    if (best >= 0 && best_gap <= 0.6 * bp.subsystems[static_cast<std::size_t>(best)].radius) {
      double& health = victim.subsystem_health[static_cast<std::size_t>(best)];
      if (health > 0.0) {
        health -= remaining * 0.6;
        if (health <= 0.0) {
          Emit({EventKind::kSubsystemDestroyed, victim.id, attacker, hit_point, static_cast<double>(best),
                victim.blueprint, victim.team, victim.kind});
        }
      }
    }
  }
  if (victim.hull <= 0.0) {
    victim.alive = false;
  }
}

bool World::FireMissile(EntityId shooter_id, EntityId target_id) {
  Entity* shooter = Find(shooter_id);
  const Entity* target = Find(target_id);
  const Blueprint* bp = FindBuiltInBlueprint("missile");
  if (shooter == nullptr || !shooter->alive || shooter->missiles <= 0 || target == nullptr || !target->alive ||
      bp == nullptr) {
    return false;
  }
  --shooter->missiles;
  // Adding the missile can move the entities about in memory, so take what is needed from the shooter first
  const Vec3d forward = Forward(shooter->orientation);
  const Vec3d muzzle =
      shooter->position + forward * (shooter->radius + 12.0) - Up(shooter->orientation) * (shooter->radius * 0.2);
  const Vec3d shooter_velocity = shooter->velocity;
  const Team team = shooter->team;
  const EntityId id = SpawnShip(*bp, team, muzzle, shooter->orientation);
  Entity* missile = Find(id);
  missile->velocity = shooter_velocity + forward * 60.0;
  missile->owner = shooter_id;
  missile->target = target_id;
  missile->controls.throttle = 1.0;
  Emit({EventKind::kMissileLaunched, shooter_id, target_id, missile->position, 0.0, bp, team, EntityKind::kMissile});
  return true;
}

bool World::UseEcm(EntityId id) {
  Entity* ship = Find(id);
  if (ship == nullptr || !ship->alive || !ship->has_ecm || ship->energy < 20.0) {
    return false;
  }
  ship->energy -= 20.0;
  for (Entity& m : entities_) {
    if (m.kind == EntityKind::kMissile && m.alive && m.target == id && Distance(m.position, ship->position) < 6000.0) {
      m.alive = false;
    }
  }
  Emit({EventKind::kEcm, id, kNoEntity, ship->position, 0.0, ship->blueprint, ship->team, ship->kind});
  return true;
}

bool World::DetonateEnergyBomb(EntityId id) {
  Entity* ship = Find(id);
  if (ship == nullptr || !ship->alive) {
    return false;
  }
  for (Entity& e : entities_) {
    if (e.id == id || !IsSolid(e) || e.kind == EntityKind::kStation) {
      continue;
    }
    if (Distance(e.position, ship->position) < 25000.0) {
      e.hull = 0.0;
      e.alive = false;
      e.last_attacker = id;
    }
  }
  Emit({EventKind::kEnergyBomb, id, kNoEntity, ship->position, 0.0, ship->blueprint, ship->team, ship->kind});
  return true;
}

void World::Destroy(Entity& e) {
  Emit({EventKind::kDestroyed, e.id, e.last_attacker, e.position, 0.0, e.blueprint, e.team, e.kind});
  if (e.blueprint == nullptr || e.kind == EntityKind::kMissile) {
    return;
  }
  const Blueprint* canister = FindBuiltInBlueprint("cargo_canister");
  if (canister != nullptr && e.stats().max_cargo_drop > 0) {
    const std::uint32_t count = rng_.Below(e.stats().max_cargo_drop + 1);
    for (std::uint32_t i = 0; i < count; ++i) {
      const EntityId id =
          SpawnShip(*canister, Team::kNeutral, e.position + RandomDirection(rng_) * (e.radius * 0.5), Quat{});
      Entity* c = Find(id);
      c->velocity = e.velocity * 0.5 + RandomDirection(rng_) * rng_.Range(8.0, 30.0);
      c->cargo_item = e.kind == EntityKind::kAsteroid
                          ? static_cast<int>(Commodity::kMinerals)
                          : static_cast<int>(rng_.Below(static_cast<std::uint32_t>(Commodity::kCount)));
    }
  }
  const Blueprint* pod = FindBuiltInBlueprint("escape_pod");
  if (pod != nullptr && e.kind == EntityKind::kShip && e.team != Team::kAlien && rng_.Chance(0.25)) {
    const EntityId id = SpawnShip(*pod, Team::kNeutral, e.position, e.orientation);
    Entity* p = Find(id);
    p->velocity = e.velocity * 0.5 + RandomDirection(rng_) * 25.0;
    p->orientation = LookAlong(p->velocity, {0.0, 1.0, 0.0});
  }
}

void World::Detonate(Entity& missile) {
  constexpr double kBlast = 90.0;
  constexpr double kDamage = 160.0;
  Emit({EventKind::kMissileDetonated, missile.id, missile.target, missile.position, 0.0, missile.blueprint,
        missile.team, EntityKind::kMissile});
  for (Entity& e : entities_) {
    if (!IsSolid(e) || e.id == missile.id || e.kind == EntityKind::kMissile || e.id == missile.owner) {
      continue;
    }
    const double gap = Distance(e.position, missile.position) - e.radius * 0.5;
    if (gap < kBlast) {
      ApplyDamage(e, kDamage * (1.0 - std::max(0.0, gap) / kBlast), missile.owner, missile.position);
    }
  }
  missile.alive = false;
  missile.silent_removal = true;
}

void World::StepMissile(Entity& m, double) {
  if (m.life < 0.0 && m.age > 0.0) {
    Detonate(m);
    return;
  }
  const Entity* target = Find(m.target);
  m.controls.throttle = 1.0;
  m.controls.fire = false;
  if (target == nullptr || !target->alive) {
    m.controls.pitch = m.controls.roll = m.controls.yaw = 0.0;
    return;
  }
  // Fly to where the target will be, at the missile's own speed
  const double speed = std::max(m.Speed(), 300.0);
  SteerToward(m, LeadDirection(m.position, Vec3d{}, target->position, target->velocity, speed));
  const double gap = Distance(m.position, target->position) - target->radius * 0.4;
  if (m.age > 0.4 && gap < 25.0) {
    Detonate(m);
  }
}

void World::StepAi(double dt) {
  for (Entity& e : entities_) {
    if (!e.alive) {
      continue;
    }
    if (e.kind == EntityKind::kMissile) {
      StepMissile(e, dt);
    } else if (e.kind == EntityKind::kShip && !e.is_player && !e.manual && e.blueprint != nullptr) {
      UpdateAi(*this, e, dt);
    }
  }
}

void World::StepMotion(Entity& e, double dt) {
  e.age += dt;
  if (e.life >= 0.0) {
    e.life -= dt;
    if (e.life < 0.0 && e.kind != EntityKind::kMissile) {
      e.alive = false;
      e.silent_removal = true;
      return;
    }
  }
  switch (e.kind) {
    case EntityKind::kPlanet:
    case EntityKind::kSun:
      return;
    case EntityKind::kStation:
      e.orientation = Turned(e.orientation, 0.0, e.spin.y * dt, 0.0);
      return;
    case EntityKind::kAsteroid:
    case EntityKind::kCanister:
      e.position += e.velocity * dt;
      e.orientation = Turned(e.orientation, e.spin.x * dt, e.spin.y * dt, e.spin.z * dt);
      return;
    default:
      break;
  }

  const BlueprintStats& s = e.stats();
  const double engine = e.SubsystemDown("engines") ? 0.3 : 1.0;
  const double max_speed = s.max_speed * engine;
  const double accel = s.acceleration * engine;
  const ShipControls& c = e.controls;
  const double cap[3] = {s.max_pitch_rate, s.max_roll_rate, s.max_yaw_rate};
  const double want[3] = {Clamp(c.pitch, -1.0, 1.0) * cap[0], Clamp(c.roll, -1.0, 1.0) * cap[1],
                          Clamp(c.yaw, -1.0, 1.0) * cap[2]};
  double* rate[3] = {&e.spin.x, &e.spin.y, &e.spin.z};

  if (e.mode == FlightMode::kClassic) {
    // The ship turns at the rate asked for, a little smoothed, and flies where its nose points
    const double k = std::min(1.0, dt / 0.12);
    for (int i = 0; i < 3; ++i) {
      *rate[i] += (want[i] - *rate[i]) * k;
    }
    e.orientation = Turned(e.orientation, e.spin.x * dt, e.spin.y * dt, e.spin.z * dt);
    double speed = Dot(e.velocity, Forward(e.orientation));
    const double target = Clamp(c.throttle, 0.0, 1.0) * max_speed;
    speed += Clamp(target - speed, -2.0 * accel * dt, accel * dt);
    e.velocity = Forward(e.orientation) * speed;
  } else {
    for (int i = 0; i < 3; ++i) {
      const double step = 4.0 * cap[i] * dt;
      if (c.assist) {
        *rate[i] += Clamp(want[i] - *rate[i], -step, step);
      } else if (std::fabs(want[i]) > 0.01) {
        // Without assist the thrusters keep turning the ship for as long as the stick is held, and nothing stops it
        // after
        const double input = want[i] / std::max(cap[i], 1e-9);
        *rate[i] = Clamp(*rate[i] + input * step, -1.5 * cap[i], 1.5 * cap[i]);
      }
    }
    e.orientation = Turned(e.orientation, e.spin.x * dt, e.spin.y * dt, e.spin.z * dt);
    const Vec3d forward = Forward(e.orientation);
    if (c.assist) {
      Vec3d dv = forward * (Clamp(c.throttle, 0.0, 1.0) * max_speed) - e.velocity;
      const double limit = 1.5 * accel * dt;
      if (Length(dv) > limit) {
        dv = Normalized(dv) * limit;
      }
      e.velocity += dv;
    } else {
      e.velocity += forward * (Clamp(c.thrust, -1.0, 1.0) * accel * dt);
      const double speed = e.Speed();
      if (speed > 1.5 * max_speed && speed > 1e-9) {
        e.velocity = e.velocity * (1.5 * max_speed / speed);
      }
    }
  }
  e.position += e.velocity * dt;

  // Energy and shields come back
  const double reactor = e.SubsystemDown("reactor") ? 0.3 : 1.0;
  e.energy = std::min(e.energy_max, e.energy + s.energy_recharge * reactor * dt);
  if (e.shield < e.shield_max && e.energy > 0.25 * e.energy_max) {
    const double regen = std::min(e.shield_max - e.shield, (4.0 + 0.02 * e.shield_max) * reactor * dt);
    e.shield += regen;
    e.energy -= 0.5 * regen;
  }
}

void World::StepWeapons(Entity& e, double dt) {
  e.weapon_cooldown -= dt;
  if (!e.controls.fire || e.weapon_cooldown > 0.0 || e.kind != EntityKind::kShip || e.blueprint == nullptr) {
    return;
  }
  constexpr double kShotEnergy = 3.0;
  if (e.energy < kShotEnergy || e.SubsystemDown("weapons")) {
    return;
  }
  std::vector<const Hardpoint*> lasers;
  for (const Hardpoint& h : e.blueprint->hardpoints) {
    if (h.kind == HardpointKind::kLaser) {
      lasers.push_back(&h);
    }
  }
  if (lasers.empty()) {
    return;
  }
  const Hardpoint& mount = *lasers[e.laser_index++ % lasers.size()];
  const Vec3d origin =
      e.position + Rotate(e.orientation, ToVec(mount.position) * static_cast<double>(e.blueprint->scale));
  const Vec3d direction = Rotate(e.orientation, ToVec(mount.direction));
  Projectile p;
  p.position = origin;
  p.velocity = e.velocity + direction * kBoltSpeed;
  p.owner = e.id;
  p.damage = e.laser_damage;
  p.life = kBoltLife;
  projectiles_.push_back(p);
  e.energy -= kShotEnergy;
  e.weapon_cooldown = 0.25;
  Emit({EventKind::kLaserFired, e.id, kNoEntity, origin, e.laser_damage, e.blueprint, e.team, e.kind});
}

void World::StepProjectiles(double dt) {
  for (Projectile& p : projectiles_) {
    const Vec3d from = p.position;
    const Vec3d to = from + p.velocity * dt;
    p.life -= dt;
    double best = 2.0;
    Entity* hit = nullptr;
    for (Entity& e : entities_) {
      if (!IsSolid(e) || e.id == p.owner) {
        continue;
      }
      const double t = SegmentSphere(from, to, e.position, e.hit_radius);
      if (t >= 0.0 && t < best) {
        best = t;
        hit = &e;
      }
    }
    if (hit != nullptr) {
      ApplyDamage(*hit, p.damage, p.owner, from + (to - from) * best);
      p.life = -1.0;
    } else {
      p.position = to;
    }
  }
  projectiles_.erase(
      std::remove_if(projectiles_.begin(), projectiles_.end(), [](const Projectile& p) { return p.life <= 0.0; }),
      projectiles_.end());
}

void World::StepStation(Entity& e, double dt) {
  if (e.kind != EntityKind::kStation || !e.hostile) {
    return;
  }
  e.launch_timer -= dt;
  if (e.launch_timer > 0.0) {
    return;
  }
  e.launch_timer = 8.0;
  int police = 0;
  for (const Entity& o : entities_) {
    police += (o.alive && o.team == Team::kPolice) ? 1 : 0;
  }
  const Blueprint* viper = FindBuiltInBlueprint("viper");
  if (police >= 4 || viper == nullptr) {
    return;
  }
  const Vec3d axis = DockAxis(e);
  const EntityId id =
      SpawnShip(*viper, Team::kPolice, DockPoint(e) + axis * (DockRadius(e) * 1.3), LookAlong(axis, Up(e.orientation)));
  if (Entity* v = Find(id)) {
    v->velocity = axis * 150.0;
    v->controls.throttle = 1.0;
  }
}

void World::StepContacts(double dt) {
  // Planets and suns
  for (Entity& body : entities_) {
    if (!body.alive || (body.kind != EntityKind::kPlanet && body.kind != EntityKind::kSun)) {
      continue;
    }
    for (Entity& e : entities_) {
      if (!e.alive || e.invulnerable || (!e.IsShipLike() && e.kind != EntityKind::kMissile)) {
        continue;
      }
      const double d = Distance(e.position, body.position);
      if (d < body.radius + e.radius * 0.3) {
        e.hull = 0.0;
        e.alive = false;
        e.last_attacker = kNoEntity;
        Emit({EventKind::kCrashed, e.id, body.id, e.position, 0.0, e.blueprint, e.team, e.kind});
      } else if (body.kind == EntityKind::kSun && d < body.radius * 1.4 && e.kind == EntityKind::kShip) {
        // The heat of the sun eats the shields, then the hull
        const double heat = (1.4 - d / body.radius) / 0.4 * 25.0 * dt;
        const double absorbed = std::min(e.shield, heat);
        e.shield -= absorbed;
        e.hull -= heat - absorbed;
        if (e.hull <= 0.0) {
          e.alive = false;
        }
      }
    }
  }

  // Docking and scooping
  for (Entity& ship : entities_) {
    if (!ship.alive || ship.ghost || ship.kind != EntityKind::kShip) {
      continue;
    }
    if (ship.is_player || ship.team == Team::kTrader) {
      for (Entity& st : entities_) {
        if (st.kind == EntityKind::kStation && CanDock(ship, st, ship.is_player)) {
          Emit({EventKind::kDocked, ship.id, st.id, ship.position, 0.0, ship.blueprint, ship.team, ship.kind});
          if (!ship.is_player) {
            ship.alive = false;
            ship.silent_removal = true;
          }
          break;
        }
      }
    }
    if (ship.alive && ship.can_scoop) {
      for (Entity& c : entities_) {
        if (c.alive && c.kind == EntityKind::kCanister &&
            Distance(c.position, ship.position) < ship.radius + c.radius + 14.0) {
          Emit({EventKind::kScooped, ship.id, c.id, c.position, static_cast<double>(c.cargo_item), c.blueprint, c.team,
                c.kind});
          c.alive = false;
          c.silent_removal = true;
          break;
        }
      }
    }
  }

  // Bumps
  for (auto first = entities_.begin(); first != entities_.end(); ++first) {
    for (auto second = std::next(first); second != entities_.end(); ++second) {
      Entity& a = *first;
      Entity& b = *second;
      if (a.ghost || b.ghost || !IsSolid(a) || !IsSolid(b) || a.kind == EntityKind::kMissile || b.kind == EntityKind::kMissile) {
        continue;
      }
      const bool a_fixed = a.kind == EntityKind::kStation;
      const bool b_fixed = b.kind == EntityKind::kStation;
      if (a_fixed && b_fixed) {
        continue;
      }
      if ((a.kind == EntityKind::kAsteroid || a.kind == EntityKind::kCanister) &&
          (b.kind == EntityKind::kAsteroid || b.kind == EntityKind::kCanister)) {
        continue;
      }
      const Vec3d offset = b.position - a.position;
      const double dist = Length(offset);
      const double reach = CollisionRadius(a) + CollisionRadius(b);
      if (dist >= reach || dist < 1e-6) {
        continue;
      }
      const Vec3d n = offset * (1.0 / dist);
      const double ma = a_fixed ? 1e15 : Mass(a);
      const double mb = b_fixed ? 1e15 : Mass(b);
      const double overlap = reach - dist;
      if (!a_fixed) {
        a.position -= n * (overlap * mb / (ma + mb));
      }
      if (!b_fixed) {
        b.position += n * (overlap * ma / (ma + mb));
      }
      const double closing = Dot(a.velocity - b.velocity, n);
      if (closing <= 0.0) {
        continue;
      }
      const double impulse = 1.3 * closing / (1.0 / ma + 1.0 / mb);
      a.velocity -= n * (impulse / ma);
      b.velocity += n * (impulse / mb);
      const double rel = closing;
      const Vec3d where = a.position + n * CollisionRadius(a);
      if (rel > 5.0) {
        // Running into a station is carelessness, not an attack on it
        ApplyDamage(a, 0.1 * rel * Clamp(mb / ma, 0.1, 5.0), b_fixed ? kNoEntity : b.id, where);
        ApplyDamage(b, 0.1 * rel * Clamp(ma / mb, 0.1, 5.0), a_fixed ? kNoEntity : a.id, where);
        Emit({EventKind::kCollision, a.id, b.id, where, rel, nullptr, Team::kNeutral, EntityKind::kShip});
      }
    }
  }
}

void World::Step(double dt) {
  stepping_ = true;
  time_ += dt;
  StepAi(dt);
  for (Entity& e : entities_) {
    if (!e.alive) {
      continue;
    }
    StepMotion(e, dt);
    if (e.alive) {
      StepWeapons(e, dt);
      StepStation(e, dt);
    }
  }
  StepProjectiles(dt);
  StepContacts(dt);

  for (auto it = entities_.begin(); it != entities_.end();) {
    if (it->alive) {
      ++it;
      continue;
    }
    Entity dead = std::move(*it);
    it = entities_.erase(it);
    if (!dead.silent_removal) {
      Destroy(dead);
    }
  }
  // The new ones join the rest; pointers to the others stay good
  entities_.splice(entities_.end(), pending_);
  stepping_ = false;
}

}  // namespace coriolis
