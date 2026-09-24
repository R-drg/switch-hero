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
// The regular files directly inside a folder, from one directory listing.
// Listing a folder on the console's SD card costs milliseconds, and a song
// used to be listed over a hundred times while its stems were looked for, so
// a folder is listed once and every lookup after that is in memory.
struct FolderFiles {
    fs::path folder;
    std::vector<std::string> names, lowered;
    static FolderFiles list(const fs::path &folder); // empty if it cannot be read
    // Case-insensitive, as Clone Hero is. Empty when there is no such file.
    fs::path find(const std::string &name) const;
    void add(std::string name);
};
SongBrief peekSong(const FolderFiles &files);
SongBrief peekSong(const fs::path &folder);
// Every song folder under root, with its files, from a single walk.
std::vector<FolderFiles> findSongs(const fs::path &root);
std::vector<fs::path> scanSongs(const fs::path &root);
// Deletes one song folder and everything in it. Refuses anything that is not
// strictly inside `root`, the library itself included, so a bad path can never
// take the whole songs folder or anything outside it with it.
void deleteSong(const fs::path &root, const fs::path &folder);
std::string difficultyName(int difficulty);
// How a title or artist sorts: colour tags stripped, lower case, and a leading
// "the " dropped, so "The Band" files under B.
std::string sortKey(const std::string &text);
// The letter a sort key files under for jumping through the list: 'A' to 'Z',
// or '#' for anything starting with a digit or symbol.
char jumpLetter(const std::string &key);
} // namespace fret
