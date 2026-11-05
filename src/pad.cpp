#include "pad.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace maze {

namespace {

constexpr double kDeadZone = 0.2;     // sticks don't rest at exactly zero
constexpr double kTriggerPull = 0.3;  // how far to pull a trigger to fire

/// An axis from -1 to 1, ignoring the middle of its travel and then rising smoothly from zero
double Shaped(Sint16 raw) {
  const double value = std::clamp(raw / 32767.0, -1.0, 1.0);
  const double size = std::fabs(value);
  if (size < kDeadZone) {
    return 0.0;
  }
  return std::copysign((size - kDeadZone) / (1.0 - kDeadZone), value);
}

/// Keep whichever of two inputs is pushed furthest, so that a resting device can't cancel a moving one
double Stronger(double a, double b) { return std::fabs(b) > std::fabs(a) ? b : a; }

}  // namespace

Pad::~Pad() {
  for (SDL_GameController* controller : controllers_) {
    SDL_GameControllerClose(controller);
  }
  for (SDL_Joystick* joystick : joysticks_) {
    SDL_JoystickClose(joystick);
  }
}

bool Pad::IsPlainJoystick(SDL_JoystickID id) const {
  return std::any_of(joysticks_.begin(), joysticks_.end(), [id](SDL_Joystick* joystick) { return SDL_JoystickInstanceID(joystick) == id; });
}

void Pad::Handle(const SDL_Event& event) {
  if (event.type == SDL_JOYDEVICEADDED) {
    const int index = event.jdevice.which;
    const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
    if (SDL_GameControllerFromInstanceID(id) != nullptr || IsPlainJoystick(id)) {
      return;  // already open
    }
    if (SDL_IsGameController(index)) {
      if (SDL_GameController* controller = SDL_GameControllerOpen(index)) {
        controllers_.push_back(controller);
        std::printf("controller: %s\n", SDL_GameControllerName(controller));
      }
    } else if (SDL_Joystick* joystick = SDL_JoystickOpen(index)) {
      joysticks_.push_back(joystick);
      std::printf("joystick: %s\n", SDL_JoystickName(joystick));
    }
  } else if (event.type == SDL_JOYDEVICEREMOVED) {
    const SDL_JoystickID id = event.jdevice.which;
    for (std::size_t i = 0; i < controllers_.size(); ++i) {
      if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controllers_[i])) == id) {
        SDL_GameControllerClose(controllers_[i]);
        controllers_.erase(controllers_.begin() + static_cast<std::ptrdiff_t>(i));
        return;
      }
    }
    for (std::size_t i = 0; i < joysticks_.size(); ++i) {
      if (SDL_JoystickInstanceID(joysticks_[i]) == id) {
        SDL_JoystickClose(joysticks_[i]);
        joysticks_.erase(joysticks_.begin() + static_cast<std::ptrdiff_t>(i));
        return;
      }
    }
  }
}

PadAction Pad::ActionFor(const SDL_Event& event) const {
  if (event.type == SDL_CONTROLLERBUTTONDOWN) {
    switch (event.cbutton.button) {
      case SDL_CONTROLLER_BUTTON_Y:
        return PadAction::kView;
      case SDL_CONTROLLER_BUTTON_X:
        return PadAction::kColours;
      case SDL_CONTROLLER_BUTTON_BACK:
        return PadAction::kNewMaze;
      case SDL_CONTROLLER_BUTTON_START:
        return PadAction::kPause;
      case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        return PadAction::kFovDown;
      case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        return PadAction::kFovUp;
      default:
        return PadAction::kButton;
    }
  }
  // A game controller sends joystick events as well, which are not to count twice
  if (event.type == SDL_JOYBUTTONDOWN && IsPlainJoystick(event.jbutton.which)) {
    switch (event.jbutton.button) {
      case 1:
        return PadAction::kView;
      case 2:
        return PadAction::kColours;
      case 3:
        return PadAction::kNewMaze;
      case 4:
        return PadAction::kPause;
      default:
        return PadAction::kButton;
    }
  }
  return PadAction::kNone;
}

PadState Pad::Read() const {
  PadState state;
  for (SDL_GameController* controller : controllers_) {
    const auto button = [controller](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(controller, b) != 0; };
    const double dpad_turn = (button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ? 1.0 : 0.0) - (button(SDL_CONTROLLER_BUTTON_DPAD_LEFT) ? 1.0 : 0.0);
    const double dpad_throttle = (button(SDL_CONTROLLER_BUTTON_DPAD_UP) ? 1.0 : 0.0) - (button(SDL_CONTROLLER_BUTTON_DPAD_DOWN) ? 1.0 : 0.0);
    state.turn = Stronger(Stronger(state.turn, Shaped(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX))), dpad_turn);
    state.throttle = Stronger(Stronger(state.throttle, -Shaped(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY))), dpad_throttle);
    state.elevate = Stronger(state.elevate, -Shaped(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY)));
    const double trigger = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0;
    state.fire = state.fire || button(SDL_CONTROLLER_BUTTON_A) || trigger > kTriggerPull;
  }
  for (SDL_Joystick* joystick : joysticks_) {
    if (SDL_JoystickNumAxes(joystick) >= 2) {
      state.turn = Stronger(state.turn, Shaped(SDL_JoystickGetAxis(joystick, 0)));
      state.throttle = Stronger(state.throttle, -Shaped(SDL_JoystickGetAxis(joystick, 1)));
    }
    if (SDL_JoystickNumAxes(joystick) >= 4) {
      state.elevate = Stronger(state.elevate, -Shaped(SDL_JoystickGetAxis(joystick, 3)));
    }
    if (SDL_JoystickNumHats(joystick) >= 1) {
      const Uint8 hat = SDL_JoystickGetHat(joystick, 0);
      state.turn = Stronger(state.turn, ((hat & SDL_HAT_RIGHT) != 0 ? 1.0 : 0.0) - ((hat & SDL_HAT_LEFT) != 0 ? 1.0 : 0.0));
      state.throttle = Stronger(state.throttle, ((hat & SDL_HAT_UP) != 0 ? 1.0 : 0.0) - ((hat & SDL_HAT_DOWN) != 0 ? 1.0 : 0.0));
    }
    state.fire = state.fire || (SDL_JoystickNumButtons(joystick) >= 1 && SDL_JoystickGetButton(joystick, 0) != 0);
  }
  return state;
}

}  // namespace maze
