// The sound design of the game, written for it: every effect after the original AdLib synthesizer's twelve.
//
// The voice is the one the AdLib sound card had: two operators, a modulator that pushes the phase of a carrier, each
// with its own envelope, as on the Yamaha OPL2 chip. An effect is a few notes of such a voice, with a pitch that can
// sweep, layered with filtered noise, clicks and crackle: a laser is a quick falling FM zap, a hull hit is a thud, a
// burst of dull noise and a metal ping, and the launch from the bay is a whole second-by-second score of clamps, hiss
// and a turbine winding up. Nothing is recorded.

#include "adlib_design.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace coriolis {

namespace {

constexpr double kTwoPi = 6.283185307179586;

/// The pitch of a MIDI note: 69 is the A of 440 Hz, 60 is middle C
double Hz(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

/// One operator of the FM voice
struct Op {
  int wave = 0;           // an OPL2 waveform, see AdLibSynth::Waveform
  double ratio = 1.0;     // a multiple of the note's frequency
  double level = 1.0;
  double attack = 0.002;  // seconds, a straight line up
  double decay = 0.15;    // the time constant of the fall to `sustain`
  double sustain = 0.0;
  double release = 0.05;  // the time constant of the fall once the key is up
  double feedback = 0.0;  // how much the operator's last sample bends its own phase
};

/// A voice: a modulator and a carrier. `index` is how far the modulator pushes the carrier's phase, in radians.
struct Patch {
  Op mod;
  Op car;
  double index = 2.0;
  double vibrato_hz = 0.0;
  double vibrato = 0.0;  // a fraction of the pitch
  double tremolo_hz = 0.0;
  double tremolo = 0.0;  // 0 to 1
};

/// A struck sound: it dies away on its own. `decay` is how long the carrier rings, and `ratio` the modulator's multiple
/// of the pitch: whole numbers are harmonic, 2.76 or 3.5 are the inharmonic ones that make bells and metal.
Patch Bell(double ratio, double index, double decay) {
  Patch p;
  p.index = index;
  p.mod = {0, ratio, 1.0, 0.001, decay * 0.4, 0.0, 0.05};
  p.car = {0, 1.0, 1.0, 0.001, decay, 0.0, 0.1};
  return p;
}

/// A struck sound with a choice of waveform for the carrier, and a separate decay for the modulator, so the sound is
/// bright at the start and purer after
Patch Fm(int car_wave, double car_decay, double ratio, double index, double mod_decay, int mod_wave = 0) {
  Patch p;
  p.index = index;
  p.mod = {mod_wave, ratio, 1.0, 0.001, mod_decay, 0.0, 0.03};
  p.car = {car_wave, 1.0, 1.0, 0.001, car_decay, 0.0, 0.03};
  return p;
}

/// A held sound: it swells in over `attack`, stays at full strength while the note is held, and dies over `release`
Patch Held(int car_wave, double attack, double release, double ratio, double index) {
  Patch p;
  p.index = index;
  p.mod = {0, ratio, 1.0, attack, 1.0, 1.0, release};
  p.car = {car_wave, 1.0, 1.0, attack, 1.0, 1.0, release};
  return p;
}

/// An envelope, one sample at a time
class Env {
 public:
  Env(const Op& op, double rate, double gate)
      : op_(op),
        attack_samples_(std::max(1.0, op.attack * rate)),
        decay_(std::exp(-1.0 / (std::max(op.decay, 1e-4) * rate))),
        release_(std::exp(-1.0 / (std::max(op.release, 1e-4) * rate))),
        gate_samples_(gate * rate) {}

  double Next() {
    double level;
    if (n_ < attack_samples_) {
      level = n_ / attack_samples_;
    } else {
      level = op_.sustain + (1.0 - op_.sustain) * fall_;
      fall_ *= decay_;
    }
    if (n_ >= gate_samples_) {
      if (!released_) {
        released_ = true;
        held_ = level;
      }
      held_ *= release_;
      level = held_;
    }
    n_ += 1.0;
    return level;
  }

 private:
  Op op_;
  double attack_samples_;
  double decay_;
  double release_;
  double gate_samples_;
  double n_ = 0.0;
  double fall_ = 1.0;
  double held_ = 0.0;
  bool released_ = false;
};

double Wave(int type, double phase) { return type == 0 ? std::sin(phase) : AdLibSynth::Waveform(type, phase); }

/// A stretch of sound being built up by laying notes and noise over each other
class Track {
 public:
  Track(double rate, double seconds, std::mt19937& rng) : rate_(rate), buf_(static_cast<std::size_t>(seconds * rate), 0.0), rng_(rng) {}

  /// A note of a patch starting at `start` seconds, with the key held for `gate` seconds. The pitch goes from f0 to f1
  /// over the gate, in a curve that sounds even, and stays at f1 after.
  void Note(const Patch& p, double start, double gate, double f0, double f1, double gain) {
    const std::size_t first = Index(start);
    const std::size_t last = std::min(buf_.size(), Index(start + gate + 7.0 * std::max(p.car.release, 0.01)));
    if (first >= last || f0 <= 0.0 || f1 <= 0.0) {
      return;
    }
    Env mod_env(p.mod, rate_, gate);
    Env car_env(p.car, rate_, gate);
    const double gate_samples = std::max(1.0, gate * rate_);
    const double step = std::pow(f1 / f0, 1.0 / gate_samples);
    double f = f0;
    double car_phase = 0.0;
    double mod_phase = 0.0;
    double last_mod = 0.0;
    for (std::size_t i = first; i < last; ++i) {
      const double n = static_cast<double>(i - first);
      const double t = n / rate_;
      double pitch = f;
      if (p.vibrato > 0.0) {
        pitch *= 1.0 + p.vibrato * std::sin(kTwoPi * p.vibrato_hz * t);
      }
      car_phase += kTwoPi * pitch * p.car.ratio / rate_;
      mod_phase += kTwoPi * pitch * p.mod.ratio / rate_;
      const double m = Wave(p.mod.wave, mod_phase + p.mod.feedback * last_mod) * mod_env.Next() * p.mod.level;
      last_mod = m;
      double s = Wave(p.car.wave, car_phase + p.index * m) * car_env.Next() * p.car.level;
      if (p.tremolo > 0.0) {
        s *= 1.0 - p.tremolo * 0.5 * (1.0 - std::cos(kTwoPi * p.tremolo_hz * t));
      }
      buf_[i] += s * gain;
      if (n < gate_samples) {
        f *= step;
      }
    }
  }

  /// Noise through a one pole low pass whose brightness (0 to 1, more is brighter) moves from `bright` to `dull` over
  /// the sound, optionally with the lows taken out, swelling in over `attack` seconds and dying with the time constant
  /// `decay`
  void Noise(double start, double dur, double bright, double dull, double attack, double decay, double gain,
             bool high_pass = false) {
    const std::size_t first = Index(start);
    const std::size_t last = std::min(buf_.size(), Index(start + dur));
    double y = 0.0;
    double slow = 0.0;
    for (std::size_t i = first; i < last; ++i) {
      const double t = static_cast<double>(i - first) / rate_;
      const double a = std::clamp(bright + (dull - bright) * (t / dur), 0.003, 1.0);
      y += a * (Random() - y);
      // Keep the loudness about the same however dull the filter is
      double s = y * std::min(8.0, std::sqrt((2.0 - a) / a)) * 0.6;
      if (high_pass) {
        slow += 0.03 * (s - slow);
        s -= slow;
      }
      const double rise = attack > 0.0 ? std::min(1.0, t / attack) : 1.0;
      const double fall = decay > 0.0 ? std::exp(-t / decay) : 1.0;
      const double edge = std::min(1.0, (dur - t) / 0.01);
      buf_[i] += s * rise * fall * edge * gain;
    }
  }

  /// A tick
  void Click(double start, double gain) { Noise(start, 0.006, 0.9, 0.9, 0.0, 0.0015, gain); }

  /// Little bursts at random times, thinning out as the sound goes on: debris, sparks, static
  void Crackle(double start, double dur, double per_second, double gain, double decay) {
    const std::size_t first = Index(start);
    const std::size_t last = std::min(buf_.size(), Index(start + dur));
    for (std::size_t i = first; i < last; ++i) {
      const double t = static_cast<double>(i - first) / rate_;
      if ((Random() + 1.0) * 0.5 >= per_second * std::exp(-t / decay) / rate_) {
        continue;
      }
      const double amp = gain * (0.35 + 0.65 * (Random() + 1.0) * 0.5) * (Random() > 0.0 ? 1.0 : -1.0);
      for (std::size_t k = 0; k < 28 && i + k < buf_.size(); ++k) {
        buf_[i + k] += amp * std::exp(-static_cast<double>(k) / 6.0) * std::cos(static_cast<double>(k) * 0.85);
      }
    }
  }

  /// The samples, scaled so the loudest is `peak` (0 to 1), with the last few milliseconds faded so there is no click
  std::vector<std::int16_t> Finish(double peak) const {
    double loudest = 0.0;
    for (const double v : buf_) {
      loudest = std::max(loudest, std::fabs(v));
    }
    const double scale = loudest > 1e-9 ? peak / loudest : 0.0;
    const std::size_t fade = std::min(buf_.size(), static_cast<std::size_t>(0.004 * rate_));
    std::vector<std::int16_t> out(buf_.size());
    for (std::size_t i = 0; i < buf_.size(); ++i) {
      const double edge = i + fade >= buf_.size() ? static_cast<double>(buf_.size() - i) / static_cast<double>(fade) : 1.0;
      out[i] = static_cast<std::int16_t>(std::clamp(buf_[i] * scale * edge, -1.0, 1.0) * 32767.0);
    }
    return out;
  }

 private:
  std::size_t Index(double seconds) const { return static_cast<std::size_t>(std::max(0.0, seconds) * rate_); }
  double Random() { return dist_(rng_); }

  double rate_;
  std::vector<double> buf_;
  std::mt19937& rng_;
  std::uniform_real_distribution<double> dist_{-1.0, 1.0};
};

struct Design {
  const char* name;
  double seconds;
  double peak;
  void (*build)(Track&);
};

// The order is the order of SoundEffect from kLaserPulse
const Design kDesigns[] = {
    // ---- Weapons ----
    {"laser pulse", 0.22, 0.8,
     [](Track& t) {
       // A bright zap that falls a long way in a tenth of a second, with a thinner copy a hair behind it
       t.Note(Fm(0, 0.07, 1.0, 5.0, 0.04), 0.0, 0.11, 2600, 380, 1.0);
       t.Note(Fm(0, 0.05, 2.0, 3.0, 0.03), 0.004, 0.08, 3900, 700, 0.3);
       t.Click(0.0, 0.15);
     }},
    {"laser beam", 0.5, 0.8,
     [](Track& t) {
       // A held buzz: a sine bent hard by its modulator with a fast wobble, a flutter, and a little hiss on top
       Patch beam = Held(0, 0.008, 0.06, 2.0, 3.0);
       beam.vibrato_hz = 38.0;
       beam.vibrato = 0.02;
       beam.tremolo_hz = 60.0;
       beam.tremolo = 0.35;
       t.Note(beam, 0.0, 0.3, 1500, 1050, 0.8);
       t.Note(Held(0, 0.01, 0.06, 1.0, 1.5), 0.0, 0.3, 3000, 2100, 0.1);
       t.Noise(0.0, 0.36, 0.5, 0.35, 0.01, 0.3, 0.08, true);
     }},
    {"laser military", 0.55, 0.85,
     [](Track& t) {
       // Two hard square zaps close together over a low thump
       const Patch zap = Fm(4, 0.09, 1.0, 3.0, 0.05);
       t.Note(zap, 0.0, 0.14, 2000, 170, 0.9);
       t.Note(zap, 0.075, 0.14, 2400, 200, 0.9);
       t.Note(Fm(0, 0.18, 1.0, 1.0, 0.1), 0.0, 0.2, 95, 42, 1.0);
       t.Click(0.0, 0.5);
       t.Click(0.075, 0.5);
     }},
    {"enemy laser", 0.3, 0.75,
     [](Track& t) {
       // Lower and rougher than ours, so a shot at you is not mistaken for one from you
       t.Note(Fm(4, 0.09, 1.0, 3.0, 0.06), 0.0, 0.16, 1100, 260, 0.9);
       t.Noise(0.0, 0.12, 0.8, 0.5, 0.0, 0.05, 0.3, true);
     }},

    // ---- Damage ----
    {"shield hit", 0.55, 0.8,
     [](Track& t) {
       // An electric sizzle: a wavering inharmonic tone with sparks
       Patch p = Fm(0, 0.3, 1.414, 6.0, 0.25);
       p.tremolo_hz = 31.0;
       p.tremolo = 0.6;
       t.Note(p, 0.0, 0.1, 760, 520, 0.8);
       t.Note(Fm(0, 0.2, 3.01, 4.0, 0.15), 0.0, 0.05, 1900, 1500, 0.4);
       t.Crackle(0.0, 0.4, 260, 0.6, 0.12);
       t.Noise(0.0, 0.25, 0.6, 0.3, 0.002, 0.08, 0.3, true);
     }},
    {"hull hit", 0.5, 0.9,
     [](Track& t) {
       // A thud, a burst of dull noise, and the ping of struck metal
       t.Note(Fm(0, 0.14, 1.0, 1.2, 0.05), 0.0, 0.05, 130, 52, 1.0);
       t.Noise(0.0, 0.2, 0.35, 0.08, 0.001, 0.07, 0.8);
       t.Note(Bell(2.76, 4.0, 0.12), 0.0, 0.02, 1250, 1180, 0.25);
       t.Crackle(0.01, 0.3, 150, 0.5, 0.1);
     }},
    {"shields down", 1.0, 0.8,
     [](Track& t) {
       // The field failing: a fall from high to low with a stutter and static
       Patch p = Fm(0, 0.6, 1.5, 4.0, 0.4);
       p.tremolo_hz = 18.0;
       p.tremolo = 0.5;
       t.Note(p, 0.0, 0.7, 1500, 110, 1.0);
       t.Crackle(0.0, 0.7, 120, 0.6, 0.4);
       t.Noise(0.0, 0.7, 0.5, 0.05, 0.002, 0.3, 0.3, true);
     }},
    {"ship explode", 2.4, 1.0,
     [](Track& t) {
       // A crack, a boom that falls through the floor, a roar that dulls, a second swell of rumble, and debris
       t.Noise(0.0, 0.03, 0.9, 0.9, 0.0, 0.01, 1.0);
       t.Note(Fm(0, 0.6, 2.0, 3.5, 0.5), 0.0, 1.4, 95, 24, 1.0);
       t.Note(Fm(0, 0.3, 1.0, 1.5, 0.3), 0.02, 0.5, 60, 30, 0.6);
       t.Noise(0.0, 1.9, 0.55, 0.015, 0.002, 0.55, 1.0);
       t.Noise(0.25, 1.4, 0.25, 0.02, 0.15, 0.4, 0.5);
       t.Crackle(0.05, 2.0, 220, 0.8, 0.7);
     }},
    {"rock break", 0.8, 0.85,
     [](Track& t) {
       // Stone: a dull knock, grit, and a patter of fragments
       t.Noise(0.0, 0.5, 0.3, 0.05, 0.001, 0.12, 1.0);
       t.Note(Fm(0, 0.1, 1.0, 1.0, 0.05), 0.0, 0.05, 150, 70, 0.8);
       t.Crackle(0.0, 0.6, 300, 0.9, 0.2);
     }},

    // ---- Missiles and countermeasures ----
    {"missile launch", 1.3, 0.85,
     [](Track& t) {
       // The clunk of the release, then the rocket catching: noise that opens up and a saw that climbs
       t.Note(Bell(2.76, 3.0, 0.06), 0.0, 0.01, 240, 220, 0.7);
       t.Click(0.0, 0.8);
       t.Noise(0.1, 1.1, 0.04, 0.35, 0.15, 0.6, 0.9);
       Patch rocket = Held(5, 0.15, 0.25, 1.0, 1.5);
       rocket.tremolo_hz = 40.0;
       rocket.tremolo = 0.3;
       t.Note(rocket, 0.1, 0.7, 110, 700, 0.5);
     }},
    {"missile blast", 1.4, 0.95,
     [](Track& t) {
       // Sharper and shorter than a ship going up
       t.Noise(0.0, 0.025, 0.9, 0.9, 0.0, 0.008, 1.0);
       t.Note(Fm(0, 0.35, 2.0, 3.0, 0.3), 0.0, 0.8, 140, 36, 1.0);
       t.Noise(0.0, 1.0, 0.5, 0.02, 0.001, 0.3, 1.0);
       t.Crackle(0.03, 1.0, 200, 0.7, 0.35);
     }},
    {"missile lock", 0.9, 0.6,
     [](Track& t) {
       // Three quick beeps, then a steady note that says it has you
       const Patch beep = Fm(0, 0.04, 2.0, 1.0, 0.03);
       for (int k = 0; k < 3; ++k) {
         t.Note(beep, 0.09 * k, 0.045, 1568, 1568, 0.8);
       }
       t.Note(Held(0, 0.005, 0.03, 2.0, 0.8), 0.3, 0.4, 2093, 2093, 0.6);
     }},
    {"target blip", 0.1, 0.45,
     [](Track& t) { t.Note(Fm(0, 0.03, 2.0, 1.0, 0.02), 0.0, 0.02, 1568, 1480, 1.0); }},
    {"ecm pulse", 1.4, 0.8,
     [](Track& t) {
       // A warble that climbs and falls, thrown back as echoes
       Patch p = Fm(0, 0.5, 1.0, 3.0, 0.4);
       p.tremolo_hz = 22.0;
       p.tremolo = 0.5;
       const double echo_gain[3] = {1.0, 0.55, 0.3};
       for (int k = 0; k < 3; ++k) {
         const double at = 0.15 * k;
         t.Note(p, at, 0.4, 500, 2400, echo_gain[k]);
         t.Note(p, at + 0.42, 0.5, 2400, 300, 0.6 * echo_gain[k]);
       }
       t.Noise(0.0, 0.8, 0.7, 0.5, 0.005, 0.2, 0.25, true);
     }},
    {"energy bomb", 3.2, 1.0,
     [](Track& t) {
       // A charge winding up, then the whole sky going: a huge thump, a long roar, and a ring that hangs
       t.Note(Held(0, 0.4, 0.05, 2.0, 2.0), 0.0, 0.55, 80, 1400, 0.6);
       t.Noise(0.55, 0.05, 0.9, 0.9, 0.0, 0.02, 1.0);
       t.Note(Fm(0, 0.9, 1.5, 4.0, 0.7), 0.55, 1.8, 70, 22, 1.0);
       t.Noise(0.55, 2.4, 0.5, 0.01, 0.003, 0.7, 1.0);
       t.Crackle(0.6, 2.4, 200, 0.7, 0.9);
       t.Note(Bell(3.5, 3.0, 0.9), 0.6, 0.2, 2200, 2100, 0.35);
       t.Note(Bell(2.76, 2.0, 1.0), 0.62, 0.2, 3300, 3200, 0.2);
     }},

    // ---- Flight ----
    {"launch bay", 7.0, 0.9,
     [](Track& t) {
       // The ride out, second by second. Two clamps let go, steam hisses, a turbine winds from a growl to a scream under a
       // swelling rumble, a rush of air builds as the mouth comes near, and a ping as the ship clears the door.
       t.Note(Bell(2.76, 3.0, 0.1), 0.0, 0.01, 210, 200, 0.9);
       t.Click(0.0, 1.0);
       t.Note(Bell(2.76, 3.0, 0.08), 0.28, 0.01, 160, 150, 0.7);
       t.Click(0.28, 0.7);
       t.Noise(0.35, 1.4, 0.5, 0.2, 0.05, 0.6, 0.45, true);
       t.Noise(0.5, 6.0, 0.03, 0.08, 1.5, 0.0, 0.8);
       Patch turbine = Held(5, 2.5, 0.4, 1.5, 1.6);
       turbine.tremolo_hz = 9.0;
       turbine.tremolo = 0.15;
       t.Note(turbine, 0.6, 5.6, 55, 460, 0.55);
       t.Note(Held(0, 3.0, 0.3, 3.0, 1.0), 0.6, 5.6, 220, 1840, 0.18);
       t.Noise(4.6, 2.0, 0.15, 0.5, 1.2, 0.0, 0.55);
       t.Note(Bell(3.5, 2.0, 0.9), 6.05, 0.05, 1760, 1760, 0.25);
     }},
    {"docking", 2.8, 0.85,
     [](Track& t) {
       // Permission: two bell notes. Then a low drone settles, the clamps close, and the air comes back with a hiss.
       t.Note(Bell(3.5, 2.0, 0.35), 0.0, 0.02, Hz(88), Hz(88), 0.7);
       t.Note(Bell(3.5, 2.0, 0.45), 0.13, 0.02, Hz(95), Hz(95), 0.7);
       t.Noise(0.2, 1.1, 0.04, 0.02, 0.2, 0.5, 0.7);
       t.Note(Held(0, 0.2, 0.3, 1.0, 1.2), 0.2, 0.9, 140, 60, 0.4);
       t.Note(Bell(2.76, 3.0, 0.08), 1.3, 0.01, 180, 170, 0.9);
       t.Click(1.3, 0.9);
       t.Note(Bell(2.76, 3.0, 0.08), 1.55, 0.01, 140, 130, 0.8);
       t.Click(1.55, 0.8);
       t.Note(Fm(0, 0.3, 1.0, 1.0, 0.2), 1.3, 0.1, 90, 60, 0.6);
       t.Noise(1.6, 1.1, 0.5, 0.2, 0.03, 0.4, 0.5, true);
     }},
    {"hyper charge", 4.4, 0.85,
     [](Track& t) {
       // The four second countdown as one long rise: a whine that climbs more than five octaves with a wobble, a deep
       // saw under it, a rush that brightens, and a tick as each second goes
       Patch whine = Held(0, 3.0, 0.2, 1.01, 3.0);
       whine.vibrato_hz = 7.0;
       whine.vibrato = 0.02;
       t.Note(whine, 0.0, 4.0, 70, 3200, 0.7);
       t.Note(Held(5, 3.5, 0.2, 1.0, 1.0), 0.0, 4.0, 40, 400, 0.4);
       t.Noise(0.0, 4.0, 0.02, 0.5, 3.8, 0.0, 0.6);
       for (int k = 1; k <= 3; ++k) {
         t.Note(Fm(0, 0.04, 2.0, 1.0, 0.03), static_cast<double>(k), 0.03, 1760, 1760, 0.5);
       }
     }},
    {"hyper exit", 2.0, 0.95,
     [](Track& t) {
       // Out of hyperspace: a thump and a flash of noise, a sweep that falls all the way, and bells that fade
       t.Noise(0.0, 0.06, 0.9, 0.9, 0.0, 0.03, 1.0);
       t.Note(Fm(0, 0.8, 2.0, 3.0, 0.5), 0.0, 1.0, 3000, 110, 0.7);
       t.Note(Fm(0, 0.5, 1.0, 2.0, 0.4), 0.0, 0.8, 70, 28, 0.9);
       t.Noise(0.0, 1.5, 0.4, 0.02, 0.01, 0.5, 0.7);
       for (int k = 0; k < 4; ++k) {
         t.Note(Bell(3.5, 2.0, 0.6), 0.1 + 0.07 * k, 0.02, 2400.0 - 300.0 * k, 2300.0 - 300.0 * k, 0.2);
       }
     }},
    {"cargo scoop", 1.0, 0.8,
     [](Track& t) {
       // A suction that opens up, a thunk as the canister hits the hold, and a little run of chimes
       t.Noise(0.0, 0.35, 0.03, 0.4, 0.2, 0.0, 0.8);
       t.Note(Fm(0, 0.1, 1.0, 1.0, 0.05), 0.3, 0.04, 180, 80, 0.9);
       t.Click(0.3, 0.6);
       const double chimes[3] = {84, 88, 91};
       for (int k = 0; k < 3; ++k) {
         t.Note(Bell(3.5, 2.0, 0.2), 0.4 + 0.06 * k, 0.02, Hz(chimes[k]), Hz(chimes[k]), 0.6);
       }
     }},
    {"dock computer on", 0.4, 0.5,
     [](Track& t) {
       const Patch p = Fm(0, 0.08, 2.0, 1.0, 0.05);
       t.Note(p, 0.0, 0.06, 880, 880, 0.8);
       t.Note(p, 0.09, 0.08, 1320, 1320, 0.8);
     }},
    {"dock computer off", 0.4, 0.5,
     [](Track& t) {
       const Patch p = Fm(0, 0.08, 2.0, 1.0, 0.05);
       t.Note(p, 0.0, 0.06, 1320, 1320, 0.8);
       t.Note(p, 0.09, 0.08, 880, 880, 0.8);
     }},

    // ---- Warnings ----
    {"mass lock", 0.7, 0.7,
     [](Track& t) {
       // Refused: two harsh low square pulses
       const Patch p = Held(4, 0.004, 0.03, 1.0, 1.5);
       t.Note(p, 0.0, 0.2, 330, 330, 0.8);
       t.Note(p, 0.26, 0.2, 247, 247, 0.8);
     }},
    {"low energy", 0.4, 0.45,
     [](Track& t) {
       const Patch p = Fm(0, 0.06, 1.0, 0.8, 0.05);
       t.Note(p, 0.0, 0.05, 988, 988, 0.8);
       t.Note(p, 0.14, 0.05, 988, 988, 0.8);
     }},
    {"hull alarm", 1.2, 0.75,
     [](Track& t) {
       // An urgent two note siren a tritone apart
       const Patch p = Held(4, 0.003, 0.02, 2.0, 1.2);
       for (int k = 0; k < 6; ++k) {
         t.Note(p, 0.18 * k, 0.15, k % 2 == 0 ? 880 : 622, k % 2 == 0 ? 880 : 622, 0.8);
       }
     }},
    {"police siren", 1.6, 0.75,
     [](Track& t) {
       // A wail: up and down twice
       const Patch p = Held(4, 0.01, 0.03, 1.0, 1.0);
       for (int k = 0; k < 4; ++k) {
         t.Note(p, 0.38 * k, 0.36, k % 2 == 0 ? 600 : 1100, k % 2 == 0 ? 1100 : 600, 0.8);
       }
     }},
    {"error", 0.35, 0.6,
     [](Track& t) {
       // A buzzer
       t.Note(Held(5, 0.003, 0.03, 1.0, 2.5), 0.0, 0.22, 150, 150, 0.9);
       t.Note(Held(4, 0.003, 0.03, 1.0, 0.0), 0.0, 0.22, 75, 75, 0.5);
     }},

    // ---- The station and the menus ----
    {"trade buy", 0.6, 0.6,
     [](Track& t) {
       // Two coin pings, the second a fourth up
       t.Note(Bell(3.5, 2.5, 0.25), 0.0, 0.01, Hz(95), Hz(95), 0.8);
       t.Note(Bell(3.5, 2.5, 0.4), 0.075, 0.01, Hz(100), Hz(100), 0.8);
     }},
    {"trade sell", 0.7, 0.65,
     [](Track& t) {
       // A ratchet climbing, and the ding of the till
       for (int k = 0; k < 5; ++k) {
         t.Click(0.04 * k, 0.5);
         t.Note(Fm(0, 0.03, 1.0, 1.0, 0.02), 0.04 * k, 0.01, 1000 + 150 * k, 1000 + 150 * k, 0.5);
       }
       t.Note(Bell(3.5, 2.0, 0.35), 0.24, 0.01, 2349, 2349, 0.9);
       t.Note(Bell(2.76, 2.0, 0.3), 0.24, 0.01, 3520, 3520, 0.3);
     }},
    {"equipment fit", 1.1, 0.7,
     [](Track& t) {
       // Servos clicking and whirring, then a chime: fitted
       for (int k = 0; k < 10; ++k) {
         t.Click(0.03 * k, 0.4);
       }
       t.Note(Held(5, 0.05, 0.1, 2.0, 1.0), 0.0, 0.3, 200, 700, 0.3);
       t.Note(Bell(3.5, 2.0, 0.5), 0.4, 0.02, Hz(84), Hz(84), 0.7);
       t.Note(Bell(3.5, 2.0, 0.6), 0.4, 0.02, Hz(91), Hz(91), 0.6);
     }},
    {"ui move", 0.08, 0.35, [](Track& t) { t.Note(Fm(0, 0.02, 2.0, 0.8, 0.015), 0.0, 0.01, 1500, 1200, 1.0); }},
    {"ui select", 0.2, 0.45,
     [](Track& t) {
       const Patch p = Fm(0, 0.07, 2.0, 1.0, 0.05);
       t.Note(p, 0.0, 0.04, 1175, 1175, 0.9);
       t.Note(p, 0.06, 0.06, 1568, 1568, 0.9);
     }},
    {"ui back", 0.2, 0.4,
     [](Track& t) {
       const Patch p = Fm(0, 0.07, 2.0, 1.0, 0.05);
       t.Note(p, 0.0, 0.04, 1175, 1175, 0.9);
       t.Note(p, 0.06, 0.06, 880, 880, 0.9);
     }},

    // ---- Moments ----
    {"bounty", 0.8, 0.65,
     [](Track& t) {
       // A quick rising run of bells
       const double run[4] = {84, 88, 91, 96};
       for (int k = 0; k < 4; ++k) {
         t.Note(Bell(3.5, 2.5, 0.3), 0.06 * k, 0.01, Hz(run[k]), Hz(run[k]), 0.8);
       }
     }},
    {"rank up", 2.0, 0.85,
     [](Track& t) {
       // Brass: a sine whose modulator starts bright and settles, climbing a major arpeggio to a held chord
       Patch brass;
       brass.index = 3.0;
       brass.mod = {0, 1.0, 1.0, 0.02, 0.25, 0.4, 0.2};
       brass.car = {0, 1.0, 1.0, 0.02, 0.2, 0.7, 0.25};
       t.Note(brass, 0.0, 0.2, 392.0, 392.0, 0.8);
       t.Note(brass, 0.2, 0.2, 523.25, 523.25, 0.8);
       t.Note(brass, 0.4, 0.2, 659.25, 659.25, 0.8);
       t.Note(brass, 0.6, 0.9, 784.0, 784.0, 0.8);
       t.Note(brass, 0.6, 0.9, 1046.5, 1046.5, 0.4);
       t.Note(brass, 0.6, 0.9, 523.25, 523.25, 0.4);
       t.Note(Bell(3.5, 2.0, 0.8), 0.65, 0.02, 2093, 2093, 0.2);
     }},
    {"death", 3.4, 0.95,
     [](Track& t) {
       // The ship going up, heard from inside; then a slow minor chord, and bells falling away
       t.Noise(0.0, 0.04, 0.9, 0.9, 0.0, 0.015, 1.0);
       t.Note(Fm(0, 0.8, 2.0, 3.5, 0.6), 0.0, 1.6, 80, 22, 1.0);
       t.Noise(0.0, 1.8, 0.5, 0.015, 0.003, 0.6, 0.9);
       t.Crackle(0.05, 1.5, 200, 0.7, 0.6);
       Patch chord = Held(0, 0.3, 1.2, 2.0, 1.5);
       chord.vibrato_hz = 4.5;
       chord.vibrato = 0.01;
       const double minor[3] = {57, 60, 64};
       for (int k = 0; k < 3; ++k) {
         t.Note(chord, 0.9, 1.2, Hz(minor[k]), Hz(minor[k]), 0.3);
       }
       const double fall[3] = {76, 72, 69};
       for (int k = 0; k < 3; ++k) {
         t.Note(Bell(3.5, 2.0, 0.9), 1.6 + 0.4 * k, 0.02, Hz(fall[k]), Hz(fall[k]), 0.35);
       }
     }},
    {"title sting", 3.0, 0.85,
     [](Track& t) {
       // A pad that swells in on an open fifth over a bass, and a run of bells that settles on a long top note
       t.Note(Held(0, 0.05, 0.4, 1.0, 2.0), 0.0, 1.0, Hz(33), Hz(33), 0.8);
       t.Note(Held(0, 0.05, 0.4, 1.0, 2.0), 1.0, 1.2, Hz(40), Hz(40), 0.7);
       const Patch pad = Held(0, 0.6, 0.6, 1.0, 0.8);
       t.Note(pad, 0.0, 1.6, Hz(57), Hz(57), 0.4);
       t.Note(pad, 0.2, 1.6, Hz(64), Hz(64), 0.35);
       t.Note(pad, 0.4, 1.6, Hz(72), Hz(72), 0.3);
       const double run[4] = {81, 84, 88, 93};
       for (int k = 0; k < 4; ++k) {
         t.Note(Bell(3.5, 2.0, 0.7), 0.8 + 0.15 * k, 0.02, Hz(run[k]), Hz(run[k]), 0.45);
       }
       t.Note(Bell(3.5, 2.0, 1.2), 1.45, 0.02, Hz(96), Hz(96), 0.35);
     }},
};

constexpr std::size_t kDesignCount = static_cast<std::size_t>(SoundEffect::kCount) - static_cast<std::size_t>(SoundEffect::kLaserPulse);
static_assert(sizeof(kDesigns) / sizeof(kDesigns[0]) == kDesignCount, "one design for each designed effect");

const Design* DesignFor(SoundEffect effect) {
  const int index = static_cast<int>(effect) - static_cast<int>(SoundEffect::kLaserPulse);
  return index >= 0 && static_cast<std::size_t>(index) < kDesignCount ? &kDesigns[index] : nullptr;
}

}  // namespace

const char* DesignName(SoundEffect effect) {
  const Design* d = DesignFor(effect);
  return d != nullptr ? d->name : "?";
}

std::vector<std::int16_t> DesignEffect(SoundEffect effect, double sample_rate, std::mt19937& rng) {
  const Design* d = DesignFor(effect);
  if (d == nullptr) {
    return {};
  }
  Track track(sample_rate, d->seconds, rng);
  d->build(track);
  return track.Finish(d->peak);
}

}  // namespace coriolis
