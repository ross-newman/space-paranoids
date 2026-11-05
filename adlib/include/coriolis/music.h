#ifndef CORIOLIS_MUSIC_H_
#define CORIOLIS_MUSIC_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace coriolis {

/// The music of the station: an original instrumental score, written for this game, dark and slow and half empty, like
/// the corridors of a station at the end of a long shift. A sub bass drone breathes under everything. Slow pads of
/// detuned voices swell and fade through chords that never quite settle (a minor ninth, a major seventh with its sharp
/// eleventh, a suspended chord with a flat ninth in it that grinds against the note above), and a ghostly voice like a
/// theremin slides between long notes. A harp is plucked now and then, glass bells ring out of the dark and swell into
/// notes backwards, a sonar ping goes out and comes back as echoes, wind moves through the corridors, and in the middle a
/// slow heartbeat comes up from below. All of it goes through one long, dark reverb, and the loop is made so that the
/// tail of the reverb runs round the end into the beginning. Nothing in it is taken from any existing piece of music.
///
/// It is 24 bars in 4/4 at 80 beats a minute (72 seconds) in D minor: three eight bar sections, a bare opening, a middle
/// that fills with harp and pulse, and a last section that thins out again to the drone, so that it can be looped for as
/// long as the pilot stays in the station.
struct StationTheme {
  static constexpr double kBeatsPerMinute = 80.0;
  static constexpr int kBars = 24;
  static constexpr int kBeatsPerBar = 4;
  /// The length of the loop in seconds
  static constexpr double Seconds() { return kBars * kBeatsPerBar * 60.0 / kBeatsPerMinute; }
};

/// The loop as 16 bit mono samples, made by additive voices, plucked string, FM bell and filtered noise synthesis and a
/// reverb; the same every time. It takes a moment (a second or two) to make.
std::vector<std::int16_t> RenderStationTheme(int sample_rate = 44100);

/// The pieces of music. The station theme plays in the station, the title theme on the title screen, the fight theme
/// when there are enemies about and the jump theme during hyperspace.
enum class MusicTrack { kStation, kTitle, kFight, kJump };
constexpr std::size_t kMusicTrackCount = 4;

/// The title theme: slow and wide and a little hopeful, in E minor at 68 beats a minute for 16 bars. Slow pads and a
/// ghostly voice carry a long melody over chords that lean toward major, with bells and a slow pulse coming in
/// halfway. It is the opening of the game, so it has a lift the station's does not.
struct TitleTheme {
  static constexpr double kBeatsPerMinute = 68.0;
  static constexpr int kBars = 16;
  static constexpr int kBeatsPerBar = 4;
  static constexpr double Seconds() { return kBars * kBeatsPerBar * 60.0 / kBeatsPerMinute; }
};

/// The fight theme: hard and driving, in D minor at 132 beats a minute for 16 bars. A kick on every beat, a plucked
/// bass on the eighths, a fast plucked pattern over the top in the second half and bell stabs, over dark pads, with
/// the flat two and the major chord a fifth up for menace.
struct FightTheme {
  static constexpr double kBeatsPerMinute = 132.0;
  static constexpr int kBars = 16;
  static constexpr int kBeatsPerBar = 4;
  static constexpr double Seconds() { return kBars * kBeatsPerBar * 60.0 / kBeatsPerMinute; }
};

/// The jump theme: a rising swell of pads, a gliding voice, a run of bells climbing and a rush of noise, in D minor at
/// 120 beats a minute for 8 bars. The ship is in hyperspace for only a few seconds, so it gets the whole climb quickly.
struct JumpTheme {
  static constexpr double kBeatsPerMinute = 120.0;
  static constexpr int kBars = 8;
  static constexpr int kBeatsPerBar = 4;
  static constexpr double Seconds() { return kBars * kBeatsPerBar * 60.0 / kBeatsPerMinute; }
};

std::vector<std::int16_t> RenderTitleTheme(int sample_rate = 44100);
std::vector<std::int16_t> RenderFightTheme(int sample_rate = 44100);
std::vector<std::int16_t> RenderJumpTheme(int sample_rate = 44100);
/// Any of the pieces, by track
std::vector<std::int16_t> RenderMusicTrack(MusicTrack track, int sample_rate = 44100);

}  // namespace coriolis

#endif  // CORIOLIS_MUSIC_H_
