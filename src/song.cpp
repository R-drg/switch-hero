#include "song.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace fret {
std::string lower(std::string s) {
    for (char &c : s)
        c = char(std::tolower((unsigned char)c));
    return s;
}
std::string trim(std::string s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
std::string prettySection(std::string raw) {
    raw = trim(raw);
    for (const char *prefix : {"section ", "section_", "prc_"})
        if (lower(raw).rfind(prefix, 0) == 0) {
            raw = raw.substr(std::string(prefix).size());
            break;
        }
    std::string out;
    bool start = true;
    for (char c : raw) {
        if (c == '_')
            c = ' ';
        if (c == ' ' && (out.empty() || out.back() == ' '))
            continue;
        out += start && c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c;
        start = c == ' ';
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out.empty() ? "Section" : out;
}
static std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}
static std::string readFile(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot open " + p.string());
    f.seekg(0, std::ios::end);
    auto n = f.tellg();
    if (n < 0 || n > 64 * 1024 * 1024)
        throw std::runtime_error("Chart exceeds 64 MiB limit");
    std::string s(size_t(n), '\0');
    f.seekg(0);
    if (!f.read(s.data(), n))
        throw std::runtime_error("Cannot read " + p.string());
    return s;
}
static void utf8(std::string &s, uint32_t c) {
    if (c < 128)
        s += char(c);
    else if (c < 2048) {
        s += char(0xc0 | (c >> 6));
        s += char(0x80 | (c & 63));
    } else if (c < 65536) {
        s += char(0xe0 | (c >> 12));
        s += char(0x80 | ((c >> 6) & 63));
        s += char(0x80 | (c & 63));
    } else {
        s += char(0xf0 | (c >> 18));
        s += char(0x80 | ((c >> 12) & 63));
        s += char(0x80 | ((c >> 6) & 63));
        s += char(0x80 | (c & 63));
    }
}
static std::string readText(const fs::path &p) {
    auto s = readFile(p);
    if (s.compare(0, 3, "\xef\xbb\xbf") == 0)
        return s.substr(3);
    if (s.size() > 1 &&
        ((uint8_t(s[0]) == 255 && uint8_t(s[1]) == 254) || (uint8_t(s[0]) == 254 && uint8_t(s[1]) == 255))) {
        bool le = uint8_t(s[0]) == 255;
        std::string out;
        auto unit = [&](size_t i) {
            return le ? (uint32_t(uint8_t(s[i])) | uint32_t(uint8_t(s[i + 1])) << 8)
                      : (uint32_t(uint8_t(s[i])) << 8 | uint8_t(s[i + 1]));
        };
        if (s.size() % 2)
            throw std::runtime_error("Truncated UTF-16 text");
        for (size_t i = 2; i + 1 < s.size(); i += 2) {
            uint32_t c = unit(i);
            if (c >= 0xd800 && c <= 0xdbff) {
                if (i + 3 >= s.size())
                    throw std::runtime_error("Truncated UTF-16 pair");
                uint32_t d = unit(i + 2);
                if (d < 0xdc00 || d > 0xdfff)
                    throw std::runtime_error("Invalid UTF-16 pair");
                c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00);
                i += 2;
            }
            utf8(out, c);
        }
        return out;
    }
    return s;
}
static Tick integer(const std::string &s) {
    size_t end;
    auto n = std::stoll(s, &end);
    if (end != s.size() || n < 0 || n > (Tick(1) << 40))
        throw std::runtime_error("Invalid chart integer: " + s);
    return n;
}
static double decimal(const std::string &s) {
    size_t end;
    double n = std::stod(s, &end);
    if (end != s.size() || !std::isfinite(n))
        throw std::runtime_error("Invalid number: " + s);
    return n;
}
FolderFiles FolderFiles::list(const fs::path &folder) {
    FolderFiles files;
    files.folder = folder;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) // the listing carries the type; no extra stat
            files.add(it->path().filename().string());
    return files;
}
void FolderFiles::add(std::string name) {
    lowered.push_back(lower(name));
    names.push_back(std::move(name));
}
fs::path FolderFiles::find(const std::string &name) const {
    const std::string want = lower(name);
    for (size_t i = 0; i < lowered.size(); ++i)
        if (lowered[i] == want)
            return folder / names[i];
    return {};
}
static void warn(Song &s, const std::string &w) {
    if (std::find(s.warnings.begin(), s.warnings.end(), w) == s.warnings.end())
        s.warnings.push_back(w);
}
struct Raw {
    Tick tick, end;
    int lane;
};
struct Marker {
    Tick start, end;
    int type;
    bool inclusive = false;
};
struct RawTrack {
    std::string instrument;
    int difficulty;
    std::vector<Raw> notes;
    std::vector<Marker> markers;
    std::vector<Phrase> phrases;
    std::vector<Phrase> solos;
    Tick soloOpen = -1; // a .chart "solo" event still waiting for its "soloend"
};
static std::string key(const std::string &i, int d) { return i + ":" + std::to_string(d); }
static RawTrack &rawTrack(std::map<std::string, RawTrack> &ts, const std::string &i, int d) {
    auto [it, inserted] = ts.try_emplace(key(i, d));
    if (inserted) {
        it->second.instrument = i;
        it->second.difficulty = d;
    }
    return it->second;
}
static void ini(Song &s, const FolderFiles &files) {
    auto p = files.find("song.ini");
    if (p.empty())
        return;
    std::istringstream f(readText(p));
    std::string l, section;
    while (std::getline(f, l)) {
        l = trim(l);
        if (l.empty() || l[0] == ';' || l[0] == '#')
            continue;
        if (l[0] == '[') {
            section = lower(l);
            continue;
        }
        if (section != "[song]")
            continue;
        auto eq = l.find('=');
        if (eq != std::string::npos)
            s.metadata[lower(trim(l.substr(0, eq)))] = unquote(l.substr(eq + 1));
    }
}
static bool chartTrack(const std::string &section, std::string &instrument, int &difficulty) {
    static const std::vector<std::string> ds = {"Easy", "Medium", "Hard", "Expert"};
    static const std::map<std::string, std::string> is = {
        {"Single", "Guitar"},       {"DoubleBass", "Bass"},    {"SingleBass", "Bass"},
        {"DoubleRhythm", "Rhythm"}, {"DoubleGuitar", "Co-op"}, {"Keyboard", "Keys"}};
    for (int d = 0; d < 4; ++d)
        if (section.rfind(ds[d], 0) == 0) {
            auto it = is.find(section.substr(ds[d].size()));
            if (it != is.end()) {
                instrument = it->second;
                difficulty = d;
                return true;
            }
        }
    return false;
}
static void parseChart(Song &s, std::map<std::string, RawTrack> &tracks) {
    std::istringstream f(readText(s.chart));
    std::string line, section;
    size_t lineNo = 0;
    bool resolution = false;
    while (std::getline(f, line)) {
        ++lineNo;
        line = trim(line);
        if (line.empty() || line == "{" || line == "}" || line.rfind("//", 0) == 0)
            continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("Malformed .chart line " + std::to_string(lineNo));
        auto k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (section == "Song") {
            s.metadata[lower(k)] = unquote(v);
            if (k == "Resolution") {
                auto res = integer(v);
                if (res == 0 || res > 1000000)
                    throw std::runtime_error("Invalid chart resolution");
                s.resolution = int(res);
                resolution = true;
            }
            continue;
        }
        if (section == "Events") {
            // tick = E "section Verse 1"
            std::istringstream event(v);
            std::string type;
            event >> type;
            std::string text;
            std::getline(event >> std::ws, text);
            text = unquote(text);
            if (type == "E" && (lower(text).rfind("section ", 0) == 0 || lower(text).rfind("prc_", 0) == 0))
                s.sections.push_back({integer(k), 0, prettySection(text)});
            continue;
        }
        std::string inst;
        int diff = 0;
        if (section != "SyncTrack" && !chartTrack(section, inst, diff))
            continue;
        Tick tick = integer(k);
        std::istringstream event(v);
        std::string type, a, b;
        event >> type >> a >> b;
        if (section == "SyncTrack") {
            if (type == "B")
                s.tempos.push_back({tick, decimal(a) / 1000});
            continue;
        }
        auto &tr = rawTrack(tracks, inst, diff);
        if (type == "N") {
            auto lane = integer(a);
            Tick len = integer(b);
            if (lane <= 4 || lane == 7)
                tr.notes.push_back({tick, tick + len, lane == 7 ? 5 : int(lane)});
            else if (lane == 5 || lane == 6)
                tr.markers.push_back({tick, tick, lane == 5 ? 1 : 3, true});
            else
                warn(s, "Unknown five-fret note markers were ignored");
        } else if (type == "S" && a == "2")
            tr.phrases.push_back({tick, tick + integer(b)});
        else if (type == "E" && a == "solo")
            tr.soloOpen = tick;
        else if (type == "E" && a == "soloend" && tr.soloOpen >= 0) {
            tr.solos.push_back({tr.soloOpen, tick});
            tr.soloOpen = -1;
        }
    }
    if (!resolution)
        throw std::runtime_error(".chart is missing Song/Resolution");
}
class MidiReader {
    const std::string &s;
    size_t pos_, end_;

