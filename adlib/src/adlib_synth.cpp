// Derived from AdLibSynth in the Atlas project, Copyright (c) 2026 Astute Systems PTY LTD. The sound effect generators
// are carried over from that code (which is under the licence in the root of that project) with the Qt and audio
// device parts taken out. The effects from kMissile on were written for this game in the same style.

#include "coriolis/adlib_synth.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "adlib_design.h"
namespace coriolis {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

std::int16_t ToInt16(double sample) {
  return static_cast<std::int16_t>(std::clamp(sample, -1.0, 1.0) * 32767.0);
}

/// Make `duration` seconds of samples: `voice` is called with the time in seconds and the progress from 0 to 1
template <typename Voice>
std::vector<std::int16_t> Make(double sample_rate, double duration, Voice&& voice) {
  const std::size_t count = static_cast<std::size_t>(duration * sample_rate);
  std::vector<std::int16_t> samples(count);
  for (std::size_t i = 0; i < count; ++i) {
    const double t = static_cast<double>(i) / sample_rate;
    samples[i] = ToInt16(voice(t, t / duration));
  }
  return samples;
}

}  // namespace

const char* SoundEffectName(SoundEffect effect) {
  switch (effect) {
    case SoundEffect::kFire:
      return "fire";
    case SoundEffect::kEnemyHit:
      return "enemy hit";
    case SoundEffect::kEnemyHitBig:
      return "enemy hit big";
    case SoundEffect::kPlayerHit:
      return "player hit";
    case SoundEffect::kPowerUp:
      return "power up";
    case SoundEffect::kWaveComplete:
      return "wave complete";
    case SoundEffect::kGameOver:
      return "game over";
    case SoundEffect::kMenuSelect:
      return "menu select";
    case SoundEffect::kMenuMove:
      return "menu move";
    case SoundEffect::kBossAlarm:
      return "boss alarm";
    case SoundEffect::kExplosion:
      return "explosion";
    case SoundEffect::kShield:
      return "shield";
    case SoundEffect::kMissile:
      return "missile";
    case SoundEffect::kHyperspace:
      return "hyperspace";
    case SoundEffect::kEcm:
      return "ecm";
    default:
      break;
  }
  return DesignName(effect);
}

AdLibSynth::AdLibSynth(std::uint32_t seed, int sample_rate)
    : sample_rate_(static_cast<double>(std::max(8000, sample_rate))), rng_(seed) {}

double AdLibSynth::Waveform(int type, double phase) {
  // AdLib OPL2 waveforms
  const double p = std::fmod(phase, kTwoPi);
  const double sine = std::sin(p);
  switch (type) {
    case 0:
      return sine;
    case 1:
      return p < kPi ? sine : 0.0;
    case 2:
      return std::abs(sine);
    case 3:
      if (p < kPi / 2.0) {
        return sine;
      }
      if (p >= kPi && p < 3.0 * kPi / 2.0) {
        return sine;
      }
      return 0.0;
    case 4:
      return p < kPi ? 1.0 : -1.0;
    case 5:
      return (p / kPi) - 1.0;
    case 6:
      return p < kPi ? (2.0 * p / kPi) - 1.0 : 3.0 - (2.0 * p / kPi);
    default:
      return sine;
  }
}

std::vector<std::int16_t> AdLibSynth::Render(SoundEffect effect) {
  switch (effect) {
    case SoundEffect::kFire:
      return Fire();
    case SoundEffect::kEnemyHit:
      return EnemyHit();
    case SoundEffect::kEnemyHitBig:
      return EnemyHitBig();
    case SoundEffect::kPlayerHit:
      return PlayerHit();
    case SoundEffect::kPowerUp:
      return PowerUp();
    case SoundEffect::kWaveComplete:
      return WaveComplete();
    case SoundEffect::kGameOver:
      return GameOver();
    case SoundEffect::kMenuSelect:
      return MenuSelect();
    case SoundEffect::kMenuMove:
      return MenuMove();
    case SoundEffect::kBossAlarm:
      return BossAlarm();
    case SoundEffect::kExplosion:
      return Explosion();
    case SoundEffect::kShield:
      return Shield();
    case SoundEffect::kMissile:
      return Missile();
    case SoundEffect::kHyperspace:
      return Hyperspace();
    case SoundEffect::kEcm:
      return Ecm();
    default:
      break;
  }
  return DesignEffect(effect, sample_rate_, rng_);
}

