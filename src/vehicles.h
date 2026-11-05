#ifndef SPACE_PARANOIDS_VEHICLES_H_
#define SPACE_PARANOIDS_VEHICLES_H_

#include "softrender/model.h"
#include "softrender/types.h"

// The vehicles of the game, built from code. Each faces along +z with +y up, is centred, and reaches 1 from its centre:
// size it with Transform::scale. Draw them with a bright Outline (shade about 5) so that the dark hulls are lined in light.

namespace vehicles {

/// The paint of a vehicle
struct Style {
  softrender::Rgb hull{16, 24, 44};
  softrender::Rgb trim{34, 52, 90};
  softrender::Rgb glow{40, 220, 255};  // emissive strips, which bloom
};

/// The three paints of the runners: a saturated body (hull) on a grey metal frame (trim). A runner has no glow.
inline Style RedStyle() { return {{215, 14, 16}, {74, 74, 80}, {255, 40, 30}}; }
inline Style YellowStyle() { return {{240, 205, 10}, {74, 74, 80}, {255, 215, 40}}; }
inline Style OrangeStyle() { return {{230, 135, 12}, {74, 74, 80}, {255, 120, 20}}; }

/// A low six sided wedge on four splayed track pods, with a flat turret and twin barrels
softrender::Model MakeCrawler(const Style& style = {});
/// The same crawler in two, so that its guns can elevate: `gun` has its origin at the pivot, which is at `gun_pivot` in
/// the frame of `hull`
struct CrawlerParts {
  softrender::Model hull;
  softrender::Model gun;
  softrender::Vec3 gun_pivot;
};
CrawlerParts MakeCrawlerParts(const Style& style = {});
/// A light cycle, its geometry imported from the open source gltron project rather than built up by hand. Unlike the
/// other vehicles it is solid and glossy with no outlines: painted body, grey frame, black canopy and wheels.
softrender::Model MakeRunner(const Style& style = RedStyle());
/// A twin hulled flyer joined by a forward swept wing, with a pod on the centre line
softrender::Model MakeKite(const Style& style = {});

}  // namespace vehicles

#endif  // SPACE_PARANOIDS_VEHICLES_H_
