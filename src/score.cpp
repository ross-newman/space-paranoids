#include "score.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "coriolis/adlib_synth.h"

namespace maze {

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr int kBeats = kScoreBars * kScoreBeatsPerBar;

double Hz(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

/// A voice of two operators, as the chip has. Only its waveforms 0 to 3 are used here: sine, half sine, absolute sine
/// and quarter sine pulses. `index` is how far the modulator pushes the carrier's phase, in radians.
struct Patch {
  int mod_wave = 0;
  int car_wave = 0;
  double mod_ratio = 1.0;
  double index = 1.0;
  double index_decay = 0.0;  // the time constant of the fall of the index to `index_floor`, or 0 to hold it
  double index_floor = 0.0;
  double feedback = 0.0;  // how much the modulator bends its own phase
  double attack = 0.005;
  double decay = 0.2;
  double sustain = 1.0;
  double release = 0.06;
  double vibrato = 0.0;  // a fraction of the pitch, at 6.1 Hz as on the chip's deep setting
};

struct Note {
  double beat;
  double beats;
  double midi;
  double level;
};

/// One channel: it plays one note at a time, and a new note cuts off what was ringing, as keying on does on the chip.
/// The end of the loop wraps round to its start, so that the loop has no seam.
std::vector<float> RenderChannel(std::vector<Note> notes, const Patch& p, std::size_t length, double rate) {
  std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.beat < b.beat; });
  const double samples_per_beat = static_cast<double>(length) / kBeats;
  std::vector<float> out(length, 0.0f);
  for (std::size_t n = 0; n < notes.size(); ++n) {
    const Note& note = notes[n];
    const std::size_t start = static_cast<std::size_t>(note.beat * samples_per_beat);
    const std::size_t next = n + 1 < notes.size() ? static_cast<std::size_t>(notes[n + 1].beat * samples_per_beat) : static_cast<std::size_t>(notes[0].beat * samples_per_beat) + length;
    const double held = note.beats * samples_per_beat / rate;
    const std::size_t count = std::min(next - start, static_cast<std::size_t>((held + p.release * 6.0) * rate));
    double carrier = 0.0;
    double modulator = 0.0;
    double last = 0.0;
    double before = 0.0;
    const double base = Hz(note.midi);
    for (std::size_t i = 0; i < count; ++i) {
      const double t = static_cast<double>(i) / rate;
      double envelope = t < p.attack ? t / p.attack : p.sustain + (1.0 - p.sustain) * std::exp(-(t - p.attack) / p.decay);
      if (t > held) {
        envelope *= std::exp(-(t - held) / p.release);
      }
      // A quick fade as the next note takes the channel, to save a click
      const std::size_t left = count - i;
      if (left < 200) {
        envelope *= static_cast<double>(left) / 200.0;
      }
      const double index = p.index_decay > 0.0 ? p.index_floor + (p.index - p.index_floor) * std::exp(-t / p.index_decay) : p.index;
      const double frequency = base * (1.0 + p.vibrato * std::sin(kTwoPi * 6.1 * t));
      const double mod = coriolis::AdLibSynth::Waveform(p.mod_wave, modulator + p.feedback * (last + before) * 0.5);
      before = last;
      last = mod;
      const double car = coriolis::AdLibSynth::Waveform(p.car_wave, carrier + index * mod);
      modulator = std::fmod(modulator + kTwoPi * frequency * p.mod_ratio / rate, kTwoPi);
      carrier = std::fmod(carrier + kTwoPi * frequency / rate, kTwoPi);
      out[(start + i) % length] += static_cast<float>(car * envelope * note.level);
    }
  }
  return out;
}

/// The chip's noise: a shift register, not a recording
class Register {
 public:
  double Next() {
    const std::uint32_t bit = (state_ ^ (state_ >> 14)) & 1U;
    state_ = (state_ >> 1) | (bit << 22);
    return (state_ & 1U) != 0U ? 1.0 : -1.0;
  }

 private:
  std::uint32_t state_ = 0x155555;
};

/// The chord of each bar: a root for the bass and three notes for the brass. The moves are F minor's own: down to the
/// flat sixth, up through the flat third and flat seventh, and in the second half the major chord on the fifth.
struct Chord {
  double root;
  std::array<double, 3> brass;
};

constexpr Chord kFm{41, {53, 56, 60}};
constexpr Chord kDb{37, {53, 56, 61}};
constexpr Chord kAb{44, {51, 56, 60}};
constexpr Chord kEb{39, {51, 55, 58}};
constexpr Chord kBbm{34, {53, 58, 61}};
constexpr Chord kC{36, {52, 55, 60}};

