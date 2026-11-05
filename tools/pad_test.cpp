// Test the controller code against virtual devices from SDL, with no hardware needed. Any real devices are listed too.
// Run it with SDL_VIDEODRIVER=dummy if there is no display. `pad_test 5` instead watches the real devices for 5 seconds.

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "pad.h"

namespace {

int failures = 0;

void Expect(bool ok, const char* what) {
  std::printf("  %-44s %s\n", what, ok ? "ok" : "FAILED");
  failures += ok ? 0 : 1;
}

void Pump(maze::Pad& pad, maze::PadAction* action = nullptr) {
  SDL_JoystickUpdate();
  SDL_GameControllerUpdate();
  SDL_Event event;
  while (SDL_PollEvent(&event) != 0) {
    pad.Handle(event);
    const maze::PadAction now = pad.ActionFor(event);
    if (action != nullptr && now != maze::PadAction::kNone) {
      *action = now;
    }
  }
}

SDL_Joystick* OpenedLast(int index) { return SDL_JoystickFromInstanceID(SDL_JoystickGetDeviceInstanceID(index)); }

}  // namespace

int main(int argc, char** argv) {
  if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
    std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
    return 1;
  }
  maze::Pad pad;
  Pump(pad);
  const int real = pad.devices();
  std::printf("%d real device(s)\n", real);
  // With a number, show what the real devices are doing for that many seconds and stop
  if (argc > 1) {
    const Uint32 end = SDL_GetTicks() + static_cast<Uint32>(std::atoi(argv[1])) * 1000u;
    double largest_throttle = 0.0;
    double largest_turn = 0.0;
    bool fired = false;
    int presses = 0;
    while (SDL_GetTicks() < end) {
      maze::PadAction action = maze::PadAction::kNone;
      Pump(pad, &action);
      presses += action != maze::PadAction::kNone ? 1 : 0;
      const maze::PadState state = pad.Read();
      largest_throttle = std::max(largest_throttle, std::fabs(state.throttle));
      largest_turn = std::max(largest_turn, std::fabs(state.turn));
      fired = fired || state.fire;
      SDL_Delay(5);
    }
    std::printf("largest throttle %.2f, largest turn %.2f, fired %s, %d button presses\n", largest_throttle, largest_turn, fired ? "yes" : "no", presses);
    SDL_Quit();
    return 0;
  }

  std::printf("game controller\n");
  const int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
  Pump(pad);
  SDL_Joystick* stick = OpenedLast(index);
  Expect(pad.devices() == real + 1 && stick != nullptr, "plugging it in opens it");
  Expect(SDL_IsGameController(index), "it is read as a game controller");

  SDL_JoystickSetVirtualAxis(stick, SDL_CONTROLLER_AXIS_LEFTX, 32767);
  SDL_JoystickSetVirtualAxis(stick, SDL_CONTROLLER_AXIS_LEFTY, -32768);
  Pump(pad);
  Expect(pad.Read().turn > 0.99, "stick right turns right");
  Expect(pad.Read().throttle > 0.99, "stick up drives forward");

  SDL_JoystickSetVirtualAxis(stick, SDL_CONTROLLER_AXIS_RIGHTY, -32768);
  Pump(pad);
  Expect(pad.Read().elevate > 0.99, "right stick up raises the guns");
  SDL_JoystickSetVirtualAxis(stick, SDL_CONTROLLER_AXIS_RIGHTY, 0);

  SDL_JoystickSetVirtualAxis(stick, SDL_CONTROLLER_AXIS_LEFTX, 3000);
  SDL_JoystickSetVirtualAxis(stick, SDL_CONTROLLER_AXIS_LEFTY, 3000);
  Pump(pad);
  Expect(pad.Read().turn == 0.0 && pad.Read().throttle == 0.0, "a stick near the middle does nothing");

  SDL_JoystickSetVirtualAxis(stick, SDL_CONTROLLER_AXIS_LEFTX, 0);
  SDL_JoystickSetVirtualAxis(stick, SDL_CONTROLLER_AXIS_LEFTY, 0);
  SDL_JoystickSetVirtualButton(stick, SDL_CONTROLLER_BUTTON_A, SDL_PRESSED);
  maze::PadAction action = maze::PadAction::kNone;
  Pump(pad, &action);
  Expect(pad.Read().fire, "A fires");
  Expect(action == maze::PadAction::kButton, "A counts as a press, for the opening screen");
  SDL_JoystickSetVirtualButton(stick, SDL_CONTROLLER_BUTTON_A, SDL_RELEASED);
  SDL_JoystickSetVirtualAxis(stick, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767);
  Pump(pad);
  Expect(pad.Read().fire, "the right trigger fires");
  SDL_JoystickSetVirtualAxis(stick, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);  // a trigger rests at the end of its range
  Pump(pad);
  Expect(!pad.Read().fire, "letting go stops firing");

  const struct {
    int button;
    maze::PadAction wanted;
    const char* what;
  } buttons[] = {{SDL_CONTROLLER_BUTTON_Y, maze::PadAction::kView, "Y changes the view"},
                 {SDL_CONTROLLER_BUTTON_X, maze::PadAction::kColours, "X changes the colours"},
                 {SDL_CONTROLLER_BUTTON_BACK, maze::PadAction::kNewMaze, "Back gives a new maze"},
                 {SDL_CONTROLLER_BUTTON_START, maze::PadAction::kPause, "Start pauses"},
                 {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, maze::PadAction::kFovDown, "left shoulder narrows the view"},
                 {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, maze::PadAction::kFovUp, "right shoulder widens the view"}};
  for (const auto& b : buttons) {
    action = maze::PadAction::kNone;
    SDL_JoystickSetVirtualButton(stick, b.button, SDL_PRESSED);
    Pump(pad, &action);
    Expect(action == b.wanted, b.what);
    SDL_JoystickSetVirtualButton(stick, b.button, SDL_RELEASED);
    Pump(pad);
  }

  SDL_JoystickSetVirtualButton(stick, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_PRESSED);
  Pump(pad);
  Expect(pad.Read().turn < -0.99, "the d-pad steers");
  SDL_JoystickSetVirtualButton(stick, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_RELEASED);
  SDL_JoystickDetachVirtual(index);
  Pump(pad);
  Expect(pad.devices() == real, "pulling it out closes it");

  std::printf("plain joystick\n");
  const int plain = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_FLIGHT_STICK, 4, 6, 1);
  Pump(pad);
  stick = OpenedLast(plain);
  Expect(pad.devices() == real + 1 && stick != nullptr, "plugging it in opens it");
  SDL_JoystickSetVirtualAxis(stick, 0, -32768);
  SDL_JoystickSetVirtualAxis(stick, 1, 32767);
  SDL_JoystickSetVirtualButton(stick, 0, SDL_PRESSED);
  action = maze::PadAction::kNone;
  Pump(pad, &action);
  Expect(pad.Read().turn < -0.99 && pad.Read().throttle < -0.99, "the stick steers and reverses");
  Expect(pad.Read().fire, "button 0 fires");
  SDL_JoystickSetVirtualAxis(stick, 0, 0);
  SDL_JoystickSetVirtualAxis(stick, 1, 0);
  SDL_JoystickSetVirtualButton(stick, 0, SDL_RELEASED);
  SDL_JoystickSetVirtualButton(stick, 1, SDL_PRESSED);
  action = maze::PadAction::kNone;
  Pump(pad, &action);
  Expect(action == maze::PadAction::kView, "button 1 changes the view");
  SDL_JoystickSetVirtualButton(stick, 1, SDL_RELEASED);
  SDL_JoystickSetVirtualHat(stick, 0, SDL_HAT_UP);
  Pump(pad);
  Expect(pad.Read().throttle > 0.99, "the hat drives");
  SDL_JoystickDetachVirtual(plain);
  Pump(pad);
  Expect(pad.devices() == real, "pulling it out closes it");

  std::printf("%s\n", failures == 0 ? "all ok" : "FAILED");
  SDL_Quit();
  return failures == 0 ? 0 : 1;
}
