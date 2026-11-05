#include "coriolis/blueprint.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

namespace coriolis {

namespace {

struct FloatStat {
  const char* name;
  float BlueprintStats::* member;
};

struct CountStat {
  const char* name;
  std::uint32_t BlueprintStats::* member;
};

const FloatStat kFloatStats[] = {
    {"mass_kg", &BlueprintStats::mass_kg},
    {"max_speed", &BlueprintStats::max_speed},
    {"acceleration", &BlueprintStats::acceleration},
    {"max_pitch_rate", &BlueprintStats::max_pitch_rate},
    {"max_roll_rate", &BlueprintStats::max_roll_rate},
    {"max_yaw_rate", &BlueprintStats::max_yaw_rate},
    {"hull", &BlueprintStats::hull},
    {"shield", &BlueprintStats::shield},
    {"energy", &BlueprintStats::energy},
    {"energy_recharge", &BlueprintStats::energy_recharge},
    {"target_radius", &BlueprintStats::target_radius},
    {"visibility_distance", &BlueprintStats::visibility_distance},
    {"bounty", &BlueprintStats::bounty},
    {"kill_points", &BlueprintStats::kill_points},
};

const CountStat kCountStats[] = {
    {"max_cargo_drop", &BlueprintStats::max_cargo_drop},
    {"missiles", &BlueprintStats::missiles},
};

struct KindName {
  HullKind kind;
  const char* name;
};

const KindName kHullKinds[] = {
    {HullKind::kShip, "ship"},         {HullKind::kStation, "station"},   {HullKind::kMissile, "missile"},
    {HullKind::kCanister, "canister"}, {HullKind::kAsteroid, "asteroid"}, {HullKind::kEscapePod, "escape_pod"},
};

struct HardpointName {
  HardpointKind kind;
  const char* name;
};

const HardpointName kHardpointKinds[] = {
    {HardpointKind::kLaser, "laser"},   {HardpointKind::kMissile, "missile"}, {HardpointKind::kScoop, "scoop"},
    {HardpointKind::kEngine, "engine"}, {HardpointKind::kDock, "dock"},
};

bool Finite(float v) { return std::isfinite(v); }
bool Finite(const Vec3& v) { return Finite(v.x) && Finite(v.y) && Finite(v.z); }

float Length(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

Vec3 Cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 Sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// ---- Writing ----

/// A float as the shortest decimal that reads back as the same float, so files show 0.1 and not 0.10000000149
Json Num(float value) {
  if (!std::isfinite(value)) {
    return Json::Number(value);
  }
  char buffer[40];
  for (int precision = 1; precision <= 9; ++precision) {
    std::snprintf(buffer, sizeof buffer, "%.*g", precision, static_cast<double>(value));
    if (std::strtof(buffer, nullptr) == value) {
      break;
    }
  }
  return Json::Number(std::strtod(buffer, nullptr));
}

Json NumArray(std::initializer_list<float> values) {
  Json a = Json::MakeArray();
  for (const float v : values) {
    a.Push(Num(v));
  }
  return a;
}

Json VecJson(const Vec3& v) { return NumArray({v.x, v.y, v.z}); }

std::string ColourText(Rgb c) {
  char buffer[8];
  std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x", c.r, c.g, c.b);
  return buffer;
}

// ---- Reading ----

class Reader {
 public:
  explicit Reader(std::string* error) : error_(error) {}

  bool Fail(const std::string& where, const std::string& message) {
    if (error_ != nullptr && error_->empty()) {
      *error_ = where + ": " + message;
    }
    return false;
  }

  bool Number(const Json& j, const std::string& where, float* out) {
    if (!j.is_number()) {
      return Fail(where, "expected a number");
    }
    *out = static_cast<float>(j.number_value());
    return true;
  }

  bool Count(const Json& j, const std::string& where, std::uint32_t* out) {
    if (!j.is_number() || j.number_value() < 0.0 || j.number_value() > 4294967295.0 ||
        j.number_value() != std::floor(j.number_value())) {
      return Fail(where, "expected a whole number of 0 or more");
    }
    *out = static_cast<std::uint32_t>(j.number_value());
    return true;
  }

  bool Text(const Json& j, const std::string& where, std::string* out) {
    if (!j.is_string()) {
      return Fail(where, "expected a string");
    }
    *out = j.string_value();
    return true;
  }

  bool Flag(const Json& j, const std::string& where, bool* out) {
    if (!j.is_bool()) {
      return Fail(where, "expected true or false");
    }
    *out = j.bool_value();
    return true;
  }

  bool Vector(const Json& j, const std::string& where, Vec3* out) {
    if (!j.is_array() || j.items().size() != 3) {
      return Fail(where, "expected [x, y, z]");
    }
    return Number(j.items()[0], where, &out->x) && Number(j.items()[1], where, &out->y) &&
           Number(j.items()[2], where, &out->z);
  }

  bool Colour(const Json& j, const std::string& where, Rgb* out) {
    if (!j.is_string() || j.string_value().size() != 7 || j.string_value()[0] != '#') {
      return Fail(where, "expected a colour like \"#rrggbb\"");
    }
    unsigned value = 0;
    for (std::size_t i = 1; i < 7; ++i) {
      const char c = j.string_value()[i];
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= static_cast<unsigned>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value |= static_cast<unsigned>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value |= static_cast<unsigned>(c - 'A' + 10);
      } else {
        return Fail(where, "expected a colour like \"#rrggbb\"");
      }
    }
    *out = {static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 8),
            static_cast<std::uint8_t>(value)};
    return true;
  }

