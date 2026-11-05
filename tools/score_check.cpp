// Make the music, check it, and write it to WAV files to listen to:
//   score_check [directory]

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "score.h"

namespace {

void WriteWav(const std::string& path, const std::vector<std::int16_t>& samples, int rate) {
  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    return;
  }
  const auto word = [file](std::uint32_t v, int bytes) { std::fwrite(&v, static_cast<std::size_t>(bytes), 1, file); };
  const std::uint32_t data = static_cast<std::uint32_t>(samples.size() * 2);
  std::fwrite("RIFF", 4, 1, file);
  word(36 + data, 4);
  std::fwrite("WAVEfmt ", 8, 1, file);
  word(16, 4);
  word(1, 2);
  word(1, 2);
  word(static_cast<std::uint32_t>(rate), 4);
  word(static_cast<std::uint32_t>(rate) * 2, 4);
  word(2, 2);
  word(16, 2);
  std::fwrite("data", 4, 1, file);
  word(data, 4);
  std::fwrite(samples.data(), 2, samples.size(), file);
  std::fclose(file);
}

double Rms(const std::vector<std::int16_t>& s) {
  double sum = 0.0;
  for (const std::int16_t v : s) {
    sum += static_cast<double>(v) * v;
  }
  return std::sqrt(sum / s.size()) / 32767.0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string directory = argc > 1 ? argv[1] : ".";
  constexpr int kRate = 44100;
  const auto begin = std::chrono::steady_clock::now();
  const maze::Score score = maze::RenderScore(kRate);
  const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();

  std::vector<std::int16_t> both(score.patrol.size());
  int peak = 0;
  for (std::size_t i = 0; i < both.size(); ++i) {
    const int sum = score.patrol[i] + score.action[i];
    peak = std::max(peak, std::abs(sum));
    both[i] = static_cast<std::int16_t>(std::max(-32767, std::min(32767, sum)));
  }
  const double seconds = static_cast<double>(both.size()) / kRate;
  const double wanted = maze::kScoreBars * maze::kScoreBeatsPerBar * 60.0 / maze::kScoreBeatsPerMinute;
  // The join: the largest step across the end of the loop against the largest anywhere
  const int seam = std::abs(both.front() - both.back());
  int largest_step = 0;
  for (std::size_t i = 1; i < both.size(); ++i) {
    largest_step = std::max(largest_step, std::abs(both[i] - both[i - 1]));
  }
  const bool ok = score.patrol.size() == score.action.size() && std::fabs(seconds - wanted) < 0.01 && peak <= 32767 && Rms(score.patrol) > 0.04 && Rms(score.action) > 0.04 && seam <= largest_step;
  std::printf("%.1f s loop, made in %.2f s, peak %.2f, level patrol %.3f action %.3f, seam %d (largest step %d): %s\n", seconds, took, peak / 32767.0, Rms(score.patrol), Rms(score.action), seam, largest_step, ok ? "ok" : "BAD");
  WriteWav(directory + "/score_patrol.wav", score.patrol, kRate);
  WriteWav(directory + "/score_full.wav", both, kRate);
  return ok ? 0 : 1;
}