  public:
    MidiReader(const std::string &data, size_t a, size_t b) : s(data), pos_(a), end_(b) {}
    size_t pos() const { return pos_; }
    bool done() const { return pos_ >= end_; }
    uint8_t byte() {
        if (pos_ >= end_)
            throw std::runtime_error("Truncated MIDI event");
        return uint8_t(s[pos_++]);
    }
    uint32_t be(int n) {
        uint32_t v = 0;
        while (n--)
            v = (v << 8) | byte();
        return v;
    }
    uint32_t vlq() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            auto b = byte();
            v = (v << 7) | (b & 127);
            if (!(b & 128))
                return v;
        }
        throw std::runtime_error("Invalid MIDI variable length integer");
    }
    std::string bytes(size_t n) {
        if (n > end_ - pos_)
            throw std::runtime_error("Truncated MIDI payload");
        auto out = s.substr(pos_, n);
        pos_ += n;
        return out;
    }
};
static void parseMidi(Song &s, std::map<std::string, RawTrack> &tracks) {
    auto data = readFile(s.chart);
    MidiReader r(data, 0, data.size());
    if (r.bytes(4) != "MThd")
        throw std::runtime_error("Missing MIDI header");
    auto length = r.be(4);
    if (length < 6)
        throw std::runtime_error("Invalid MIDI header");
    auto format = r.be(2), count = r.be(2), division = r.be(2);
    if (format > 1 || !division || (division & 0x8000))
        throw std::runtime_error("Only PPQN MIDI formats 0 and 1 supported");
    if (count > 1024)
        throw std::runtime_error("Too many MIDI tracks");
    s.resolution = int(division);
    r.bytes(length - 6);
    for (unsigned ti = 0; ti < count; ++ti) {
        if (r.bytes(4) != "MTrk")
            throw std::runtime_error("Missing MIDI track");
        auto n = r.be(4);
        auto payload = r.bytes(n);
        MidiReader tr(payload, 0, payload.size());
        struct MN {
            Tick start, end;
            int pitch;
        };
        struct SX {
            Tick tick;
            int diff, type, value;
        };
        std::vector<MN> notes;
        std::vector<SX> sx;
        std::vector<std::pair<Tick, std::string>> texts;
        std::map<int, Tick> active;
        std::string name;
        Tick tick = 0;
        uint8_t running = 0;
        bool opens = false;
        while (!tr.done()) {
            tick += tr.vlq();
            if (tick > (Tick(1) << 40))
                throw std::runtime_error("MIDI tick overflow");
            uint8_t b = tr.byte(), status = b;
            int first = -1;
            if (b < 128) {
                if (!running)
                    throw std::runtime_error("MIDI running status missing");
                status = running;
                first = b;
            }
            if (status == 0xff) {
                auto type = tr.byte();
                auto text = tr.bytes(tr.vlq());
                if (type == 3)
                    name = trim(text);
                else if (type >= 1 && type <= 15)
                    texts.push_back({tick, text});
                if (type >= 1 && type <= 15 && (text == "[ENHANCED_OPENS]" || text == "ENHANCED_OPENS"))
                    opens = true;
                if (type == 0x51) {
                    if (text.size() != 3)
                        throw std::runtime_error("Invalid MIDI tempo");
                    auto us = (uint32_t(uint8_t(text[0])) << 16) | (uint32_t(uint8_t(text[1])) << 8) |
                              uint8_t(text[2]);
                    if (!us)
                        throw std::runtime_error("Zero MIDI tempo");
                    s.tempos.push_back({tick, 60000000.0 / us});
                }
                if (type == 0x2f)
                    break;
                continue;
            }
            if (status == 0xf0 || status == 0xf7) {
                auto v = tr.bytes(tr.vlq());
                if (v.size() >= 7 && v[0] == 'P' && v[1] == 'S' && v[2] == 0 && v[3] == 0) {
                    int d = uint8_t(v[4]), type = uint8_t(v[5]), value = uint8_t(v[6]);
                    if ((d <= 3 || (d == 255 && type == 4)) && (type == 1 || type == 4) &&
                        (value == 0 || value == 1))
                        sx.push_back({tick, d, type, value});
                    else
                        warn(s, "Unsupported MIDI SysEx modifier ignored");
                }
                continue;
            }
            if (status >= 0xf0)
                throw std::runtime_error("Unsupported MIDI system event");
            running = status;
            int a = first >= 0 ? first : tr.byte();
            int kind = status >> 4;
            int v = (kind == 0xc || kind == 0xd) ? 0 : tr.byte();
            if (a > 127 || v > 127)
                throw std::runtime_error("Invalid MIDI data byte");
            int id = ((status & 15) << 8) | a;
            if (kind == 9 && v) {
                if (active.count(id))
                    throw std::runtime_error("Overlapping MIDI note-on for same channel/pitch");
                active[id] = tick;
            } else if (kind == 8 || (kind == 9 && !v)) {
                auto it = active.find(id);
                if (it != active.end()) {
                    notes.push_back({it->second, tick, a});
                    active.erase(it);
                }
            }
        }
        static const std::map<std::string, std::string> names = {
            {"PART GUITAR", "Guitar"}, {"T1 GEMS", "Guitar"},         {"PART BASS", "Bass"},
            {"PART RHYTHM", "Rhythm"}, {"PART GUITAR COOP", "Co-op"}, {"PART KEYS", "Keys"}};
        if (name == "EVENTS")
            for (auto &[at, text] : texts) {
                // "[section verse_1]", "section verse_1" or Rock Band's "[prc_verse_1]".
                std::string t = trim(text);
                if (t.size() > 1 && t.front() == '[' && t.back() == ']')
                    t = t.substr(1, t.size() - 2);
                const std::string l = lower(t);
                if (l.rfind("section ", 0) == 0 || l.rfind("section_", 0) == 0 || l.rfind("prc_", 0) == 0)
                    s.sections.push_back({at, 0, prettySection(t)});
            }
        auto ni = names.find(name);
        if (ni == names.end())
            continue;
        if (!active.empty())
            throw std::runtime_error("Unclosed MIDI notes in " + name);
        bool has116 = std::any_of(notes.begin(), notes.end(), [](const MN &x) { return x.pitch == 116; });
        int sp = has116 ? 116 : 103;
        if (s.metadata.count("star_power_note"))
            sp = int(integer(s.metadata.at("star_power_note")));
        if (s.metadata.count("multiplier_note"))
            sp = int(integer(s.metadata.at("multiplier_note")));
        for (int d = 0; d < 4; ++d) {
            auto &out = rawTrack(tracks, ni->second, d);
            int base = 60 + d * 12;
            for (const auto &x : notes) {
                int lane = x.pitch - base;
                if (lane >= 0 && lane <= 4)
                    out.notes.push_back({x.start, x.end, lane});
                else if (lane == -1 && opens)
                    out.notes.push_back({x.start, x.end, 5});
                else if (lane == 5 || lane == 6)
                    out.markers.push_back({x.start, x.end, lane == 5 ? 2 : 4, false});
                else if (x.pitch == 104)
                    out.markers.push_back({x.start, x.end, 3, false});
                if (x.pitch == sp)
                    out.phrases.push_back({x.start, x.end});
                // Where 116 carries star power (Rock Band and later), 103 marks solos;
                // older charts used 103 for star power itself.
                else if (x.pitch == 103)
                    out.solos.push_back({x.start, x.end});
                if (x.pitch >= 120)
                    warn(s, "Trill/tremolo/BRE special scoring is not implemented; notes play normally");
            }
            std::map<int, Tick> starts;
            for (auto x : sx)
                if (x.diff == d || x.diff == 255) {
                    if (x.value)
                        starts[x.type] = x.tick;
                    else {
                        auto it = starts.find(x.type);
                        if (it != starts.end()) {
                            out.markers.push_back({it->second, x.tick, x.type == 1 ? 5 : 3, x.type == 4});
                            starts.erase(it);
                        }
                    }
                }
            if (!starts.empty())
                throw std::runtime_error("Unclosed MIDI SysEx phrase");
        }
    }
}
double Song::seconds(Tick tick) const {
    auto it = std::upper_bound(tempos.begin(), tempos.end(), tick,
                               [](Tick t, const Tempo &v) { return t < v.tick; });
    if (it != tempos.begin())
        --it;
    return it->seconds + double(tick - it->tick) * 60.0 / (resolution * it->bpm);
}
double Song::tickAt(double time) const {
    auto it = std::upper_bound(tempos.begin(), tempos.end(), time,
                               [](double t, const Tempo &v) { return t < v.seconds; });
    if (it != tempos.begin())
        --it;
    return it->tick + (time - it->seconds) * resolution * it->bpm / 60.0;
}
static void finish(Song &s, std::map<std::string, RawTrack> &raw) {
    if (s.resolution <= 0 || s.resolution > 1000000)
        throw std::runtime_error("Unsupported resolution");
    std::stable_sort(s.tempos.begin(), s.tempos.end(), [](auto a, auto b) { return a.tick < b.tick; });
    std::vector<Tempo> tempos = {{0, 120, 0}};
    for (auto t : s.tempos) {
        if (t.bpm <= 0 || t.bpm > 1000000)
            throw std::runtime_error("Invalid tempo");
        if (t.tick == tempos.back().tick)
            tempos.back() = t;
        else
            tempos.push_back(t);
    }
    s.tempos = tempos;
    s.tempos[0].seconds = 0;
    for (size_t i = 1; i < s.tempos.size(); ++i) {
        auto &prev = s.tempos[i - 1];
        auto &t = s.tempos[i];
        t.seconds = prev.seconds + double(t.tick - prev.tick) * 60 / (s.resolution * prev.bpm);
    }
    for (auto &section : s.sections)
        section.time = s.seconds(section.tick);
    std::stable_sort(s.sections.begin(), s.sections.end(), [](const Section &a, const Section &b) { return a.tick < b.tick; });
    s.offset = s.metadata.count("delay")    ? decimal(s.metadata.at("delay")) / 1000
               : s.metadata.count("offset") ? decimal(s.metadata.at("offset"))
                                            : 0;
    if (std::abs(s.offset) > 3600)
        throw std::runtime_error("Song offset exceeds one hour");
    s.name = s.metadata.count("name") ? s.metadata.at("name") : s.folder.filename().string();
    s.artist = s.metadata.count("artist") ? s.metadata.at("artist") : "Unknown artist";
    Tick threshold = s.midi ? s.resolution / 3 + 1 : Tick(std::floor(s.resolution * 65.0 / 192.0));
    if (s.metadata.count("eighthnote_hopo") &&
        (lower(s.metadata.at("eighthnote_hopo")) == "true" || s.metadata.at("eighthnote_hopo") == "1"))
        threshold = s.resolution / 2;
    if (s.metadata.count("hopo_frequency"))
        threshold = integer(s.metadata.at("hopo_frequency"));
    Tick cutoff = s.midi ? s.resolution / 3 : 0;
    if (s.midi && s.metadata.count("sustain_cutoff_threshold"))
        cutoff = integer(s.metadata.at("sustain_cutoff_threshold"));
    for (auto &[_, rt] : raw) {
        if (rt.notes.empty())
            continue;
        Track tr;
        tr.instrument = rt.instrument;
        tr.difficulty = rt.difficulty;
        tr.phrases = rt.phrases;
        std::sort(tr.phrases.begin(), tr.phrases.end(), [](auto a, auto b) { return a.start < b.start; });
        tr.solos = rt.solos;
        std::sort(tr.solos.begin(), tr.solos.end(), [](auto a, auto b) { return a.start < b.start; });
        std::map<Tick, Note> groups;
        if (rt.notes.size() > 1000000)
            throw std::runtime_error("Track exceeds one million notes");
        for (auto x : rt.notes) {
            auto &n = groups[x.tick];
            n.tick = x.tick;
            n.mask |= uint8_t(1 << x.lane);
            n.endTick[x.lane] = std::max(n.endTick[x.lane], x.end - x.tick < cutoff ? x.tick : x.end);
        }
        for (auto &[_, n] : groups) {
            bool hopo = !tr.notes.empty() && n.tick - tr.notes.back().tick <= threshold &&
                        (n.mask & (n.mask - 1)) == 0 && n.mask != tr.notes.back().mask;
            bool flip = false, forceH = false, forceS = false, tap = false, open = false;
            for (auto m : rt.markers)
                if (n.tick >= m.start &&
                    (n.tick < m.end || ((m.inclusive || m.start == m.end) && n.tick == m.end))) {
                    if (m.type == 1)
                        flip = true;
                    if (m.type == 2)
                        forceH = true;
                    if (m.type == 3)
                        tap = true;
                    if (m.type == 4)
                        forceS = true;
                    if (m.type == 5)
                        open = true;
                }
            if (open) {
                Tick end = n.tick;
                for (auto t : n.endTick)
                    end = std::max(end, t);
                n.mask = 32;
                n.endTick = {};
                n.endTick[5] = end;
            }
            if ((n.mask & 32) && (n.mask & 31))
                throw std::runtime_error("Open note combined with fretted chord");
            if (flip)
                hopo = !hopo;
            if (forceH)
                hopo = true;
            if (forceS)
                hopo = false;
            n.kind = tap ? Kind::Tap : hopo ? Kind::Hopo : Kind::Strum;
            n.time = s.seconds(n.tick);
            for (int l = 0; l < 6; ++l)
                if (n.mask & (1 << l)) {
                    n.end[l] = s.seconds(n.endTick[l]);
                    s.duration = std::max(s.duration, n.end[l]);
                }
            for (size_t p = 0; p < tr.phrases.size(); ++p)
                if (n.tick >= tr.phrases[p].start &&
                    (n.tick < tr.phrases[p].end || n.tick == tr.phrases[p].start)) {
                    n.phrase = int(p);
                    break;
                }
            // Solo ends are inclusive: a .chart soloend sits on the last note.
            for (size_t p = 0; p < tr.solos.size(); ++p)
                if (n.tick >= tr.solos[p].start && n.tick <= tr.solos[p].end) {
                    n.solo = int(p);
                    break;
                }
            tr.notes.push_back(n);
        }
        s.tracks.push_back(std::move(tr));
    }
    if (s.tracks.empty())
        throw std::runtime_error("No supported five-fret tracks");
    std::sort(s.tracks.begin(), s.tracks.end(), [](const Track &a, const Track &b) {
        auto rank = [](const std::string &i) {
            return i == "Guitar" ? 0 : i == "Bass" ? 1 : i == "Rhythm" ? 2 : i == "Co-op" ? 3 : 4;
        };
        return std::make_pair(rank(a.instrument), -a.difficulty) <
               std::make_pair(rank(b.instrument), -b.difficulty);
    });
    if (s.metadata.count("modchart") &&
        (s.metadata.at("modchart") == "1" || lower(s.metadata.at("modchart")) == "true"))
        warn(s, "Modchart scripts are not executed");
}
static void audioFiles(Song &s, const FolderFiles &files) {
    const std::vector<std::string> exts = {".opus", ".ogg", ".mp3", ".flac", ".wav"};
    const std::vector<std::pair<std::string, std::string>> stems = {
        {"song", "musicstream"},    {"guitar", "guitarstream"}, {"rhythm", "rhythmstream"},
        {"bass", "bassstream"},     {"keys", "keysstream"},     {"drums", "drumstream"},
        {"drums_1", "drumstream"},  {"drums_2", "drum2stream"}, {"drums_3", "drum3stream"},
        {"drums_4", "drum4stream"}, {"vocals", "vocalstream"},  {"vocals_1", ""},
        {"vocals_2", ""},           {"vocals_explicit", ""},    {"vocals_explicit_1", ""},
        {"vocals_explicit_2", ""},  {"crowd", "crowdstream"}};
    auto locate = [&](const std::string &name) {
        for (const auto &ext : exts) {
            auto p = files.find(name + ext);
            if (!p.empty())
                return p;
        }
        return fs::path{};
    };
    std::set<fs::path> seen;
    bool numberedDrums = false, numberedVocals = false;
    for (int i = 1; i <= 4; ++i)
        numberedDrums |= !locate("drums_" + std::to_string(i)).empty();
    for (int i = 1; i <= 2; ++i)
        numberedVocals |= !locate("vocals_" + std::to_string(i)).empty();
    for (const auto &[stem, tag] : stems) {
        if ((stem == "drums" && numberedDrums) || (stem == "vocals" && numberedVocals) ||
            (stem == "drums_1" && !numberedDrums))
            continue;
        auto p = locate(stem);
        const bool listed = !p.empty();
        if (p.empty() && !tag.empty() && s.metadata.count(tag)) {
            auto name = s.metadata.at(tag);
            std::replace(name.begin(), name.end(), '\\', '/');
            fs::path rel(name);
            if (name != "None" && !name.empty()) {
                if (rel.is_absolute() || std::find(rel.begin(), rel.end(), fs::path("..")) != rel.end())
                    throw std::runtime_error("Audio reference must stay inside song folder");
                p = s.folder / rel;
                if (!fs::is_regular_file(p))
                    p = files.find(rel.filename().string());
            }
        }
        // libstdc++'s canonicalization rejects libnx device paths such as
        // "sdmc:/switch/...". All candidates are already constrained to the
        // song folder above, so lexical normalization is sufficient for stem
        // deduplication and does not require a host filesystem interpretation.
        // Found in the listing means it is a file already; only a path taken
        // from song.ini still needs checking.
        if (!p.empty() && (listed || fs::is_regular_file(p)) && seen.insert(p.lexically_normal()).second)
            s.audio.push_back(p);
    }
    if (s.audio.empty())
        throw std::runtime_error("No song audio found (OGG/Opus/MP3/FLAC/WAV)");
}
Song loadSong(const fs::path &folder) {
    if (!fs::is_directory(folder))
        throw std::runtime_error("Song folder does not exist: " + folder.string());
    Song s;
    s.folder = folder;
    const auto files = FolderFiles::list(folder);
    std::map<std::string, RawTrack> raw;
    s.chart = files.find("notes.mid");
    if (!s.chart.empty()) {
        s.midi = true;
        ini(s, files);
        parseMidi(s, raw);
    } else {
        s.chart = files.find("notes.chart");
        if (s.chart.empty())
            throw std::runtime_error("Expected notes.chart or notes.mid");
        parseChart(s, raw);
        ini(s, files);
    }
    finish(s, raw);
    audioFiles(s, files);
    return s;
}
SongBrief peekSong(const fs::path &folder) { return peekSong(FolderFiles::list(folder)); }
SongBrief peekSong(const FolderFiles &files) {
    const fs::path &folder = files.folder;
    SongBrief brief;
    brief.name = folder.filename().string();
    Song probe;
    probe.folder = folder;
    const bool midi = !files.find("notes.mid").empty();
    const fs::path chart = midi ? fs::path() : files.find("notes.chart");
    if (!midi && chart.empty()) {
        brief.error = "Expected notes.chart or notes.mid";
        return brief;
    }
    ini(probe, files); // song.ini carries the display metadata for nearly every song
    auto meta = [&](const char *k) {
        auto it = probe.metadata.find(k);
        return it == probe.metadata.end() ? std::string() : trim(it->second);
    };
    brief.name = meta("name").empty() ? brief.name : meta("name");
    brief.artist = meta("artist");
    if (!midi && (meta("name").empty() || brief.artist.empty())) {
        // No usable ini: read just the chart's [Song] block, not its notes.
        std::ifstream f(chart);
        std::string line, section;
        while (std::getline(f, line)) {
            line = trim(line);
            if (!line.empty() && line[0] == '[') {
                if (!section.empty())
                    break; // past the header block
                section = lower(line);
                continue;
            }
            auto eq = line.find('=');
            if (section != "[song]" || eq == std::string::npos)
                continue;
            const std::string key = lower(trim(line.substr(0, eq))), value = unquote(line.substr(eq + 1));
            if (key == "name" && meta("name").empty() && !value.empty())
                brief.name = value;
            if (key == "artist" && brief.artist.empty())
                brief.artist = value;
        }
    }
    return brief;
}
std::vector<FolderFiles> findSongs(const fs::path &root) {
    std::error_code ec;
    if (!fs::is_directory(root, ec))
        return {};
    // One walk lists every folder once, and each folder's files are kept, so
    // reading a song's title afterwards needs no second listing.
    std::map<fs::path, FolderFiles> folders;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    while (it != end) {
        if (it.depth() > 16)
            it.disable_recursion_pending();
        if (it->is_regular_file(ec)) {
            auto &f = folders[it->path().parent_path()];
            f.folder = it->path().parent_path();
            f.add(it->path().filename().string());
        }
        it.increment(ec);
        if (ec)
            ec.clear();
    }
    std::vector<FolderFiles> songs;
    for (auto &[path, files] : folders)
        if (!files.find("notes.chart").empty() || !files.find("notes.mid").empty())
            songs.push_back(std::move(files));
    return songs;
}
std::vector<fs::path> scanSongs(const fs::path &root) {
    std::vector<fs::path> out;
    for (auto &f : findSongs(root))
        out.push_back(std::move(f.folder));
    return out;
}
void deleteSong(const fs::path &root, const fs::path &folder) {
    // Compared component by component on normalised paths. No canonical():
    // libstdc++ on the Switch rejects sdmc:/ paths there.
    const auto base = root.lexically_normal(), target = folder.lexically_normal();
    auto b = base.begin(), t = target.begin();
    for (; b != base.end() && !b->empty(); ++b, ++t)
        if (t == target.end() || *t != *b)
            throw std::runtime_error("Refusing to delete outside the songs folder");
    bool deeper = false;
    for (; t != target.end(); ++t)
        if (!t->empty()) {
            if (*t == "..")
                throw std::runtime_error("Refusing to delete outside the songs folder");
            deeper = true;
        }
    if (!deeper)
        throw std::runtime_error("Refusing to delete the songs folder itself");
    std::error_code ec;
    fs::remove_all(target, ec);
    if (ec)
        throw std::runtime_error("Could not delete " + target.filename().string() + ": " + ec.message());
}
std::string sortKey(const std::string &text) {
    std::string out;
    bool tag = false;
    for (char c : text) {
        if (c == '<')
            tag = true;
        else if (c == '>')
            tag = false;
        else if (!tag)
            out += c;
    }
    out = lower(trim(out));
    if (out.rfind("the ", 0) == 0)
        out = trim(out.substr(4));
    return out;
}
char jumpLetter(const std::string &key) {
    const char c = key.empty() ? '#' : key[0];
    return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : '#';
}
std::vector<Section> practiceSections(const Song &song) {
    // Sections that start after the last note are empty, and any before the
    // first note would only loop silence.
    double first = 1e300, last = 0;
    for (const auto &t : song.tracks)
        if (!t.notes.empty())
            first = std::min(first, t.notes.front().time), last = std::max(last, t.notes.back().time);
    std::vector<Section> out;
    for (const auto &s : song.sections)
        if (s.time <= last)
            out.push_back(s);
    if (!out.empty())
        return out;
    // No named sections: eight measures of four beats at a time, from the start.
    const Tick chunk = Tick(song.resolution) * 4 * 8;
    const Tick end = Tick(song.tickAt(std::max(last, 0.0))) + 1;
    int part = 1;
    for (Tick t = 0; t < end; t += chunk)
        out.push_back({t, song.seconds(t), "Part " + std::to_string(part++)});
    (void)first;
    return out;
}
std::string difficultyName(int d) {
    static const char *names[] = {"Easy", "Medium", "Hard", "Expert"};
    return names[std::clamp(d, 0, 3)];
}
} // namespace fret
