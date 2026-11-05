#ifndef SPACE_PARANOIDS_PAD_H_
#define SPACE_PARANOIDS_PAD_H_

#include <SDL.h>

#include <vector>

// Game controllers and joysticks. A device SDL knows the layout of is read as a game controller, so that its buttons are
// the same whatever made it, and anything else is read as a plain joystick by the number of its axes and buttons. They
// can come and go while the game runs. Needs SDL_INIT_GAMECONTROLLER.

namespace maze {

/// What the devices are asking for this moment, taken together
struct PadState {
  double throttle = 0.0;  // -1 back to 1 forward
  double turn = 0.0;      // -1 left to 1 right
  double elevate = 0.0;   // -1 guns down to 1 guns up
  bool fire = false;
};

/// What a button press means
enum class PadAction {
  kNone,    // not a button press
  kButton,  // a button with no other job, which is enough to get past the opening screen
  kView,
  kColours,
  kNewMaze,
  kPause,
  kFovDown,
  kFovUp,
};

class Pad {
 public:
  Pad() = default;
  ~Pad();
  Pad(const Pad&) = delete;
  Pad& operator=(const Pad&) = delete;

  /// Pass every SDL event through here, to pick up devices as they are plugged in and pulled out
  void Handle(const SDL_Event& event);

  /// What a press event asks for, from a device of either kind
  PadAction ActionFor(const SDL_Event& event) const;

  PadState Read() const;

  int devices() const { return static_cast<int>(controllers_.size() + joysticks_.size()); }

 private:
  bool IsPlainJoystick(SDL_JoystickID id) const;

  std::vector<SDL_GameController*> controllers_;
  std::vector<SDL_Joystick*> joysticks_;
};

}  // namespace maze

#endif  // SPACE_PARANOIDS_PAD_H_
