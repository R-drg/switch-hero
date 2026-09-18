#pragma once
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// Chorus Encore (enchor.us), the community Clone Hero chart index. This part
// has no networking: it builds requests, reads responses and unpacks the .sng
// files charts are served as, so all of it can be tested offline.
namespace fret::enchor {
namespace fs = std::filesystem;

struct Chart {
    std::string name, artist, album, charter, md5;
    int guitarDifficulty = -1; // the chart's 0-6 intensity rating; -1 has no guitar part
    double seconds = 0;
    bool video = false;
};
struct Page {
    std::vector<Chart> charts;
    int found = 0;
    int page = 1;
};

const char *const searchUrl = "https://api.enchor.us/search";
std::string searchBody(const std::string &query, int page, int perPage);
// Throws on a response that is not a search result.
Page parseSearch(const std::string &json);
// Charts with a background video also come without it, which is all we play.
std::string downloadUrl(const Chart &chart);
// "Artist - Name (Charter)", made safe as a single FAT32 folder name.
std::string folderName(const Chart &chart);
// Chart text can carry Clone Hero rich-text tags like <color=#f00>; drop them.
std::string plainText(const std::string &s);

// Unpacks a .sng package into `folder`, writing its metadata as song.ini.
// Video is skipped. Unpacks into a sibling ".part" folder first, so a failed
// or interrupted unpack never leaves a half-song in the library. Throws on a
// malformed package.
void unpackSng(const fs::path &sng, const fs::path &folder);
} // namespace fret::enchor
