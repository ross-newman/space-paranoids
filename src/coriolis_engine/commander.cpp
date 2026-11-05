#include "coriolis/commander.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace coriolis {

int CargoUsed(const Commander& c) {
  int used = 0;
  for (const int n : c.cargo) {
    used += n;
  }
  return used;
}

int CargoFree(const Commander& c) { return std::max(0, c.cargo_capacity - CargoUsed(c)); }

const char* RankName(double kills) {
  struct Rank {
    double from;
    const char* name;
  };
  static const Rank kRanks[] = {{0, "Harmless"},    {8, "Mostly Harmless"}, {16, "Poor"},
                                {32, "Average"},    {64, "Above Average"},  {128, "Competent"},
                                {512, "Dangerous"}, {2560, "Deadly"},       {6400, "Elite"}};
  const char* name = kRanks[0].name;
  for (const Rank& r : kRanks) {
    if (kills >= r.from) {
      name = r.name;
    }
  }
  return name;
}

const char* LegalName(int legal_status) {
  if (legal_status <= 0) {
    return "Clean";
  }
  return legal_status < 50 ? "Offender" : "Fugitive";
}

bool BuyCargo(Commander& c, Market& market, Commodity item, int quantity) {
  MarketEntry& e = market.entry(item);
  if (quantity <= 0 || e.quantity < quantity || CargoFree(c) < quantity) {
    return false;
  }
  const std::int64_t cost = static_cast<std::int64_t>(e.price) * quantity;
  if (cost > c.credits) {
    return false;
  }
  c.credits -= cost;
  c.cargo[static_cast<std::size_t>(item)] += quantity;
  e.quantity -= quantity;
  return true;
}

bool SellCargo(Commander& c, Market& market, Commodity item, int quantity) {
  int& held = c.cargo[static_cast<std::size_t>(item)];
  if (quantity <= 0 || held < quantity) {
    return false;
  }
  MarketEntry& e = market.entry(item);
  c.credits += static_cast<std::int64_t>(e.price) * quantity;
  held -= quantity;
  e.quantity += quantity;
  return true;
}

int ItemPrice(Item item) {
  switch (item) {
    case Item::kMissile:
      return 300;
    case Item::kEcm:
      return 6000;
    case Item::kFuelScoop:
      return 5250;
    case Item::kDockingComputer:
      return 15000;
    case Item::kEnergyBomb:
      return 9000;
    case Item::kEscapePod:
      return 10000;
    case Item::kBeamLaser:
      return 10000;
    case Item::kMilitaryLaser:
      return 60000;
    case Item::kCargoBay:
      return 4000;
    case Item::kGalacticHyperdrive:
      return 50000;
    case Item::kFuel:
      return 20;
  }
  return 0;
}

int ItemTechLevel(Item item) {
  switch (item) {
    case Item::kMissile:
    case Item::kFuel:
      return 1;
    case Item::kEcm:
    case Item::kEscapePod:
      return 3;
    case Item::kFuelScoop:
      return 5;
    case Item::kCargoBay:
      return 5;
    case Item::kEnergyBomb:
    case Item::kBeamLaser:
      return 7;
    case Item::kDockingComputer:
      return 9;
    case Item::kMilitaryLaser:
    case Item::kGalacticHyperdrive:
      return 10;
  }
  return 1;
}

const char* ItemName(Item item) {
  switch (item) {
    case Item::kMissile:
      return "Missile";
    case Item::kEcm:
      return "ECM";
    case Item::kFuelScoop:
      return "Fuel scoop";
    case Item::kDockingComputer:
      return "Docking computer";
    case Item::kEnergyBomb:
      return "Energy bomb";
    case Item::kEscapePod:
      return "Escape pod";
    case Item::kBeamLaser:
      return "Beam laser";
    case Item::kMilitaryLaser:
      return "Military laser";
    case Item::kCargoBay:
      return "Extra cargo bay";
    case Item::kGalacticHyperdrive:
      return "Galactic hyperdrive";
    case Item::kFuel:
      return "Fuel";
  }
  return "";
}

