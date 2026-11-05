#include "coriolis/sound.h"

#include <algorithm>
#include <cmath>

#include "coriolis/music.h"

namespace coriolis {

SoundMixer::SoundMixer(int sample_rate, std::uint32_t seed) : sample_rate_(std::max(8000, sample_rate)) {
  AdLibSynth synth(seed, sample_rate_);
  for (int i = 0; i < kSoundEffectCount; ++i) {
    bank_[static_cast<std::size_t>(i)] = synth.Render(static_cast<SoundEffect>(i));
  }
  voices_.reserve(kMaxVoices);
}

void SoundMixer::set_master_volume(float volume) { master_ = std::clamp(volume, 0.0f, 1.0f); }

void SoundMixer::PrepareMusic() {
  for (std::size_t i = 0; i < kMusicTrackCount; ++i) {
    PrepareMusic(static_cast<MusicTrack>(i));
  }
}

void SoundMixer::PrepareMusic(MusicTrack track) {
  MusicState& m = music_[static_cast<std::size_t>(track)];
  if (m.samples.empty()) {
    m.samples = RenderMusicTrack(track, sample_rate_);
  }
}

void SoundMixer::SetMusic(MusicTrack track, float level) {
  if (level > 0.0f) {
    PrepareMusic(track);
  }
  music_[static_cast<std::size_t>(track)].target = level;
}

double SoundMixer::Duration(SoundEffect effect) const {
  const std::size_t i = static_cast<std::size_t>(effect);
  return i < bank_.size() ? static_cast<double>(bank_[i].size()) / sample_rate_ : 0.0;
}

void SoundMixer::Play(SoundEffect effect, float gain) {
  if (static_cast<int>(effect) < 0 || static_cast<int>(effect) >= kSoundEffectCount || gain <= 0.001f) {
    return;
  }
  // Too many of the same sound: the oldest of them is cut off and this one takes its place
  std::size_t same = 0;
  std::size_t oldest_same = voices_.size();
  for (std::size_t i = 0; i < voices_.size(); ++i) {
    if (voices_[i].effect == effect) {
      ++same;
      if (oldest_same == voices_.size() || voices_[i].position > voices_[oldest_same].position) {
        oldest_same = i;
      }
    }
  }
  if (same >= kMaxPerEffect) {
    voices_[oldest_same] = {effect, 0, std::min(gain, 1.0f)};
    return;
  }
  if (voices_.size() >= kMaxVoices) {
    // The voice furthest through its sound goes
    std::size_t furthest = 0;
    for (std::size_t i = 1; i < voices_.size(); ++i) {
      if (voices_[i].position > voices_[furthest].position) {
        furthest = i;
      }
    }
    voices_[furthest] = {effect, 0, std::min(gain, 1.0f)};
    return;
  }
  voices_.push_back({effect, 0, std::min(gain, 1.0f)});
}

void SoundMixer::Mix(std::int16_t* out, std::size_t frames) {
  scratch_.assign(frames, 0.0f);
  std::vector<float>& sum = scratch_;
  for (Voice& v : voices_) {
    const std::vector<std::int16_t>& samples = bank_[static_cast<std::size_t>(v.effect)];
    const std::size_t count = std::min(frames, samples.size() - std::min(v.position, samples.size()));
    for (std::size_t n = 0; n < count; ++n) {
      sum[n] += static_cast<float>(samples[v.position + n]) * (1.0f / 32768.0f) * v.gain;
    }
    v.position += frames;
  }
  // Finished sounds go
  voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                               [&](const Voice& v) { return v.position >= bank_[static_cast<std::size_t>(v.effect)].size(); }),
                voices_.end());
  for (std::size_t n = 0; n < frames; ++n) {
    // The music, each track faded in and out on its own; a track starts from the top each time it comes back
    for (MusicState& m : music_) {
      if (m.gain > 1e-4 || m.target >= 0.0f) {
        m.gain += ((m.target >= 0.0f ? static_cast<double>(m.target) : 0.0) - m.gain) /
                  ((m.target >= 0.0f ? 1.2 : 0.45) * static_cast<double>(sample_rate_));
        if (!m.samples.empty()) {
          sum[n] += static_cast<float>(m.samples[m.position]) * (1.0f / 32768.0f) * static_cast<float>(m.gain);
          m.position = (m.position + 1) % m.samples.size();
        }
        if (m.target < 0.0f && m.gain <= 1e-4) {
          m.gain = 0.0;
          m.position = 0;
        }
      }
    }
    // The engine drone, if there is one
    if (engine_gain_ > 1e-4 || engine_target_ >= 0.0f) {
      const double rate = static_cast<double>(sample_rate_);
      engine_gain_ += ((engine_target_ >= 0.0f ? 1.0 : 0.0) - engine_gain_) / (0.5 * rate);
      engine_level_ += (std::max(0.0f, engine_target_) - engine_level_) / (0.25 * rate);
      const double f = 38.0 + 62.0 * engine_level_;
      engine_phase_ = std::fmod(engine_phase_ + 6.283185307179586 * f / rate, 6.283185307179586);
      engine_phase2_ = std::fmod(engine_phase2_ + 6.283185307179586 * f * 1.5 / rate, 6.283185307179586);
      engine_rng_ = engine_rng_ * 1664525U + 1013904223U;
      const double white = static_cast<double>(engine_rng_ >> 8) / 8388608.0 - 1.0;
      engine_noise_ += (0.02 + 0.1 * engine_level_) * (white - engine_noise_);
      const double tone = std::sin(engine_phase_ + 1.4 * std::sin(engine_phase2_)) * 0.55 + std::sin(2.0 * engine_phase_) * 0.15;
      sum[n] += static_cast<float>((tone * 0.5 + engine_noise_ * 1.0) * engine_gain_ * (0.10 + 0.16 * engine_level_));
    }
    // A soft limiter, so a lot of sounds at once crowd together rather than crackle
    out[n] = static_cast<std::int16_t>(std::tanh(sum[n] * master_) * 32767.0f);
  }
}

}  // namespace coriolis