constexpr std::array<Chord, kScoreBars> kChords = {kFm, kFm, kDb, kDb, kAb, kAb, kEb, kEb, kBbm, kBbm, kDb, kDb, kFm, kFm, kC, kC};

/// Which note of the chord each sixteenth of the arpeggio plays: 3 is the lowest again, an octave up
constexpr std::array<int, 16> kArpeggio = {0, 1, 2, 3, 2, 1, 3, 2, 0, 2, 1, 3, 2, 3, 1, 2};

/// The long lead line over the second half
constexpr Note kLead[] = {
    {32, 2, 77, 1}, {34, 1, 80, 1},   {35, 1, 82, 1},   {36, 3, 82, 1}, {39, 1, 80, 1},   {40, 2, 77, 1}, {42, 2, 85, 1}, {44, 4, 84, 1},
    {48, 2, 84, 1}, {50, 1, 80, 1},   {51, 1, 79, 1},   {52, 4, 77, 1}, {56, 2, 79, 1},   {58, 2, 76, 1}, {60, 2, 72, 1}, {62, 2, 79, 1},
};

std::vector<float> Kick(std::size_t length, double rate) {
  std::vector<float> out(length, 0.0f);
  const double samples_per_beat = static_cast<double>(length) / kBeats;
  for (int beat = 0; beat < kBeats; ++beat) {
    const std::size_t start = static_cast<std::size_t>(beat * samples_per_beat);
    double phase = 0.0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(0.2 * rate); ++i) {
      const double t = static_cast<double>(i) / rate;
      phase += kTwoPi * (45.0 + 110.0 * std::exp(-t / 0.03)) / rate;
      out[(start + i) % length] += static_cast<float>(coriolis::AdLibSynth::Waveform(0, phase) * std::exp(-t / 0.075));
    }
  }
  return out;
}

std::vector<float> Snare(std::size_t length, double rate) {
  std::vector<float> out(length, 0.0f);
  const double samples_per_beat = static_cast<double>(length) / kBeats;
  Register noise;
  for (int beat = 1; beat < kBeats; beat += 2) {
    const std::size_t start = static_cast<std::size_t>(beat * samples_per_beat);
    for (std::size_t i = 0; i < static_cast<std::size_t>(0.16 * rate); ++i) {
      const double t = static_cast<double>(i) / rate;
      const double tone = coriolis::AdLibSynth::Waveform(0, kTwoPi * 190.0 * t) * std::exp(-t / 0.04);
      out[(start + i) % length] += static_cast<float>(0.5 * tone + 0.6 * noise.Next() * std::exp(-t / 0.05));
    }
  }
  return out;
}

std::vector<float> Hat(std::size_t length, double rate) {
  std::vector<float> out(length, 0.0f);
  const double samples_per_beat = static_cast<double>(length) / kBeats;
  Register noise;
  for (int eighth = 1; eighth < kBeats * 2; eighth += 2) {
    const std::size_t start = static_cast<std::size_t>(eighth * samples_per_beat / 2.0);
    double previous = 0.0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(0.06 * rate); ++i) {
      const double t = static_cast<double>(i) / rate;
      const double value = noise.Next();
      out[(start + i) % length] += static_cast<float>((value - previous) * 0.5 * std::exp(-t / 0.018));
      previous = value;
    }
  }
  return out;
}

}  // namespace

