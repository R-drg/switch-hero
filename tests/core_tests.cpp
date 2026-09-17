#include "game.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace fret;
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
void near(double a, double b, const char *message) { check(std::abs(a - b) < 1e-6, message); }
template <class F> void rejects(F f, const char *message) {
    bool threw = false;
    try {
        f();
    } catch (const std::exception &) {
        threw = true;
    }
    check(threw, message);
}
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("fixture path required");
        fs::path root = argv[1];
        auto a = loadSong(root / "chart"), b = loadSong(root / "midi");
        check(a.name == "INI title", "INI metadata must override chart metadata");
        near(a.offset, .25, "INI delay overrides chart offset");
        near(a.seconds(192), .5, "120 BPM conversion");
        near(a.seconds(384), 1, "tempo change boundary");
        near(a.seconds(576), 2, "60 BPM after change");
        near(a.tickAt(2), 576, "inverse tempo mapping");
        auto &ns = a.tracks[0].notes;
        check(ns.size() == 6, "chords grouped into one hit");
        check(ns[1].kind == Kind::Hopo, "natural HOPO");
        check(ns[2].kind == Kind::Hopo, "forced same-fret HOPO");
        check(ns[3].mask == 6, "chord mask");
        near(ns[3].end[1], 1, "disjoint chord first end");
        near(ns[3].end[2], 2, "disjoint chord second end");
        check(ns[4].mask == 32 && ns[4].kind == Kind::Tap, "open tap note");
        check(ns[3].phrase == 0 && ns[4].phrase == -1, "star phrase end exclusive");
        near(ns[0].end[0], 2, "sustain crosses tempo change");
        check(b.tracks[0].notes.size() == ns.size(), "MIDI/chart note count equivalence");
        for (size_t i = 0; i < ns.size(); ++i) {
            auto &x = ns[i];
            auto &y = b.tracks[0].notes[i];
            check(x.mask == y.mask, "MIDI/chart lane equivalence");
            near(x.time, y.time, "MIDI/chart timing equivalence");
            check(x.kind == y.kind, "MIDI/chart modifier equivalence");
            for (int l = 0; l < 6; ++l)
                if (x.mask & (1 << l))
                    near(x.end[l], y.end[l], "MIDI/chart sustain equivalence");
        }
        check(loadSong(root / "utf16").tracks[0].notes.size() == 6, "UTF16 chart");
        check(loadSong(root / "running").tracks[0].notes.size() == 2, "running status across metadata");
        auto sx = loadSong(root / "sysex").tracks[0].notes;
        check(sx[0].mask == 32, "SysEx open conversion");
        check(sx[1].mask == 2, "open SysEx end exclusive");
        check(sx[1].kind == Kind::Tap, "tap SysEx end inclusive");
        rejects([&]() { loadSong(root / "bad"); }, "zero resolution rejected");
        rejects([&]() { loadSong(root / "truncated"); }, "truncated MIDI rejected");
        auto stems = loadSong(root / "stems");
        check(stems.audio.size() == 4, "numbered stems replace combined stems; preview excluded");
        check(scanSongs(root).size() == 8, "recursive song scan");
        Track t;
        t.instrument = "Guitar";
        t.notes = {ns[0]};
        t.notes[0].phrase = -1;
        Session sustain(a, t);
        sustain.update(0, 1, false);
        check(sustain.hits == 1, "press-to-hit accepts fret edge");
        sustain.update(.5, 1, false);
        near(sustain.score, 75, "sustain scoring independent of frame count");
        sustain.update(.6, 0, false);
        double released = sustain.score;
        sustain.update(.7, 1, false);
        near(sustain.score, released, "released sustain cannot be reacquired");
        Track extended;
        extended.notes = {ns[0], ns[1]};
        for (auto &n : extended.notes)
            n.phrase = -1;
        Session ext(a, extended);
        ext.update(0, 1, false);
        ext.update(.125, 3, false);
        check(ext.hits == 2, "new note can be hit while holding another fret's sustain");
        check(ext.state[0].held == 1, "extended sustain remains active");
        Note repeat = t.notes[0];
        repeat.time = .5;
        repeat.tick = 192;
        repeat.end = {};
        repeat.end[0] = .5;
        t.notes[0].end = {};
        t.notes.push_back(repeat);
        Session held(a, t);
        held.update(0, 1, false);
        held.update(.5, 1, false);
        check(held.hits == 1, "held fret must not auto-hit repeated note");
        held.update(.7, 1, false);
        check(held.misses == 1, "miss window expires");
        Track chord;
        chord.notes = {ns[3]};
        chord.notes[0].time = 0;
        chord.notes[0].phrase = -1;
        Session ch(a, chord);
        ch.update(0, 2, false);
        check(ch.hits == 0, "partial chord not hit");
        ch.update(.03, 6, false);
        check(ch.hits == 1, "chord accepted after all frets pressed");
        Track open;
        open.notes = {ns[4]};
        open.notes[0].time = 0;
        Session op(a, open);
        op.update(0, 0, false, false);
        check(op.hits == 0, "open needs explicit input");
        op.update(.01, 0, false, true);
        check(op.hits == 1, "open input hits");
        Session strum(a, t);
        strum.gamepadMode = false;
        strum.update(0, 1, false);
        check(strum.hits == 0, "strum mode needs strum for normal note");
        strum.update(.01, 1, true);
        check(strum.hits == 1, "strum mode hits");
        Session stars(a, a.tracks[0]);
        stars.update(0, 1, false);
        stars.update(.12, 0, false);
        stars.update(.125, 2, false);
        stars.update(.24, 0, false);
        stars.update(.25, 2, false);
        stars.update(.49, 0, false);
        stars.update(.5, 6, false);
        near(stars.power, .25, "completed star phrase awards power");
        std::cout << checks << " core checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
