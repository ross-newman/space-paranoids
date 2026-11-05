#include "coriolis/missions.h"

#include <cstdio>

namespace coriolis {

namespace {

struct StepInfo {
  MissionKind kind;
  int count;
  /// In tenths of a credit; the last step pays in a hyperdrive instead
  int reward;
  const char* title;
  const char* done;
};

const StepInfo kSteps[kStorySteps] = {
    {MissionKind::kDeliver, 0, 1500, "THE SEALED PACKET",
     "The packet is taken without a word, and the payment is in your account."},
    {MissionKind::kHunt, 3, 4000, "CLEAR THE LANES",
     "The station pays out for the pirates you shot down."},
    {MissionKind::kDeliver, 0, 8000, "THE SECOND PACKET",
     "The woman's contact reads the packet twice and goes pale. She was right to be afraid."},
    {MissionKind::kHunt, 8, 20000, "THE WARLORD'S FLEET",
     "With his fleet broken the warlord has gone to ground. The lanes are quiet again."},
    {MissionKind::kDeliver, 0, 0, "BEYOND THE RIM",
     "The engineer fits a galactic hyperdrive to your ship. The next galaxy is one jump away: press J in flight."},
};

std::string Credits(int tenths) {
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%d CR", tenths / 10);
  return buffer;
}

}  // namespace

bool MissionOffered(const Commander& c, const Galaxy& galaxy, Mission* out) {
  if (c.story_step < 0 || c.story_step >= kStorySteps) {
    return false;
  }
  const StepInfo& info = kSteps[c.story_step];
  Mission m;
  m.kind = info.kind;
  m.step = c.story_step;
  m.count = info.count;
  m.reward = info.reward;
  if (info.kind == MissionKind::kDeliver) {
    const std::vector<int> near = galaxy.InRange(c.system, kMaxFuel);
    if (near.empty()) {
      return false;
    }
    const std::size_t n = near.size();
    const std::size_t from = static_cast<std::size_t>(c.system);
    if (c.story_step == 0) {
      m.destination = near[(from * 7 + 3) % n];
    } else if (c.story_step == 2) {
      m.destination = near[(from * 5 + 1) % n];
    } else {
      // The last delivery is the longest one
      m.destination = near.back();
    }
  }
  *out = m;
  return true;
}

std::string MissionTitle(const Mission& m) {
  return m.step >= 0 && m.step < kStorySteps ? kSteps[m.step].title : "";
}

std::vector<std::string> MissionBriefing(const Mission& m, const Galaxy& galaxy) {
  const std::string to = m.destination >= 0 ? galaxy.system(m.destination).name : std::string();
  std::string pay = m.reward > 0 ? Credits(m.reward) : std::string("A GALACTIC HYPERDRIVE");
  std::vector<std::string> lines;
  switch (m.step) {
    case 0:
      lines = {"A woman at the bar asks you to carry a sealed packet to " + to + ".",
               "She will not say what is in it, only that it must not be opened",
               "and that you will be paid when it arrives."};
      break;
    case 1:
      lines = {"Pirates have been taking traders on these routes. The station",
               "pays for the ones that stop: shoot down " + std::to_string(m.count) + " pirate ships, then dock",
               "at any station to collect."};
      break;
    case 2:
      lines = {"The woman again. Someone is following her, and she has another",
               "packet for " + to + ". Take it, and mind your back."};
      break;
    case 3:
      lines = {"The pirates are being organised. Their leader keeps out of sight,",
               "but his fleet can be broken: shoot down " + std::to_string(m.count) + " pirate ships,",
               "then dock at any station."};
      break;
    case 4:
      lines = {"The packets held the plans of a galactic hyperdrive. Take them to",
               to + ", where an engineer will build one for you."};
      break;
    default:
      break;
  }
  lines.push_back("");
  lines.push_back("PAYMENT: " + pay);
  return lines;
}

std::string MissionStatus(const Mission& m, const Galaxy& galaxy) {
  if (m.kind == MissionKind::kDeliver) {
    return "DELIVER THE PACKET TO " + (m.destination >= 0 ? galaxy.system(m.destination).name : std::string("?"));
  }
  if (m.kind == MissionKind::kHunt) {
    return "SHOOT DOWN PIRATES: " + std::to_string(m.progress) + " OF " + std::to_string(m.count) +
           (m.progress >= m.count ? ", THEN DOCK" : "");
  }
  return "";
}

std::string MissionDone(const Mission& m) { return m.step >= 0 && m.step < kStorySteps ? kSteps[m.step].done : ""; }

bool MissionComplete(const Mission& m, int system) {
  switch (m.kind) {
    case MissionKind::kDeliver:
      return system == m.destination;
    case MissionKind::kHunt:
      return m.progress >= m.count;
    case MissionKind::kNone:
      break;
  }
  return false;
}

void PayMission(Commander& c) {
  const Mission m = c.mission;
  c.mission = Mission{};
  c.story_step = m.step + 1;
  if (m.reward > 0) {
    c.credits += m.reward;
  } else if (m.step == kStorySteps - 1) {
    if (c.equipment.galactic_hyperdrive) {
      c.credits += ItemPrice(Item::kGalacticHyperdrive);
    } else {
      c.equipment.galactic_hyperdrive = true;
    }
  }
}

}  // namespace coriolis
