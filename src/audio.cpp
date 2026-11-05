#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace maze {

namespace {

constexpr int kRate = 44100;
constexpr float kMusicLevel = 0.42f;   // under the effects
constexpr float kMusicFade = 2.0f;     // seconds to come in or go out
constexpr float kTensionRise = 2.5f;   // seconds for the action layer to come in
constexpr float kTensionFall = 7.0f;   // and to go, so it doesn't flutter as a kite passes a corner
constexpr double kTwoPi = 6.283185307179586;
constexpr int kChirpCount = 12;

/// A burst of data: a bright little FM note, stepping in pitch like a modem, inside a burst of the chip's noise
std::vector<std::int16_t> MakeChirp(std::mt19937& random) {
  const std::size_t count = static_cast<std::size_t>((0.018 + (random() % 16) / 1000.0) * kRate);
  const double first = 900.0 + random() % 2600;
  const double second = 900.0 + random() % 2600;
  const double ratio = 1.5 + (random() % 4) * 0.5;
  std::vector<std::int16_t> samples(count);
  std::uint32_t noise = 0x155555;
  double carrier = 0.0;
  double modulator = 0.0;
  for (std::size_t i = 0; i < count; ++i) {
    const double t = static_cast<double>(i) / count;
    const double frequency = t < 0.5 ? first : second;
    carrier += kTwoPi * frequency / kRate;
    modulator += kTwoPi * frequency * ratio / kRate;
    const std::uint32_t bit = (noise ^ (noise >> 14)) & 1U;
    noise = (noise >> 1) | (bit << 22);
    const double fm = coriolis::AdLibSynth::Waveform(1, carrier + 2.2 * coriolis::AdLibSynth::Waveform(0, modulator));
    const double hiss = (noise & 1U) != 0U ? 1.0 : -1.0;
    const double envelope = std::min(1.0, t * 40.0) * (1.0 - t) * (1.0 - t);
    samples[i] = static_cast<std::int16_t>((0.6 * fm + 0.4 * hiss) * envelope * 32767.0);
  }
  return samples;
}

}  // namespace

Audio::Audio() : mixer_(kRate, 1), score_(RenderScore(kRate)) {
  mixer_.set_master_volume(0.8f);
  std::mt19937 random(7);
  for (int i = 0; i < kChirpCount; ++i) {
    chirps_.push_back(MakeChirp(random));
  }
  SDL_AudioSpec wanted{};
  wanted.freq = kRate;
  wanted.format = AUDIO_S16SYS;
  wanted.channels = 1;
  wanted.samples = 1024;
  wanted.callback = &Audio::Callback;
  wanted.userdata = this;
  device_ = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
  if (device_ == 0) {
    std::fprintf(stderr, "no sound: %s\n", SDL_GetError());
    return;
  }
  SDL_PauseAudioDevice(device_, 0);
}

Audio::~Audio() {
  if (device_ != 0) {
    SDL_CloseAudioDevice(device_);
  }
}

void Audio::Play(coriolis::SoundEffect effect, float gain) {
  if (device_ == 0) {
    return;
  }
  SDL_LockAudioDevice(device_);
  mixer_.Play(effect, gain);
  SDL_UnlockAudioDevice(device_);
}

void Audio::Chirp(std::size_t which) {
  if (device_ == 0) {
    return;
  }
  SDL_LockAudioDevice(device_);
  chirp_ = &chirps_[which * 7 % chirps_.size()];
  chirp_position_ = 0;
  SDL_UnlockAudioDevice(device_);
}

void Audio::SetEngine(float level) {
  if (device_ == 0) {
    return;
  }
  SDL_LockAudioDevice(device_);
  mixer_.SetEngine(level);
  SDL_UnlockAudioDevice(device_);
}

void Audio::SetMusic(bool on) {
  if (device_ == 0) {
    return;
  }
  SDL_LockAudioDevice(device_);
  music_target_ = on ? 1.0f : 0.0f;
  SDL_UnlockAudioDevice(device_);
}

void Audio::SetTension(float tension) {
  if (device_ == 0) {
    return;
  }
  SDL_LockAudioDevice(device_);
  tension_target_ = std::clamp(tension, 0.0f, 1.0f);
  SDL_UnlockAudioDevice(device_);
}

void Audio::ToggleMute() {
  if (device_ == 0) {
    return;
  }
  SDL_LockAudioDevice(device_);
  muted_ = !muted_;
  SDL_UnlockAudioDevice(device_);
}

void Audio::Callback(void* self, Uint8* stream, int bytes) {
  static_cast<Audio*>(self)->Fill(reinterpret_cast<std::int16_t*>(stream), static_cast<std::size_t>(bytes) / sizeof(std::int16_t));
}

void Audio::Fill(std::int16_t* out, std::size_t frames) {
  mixer_.Mix(out, frames);
  const float music_step = 1.0f / (kMusicFade * kRate);
  const float rise = 1.0f / (kTensionRise * kRate);
  const float fall = 1.0f / (kTensionFall * kRate);
  const std::size_t length = score_.patrol.size();
  for (std::size_t i = 0; i < frames; ++i) {
    music_gain_ += std::clamp(music_target_ - music_gain_, -music_step, music_step);
    tension_gain_ += std::clamp(tension_target_ - tension_gain_, -fall, rise);
    const float music = (static_cast<float>(score_.patrol[position_]) + tension_gain_ * static_cast<float>(score_.action[position_])) * kMusicLevel * music_gain_;
    position_ = (position_ + 1) % length;
    float chirp = 0.0f;
    if (chirp_ != nullptr) {
      chirp = 0.3f * static_cast<float>((*chirp_)[chirp_position_]);
      if (++chirp_position_ >= chirp_->size()) {
        chirp_ = nullptr;
      }
    }
    const float mixed = muted_ ? 0.0f : static_cast<float>(out[i]) + music + chirp;
    out[i] = static_cast<std::int16_t>(std::clamp(mixed, -32767.0f, 32767.0f));
  }
}

}  // namespace maze
