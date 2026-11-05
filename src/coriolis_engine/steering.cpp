#include "coriolis/steering.h"

#include <algorithm>
#include <cmath>

#include "coriolis/types.h"

namespace coriolis {

namespace {
constexpr double kHalfPi = 1.5707963267948966;
constexpr double kPiD = 3.141592653589793;
}  // namespace

void SteerToward(Entity& ship, const Vec3d& direction) {
  const Vec3d wanted = Normalized(direction);
  if (Length(wanted) < 0.5) {
    return;
  }
  const Vec3d local = Rotate(Conjugate(ship.orientation), wanted);
  const double planar = std::hypot(local.x, local.y);
  const double angle = std::atan2(planar, local.z);
  ShipControls& c = ship.controls;
  if (angle < 0.3) {
    c.yaw = Clamp(std::atan2(local.x, local.z) * 3.0, -1.0, 1.0);
    c.pitch = Clamp(std::atan2(local.y, local.z) * 3.0, -1.0, 1.0);
    c.roll = 0.0;
    return;
  }
  // The angle of the target round the nose, from the roof towards the right wing
  double roll_error = std::atan2(local.x, local.y);
  double flip = 1.0;
  if (roll_error > kHalfPi) {
    roll_error -= kPiD;
    flip = -1.0;
  } else if (roll_error < -kHalfPi) {
    roll_error += kPiD;
    flip = -1.0;
  }
  c.roll = Clamp(roll_error * 2.0, -1.0, 1.0);
  c.pitch = flip * Clamp(angle * 3.0, 0.0, 1.0) * (0.25 + 0.75 * std::cos(roll_error));
  c.yaw = 0.0;
}

void SteerToOrientation(Entity& ship, const Quat& target) {
  Quat error = Conjugate(ship.orientation) * target;
  if (error.w < 0.0) {
    error = {-error.w, -error.x, -error.y, -error.z};
  }
  const double s = std::sqrt(error.x * error.x + error.y * error.y + error.z * error.z);
  if (s < 1e-9) {
    ship.controls.pitch = ship.controls.roll = ship.controls.yaw = 0.0;
    return;
  }
  const double angle = 2.0 * std::atan2(s, error.w);
  // The turn needed about each of the ship's own axes. A turn about x lowers the nose, about z lifts the right wing.
  const double about_x = error.x / s * angle;
  const double about_y = error.y / s * angle;
  const double about_z = error.z / s * angle;
  ship.controls.pitch = Clamp(-about_x * 3.0, -1.0, 1.0);
  ship.controls.yaw = Clamp(about_y * 3.0, -1.0, 1.0);
  ship.controls.roll = Clamp(-about_z * 3.0, -1.0, 1.0);
}

Vec3d LeadDirection(const Vec3d& shooter_position, const Vec3d& shooter_velocity, const Vec3d& target_position,
                    const Vec3d& target_velocity, double bolt_speed) {
  // Work in the shooter's frame, where a bolt leaves at bolt_speed in the direction of fire
  const Vec3d p = target_position - shooter_position;
  const Vec3d v = target_velocity - shooter_velocity;
  const double a = Dot(v, v) - bolt_speed * bolt_speed;
  const double b = 2.0 * Dot(p, v);
  const double c = Dot(p, p);
  double t = Length(p) / bolt_speed;
  if (std::fabs(a) < 1e-9) {
    if (std::fabs(b) > 1e-9 && -c / b > 0.0) {
      t = -c / b;
    }
  } else {
    const double disc = b * b - 4.0 * a * c;
    if (disc >= 0.0) {
      const double root = std::sqrt(disc);
      const double t1 = (-b - root) / (2.0 * a);
      const double t2 = (-b + root) / (2.0 * a);
      const double low = std::min(t1, t2);
      const double high = std::max(t1, t2);
      if (low > 0.0) {
        t = low;
      } else if (high > 0.0) {
        t = high;
      }
    }
  }
  return Normalized(p + v * t);
}

double AngleBetween(const Vec3d& a, const Vec3d& b) {
  const double la = Length(a);
  const double lb = Length(b);
  if (la < 1e-12 || lb < 1e-12) {
    return 0.0;
  }
  return std::acos(Clamp(Dot(a, b) / (la * lb), -1.0, 1.0));
}

}  // namespace coriolis
