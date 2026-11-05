#include "coriolis/game.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "coriolis/ai.h"
#include "coriolis/hash.h"
#include "coriolis/missions.h"
#include "coriolis/steering.h"

namespace coriolis {

namespace {

constexpr double kStep = 1.0 / 60.0;
constexpr double kJumpCountdown = 4.0;
/// The last seconds of the countdown zoom into space, and the arrival zooms back out over as long
constexpr double kWarpSeconds = 1.6;
/// Nothing bigger than a rock may be this close if the ship is to jump
constexpr double kMassLockRange = 6000.0;
/// How long the ride out of the bay takes, and how far back in the bay (as a fraction of its depth) the ship starts
constexpr double kLaunchSeconds = 6.5;
constexpr double kBayStart = 0.9;
/// The ride ends this far outside the mouth
constexpr double kLaunchBeyondMouth = 100.0;
/// The tractor beam landing: the ship is drawn in along the bay (approach), lowered to the floor (descend) and held for
/// a moment while the clamps close (settle)
constexpr double kLandApproach = 3.2;
constexpr double kLandDescend = 2.2;
constexpr double kLandSettle = 0.9;
constexpr double kLandSeconds = kLandApproach + kLandDescend + kLandSettle;
/// How far into the bay, as a fraction of its depth, the ship is set down
constexpr double kLandDepth = 0.55;
/// A ship that flies into the mouth of the bay is taken by the tractor beam from this far outside the mouth, and from
/// any angle, any roll and any speed: it must never be left to crash into the walls from inside. The opening it has to
/// be flying into widens like a funnel with the distance.
constexpr double kCaptureReach = 250.0;
constexpr double kCaptureFunnel = 0.6;

double SmoothStep01(double t) {
  t = Clamp(t, 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

/// A blend between two orientations, along the short way round
Quat Blend(const Quat& a, const Quat& b, double t) {
  const double dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
  const double k = dot < 0.0 ? -1.0 : 1.0;
  return Normalized(Quat{a.w * (1.0 - t) + k * b.w * t, a.x * (1.0 - t) + k * b.x * t, a.y * (1.0 - t) + k * b.y * t,
                         a.z * (1.0 - t) + k * b.z * t});
}

Vec3d RandomUnit(Random& rng) {
  Vec3d v;
  do {
    v = {rng.Range(-1.0, 1.0), rng.Range(-1.0, 1.0), rng.Range(-1.0, 1.0)};
  } while (Dot(v, v) > 1.0 || Dot(v, v) < 0.01);
  return Normalized(v);
}

/// Each system has one of the three stations, picked from its seed so that it is always the same one there
const Blueprint& StationFor(const StarSystem& s) {
  static const char* const kStations[] = {"coriolis", "dodo", "wheel_station"};
  return *FindBuiltInBlueprint(kStations[Hash32(s.seed ^ 0x57A71011U) % 3U]);
}

/// A commander who has not yet earned or spent anything is on the first flight
bool IsUntouched(const Commander& c) { return c.kills == 0.0 && c.credits == Commander{}.credits; }

}  // namespace

Game::Game(const Commander& commander, std::uint64_t seed)
    : commander_(commander), galaxy_(commander.galaxy), market_(), world_(seed) {
  last_rank_ = RankName(commander_.kills);
  first_flight_ = IsUntouched(commander_);
  RefreshMarket();
}

Entity* Game::player() { return world_.Find(world_.player()); }

void Game::RefreshMarket() { market_ = Market::ForSystem(system(), commander_.market_mood); }

void Game::NewGame(const Commander& commander) {
  commander_ = commander;
  galaxy_ = Galaxy(commander.galaxy);
  world_.Clear();
  mode_ = GameMode::kDocked;
  launch_clock_ = -1.0;
  landing_clock_ = -1.0;
  autopilot_ = false;
  throttle_ = 0.0;
  target_ = lock_ = kNoEntity;
  selected_system_ = -1;
  jump_timer_ = -1.0;
  arrival_clock_ = -1.0;
  station_angered_ = false;
  last_rank_ = RankName(commander_.kills);
  first_flight_ = IsUntouched(commander_);
  death_cause_.clear();
  crash_cause_.clear();
  RefreshMarket();
}

std::vector<std::string> Game::TakeMessages() {
  std::vector<std::string> out;
  out.swap(messages_);
  return out;
}

std::vector<Event> Game::TakeEvents() {
  std::vector<Event> out;
  out.swap(frame_events_);
  return out;
}

bool Game::Buy(Commodity item, int quantity) {
  return mode_ == GameMode::kDocked && BuyCargo(commander_, market_, item, quantity);
}

bool Game::Sell(Commodity item, int quantity) {
  return mode_ == GameMode::kDocked && SellCargo(commander_, market_, item, quantity);
}

bool Game::BuyItem(Item item) {
  return mode_ == GameMode::kDocked && coriolis::BuyItem(commander_, item, system().tech_level);
}

bool Game::BuyShip(int index) {
  return mode_ == GameMode::kDocked && coriolis::BuyShip(commander_, index, system().tech_level);
}

bool Game::MissionOnOffer(Mission* out) const {
  return commander_.mission.kind == MissionKind::kNone && MissionOffered(commander_, galaxy_, out);
}

bool Game::AcceptMission() {
  Mission offer;
  if (mode_ != GameMode::kDocked || !MissionOnOffer(&offer)) {
    return false;
  }
  commander_.mission = offer;
  Say("Job taken: " + MissionStatus(offer, galaxy_));
  return true;
}

bool Game::AbandonMission() {
  if (mode_ != GameMode::kDocked || commander_.mission.kind == MissionKind::kNone) {
    return false;
  }
  commander_.mission = Mission{};
  Say("Job abandoned");
  return true;
}

void Game::SpawnPlayer(const Vec3d& position, const Quat& orientation, double speed) {
  const Blueprint* bp = FindBuiltInBlueprint(commander_.ship);
  if (bp == nullptr) {
    bp = FindBuiltInBlueprint("cobra_mk3_trader");
  }
  const EntityId id = world_.SpawnShip(*bp, Team::kPlayer, position, orientation);
  world_.SetPlayer(id);
  Entity* e = world_.Find(id);
  e->mode = flight_mode_;
  e->missiles = commander_.missiles;
  e->has_ecm = commander_.equipment.ecm;
  e->laser_damage = 18.0 + 8.0 * commander_.equipment.laser_level;
  e->velocity = Forward(orientation) * speed;
  throttle_ = Clamp(speed / std::max(1.0, static_cast<double>(bp->stats.max_speed)), 0.0, 1.0);
  e->controls.throttle = throttle_;
}

void Game::BuildSystemWorld(bool arriving, bool ride_out) {
  const StarSystem& s = system();
  world_.Clear();
  launch_clock_ = -1.0;
  landing_clock_ = -1.0;
  arrival_clock_ = -1.0;
  target_ = lock_ = kNoEntity;
  station_angered_ = false;
  Random r(static_cast<std::uint64_t>(s.seed) ^ 0x5151515151ULL);

  const double planet_radius = s.radius_km * 3.0;
  const Vec3d outward = RandomUnit(r);
  const Vec3d station_position = outward * (planet_radius + 9000.0);
  Vec3d sun_direction = RandomUnit(r);
  if (Dot(sun_direction, outward) > 0.8) {
    sun_direction = -sun_direction;
  }
  world_.SpawnBody(EntityKind::kPlanet, planet_radius, Vec3d{});
  world_.SpawnBody(EntityKind::kSun, 45000.0, sun_direction * 170000.0);
  const EntityId station_id =
      world_.SpawnShip(StationFor(s), Team::kStation, station_position, LookAlong(outward, {0.0, 1.0, 0.0}));
  // Copy what is needed: adding the player may move the entities about

  if (arriving) {
    // Come out of hyperspace well in front of the station's bay, heading for it
    const Vec3d side = Normalized(Cross(outward, Vec3d{0.0, 1.0, 0.0}));
    const Vec3d start = station_position + outward * 40000.0 + side * r.Range(-6000.0, 6000.0);
    SpawnPlayer(start, LookAlong(station_position - start, {0.0, 1.0, 0.0}), 150.0);
  } else if (ride_out) {
    // Start at the back of the bay, nose to the mouth, and ride out (see AdvanceLaunch)
    const Entity& st = *world_.Find(station_id);
    const Bay bay = BayOf(st);
    const Quat facing = st.orientation;
    SpawnPlayer(bay.mouth - bay.axis * (bay.depth * kBayStart), facing, 0.0);
    if (Entity* me = player()) {
      me->ghost = true;
      me->invulnerable = true;
    }
    launch_clock_ = 0.0;
  } else {
    const Entity& st = *world_.Find(station_id);
    const Bay bay = BayOf(st);
    const Quat facing = st.orientation;
    SpawnPlayer(bay.mouth + bay.axis * (DockRadius(st) * 1.3), facing, 180.0);
  }
  spawner_.PopulateRocks(world_, s);
  for (int i = 0; i < 3; ++i) {
    spawner_.Update(world_, s, 12.0, commander_.kills, false, first_flight_);
  }
  world_.TakeEvents();
}

bool Game::Launch(bool ride_out) {
  if (mode_ != GameMode::kDocked) {
    return false;
  }
  mode_ = GameMode::kFlight;
  autopilot_ = false;
  assist_ = true;
  BuildSystemWorld(false, ride_out);
  Say("Launching from " + system().name);
  return true;
}

double Game::launch_progress() const {
  return launch_clock_ < 0.0 ? 1.0 : Clamp(launch_clock_ / kLaunchSeconds, 0.0, 1.0);
}

void Game::AdvanceLaunch(double dt) {
  if (launch_clock_ < 0.0) {
    return;
  }
  Entity* me = player();
  const Entity* station = world_.FindKind(EntityKind::kStation);
  if (me == nullptr || station == nullptr) {
    launch_clock_ = -1.0;
    return;
  }
  // The ship is carried out along the axis of the bay, from rest to a good speed, turning with the station
  const Bay bay = BayOf(*station);
  const double travel = bay.depth * kBayStart + kLaunchBeyondMouth;
  const double accel = 2.0 * travel / (kLaunchSeconds * kLaunchSeconds);
  launch_clock_ += dt;
  const double t = std::min(launch_clock_, kLaunchSeconds);
  me->position = bay.mouth - bay.axis * (bay.depth * kBayStart) + bay.axis * (0.5 * accel * t * t);
  me->velocity = bay.axis * (accel * t);
  me->orientation = station->orientation;
  if (launch_clock_ >= kLaunchSeconds) {
    // Out: the pilot has the ship
    launch_clock_ = -1.0;
    me->ghost = false;
    me->invulnerable = false;
    throttle_ = Clamp(Length(me->velocity) / std::max(1.0, static_cast<double>(me->blueprint->stats.max_speed)), 0.0, 1.0);
    me->controls.throttle = throttle_;
  }
}

void Game::StartLanding() {
  Entity* me = player();
  const Entity* station = world_.FindKind(EntityKind::kStation);
  if (me == nullptr || station == nullptr) {
    Dock();
    return;
  }
  // The beam takes the ship: the pilot can no longer steer, and nothing can bump or hurt it on the way in
  landing_clock_ = 0.0;
  landing_from_ = me->position;
  landing_velocity_ = me->velocity;
  landing_facing_ = me->orientation;
  me->ghost = true;
  me->invulnerable = true;
  autopilot_ = false;
  jump_timer_ = -1.0;
  target_ = lock_ = kNoEntity;
  Say("Tractor beam engaged: landing");
}

double Game::landing_progress() const {
  return landing_clock_ < 0.0 ? 1.0 : Clamp(landing_clock_ / kLandSeconds, 0.0, 1.0);
}

void Game::AdvanceLanding(double dt) {
  if (landing_clock_ < 0.0) {
    return;
  }
  Entity* me = player();
  const Entity* station = world_.FindKind(EntityKind::kStation);
  if (me == nullptr || station == nullptr) {
    landing_clock_ = -1.0;
    return;
  }
  const Bay bay = BayOf(*station);
  const Vec3d hover = bay.mouth - bay.axis * (bay.depth * kLandDepth);
  // Down on the floor, but not through it however big the ship
  const double floor_y = -bay.half_height + std::min(0.5 * me->radius, 0.45 * bay.half_height);
  const Quat settled = LookAlong(-bay.axis, bay.up);

  landing_clock_ += dt;
  const double t = landing_clock_;
  Vec3d position;
  Quat facing = settled;
  if (t < kLandApproach) {
    // Drawn in along the axis, easing from its own speed to a stop over the middle of the bay
    const double u = t / kLandApproach;
    const Vec3d chord = hover - landing_from_;
    Vec3d tangent = landing_velocity_ * kLandApproach;
    const double limit = 0.8 * Length(chord);
    if (Length(tangent) > limit && Length(tangent) > 1e-9) {
      tangent = tangent * (limit / Length(tangent));
    }
    const double u2 = u * u;
    const double u3 = u2 * u;
    position = landing_from_ * (2.0 * u3 - 3.0 * u2 + 1.0) + tangent * (u3 - 2.0 * u2 + u) +
               hover * (-2.0 * u3 + 3.0 * u2);
    facing = Blend(landing_facing_, settled, SmoothStep01(u));
  } else {
    const double s = SmoothStep01((t - kLandApproach) / kLandDescend);
    position = hover + bay.up * (floor_y * s);
  }
  const Vec3d before = me->position;
  me->position = position;
  me->velocity = (position - before) * (1.0 / dt);
  me->orientation = facing;
  me->spin = Vec3d{};
  if (landing_clock_ >= kLandSeconds) {
    // Down: the station takes over
    Dock();
  }
}

void Game::Dock() {
  launch_clock_ = -1.0;
  landing_clock_ = -1.0;
  crash_cause_.clear();
  mode_ = GameMode::kDocked;
  world_.Clear();
  autopilot_ = false;
  jump_timer_ = -1.0;
  arrival_clock_ = -1.0;
  first_flight_ = false;
  target_ = lock_ = kNoEntity;
  ++commander_.market_mood;
  RefreshMarket();
  Say("Docked at " + system().name);
  if (commander_.mission.kind != MissionKind::kNone && MissionComplete(commander_.mission, commander_.system)) {
    Say(MissionDone(commander_.mission));
    PayMission(commander_);
  }
}

bool Game::Eject() {
  if (!commander_.equipment.escape_pod) {
    return false;
  }
  // The pod lands at the station. The ship, its cargo and its fittings are lost, and the pilot is let off.
  const std::int64_t money = commander_.credits;
  const double kills = commander_.kills;
  Commander fresh;
  fresh.name = commander_.name;
  fresh.credits = money;
  fresh.kills = kills;
  fresh.galaxy = commander_.galaxy;
  fresh.system = commander_.system;
  fresh.market_mood = commander_.market_mood;
  fresh.mission = commander_.mission;
  fresh.story_step = commander_.story_step;
  commander_ = fresh;
  Dock();
  Say("You ejected. The ship is lost, but the escape pod got you to the station.");
  return true;
}

void Game::OnKill(const Event& event) {
  if (event.blueprint == nullptr || event.entity_kind == EntityKind::kMissile) {
    return;
  }
  const BlueprintStats& s = event.blueprint->stats;
  commander_.kills += s.kill_points;
  std::string text = "Destroyed " + event.blueprint->name;
  if (event.team == Team::kPirate || event.team == Team::kAlien) {
    commander_.credits += static_cast<std::int64_t>(std::llround(s.bounty * 10.0));
    if (s.bounty > 0.0f) {
      text += ", bounty " + std::to_string(static_cast<int>(s.bounty)) + " Cr";
    }
  } else if (event.team == Team::kPolice) {
    commander_.legal_status = std::min(255, commander_.legal_status + 64);
    text += ": you are now a fugitive";
  } else if (event.team == Team::kHunter) {
    commander_.legal_status = std::min(255, commander_.legal_status + 20);
  } else if (event.team == Team::kTrader) {
    commander_.legal_status = std::min(255, commander_.legal_status + 5);
  }
  Say(text);
  Mission& job = commander_.mission;
  if (event.team == Team::kPirate && job.kind == MissionKind::kHunt && job.progress < job.count) {
    ++job.progress;
    Say(MissionStatus(job, galaxy_));
  }
  const std::string rank = RankName(commander_.kills);
  if (rank != last_rank_) {
    last_rank_ = rank;
    Say("Right on, Commander! Your rank is now " + rank);
  }
}

void Game::ProcessEvents() {
  const EntityId me = world_.player();
  for (const Event& ev : world_.TakeEvents()) {
    frame_events_.push_back(ev);
    switch (ev.kind) {
      case EventKind::kDestroyed:
        if (ev.a == me && me != kNoEntity) {
          if (!Eject()) {
            // Work out what did it before the world is cleared
            const Entity* killer = world_.Find(ev.b);
            if (!crash_cause_.empty()) {
              death_cause_ = crash_cause_;
            } else if (killer != nullptr && killer->kind == EntityKind::kMissile) {
              death_cause_ = "BLOWN UP BY A MISSILE";
            } else if (killer != nullptr && killer->blueprint != nullptr) {
              death_cause_ = "SHOT DOWN BY " + killer->blueprint->name;
            } else {
              death_cause_ = "SHIP DESTROYED";
            }
            mode_ = GameMode::kDead;
            world_.Clear();
            Say("You are dead.");
          }
          return;
        }
        if (ev.b == me && me != kNoEntity) {
          OnKill(ev);
        }
        break;
      case EventKind::kCrashed:
        if (ev.a == me && me != kNoEntity) {
          Say("You crashed.");
          const Entity* body = world_.Find(ev.b);
          crash_cause_ = body != nullptr && body->kind == EntityKind::kSun ? "BURNED UP IN THE SUN"
                                                                          : "CRASHED INTO THE PLANET";
        }
        break;
      case EventKind::kDocked:
        if (ev.a == me && me != kNoEntity && !landing()) {
          StartLanding();
        }
        break;
      case EventKind::kScooped:
        if (ev.a == me && me != kNoEntity) {
          if (CargoFree(commander_) > 0) {
            const std::size_t item =
                static_cast<std::size_t>(Clamp(ev.amount, 0.0, static_cast<double>(kCommodityCount - 1)));
            ++commander_.cargo[item];
            Say(std::string("Scooped ") + CommodityData(static_cast<Commodity>(item)).name);
          }
        }
        break;
      default:
        break;
    }
  }
}

bool Game::MassLocked() const {
  const Entity* me = player();
  if (me == nullptr) {
    return true;
  }
  for (const Entity& e : world_.entities()) {
    if (e.id == me->id || !e.alive) {
      continue;
    }
    if (e.kind == EntityKind::kStation && Distance(e.position, me->position) < kSafeZone) {
      return true;
    }
    if ((e.kind == EntityKind::kShip || e.kind == EntityKind::kMissile) &&
        Distance(e.position, me->position) < kMassLockRange) {
      return true;
    }
    if ((e.kind == EntityKind::kPlanet || e.kind == EntityKind::kSun) &&
        Distance(e.position, me->position) < e.radius * 1.5) {
      return true;
    }
  }
  return false;
}

bool Game::StartJump() {
  if (mode_ != GameMode::kFlight || player() == nullptr) {
    return false;
  }
  if (selected_system_ < 0 || selected_system_ >= kSystemsPerGalaxy || selected_system_ == commander_.system) {
    Say("No destination selected");
    return false;
  }
  const double distance = galaxy_.DistanceLy(commander_.system, selected_system_);
  if (distance > commander_.fuel + 1e-9) {
    Say("Not enough fuel");
    return false;
  }
  if (MassLocked()) {
    Say("Mass locked: get clear of the station and other ships");
    return false;
  }
  jump_timer_ = kJumpCountdown;
  galactic_jump_ = false;
  Say("Hyperspace in " + std::to_string(static_cast<int>(kJumpCountdown)) + " seconds");
  return true;
}

bool Game::StartGalacticJump() {
  if (mode_ != GameMode::kFlight || player() == nullptr) {
    return false;
  }
  if (!commander_.equipment.galactic_hyperdrive) {
    Say("No galactic hyperdrive fitted");
    return false;
  }
  if (MassLocked()) {
    Say("Mass locked: get clear of the station and other ships");
    return false;
  }
  jump_timer_ = kJumpCountdown;
  galactic_jump_ = true;
  Say("Galactic hyperspace in " + std::to_string(static_cast<int>(kJumpCountdown)) + " seconds");
  return true;
}

void Game::Jump() {
  jump_timer_ = -1.0;
  if (MassLocked()) {
    Say("Jump aborted: mass locked");
    return;
  }
  if (galactic_jump_) {
    galactic_jump_ = false;
    if (!commander_.equipment.galactic_hyperdrive) {
      Say("Jump aborted: no galactic hyperdrive");
      return;
    }
    // The next galaxy lies over the same part of the chart, so arrive at the system nearest where the ship was
    const int x = system().x;
    const int y = system().y;
    commander_.equipment.galactic_hyperdrive = false;
    commander_.galaxy = (commander_.galaxy + 1) % kGalaxyCount;
    galaxy_ = Galaxy(commander_.galaxy);
    commander_.system = galaxy_.Nearest(x, y);
    selected_system_ = -1;
    commander_.legal_status /= 2;
    ++commander_.market_mood;
    RefreshMarket();
    autopilot_ = false;
    BuildSystemWorld(true);
    arrival_clock_ = kWarpSeconds;
    Say("Arrived in galaxy " + std::to_string(commander_.galaxy + 1) + " at " + system().name);
    return;
  }
  const double distance = galaxy_.DistanceLy(commander_.system, selected_system_);
  if (distance > commander_.fuel + 1e-9) {
    Say("Jump aborted: not enough fuel");
    return;
  }
  commander_.fuel = std::max(0.0, commander_.fuel - distance);
  commander_.system = selected_system_;
  selected_system_ = -1;
  // Time in hyperspace heals the record a little
  commander_.legal_status /= 2;
  ++commander_.market_mood;
  RefreshMarket();
  autopilot_ = false;
  BuildSystemWorld(true);
  arrival_clock_ = kWarpSeconds;
  Say("Arrived at " + system().name);
}

double Game::hyperspace_warp() const {
  if (jump_timer_ > 0.0 && jump_timer_ < kWarpSeconds) {
    return SmoothStep01(1.0 - jump_timer_ / kWarpSeconds);
  }
  if (arrival_clock_ > 0.0) {
    return SmoothStep01(arrival_clock_ / kWarpSeconds);
  }
  return 0.0;
}

void Game::CycleTarget(int step) {
  const Entity* me = player();
  if (me == nullptr) {
    return;
  }
  std::vector<const Entity*> ships;
  for (const Entity& e : world_.entities()) {
    if (e.alive && e.kind == EntityKind::kShip && e.id != me->id && Distance(e.position, me->position) < 25000.0) {
      ships.push_back(&e);
    }
  }
  if (ships.empty()) {
    target_ = kNoEntity;
    return;
  }
  std::sort(ships.begin(), ships.end(), [me](const Entity* a, const Entity* b) {
    return Distance(a->position, me->position) < Distance(b->position, me->position);
  });
  std::size_t next = step < 0 ? ships.size() - 1 : 0;
  for (std::size_t i = 0; i < ships.size(); ++i) {
    if (ships[i]->id == target_) {
      next = step < 0 ? (i + ships.size() - 1) % ships.size() : (i + 1) % ships.size();
    }
  }
  target_ = ships[next]->id;
}

void Game::LockMissile() {
  const Entity* me = player();
  if (me == nullptr) {
    return;
  }
  const Entity* t = world_.Find(target_);
  if (t == nullptr || !t->alive) {
    // Nothing chosen: take the nearest ship in front
    double best = 25000.0;
    t = nullptr;
    for (const Entity& e : world_.entities()) {
      if (!e.alive || e.kind != EntityKind::kShip || e.id == me->id) {
        continue;
      }
      const double d = Distance(e.position, me->position);
      if (d < best && AngleBetween(Forward(me->orientation), e.position - me->position) < 0.4) {
        best = d;
        t = &e;
      }
    }
  }
  if (t == nullptr) {
    lock_ = kNoEntity;
    Say("No target");
    return;
  }
  target_ = lock_ = t->id;
  Say("Missile locked on " + t->blueprint->name);
}

void Game::ApplyInput(const FlightInput& input, double dt) {
  Entity* me = player();
  if (me == nullptr) {
    return;
  }
  if (input.Pressed(Action::kToggleFlightAssist)) {
    if (flight_mode_ == FlightMode::kNewtonian) {
      assist_ = !assist_;
      Say(assist_ ? "Flight assist on" : "Flight assist off");
    } else {
      Say("Flight assist only matters in Newtonian flight");
    }
  }
  if (input.Pressed(Action::kDockingComputer)) {
    if (commander_.equipment.docking_computer) {
      autopilot_ = !autopilot_;
      Say(autopilot_ ? "Docking computer on" : "Docking computer off");
    } else {
      Say("No docking computer fitted");
    }
  }
  if (input.Pressed(Action::kNextTarget)) {
    CycleTarget(1);
  }
  if (input.Pressed(Action::kPreviousTarget)) {
    CycleTarget(-1);
  }
  // Throttle jumps to the end of its range at a key press, for a quick burst or to stop at once
  if (input.Pressed(Action::kThrottleFull)) {
    throttle_ = 1.0;
  }
  if (input.Pressed(Action::kThrottleZero)) {
    throttle_ = 0.0;
  }
  if (input.Pressed(Action::kTargetMissile)) {
    LockMissile();
  }
  if (input.Pressed(Action::kFireMissile)) {
    const Entity* t = world_.Find(lock_);
    if (t != nullptr && t->alive && me->missiles > 0) {
      const EntityId shooter = me->id;
      world_.FireMissile(shooter, lock_);
      // The missile may have moved the entities in memory: find the ship again
      if (const Entity* again = world_.Find(shooter)) {
        commander_.missiles = std::max(0, again->missiles);
      }
      lock_ = kNoEntity;
    } else {
      Say(me->missiles > 0 ? "No missile lock" : "No missiles");
    }
    // Firing may have moved the entities in memory
    me = player();
    if (me == nullptr) {
      return;
    }
  }
  if (input.Pressed(Action::kEcm)) {
    if (!world_.UseEcm(me->id)) {
      Say(commander_.equipment.ecm ? "ECM needs more energy" : "No ECM fitted");
    }
  }
  if (input.Pressed(Action::kEnergyBomb)) {
    if (commander_.equipment.energy_bomb) {
      commander_.equipment.energy_bomb = false;
      world_.DetonateEnergyBomb(me->id);
    } else {
      Say("No energy bomb fitted");
    }
  }
  if (input.Pressed(Action::kEscapePod)) {
    if (Eject()) {
      return;
    }
    Say("No escape pod fitted");
  }
  if (input.Pressed(Action::kHyperspace)) {
    if (jump_timer_ > 0.0) {
      CancelJump();
      Say("Hyperspace cancelled");
    } else {
      StartJump();
    }
  }
  if (input.Pressed(Action::kGalacticHyperspace)) {
    if (jump_timer_ > 0.0) {
      CancelJump();
      Say("Hyperspace cancelled");
    } else {
      StartGalacticJump();
    }
  }

  me->mode = flight_mode_;
  ShipControls& c = me->controls;
  if (autopilot_) {
    UpdateAutopilot(world_, *me, dt);
    return;
  }
  c.pitch = input.pitch;
  c.roll = input.roll;
  c.yaw = input.yaw;
  c.assist = assist_;
  throttle_ = Clamp(throttle_ + input.throttle * 0.6 * dt, 0.0, 1.0);
  c.throttle = throttle_;
  c.thrust = input.throttle;
  c.fire = input.Held(Action::kFireLasers);
}

void Game::CaptureInBay() {
  if (launching() || landing()) {
    return;
  }
  const Entity* me = player();
  const Entity* station = world_.FindKind(EntityKind::kStation);
  if (me == nullptr || me->ghost || station == nullptr || !station->alive || station->hostile) {
    return;
  }
  const Bay bay = BayOf(*station);
  const Vec3d rel = me->position - bay.mouth;
  const double along = Dot(rel, bay.axis);
  if (along > kCaptureReach || along < -bay.depth) {
    return;
  }
  const double funnel = 1.0 + kCaptureFunnel * Clamp(along / kCaptureReach, 0.0, 1.0);
  if (std::fabs(Dot(rel, bay.right)) > bay.half_width * funnel || std::fabs(Dot(rel, bay.up)) > bay.half_height * funnel) {
    return;
  }
  // Only a ship going in: one that has just launched, or is backing out, is left alone
  if (Dot(me->velocity - station->velocity, bay.axis) > -1.0) {
    return;
  }
  StartLanding();
}

void Game::Step(const FlightInput& input, double dt) {
  CaptureInBay();
  // The controls are locked while the ship rides out of the bay and while the tractor beam lands it
  ApplyInput(launching() || landing() ? FlightInput{} : input, dt);
  if (mode_ != GameMode::kFlight) {
    return;
  }
  Entity* me = player();
  if (me != nullptr) {
    me->can_scoop = commander_.equipment.fuel_scoop && CargoFree(commander_) > 0;
    // Scooping fuel from the sun, close in
    if (commander_.equipment.fuel_scoop) {
      for (const Entity& e : world_.entities()) {
        if (e.kind == EntityKind::kSun) {
          const double d = Distance(e.position, me->position);
          if (d < e.radius * 1.35 && d > e.radius) {
            commander_.fuel = std::min(kMaxFuel, commander_.fuel + 0.2 * dt);
          }
        }
      }
    }
  }
  // The police are after a fugitive, and after anyone who has attacked the station
  const Entity* station = world_.FindKind(EntityKind::kStation);
  if (station != nullptr && station->hostile && !station_angered_) {
    station_angered_ = true;
    commander_.legal_status = std::min(255, commander_.legal_status + 20);
    Say("You attacked the station: the police are coming");
  }
  world_.set_player_wanted(commander_.legal_status >= 50 || (station != nullptr && station->hostile));

  world_.Step(dt);
  AdvanceLaunch(dt);
  AdvanceLanding(dt);
  ProcessEvents();
  if (mode_ != GameMode::kFlight) {
    return;
  }
  if (Entity* now = player()) {
    commander_.missiles = std::max(0, now->missiles);
    spawner_.Update(world_, system(), dt, commander_.kills, world_.player_wanted(), first_flight_);
    world_.Cull(90000.0);
  }
  if (arrival_clock_ > 0.0) {
    arrival_clock_ -= dt;
  }
  if (jump_timer_ > 0.0) {
    jump_timer_ -= dt;
    if (jump_timer_ <= 0.0) {
      Jump();
    }
  }
  const Entity* t = world_.Find(target_);
  if (t == nullptr || !t->alive) {
    target_ = kNoEntity;
  }
  const Entity* l = world_.Find(lock_);
  if (l == nullptr || !l->alive) {
    lock_ = kNoEntity;
  }
}

void Game::Update(const FlightInput& input, double seconds) {
  if (mode_ != GameMode::kFlight) {
    return;
  }
  accumulator_ += Clamp(seconds, 0.0, 0.25);
  // A press waits for the next step if this frame is too short to hold one
  pending_ |= input.pressed;
  FlightInput in = input;
  while (accumulator_ >= kStep && mode_ == GameMode::kFlight) {
    in.pressed = pending_;
    pending_.reset();
    Step(in, kStep);
    accumulator_ -= kStep;
    in.released.reset();
  }
}

}  // namespace coriolis
