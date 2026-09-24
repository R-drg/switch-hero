#include "enchor.hpp"
#include "json.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace fret::enchor {
using nlohmann::json;

std::string searchBody(const std::string &query, int page, int perPage) {
    // The same shape the official client sends; "guitar" limits results to
    // charts that have a lead guitar part.
    return json{{"search", query.empty() ? "*" : query},
                {"per_page", perPage},
                {"page", page},
                {"instrument", "guitar"},
                {"difficulty", nullptr},
                {"drumType", nullptr},
                {"drumsReviewed", false},
                {"sort", nullptr},
                // The API accepts website, bridge or api; we are a third-party client.
                {"source", "api"}}
        .dump();
}

namespace {
std::string text(const json &o, const char *key) {
    auto it = o.find(key);
    return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
}
double number(const json &o, const char *key, double fallback) {
    auto it = o.find(key);
    return it != o.end() && it->is_number() ? it->get<double>() : fallback;
}
bool flag(const json &o, const char *key) {
    auto it = o.find(key);
    return it != o.end() && it->is_boolean() && it->get<bool>();
}
bool isMd5(const std::string &s) {
    return s.size() == 32 && std::all_of(s.begin(), s.end(), [](char ch) { return std::isxdigit((unsigned char)ch); });
}
int difficultyIndex(const std::string &d) {
    return d == "easy" ? 0 : d == "medium" ? 1 : d == "hard" ? 2 : d == "expert" ? 3 : -1;
}
// The index's five-fret instruments, their rating keys and the game's names
// for them, in the game's part order.
struct Instrument {
    const char *api, *rating, *name;
};
const Instrument fiveFret[] = {{"guitar", "diff_guitar", "Guitar"},
                               {"bass", "diff_bass", "Bass"},
                               {"rhythm", "diff_rhythm", "Rhythm"},
                               {"guitarcoop", "diff_guitar_coop", "Co-op"},
                               {"keys", "diff_keys", "Keys"}};
// Fills the parts and feature flags from "notesData".
void readNotes(const json &c, Chart &chart) {
    auto it = c.find("notesData");
    const json empty = json::object();
    const json &notes = it != c.end() && it->is_object() ? *it : empty;
    auto list = [&](const char *key) -> const json & {
        static const json none = json::array();
        auto l = notes.find(key);
        return l != notes.end() && l->is_array() ? *l : none;
    };
    for (const auto &inst : fiveFret) {
        Part part;
        part.instrument = inst.name;
        part.intensity = int(number(c, inst.rating, -1));
        for (const auto &n : list("noteCounts"))
            if (n.is_object() && text(n, "instrument") == inst.api)
                if (const int d = difficultyIndex(text(n, "difficulty")); d >= 0)
                    part.notes[size_t(d)] = std::max(0, int(number(n, "count", 0)));
        for (const auto &n : list("maxNps"))
            if (n.is_object() && text(n, "instrument") == inst.api)
                if (const int d = difficultyIndex(text(n, "difficulty")); d >= 0)
                    part.peakNps[size_t(d)] = float(std::max(0.0, number(n, "nps", 0)));
        const bool anyNotes = std::any_of(part.notes.begin(), part.notes.end(), [](int n) { return n > 0; });
        // The note counts decide which parts exist; some charts rate parts
        // they never chart. Only entries without counts fall back to ratings.
        if (anyNotes || (list("noteCounts").empty() && part.intensity >= 0))
            chart.parts.push_back(std::move(part));
    }
    bool drums = number(c, "diff_drums", -1) >= 0;
    for (const auto &i : list("instruments"))
        drums = drums || (i.is_string() && i.get<std::string>() == "drums");
    if (drums)
        chart.otherParts.push_back("Drums");
    if (flag(notes, "hasVocals") || number(c, "diff_vocals", -1) >= 0)
        chart.otherParts.push_back("Vocals");
    chart.solos = flag(notes, "hasSoloSections");
    chart.openNotes = flag(notes, "hasOpenNotes");
    chart.tapNotes = flag(notes, "hasTapNotes");
}
} // namespace

