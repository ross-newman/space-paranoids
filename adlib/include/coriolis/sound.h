#ifndef CORIOLIS_SOUND_H_
#define CORIOLIS_SOUND_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "coriolis/adlib_synth.h"
#include "coriolis/music.h"

namespace coriolis {

/// A sound to play and how loud: 1 is full volume
struct SoundCue {
  SoundEffect effect = SoundEffect::kMenuMove;
  float gain = 1.0f;
};

/// Plays the effects of `AdLibSynth` on top of each other into a buffer of samples. It does not touch an audio device:
/// the front end asks it for `Mix`ed samples whenever the device wants more. Every effect is made once, when the mixer
/// is built. It is not thread safe, so a front end that mixes on an audio thread has to lock round `Play` and `Mix`.
class SoundMixer {
 public:
  explicit SoundMixer(int sample_rate = 44100, std::uint32_t seed = 1);

  int sample_rate() const { return sample_rate_; }

  /// Start an effect. At most `kMaxVoices` sounds play at once, and no more than `kMaxPerEffect` of the same effect:
  /// past either limit the oldest of the same effect (or the oldest of all) is cut short.
  void Play(SoundEffect effect, float gain = 1.0f);
  void Play(const SoundCue& cue) { Play(cue.effect, cue.gain); }

  /// Fill `out` with `frames` mono samples of everything that is playing now, and move on that much time
  void Mix(std::int16_t* out, std::size_t frames);

  /// Silence everything that is playing
  void Stop() { voices_.clear(); }

  /// The engine's drone, which goes on as long as the ship is flying: a low FM growl and a rumble that rise in pitch
  /// and loudness with `level`, 0 to 1 (the throttle). Below 0 it fades out. It follows changes smoothly.
  void SetEngine(float level) { engine_target_ = level; }
  float engine_gain() const { return static_cast<float>(engine_gain_); }

  /// The station music (see music.h), which loops for as long as it is wanted. `level` is 0 to 1 and below 0 for none:
  /// the music fades in and out over a second or two, and starts again from the beginning each time it comes back.
  /// The first call that wants music makes it if `PrepareMusic` has not (it takes a moment, so a front end calls that
  /// when it starts).
  void SetMusic(float level) { SetMusic(MusicTrack::kStation, level); }
  /// The same for any of the tracks. Several can play at once, each fading on its own, so changing the music is a
  /// crossfade: ask for the new one and let the old one go.
  void SetMusic(MusicTrack track, float level);
  /// Make one track now, or with no argument all of them
  void PrepareMusic();
  void PrepareMusic(MusicTrack track);
  float music_gain() const { return music_gain(MusicTrack::kStation); }
  float music_gain(MusicTrack track) const { return static_cast<float>(music_[static_cast<std::size_t>(track)].gain); }

  /// 0 (silent) to 1 (full): the volume of everything
  void set_master_volume(float volume);
  float master_volume() const { return master_; }

  std::size_t active_voices() const { return voices_.size(); }
  /// The length in seconds of an effect
  double Duration(SoundEffect effect) const;

  static constexpr std::size_t kMaxVoices = 24;
  static constexpr std::size_t kMaxPerEffect = 6;

 private:
  struct Voice {
    SoundEffect effect;
    std::size_t position = 0;
    float gain = 1.0f;
  };

  int sample_rate_;
  float master_ = 0.7f;
  std::array<std::vector<std::int16_t>, kSoundEffectCount> bank_;
  std::vector<Voice> voices_;
  std::vector<float> scratch_;

  // The engine drone
  float engine_target_ = -1.0f;
  double engine_gain_ = 0.0;
  double engine_level_ = 0.0;
  double engine_phase_ = 0.0;
  double engine_phase2_ = 0.0;
  double engine_noise_ = 0.0;
  std::uint32_t engine_rng_ = 12345;

  // The music, a track of the loops of music.h each
  struct MusicState {
    std::vector<std::int16_t> samples;
    std::size_t position = 0;
    float target = -1.0f;
    double gain = 0.0;
  };
  std::array<MusicState, kMusicTrackCount> music_;
};

}  // namespace coriolis

#endif  // CORIOLIS_SOUND_H_