  /// A member that must be there
  const Json* Require(const Json& object, const std::string& where, const char* key) {
    const Json* j = object.Find(key);
    if (j == nullptr) {
      Fail(where, std::string("missing \"") + key + "\"");
    }
    return j;
  }

  bool Object(const Json& j, const std::string& where) {
    if (!j.is_object()) {
      return Fail(where, "expected an object");
    }
    return true;
  }

  bool Array(const Json& j, const std::string& where) {
    if (!j.is_array()) {
      return Fail(where, "expected an array");
    }
    return true;
  }

 private:
  std::string* error_;
};

std::string At(const char* what, std::size_t index) { return std::string(what) + "[" + std::to_string(index) + "]"; }

template <typename Table, typename Kind>
bool ReadKind(Reader& r, const Json& j, const std::string& where, const Table& table, Kind* out) {
  if (!j.is_string()) {
    return r.Fail(where, "expected a string");
  }
  for (const auto& entry : table) {
    if (j.string_value() == entry.name) {
      *out = entry.kind;
      return true;
    }
  }
  return r.Fail(where, "unknown value \"" + j.string_value() + "\"");
}

bool ReadStats(Reader& r, const Json& j, BlueprintStats* stats) {
  if (!r.Object(j, "stats")) {
    return false;
  }
  for (std::size_t i = 0; i < j.keys().size(); ++i) {
    const std::string& key = j.keys()[i];
    const std::string where = "stats." + key;
    bool known = false;
    for (const FloatStat& s : kFloatStats) {
      if (key == s.name) {
        known = true;
        if (!r.Number(j.items()[i], where, &(stats->*(s.member)))) {
          return false;
        }
      }
    }
    for (const CountStat& s : kCountStats) {
      if (key == s.name) {
        known = true;
        if (!r.Count(j.items()[i], where, &(stats->*(s.member)))) {
          return false;
        }
      }
    }
    if (!known) {
      return r.Fail(where, "unknown stat");
    }
  }
  return true;
}

bool ReadVertices(Reader& r, const Json& j, std::vector<BlueprintVertex>* out) {
  if (!r.Array(j, "vertices")) {
    return false;
  }
  for (std::size_t i = 0; i < j.items().size(); ++i) {
    const Json& item = j.items()[i];
    const std::string where = At("vertices", i);
    if (!item.is_array() || (item.items().size() != 3 && item.items().size() != 4)) {
      return r.Fail(where, "expected [x, y, z] or [x, y, z, visibility]");
    }
    BlueprintVertex v;
    if (!r.Number(item.items()[0], where, &v.position.x) || !r.Number(item.items()[1], where, &v.position.y) ||
        !r.Number(item.items()[2], where, &v.position.z)) {
      return false;
    }
    if (item.items().size() == 4 && !r.Number(item.items()[3], where, &v.visibility)) {
      return false;
    }
    out->push_back(v);
  }
  return true;
}

bool ReadTriangles(Reader& r, const Json& j, std::vector<BlueprintTriangle>* out) {
  if (!r.Array(j, "triangles")) {
    return false;
  }
  for (std::size_t i = 0; i < j.items().size(); ++i) {
    const Json& item = j.items()[i];
    const std::string where = At("triangles", i);
    if (!r.Object(item, where)) {
      return false;
    }
    BlueprintTriangle t{{0, 0, 0}, {0, 0, 0}, false, 0};
    const Json* v = r.Require(item, where, "v");
    const Json* colour = r.Require(item, where, "colour");
    if (v == nullptr || colour == nullptr) {
      return false;
    }
    if (!v->is_array() || v->items().size() != 3) {
      return r.Fail(where + ".v", "expected three vertex numbers");
    }
    for (std::size_t k = 0; k < 3; ++k) {
      if (!r.Count(v->items()[k], where + ".v", &t.v[k])) {
        return false;
      }
    }
    if (!r.Colour(*colour, where + ".colour", &t.colour)) {
      return false;
    }
    if (const Json* e = item.Find("emissive")) {
      if (!r.Flag(*e, where + ".emissive", &t.emissive)) {
        return false;
      }
    }
    if (const Json* e = item.Find("edges")) {
      std::uint32_t mask = 0;
      if (!r.Count(*e, where + ".edges", &mask)) {
        return false;
      }
      if (mask > 7) {
        return r.Fail(where + ".edges", "expected 0 to 7");
      }
      t.edges = static_cast<std::uint8_t>(mask);
    }
    out->push_back(t);
  }
  return true;
}

bool ReadEdges(Reader& r, const Json& j, std::vector<BlueprintEdge>* out) {
  if (!r.Array(j, "edges")) {
    return false;
  }
  for (std::size_t i = 0; i < j.items().size(); ++i) {
    const Json& item = j.items()[i];
    const std::string where = At("edges", i);
    if (!item.is_array() || (item.items().size() != 2 && item.items().size() != 3)) {
      return r.Fail(where, "expected [a, b] or [a, b, visibility]");
    }
    BlueprintEdge e;
    if (!r.Count(item.items()[0], where, &e.a) || !r.Count(item.items()[1], where, &e.b)) {
      return false;
    }
    if (item.items().size() == 3 && !r.Number(item.items()[2], where, &e.visibility)) {
      return false;
    }
    out->push_back(e);
  }
  return true;
}

bool ReadHardpoints(Reader& r, const Json& j, std::vector<Hardpoint>* out) {
  if (!r.Array(j, "hardpoints")) {
    return false;
  }
  for (std::size_t i = 0; i < j.items().size(); ++i) {
    const Json& item = j.items()[i];
    const std::string where = At("hardpoints", i);
    if (!r.Object(item, where)) {
      return false;
    }
    Hardpoint h;
    const Json* kind = r.Require(item, where, "kind");
    const Json* position = r.Require(item, where, "position");
    if (kind == nullptr || position == nullptr || !ReadKind(r, *kind, where + ".kind", kHardpointKinds, &h.kind) ||
        !r.Vector(*position, where + ".position", &h.position)) {
      return false;
    }
    if (const Json* d = item.Find("direction")) {
      if (!r.Vector(*d, where + ".direction", &h.direction)) {
        return false;
      }
    }
    out->push_back(h);
  }
  return true;
}

bool ReadSubsystems(Reader& r, const Json& j, std::vector<Subsystem>* out) {
  if (!r.Array(j, "subsystems")) {
    return false;
  }
  for (std::size_t i = 0; i < j.items().size(); ++i) {
    const Json& item = j.items()[i];
    const std::string where = At("subsystems", i);
    if (!r.Object(item, where)) {
      return false;
    }
    Subsystem s;
    const Json* name = r.Require(item, where, "name");
    const Json* centre = r.Require(item, where, "centre");
    const Json* radius = r.Require(item, where, "radius");
    const Json* health = r.Require(item, where, "health");
    if (name == nullptr || centre == nullptr || radius == nullptr || health == nullptr ||
        !r.Text(*name, where + ".name", &s.name) || !r.Vector(*centre, where + ".centre", &s.centre) ||
        !r.Number(*radius, where + ".radius", &s.radius) || !r.Number(*health, where + ".health", &s.health)) {
      return false;
    }
    out->push_back(std::move(s));
  }
  return true;
}

bool ValidId(const std::string& id) {
  if (id.empty()) {
    return false;
  }
  for (const char c : id) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
      return false;
    }
  }
  return true;
}

}  // namespace

