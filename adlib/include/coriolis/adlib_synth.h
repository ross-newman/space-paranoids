#ifndef CORIOLIS_ADLIB_SYNTH_H_
#define CORIOLIS_ADLIB_SYNTH_H_

// Derived from AdLibSynth in the Atlas project, Copyright (c) 2026 Astute Systems PTY LTD. The sound effect generators
// are carried over from that code (which is under the licence in the root of that project) with the Qt and audio
// device parts taken out, so this builds with no dependencies. See the README, "Sound".

#include <cstdint>
#include <random>
#include <vector>

namespace coriolis {

/// The sound effects. The first twelve are the effects of the original AdLib synthesizer; the next three were written
/// for this game in the same style, and the rest are the sound design of the game itself.
enum class SoundEffect {
  kFire,          // a laser pulse
  kEnemyHit,      // something small hit or destroyed
  kEnemyHitBig,   // something big hit
  kPlayerHit,     // the player's ship takes damage
  kPowerUp,       // picking something up
  kWaveComplete,  // a fanfare
  kGameOver,      // a sad run down
  kMenuSelect,    // confirm
  kMenuMove,      // navigate
  kBossAlarm,     // a warning siren
  kExplosion,     // a large explosion
  kShield,        // a force field
  // For this game
  kMissile,     // a missile leaving its rail
  kHyperspace,  // the hyperdrive charging
  kEcm,         // the ECM pulse

  // The sound design of the game. Weapons
  kLaserPulse,     // the player's pulse laser
  kLaserBeam,      // the beam laser: a sustained sizzling bolt
  kLaserMilitary,  // the military laser: a punchy double shot
  kEnemyLaser,     // another ship's laser, harsher and lower
  // Damage
  kShieldHit,    // a shot absorbed by the shields: an electric sizzle
  kHullHit,      // a shot that gets through: a metal crunch
  kShieldsDown,  // the shields fail
  kShipExplode,  // a ship destroyed: crack, boom, roar and debris
  kRockBreak,    // an asteroid or canister broken up
  // Missiles and countermeasures
  kMissileLaunch,  // the clunk of release and the rocket catching
  kMissileBlast,   // a missile going off
  kMissileLock,    // three beeps and a steady tone
  kTargetBlip,     // a target chosen
  kEcmPulse,       // the ECM: a rising warble with echoes
  kEnergyBomb,     // a charge, a huge thump and a shimmer
  // Flight
  kLaunchBay,      // the whole ride out of the bay: clamps, hiss, a rising turbine and a rush (6.7 s)
  kDocking,        // the docking chime, the clamps and the air coming back
  kHyperCharge,    // the hyperdrive winding up over the four second countdown, with a tick each second
  kHyperExit,      // arriving: a thump, a falling sweep and a shimmer
  kCargoScoop,     // a canister pulled in
  kDockComputerOn,   // two rising beeps
  kDockComputerOff,  // two falling beeps
  // Warnings
  kMassLock,    // hyperdrive refused: two harsh low pulses
  kLowEnergy,   // two soft beeps
  kHullAlarm,   // the hull is failing: an urgent siren
  kPoliceSiren,  // a wail
  kError,       // a buzzer
  // The station and the menus
  kTradeBuy,       // two coin pings
  kTradeSell,      // a ratchet and a bell
  kEquipmentFit,   // servos, then a chime
  kUiMove,         // a soft tick
  kUiSelect,       // two rising notes
  kUiBack,         // two falling notes
  // Moments
  kBounty,     // a bounty paid
  kRankUp,     // a brass fanfare
  kDeath,      // an explosion, a minor chord and falling bells
  kTitleSting, // the start of a game: a pad, a bass and a run of bells
  kCount
};

constexpr int kSoundEffectCount = static_cast<int>(SoundEffect::kCount);

const char* SoundEffectName(SoundEffect effect);

/// Makes the sound effects as 16 bit mono samples by FM synthesis, in the style of the Yamaha OPL2 chip of the AdLib
/// sound card: sine carriers modulated by sine modulators, the OPL2's other waveforms, exponential envelopes, and noise.
/// Nothing is read from a file. The same seed gives the same samples.
class AdLibSynth {
 public:
  explicit AdLibSynth(std::uint32_t seed = 1, int sample_rate = 44100);

  int sample_rate() const { return static_cast<int>(sample_rate_); }

  /// The samples of an effect, at full scale (the effects are mixed down by the player)
  std::vector<std::int16_t> Render(SoundEffect effect);

  /// An OPL2 waveform at a phase in radians: 0 sine, 1 half sine, 2 absolute sine, 3 quarter sine pulses, 4 square,
  /// 5 saw, 6 triangle. Any other type is a sine.
  static double Waveform(int type, double phase);

 private:
  double Noise() { return noise_(rng_); }

  std::vector<std::int16_t> Fire();
  std::vector<std::int16_t> EnemyHit();
  std::vector<std::int16_t> EnemyHitBig();
  std::vector<std::int16_t> PlayerHit();
  std::vector<std::int16_t> PowerUp();
  std::vector<std::int16_t> WaveComplete();
  std::vector<std::int16_t> GameOver();
  std::vector<std::int16_t> MenuSelect();
  std::vector<std::int16_t> MenuMove();
  std::vector<std::int16_t> BossAlarm();
  std::vector<std::int16_t> Explosion();
  std::vector<std::int16_t> Shield();
  std::vector<std::int16_t> Missile();
  std::vector<std::int16_t> Hyperspace();
  std::vector<std::int16_t> Ecm();

  double sample_rate_;
  std::mt19937 rng_;
  std::uniform_real_distribution<double> noise_{-1.0, 1.0};
};

}  // namespace coriolis

#endif  // CORIOLIS_ADLIB_SYNTH_H_
