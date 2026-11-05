#ifndef SPACE_PARANOIDS_AUDIO_H_
#define SPACE_PARANOIDS_AUDIO_H_

#include <SDL.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "coriolis/sound.h"
#include "score.h"

// All the sound of the game on an SDL audio device. Effects and the engine come from the AdLib mixer in adlib/ and the
// music from score.h, so every sound is FM from the same synthesis. With no sound device the game runs
// silent. Needs SDL_INIT_AUDIO.

namespace maze {

class Audio {
 public:
  Audio();
  ~Audio();
  Audio(const Audio&) = delete;
  Audio& operator=(const Audio&) = delete;

  /// Play an effect at `gain`, 0 to 1
  void Play(coriolis::SoundEffect effect, float gain);
  /// A burst of data, for one letter arriving on the terminal. Each `which` sounds a little different.
  void Chirp(std::size_t which);
  /// How hard the engine is working, 0 to 1, or below 0 for off
  void SetEngine(float level);
  /// Start or stop the music
  void SetMusic(bool on);
  /// How close the fight is, 0 to 1: the action layer of the music follows it
  void SetTension(float tension);
  /// Turn all sound on or off
  void ToggleMute();
  bool opened() const { return device_ != 0; }

 private:
  static void Callback(void* self, Uint8* stream, int bytes);
  void Fill(std::int16_t* out, std::size_t frames);

  coriolis::SoundMixer mixer_;
  Score score_;
  std::vector<std::vector<std::int16_t>> chirps_;
  const std::vector<std::int16_t>* chirp_ = nullptr;  // the one playing
  std::size_t chirp_position_ = 0;
  SDL_AudioDeviceID device_ = 0;
  std::size_t position_ = 0;
  float music_target_ = 0.0f;
  float tension_target_ = 0.0f;
  float music_gain_ = 0.0f;
  float tension_gain_ = 0.0f;
  bool muted_ = false;
};

}  // namespace maze

#endif  // SPACE_PARANOIDS_AUDIO_H_
