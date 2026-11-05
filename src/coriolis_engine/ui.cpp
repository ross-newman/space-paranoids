#include "coriolis/ui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "coriolis/blueprint.h"
#include "coriolis/commander.h"
#include "coriolis/hash.h"
#include "coriolis/renderer.h"
#include "coriolis/screens.h"

namespace coriolis {

namespace {

const Rgb kTitleColour{255, 230, 120};
const Rgb kText{170, 230, 190};
const Rgb kDim{100, 130, 115};
const Rgb kBright{255, 255, 255};

std::int64_t TextWidth(const std::string& text, float scale) {
  return static_cast<std::int64_t>(static_cast<float>(text.size()) * 6.0f * scale);
}

void DrawCentred(Image& image, const std::string& text, std::int64_t y, float scale, Rgb colour) {
  DrawText(image, text, (static_cast<std::int64_t>(image.width()) - TextWidth(text, scale)) / 2, y, scale, colour);
}

void FillRows(Image& image, std::int64_t y0, std::int64_t y1, Rgb colour) {
  for (std::int64_t y = y0; y < y1; ++y) {
    for (std::int64_t x = 0; x < static_cast<std::int64_t>(image.width()); ++x) {
      image.Set(x, y, colour);
    }
  }
}

struct Pt {
  double x;
  double y;
};

/// A line of points with each corner rounded by a curve of radius r. An end of an open line may give up its whole
/// length to a corner, any other side only half of it.
std::vector<Pt> Rounded(const std::vector<Pt>& v, double r, bool closed) {
  std::vector<Pt> out;
  const std::size_t n = v.size();
  for (std::size_t i = 0; i < n; ++i) {
    if (!closed && (i == 0 || i + 1 == n)) {
      out.push_back(v[i]);
      continue;
    }
    const std::size_t ia = (i + n - 1) % n;
    const std::size_t ib = (i + 1) % n;
    const Pt c = v[i];
    auto toward = [&](std::size_t j) {
      const double dx = v[j].x - c.x;
      const double dy = v[j].y - c.y;
      const double len = std::hypot(dx, dy);
      const bool whole = !closed && (j == 0 || j + 1 == n);
      const double d = std::min(r, len * (whole ? 1.0 : 0.5));
      return Pt{c.x + dx / len * d, c.y + dy / len * d};
    };
    const Pt p1 = toward(ia);
    const Pt p2 = toward(ib);
    for (int k = 0; k <= 8; ++k) {
      const double t = k / 8.0;
      const double a = (1 - t) * (1 - t);
      const double b = 2 * t * (1 - t);
      const double d = t * t;
      out.push_back({a * p1.x + b * c.x + d * p2.x, a * p1.y + b * c.y + d * p2.y});
    }
  }
  if (closed && !out.empty()) {
    out.push_back(out.front());
  }
  return out;
}

/// A round ended stroke from a to b, anti-aliased, its colour fading from top to bottom between the rows y0 and y1
void StrokeLine(Image& image, Pt a, Pt b, double radius, Rgb top, Rgb bottom, double y0, double y1) {
  const std::int64_t w = static_cast<std::int64_t>(image.width());
  const std::int64_t h = static_cast<std::int64_t>(image.height());
  const std::int64_t xa = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(std::min(a.x, b.x) - radius - 1)));
  const std::int64_t xb = std::min<std::int64_t>(w - 1, static_cast<std::int64_t>(std::ceil(std::max(a.x, b.x) + radius + 1)));
  const std::int64_t ya = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(std::min(a.y, b.y) - radius - 1)));
  const std::int64_t yb = std::min<std::int64_t>(h - 1, static_cast<std::int64_t>(std::ceil(std::max(a.y, b.y) + radius + 1)));
  const double dx = b.x - a.x;
  const double dy = b.y - a.y;
  const double len2 = dx * dx + dy * dy;
  for (std::int64_t y = ya; y <= yb; ++y) {
    for (std::int64_t x = xa; x <= xb; ++x) {
      const double px = x + 0.5;
      const double py = y + 0.5;
      const double u = len2 > 0 ? std::clamp(((px - a.x) * dx + (py - a.y) * dy) / len2, 0.0, 1.0) : 0.0;
      const double d = std::hypot(px - (a.x + u * dx), py - (a.y + u * dy));
      const double cover = std::clamp(radius + 0.5 - d, 0.0, 1.0);
      if (cover <= 0) {
        continue;
      }
      const double f = y1 > y0 ? std::clamp((py - y0) / (y1 - y0), 0.0, 1.0) : 0.0;
      std::uint8_t* p = image.data() + (static_cast<std::size_t>(y) * image.width() + static_cast<std::size_t>(x)) * 3;
      const double c[3] = {top.r + (bottom.r - top.r) * f, top.g + (bottom.g - top.g) * f,
                           top.b + (bottom.b - top.b) * f};
      for (int i = 0; i < 3; ++i) {
        p[i] = static_cast<std::uint8_t>(p[i] + (c[i] - p[i]) * cover);
      }
    }
  }
}

/// One letter of the title as strokes, in units where the letter is 1 high; width is how wide it is
struct Letter {
  double width;
  std::vector<std::vector<Pt>> strokes;
};

Letter TitleLetter(char c) {
  const double r = 0.26;
  switch (c) {
    case 'C': {
      const double w = 0.95;
      return {w, {Rounded({{w, 0.34}, {w, 0}, {0, 0}, {0, 1}, {w, 1}, {w, 0.66}}, r, false)}};
    }
    case 'O': {
      const double w = 1.0;
      return {w, {Rounded({{0, 0}, {w, 0}, {w, 1}, {0, 1}}, r, true)}};
    }
    case 'R': {
      const double w = 0.9;
      return {w,
              {Rounded({{0, 1}, {0, 0}, {w, 0}, {w, 0.55}, {0, 0.55}}, r, false), {{0.4 * w, 0.55}, {w, 1}}}};
    }
    case 'L': {
      const double w = 0.8;
      return {w, {Rounded({{0, 0}, {0, 1}, {w, 1}}, 0.12, false)}};
    }
    case 'S': {
      const double w = 0.9;
      return {w,
              {Rounded({{w, 0.26}, {w, 0}, {0, 0}, {0, 0.5}, {w, 0.5}, {w, 1}, {0, 1}, {0, 0.74}}, 0.22, false)}};
    }
    default:  // I
      return {0.0, {{{0, 0}, {0, 1}}}};
  }
}