// A laser "pew": a falling frequency with FM modulation
std::vector<std::int16_t> AdLibSynth::Fire() {
  const double start = 880.0;
  const double end = 220.0;
  return Make(sample_rate_, 0.08, [&](double t, double progress) {
    // Exponential frequency sweep
    const double freq = start * std::pow(end / start, progress);
    // FM modulation for a metallic quality
    const double modulator = std::sin(kTwoPi * freq * 2.0 * t) * 2.0 * (1.0 - progress);
    const double carrier = std::sin(kTwoPi * freq * t + modulator);
    // Quick attack, exponential decay
    return carrier * std::exp(-t * 30.0) * 0.6;
  });
}

// A crunchy hit with noise and tone
std::vector<std::int16_t> AdLibSynth::EnemyHit() {
  const double base = 150.0;
  return Make(sample_rate_, 0.15, [&](double t, double progress) {
    const double freq = base * (1.0 - progress * 0.5);
    const double tone = Waveform(4, kTwoPi * freq * t);
    const double modulator = std::sin(kTwoPi * 80.0 * t) * 3.0;
    const double crunch = std::sin(kTwoPi * 60.0 * t + modulator);
    const double hiss = Noise() * 0.5;
    return (tone * 0.4 + crunch * 0.3 + hiss * 0.3) * std::exp(-t * 15.0);
  });
}

// A larger, more dramatic explosion
std::vector<std::int16_t> AdLibSynth::EnemyHitBig() {
  return Make(sample_rate_, 0.4, [&](double t, double progress) {
    const double freq1 = 120.0 * (1.0 - progress * 0.7);
    const double freq2 = 80.0 * (1.0 - progress * 0.5);
    const double freq3 = 200.0 * (1.0 - progress * 0.8);
    const double mod = std::sin(kTwoPi * freq2 * t) * 4.0;
    const double carrier1 = std::sin(kTwoPi * freq1 * t + mod);
    const double carrier2 = Waveform(2, kTwoPi * freq3 * t);
    const double hiss = Noise() * (1.0 - progress * 0.5);
    const double envelope1 = std::exp(-t * 8.0);
    const double envelope2 = std::exp(-t * 3.0) * 0.5;
    return carrier1 * envelope1 * 0.4 + carrier2 * envelope2 * 0.3 + hiss * envelope1 * 0.3;
  });
}

// A harsh alarm-like sound
std::vector<std::int16_t> AdLibSynth::PlayerHit() {
  return Make(sample_rate_, 0.3, [&](double t, double) {
    const double freq = std::sin(kTwoPi * 8.0 * t) > 0.0 ? 440.0 : 220.0;
    const double mod = std::sin(kTwoPi * freq * 3.0 * t) * 2.0;
    const double carrier = std::sin(kTwoPi * freq * t + mod);
    return carrier * std::exp(-t * 5.0) * 0.7;
  });
}

// A rising arpeggio with sparkle
std::vector<std::int16_t> AdLibSynth::PowerUp() {
  const double duration = 0.25;
  const std::array<double, 4> notes = {523.25, 659.25, 783.99, 1046.50};
  const double note_length = duration / static_cast<double>(notes.size());
  return Make(sample_rate_, duration, [&](double t, double) {
    const std::size_t index = std::min(notes.size() - 1, static_cast<std::size_t>(t / note_length));
    const double note_time = t - static_cast<double>(index) * note_length;
    const double freq = notes[index];
    const double mod = std::sin(kTwoPi * freq * 2.0 * t) * 1.5;
    const double carrier = std::sin(kTwoPi * freq * t + mod);
    const double shimmer = std::sin(kTwoPi * freq * 4.0 * t) * 0.2;
    const double overall = 1.0 - (t / duration) * 0.3;
    return (carrier + shimmer) * std::exp(-note_time * 20.0) * overall * 0.5;
  });
}