const char* HullKindName(HullKind kind) {
  for (const KindName& k : kHullKinds) {
    if (k.kind == kind) {
      return k.name;
    }
  }
  return "ship";
}

const char* HardpointKindName(HardpointKind kind) {
  for (const HardpointName& k : kHardpointKinds) {
    if (k.kind == kind) {
      return k.name;
    }
  }
  return "laser";
}

std::vector<std::string> ValidateBlueprint(const Blueprint& b) {
  std::vector<std::string> problems;
  auto add = [&problems](const std::string& message) { problems.push_back(message); };

  if (!ValidId(b.id)) {
    add("id must be lower case letters, digits and underscores");
  }
  if (b.name.empty()) {
    add("name is empty");
  }
  if (!Finite(b.scale) || b.scale <= 0.0f) {
    add("scale must be above 0");
  }

  const BlueprintStats& s = b.stats;
  for (const FloatStat& f : kFloatStats) {
    const float value = s.*(f.member);
    if (!Finite(value) || value < 0.0f) {
      add(std::string("stats.") + f.name + " must be 0 or more");
    }
  }
  if (s.mass_kg <= 0.0f) {
    add("stats.mass_kg must be above 0");
  }
  if (s.hull <= 0.0f) {
    add("stats.hull must be above 0");
  }

  if (b.vertices.empty()) {
    add("there are no vertices");
  }
  for (std::size_t i = 0; i < b.vertices.size(); ++i) {
    if (!Finite(b.vertices[i].position) || !Finite(b.vertices[i].visibility) || b.vertices[i].visibility < 0.0f) {
      add(At("vertices", i) + " has a bad number");
    }
  }
  if (b.triangles.empty()) {
    add("there are no triangles");
  }
  for (std::size_t i = 0; i < b.triangles.size(); ++i) {
    const BlueprintTriangle& t = b.triangles[i];
    for (const std::uint32_t v : t.v) {
      if (v >= b.vertices.size()) {
        add(At("triangles", i) + " uses vertex " + std::to_string(v) + ", which does not exist");
        break;
      }
    }
    if (t.edges > 7) {
      add(At("triangles", i) + " edges must be 0 to 7");
    }
  }
  for (std::size_t i = 0; i < b.edges.size(); ++i) {
    const BlueprintEdge& e = b.edges[i];
    if (e.a >= b.vertices.size() || e.b >= b.vertices.size()) {
      add(At("edges", i) + " uses a vertex that does not exist");
    }
    if (!Finite(e.visibility) || e.visibility < 0.0f) {
      add(At("edges", i) + " has a bad visibility");
    }
  }
  for (std::size_t i = 0; i < b.hardpoints.size(); ++i) {
    const Hardpoint& h = b.hardpoints[i];
    if (!Finite(h.position) || !Finite(h.direction)) {
      add(At("hardpoints", i) + " has a bad number");
    } else if (std::fabs(Length(h.direction) - 1.0f) > 0.01f) {
      add(At("hardpoints", i) + " direction must be a unit vector");
    }
  }
  std::set<std::string> names;
  for (std::size_t i = 0; i < b.subsystems.size(); ++i) {
    const Subsystem& sub = b.subsystems[i];
    if (sub.name.empty()) {
      add(At("subsystems", i) + " has no name");
    } else if (!names.insert(sub.name).second) {
      add(At("subsystems", i) + " repeats the name \"" + sub.name + "\"");
    }
    if (!Finite(sub.centre) || !Finite(sub.radius) || sub.radius <= 0.0f) {
      add(At("subsystems", i) + " needs a centre and a radius above 0");
    }
    if (!Finite(sub.health) || sub.health <= 0.0f) {
      add(At("subsystems", i) + " health must be above 0");
    }
  }
  return problems;
}