/// The word CORIOLIS in wide, rounded, gold letters like the logo, centred, with its top at y and about the given
/// height, made smaller if it would not fit across 90% of the picture. Drawn from strokes, so it is the same shape at
/// any size and needs no font.
void DrawLogoTitle(Image& image, std::int64_t y, std::int64_t height) {
  const char* word = "CORIOLIS";
  const double weight = 0.15;
  const double gap = 0.5;
  std::vector<Letter> letters;
  double units = 0;
  for (const char* p = word; *p != '\0'; ++p) {
    letters.push_back(TitleLetter(*p));
    units += letters.back().width + (p == word ? 0.0 : gap);
  }
  const double s = std::min(height / (1.0 + weight), 0.9 * image.width() / (units + weight));
  const double radius = 0.5 * weight * s;
  const double left = (image.width() - units * s) / 2.0;
  const double top = y + radius + (height - (1.0 + weight) * s) / 2.0;
  const double y1 = top + s;
  for (int pass = 0; pass < 2; ++pass) {
    const double shift = pass == 0 ? 0.05 * s : 0.0;
    const Rgb a = pass == 0 ? Rgb{90, 55, 10} : Rgb{255, 238, 150};
    const Rgb b = pass == 0 ? Rgb{90, 55, 10} : Rgb{200, 140, 30};
    double x = left;
    for (const Letter& letter : letters) {
      for (const std::vector<Pt>& line : letter.strokes) {
        for (std::size_t i = 0; i + 1 < line.size(); ++i) {
          const Pt p{x + line[i].x * s + shift, top + line[i].y * s + shift};
          const Pt q{x + line[i + 1].x * s + shift, top + line[i + 1].y * s + shift};
          StrokeLine(image, p, q, radius, a, b, top - radius, y1 + radius);
        }
      }
      x += (letter.width + gap) * s;
    }
  }
}

/// The sizes of text that the title, the pause menu and the end screen all use, so they match: a heading, the items
/// of a menu or a table, and small print. They are fixed shares of the screen's text scale, so they grow with the
/// picture at any resolution.
struct TextSizes {
  float heading;
  float item;
  float small;
};

TextSizes SizesFor(const Image& image) {
  const float s = UiScale(image);
  return {4.0f * s, 1.6f * s, 4.0f / 3.0f * s};
}

/// The scale, made smaller if the text would not fit across 90% of the picture
float FitScale(const Image& image, const std::string& text, float scale) {
  const float room = static_cast<float>(image.width()) * 0.9f;
  const float chars = static_cast<float>(std::max<std::size_t>(1, text.size()));
  return std::max(1.0f, std::min(scale, room / (chars * 6.0f)));
}

void DrawCentredFit(Image& image, const std::string& text, std::int64_t y, float scale, Rgb colour) {
  DrawCentred(image, text, y, FitScale(image, text, scale), colour);
}

Rgb Scaled(Rgb colour, double f) {
  f = std::clamp(f, 0.0, 1.0);
  return {static_cast<std::uint8_t>(colour.r * f), static_cast<std::uint8_t>(colour.g * f),
          static_cast<std::uint8_t>(colour.b * f)};
}

/// A menu: one row per item, the chosen one in brackets and bright. Returns the y below the last row.
std::int64_t DrawMenu(Image& image, const std::vector<std::string>& items, int selected, std::int64_t y,
                      float scale) {
  for (std::size_t i = 0; i < items.size(); ++i) {
    const bool here = static_cast<int>(i) == selected;
    const std::string text = std::string(here ? "> " : "  ") + items[i] + (here ? " <" : "  ");
    DrawCentredFit(image, text, y, scale, here ? kBright : kDim);
    y += static_cast<std::int64_t>(9 * scale);
  }
  return y;
}

std::string CreditsText(std::int64_t tenths) {
  char buffer[48];
  std::snprintf(buffer, sizeof buffer, "%lld.%lld CR", static_cast<long long>(tenths / 10),
                static_cast<long long>(tenths % 10));
  return buffer;
}

std::string Upper(std::string text) {
  for (char& c : text) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return text;
}

/// The ship broken into its triangles, which fly apart by `amount` and tumble as they go: the wreck on the end screen.
/// The model has to have its faces wound outwards (as a blueprint's are) or have the centres of its meshes.
Model Shattered(const Model& source, float amount) {
  Model out;
  out.name = source.name;
  out.vertices.reserve(source.faces.size() * 3);
  out.faces.reserve(source.faces.size());
  const auto sub = [](const Vec3& a, const Vec3& b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; };
  const auto dot = [](const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
  const auto cross = [](const Vec3& a, const Vec3& b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
  };
  const auto fraction = [](std::uint32_t h) { return static_cast<float>(h % 1000U) / 999.0f; };
  for (std::size_t i = 0; i < source.faces.size(); ++i) {
    const Face& f = source.faces[i];
    Vec3 p[3] = {source.vertices[f.v[0]], source.vertices[f.v[1]], source.vertices[f.v[2]]};
    std::uint8_t edges = f.edges;
    const Vec3 mid{(p[0].x + p[1].x + p[2].x) / 3.0f, (p[0].y + p[1].y + p[2].y) / 3.0f,
                   (p[0].z + p[1].z + p[2].z) / 3.0f};
    if (dot(cross(sub(p[1], p[0]), sub(p[2], p[0])), sub(mid, f.centre)) < 0.0f) {
      // Turn the face round: v1 and v2 swap, and so do the outlines of the edges that touch them
      std::swap(p[1], p[2]);
      edges = static_cast<std::uint8_t>(((f.edges & 4) ? 1 : 0) | (f.edges & 2) | ((f.edges & 1) ? 4 : 0));
    }
    const std::uint32_t h1 = Hash32(static_cast<std::uint32_t>(i) * 2654435761U + 17U);
    const std::uint32_t h2 = Hash32(h1 + 99U);
    const std::uint32_t h3 = Hash32(h2 + 7U);
    const std::uint32_t h4 = Hash32(h3 + 1234U);
    // Away from the middle of the ship, by a different distance for each piece
    Vec3 dir = mid;
    const float length = std::sqrt(dot(mid, mid));
    dir = length < 1e-4f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{mid.x / length, mid.y / length, mid.z / length};
    const float dist = amount * (0.5f + 1.5f * fraction(h1));
    Vec3 axis{fraction(h2) * 2.0f - 1.0f, fraction(h3) * 2.0f - 1.0f, fraction(h4) * 2.0f - 1.0f};
    const float axis_length = std::sqrt(dot(axis, axis));
    axis = axis_length < 1e-3f ? Vec3{0.0f, 1.0f, 0.0f}
                               : Vec3{axis.x / axis_length, axis.y / axis_length, axis.z / axis_length};
    const float angle = amount * 3.0f * (fraction(h4) * 2.0f - 1.0f);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    Vec3 q[3];
    for (int k = 0; k < 3; ++k) {
      const Vec3 r = sub(p[k], mid);
      const Vec3 ar = cross(axis, r);
      const float d = dot(axis, r) * (1.0f - c);
      q[k] = {mid.x + dir.x * dist + r.x * c + ar.x * s + axis.x * d, mid.y + dir.y * dist + r.y * c + ar.y * s + axis.y * d,
              mid.z + dir.z * dist + r.z * c + ar.z * s + axis.z * d};
    }
    const Vec3 n = cross(sub(q[1], q[0]), sub(q[2], q[0]));
    const float nl = std::sqrt(dot(n, n));
    if (nl < 1e-9f) {
      continue;
    }
    // The renderer turns a normal away from the face's centre: put the centre just behind the face
    const Vec3 nm{(q[0].x + q[1].x + q[2].x) / 3.0f, (q[0].y + q[1].y + q[2].y) / 3.0f,
                  (q[0].z + q[1].z + q[2].z) / 3.0f};
    const std::size_t base = out.vertices.size();
    out.vertices.push_back(q[0]);
    out.vertices.push_back(q[1]);
    out.vertices.push_back(q[2]);
    out.faces.push_back({{base, base + 1, base + 2}, f.colour, f.emissive, edges,
                         {nm.x - n.x / nl, nm.y - n.y / nl, nm.z - n.z / nl}});
  }
  return out;
}

const Model& TitleModel() {
  static const Model model = BlueprintToModel(*FindBuiltInBlueprint("cobra_mk3_trader"));
  return model;
}

const Screen kDockedScreens[] = {Screen::kStatus, Screen::kMarket, Screen::kEquipment, Screen::kShipyard,
                                 Screen::kMissions, Screen::kChart, Screen::kSystem};

}  // namespace

