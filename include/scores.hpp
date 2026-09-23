#pragma once
#include "song.hpp"
#include <map>
#include <string>

namespace fret {
// Stars and letter rank for a finished run, from the share of notes hit. The
// results screen and the saved records use the same thresholds.
int starsFor(double accuracy, bool failed);
char rankFor(double accuracy, bool failed);

// Best runs, one per song folder, part and difficulty. Kept in a small text
// file next to the settings; folder names hold spaces, so fields are tab
// separated.
struct Record {
    int score = 0, stars = 0, accuracy = 0; // accuracy in percent
    bool fullCombo = false;
};
class Scores {
    std::map<std::string, Record> records;
    static std::string key(const std::string &folder, const std::string &part, int difficulty);

  public:
    void load(const fs::path &path);
    void save(const fs::path &path) const;
    // The record for one chart, or null when it has never been completed.
    const Record *find(const std::string &folder, const std::string &part, int difficulty) const;
    // The most stars earned on any chart of the song, or -1 when none.
    int bestStars(const std::string &folder) const;
    // Keeps `run` if it beats the stored score; true when it does.
    bool submit(const std::string &folder, const std::string &part, int difficulty, const Record &run);
    // Drops every record of a song, for when it is deleted.
    void forget(const std::string &folder);
};
} // namespace fret
