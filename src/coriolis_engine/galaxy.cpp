#include "coriolis/galaxy.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <utility>

#include "coriolis/random.h"

namespace coriolis {

namespace {

const char* const kEconomies[8] = {"Rich Industrial",      "Average Industrial",  "Poor Industrial",
                                   "Mainly Industrial",    "Mainly Agricultural", "Rich Agricultural",
                                   "Average Agricultural", "Poor Agricultural"};

const char* const kGovernments[8] = {"Anarchy",   "Feudal",      "Multi-government", "Dictatorship",
                                     "Communist", "Confederacy", "Democracy",        "Corporate State"};

const char* const kSyllables[] = {"la", "ve", "za", "on", "ce", "ti", "ar", "di", "bi", "ra", "ne", "su", "ko",
                                  "mi", "ly", "qu", "xe", "ga", "ho", "po", "ru", "te", "vi", "wa", "yo", "ta",
                                  "re", "so", "ka", "lu", "ma", "no", "pi", "sa", "do", "fe", "ju", "be"};
constexpr std::uint32_t kSyllableCount = sizeof(kSyllables) / sizeof(kSyllables[0]);

const char* const kSizes[] = {"Tall", "Squat", "Lanky"};
const char* const kColours[] = {"Green", "Amber", "Violet", "Silver", "Crimson", "Pale"};
const char* const kFeatures[] = {"Slimy", "Spiny", "Horned", "Furred", "Scaled", "Winged"};
const char* const kKinds[] = {"Rodents", "Frogs", "Lizards", "Crabs", "Birds", "Humanoids", "Felines", "Insects"};

std::string MakeName(Random& r) {
  std::string name;
  const std::uint32_t parts = 2 + r.Below(3);
  for (std::uint32_t i = 0; i < parts; ++i) {
    name += kSyllables[r.Below(kSyllableCount)];
  }
  name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
  return name;
}

std::string MakeSpecies(Random& r) {
  // About one in four systems is settled by humans
  if (r.Below(4) == 0) {
    return "Human Colonials";
  }
  std::string s;
  if (r.Below(2) == 0) {
    s += std::string(kSizes[r.Below(3)]) + " ";
  }
  if (r.Below(2) == 0) {
    s += std::string(kColours[r.Below(6)]) + " ";
  }
  if (r.Below(2) == 0) {
    s += std::string(kFeatures[r.Below(6)]) + " ";
  }
  return s + kKinds[r.Below(8)];
}

std::string Lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

}  // namespace

const char* EconomyName(int economy) { return kEconomies[std::clamp(economy, 0, 7)]; }
const char* GovernmentName(int government) { return kGovernments[std::clamp(government, 0, 7)]; }

Galaxy::Galaxy(int number) : number_(std::clamp(number, 0, kGalaxyCount - 1)) {
  std::set<std::pair<int, int>> taken;
  std::set<std::string> names;
  systems_.reserve(kSystemsPerGalaxy);
  for (int i = 0; i < kSystemsPerGalaxy; ++i) {
    Random r(0x9E3779B97F4A7C15ULL * static_cast<std::uint64_t>(number_ + 1) +
             static_cast<std::uint64_t>(i) * 0xD1B54A32D192ED03ULL);
    StarSystem s;
    s.galaxy = number_;
    s.index = i;
    // Systems never share a place on the chart or a name
    do {
      s.x = static_cast<int>(r.Below(256));
      s.y = static_cast<int>(r.Below(128));
    } while (!taken.insert({s.x, s.y}).second);
    do {
      s.name = MakeName(r);
    } while (!names.insert(s.name).second);

    s.economy = static_cast<int>(r.Below(8));
    s.government = static_cast<int>(r.Below(8));
    // Anarchies and feudal states are never rich
    if (s.government <= 1) {
      s.economy |= 2;
    }
    const int flipped = (s.economy ^ 7) & 7;
    const int tech = flipped + static_cast<int>(r.Below(4)) + (s.government + 1) / 2;
    s.tech_level = tech + 1;
    s.population = tech * 4 + s.economy + s.government + 1;
    s.productivity = (flipped + 3) * (s.government + 4) * s.population * 8;
    s.radius_km = (static_cast<int>(r.Below(16)) + 11) * 256 + static_cast<int>(r.Below(256));
    s.species = MakeSpecies(r);
    s.seed = static_cast<std::uint32_t>(r.Next());
    systems_.push_back(std::move(s));
  }
}

double Galaxy::DistanceLy(int a, int b) const {
  const StarSystem& p = system(a);
  const StarSystem& q = system(b);
  return 0.25 * std::hypot(static_cast<double>(p.x - q.x), static_cast<double>(p.y - q.y));
}

std::vector<int> Galaxy::InRange(int from, double ly) const {
  std::vector<int> found;
  for (int i = 0; i < static_cast<int>(systems_.size()); ++i) {
    if (i != from && DistanceLy(from, i) <= ly) {
      found.push_back(i);
    }
  }
  std::sort(found.begin(), found.end(), [&](int a, int b) { return DistanceLy(from, a) < DistanceLy(from, b); });
  return found;
}

int Galaxy::Find(const std::string& name) const {
  const std::string wanted = Lower(name);
  for (const StarSystem& s : systems_) {
    if (Lower(s.name) == wanted) {
      return s.index;
    }
  }
  return -1;
}

int Galaxy::Nearest(int x, int y) const {
  int best = 0;
  double best_distance = 1e30;
  for (const StarSystem& s : systems_) {
    const double d = std::hypot(static_cast<double>(s.x - x), static_cast<double>(s.y - y));
    if (d < best_distance) {
      best_distance = d;
      best = s.index;
    }
  }
  return best;
}

}  // namespace coriolis