GameUi::GameUi(std::string save_path, std::uint64_t seed)
    : save_path_(std::move(save_path)), game_(Commander{}, seed), title_model_(TitleModel()) {
  if (!save_path_.empty()) {
    Commander probe;
    std::string error;
    has_save_ = LoadCommander(save_path_, &probe, &error);
  }
}

float GameUi::TakeRumble() {
  const float r = rumble_;
  rumble_ = 0.0f;
  return r;
}

std::vector<SoundCue> GameUi::TakeSounds() {
  std::vector<SoundCue> out;
  out.swap(sounds_);
  return out;
}

void GameUi::Cue(SoundEffect effect, float gain) {
  if (gain > 0.02f && sounds_.size() < 64) {
    sounds_.push_back({effect, std::min(gain, 1.0f)});
  }
}

/// The clicks of the menus. The arrow keys are flight controls in flight, so they only click in a menu. Buying and
/// selling make their own sounds.
void GameUi::CueKey(UiKey key) {
  const bool flying = screen_ == Screen::kFlight && !paused_ && !chart_open_ && !help_open_;
  if (flying) {
    return;
  }
  const bool trading = screen_ == Screen::kMarket || screen_ == Screen::kEquipment || screen_ == Screen::kShipyard;
  switch (key) {
    case UiKey::kUp:
    case UiKey::kDown:
    case UiKey::kNextTab:
    case UiKey::kPrevTab:
      Cue(SoundEffect::kUiMove, 0.8f);
      break;
    case UiKey::kLeft:
    case UiKey::kRight:
      if (screen_ != Screen::kMarket) {
        Cue(SoundEffect::kUiMove, 0.8f);
      }
      break;
    case UiKey::kBack:
      Cue(SoundEffect::kUiBack, 0.8f);
      break;
    case UiKey::kAccept:
    case UiKey::kAlternate:
      if (!trading) {
        Cue(SoundEffect::kUiSelect, 0.8f);
      }
      break;
    case UiKey::kSave:
      Cue(SoundEffect::kUiSelect, 0.8f);
      break;
    default:
      break;
  }
}

/// Turn what happened in the world into sounds. Things far from the player are quiet, and very far ones silent.
void GameUi::CueEvents(const std::vector<Event>& events) {
  const EntityId me = game_.world().player();
  const Entity* player = game_.player();
  const auto near = [&](const Vec3d& where) {
    if (player == nullptr) {
      return 1.0f;
    }
    return static_cast<float>(std::clamp(1.0 - Distance(where, player->position) / 9000.0, 0.0, 1.0));
  };
  // Which laser the player has decides what the player's own shots sound like
  const int laser = game_.commander().equipment.laser_level;
  for (const Event& e : events) {
    const bool mine = e.a == me || (e.kind == EventKind::kCollision && e.b == me);
    const float loud = near(e.position);
    switch (e.kind) {
      case EventKind::kLaserFired:
        if (mine) {
          Cue(laser >= 2 ? SoundEffect::kLaserMilitary : (laser == 1 ? SoundEffect::kLaserBeam : SoundEffect::kLaserPulse), 0.8f);
        } else {
          Cue(SoundEffect::kEnemyLaser, 0.6f * loud);
        }
        break;
      case EventKind::kShieldHit:
        Cue(SoundEffect::kShieldHit, mine ? 0.9f : 0.45f * loud);
        break;
      case EventKind::kHullHit:
        Cue(SoundEffect::kHullHit, mine ? 1.0f : 0.5f * loud);
        break;
      case EventKind::kSubsystemDestroyed:
        Cue(mine ? SoundEffect::kShieldsDown : SoundEffect::kHullHit, mine ? 0.9f : 0.7f * loud);
        break;
      case EventKind::kDestroyed:
        if (mine) {
          Cue(SoundEffect::kDeath, 1.0f);
        } else if (e.entity_kind == EntityKind::kShip || e.entity_kind == EntityKind::kStation) {
          Cue(SoundEffect::kShipExplode, loud);
          // A pirate or alien the player shot down pays a bounty
          if (e.b == me && (e.team == Team::kPirate || e.team == Team::kAlien)) {
            Cue(SoundEffect::kBounty, 0.7f);
          }
        } else if (e.entity_kind != EntityKind::kMissile) {
          Cue(SoundEffect::kRockBreak, 0.8f * loud);
        }
        break;
      case EventKind::kMissileLaunched:
        Cue(SoundEffect::kMissileLaunch, mine ? 0.9f : 0.6f * loud);
        break;
      case EventKind::kMissileDetonated:
        Cue(SoundEffect::kMissileBlast, 0.9f * loud);
        break;
      case EventKind::kEcm:
        Cue(SoundEffect::kEcmPulse, mine ? 0.9f : 0.5f * loud);
        break;
      case EventKind::kEnergyBomb:
        Cue(SoundEffect::kEnergyBomb, 1.0f);
        break;
      case EventKind::kDocked:
        if (mine) {
          Cue(SoundEffect::kDocking, 0.9f);
        }
        break;
      case EventKind::kCollision:
        if (mine) {
          Cue(SoundEffect::kHullHit, 0.7f);
        }
        break;
      case EventKind::kScooped:
        if (mine) {
          Cue(SoundEffect::kCargoScoop, 0.9f);
        }
        break;
      case EventKind::kCrashed:
        if (mine) {
          Cue(SoundEffect::kShipExplode, 1.0f);
        }
        break;
    }
  }
}

/// Sounds for what the game says: a refusal is a buzzer, a mass lock its own warning, a new rank a fanfare
void GameUi::CueMessages(const std::vector<std::string>& messages) {
  for (std::string text : messages) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto has = [&](const char* part) { return text.find(part) != std::string::npos; };
    if (has("mass lock")) {
      Cue(SoundEffect::kMassLock, 0.9f);
    } else if (has("right on, commander")) {
      Cue(SoundEffect::kRankUp, 0.9f);
    } else if (has("not enough fuel") || has("no destination") || has("no docking computer") || has("no missile") ||
               has("no ecm") || has("needs more energy") || has("no energy bomb") || has("no escape pod") ||
               has("no target") || has("cannot")) {
      Cue(SoundEffect::kError, 0.6f);
    }
  }
}

