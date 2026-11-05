// A terminal types out the mission, and any key then starts it. Pressing a key while it types skips to the end.
// Drive a tank round a maze and shoot down the kites. Clearing them all leads on to the next maze, with more kites.
//   Up forward   Down back   Left/A Right/D turn   W guns up   S guns down   Space fire
//   1 2 3 4 cameras   V next camera   - = field of view   Esc pause, which lists the keys   Q quit from there
//   T colours   R new maze   N sound on/off   Alt+Enter fullscreen
// A controller works as well: left stick or d-pad to drive, right stick to raise the guns, A or right trigger to fire, Y next camera, X colours, Back new maze,
// shoulder buttons field of view, Start pause.
// A plain joystick drives with its stick or hat and raises the guns on axis 3, with button 0 fire, 1 next camera, 2 colours, 3 new maze and
// 4 pause.
// With a number on the command line it draws that many frames, half on each screen, and stops, to check that it runs.

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "audio.h"
#include "battle.h"
#include "maze.h"
#include "pad.h"
#include "softrender/softrender.h"
#include "terminal.h"
#include "vehicles.h"

namespace {

using namespace softrender;

constexpr int kRooms = 10;
constexpr int kFirstKites = 6;
constexpr int kMazeLines = 1200;  // the most the maze is drawn at, which holds about 60 frames a second on 24 cores
constexpr double kRadarRange = 42.0;  // the same as the radar shows
constexpr double kBlipEvery = 2.4;    // seconds between sweeps that find a kite

bool IsFullscreenKey(const SDL_Event& event) {
  return event.type == SDL_KEYDOWN && event.key.repeat == 0 && (event.key.keysym.mod & KMOD_ALT) != 0 && (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER);
}

/// Shift, Alt and the like do nothing alone, and on the opening screen must not count as the key that moves it on
bool IsModifierKey(SDL_Keycode key) {
  switch (key) {
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
    case SDLK_LCTRL:
    case SDLK_RCTRL:
    case SDLK_LALT:
    case SDLK_RALT:
    case SDLK_LGUI:
    case SDLK_RGUI:
      return true;
    default:
      return false;
  }
}

/// The desktop's own resolution is kept, and the pictures are remade to fit it
void ToggleFullscreen(SDL_Window* window) {
  const bool going_full = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0;
  SDL_SetWindowFullscreen(window, going_full ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  SDL_ShowCursor(going_full ? SDL_DISABLE : SDL_ENABLE);
}

}  // namespace

int main(int argc, char** argv) {
  const int frame_limit = argc > 1 ? std::atoi(argv[1]) : 0;
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
    std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");  // smooth, not blocky, when the maze is scaled up
  SDL_Window* window = SDL_CreateWindow("maze", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 540, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer* output = window != nullptr ? SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC) : nullptr;
  if (output == nullptr) {
    std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  const std::array<maze::Theme, 2> themes = {maze::CharcoalTheme(), maze::BlueTheme()};
  const vehicles::CrawlerParts tank_model = vehicles::MakeCrawlerParts();
  const Model kite_model = vehicles::MakeKite({{20, 8, 10}, {70, 14, 18}, {255, 40, 30}});
  std::size_t theme = 0;
  std::uint32_t seed = 1;
  int kites = kFirstKites;
  int banked_score = 0;  // from the mazes before this one
  maze::ViewMode mode = maze::ViewMode::kCockpit;

  auto plan = std::make_unique<maze::Plan>(kRooms, kRooms, seed);
  auto view = std::make_unique<maze::View>(*plan, themes[theme]);
  auto battle = std::make_unique<maze::Battle>(*plan, kites, seed);
  maze::Tank tank = maze::StartTank(*plan);

  // The opening screen is drawn at the size of the window on the screen. The maze costs far more to draw, so past
  // kMazeLines it is drawn smaller and the graphics card smooths it up. Both are made again when the window changes.
  int screen_width = 0;
  int screen_height = 0;
  int width = 0;
  int height = 0;
  SDL_Texture* texture = nullptr;
  SDL_Texture* terminal_texture = nullptr;
  std::unique_ptr<Image> maze_picture;
  std::unique_ptr<Image> terminal_picture;
  std::unique_ptr<maze::Terminal> terminal;
  Renderer renderer;
  Renderer terminal_renderer;
  // A picture that will be scaled up gets its staircases softened, as they would otherwise be scaled up too
  const auto scene_options = [&]() {
    RenderOptions options = maze::SceneOptions(themes[theme], height);
    options.soften_edges = height < screen_height;
    return options;
  };
  const auto fit_to_window = [&]() {
    int w = 0;
    int h = 0;
    SDL_GetRendererOutputSize(output, &w, &h);
    w = std::max(w, 320);
    h = std::max(h, 200);
    if (w == screen_width && h == screen_height) {
      return;
    }
    screen_width = w;
    screen_height = h;
    const double shrink = std::min(1.0, static_cast<double>(kMazeLines) / h);
    width = static_cast<int>(w * shrink);
    height = static_cast<int>(h * shrink);
    if (texture != nullptr) {
      SDL_DestroyTexture(texture);
      SDL_DestroyTexture(terminal_texture);
    }
    texture = SDL_CreateTexture(output, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height);
    terminal_texture = SDL_CreateTexture(output, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, w, h);
    maze_picture = std::make_unique<Image>(width, height);
    terminal_picture = std::make_unique<Image>(w, h);
    terminal = std::make_unique<maze::Terminal>(w, h);
    renderer.set_options(scene_options());
    terminal_renderer.set_options(maze::Terminal::Options(h));
  };
  fit_to_window();
  if (texture == nullptr) {
    std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  bool at_terminal = true;
  double terminal_time = 0.0;

  int shots_heard = 0;
  int kills_heard = 0;
  const auto new_maze = [&]() {
    ++seed;
    plan = std::make_unique<maze::Plan>(kRooms, kRooms, seed);
    view = std::make_unique<maze::View>(*plan, themes[theme]);
    battle = std::make_unique<maze::Battle>(*plan, kites, seed);
    tank = maze::StartTank(*plan);
    shots_heard = 0;
    kills_heard = 0;
  };

  maze::Pad pad;
  maze::Audio audio;
  std::size_t letters_heard = 0;
  double blip_timer = 0.0;
  double fov = maze::kFovDefault;
  bool paused = false;
  const auto next_view = [&]() { mode = static_cast<maze::ViewMode>((static_cast<int>(mode) + 1) % maze::kViewCount); };
  const auto change_fov = [&](double step) { fov = std::clamp(fov + step, maze::kFovLow, maze::kFovHigh); };
  const auto toggle_pause = [&]() {
    paused = !paused;
    if (paused) {
      audio.SetEngine(-1.0f);
    }
    audio.SetMusic(!paused && !at_terminal);
  };
  const auto change_colours = [&]() {
    theme = (theme + 1) % themes.size();
    view = std::make_unique<maze::View>(*plan, themes[theme]);
    renderer.set_options(scene_options());
  };
  const auto leave_terminal = [&]() {
    if (terminal_time < terminal->TypingTime()) {
      terminal_time = terminal->TypingTime();
    } else {
      at_terminal = false;
      audio.Play(coriolis::SoundEffect::kTitleSting, 0.7f);
      audio.SetMusic(true);
    }
  };

  bool running = true;
  int frames = 0;
  Uint32 last = SDL_GetTicks();
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
      pad.Handle(event);
      const maze::PadAction action = pad.ActionFor(event);
      if (action == maze::PadAction::kPause) {
        toggle_pause();
      } else if (paused) {
        // Only the keys below do anything until the game goes on
      } else if (at_terminal && action != maze::PadAction::kNone) {
        leave_terminal();
      } else if (action == maze::PadAction::kView) {
        next_view();
      } else if (action == maze::PadAction::kColours) {
        change_colours();
      } else if (action == maze::PadAction::kNewMaze) {
        new_maze();
      } else if (action == maze::PadAction::kFovDown) {
        change_fov(-maze::kFovStep);
      } else if (action == maze::PadAction::kFovUp) {
        change_fov(maze::kFovStep);
      }
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (IsFullscreenKey(event)) {
        ToggleFullscreen(window);
      } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0 && event.key.keysym.sym == SDLK_ESCAPE) {
        toggle_pause();
      } else if (paused) {
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_q) {
          running = false;
        }
      } else if (at_terminal && event.type == SDL_KEYDOWN && event.key.repeat == 0) {
        if (!IsModifierKey(event.key.keysym.sym)) {
          leave_terminal();
        }
      } else if (event.type == SDL_KEYDOWN) {
        // Field of view repeats while the key is held, and the rest only act once
        if (event.key.keysym.sym == SDLK_MINUS || event.key.keysym.sym == SDLK_KP_MINUS) {
          change_fov(-maze::kFovStep);
        } else if (event.key.keysym.sym == SDLK_EQUALS || event.key.keysym.sym == SDLK_KP_PLUS) {
          change_fov(maze::kFovStep);
        }
        if (event.key.repeat != 0) {
          continue;
        }
        switch (event.key.keysym.sym) {
          case SDLK_1:
          case SDLK_2:
          case SDLK_3:
          case SDLK_4:
            mode = static_cast<maze::ViewMode>(event.key.keysym.sym - SDLK_1);
            break;
          case SDLK_v:
            next_view();
            break;
          case SDLK_t:
            change_colours();
            break;
          case SDLK_r:
            new_maze();
            break;
          case SDLK_n:
            audio.ToggleMute();
            break;
          default:
            break;
        }
      }
    }