bool BuyItem(Commander& c, Item item, int system_tech_level) {
  if (system_tech_level < ItemTechLevel(item)) {
    return false;
  }
  const std::int64_t price = ItemPrice(item);
  const auto pay = [&c, price]() {
    if (c.credits < price) {
      return false;
    }
    c.credits -= price;
    return true;
  };
  switch (item) {
    case Item::kMissile:
      if (c.missiles >= 4) {
        return false;
      }
      if (!pay()) {
        return false;
      }
      ++c.missiles;
      return true;
    case Item::kEcm:
      if (c.equipment.ecm || !pay()) {
        return false;
      }
      c.equipment.ecm = true;
      return true;
    case Item::kFuelScoop:
      if (c.equipment.fuel_scoop || !pay()) {
        return false;
      }
      c.equipment.fuel_scoop = true;
      return true;
    case Item::kDockingComputer:
      if (c.equipment.docking_computer || !pay()) {
        return false;
      }
      c.equipment.docking_computer = true;
      return true;
    case Item::kEnergyBomb:
      if (c.equipment.energy_bomb || !pay()) {
        return false;
      }
      c.equipment.energy_bomb = true;
      return true;
    case Item::kEscapePod:
      if (c.equipment.escape_pod || !pay()) {
        return false;
      }
      c.equipment.escape_pod = true;
      return true;
    case Item::kBeamLaser:
      if (c.equipment.laser_level >= 1 || !pay()) {
        return false;
      }
      c.equipment.laser_level = 1;
      return true;
    case Item::kMilitaryLaser:
      if (c.equipment.laser_level >= 2 || !pay()) {
        return false;
      }
      c.equipment.laser_level = 2;
      return true;
    case Item::kCargoBay:
      if (c.equipment.cargo_bay || !pay()) {
        return false;
      }
      c.equipment.cargo_bay = true;
      c.cargo_capacity += kCargoBayExtra;
      return true;
    case Item::kGalacticHyperdrive:
      if (c.equipment.galactic_hyperdrive || !pay()) {
        return false;
      }
      c.equipment.galactic_hyperdrive = true;
      return true;
    case Item::kFuel: {
      // Fill the tank in tenths of a light year, as far as the money goes. A part of a tenth missing (the scoop and the
      // jumps leave such crumbs) still takes a whole tenth's price, so the tank can always be topped up.
      const double deficit = kMaxFuel - c.fuel;
      if (deficit < 1e-6) {
        return false;
      }
      const int tenths = static_cast<int>(std::ceil(deficit * 10.0 - 1e-6));
      const int affordable =
          static_cast<int>(std::min<std::int64_t>(tenths, c.credits / std::max<std::int64_t>(1, price / 10)));
      if (affordable <= 0) {
        return false;
      }
      c.credits -= static_cast<std::int64_t>(affordable) * (price / 10);
      c.fuel = affordable >= tenths ? kMaxFuel : c.fuel + affordable / 10.0;
      return true;
    }
  }
  return false;
}

namespace {

// id, name, price in tenths of a credit, hold in tonnes, tech level of the system that sells it
const ShipForSale kShipsForSale[] = {
    {"adder", "ADDER", 40000, 15, 3},
    {"cobra_mk1", "COBRA MK I", 60000, 18, 4},
    {"cobra_mk3_trader", "COBRA MK III", 100000, 20, 5},
    {"asp_mk2", "ASP MK II", 220000, 25, 8},
    {"transporter", "TRANSPORTER", 300000, 50, 8},
    {"python", "PYTHON", 450000, 60, 10},
    {"anaconda", "ANACONDA", 800000, 100, 12},
};

constexpr int kTradeInPercent = 60;

}  // namespace

int ShipForSaleCount() { return static_cast<int>(sizeof(kShipsForSale) / sizeof(kShipsForSale[0])); }

const ShipForSale& ShipForSaleAt(int index) { return kShipsForSale[std::clamp(index, 0, ShipForSaleCount() - 1)]; }

const ShipForSale* FindShipForSale(const std::string& id) {
  for (const ShipForSale& s : kShipsForSale) {
    if (id == s.id) {
      return &s;
    }
  }
  return nullptr;
}

int TradeInValue(const Commander& c) {
  const ShipForSale* mine = FindShipForSale(c.ship);
  return mine == nullptr ? 0 : mine->price * kTradeInPercent / 100;
}

bool BuyShip(Commander& c, int index, int system_tech_level) {
  if (index < 0 || index >= ShipForSaleCount()) {
    return false;
  }
  const ShipForSale& ship = kShipsForSale[index];
  if (system_tech_level < ship.tech_level || c.ship == ship.id) {
    return false;
  }
  const std::int64_t cost = static_cast<std::int64_t>(ship.price) - TradeInValue(c);
  const int hold = ship.hold + (c.equipment.cargo_bay ? kCargoBayExtra : 0);
  if (c.credits < cost || CargoUsed(c) > hold) {
    return false;
  }
  c.credits -= cost;
  c.ship = ship.id;
  c.cargo_capacity = hold;
  return true;
}

