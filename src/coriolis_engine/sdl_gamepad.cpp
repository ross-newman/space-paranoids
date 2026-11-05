#include "coriolis/sdl_gamepad.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace coriolis {

namespace {

float Axis(SDL_Gamepad* pad, SDL_GamepadAxis axis) {
  return std::clamp(static_cast<float>(SDL_GetGamepadAxis(pad, axis)) / 32767.0f, -1.0f, 1.0f);
}

float Trigger(SDL_Gamepad* pad, SDL_GamepadAxis axis) {
  return std::clamp(static_cast<float>(SDL_GetGamepadAxis(pad, axis)) / 32767.0f, 0.0f, 1.0f);
}

struct ButtonMap {
  SDL_GamepadButton sdl;
  GamepadButton button;
};

const ButtonMap kButtons[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, GamepadButton::kSouth},
    {SDL_GAMEPAD_BUTTON_EAST, GamepadButton::kEast},
    {SDL_GAMEPAD_BUTTON_WEST, GamepadButton::kWest},
    {SDL_GAMEPAD_BUTTON_NORTH, GamepadButton::kNorth},
    {SDL_GAMEPAD_BUTTON_BACK, GamepadButton::kBack},
    {SDL_GAMEPAD_BUTTON_START, GamepadButton::kStart},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, GamepadButton::kLeftStick},
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, GamepadButton::kRightStick},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, GamepadButton::kLeftShoulder},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, GamepadButton::kRightShoulder},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, GamepadButton::kDpadUp},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, GamepadButton::kDpadDown},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, GamepadButton::kDpadLeft},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, GamepadButton::kDpadRight},
    {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, GamepadButton::kPaddleRightUpper},
    {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, GamepadButton::kPaddleRightLower},
    {SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, GamepadButton::kPaddleLeftUpper},
    {SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, GamepadButton::kPaddleLeftLower},
};

GamepadKind KindOf(SDL_Gamepad* pad) {
  // Only the Elite pads have paddles, whatever type they report
  if (SDL_GamepadHasButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1) ||
      SDL_GamepadHasButton(pad, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1)) {
    return GamepadKind::kXboxElite;
  }
  if (SDL_GetGamepadType(pad) == SDL_GAMEPAD_TYPE_XBOX360) {
    return GamepadKind::kXbox360;
  }
  return GamepadKind::kGeneric;
}

}  // namespace

SdlGamepad::~SdlGamepad() {
  Close();
  if (started_) {
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
  }
}

bool SdlGamepad::Init(std::string* error) {
  if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
    if (error != nullptr) {
      *error = std::string("cannot start the SDL gamepad subsystem: ") + SDL_GetError();
    }
    return false;
  }
  started_ = true;
  OpenFirst();
  return true;
}

void SdlGamepad::OpenFirst() {
  if (pad_ != nullptr) {
    return;
  }
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  if (ids == nullptr) {
    return;
  }
  for (int i = 0; i < count && pad_ == nullptr; ++i) {
    pad_ = SDL_OpenGamepad(ids[i]);
    if (pad_ != nullptr) {
      id_ = ids[i];
      kind_ = KindOf(pad_);
    }
  }
  SDL_free(ids);
}

void SdlGamepad::Close() {
  if (pad_ != nullptr) {
    SDL_CloseGamepad(pad_);
    pad_ = nullptr;
    id_ = 0;
    kind_ = GamepadKind::kGeneric;
  }
}

void SdlGamepad::HandleEvent(const SDL_Event& event) {
  if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
    OpenFirst();
  } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED && pad_ != nullptr && event.gdevice.which == id_) {
    Close();
    // Another pad may already be connected
    OpenFirst();
  }
}

std::string SdlGamepad::name() const {
  if (pad_ == nullptr) {
    return {};
  }
  const char* n = SDL_GetGamepadName(pad_);
  return n != nullptr ? n : "";
}

bool SdlGamepad::Read(GamepadState* state) const {
  *state = GamepadState();
  if (pad_ == nullptr) {
    return false;
  }
  state->left_x = Axis(pad_, SDL_GAMEPAD_AXIS_LEFTX);
  // SDL has up as negative
  state->left_y = -Axis(pad_, SDL_GAMEPAD_AXIS_LEFTY);
  state->right_x = Axis(pad_, SDL_GAMEPAD_AXIS_RIGHTX);
  state->right_y = -Axis(pad_, SDL_GAMEPAD_AXIS_RIGHTY);
  state->left_trigger = Trigger(pad_, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
  state->right_trigger = Trigger(pad_, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
  for (const ButtonMap& m : kButtons) {
    if (SDL_GetGamepadButton(pad_, m.sdl)) {
      state->Set(m.button);
    }
  }
  return true;
}

void SdlGamepad::Rumble(float low, float high, int milliseconds) {
  if (pad_ == nullptr || milliseconds <= 0) {
    return;
  }
  const auto strength = [](float v) { return static_cast<Uint16>(std::clamp(v, 0.0f, 1.0f) * 65535.0f); };
  SDL_RumbleGamepad(pad_, strength(low), strength(high), static_cast<Uint32>(milliseconds));
}

}  // namespace coriolis