/// Sounds for changes in the ship's state that are not events of the world: a missile locked, a target chosen, the
/// docking computer, and the warnings, which repeat for as long as the trouble lasts
void GameUi::CueState(double seconds) {
  const Entity* me = game_.player();
  if (me == nullptr) {
    return;
  }
  if (game_.missile_lock() != kNoEntity && game_.missile_lock() != last_lock_) {
    Cue(SoundEffect::kMissileLock, 0.8f);
  }
  last_lock_ = game_.missile_lock();
  if (game_.target() != kNoEntity && game_.target() != last_target_) {
    Cue(SoundEffect::kTargetBlip, 0.8f);
  }
  last_target_ = game_.target();
  if (game_.autopilot() != last_autopilot_) {
    Cue(game_.autopilot() ? SoundEffect::kDockComputerOn : SoundEffect::kDockComputerOff, 0.8f);
    last_autopilot_ = game_.autopilot();
  }
  if (me->shield_max > 0.0 && last_shield_ > 0.0 && me->shield <= 0.0) {
    Cue(SoundEffect::kShieldsDown, 0.9f);
  }
  last_shield_ = me->shield;
  const int legal = game_.commander().legal_status;
  if (legal >= 50 && last_legal_ < 50) {
    Cue(SoundEffect::kPoliceSiren, 0.8f);
  } else if (legal > 0 && last_legal_ == 0) {
    Cue(SoundEffect::kError, 0.5f);
  }
  last_legal_ = legal;
  // A failing hull is an alarm every couple of seconds, low energy a double beep
  alarm_timer_ -= seconds;
  if (alarm_timer_ <= 0.0 && !game_.launching() && !game_.landing()) {
    if (me->hull < 0.3 * me->hull_max) {
      Cue(SoundEffect::kHullAlarm, 0.8f);
      alarm_timer_ = 2.5;
    } else if (me->energy < 0.2 * me->energy_max) {
      Cue(SoundEffect::kLowEnergy, 0.7f);
      alarm_timer_ = 2.0;
    }
  }
}

/// How loud the engine drone is to be, 0 to 1, or below 0 when there is none: it is heard while flying, and runs up
/// through the launch
float GameUi::EngineLevel() const {
  if (screen_ != Screen::kFlight || paused_ || chart_open_ || help_open_) {
    return -1.0f;
  }
  if (game_.launching()) {
    return 0.25f + 0.75f * static_cast<float>(game_.launch_progress());
  }
  if (game_.landing()) {
    // The engines wind down as the ship is set on the floor
    return 0.25f * (1.0f - static_cast<float>(game_.landing_progress()));
  }
  return static_cast<float>(game_.throttle());
}

/// How loud the station music is to be: it plays in the station, whatever the screen (status, trading, equipment, the
/// charts), and not on the title, in flight or on the end screen. The pause menu leaves it playing, quietly.
float GameUi::MusicLevel(MusicTrack track) const {
  switch (track) {
    case MusicTrack::kStation:
      switch (screen_) {
        case Screen::kStatus:
        case Screen::kMarket:
        case Screen::kEquipment:
        case Screen::kShipyard:
        case Screen::kMissions:
        case Screen::kChart:
        case Screen::kSystem:
          return paused_ ? 0.2f : 0.5f;
        default:
          return -1.0f;
      }
    case MusicTrack::kTitle:
      return screen_ == Screen::kTitle ? 0.5f : -1.0f;
    case MusicTrack::kJump:
      if (screen_ == Screen::kFlight && (game_.jump_countdown() > 0.0 || game_.hyperspace_warp() > 0.0)) {
        return paused_ ? 0.15f : 0.45f;
      }
      return -1.0f;
    case MusicTrack::kFight:
      if (screen_ == Screen::kFlight && fight_hold_ > 0.0 && game_.jump_countdown() <= 0.0 &&
          game_.hyperspace_warp() <= 0.0) {
        return paused_ ? 0.15f : 0.4f;
      }
      return -1.0f;
  }
  return -1.0f;
}

/// Is a hostile ship or missile close: pirates and aliens, and the police when the pilot is wanted
bool GameUi::InCombat() const {
  const Entity* me = game_.player();
  if (me == nullptr) {
    return false;
  }
  for (const Entity& e : game_.world().entities()) {
    if (!e.alive || e.id == me->id || (e.kind != EntityKind::kShip && e.kind != EntityKind::kMissile)) {
      continue;
    }
    const bool hostile =
        e.team == Team::kPirate || e.team == Team::kAlien || (e.team == Team::kPolice && game_.world().player_wanted());
    if (hostile && Distance(e.position, me->position) < 9000.0) {
      return true;
    }
  }
  return false;
}

void GameUi::Say(const std::string& text) {
  message_ = text;
  message_age_ = 0.0;
}

bool GameUi::SaveNow() {
  if (save_path_.empty()) {
    return false;
  }
  std::string error;
  if (!SaveCommander(game_.commander(), save_path_, &error)) {
    Say("Could not save: " + error);
    return false;
  }
  has_save_ = true;
  Say("Game saved");
  return true;
}

void GameUi::StartNewGame() {
  Commander c;
  c.name = "Jameson";
  const int lave = game_.galaxy().Find("Lave");
  c.system = lave >= 0 ? lave : 0;
  game_.NewGame(c);
  in_progress_ = true;
  paused_ = false;
  chart_open_ = false;
  market_row_ = 0;
  equipment_row_ = 0;
  shipyard_row_ = 0;
  game_.TakeMessages();
  Say("Welcome to " + game_.system().name + ", Commander");
  Cue(SoundEffect::kTitleSting, 0.8f);
  ShowDockedScreen(Screen::kStatus);
}

void GameUi::Continue() {
  Commander c;
  std::string error;
  if (!LoadCommander(save_path_, &c, &error)) {
    Say("Could not load: " + error);
    return;
  }
  game_.NewGame(c);
  in_progress_ = true;
  paused_ = false;
  chart_open_ = false;
  game_.TakeMessages();
  Say("Welcome back, Commander " + c.name);
  ShowDockedScreen(Screen::kStatus);
}

void GameUi::Resume() {
  paused_ = false;
  chart_open_ = false;
  if (game_.mode() == GameMode::kFlight) {
    screen_ = Screen::kFlight;
  } else {
    ShowDockedScreen(Screen::kStatus);
  }
}

void GameUi::ShowDockedScreen(Screen screen) { screen_ = screen; }

int GameUi::TitleItemCount() const { return 2 + (in_progress_ ? 1 : 0) + (has_save_ ? 1 : 0); }

std::string GameUi::TitleItem(int index) const {
  std::vector<std::string> items;
  if (in_progress_) {
    items.push_back("RESUME");
  }
  if (has_save_) {
    items.push_back("CONTINUE SAVED GAME");
  }
  items.push_back("NEW GAME");
  items.push_back("QUIT");
  return items.at(static_cast<std::size_t>(index));
}

void GameUi::ChooseTitleItem(int index) {
  const std::string item = TitleItem(index);
  if (item == "RESUME") {
    Resume();
  } else if (item == "CONTINUE SAVED GAME") {
    Continue();
  } else if (item == "NEW GAME") {
    StartNewGame();
  } else {
    quit_ = true;
  }
}