Score RenderScore(int sample_rate) {
  const double rate = static_cast<double>(sample_rate);
  const std::size_t length = static_cast<std::size_t>(kBeats * 60.0 / kScoreBeatsPerMinute * rate);

  // Channel 1: the bass, pulsing on the eighths with a jump to the octave
  Patch bass;
  bass.index = 2.6;
  bass.index_decay = 0.12;
  bass.index_floor = 0.7;
  bass.feedback = 0.8;
  bass.car_wave = 2;
  bass.decay = 0.16;
  bass.sustain = 0.45;
  bass.release = 0.03;
  std::vector<Note> bass_notes;
  for (int bar = 0; bar < kScoreBars; ++bar) {
    const double root = kChords[static_cast<std::size_t>(bar)].root;
    for (int eighth = 0; eighth < 8; ++eighth) {
      const bool up = eighth == 3 || eighth == 6;
      bass_notes.push_back({bar * 4.0 + eighth * 0.5, 0.4, root + (up ? 12.0 : 0.0), eighth == 0 ? 1.0 : 0.82});
    }
  }

  // Channels 2 to 4: slow brass, swelling into each chord
  Patch brass;
  brass.index = 1.9;
  brass.index_floor = 1.1;
  brass.index_decay = 0.5;
  brass.feedback = 0.5;
  brass.attack = 0.16;
  brass.decay = 0.4;
  brass.sustain = 0.8;
  brass.release = 0.3;
  brass.vibrato = 0.002;
  std::array<std::vector<Note>, 3> brass_notes;
  for (int bar = 0; bar < kScoreBars; ++bar) {
    const Chord& chord = kChords[static_cast<std::size_t>(bar)];
    const bool same = bar % 2 == 1 && kChords[static_cast<std::size_t>(bar - 1)].root == chord.root;
    if (same) {
      continue;  // a chord held over two bars
    }
    for (std::size_t voice = 0; voice < 3; ++voice) {
      brass_notes[voice].push_back({bar * 4.0, 7.8, chord.brass[voice], 1.0});
    }
  }

  // Channel 5: the arpeggio, an unbroken run of sixteenths, more insistent in the second half
  Patch pluck;
  pluck.car_wave = 1;
  pluck.mod_ratio = 3.0;
  pluck.index = 2.4;
  pluck.index_decay = 0.08;
  pluck.index_floor = 0.3;
  pluck.attack = 0.002;
  pluck.decay = 0.11;
  pluck.sustain = 0.0;
  pluck.release = 0.04;
  std::vector<Note> arpeggio_notes;
  for (int bar = 0; bar < kScoreBars; ++bar) {
    const Chord& chord = kChords[static_cast<std::size_t>(bar)];
    for (int step = 0; step < 16; ++step) {
      const int which = kArpeggio[static_cast<std::size_t>(step)];
      const double midi = 12.0 + (which == 3 ? chord.brass[0] + 12.0 : chord.brass[static_cast<std::size_t>(which)]);
      const double accent = step % 4 == 0 ? 1.0 : 0.72;
      arpeggio_notes.push_back({bar * 4.0 + step * 0.25, 0.24, midi, accent * (bar < 8 ? 0.8 : 1.0)});
    }
  }

  // Channel 6: the lead
  Patch lead;
  lead.car_wave = 0;
  lead.mod_wave = 0;
  lead.index = 2.6;
  lead.index_decay = 0.35;
  lead.index_floor = 1.3;
  lead.feedback = 0.9;
  lead.attack = 0.04;
  lead.decay = 0.3;
  lead.sustain = 0.8;
  lead.release = 0.12;
  lead.vibrato = 0.004;
  const std::vector<Note> lead_notes(std::begin(kLead), std::end(kLead));

  // The nine channels, each a line of the mix
  const std::vector<float> channels[kScoreChannels] = {
      RenderChannel(bass_notes, bass, length, rate),
      RenderChannel(brass_notes[0], brass, length, rate),
      RenderChannel(brass_notes[1], brass, length, rate),
      RenderChannel(brass_notes[2], brass, length, rate),
      RenderChannel(arpeggio_notes, pluck, length, rate),
      RenderChannel(lead_notes, lead, length, rate),
      Kick(length, rate),
      Snare(length, rate),
      Hat(length, rate),
  };

  std::vector<float> patrol(length, 0.0f);
  std::vector<float> action(length, 0.0f);
  const std::size_t samples_per_beat = length / kBeats;
  for (std::size_t i = 0; i < length; ++i) {
    // The brass ducks on every beat, breathing with the pulse
    const double since_beat = static_cast<double>(i % samples_per_beat) / rate;
    const float pump = static_cast<float>(1.0 - 0.45 * std::exp(-since_beat / 0.14));
    patrol[i] = 0.5f * channels[0][i] + 0.17f * pump * (channels[1][i] + channels[2][i] + channels[3][i]);
    action[i] = 0.14f * channels[4][i] + 0.17f * channels[5][i] + 0.5f * channels[6][i] + 0.17f * channels[7][i] + 0.1f * channels[8][i];
  }

  // A soft repeat of the arpeggio and lead, three sixteenths behind
  const std::size_t echo = samples_per_beat * 3 / 4;
  for (std::size_t i = 0; i < length; ++i) {
    action[(i + echo) % length] += 0.22f * (0.14f * channels[4][i] + 0.17f * channels[5][i]);
  }

  float peak = 0.001f;
  for (std::size_t i = 0; i < length; ++i) {
    peak = std::max(peak, std::fabs(patrol[i]) + std::fabs(action[i]));
  }
  const float scale = 0.95f / peak;
  Score score;
  score.patrol.resize(length);
  score.action.resize(length);
  for (std::size_t i = 0; i < length; ++i) {
    score.patrol[i] = static_cast<std::int16_t>(patrol[i] * scale * 32767.0f);
    score.action[i] = static_cast<std::int16_t>(action[i] * scale * 32767.0f);
  }
  return score;
}

}  // namespace maze