Json CommanderToJson(const Commander& c) {
  Json root = Json::MakeObject();
  root.Set("format", Json::String("coriolis-commander"));
  root.Set("version", Json::Number(1));
  root.Set("name", Json::String(c.name));
  root.Set("credits", Json::Number(static_cast<double>(c.credits)));
  root.Set("fuel", Json::Number(c.fuel));
  root.Set("galaxy", Json::Number(c.galaxy));
  root.Set("system", Json::Number(c.system));
  Json cargo = Json::MakeArray();
  for (const int n : c.cargo) {
    cargo.Push(Json::Number(n));
  }
  root.Set("cargo", std::move(cargo));
  root.Set("cargo_capacity", Json::Number(c.cargo_capacity));
  root.Set("missiles", Json::Number(c.missiles));
  Json eq = Json::MakeObject();
  eq.Set("ecm", Json::Bool(c.equipment.ecm));
  eq.Set("fuel_scoop", Json::Bool(c.equipment.fuel_scoop));
  eq.Set("docking_computer", Json::Bool(c.equipment.docking_computer));
  eq.Set("energy_bomb", Json::Bool(c.equipment.energy_bomb));
  eq.Set("escape_pod", Json::Bool(c.equipment.escape_pod));
  eq.Set("cargo_bay", Json::Bool(c.equipment.cargo_bay));
  eq.Set("galactic_hyperdrive", Json::Bool(c.equipment.galactic_hyperdrive));
  eq.Set("laser_level", Json::Number(c.equipment.laser_level));
  root.Set("equipment", std::move(eq));
  root.Set("legal_status", Json::Number(c.legal_status));
  root.Set("kills", Json::Number(c.kills));
  root.Set("market_mood", Json::Number(static_cast<double>(c.market_mood)));
  root.Set("ship", Json::String(c.ship));
  root.Set("story_step", Json::Number(c.story_step));
  Json mission = Json::MakeObject();
  mission.Set("kind", Json::Number(static_cast<int>(c.mission.kind)));
  mission.Set("step", Json::Number(c.mission.step));
  mission.Set("destination", Json::Number(c.mission.destination));
  mission.Set("count", Json::Number(c.mission.count));
  mission.Set("progress", Json::Number(c.mission.progress));
  mission.Set("reward", Json::Number(c.mission.reward));
  root.Set("mission", std::move(mission));
  return root;
}