void GameUi::MoveDestination(int dx, int dy) {
  const Galaxy& galaxy = game_.galaxy();
  const int from = game_.selected_system() >= 0 ? game_.selected_system() : game_.commander().system;
  const StarSystem& a = galaxy.system(from);
  int best = -1;
  double best_score = 1e18;
  for (const StarSystem& s : galaxy.systems()) {
    if (s.index == from) {
      continue;
    }
    const double vx = s.x - a.x;
    const double vy = s.y - a.y;
    const double along = vx * dx + vy * dy;
    const double across = std::fabs(dx != 0 ? vy : vx);
    // Only systems roughly in the direction of the key, the nearest first
    if (along <= 0.0 || across > along * 1.5) {
      continue;
    }
    const double score = std::hypot(vx, vy) + 0.5 * across;
    if (score < best_score) {
      best_score = score;
      best = s.index;
    }
  }
  if (best >= 0) {
    game_.SelectSystem(best);
  }
}

void GameUi::Key(UiKey key) {
  CueKey(key);
  switch (screen_) {
    case Screen::kTitle: {
      const int count = TitleItemCount();
      if (key == UiKey::kUp) {
        title_row_ = (title_row_ + count - 1) % count;
      } else if (key == UiKey::kDown) {
        title_row_ = (title_row_ + 1) % count;
      } else if (key == UiKey::kAccept) {
        ChooseTitleItem(std::clamp(title_row_, 0, count - 1));
      } else if (key == UiKey::kBack) {
        if (in_progress_) {
          Resume();
        } else {
          quit_ = true;
        }
      }
      return;
    }
    case Screen::kDead:
      // A moment before the keys count, so a press meant for the game does not skip the screen
      if (dead_age_ < 1.2) {
        return;
      }
      if (key == UiKey::kUp || key == UiKey::kDown) {
        const int count = DeadItemCount();
        dead_row_ = (dead_row_ + (key == UiKey::kUp ? count - 1 : 1)) % count;
      } else if (key == UiKey::kAccept) {
        ChooseDeadItem(std::clamp(dead_row_, 0, DeadItemCount() - 1));
      } else if (key == UiKey::kBack) {
        screen_ = Screen::kTitle;
        title_row_ = 0;
      }
      return;
    case Screen::kFlight:
      if (help_open_) {
        help_open_ = false;  // any key closes the list
        return;
      }
      if (chart_open_) {
        if (key == UiKey::kUp) {
          MoveDestination(0, -1);
        } else if (key == UiKey::kDown) {
          MoveDestination(0, 1);
        } else if (key == UiKey::kLeft) {
          MoveDestination(-1, 0);
        } else if (key == UiKey::kRight) {
          MoveDestination(1, 0);
        } else if (key == UiKey::kAlternate) {
          game_.SelectSystem(-1);
        } else if (key == UiKey::kAccept || key == UiKey::kBack) {
          chart_open_ = false;
        }
        return;
      }
      if (paused_) {
        if (key == UiKey::kUp || key == UiKey::kDown) {
          pause_row_ = 1 - pause_row_;
        } else if (key == UiKey::kAccept) {
          paused_ = false;
          if (pause_row_ == 1) {
            screen_ = Screen::kTitle;
            title_row_ = 0;
          }
        } else if (key == UiKey::kBack) {
          paused_ = false;
        }
        return;
      }
      if (key == UiKey::kBack) {
        paused_ = true;
        pause_row_ = 0;
      } else if (key == UiKey::kHelp) {
        help_open_ = true;
      } else if (key == UiKey::kFlightMode) {
        const bool classic = game_.flight_mode() == FlightMode::kClassic;
        game_.SetFlightMode(classic ? FlightMode::kNewtonian : FlightMode::kClassic);
        Say(classic ? "Newtonian flight" : "Classic flight");
      }
      return;
    default:
      break;
  }

  // In the station
  const int tabs = static_cast<int>(sizeof(kDockedScreens) / sizeof(kDockedScreens[0]));
  int tab = 0;
  for (int i = 0; i < tabs; ++i) {
    if (kDockedScreens[i] == screen_) {
      tab = i;
    }
  }
  switch (key) {
    case UiKey::kNextTab:
      ShowDockedScreen(kDockedScreens[(tab + 1) % tabs]);
      return;
    case UiKey::kPrevTab:
      ShowDockedScreen(kDockedScreens[(tab + tabs - 1) % tabs]);
      return;
    case UiKey::kBack:
      screen_ = Screen::kTitle;
      title_row_ = 0;
      return;
    case UiKey::kSave:
      SaveNow();
      return;
    case UiKey::kLaunch:
      if (game_.Launch()) {
        Cue(SoundEffect::kLaunchBay, 0.9f);
        screen_ = Screen::kFlight;
        paused_ = false;
        chart_open_ = false;
        Collect();
      }
      return;
    default:
      break;
  }

  if (screen_ == Screen::kMarket) {
    const int rows = static_cast<int>(kCommodityCount);
    const Commodity item = static_cast<Commodity>(market_row_);
    if (key == UiKey::kUp) {
      market_row_ = (market_row_ + rows - 1) % rows;
    } else if (key == UiKey::kDown) {
      market_row_ = (market_row_ + 1) % rows;
    } else if (key == UiKey::kAccept || key == UiKey::kRight) {
      const bool bought = game_.Buy(item, 1);
      Cue(bought ? SoundEffect::kTradeBuy : SoundEffect::kError, 0.8f);
      Say(bought ? std::string("Bought 1 ") + CommodityData(item).name
                 : "Cannot buy: check the cash, the hold and the stock");
    } else if (key == UiKey::kAlternate || key == UiKey::kLeft) {
      const bool sold = game_.Sell(item, 1);
      Cue(sold ? SoundEffect::kTradeSell : SoundEffect::kError, 0.8f);
      Say(sold ? std::string("Sold 1 ") + CommodityData(item).name : std::string("You have no ") + CommodityData(item).name);
    }
  } else if (screen_ == Screen::kEquipment) {
    const int rows = EquipmentCount();
    if (key == UiKey::kUp) {
      equipment_row_ = (equipment_row_ + rows - 1) % rows;
    } else if (key == UiKey::kDown) {
      equipment_row_ = (equipment_row_ + 1) % rows;
    } else if (key == UiKey::kAccept) {
      const Item item = EquipmentItem(equipment_row_);
      const bool fitted = game_.BuyItem(item);
      Cue(fitted ? SoundEffect::kEquipmentFit : SoundEffect::kError, 0.8f);
      Say(fitted ? std::string("Fitted ") + ItemName(item)
                 : "Cannot buy: not for sale here, already fitted, or too dear");
    }
  } else if (screen_ == Screen::kShipyard) {
    const int rows = ShipForSaleCount();
    if (key == UiKey::kUp) {
      shipyard_row_ = (shipyard_row_ + rows - 1) % rows;
    } else if (key == UiKey::kDown) {
      shipyard_row_ = (shipyard_row_ + 1) % rows;
    } else if (key == UiKey::kAccept) {
      const bool bought = game_.BuyShip(shipyard_row_);
      Cue(bought ? SoundEffect::kEquipmentFit : SoundEffect::kError, 0.8f);
      Say(bought ? std::string("Bought the ") + ShipForSaleAt(shipyard_row_).name
                 : "Cannot buy: not sold here, already yours, too dear, or the hold is too small for the cargo");
    }
  } else if (screen_ == Screen::kMissions) {
    if (key == UiKey::kAccept) {
      const bool taken = game_.AcceptMission();
      Cue(taken ? SoundEffect::kEquipmentFit : SoundEffect::kError, 0.8f);
    } else if (key == UiKey::kAlternate) {
      const bool gave_up = game_.AbandonMission();
      Cue(gave_up ? SoundEffect::kUiBack : SoundEffect::kError, 0.8f);
    }
  } else if (screen_ == Screen::kChart || screen_ == Screen::kSystem) {
    if (key == UiKey::kUp) {
      MoveDestination(0, -1);
    } else if (key == UiKey::kDown) {
      MoveDestination(0, 1);
    } else if (key == UiKey::kLeft) {
      MoveDestination(-1, 0);
    } else if (key == UiKey::kRight) {
      MoveDestination(1, 0);
    } else if (key == UiKey::kAlternate) {
      game_.SelectSystem(-1);
    }
  }
}