Json BlueprintToJson(const Blueprint& b) {
  Json root = Json::MakeObject();
  root.Set("format", Json::String("coriolis-blueprint"));
  root.Set("version", Json::Number(kBlueprintVersion));
  root.Set("id", Json::String(b.id));
  root.Set("name", Json::String(b.name));
  root.Set("kind", Json::String(HullKindName(b.kind)));
  root.Set("spin", Json::String(b.spin == Spin::kRoll ? "roll" : (b.spin == Spin::kSway ? "sway" : "yaw")));
  root.Set("scale", Num(b.scale));
  root.Set("ai_profile", Json::String(b.ai_profile));

  Json stats = Json::MakeObject();
  for (const FloatStat& f : kFloatStats) {
    stats.Set(f.name, Num(b.stats.*(f.member)));
  }
  for (const CountStat& f : kCountStats) {
    stats.Set(f.name, Json::Number(static_cast<double>(b.stats.*(f.member))));
  }
  root.Set("stats", std::move(stats));

  Json vertices = Json::MakeArray();
  for (const BlueprintVertex& v : b.vertices) {
    Json a = VecJson(v.position);
    if (v.visibility != 0.0f) {
      a.Push(Num(v.visibility));
    }
    vertices.Push(std::move(a));
  }
  root.Set("vertices", std::move(vertices));

  Json triangles = Json::MakeArray();
  for (const BlueprintTriangle& t : b.triangles) {
    Json o = Json::MakeObject();
    Json v = Json::MakeArray();
    for (const std::uint32_t index : t.v) {
      v.Push(Json::Number(index));
    }
    o.Set("v", std::move(v));
    o.Set("colour", Json::String(ColourText(t.colour)));
    if (t.emissive) {
      o.Set("emissive", Json::Bool(true));
    }
    if (t.edges != 0) {
      o.Set("edges", Json::Number(t.edges));
    }
    triangles.Push(std::move(o));
  }
  root.Set("triangles", std::move(triangles));

  Json edges = Json::MakeArray();
  for (const BlueprintEdge& e : b.edges) {
    Json a = Json::MakeArray();
    a.Push(Json::Number(e.a));
    a.Push(Json::Number(e.b));
    if (e.visibility != 0.0f) {
      a.Push(Num(e.visibility));
    }
    edges.Push(std::move(a));
  }
  root.Set("edges", std::move(edges));

  Json hardpoints = Json::MakeArray();
  for (const Hardpoint& h : b.hardpoints) {
    Json o = Json::MakeObject();
    o.Set("kind", Json::String(HardpointKindName(h.kind)));
    o.Set("position", VecJson(h.position));
    o.Set("direction", VecJson(h.direction));
    hardpoints.Push(std::move(o));
  }
  root.Set("hardpoints", std::move(hardpoints));

  Json subsystems = Json::MakeArray();
  for (const Subsystem& s : b.subsystems) {
    Json o = Json::MakeObject();
    o.Set("name", Json::String(s.name));
    o.Set("centre", VecJson(s.centre));
    o.Set("radius", Num(s.radius));
    o.Set("health", Num(s.health));
    subsystems.Push(std::move(o));
  }
  root.Set("subsystems", std::move(subsystems));
  return root;
}