    fit_to_window();

    const Uint32 now = SDL_GetTicks();
    const double dt = std::min(0.05, (now - last) / 1000.0);
    last = now;

    if (at_terminal) {
      if (!paused) {
        terminal_time += dt;
      }
      // Each letter arrives as a burst of data, except when the rest are skipped to all at once
      const std::size_t letters = terminal->LettersTyped(terminal_time);
      if (letters == letters_heard + 1) {
        audio.Chirp(letters);
      }
      letters_heard = letters;
      terminal_renderer.Render(
          *terminal_picture, MakeCamera(screen_width, screen_height), [&](Canvas& canvas, const Camera&) { terminal->Draw(canvas.image(), terminal_time); },
          [&](Image& picture) {
            terminal->DrawGlass(picture);
            if (paused) {
              maze::DrawPauseScreen(picture);
            }
          });
      SDL_UpdateTexture(terminal_texture, nullptr, terminal_picture->data(), screen_width * 3);
      SDL_RenderClear(output);
      SDL_RenderCopy(output, terminal_texture, nullptr, nullptr);
      SDL_RenderPresent(output);
      if (frame_limit > 0 && ++frames >= frame_limit / 2) {
        terminal_time = terminal->TypingTime();
        leave_terminal();
      }
      continue;
    }