void GameUi::Collect() {
  const std::vector<Event> events = game_.TakeEvents();
  const std::vector<std::string> messages = game_.TakeMessages();
  if (screen_ == Screen::kFlight) {
    cockpit_.Notice(game_, events, messages);
    CueEvents(events);
    const EntityId me = game_.world().player();
    for (const Event& e : events) {
      if (e.a == me && e.kind == EventKind::kHullHit) {
        rumble_ = std::max(rumble_, 0.7f);
      } else if (e.a == me && e.kind == EventKind::kShieldHit) {
        rumble_ = std::max(rumble_, 0.3f);
      }
    }
  }
  CueMessages(messages);
  if (!messages.empty()) {
    Say(messages.back());
  }
}

void GameUi::Update(const FlightInput& input, double seconds) {
  time_ += seconds;
  ++frame_;
  message_age_ += seconds;
  if (screen_ == Screen::kDead) {
    dead_age_ += seconds;
    return;
  }
  if (screen_ != Screen::kFlight) {
    fight_hold_ = 0.0;
    return;
  }
  // A view key chosen again goes back to the front view, so a glance behind or to the side is one key to look and the
  // same key to look away
  const auto look = [this](CockpitView view) {
    cockpit_.set_view(cockpit_.view() == view && view != CockpitView::kFront ? CockpitView::kFront : view);
  };
  if (input.Pressed(Action::kViewFront)) {
    cockpit_.set_view(CockpitView::kFront);
  } else if (input.Pressed(Action::kViewRear)) {
    look(CockpitView::kRear);
  } else if (input.Pressed(Action::kViewLeft)) {
    look(CockpitView::kLeft);
  } else if (input.Pressed(Action::kViewRight)) {
    look(CockpitView::kRight);
  }
  if (input.Pressed(Action::kPause) && !chart_open_ && !help_open_) {
    paused_ = !paused_;
    pause_row_ = 0;
  }
  if (input.Pressed(Action::kGalacticChart) && !paused_ && !help_open_) {
    chart_open_ = !chart_open_;
  }
  if (!paused_ && !chart_open_ && !help_open_) {
    const int system_before = game_.commander().system;
    game_.Update(input, seconds);
    fight_hold_ = InCombat() ? 8.0 : std::max(0.0, fight_hold_ - seconds);
    const bool jumping = game_.jump_countdown() > 0.0;
    if (jumping && !was_jumping_) {
      Cue(SoundEffect::kHyperCharge);
    }
    was_jumping_ = jumping;
    if (game_.commander().system != system_before) {
      Cue(SoundEffect::kHyperExit, 0.9f);  // arrived
    }
    CueState(seconds);
    cockpit_.Advance(seconds);
  }
  Collect();
  if (game_.mode() == GameMode::kDocked) {
    screen_ = Screen::kStatus;
    SaveNow();
  } else if (game_.mode() == GameMode::kDead) {
    screen_ = Screen::kDead;
    in_progress_ = false;
    dead_age_ = 0.0;
    dead_row_ = 0;
    const Blueprint* ship = FindBuiltInBlueprint(game_.commander().ship);
    wreck_ = ship != nullptr ? BlueprintToModel(*ship) : title_model_;
  }
}

void GameUi::DrawBar(Image& image, const std::string& text) {
  const float scale = UiScale(image);
  const std::int64_t w = static_cast<std::int64_t>(image.width());
  const std::int64_t h = static_cast<std::int64_t>(image.height());
  const std::int64_t bar = static_cast<std::int64_t>(11.0f * scale);
  const std::int64_t pad = static_cast<std::int64_t>(4.0f * scale);
  // Text made smaller if it would run off the picture
  const auto fit = [&](const std::string& s) {
    return std::max(1.0f, std::min(scale, static_cast<float>(w - 2 * pad) / (6.0f * static_cast<float>(std::max<std::size_t>(1, s.size())))));
  };
  FillRows(image, h - bar, h, {2, 6, 4});
  FillRows(image, h - bar, h - bar + 1, {60, 110, 80});
  DrawText(image, text, pad, h - bar + static_cast<std::int64_t>(2.0f * scale), fit(text), kDim);
  if (message_age_ < 8.0 && !message_.empty()) {
    DrawText(image, message_, pad, h - bar - static_cast<std::int64_t>(10.0f * scale), fit(message_), {255, 232, 130});
  }
}

void GameUi::DrawTitle(Image& image) {
  image.Clear({2, 4, 9});
  DrawStars(image, frame_);
  const std::int64_t h = static_cast<std::int64_t>(image.height());
  const std::int64_t w = static_cast<std::int64_t>(image.width());
  // The ship turns slowly under the name
  const std::uint32_t tile_size = static_cast<std::uint32_t>(std::max<std::int64_t>(16, h * 60 / 100));
  Image tile(tile_size, tile_size);
  RenderOptions options;
  options.camera_distance = 10.0f;
  Renderer(options).DrawShip(tile, title_model_, static_cast<float>(time_) * 0.5f);
  const std::int64_t x0 = (w - tile_size) / 2;
  const std::int64_t y0 = h * 22 / 100;
  for (std::uint32_t y = 0; y < tile_size; ++y) {
    for (std::uint32_t x = 0; x < tile_size; ++x) {
      const std::uint8_t* p = tile.data() + (static_cast<std::size_t>(y) * tile_size + x) * 3;
      if (p[0] > 0 || p[1] > 0 || p[2] > 0) {
        image.Set(x0 + x, y0 + y, {p[0], p[1], p[2]});
      }
    }
  }
  const TextSizes sizes = SizesFor(image);
  const float big = sizes.heading;
  DrawLogoTitle(image, h * 6 / 100, static_cast<std::int64_t>(7.0f * big));
  const float small = sizes.small;
  DrawCentred(image, "A SPACE TRADING AND DOGFIGHT GAME", h * 6 / 100 + static_cast<std::int64_t>(9.0f * big), small,
              kDim);

  std::vector<std::string> items;
  for (int i = 0; i < TitleItemCount(); ++i) {
    items.push_back(TitleItem(i));
  }
  DrawMenu(image, items, title_row_, h * 70 / 100, sizes.item);
  if (message_age_ < 6.0 && !message_.empty()) {
    DrawCentred(image, message_, h - static_cast<std::int64_t>(18 * small), small, {255, 232, 130});
  }
  DrawCentred(image, "ARROWS AND ENTER, OR THE D-PAD AND A", h - static_cast<std::int64_t>(9 * small), small, kDim);
}