bool BlueprintFromJson(const Json& json, Blueprint* out, std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  Reader r(error);
  if (!r.Object(json, "blueprint")) {
    return false;
  }

  const Json* format = r.Require(json, "blueprint", "format");
  if (format == nullptr) {
    return false;
  }
  if (!format->is_string() || format->string_value() != "coriolis-blueprint") {
    return r.Fail("format", "not an coriolis-blueprint file");
  }
  const Json* version = r.Require(json, "blueprint", "version");
  if (version == nullptr) {
    return false;
  }
  if (!version->is_number() || version->number_value() != kBlueprintVersion) {
    return r.Fail("version", "unsupported version, expected " + std::to_string(kBlueprintVersion));
  }

  Blueprint b;
  const Json* id = r.Require(json, "blueprint", "id");
  const Json* name = r.Require(json, "blueprint", "name");
  const Json* vertices = r.Require(json, "blueprint", "vertices");
  const Json* triangles = r.Require(json, "blueprint", "triangles");
  if (id == nullptr || name == nullptr || vertices == nullptr || triangles == nullptr || !r.Text(*id, "id", &b.id) ||
      !r.Text(*name, "name", &b.name) || !ReadVertices(r, *vertices, &b.vertices) ||
      !ReadTriangles(r, *triangles, &b.triangles)) {
    return false;
  }
  if (const Json* j = json.Find("kind")) {
    if (!ReadKind(r, *j, "kind", kHullKinds, &b.kind)) {
      return false;
    }
  }
  if (const Json* j = json.Find("spin")) {
    if (!j->is_string() || (j->string_value() != "yaw" && j->string_value() != "roll" && j->string_value() != "sway")) {
      return r.Fail("spin", "expected \"yaw\", \"roll\" or \"sway\"");
    }
    b.spin = j->string_value() == "roll" ? Spin::kRoll : (j->string_value() == "sway" ? Spin::kSway : Spin::kYaw);
  }
  if (const Json* j = json.Find("scale")) {
    if (!r.Number(*j, "scale", &b.scale)) {
      return false;
    }
  }
  if (const Json* j = json.Find("ai_profile")) {
    if (!r.Text(*j, "ai_profile", &b.ai_profile)) {
      return false;
    }
  }
  if (const Json* j = json.Find("stats")) {
    if (!ReadStats(r, *j, &b.stats)) {
      return false;
    }
  }
  if (const Json* j = json.Find("edges")) {
    if (!ReadEdges(r, *j, &b.edges)) {
      return false;
    }
  }
  if (const Json* j = json.Find("hardpoints")) {
    if (!ReadHardpoints(r, *j, &b.hardpoints)) {
      return false;
    }
  }
  if (const Json* j = json.Find("subsystems")) {
    if (!ReadSubsystems(r, *j, &b.subsystems)) {
      return false;
    }
  }

  const std::vector<std::string> problems = ValidateBlueprint(b);
  if (!problems.empty()) {
    return r.Fail("blueprint", problems.front());
  }
  *out = std::move(b);
  return true;
}

