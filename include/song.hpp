#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fret {
using Tick = int64_t;
namespace fs = std::filesystem;
enum class Kind { Strum, Hopo, Tap };
struct Tempo {
    Tick tick;
    double bpm;
    double seconds = 0;
};
struct Note {
    Tick tick = 0;
    uint8_t mask = 0; // bits 0..4: frets; bit 5: open
    std::array<Tick, 6> endTick{};
    double time = 0;
    std::array<double, 6> end{};
    Kind kind = Kind::Strum;
    int phrase = -1;
};
struct Phrase {
    Tick start, end;
};
struct Track {
    std::string instrument;
    int difficulty = 3;
    std::vector<Note> notes;
    std::vector<Phrase> phrases;
};
struct Song {
    fs::path folder, chart;
    std::string name, artist;
    int resolution = 192;
    bool midi = false;
    double offset = 0; // Positive: notes later than audio.
    double duration = 0;
    std::map<std::string, std::string> metadata;
    std::vector<Tempo> tempos;
    std::vector<Track> tracks;
    std::vector<fs::path> audio;
    std::vector<std::string> warnings;
    double seconds(Tick tick) const;
    double tickAt(double seconds) const;
};
std::string lower(std::string s);
std::string trim(std::string s);
Song loadSong(const fs::path &folder);
// Title and artist only, without parsing the chart: song libraries can hold
// hundreds of songs and the list needs nothing else to draw.
struct SongBrief {
    std::string name, artist, error;
};
SongBrief peekSong(const fs::path &folder);
std::vector<fs::path> scanSongs(const fs::path &root);
std::string difficultyName(int difficulty);
} // namespace fret
