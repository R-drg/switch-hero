#include "enchor.hpp"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace fret::enchor;
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *message) {
    bool threw = false;
    try {
        f();
    } catch (const std::exception &) {
        threw = true;
    }
    check(threw, message);
}
std::string slurp(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// Builds a .sng package the way the format spec describes it.
struct Package {
    std::vector<std::pair<std::string, std::string>> metadata, files;
    std::string bytes() const {
        std::string out = "SNGPKG";
        auto put = [&](uint64_t v, int n) {
            for (int i = 0; i < n; ++i)
                out += char((v >> (8 * i)) & 0xFF);
        };
        put(1, 4);
        const unsigned char mask[16] = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9, 3};
        out.append(reinterpret_cast<const char *>(mask), 16);
        std::string meta;
        for (auto &[k, v] : metadata) {
            for (int i = 0; i < 4; ++i) meta += char((k.size() >> (8 * i)) & 0xFF);
            meta += k;
            for (int i = 0; i < 4; ++i) meta += char((v.size() >> (8 * i)) & 0xFF);
            meta += v;
        }
        put(meta.size() + 8, 8);
        put(metadata.size(), 8);
        out += meta;
        size_t indexSize = 0;
        for (auto &f : files)
            indexSize += 1 + f.first.size() + 16;
        put(indexSize + 8, 8);
        put(files.size(), 8);
        uint64_t offset = out.size() + indexSize + 8; // data starts after the index and its length
        for (auto &[name, data] : files) {
            out += char(name.size());
            out += name;
            put(data.size(), 8);
            put(offset, 8);
            offset += data.size();
        }
        size_t total = 0;
        for (auto &f : files)
            total += f.second.size();
        put(total, 8);
        for (auto &[name, data] : files)
            for (size_t i = 0; i < data.size(); ++i)
                out += char(data[i] ^ mask[i % 16] ^ (i & 0xFF));
        return out;
    }
    void write(const fs::path &p) const {
        std::ofstream f(p, std::ios::binary);
        f << bytes();
    }
};