bool LoadBlueprintFile(const std::string& path, Blueprint* out, std::string* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    if (error != nullptr) {
      *error = path + ": cannot open the file";
    }
    return false;
  }
  std::ostringstream text;
  text << file.rdbuf();
  Json json;
  std::string why;
  if (!Json::Parse(text.str(), &json, &why) || !BlueprintFromJson(json, out, &why)) {
    if (error != nullptr) {
      *error = path + ": " + why;
    }
    return false;
  }
  return true;
}

bool SaveBlueprintFile(const Blueprint& blueprint, const std::string& path, std::string* error) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    if (error != nullptr) {
      *error = path + ": cannot create the file";
    }
    return false;
  }
  file << BlueprintToJson(blueprint).Dump();
  file.flush();
  if (!file) {
    if (error != nullptr) {
      *error = path + ": cannot write the file";
    }
    return false;
  }
  return true;
}

Model BlueprintToModel(const Blueprint& blueprint) {
  Model model;
  model.name = blueprint.name;
  model.spin = blueprint.spin;
  model.vertices.reserve(blueprint.vertices.size());
  for (const BlueprintVertex& v : blueprint.vertices) {
    model.vertices.push_back(v.position);
  }
  model.faces.reserve(blueprint.triangles.size());
  for (const BlueprintTriangle& t : blueprint.triangles) {
    const Vec3& a = model.vertices[t.v[0]];
    const Vec3& b = model.vertices[t.v[1]];
    const Vec3& c = model.vertices[t.v[2]];
    const Vec3 middle{(a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f, (a.z + b.z + c.z) / 3.0f};
    Vec3 n = Cross(Sub(b, a), Sub(c, a));
    const float length = Length(n);
    // The renderer points normals away from this centre, so put it one unit behind the face
    Vec3 centre = middle;
    if (length > 1e-12f) {
      centre = {middle.x - n.x / length, middle.y - n.y / length, middle.z - n.z / length};
    }
    model.faces.push_back({{t.v[0], t.v[1], t.v[2]}, t.colour, t.emissive, t.edges, centre});
  }
  return model;
}

Blueprint BlueprintFromModel(const Model& model, const std::string& id) {
  Blueprint b;
  b.id = id;
  b.name = model.name;
  b.spin = model.spin;
  b.vertices.reserve(model.vertices.size());
  for (const Vec3& v : model.vertices) {
    b.vertices.push_back({v, 0.0f});
  }
  b.triangles.reserve(model.faces.size());
  for (const Face& f : model.faces) {
    BlueprintTriangle t;
    t.v = {static_cast<std::uint32_t>(f.v[0]), static_cast<std::uint32_t>(f.v[1]), static_cast<std::uint32_t>(f.v[2])};
    t.colour = f.colour;
    t.emissive = f.emissive;
    t.edges = f.edges;
    const Vec3& a = model.vertices[f.v[0]];
    const Vec3& bb = model.vertices[f.v[1]];
    const Vec3& c = model.vertices[f.v[2]];
    const Vec3 middle{(a.x + bb.x + c.x) / 3.0f, (a.y + bb.y + c.y) / 3.0f, (a.z + bb.z + c.z) / 3.0f};
    if (Dot(Cross(Sub(bb, a), Sub(c, a)), Sub(middle, f.centre)) < 0.0f) {
      // Swapping v1 and v2 turns the face round; the outlined edges v0-v1 and v2-v0 swap places
      std::swap(t.v[1], t.v[2]);
      t.edges = static_cast<std::uint8_t>(((f.edges & 4) ? 1 : 0) | (f.edges & 2) | ((f.edges & 1) ? 4 : 0));
    }
    b.triangles.push_back(t);
  }
  return b;
}

}  // namespace coriolis
