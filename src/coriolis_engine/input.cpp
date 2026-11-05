#include "coriolis/input.h"

#include <algorithm>
#include <cmath>

namespace coriolis {

const char* ActionName(Action action) {
  switch (action) {
    case Action::kFireLasers:
      return "fire lasers";
    case Action::kFireMissile:
      return "fire missile";
    case Action::kTargetMissile:
      return "target missile";
    case Action::kNextTarget:
      return "next target";
    case Action::kEcm:
      return "ECM";
    case Action::kEnergyBomb:
      return "energy bomb";
    case Action::kEscapePod:
      return "escape pod";
    case Action::kHyperspace:
      return "hyperspace";
    case Action::kDockingComputer:
      return "docking computer";
    case Action::kToggleFlightAssist:
      return "flight assist";
    case Action::kViewFront:
      return "front view";
    case Action::kViewRear:
      return "rear view";
    case Action::kViewLeft:
      return "left view";
    case Action::kViewRight:
      return "right view";
    case Action::kGalacticChart:
      return "galactic chart";
    case Action::kPause:
      return "pause";
    case Action::kPreviousTarget:
      return "previous target";
    case Action::kThrottleFull:
      return "full throttle";
    case Action::kThrottleZero:
      return "throttle off";
    case Action::kGalacticHyperspace:
      return "galactic hyperspace";
    case Action::kCount:
      break;
  }
  return "?";
}

InputConfig DefaultInputConfig(GamepadKind kind) {
  InputConfig c;
  // The Xbox 360 sticks rest off centre and wobble more than the Elite pads' do
  switch (kind) {
    case GamepadKind::kXbox360:
      c.stick_dead_zone = 0.18f;
      break;
    case GamepadKind::kXboxElite:
      c.stick_dead_zone = 0.10f;
      break;
    case GamepadKind::kGeneric:
      c.stick_dead_zone = 0.15f;
      break;
  }

  using B = GamepadButton;
  using A = Action;
  c.bindings = {
      {B::kRightTrigger, A::kFireLasers},
      {B::kLeftTrigger, A::kFireMissile},
      {B::kRightShoulder, A::kTargetMissile},
      {B::kLeftShoulder, A::kEcm},
      {B::kLeftStick, A::kNextTarget},
      {B::kRightStick, A::kEscapePod, 1.0f},
      {B::kSouth, A::kToggleFlightAssist},
      {B::kEast, A::kDockingComputer},
      {B::kWest, A::kEnergyBomb},
      {B::kNorth, A::kHyperspace},
      {B::kDpadUp, A::kViewFront},
      {B::kDpadDown, A::kViewRear},
      {B::kDpadLeft, A::kViewLeft},
      {B::kDpadRight, A::kViewRight},
      {B::kBack, A::kGalacticChart},
      {B::kStart, A::kPause},
      {B::kPaddleRightUpper, A::kToggleFlightAssist},
      {B::kPaddleRightLower, A::kNextTarget},
      {B::kPaddleLeftUpper, A::kEnergyBomb},
      {B::kPaddleLeftLower, A::kDockingComputer},
  };
  return c;
}

void ShapeStick(const InputConfig& config, float x, float y, float* out_x, float* out_y) {
  if (!std::isfinite(x) || !std::isfinite(y)) {
    *out_x = 0.0f;
    *out_y = 0.0f;
    return;
  }
  const float magnitude = std::sqrt(x * x + y * y);
  const float inner = std::clamp(config.stick_dead_zone, 0.0f, 0.95f);
  const float outer = std::max(config.stick_outer_zone, inner + 0.01f);
  if (magnitude <= inner) {
    *out_x = 0.0f;
    *out_y = 0.0f;
    return;
  }
  // Rescale so the output starts from 0 at the edge of the dead zone and reaches 1 at the outer zone
  const float scaled = std::min((magnitude - inner) / (outer - inner), 1.0f);
  const float shaped = std::pow(scaled, std::max(config.stick_response, 0.1f));
  *out_x = x / magnitude * shaped;
  *out_y = y / magnitude * shaped;
}

void InputMapper::Reset() {
  held_for_.assign(config_.bindings.size(), 0.0f);
  previous_.reset();
  left_trigger_down_ = false;
  right_trigger_down_ = false;
}

bool InputMapper::ButtonDown(const GamepadState& pad, GamepadButton button) const {
  if (button == GamepadButton::kLeftTrigger) {
    return left_trigger_down_;
  }
  if (button == GamepadButton::kRightTrigger) {
    return right_trigger_down_;
  }
  return pad.Down(button);
}

FlightInput InputMapper::Update(const GamepadState& pad, float seconds) {
  FlightInput out;

  float lx = 0.0f;
  float ly = 0.0f;
  float rx = 0.0f;
  float ry = 0.0f;
  ShapeStick(config_, pad.left_x, pad.left_y, &lx, &ly);
  ShapeStick(config_, pad.right_x, pad.right_y, &rx, &ry);
  out.pitch = config_.invert_pitch ? ly : -ly;
  out.roll = lx;
  out.yaw = rx;
  out.throttle = ry;

  // Triggers become buttons, with a gap between pressing and releasing so a trigger hovering at the threshold does not
  // chatter
  const auto trigger = [this](float value, bool was_down) {
    return value >= (was_down ? config_.trigger_release : config_.trigger_press);
  };
  left_trigger_down_ = trigger(pad.left_trigger, left_trigger_down_);
  right_trigger_down_ = trigger(pad.right_trigger, right_trigger_down_);

  const float dt = std::isfinite(seconds) ? std::max(seconds, 0.0f) : 0.0f;
  std::bitset<kActionCount> now;
  for (std::size_t i = 0; i < config_.bindings.size(); ++i) {
    const ButtonBinding& binding = config_.bindings[i];
    if (!ButtonDown(pad, binding.button)) {
      held_for_[i] = 0.0f;
      continue;
    }
    held_for_[i] += dt;
    if (held_for_[i] >= binding.hold_seconds) {
      now.set(static_cast<std::size_t>(binding.action));
    }
  }

  out.held = now;
  out.pressed = now & ~previous_;
  out.released = previous_ & ~now;
  previous_ = now;
  return out;
}

}  // namespace coriolis
