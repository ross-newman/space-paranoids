#ifndef SPACE_PARANOIDS_SCORE_H_
#define SPACE_PARANOIDS_SCORE_H_

#include <cstdint>
#include <vector>

// The music of the game: an original piece written for it, made only the way an AdLib card could make it. There are nine
// channels, as on the OPL2 chip, each playing one note at a time, each note two operators with envelopes and the chip's
// four waveforms. The drums use three of the nine, like the chip's rhythm mode, with its noise.
//
// It is one loop in two layers of exactly the same length, to play in step. The patrol layer is the maze when nothing is
// near: bass and slow brass. The action layer goes on top of it as kites close in: arpeggio, lead and drums.

namespace maze {

struct Score {
  std::vector<std::int16_t> patrol;
  std::vector<std::int16_t> action;
};

constexpr double kScoreBeatsPerMinute = 100.0;
constexpr int kScoreBars = 16;
constexpr int kScoreBeatsPerBar = 4;
constexpr int kScoreChannels = 9;

/// Make the music, the same every time. Both layers together stay inside full scale.
Score RenderScore(int sample_rate = 44100);

}  // namespace maze

#endif  // SPACE_PARANOIDS_SCORE_H_