// A triumphant fanfare
std::vector<std::int16_t> AdLibSynth::WaveComplete() {
  const double duration = 0.6;
  const std::array<std::array<double, 3>, 3> chords = {{
      {261.63, 329.63, 392.00},  // C major
      {293.66, 369.99, 440.00},  // D major
      {329.63, 415.30, 493.88},  // E major
  }};
  return Make(sample_rate_, duration, [&](double t, double) {
    const std::size_t chord = std::min<std::size_t>(2, static_cast<std::size_t>(t / (duration / 3.0)));
    double sample = 0.0;
    for (std::size_t n = 0; n < 3; ++n) {
      const double freq = chords[chord][n];
      // Organ-like FM
      const double mod = std::sin(kTwoPi * freq * 2.0 * t);
      sample += std::sin(kTwoPi * freq * t + mod);
    }
    const double envelope = std::min(t * 20.0, 1.0) * std::exp(-(t - 0.1) * 3.0);
    return sample / 3.0 * envelope * 0.6;
  });
}

// Sad descending tones
std::vector<std::int16_t> AdLibSynth::GameOver() {
  const double duration = 1.2;
  const std::array<double, 7> notes = {392.00, 349.23, 329.63, 293.66, 261.63, 233.08, 220.00};
  const double note_length = duration / static_cast<double>(notes.size());
  return Make(sample_rate_, duration, [&](double t, double) {
    const std::size_t index = std::min(notes.size() - 1, static_cast<std::size_t>(t / note_length));
    const double note_time = t - static_cast<double>(index) * note_length;
    const double freq = notes[index];
    // A mournful organ with a sub-octave and a slow vibrato
    const double mod = std::sin(kTwoPi * freq * 1.5 * t) * 0.8;
    double carrier = std::sin(kTwoPi * freq * t + mod);
    carrier += std::sin(kTwoPi * freq * 0.5 * t) * 0.3;
    carrier *= 1.0 + std::sin(kTwoPi * 5.0 * t) * 0.02;
    return carrier * std::exp(-note_time * 8.0) * 0.5;
  });
}

// A bright confirmation beep
std::vector<std::int16_t> AdLibSynth::MenuSelect() {
  const double freq = 880.0;
  return Make(sample_rate_, 0.1, [&](double t, double) {
    const double mod = std::sin(kTwoPi * freq * 2.0 * t);
    return std::sin(kTwoPi * freq * t + mod) * std::exp(-t * 30.0) * 0.5;
  });
}

// A subtle navigation tick
std::vector<std::int16_t> AdLibSynth::MenuMove() {
  const double freq = 660.0;
  return Make(sample_rate_, 0.05, [&](double t, double) {
    return Waveform(4, kTwoPi * freq * t) * std::exp(-t * 60.0) * 0.3;
  });
}

// An ominous warning siren
std::vector<std::int16_t> AdLibSynth::BossAlarm() {
  return Make(sample_rate_, 0.8, [&](double t, double) {
    const double freq = 200.0 + 150.0 * std::sin(kTwoPi * 2.0 * t);
    const double mod = std::sin(kTwoPi * freq * 3.0 * t) * 3.0;
    const double carrier = std::sin(kTwoPi * freq * t + mod);
    const double rumble = std::sin(kTwoPi * 50.0 * t) * 0.3;
    const double envelope = std::min(t * 10.0, 1.0) * std::exp(-(t - 0.1) * 2.0);
    return (carrier * 0.7 + rumble) * envelope * 0.6;
  });
}

