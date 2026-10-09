#pragma once
#include <cstddef>
#include <memory>
namespace w8_native
{
/* A movie-owned PCM voice on the same engine as recovered soundman. */
class MovieAudio
{
  public:
    MovieAudio(unsigned rate, unsigned channels);
    ~MovieAudio();
    MovieAudio(const MovieAudio&) = delete;
    MovieAudio& operator=(const MovieAudio&) = delete;
    void append(const float* samples, size_t frames);
    void start();
    void finish();
    bool drained() const;

  private:
    struct State;
    std::unique_ptr<State> state;
};
bool movie_audio_available();
} // namespace w8_native
