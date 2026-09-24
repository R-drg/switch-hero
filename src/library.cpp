#include "library.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace fret {
bool operator==(const LibraryEntry &a, const LibraryEntry &b) {
    return a.folder == b.folder && a.name == b.name && a.artist == b.artist && a.error == b.error &&
           a.signature == b.signature;
}

uint64_t folderSignature(const FolderFiles &files) {
    auto names = files.lowered;
    std::sort(names.begin(), names.end());
    uint64_t h = 1469598103934665603ull; // FNV-1a
    for (const auto &n : names) {
        for (unsigned char c : n)
            h = (h ^ c) * 1099511628211ull;
        h = (h ^ '\n') * 1099511628211ull;
    }
    return h;
}

std::vector<LibraryEntry> scanLibrary(const fs::path &root, const std::vector<LibraryEntry> &known,
                                      const ScanProgress &progress, const std::atomic<bool> *cancel) {
    std::unordered_map<std::string, const LibraryEntry *> byFolder;
    for (const auto &e : known)
        byFolder[e.folder.string()] = &e;
    auto folders = findSongs(root);
    std::vector<LibraryEntry> out;
    out.reserve(folders.size());
    for (const auto &files : folders) {
        if (cancel && *cancel)
            break;
        LibraryEntry e;
        e.folder = files.folder;
        e.signature = folderSignature(files);
        const auto hit = byFolder.find(e.folder.string());
        if (hit != byFolder.end() && hit->second->signature == e.signature)
            e = *hit->second;
        else {
            auto brief = peekSong(files);
            e.name = std::move(brief.name), e.artist = std::move(brief.artist), e.error = std::move(brief.error);
        }
        out.push_back(std::move(e));
        if (progress)
            progress(out.size(), folders.size(), out.back().name);
    }
    return out;
}

namespace {
constexpr const char *magic = "switch-hero library 1";
// One entry per line, fields split by tabs: tabs and line breaks inside a
// field would break that, so they become spaces (titles never need them).
std::string field(std::string s) {
    std::replace_if(s.begin(), s.end(), [](char c) { return c == '\t' || c == '\n' || c == '\r'; }, ' ');
    return s;
}
} // namespace

std::vector<LibraryEntry> loadLibraryCache(const fs::path &file) {
    std::ifstream in(file, std::ios::binary);
    std::string line;
    if (!std::getline(in, line) || line != magic)
        return {};
    std::vector<LibraryEntry> out;
    while (std::getline(in, line)) {
        std::vector<std::string> parts;
        std::istringstream fields(line);
        for (std::string p; std::getline(fields, p, '\t');)
            parts.push_back(p);
        if (line.size() && line.back() == '\t')
            parts.push_back({}); // an empty last field
        if (parts.size() != 5 || parts[1].empty())
            return {}; // damaged: rescan rather than trust any of it
        LibraryEntry e;
        try {
            e.signature = std::stoull(parts[0], nullptr, 16);
        } catch (const std::exception &) {
            return {};
        }
        e.folder = parts[1], e.name = parts[2], e.artist = parts[3], e.error = parts[4];
        out.push_back(std::move(e));
    }
    return out;
}

void saveLibraryCache(const fs::path &file, const std::vector<LibraryEntry> &entries) {
    // Written beside the old one and swapped in, so a crash or a pulled SD
    // card mid-write leaves the previous cache rather than half of one.
    fs::path temp = file;
    temp += ".part";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("Cannot save the song list: " + file.string());
        out << magic << '\n';
        for (const auto &e : entries) {
            std::ostringstream sig;
            sig << std::hex << e.signature;
            out << sig.str() << '\t' << field(e.folder.string()) << '\t' << field(e.name) << '\t' << field(e.artist)
                << '\t' << field(e.error) << '\n';
        }
        if (!out)
            throw std::runtime_error("Cannot save the song list: " + file.string());
    }
    std::error_code ec;
    fs::remove(file, ec); // rename cannot replace a file on every platform
    fs::rename(temp, file);
}
} // namespace fret