// A deep rumbling explosion
std::vector<std::int16_t> AdLibSynth::Explosion() {
  double last = 0.0;  // the state of the low pass filter
  return Make(sample_rate_, 0.5, [&](double t, double progress) {
    const double freq = 80.0 * (1.0 - progress * 0.6);
    const double mod = std::sin(kTwoPi * freq * 2.0 * t) * 5.0;
    const double carrier = std::sin(kTwoPi * freq * t + mod);
    // Heavy filtered noise: a simple low pass by averaging
    last = last * 0.9 + Noise() * 0.1;
    // An initial transient and then the body
    const double envelope = std::exp(-t * 50.0) * 0.5 + std::exp(-t * 5.0) * 0.5;
    return (carrier * 0.5 + last * 0.5) * envelope * 0.8;
  });
}

// A sci-fi force field
std::vector<std::int16_t> AdLibSynth::Shield() {
  const double duration = 0.3;
  return Make(sample_rate_, duration, [&](double t, double) {
    const double freq = 400.0 + 400.0 * (t / duration);
    // Ring modulation, with sparkle
    const double ring = std::sin(kTwoPi * freq * t) * std::sin(kTwoPi * freq * 1.5 * t);
    const double sparkle = std::sin(kTwoPi * freq * 4.0 * t) * 0.2;
    // A bell-like envelope
    const double envelope = std::exp(-t * 8.0) * std::min(t * 50.0, 1.0);
    return (ring * 0.7 + sparkle) * envelope * 0.5;
  });
}

// A missile leaving its rail: a whoosh that climbs and fades, with a rocket's hiss
std::vector<std::int16_t> AdLibSynth::Missile() {
  double last = 0.0;
  double phase = 0.0;
  return Make(sample_rate_, 0.45, [&](double t, double progress) {
    const double freq = 180.0 * std::pow(5.0, progress);
    phase += kTwoPi * freq / sample_rate_;
    const double mod = std::sin(phase * 0.5) * 2.0;
    const double carrier = std::sin(phase + mod);
    last = last * 0.6 + Noise() * 0.4;
    const double envelope = std::min(t * 60.0, 1.0) * std::exp(-t * 4.0);
    return (carrier * 0.4 + last * 0.6) * envelope * 0.7;
  });
}

// The hyperdrive charging: a sweep that climbs a long way, with a warble, then a rush
std::vector<std::int16_t> AdLibSynth::Hyperspace() {
  const double duration = 1.5;
  double phase = 0.0;
  return Make(sample_rate_, duration, [&](double t, double progress) {
    const double freq = 90.0 * std::pow(28.0, progress * progress);
    phase += kTwoPi * freq / sample_rate_;
    const double warble = std::sin(kTwoPi * (6.0 + 20.0 * progress) * t) * 1.5;
    const double carrier = std::sin(phase + warble);
    const double fifth = std::sin(phase * 1.5) * 0.3;
    const double envelope = std::min(t * 8.0, 1.0) * (1.0 - 0.4 * progress) * std::min((duration - t) * 12.0, 1.0);
    return (carrier * 0.7 + fifth) * envelope * 0.6;
  });
}

// The ECM pulse: a buzzing two tone warble that dies away
std::vector<std::int16_t> AdLibSynth::Ecm() {
  return Make(sample_rate_, 0.5, [&](double t, double) {
    const double freq = std::sin(kTwoPi * 24.0 * t) > 0.0 ? 1800.0 : 1200.0;
    const double buzz = Waveform(4, kTwoPi * freq * t) * 0.35;
    const double mod = std::sin(kTwoPi * freq * 2.0 * t) * 1.5;
    const double tone = std::sin(kTwoPi * freq * t + mod) * 0.5;
    return (buzz + tone) * std::exp(-t * 5.0) * std::min(t * 100.0, 1.0) * 0.6;
  });
}

}  // namespace coriolis
