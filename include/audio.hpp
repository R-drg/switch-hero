#pragma once
#include "song.hpp"
#include <SDL.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace fret {
class Decoder {
  public:
    int rate = 0, channels = 0;
    double duration = 0;
    virtual ~Decoder() = default;
    virtual size_t read(float *samples, size_t frames) = 0;
};
std::unique_ptr<Decoder> openDecoder(const fs::path &path);
class Audio {
    struct Stream;
    std::vector<std::unique_ptr<Stream>> streams;
    SDL_AudioDeviceID device = 0;
    SDL_AudioSpec format{};
    std::thread worker;
    std::atomic<bool> stopping{false}, paused{true}, failed{false};
    mutable std::mutex queueMutex, errorMutex;
    uint64_t submitted = 0;
    std::string failure;
    double leadIn = 2.0, totalDuration = 0;
    void pump();

  public:
    Audio();
    ~Audio();
    Audio(const Audio &) = delete;
    Audio &operator=(const Audio &) = delete;
    void load(const Song &song);
    void stop();
    void pause(bool value);
    double position() const;
    bool isPaused() const { return paused; }
    std::string error() const;
    double duration() const { return totalDuration; }
};
} // namespace fret