Page parseSearch(const std::string &body) {
    const json doc = json::parse(body, nullptr, false);
    if (doc.is_discarded() || !doc.is_object())
        throw std::runtime_error("Unexpected response from Chorus Encore");
    if (!doc.contains("data") || !doc["data"].is_array()) {
        const auto error = text(doc, "error");
        throw std::runtime_error("Chorus Encore: " + (error.empty() ? std::string("unexpected response") : error));
    }
    Page page;
    page.found = int(number(doc, "found", 0));
    page.page = int(number(doc, "page", 1));
    for (const auto &c : doc["data"]) {
        if (!c.is_object())
            continue;
        Chart chart;
        chart.md5 = text(c, "md5");
        // An md5 is 32 hex digits; anything else would be a bad file URL.
        if (!isMd5(chart.md5))
            continue;
        chart.name = plainText(text(c, "name"));
        chart.artist = plainText(text(c, "artist"));
        chart.album = plainText(text(c, "album"));
        chart.charter = plainText(text(c, "charter"));
        chart.genre = plainText(text(c, "genre"));
        chart.year = plainText(text(c, "year"));
        if (const auto art = text(c, "albumArtMd5"); isMd5(art))
            chart.albumArtMd5 = art;
        chart.guitarDifficulty = int(number(c, "diff_guitar", -1));
        chart.seconds = number(c, "song_length", 0) / 1000;
        auto video = c.find("hasVideoBackground");
        chart.video = video != c.end() && video->is_boolean() && video->get<bool>();
        readNotes(c, chart);
        page.charts.push_back(std::move(chart));
    }
    return page;
}

std::string downloadUrl(const Chart &chart) {
    return "https://files.enchor.us/" + chart.md5 + (chart.video ? "_novideo" : "") + ".sng";
}

std::string albumArtUrl(const Chart &chart) {
    return chart.albumArtMd5.empty() ? std::string() : "https://files.enchor.us/" + chart.albumArtMd5 + ".jpg";
}