void GameUi::DrawDocked(Image& image) {
  std::string hint = "TAB NEXT SCREEN   L LAUNCH   S SAVE   ESC MENU";
  switch (screen_) {
    case Screen::kStatus:
      DrawStatusScreen(image, game_);
      break;
    case Screen::kMarket:
      DrawMarketScreen(image, game_, market_row_);
      hint = "UP DOWN CHOOSE   ENTER BUY   BACKSPACE SELL   TAB NEXT   L LAUNCH";
      break;
    case Screen::kEquipment:
      DrawEquipmentScreen(image, game_, equipment_row_, static_cast<float>(time_) * 0.8f);
      hint = "UP DOWN CHOOSE   ENTER BUY   TAB NEXT   L LAUNCH";
      break;
    case Screen::kShipyard:
      DrawShipyardScreen(image, game_, shipyard_row_, static_cast<float>(time_) * 0.8f);
      hint = "UP DOWN CHOOSE   ENTER BUY (TRADES IN YOUR SHIP)   TAB NEXT   L LAUNCH";
      break;
    case Screen::kMissions:
      DrawMissionsScreen(image, game_);
      hint = "ENTER TAKE THE JOB   BACKSPACE GIVE UP   TAB NEXT   L LAUNCH";
      break;
    case Screen::kChart:
      DrawChartScreen(image, game_);
      hint = "ARROWS CHOOSE A DESTINATION   BACKSPACE CLEAR   TAB NEXT   L LAUNCH";
      break;
    case Screen::kSystem:
      DrawSystemScreen(image, game_, game_.selected_system() >= 0 ? game_.selected_system() : game_.commander().system);
      hint = "ARROWS CHOOSE A SYSTEM   TAB NEXT   L LAUNCH";
      break;
    default:
      break;
  }
  DrawBar(image, hint);
}

/// The flight keys, over the view, for a pilot who has to fly with the keyboard alone
void GameUi::DrawHelp(Image& image) {
  std::uint8_t* p = image.data();
  for (std::size_t i = 0; i < image.size(); ++i) {
    p[i] = static_cast<std::uint8_t>(p[i] / 4);
  }
  const struct {
    const char* keys;
    const char* what;
  } rows[] = {
      {"ARROWS", "PITCH AND ROLL (DOWN IS NOSE UP)"},
      {"Q  E", "ROLL LEFT AND RIGHT"},
      {"A  D", "YAW LEFT AND RIGHT"},
      {"W  S", "THROTTLE UP AND DOWN"},
      {"X  Z", "FULL THROTTLE, THROTTLE OFF"},
      {"SHIFT (HOLD)", "FINE CONTROL FOR AIMING"},
      {"SPACE", "FIRE LASERS"},
      {"T", "LOCK A MISSILE ON THE TARGET"},
      {"M", "FIRE MISSILE"},
      {"TAB  SHIFT TAB", "NEXT AND PREVIOUS TARGET"},
      {"C", "ECM"},
      {"B", "ENERGY BOMB"},
      {"Y  N", "FLIGHT ASSIST, CLASSIC OR NEWTONIAN"},
      {"K", "DOCKING COMPUTER"},
      {"G  H", "GALACTIC CHART, HYPERSPACE"},
      {"J", "GALACTIC HYPERDRIVE: JUMP TO THE NEXT GALAXY"},
      {"1 2 3 4", "FRONT, REAR, LEFT, RIGHT VIEW"},
      {"R  ,  .", "REAR, LEFT, RIGHT VIEW (AGAIN TO LOOK BACK)"},
      {"BACKSPACE (HOLD)", "ESCAPE POD"},
      {"ESC  P", "PAUSE"},
      {"F1", "THIS LIST"},
  };
  const std::int64_t h = static_cast<std::int64_t>(image.height());
  const TextSizes sizes = SizesFor(image);
  const std::int64_t count = static_cast<std::int64_t>(sizeof(rows) / sizeof(rows[0]));
  // Fit the list to the picture, both ways
  float scale = static_cast<float>(h) * 0.88f / static_cast<float>((count + 3) * 9);
  scale = std::min(scale, FitScale(image, "BACKSPACE (HOLD)   PITCH AND ROLL (DOWN IS NOSE UP)", sizes.item));
  scale = std::max(1.0f, scale);
  const std::int64_t line = static_cast<std::int64_t>(9.0f * scale);
  std::int64_t y = (h - (count + 3) * line) / 2;
  DrawCentred(image, "FLIGHT CONTROLS", y, scale * 1.5f, kTitleColour);
  y += 2 * line;
  const std::int64_t column = (static_cast<std::int64_t>(image.width()) - TextWidth("BACKSPACE (HOLD)   PITCH AND ROLL (DOWN IS NOSE UP)", scale)) / 2;
  for (const auto& r : rows) {
    DrawText(image, r.keys, column, y, scale, kBright);
    DrawText(image, r.what, column + TextWidth("BACKSPACE (HOLD)   ", scale), y, scale, kText);
    y += line;
  }
}

void GameUi::DrawFlight(Image& image) {
  if (chart_open_) {
    DrawChartScreen(image, game_);
    DrawBar(image, "ARROWS CHOOSE A DESTINATION   H JUMP   G CLOSE");
    return;
  }
  cockpit_.Draw(image, game_, frame_);
  if (help_open_) {
    DrawHelp(image);
  }
  if (paused_) {
    std::uint8_t* p = image.data();
    for (std::size_t i = 0; i < image.size(); ++i) {
      p[i] = static_cast<std::uint8_t>(p[i] / 3);
    }
    const std::int64_t h = static_cast<std::int64_t>(image.height());
    const TextSizes sizes = SizesFor(image);
    DrawCentredFit(image, "PAUSED", h * 30 / 100, sizes.heading, kTitleColour);
    const std::int64_t below = DrawMenu(image, {"RESUME", "QUIT TO TITLE"}, pause_row_, h * 48 / 100, sizes.item);
    DrawCentredFit(image, "UP DOWN CHOOSE   ENTER SELECT   ESC RESUME", below + static_cast<std::int64_t>(4 * sizes.small),
                   sizes.small, kDim);
  }
}

int GameUi::DeadItemCount() const { return 3 + (has_save_ ? 1 : 0); }

std::string GameUi::DeadItem(int index) const {
  std::vector<std::string> items;
  if (has_save_) {
    items.push_back("CONTINUE SAVED GAME");
  }
  items.push_back("NEW GAME");
  items.push_back("TITLE SCREEN");
  items.push_back("QUIT");
  return items.at(static_cast<std::size_t>(index));
}