int main() {
    try {
        check(searchBody("smoke", 2, 25).find("\"page\":2") != std::string::npos, "request carries the page");
        check(searchBody("", 1, 25).find("\"search\":\"*\"") != std::string::npos, "empty search lists newest");
        check(searchBody("a\"b", 1, 25).find("a\\\"b") != std::string::npos, "queries are escaped");

        const std::string response = R"({"found":40,"page":1,"data":[
            {"name":"<b>Song</b>","artist":"Band","charter":"<color=#f00>Someone</color>",
             "md5":"0123456789abcdef0123456789abcdef","diff_guitar":4,"song_length":61500,"hasVideoBackground":true},
            {"name":"Bad","md5":"../../etc"},
            {"name":"No guitar rating","md5":"fedcba9876543210fedcba9876543210","diff_guitar":null}]})";
        auto page = parseSearch(response);
        check(page.found == 40 && page.charts.size() == 2, "charts with a bad md5 are dropped");
        check(page.charts[0].name == "Song" && page.charts[0].charter == "Someone", "rich-text tags are stripped");
        check(plainText("  Beenox,  <b>Neversoft</b> ") == "Beenox, Neversoft", "space runs collapse");
        check(page.charts[0].guitarDifficulty == 4 && page.charts[0].seconds == 61.5, "difficulty and length");
        check(page.charts[1].guitarDifficulty == -1, "missing rating reads as unknown");
        check(downloadUrl(page.charts[0]) == "https://files.enchor.us/0123456789abcdef0123456789abcdef_novideo.sng",
              "video charts download without the video");
        {
            // Parts, difficulties and features, as the index's notesData lists them.
            const auto detail = parseSearch(R"({"found":1,"data":[
                {"name":"Full","md5":"00112233445566778899aabbccddeeff","albumArtMd5":"ffeeddccbbaa99887766554433221100",
                 "year":"2010","genre":"Rock","diff_guitar":5,"diff_bass":2,"diff_drums":3,"diff_keys":-1,
                 "diff_guitar_coop":-1,"diff_rhythm":4,
                 "notesData":{"instruments":["guitar","bass","drums","rhythm"],"hasSoloSections":true,
                   "hasOpenNotes":true,"hasTapNotes":false,"hasVocals":true,
                   "noteCounts":[{"instrument":"guitar","difficulty":"expert","count":900},
                                 {"instrument":"guitar","difficulty":"easy","count":200},
                                 {"instrument":"bass","difficulty":"hard","count":300},
                                 {"instrument":"drums","difficulty":"expert","count":1000},
                                 {"instrument":"guitar","difficulty":"bogus","count":5}],
                   "maxNps":[{"instrument":"guitar","difficulty":"expert","nps":14.5}]}},
                {"name":"Bare","md5":"fedcba9876543210fedcba9876543210","albumArtMd5":"nope"},
                {"name":"Old","md5":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","diff_guitar":3,"diff_bass":-1}]})");
            const auto &full = detail.charts[0];
            check(full.parts.size() == 2 && full.parts[0].instrument == "Guitar" && full.parts[1].instrument == "Bass",
                  "parts come from the note counts, in song setup order");
            check(full.parts[0].intensity == 5 && full.parts[0].notes[3] == 900 && full.parts[0].notes[0] == 200 &&
                      !full.parts[0].charted(1) && full.parts[0].peakNps[3] == 14.5f,
                  "per-difficulty note counts and peaks");
            check(full.otherParts == std::vector<std::string>{"Drums", "Vocals"}, "drums and vocals are noted");
            check(full.solos && full.openNotes && !full.tapNotes, "feature flags");
            check(full.year == "2010" && full.genre == "Rock" &&
                      albumArtUrl(full) == "https://files.enchor.us/ffeeddccbbaa99887766554433221100.jpg",
                  "year, genre and cover art");
            const auto &bare = detail.charts[1];
            check(bare.parts.empty() && bare.otherParts.empty() && albumArtUrl(bare).empty(),
                  "missing notesData and a bad art hash are harmless");
            const auto &old = detail.charts[2];
            check(old.parts.size() == 1 && old.parts[0].intensity == 3 && !old.parts[0].charted(3),
                  "entries without note counts fall back to ratings");
        }
        rejects([] { parseSearch("<html>"); }, "non-JSON is rejected");
        try {
            parseSearch(R"({"error":"Bad Request","statusCode":400})");
            check(false, "error responses throw");
        } catch (const std::runtime_error &e) {
            check(std::string(e.what()).find("Bad Request") != std::string::npos, "server error is reported");
        }

        Chart c;
        c.artist = "AC/DC", c.name = "What?: Live.", c.charter = "x";
        check(folderName(c) == "AC DC - What   Live. (x)", "folder names lose path and reserved characters");
        c.charter.clear();
        check(folderName(c) == "AC DC - What   Live", "no trailing dots");
        c.name = std::string(200, 'a');
        check(folderName(c).size() <= 96, "folder names stay short");
        // "Unknown Artist - " is 17 bytes, so the cut at 96 lands inside the first "é".
        c.name = std::string(78, 'a') + "\xC3\xA9\xC3\xA9";
        c.artist = "";
        {
            const auto n = folderName(c);
            check(n.size() == 95 && n.back() == 'a', "never cuts a UTF-8 character in half");
        }

        const fs::path root = fs::temp_directory_path() / "switch-hero-enchor-tests";
        fs::remove_all(root);
        fs::create_directories(root);
        Package p;
        p.metadata = {{"name", "Song"}, {"artist", "Band\nInjected = 1"}, {"diff_guitar", "4"}};
        std::string notes;
        for (int i = 0; i < 300; ++i)
            notes += char(i); // long enough to cross the 16-byte mask and the 256-byte position key
        p.files = {{"notes.chart", notes}, {"guitar.opus", "OggS audio"}, {"video.mp4", "big"}};
        p.write(root / "a.sng");
        unpackSng(root / "a.sng", root / "Song");
        check(slurp(root / "Song" / "notes.chart") == notes, "files unmask byte for byte");
        check(slurp(root / "Song" / "guitar.opus") == "OggS audio", "every file is unpacked");
        check(!fs::exists(root / "Song" / "video.mp4"), "video is skipped");
        check(!fs::exists(root / "Song.part"), "the staging folder is gone");
        const auto ini = slurp(root / "Song" / "song.ini");
        check(ini.rfind("[song]\n", 0) == 0 && ini.find("name = Song\n") != std::string::npos,
              "metadata becomes song.ini");
        check(ini.find("\nInjected") == std::string::npos, "metadata cannot add song.ini lines");

        Package evil;
        evil.files = {{"../escape.txt", "x"}};
        evil.write(root / "evil.sng");
        rejects([&] { unpackSng(root / "evil.sng", root / "Evil"); }, "path traversal is refused");
        check(!fs::exists(root / "escape.txt") && !fs::exists(root / "Evil"), "nothing is written for a bad package");

        auto cut = p.bytes();
        cut.resize(cut.size() - 100);
        std::ofstream(root / "cut.sng", std::ios::binary) << cut;
        rejects([&] { unpackSng(root / "cut.sng", root / "Cut"); }, "truncated packages are refused");
        check(!fs::exists(root / "Cut") && !fs::exists(root / "Cut.part"), "a failed unpack leaves nothing behind");
        std::ofstream(root / "junk.sng", std::ios::binary) << "not a package at all";
        rejects([&] { unpackSng(root / "junk.sng", root / "Junk"); }, "non-packages are refused");
        fs::remove_all(root);

        std::cout << checks << " chart download checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
