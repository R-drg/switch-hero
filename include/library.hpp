#pragma once
// The song list, and a cache of it on the SD card. Walking a big library on
// the console takes seconds (every folder listing and song.ini read is a
// round trip to the SD card), so the game starts from the cached list and
// checks the card again in the background.
#include "song.hpp"
#include <atomic>
#include <cstdint>
#include <functional>

namespace fret {
struct LibraryEntry {
    fs::path folder;
    std::string name, artist, error;
    // The folder's file names when it was read. A song whose files are
    // unchanged reuses its title and artist rather than opening song.ini again.
    uint64_t signature = 0;
};
bool operator==(const LibraryEntry &a, const LibraryEntry &b);

// A fingerprint of a folder's file names, in any order and any case.
uint64_t folderSignature(const FolderFiles &files);

// Every song under root. Folders that match an entry of `known` by path and
// signature are taken from it; the rest are peeked. `progress`, if set, is
// called after each song with (done, total, title). Setting `cancel` stops
// the walk early; the partial list it returns must be thrown away.
using ScanProgress = std::function<void(size_t done, size_t total, const std::string &title)>;
std::vector<LibraryEntry> scanLibrary(const fs::path &root, const std::vector<LibraryEntry> &known,
                                      const ScanProgress &progress = {},
                                      const std::atomic<bool> *cancel = nullptr);

// Empty when the file is missing, from another version, or damaged.
std::vector<LibraryEntry> loadLibraryCache(const fs::path &file);
// Throws when the file cannot be written.
void saveLibraryCache(const fs::path &file, const std::vector<LibraryEntry> &entries);
} // namespace fret
