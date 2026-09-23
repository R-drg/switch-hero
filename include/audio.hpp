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
    // Jumps to `seconds` from the start. Formats without a native seek decode
    // and discard up to it.
    virtual void seek(double seconds);
};
std::unique_ptr<Decoder> openDecoder(const fs::path &path);

// Interface sounds. The game ships no audio files, so every one of these is
// synthesised into a buffer when the mixer starts.
enum class Sfx {
    Move,      // cursor moving down the song list
    Select,    // a chord stab for confirming
    Back,      // muted thunk
    Toggle,    // switch flipped
    Count,     // drumstick click counting the song in
    Miss,      // dead-string screech when a note is dropped
    StarPower, // riser into the chord
    Streak,    // shimmer when a streak milestone lands
    Fail,      // the set falls apart
    Win,       // final chord
    Count_
};

// One mixer runs for the whole session: the song plays on its own pausable
// channel, and interface sounds play on a second channel that is always live,
// so menus and the pause screen still make noise.
class Audio {
    struct Stream;
    struct Engine;
    std::unique_ptr<Engine> engine;
    std::vector<std::unique_ptr<Stream>> streams;
    mutable std::mutex streamMutex, errorMutex;
    std::atomic<bool> paused{true}, failed{false}, duckGuitar{false}, silent{false};
    // songGain fades the song channel (previews); the volumes are the player's.
    std::atomic<float> songGain{1}, musicVolume{1}, sfxVolume{1};
    bool guitarStem = false;
    std::string failure;
    double leadIn = 2.0, totalDuration = 0;
    uint64_t generated = 0;
    void fail(const std::string &message);
    // `prime` waits for the mixer to buffer the start, which song timing needs
    // and a preview does not.
    void loadStreams(const Song &song, double leadIn, double startSeconds, bool prime);
    friend struct Engine;

  public:
    Audio();
    ~Audio();
    Audio(const Audio &) = delete;
    Audio &operator=(const Audio &) = delete;
    // Opens the output and starts mixing. Safe to call more than once; sound
    // effects work from here on, with or without a song loaded.
    void start();
    void load(const Song &song);
    // Plays the song from `startSeconds` with no lead-in, for the song list's
    // preview. Nothing is judged against it, so it has no count-in.
    void preview(const Song &song, double startSeconds);
    // The same, split so the slow part can leave the main thread: prepare()
    // opens and seeks the stems and touches no mixer state, so it runs on any
    // thread; playPrepared() swaps them in.
    struct Prepared;
    static std::shared_ptr<Prepared> prepare(std::vector<fs::path> stems, double songDuration, double startSeconds);
    void playPrepared(std::shared_ptr<Prepared> prepared);
    void setSongGain(float gain) { songGain = gain; }
    // Both run 0 to 1.
    void setVolumes(float music, float sfx) { musicVolume = music, sfxVolume = sfx; }
    // Loads a click track on the song channel instead of a song, for offset
    // calibration: the clicks then carry exactly the latency the music does.
    // Beat k lands at position() == k * 60 / bpm.
    void loadClicks(double bpm);
    // Mutes the song channel without stopping its clock.
    void silence(bool value) { silent = value; }
    void stop();
    void pause(bool value);
    void playSfx(Sfx sound, float gain = 1);
    // Silences the guitar stem while the rock meter is in the red, as the
    // original games do. No effect unless the song ships a separate guitar stem.
    void muteGuitar(bool value) { duckGuitar = value && guitarStem; }
    bool hasGuitarStem() const { return guitarStem; }
    double position() const;
    bool isPaused() const { return paused; }
    std::string error() const;
    double duration() const { return totalDuration; }
    // Mixes the next block of song audio; used by the mixer thread.
    bool renderSong(float *out, size_t frames);
};
} // namespace fret