namespace {

bool Fail(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

bool GetNumber(const Json& j, const char* key, double* out, std::string* error) {
  const Json* m = j.Find(key);
  if (m == nullptr || !m->is_number()) {
    return Fail(error, std::string("commander: \"") + key + "\" is missing or not a number");
  }
  *out = m->number_value();
  return true;
}

bool GetFlag(const Json& j, const char* key, bool* out, std::string* error) {
  const Json* m = j.Find(key);
  if (m == nullptr || !m->is_bool()) {
    return Fail(error, std::string("commander: equipment \"") + key + "\" is missing or not true/false");
  }
  *out = m->bool_value();
  return true;
}

/// A flag that older saves do not have: missing means false, but if it is there it has to be true or false
bool GetOptionalFlag(const Json& j, const char* key, bool* out, std::string* error) {
  if (j.Find(key) == nullptr) {
    *out = false;
    return true;
  }
  return GetFlag(j, key, out, error);
}

}  // namespace

bool CommanderFromJson(const Json& json, Commander* out, std::string* error) {
  const Json* format = json.Find("format");
  if (!json.is_object() || format == nullptr || !format->is_string() ||
      format->string_value() != "coriolis-commander") {
    return Fail(error, "commander: not an coriolis-commander file");
  }
  Commander c;
  double v = 0.0;
  const Json* name = json.Find("name");
  const Json* ship = json.Find("ship");
  const Json* cargo = json.Find("cargo");
  const Json* eq = json.Find("equipment");
  if (name == nullptr || !name->is_string() || ship == nullptr || !ship->is_string() || cargo == nullptr ||
      !cargo->is_array() || cargo->items().size() != kCommodityCount || eq == nullptr || !eq->is_object()) {
    return Fail(error, "commander: name, ship, cargo or equipment is missing or the wrong kind");
  }
  c.name = name->string_value();
  c.ship = ship->string_value();
  for (std::size_t i = 0; i < kCommodityCount; ++i) {
    const Json& n = cargo->items()[i];
    if (!n.is_number() || n.number_value() < 0.0 || n.number_value() > 100000.0) {
      return Fail(error, "commander: bad cargo amount");
    }
    c.cargo[i] = static_cast<int>(n.number_value());
  }
  if (!GetNumber(json, "credits", &v, error)) {
    return false;
  }
  c.credits = static_cast<std::int64_t>(v);
  if (!GetNumber(json, "fuel", &c.fuel, error) || !GetNumber(json, "kills", &c.kills, error)) {
    return false;
  }
  c.fuel = std::clamp(c.fuel, 0.0, kMaxFuel);
  double galaxy = 0.0, system = 0.0, capacity = 0.0, missiles = 0.0, legal = 0.0, mood = 0.0, laser = 0.0;
  if (!GetNumber(json, "galaxy", &galaxy, error) || !GetNumber(json, "system", &system, error) ||
      !GetNumber(json, "cargo_capacity", &capacity, error) || !GetNumber(json, "missiles", &missiles, error) ||
      !GetNumber(json, "legal_status", &legal, error) || !GetNumber(json, "market_mood", &mood, error) ||
      !GetNumber(*eq, "laser_level", &laser, error)) {
    return false;
  }
  if (galaxy < 0 || galaxy >= kGalaxyCount || system < 0 || system >= kSystemsPerGalaxy || capacity < 0 ||
      capacity > 1000 || missiles < 0 || missiles > 16 || legal < 0 || legal > 255 || laser < 0 || laser > 2) {
    return Fail(error, "commander: a value is out of range");
  }
  c.galaxy = static_cast<int>(galaxy);
  c.system = static_cast<int>(system);
  c.cargo_capacity = static_cast<int>(capacity);
  c.missiles = static_cast<int>(missiles);
  c.legal_status = static_cast<int>(legal);
  c.market_mood = static_cast<std::uint32_t>(mood);
  c.equipment.laser_level = static_cast<int>(laser);
  if (!GetFlag(*eq, "ecm", &c.equipment.ecm, error) || !GetFlag(*eq, "fuel_scoop", &c.equipment.fuel_scoop, error) ||
      !GetFlag(*eq, "docking_computer", &c.equipment.docking_computer, error) ||
      !GetFlag(*eq, "energy_bomb", &c.equipment.energy_bomb, error) ||
      !GetFlag(*eq, "escape_pod", &c.equipment.escape_pod, error) ||
      !GetOptionalFlag(*eq, "cargo_bay", &c.equipment.cargo_bay, error) ||
      !GetOptionalFlag(*eq, "galactic_hyperdrive", &c.equipment.galactic_hyperdrive, error)) {
    return false;
  }
  // The story and the job under way, which saves from before the story do not have
  c.story_step = 0;
  c.mission = Mission{};
  const Json* story = json.Find("story_step");
  const Json* mission = json.Find("mission");
  if (story != nullptr || mission != nullptr) {
    double step = 0.0, kind = 0.0, mstep = 0.0, dest = -1.0, count = 0.0, progress = 0.0, reward = 0.0;
    if (story == nullptr || mission == nullptr || !mission->is_object() ||
        !GetNumber(json, "story_step", &step, error) || !GetNumber(*mission, "kind", &kind, error) ||
        !GetNumber(*mission, "step", &mstep, error) || !GetNumber(*mission, "destination", &dest, error) ||
        !GetNumber(*mission, "count", &count, error) || !GetNumber(*mission, "progress", &progress, error) ||
        !GetNumber(*mission, "reward", &reward, error)) {
      return Fail(error, "commander: the story or the mission is damaged");
    }
    if (step < 0 || step > 100 || kind < 0 || kind > 2 || mstep < 0 || mstep > 100 || dest < -1 ||
        dest >= kSystemsPerGalaxy || count < 0 || count > 1000 || progress < 0 || progress > 1000 || reward < 0 ||
        reward > 100000000) {
      return Fail(error, "commander: a value of the mission is out of range");
    }
    c.story_step = static_cast<int>(step);
    c.mission.kind = static_cast<MissionKind>(static_cast<int>(kind));
    c.mission.step = static_cast<int>(mstep);
    c.mission.destination = static_cast<int>(dest);
    c.mission.count = static_cast<int>(count);
    c.mission.progress = static_cast<int>(progress);
    c.mission.reward = static_cast<int>(reward);
  }
  *out = c;
  return true;
}

bool SaveCommander(const Commander& c, const std::string& path, std::string* error) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    return Fail(error, path + ": cannot create the file");
  }
  file << CommanderToJson(c).Dump();
  file.flush();
  return file ? true : Fail(error, path + ": cannot write the file");
}

bool LoadCommander(const std::string& path, Commander* out, std::string* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return Fail(error, path + ": cannot open the file");
  }
  std::ostringstream text;
  text << file.rdbuf();
  Json json;
  std::string why;
  if (!Json::Parse(text.str(), &json, &why) || !CommanderFromJson(json, out, &why)) {
    return Fail(error, path + ": " + why);
  }
  return true;
}

}  // namespace coriolis