    if (!paused) {
      const Uint8* keys = SDL_GetKeyboardState(nullptr);
      maze::Controls controls;
      controls.throttle = (keys[SDL_SCANCODE_UP] ? 1.0 : 0.0) - (keys[SDL_SCANCODE_DOWN] ? 1.0 : 0.0);
      controls.elevate = (keys[SDL_SCANCODE_W] ? 1.0 : 0.0) - (keys[SDL_SCANCODE_S] ? 1.0 : 0.0);
      controls.turn = (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D] ? 1.0 : 0.0) - (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A] ? 1.0 : 0.0);
      const maze::PadState held = pad.Read();
      controls.throttle = std::clamp(controls.throttle + held.throttle, -1.0, 1.0);
      controls.turn = std::clamp(controls.turn + held.turn, -1.0, 1.0);
      controls.elevate = std::clamp(controls.elevate + held.elevate, -1.0, 1.0);
      maze::Step(&tank, controls, dt, *plan);
      battle->Update(tank, keys[SDL_SCANCODE_SPACE] != 0 || held.fire, dt, *plan);

      // The soundscape: the engine follows the tank, the music follows the nearest kite, and the radar pings what it finds
      audio.SetEngine(0.15f + 0.85f * static_cast<float>(std::fabs(tank.speed) / maze::kTankTopSpeed));
      while (shots_heard < battle->shots_fired()) {
        audio.Play(coriolis::SoundEffect::kLaserPulse, 0.4f);
        ++shots_heard;
      }
      while (kills_heard < battle->destroyed()) {
        audio.Play(coriolis::SoundEffect::kShipExplode, 0.9f);
        ++kills_heard;
      }
      double nearest = 1e9;
      for (const maze::Kite& kite : battle->kites()) {
        nearest = std::min(nearest, std::hypot(kite.x - tank.x, kite.z - tank.z));
      }
      audio.SetTension(static_cast<float>(1.0 - (nearest - 14.0) / (kRadarRange - 14.0)));
      blip_timer -= dt;
      if (blip_timer <= 0.0 && nearest < kRadarRange) {
        blip_timer = kBlipEvery;
        audio.Play(coriolis::SoundEffect::kTargetBlip, 0.12f + 0.3f * static_cast<float>(1.0 - nearest / kRadarRange));
      }

      if (battle->cleared() && !battle->exploding()) {
        audio.Play(coriolis::SoundEffect::kRankUp, 0.7f);
        banked_score += battle->destroyed() * maze::kKiteScore;
        kites += 2;
        new_maze();
      }
    }

    renderer.Render(
        *maze_picture, maze::TankCamera(tank, *plan, mode, fov, width, height),
        [&](Canvas& canvas, const Camera& camera) {
          view->Draw(canvas, camera);
          battle->Draw(canvas, camera, kite_model);
          if (mode != maze::ViewMode::kCockpit) {
            maze::DrawTank(canvas, camera, tank_model, tank);
          }
        },
        [&](Image& picture) {
          maze::DrawHud(picture, *battle, *plan, tank, mode, fov, banked_score + battle->destroyed() * maze::kKiteScore);
          if (paused) {
            maze::DrawPauseScreen(picture);
          }
        });
    SDL_UpdateTexture(texture, nullptr, maze_picture->data(), width * 3);
    SDL_RenderClear(output);
    SDL_RenderCopy(output, texture, nullptr, nullptr);
    SDL_RenderPresent(output);
    if (frame_limit > 0 && ++frames >= frame_limit) {
      running = false;
    }
  }

  SDL_DestroyTexture(texture);
  SDL_DestroyTexture(terminal_texture);
  SDL_DestroyRenderer(output);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
