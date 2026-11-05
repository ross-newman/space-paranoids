#include "coriolis/bot.h"

#include <algorithm>
#include <cmath>

#include "coriolis/steering.h"

namespace coriolis {

namespace {

/// Controls that turn the ship towards a direction, found by letting the steering code work on a copy of the ship
void Aim(const Entity& me, const Vec3d& direction, FlightInput* in) {
  Entity probe = me;
  SteerToward(probe, direction);
  in->pitch = static_cast<float>(probe.controls.pitch);
  in->roll = static_cast<float>(probe.controls.roll);
  in->yaw = static_cast<float>(probe.controls.yaw);
}

void SetThrottle(const Game& game, double wanted, FlightInput* in) {
  in->throttle = static_cast<float>(Clamp((wanted - game.throttle()) * 3.0, -1.0, 1.0));
}

}  // namespace

FlightInput BotPilot::Fly(const Game& game) {
  FlightInput in;
  const Entity* me = game.player();
  if (me == nullptr) {
    return in;
  }
  if (home_system_ != game.commander().system) {
    if (home_system_ >= 0) {
      ++jumps_;
      want_jump_ = false;
    }
    home_system_ = game.commander().system;
  }

  // Keep clear of the planet and the sun: climb away from them whatever else is going on
  for (const Entity& body : game.world().entities()) {
    if (body.kind != EntityKind::kPlanet && body.kind != EntityKind::kSun) {
      continue;
    }
    const double clearance = Distance(me->position, body.position) - body.radius;
    if (clearance < (body.kind == EntityKind::kPlanet ? 5000.0 : 0.4 * body.radius)) {
      if (game.autopilot()) {
        in.pressed.set(static_cast<std::size_t>(Action::kDockingComputer));
      }
      Aim(*me, me->position - body.position, &in);
      SetThrottle(game, 1.0, &in);
      return in;
    }
  }

  // Keep clear of rocks and the station's body, which hurt at speed
  for (const Entity& rock : game.world().entities()) {
    if (!rock.alive || (rock.kind != EntityKind::kAsteroid && rock.kind != EntityKind::kStation)) {
      continue;
    }
    const Vec3d to_rock = rock.position - me->position;
    const double distance = Length(to_rock) - rock.radius;
    const double closing = Dot(me->velocity - rock.velocity, Normalized(to_rock));
    const bool dock_run = rock.kind == EntityKind::kStation && game.autopilot();
    if (!dock_run && closing > 0.0 && distance < 1.5 * closing + 300.0 && distance < 3000.0) {
      Aim(*me, me->position - rock.position + Normalized(Cross(to_rock, Up(me->orientation))) * (rock.radius * 2.0),
          &in);
      SetThrottle(game, 0.5, &in);
      return in;
    }
  }

  // Badly hurt: let the docking computer take the ship home to the police and the station
  if (me->hull < 0.5 * me->hull_max && game.commander().equipment.docking_computer &&
      game.world().FindKind(EntityKind::kStation) != nullptr) {
    if (!game.autopilot()) {
      in.pressed.set(static_cast<std::size_t>(Action::kDockingComputer));
    }
    return in;
  }

  // Fight whatever is after us
  const Entity* foe = nullptr;
  double best = 6000.0;
  for (const Entity& e : game.world().entities()) {
    if (e.alive && e.kind == EntityKind::kShip && (e.team == Team::kPirate || e.team == Team::kAlien)) {
      const double d = Distance(e.position, me->position);
      if (d < best) {
        best = d;
        foe = &e;
      }
    }
  }
  if (foe != nullptr) {
    if (game.autopilot()) {
      in.pressed.set(static_cast<std::size_t>(Action::kDockingComputer));
    }
    const Vec3d aim = LeadDirection(me->position, me->velocity, foe->position, foe->velocity, kBoltSpeed);
    Aim(*me, aim, &in);
    SetThrottle(game, best > 900.0 ? 1.0 : 0.6, &in);
    if (best < 2500.0 && me->energy > 0.3 * me->energy_max && AngleBetween(Forward(me->orientation), aim) < 0.06) {
      in.held.set(static_cast<std::size_t>(Action::kFireLasers));
    }
    return in;
  }

  const Entity* station = game.world().FindKind(EntityKind::kStation);
  if (station == nullptr || game.autopilot()) {
    return in;
  }
  if (want_jump_) {
    if (game.jump_countdown() > 0.0) {
      SetThrottle(game, 0.5, &in);
      return in;
    }
    if (game.selected_system() >= 0 && !game.MassLocked()) {
      in.pressed.set(static_cast<std::size_t>(Action::kHyperspace));
      return in;
    }
    // Fly straight out, away from the station
    Aim(*me, me->position - station->position, &in);
    SetThrottle(game, 1.0, &in);
    return in;
  }
  // Home again: the docking computer does the rest
  if (game.commander().equipment.docking_computer) {
    in.pressed.set(static_cast<std::size_t>(Action::kDockingComputer));
  } else {
    Aim(*me, DockPoint(*station) - me->position, &in);
    SetThrottle(game, 0.4, &in);
  }
  return in;
}

bool BotPilot::Dock(Game& game) {
  ++landings_;
  Commander& c = game.commander();
  for (std::size_t i = 0; i < kCommodityCount; ++i) {
    if (c.cargo[i] > 0) {
      game.Sell(static_cast<Commodity>(i), c.cargo[i]);
    }
  }
  game.BuyItem(Item::kFuel);
  while (c.missiles < 3 && game.BuyItem(Item::kMissile)) {
  }

  // Buy what looks cheapest for what it is, within the means and the hold
  int pick = -1;
  double lowest = 1e9;
  for (std::size_t i = 0; i < kCommodityCount; ++i) {
    const CommodityInfo& info = CommodityData(static_cast<Commodity>(i));
    const MarketEntry& e = game.market().entry(static_cast<Commodity>(i));
    const double ratio = static_cast<double>(e.price) / info.base_price;
    if (!info.illegal && e.quantity > 0 && ratio < lowest) {
      lowest = ratio;
      pick = static_cast<int>(i);
    }
  }
  if (pick >= 0) {
    const MarketEntry& e = game.market().entry(static_cast<Commodity>(pick));
    const int affordable = static_cast<int>(c.credits / std::max(1, e.price));
    const int quantity = std::min({e.quantity, CargoFree(c), affordable});
    if (quantity > 0) {
      game.Buy(static_cast<Commodity>(pick), quantity);
    }
  }

  // Somewhere new, if the fuel allows
  const std::vector<int> reachable = game.galaxy().InRange(c.system, c.fuel);
  want_jump_ = !reachable.empty();
  if (want_jump_) {
    const std::uint32_t choices = std::min<std::uint32_t>(5, static_cast<std::uint32_t>(reachable.size()));
    game.SelectSystem(reachable[rng_.Below(choices)]);
  }
  return true;
}

}  // namespace coriolis
