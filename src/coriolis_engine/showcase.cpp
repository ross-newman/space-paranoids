#include "coriolis/showcase.h"

#include <algorithm>
#include <cmath>

#include "coriolis/ships.h"

namespace coriolis {

std::size_t Showcase::ShipAt(double elapsed_seconds) const {
  if (options_.fixed_ship >= 0) {
    return static_cast<std::size_t>(options_.fixed_ship) % kShipCount;
  }
  const double dwell = std::max(0.1, options_.seconds_per_ship);
  const double turns = std::floor(std::max(0.0, elapsed_seconds) / dwell);
  return static_cast<std::size_t>(turns) % kShipCount;
}

void Showcase::Render(Image& image, double elapsed_seconds) const {
  const Model& ship = AllShips()[ShipAt(elapsed_seconds)];
  const double seconds = std::max(0.0, elapsed_seconds);
  image.Clear({0, 0, 0});
  DrawStars(image, static_cast<std::uint32_t>(seconds * options_.star_rate));
  renderer_.DrawShip(image, ship, static_cast<float>(seconds * options_.turn_rate));
  DrawCaption(image, ship.name);
}

}  // namespace coriolis