std::string plainText(const std::string &s) {
    std::string out;
    bool tag = false;
    for (char c : s) {
        if (c == '<')
            tag = true;
        else if (c == '>')
            tag = false;
        else if (!tag) {
            // Collapse the runs of spaces that tags and credits leave behind.
            const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r';
            if (space && (out.empty() || out.back() == ' '))
                continue;
            out += space ? ' ' : c;
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

std::string folderName(const Chart &chart) {
    std::string raw = (chart.artist.empty() ? "Unknown Artist" : chart.artist) + " - " +
                      (chart.name.empty() ? "Unknown Song" : chart.name);
    if (!chart.charter.empty())
        raw += " (" + chart.charter + ")";
    std::string out;
    for (unsigned char c : raw) {
        if (c < 32 || std::string_view("/\\:*?\"<>|").find(char(c)) != std::string_view::npos)
            out += ' ';
        else
            out += char(c);
    }
    // FAT32 allows 255 characters; stay well short of it, and never cut a
    // UTF-8 character in half.
    if (out.size() > 96) {
        size_t cut = 96;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
            --cut;
        out.resize(cut);
    }
    // Windows and FAT both dislike names ending in a dot or space.
    while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    while (!out.empty() && out.front() == ' ')
        out.erase(out.begin());
    return out.empty() ? "Download" : out;
}

namespace {
struct Reader {
    std::ifstream in;
    uint64_t size = 0;
    void bytes(void *out, size_t n) {
        if (!in.read(static_cast<char *>(out), std::streamsize(n)))
            throw std::runtime_error("Chart package is truncated");
    }
    template <class T> T number() {
        std::array<unsigned char, sizeof(T)> b{};
        bytes(b.data(), b.size());
        uint64_t v = 0;
        for (size_t i = 0; i < b.size(); ++i)
            v |= uint64_t(b[i]) << (8 * i); // little-endian
        return T(v);
    }
    std::string string(uint64_t n) {
        if (n > (1u << 20))
            throw std::runtime_error("Chart package has an oversized field");
        std::string s(size_t(n), '\0');
        bytes(s.data(), s.size());
        return s;
    }
};
// Only plain file names: an archive must not be able to write outside its folder.
bool safeName(const std::string &n) {
    return !n.empty() && n != "." && n != ".." && n.find('/') == std::string::npos &&
           n.find('\\') == std::string::npos && n.find(':') == std::string::npos && n.find('\0') == std::string::npos;
}
std::string lowered(std::string s) {
    for (auto &c : s)
        c = char(std::tolower((unsigned char)c));
    return s;
}
} // namespace

void unpackSng(const fs::path &sng, const fs::path &folder) {
    Reader r;
    r.in.open(sng, std::ios::binary);
    if (!r.in)
        throw std::runtime_error("Cannot open the downloaded chart");
    r.size = fs::file_size(sng);
    char magic[6];
    r.bytes(magic, 6);
    if (std::string(magic, 6) != "SNGPKG")
        throw std::runtime_error("Not a chart package");
    r.number<uint32_t>(); // version
    std::array<unsigned char, 16> mask{};
    r.bytes(mask.data(), mask.size());

    r.number<uint64_t>(); // metadata section length
    const auto metaCount = r.number<uint64_t>();
    if (metaCount > 4096)
        throw std::runtime_error("Chart package has too much metadata");
    std::vector<std::pair<std::string, std::string>> metadata;
    for (uint64_t i = 0; i < metaCount; ++i) {
        auto key = r.string(r.number<uint32_t>());
        auto value = r.string(r.number<uint32_t>());
        metadata.emplace_back(std::move(key), std::move(value));
    }

    r.number<uint64_t>(); // file index length
    const auto fileCount = r.number<uint64_t>();
    if (fileCount > 256)
        throw std::runtime_error("Chart package has too many files");
    struct Entry {
        std::string name;
        uint64_t length, offset;
    };
    std::vector<Entry> files;
    for (uint64_t i = 0; i < fileCount; ++i) {
        Entry e;
        e.name = r.string(r.number<uint8_t>());
        e.length = r.number<uint64_t>();
        e.offset = r.number<uint64_t>();
        if (!safeName(e.name))
            throw std::runtime_error("Chart package has an unsafe file name");
        if (e.offset > r.size || e.length > r.size - e.offset)
            throw std::runtime_error("Chart package is truncated");
        files.push_back(std::move(e));
    }

    fs::path part = folder;
    part += ".part";
    std::error_code ec;
    fs::remove_all(part, ec);
    fs::create_directories(part);
    try {
        bool hasIni = false;
        std::vector<char> buffer(1 << 16);
        for (const auto &f : files) {
            const auto name = lowered(f.name);
            if (name.rfind("video.", 0) == 0)
                continue; // background video: large, and never played here
            hasIni = hasIni || name == "song.ini";
            std::ofstream out(part / f.name, std::ios::binary);
            r.in.clear();
            r.in.seekg(std::streamoff(f.offset));
            // Unmask in chunks; the key depends on the position within the file.
            for (uint64_t done = 0; done < f.length;) {
                const size_t n = size_t(std::min<uint64_t>(buffer.size(), f.length - done));
                r.bytes(buffer.data(), n);
                for (size_t i = 0; i < n; ++i) {
                    const uint64_t at = done + i;
                    buffer[i] = char(buffer[i] ^ mask[at % 16] ^ (at & 0xFF));
                }
                out.write(buffer.data(), std::streamsize(n));
                done += n;
            }
            if (!out)
                throw std::runtime_error("Could not write the chart to the SD card");
        }
        if (!hasIni) {
            std::ofstream ini(part / "song.ini", std::ios::binary);
            ini << "[song]\n";
            for (auto [key, value] : metadata) {
                std::replace(value.begin(), value.end(), '\n', ' ');
                std::replace(value.begin(), value.end(), '\r', ' ');
                if (safeName(key) && key.find('=') == std::string::npos && key.find('\n') == std::string::npos)
                    ini << key << " = " << value << '\n';
            }
            if (!ini)
                throw std::runtime_error("Could not write the chart to the SD card");
        }
        fs::remove_all(folder, ec);
        fs::rename(part, folder);
    } catch (...) {
        fs::remove_all(part, ec);
        throw;
    }
}
} // namespace fret::enchor
