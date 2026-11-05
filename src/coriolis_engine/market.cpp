#include "coriolis/market.h"

#include <algorithm>

#include "coriolis/random.h"

namespace coriolis {

namespace {

// name, unit, base price, economy factor, base quantity, quantity factor, mood mask, illegal
const std::array<CommodityInfo, kCommodityCount> kCommodities{{
    {"Food", Unit::kTonnes, 22, -2, 6, -2, 0x01, false},
    {"Textiles", Unit::kTonnes, 28, -1, 10, -1, 0x03, false},
    {"Radioactives", Unit::kTonnes, 70, -3, 2, 1, 0x07, false},
    {"Slaves", Unit::kTonnes, 45, -5, 8, -2, 0x0F, true},
    {"Liquor", Unit::kTonnes, 90, -5, 30, -3, 0x03, false},
    {"Luxuries", Unit::kTonnes, 180, 8, 40, -3, 0x78, false},
    {"Narcotics", Unit::kTonnes, 220, 28, 34, -4, 0x03, true},
    {"Computers", Unit::kTonnes, 150, 14, 20, -4, 0x07, false},
    {"Machinery", Unit::kTonnes, 110, 6, 10, -2, 0x1F, false},
    {"Alloys", Unit::kTonnes, 85, 1, 20, -3, 0x07, false},
    {"Firearms", Unit::kTonnes, 130, 12, 4, 3, 0x07, true},
    {"Furs", Unit::kTonnes, 170, -9, 8, -5, 0x3F, false},
    {"Minerals", Unit::kTonnes, 36, -1, 25, -2, 0x03, false},
    {"Gold", Unit::kKilograms, 100, -1, 9, -1, 0x07, false},
    {"Platinum", Unit::kKilograms, 175, -2, 2, -2, 0x03, false},
    {"Gem-Stones", Unit::kGrams, 50, -1, 10, -3, 0x0F, false},
    {"Alien Items", Unit::kTonnes, 60, 15, 2, 1, 0x07, false},
}};

}  // namespace

const CommodityInfo& CommodityData(Commodity c) { return kCommodities[static_cast<std::size_t>(c)]; }

const char* UnitName(Unit u) {
  switch (u) {
    case Unit::kTonnes:
      return "t";
    case Unit::kKilograms:
      return "kg";
    case Unit::kGrams:
      return "g";
  }
  return "";
}

Market Market::ForSystem(const StarSystem& system, std::uint32_t mood) {
  Market m;
  Random r(static_cast<std::uint64_t>(system.seed) * 0x9E3779B97F4A7C15ULL + mood);
  for (std::size_t i = 0; i < kCommodityCount; ++i) {
    const CommodityInfo& info = kCommodities[i];
    const int swing = static_cast<int>(r.Below(256)) & info.mood_mask;
    m.entries_[i].price = std::max(3, info.base_price + swing + info.economy_factor * system.economy);
    const int quantity =
        info.base_quantity + (static_cast<int>(r.Below(256)) & info.mood_mask) - info.quantity_factor * system.economy;
    m.entries_[i].quantity = std::clamp(quantity, 0, 63);
    // Only the more advanced worlds have alien goods
    if (static_cast<Commodity>(i) == Commodity::kAlienItems && system.tech_level < 8) {
      m.entries_[i].quantity = 0;
    }
  }
  return m;
}

}  // namespace coriolis
