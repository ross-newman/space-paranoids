#include "coriolis/ai.h"

#include <algorithm>
#include <cmath>

#include "coriolis/steering.h"

namespace coriolis {

namespace {

// name, attacks player, hunts pirates, hunts traders, combat, flees, aggression, accuracy, flee hull, detect range,
// missiles, ECM, drones, laser damage, cruise throttle
const AiProfile kProfiles[] = {
    {"pirate_light", true, false, true, true, false, 0.6, 0.50, 0.25, 20000, false, false, 0, 11, 0.6},
    {"pirate_medium", true, false, true, true, false, 0.75, 0.65, 0.20, 20000, true, false, 0, 14, 0.6},
    {"pirate_heavy", true, false, true, true, false, 0.8, 0.60, 0.15, 20000, true, true, 0, 17, 0.5},
    {"police", false, true, false, true, false, 0.8, 0.75, 0.10, 25000, true, true, 0, 18, 0.5},
    {"bounty_hunter", false, true, false, true, false, 0.8, 0.75, 0.15, 25000, true, true, 0, 19, 0.5},
    {"elite_hunter", true, true, false, true, false, 1.0, 0.90, 0.05, 30000, true, true, 0, 26, 0.6},
    {"trader", false, false, false, false, true, 0.0, 0.0, 0.0, 15000, false, true, 0, 0, 0.7},
    {"trader_armed", false, false, false, true, false, 0.5, 0.50, 0.30, 15000, false, true, 0, 14, 0.6},
    {"alien_mothership", true, true, true, true, false, 0.9, 0.60, 0.0, 30000, false, false, 6, 20, 0.6},
    {"alien_drone", true, true, true, true, false, 1.0, 0.50, 0.0, 20000, false, false, 0, 10, 0.8},
    {"drifter", false, false, false, false, false, 0.0, 0.0, 0.0, 0, false, false, 0, 0, 0.2},
};

double Wobble(const Entity& e, double time, double phase) {
  return std::sin(time * (1.7 + phase) + phase * 11.0 + static_cast<double>(e.id));
}

Vec3d RandomUnit(Random& rng) {
  Vec3d v;
  do {
    v = {rng.Range(-1.0, 1.0), rng.Range(-1.0, 1.0), rng.Range(-1.0, 1.0)};
  } while (Dot(v, v) > 1.0 || Dot(v, v) < 0.01);
  return Normalized(v);
}

/// A direction at right angles to `v`, chosen at random
Vec3d RandomPerpendicular(Random& rng, const Vec3d& v) {
  Vec3d r = Cross(v, RandomUnit(rng));
  if (Length(r) < 1e-6) {
    r = Cross(v, Vec3d{1.0, 0.0, 0.0});
  }
  return Normalized(r);
}

Entity* NearestHostile(World& world, const Entity& self, double range, const AiProfile& p) {
  Entity* best = nullptr;
  double best_score = range;
  for (Entity& o : world.entities()) {
    if (o.id == self.id || !o.alive || o.kind != EntityKind::kShip || !IsHostile(world, self, o)) {
      continue;
    }
    // A pilot goes for the player a little before anything else at the same range
    const double d = Distance(self.position, o.position) * (o.is_player && p.attacks_player ? 0.8 : 1.0);
    if (d < best_score) {
      best_score = d;
      best = &o;
    }
  }
  return best;
}

Vec3d PickWaypoint(World& world, const Entity& e, const AiProfile& p) {
  const Entity* station = world.FindKind(EntityKind::kStation);
  if (station != nullptr && (p.hunts_pirates || p.flees)) {
    return station->position + RandomUnit(world.rng()) * world.rng().Range(2500.0, 8000.0);
  }
  return e.position + RandomUnit(world.rng()) * world.rng().Range(8000.0, 18000.0);
}

bool MissileIncoming(const World& world, const Entity& e, double range) {
  for (const Entity& m : world.entities()) {
    if (m.kind == EntityKind::kMissile && m.alive && m.target == e.id && Distance(m.position, e.position) < range) {
      return true;
    }
  }
  return false;
}

void Cruise(World& world, Entity& e, const AiProfile& p) {
  AiBrain& b = e.ai;
  if (Distance(e.position, b.waypoint) < 1500.0 || Length(b.waypoint) == 0.0) {
    b.waypoint = PickWaypoint(world, e, p);
  }
  SteerToward(e, b.waypoint - e.position);
  e.controls.throttle = p.cruise_throttle;
}

}  // namespace

const AiProfile* FindAiProfile(const std::string& name) {
  for (const AiProfile& p : kProfiles) {
    if (name == p.name) {
      return &p;
    }
  }
  return nullptr;
}

bool IsHostile(const World& world, const Entity& self, const Entity& other) {
  if (other.id == self.id || !other.alive || other.kind != EntityKind::kShip) {
    return false;
  }
  // Everyone hits back at whoever hurt them, unless it was one of their own
  if (self.last_attacker == other.id && other.team != self.team) {
    return true;
  }
  const AiProfile* p = self.ai.profile;
  if (p == nullptr && self.blueprint != nullptr) {
    p = FindAiProfile(self.blueprint->ai_profile);
  }
  if (p == nullptr || other.team == self.team) {
    return false;
  }
  if (self.team == Team::kAlien) {
    return other.team != Team::kAlien;
  }
  if (other.is_player) {
    return p->attacks_player || (p->hunts_pirates && world.player_wanted());
  }
  switch (other.team) {
    case Team::kPirate:
    case Team::kAlien:
      return p->hunts_pirates;
    case Team::kTrader:
      return p->hunts_traders;
    default:
      return false;
  }
}

void UpdateAi(World& world, Entity& e, double dt) {
  AiBrain& b = e.ai;
  if (!b.started) {
    b.started = true;
    b.profile = FindAiProfile(e.blueprint->ai_profile);
    b.aim_wobble = world.rng().Uniform();
    b.think = world.rng().Range(0.0, 0.4);
    if (b.profile != nullptr) {
      e.laser_damage = b.profile->laser_damage > 0.0 ? b.profile->laser_damage : e.laser_damage;
      e.has_ecm = b.profile->uses_ecm;
      b.drones_left = b.profile->drones;
    }
  }
  e.controls.fire = false;
  e.controls.assist = true;
  e.controls.pitch = e.controls.roll = e.controls.yaw = 0.0;
  const AiProfile* p = b.profile;
  if (p == nullptr || e.blueprint->stats.max_speed <= 0.0f) {
    return;
  }
  if (p->detect_range <= 0.0) {
    e.controls.throttle = p->cruise_throttle;
    return;
  }

  b.timer += dt;
  b.think -= dt;
  b.missile_cooldown -= dt;
  b.drone_cooldown -= dt;

  if (p->uses_ecm && e.has_ecm && MissileIncoming(world, e, 2500.0)) {
    world.UseEcm(e.id);
  }

  // Traders fly to the station and run from trouble
  if (p->flees) {
    Entity* threat = nullptr;
    if (b.think <= 0.0) {
      b.think = 0.5;
      double best = p->detect_range;
      for (Entity& o : world.entities()) {
        if (o.kind == EntityKind::kShip && o.alive && IsHostile(world, o, e) &&
            Distance(o.position, e.position) < best) {
          best = Distance(o.position, e.position);
          b.target = o.id;
        }
      }
      if (best >= p->detect_range) {
        b.target = kNoEntity;
      }
    }
    threat = world.Find(b.target);
    if (threat != nullptr && threat->alive) {
      // Run, but run towards the station when there is one: the police may help
      Vec3d away = Normalized(e.position - threat->position);
      const Entity* station = world.FindKind(EntityKind::kStation);
      if (station != nullptr && Distance(e.position, station->position) > 3000.0) {
        away = Normalized(away + Normalized(DockPoint(*station) - e.position) * 0.6);
      }
      SteerToward(e, away);
      e.controls.throttle = 1.0;
      return;
    }
    const Entity* station = world.FindKind(EntityKind::kStation);
    if (station != nullptr && !station->hostile) {
      const Vec3d dock = DockPoint(*station);
      SteerToward(e, dock - e.position);
      e.controls.throttle = Clamp(Distance(e.position, dock) / 3000.0, 0.25, p->cruise_throttle);
    } else {
      Cruise(world, e, *p);
    }
    return;
  }

  // Everyone else
  if (b.think <= 0.0) {
    b.think = 0.35 + world.rng().Uniform() * 0.3;
    const Entity* current = world.Find(b.target);
    if (current == nullptr || !current->alive || !IsHostile(world, e, *current) || b.mode == AiMode::kCruise) {
      const Entity* found = p->combat ? NearestHostile(world, e, p->detect_range, *p) : nullptr;
      b.target = found != nullptr ? found->id : kNoEntity;
    }
  }
  Entity* target = world.Find(b.target);
  if (target != nullptr && !target->alive) {
    target = nullptr;
    b.target = kNoEntity;
  }
  if (target == nullptr && b.mode != AiMode::kCruise && b.mode != AiMode::kFlee) {
    b.mode = AiMode::kCruise;
  }
  if (target != nullptr && b.mode == AiMode::kCruise) {
    b.mode = AiMode::kApproach;
    b.timer = 0.0;
  }
  if (e.hull < p->flee_hull * e.hull_max && b.mode != AiMode::kFlee && p->flee_hull > 0.0) {
    b.mode = AiMode::kFlee;
    b.timer = 0.0;
  }

  const double time = world.time();
  switch (b.mode) {
    case AiMode::kCruise:
      Cruise(world, e, *p);
      break;

    case AiMode::kApproach: {
      const Vec3d aim = LeadDirection(e.position, e.velocity, target->position, target->velocity, kBoltSpeed);
      SteerToward(e, aim);
      e.controls.throttle = 1.0;
      if (Distance(e.position, target->position) < 2200.0) {
        b.mode = AiMode::kAttack;
        b.timer = 0.0;
      }
      break;
    }

    case AiMode::kAttack: {
      // Spawning a missile or drone can move entities in memory: keep copies of what is needed from the target
      const EntityId target_id = target->id;
      const Vec3d target_position = target->position;
      const double distance = Distance(e.position, target_position);
      Vec3d aim = LeadDirection(e.position, e.velocity, target->position, target->velocity, kBoltSpeed);
      // Imperfect aim: a slow wander about the true line
      const double loose = (1.0 - p->accuracy) * 0.12;
      aim = Normalized(aim + Vec3d{Wobble(e, time, b.aim_wobble), Wobble(e, time, b.aim_wobble + 1.3),
                                   Wobble(e, time, b.aim_wobble + 2.9)} *
                                 loose);
      SteerToward(e, aim);
      e.controls.throttle = distance > 700.0 ? 1.0 : 0.6;
      const double off = AngleBetween(Forward(e.orientation), aim);
      // Fire in bursts, not in a steady stream
      const bool burst = Wobble(e, time * 0.7, b.aim_wobble + 5.0) > -0.3;
      if (burst && distance < 2400.0 && off < 0.03 + 0.07 * (1.0 - p->accuracy)) {
        e.controls.fire = true;
      }
      if (p->uses_missiles && e.missiles > 0 && b.missile_cooldown <= 0.0 && distance > 800.0 && distance < 3500.0 &&
          off < 0.15) {
        world.FireMissile(e.id, target_id);
        b.missile_cooldown = 9.0;
      }
      if (b.drones_left > 0 && b.drone_cooldown <= 0.0 && distance < 4000.0) {
        const Blueprint* drone = FindBuiltInBlueprint("thargon");
        if (drone != nullptr) {
          const EntityId id = world.SpawnShip(*drone, Team::kAlien,
                                              e.position + Forward(e.orientation) * (e.radius + 20.0), e.orientation);
          if (Entity* d = world.Find(id)) {
            d->velocity = e.velocity;
            d->ai.target = target_id;
            d->last_attacker = target_id;
          }
          --b.drones_left;
          b.drone_cooldown = 3.0;
        }
      }
      // Break off when close, or after a long run
      if (distance < 150.0 + 250.0 * (1.0 - p->aggression) || b.timer > 4.0 + 4.0 * p->aggression) {
        b.mode = AiMode::kBreak;
        b.timer = 0.0;
        const Vec3d away = Normalized(e.position - target_position);
        b.break_direction = Normalized(away + RandomPerpendicular(world.rng(), away) * 0.9);
      }
      break;
    }

    case AiMode::kBreak: {
      SteerToward(e, b.break_direction);
      e.controls.throttle = 1.0;
      if (b.timer > 1.5 + 1.5 * (1.0 - p->aggression)) {
        b.mode = target != nullptr ? AiMode::kApproach : AiMode::kCruise;
        b.timer = 0.0;
      }
      break;
    }

    case AiMode::kFlee: {
      const Entity* threat = target != nullptr ? target : world.Find(e.last_attacker);
      if (threat == nullptr || !threat->alive) {
        b.mode = AiMode::kCruise;
        break;
      }
      SteerToward(e, e.position - threat->position);
      e.controls.throttle = 1.0;
      // Far enough away: gone
      if (Distance(e.position, threat->position) > 12000.0) {
        e.alive = false;
        e.silent_removal = true;
      }
      break;
    }
  }
}

bool UpdateAutopilot(World& world, Entity& ship, double dt) {
  (void)dt;
  const Entity* station = world.FindKind(EntityKind::kStation);
  if (station == nullptr || station->hostile) {
    return false;
  }
  const Vec3d axis = DockAxis(*station);
  const Vec3d dock = DockPoint(*station);
  const double radius = DockRadius(*station);
  const Vec3d rel = ship.position - dock;
  const double along = Dot(rel, axis);
  const double lateral = Length(rel - axis * along);

  ship.controls.fire = false;
  ship.controls.assist = true;
  const bool in_corridor = along > 0.0 && lateral < std::max(40.0, along * 0.25);
  if (!in_corridor) {
    // Fly to a point out in front of the bay
    const Vec3d stage = dock + axis * (radius * 2.5 + 400.0);
    SteerToward(ship, stage - ship.position);
    ship.controls.throttle = Clamp(Distance(ship.position, stage) / 2500.0, 0.25, 1.0);
  } else {
    // Come in along the axis, slowing, and in the last stretch turn the roof to match the station's. The nose points at
    // the bay, so as the ship settles onto the axis it points straight in.
    if (along < 1500.0) {
      SteerToOrientation(ship, LookAlong(dock - ship.position, Up(station->orientation)));
    } else {
      SteerToward(ship, dock - ship.position);
    }
    ship.controls.throttle = Clamp(along / 1200.0, 0.14, 0.6);
  }
  return true;
}

}  // namespace coriolis