void GameUi::ChooseDeadItem(int index) {
  const std::string item = DeadItem(index);
  if (item == "CONTINUE SAVED GAME") {
    Continue();
  } else if (item == "NEW GAME") {
    StartNewGame();
  } else if (item == "TITLE SCREEN") {
    screen_ = Screen::kTitle;
    title_row_ = 0;
  } else {
    quit_ = true;
  }
}

void GameUi::DrawDead(Image& image) {
  const std::int64_t w = static_cast<std::int64_t>(image.width());
  const std::int64_t h = static_cast<std::int64_t>(image.height());
  const TextSizes sizes = SizesFor(image);
  const double t = dead_age_;

  // Dark red at the top, fading to near black, with stars
  for (std::int64_t y = 0; y < h; ++y) {
    const double f = static_cast<double>(y) / static_cast<double>(h);
    FillRows(image, y, y + 1,
             {static_cast<std::uint8_t>(16.0 - 14.0 * f), static_cast<std::uint8_t>(3.0 - 2.0 * f),
              static_cast<std::uint8_t>(4.0 + 2.0 * f)});
  }
  DrawStars(image, frame_);

  // The wreck, in the middle under the heading: the ship flies apart and the pieces drift and dim
  const std::int64_t side = std::max<std::int64_t>(16, h * 30 / 100);
  const std::int64_t x0 = (w - side) / 2;
  const std::int64_t y0 = h * 16 / 100;
  const std::int64_t cx = x0 + side / 2;
  const std::int64_t cy = y0 + side / 2;
  // A flash that spreads and dies in the first moments
  if (t < 1.2) {
    const double flash = std::pow(1.0 - t / 1.2, 1.5);
    const double radius = static_cast<double>(side) * (0.3 + 0.9 * t / 1.2);
    const std::int64_t reach = static_cast<std::int64_t>(radius) + 1;
    for (std::int64_t y = std::max<std::int64_t>(0, cy - reach); y < std::min(h, cy + reach); ++y) {
      for (std::int64_t x = std::max<std::int64_t>(0, cx - reach); x < std::min(w, cx + reach); ++x) {
        const double d = std::hypot(static_cast<double>(x - cx), static_cast<double>(y - cy));
        if (d >= radius) {
          continue;
        }
        const double k = (1.0 - d / radius) * (1.0 - d / radius) * flash;
        std::uint8_t* p = image.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3;
        p[0] = static_cast<std::uint8_t>(std::min(255.0, p[0] + 255.0 * k));
        p[1] = static_cast<std::uint8_t>(std::min(255.0, p[1] + 150.0 * k));
        p[2] = static_cast<std::uint8_t>(std::min(255.0, p[2] + 50.0 * k));
      }
    }
  }
  {
    const Model& ship = wreck_.faces.empty() ? title_model_ : wreck_;
    const float amount = static_cast<float>(1.3 * (1.0 - std::exp(-1.5 * t)) + 0.02 * t);
    const double dim = 1.0 - 0.5 * std::clamp((t - 1.5) / 4.5, 0.0, 1.0);
    Image tile(static_cast<std::uint32_t>(side), static_cast<std::uint32_t>(side));
    RenderOptions options;
    options.camera_distance = 14.0f;
    Renderer(options).DrawShip(tile, Shattered(ship, amount), 0.6f + static_cast<float>(t) * 0.25f);
    for (std::int64_t y = 0; y < side; ++y) {
      for (std::int64_t x = 0; x < side; ++x) {
        const std::uint8_t* p = tile.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(side) + static_cast<std::size_t>(x)) * 3;
        if (p[0] > 0 || p[1] > 0 || p[2] > 0) {
          image.Set(x0 + x, y0 + y, Scaled({p[0], p[1], p[2]}, dim));
        }
      }
    }
  }

  // The heading, with a shadow, fading in
  const double fade = std::clamp(t / 0.5, 0.0, 1.0);
  const float heading = FitScale(image, "GAME OVER", sizes.heading);
  const std::int64_t shadow = std::max<std::int64_t>(1, static_cast<std::int64_t>(heading / 6.0f));
  DrawCentred(image, "GAME OVER", h * 3 / 100 + shadow, heading, Scaled({90, 18, 14}, fade));
  DrawCentred(image, "GAME OVER", h * 3 / 100, heading, Scaled({255, 90, 70}, fade));

  // What happened, a line at a time
  const Commander& c = game_.commander();
  char kills[48];
  std::snprintf(kills, sizeof kills, " (%.0f KILLS)", std::floor(c.kills));
  std::vector<std::pair<std::string, std::string>> rows;
  rows.push_back({"CAUSE", game_.death_cause().empty() ? "SHIP DESTROYED" : game_.death_cause()});
  rows.push_back({"LOCATION", Upper(game_.system().name)});
  rows.push_back({"RATING", Upper(RankName(c.kills)) + kills});
  rows.push_back({"CASH", CreditsText(c.credits)});
  if (c.legal_status > 0) {
    rows.push_back({"LEGAL STATUS", Upper(LegalName(c.legal_status))});
  }
  if (CargoUsed(c) > 0) {
    rows.push_back({"CARGO LOST", std::to_string(CargoUsed(c)) + " UNITS"});
  }
  std::size_t label_width = 0;
  for (const auto& row : rows) {
    label_width = std::max(label_width, row.first.size());
  }
  std::string widest;
  for (const auto& row : rows) {
    const std::string line = std::string(label_width, ' ') + "  " + row.second;
    if (line.size() > widest.size()) {
      widest = line;
    }
  }
  const float item = FitScale(image, widest, sizes.item);
  const std::int64_t column = TextWidth(widest, item);
  const std::int64_t left = (w - column) / 2;
  const std::int64_t value_x = left + TextWidth(std::string(label_width + 2, ' '), item);
  std::int64_t y = h * 46 / 100;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const double shown = std::clamp((t - 1.0 - 0.22 * static_cast<double>(i)) / 0.25, 0.0, 1.0);
    if (shown > 0.0) {
      DrawText(image, rows[i].first, left, y, item, Scaled(kDim, shown));
      DrawText(image, rows[i].second, value_x, y, item, Scaled(i == 0 ? Rgb{255, 150, 100} : kText, shown));
    }
    y += static_cast<std::int64_t>(8 * item);
  }

  // The choices, once the rest is in
  if (t >= 2.0) {
    std::vector<std::string> items;
    for (int i = 0; i < DeadItemCount(); ++i) {
      items.push_back(DeadItem(i));
    }
    const std::int64_t below = DrawMenu(image, items, dead_row_, h * 74 / 100, sizes.item);
    DrawCentredFit(image, "UP DOWN CHOOSE   ENTER SELECT", below + static_cast<std::int64_t>(2 * sizes.small), sizes.small,
                   kDim);
  }
}

void GameUi::Draw(Image& image) {
  switch (screen_) {
    case Screen::kTitle:
      DrawTitle(image);
      break;
    case Screen::kDead:
      DrawDead(image);
      break;
    case Screen::kFlight:
      DrawFlight(image);
      break;
    default:
      DrawDocked(image);
      break;
  }
}

}  // namespace coriolis
