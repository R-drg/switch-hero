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
        // The head is scored by timing tier, so measure the sustain on its own.
        const double head = sustain.score;
        check(sustain.state[0].tier == 3, "dead-on press is a perfect hit");
        sustain.update(.5, 1, false);
        near(sustain.score - head, 25, "sustain scoring independent of frame count");
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
        Session openFret(a, open);
        openFret.update(0, 0, false, false);
        openFret.update(.01, 1, false, false);
        check(openFret.hits == 1, "any fret press hits an open note");
        Session strum(a, t);
        strum.gamepadMode = false;
        strum.update(0, 1, false);
        check(strum.hits == 0, "strum mode needs strum for normal note");
        strum.update(.01, 1, true);
        check(strum.hits == 1, "strum mode hits");
        Track run;
        for (int i = 0; i < 6; ++i) {
            Note n = ns[0];
            n.time = i * .06;
            n.mask = uint8_t(1 << (i % 2));
            n.kind = Kind::Strum;
            n.end = {};
            n.phrase = -1;
            run.notes.push_back(n);
        }
        Session roll(a, run);
        uint8_t last = 0;
        for (int i = 0; i < 6; ++i) {
            uint8_t fret = uint8_t(1 << (i % 2));
            roll.update(i * .06, last | fret, false); // new fret lands before the old one lifts
            roll.update(i * .06 + .02, fret, false);
            last = fret;
        }
        check(roll.hits == 6, "fast run hits while previous fret is still held");
        Session together(a, run);
        together.update(.02, 3, false);
        check(together.hits == 2, "two notes pressed in one frame both count");
        Session skip(a, run);
        skip.update(.065, 2, false);
        check(skip.hits == 1 && skip.misses == 1, "hitting a later note misses the due one");
        Session early(a, run);
        early.update(-.05, 2, false);
        check(early.hits == 0 && early.misses == 0, "press does not reach past an upcoming note");
        // A dropped note in a fast run on one fret must not put every later press a
        // note behind: the press belongs to the note nearest it, not the stale one
        // still sitting inside the window.
        Track same;
        for (int i = 0; i < 3; ++i) {
            Note n = ns[0];
            n.time = i * .06;
            n.mask = 1;
            n.kind = Kind::Strum;
            n.end = {};
            n.phrase = -1;
            same.notes.push_back(n);
        }
        Session desync(a, same);
        desync.update(0, 1, false);
        desync.update(.03, 0, false); // release, then skip the middle note entirely
        desync.update(.12, 1, false);
        check(desync.hits == 2, "a skipped note does not swallow the next press");
        check(desync.state[2].result == 1, "the press lands on the note it was aimed at");
        check(desync.state[1].result == -1, "the skipped note is the one that misses");
        Track chordRun;
        chordRun.notes = {run.notes[0], run.notes[2]};
        chordRun.notes[0].mask = 3;
        Session rolledChord(a, chordRun);
        rolledChord.update(0, 1, false);
        check(rolledChord.hits == 0, "partial chord does not hit a later single note");
        rolledChord.update(.016, 3, false);
        check(rolledChord.hits == 1 && rolledChord.misses == 0, "rolled chord completes");
        // Rock meter: hits nudge it up, mistakes cost three times as much.
        Track meterTrack;
        for (int i = 0; i < 12; ++i) {
            Note n = ns[0];
            n.time = i * .5;
            n.mask = 1;
            n.kind = Kind::Strum;
            n.end = {};
            n.phrase = -1;
            meterTrack.notes.push_back(n);
        }
        Session meter(a, meterTrack);
        near(meter.meter, .5, "rock meter starts centred");
        meter.update(0, 1, false);
        near(meter.meter, .5 + Session::meterStep, "a hit nudges the rock meter up");
        double afterHit = meter.meter;
        meter.update(.7, 0, false); // note 1 expires unplayed
        near(meter.meter, afterHit - Session::meterStep * 3, "a miss costs three steps");
        check(meter.combo == 0, "a miss breaks the streak");
        Session powered(a, meterTrack);
        powered.power = 1;
        powered.activate();
        powered.update(0, 1, false);
        near(powered.meter, .5 + Session::meterStep * 2, "star power doubles rock meter recovery");
        Session doomed(a, meterTrack);
        doomed.meter = Session::meterStep * 3;
        doomed.update(.7, 0, false);
        check(doomed.meter == 0 && doomed.failed, "draining the rock meter fails the song");
        Session safe(a, meterTrack);
        safe.noFail = true;
        safe.meter = Session::meterStep;
        safe.update(.7, 0, false);
        check(safe.meter == 0 && !safe.failed, "no-fail mode never fails");
        // Chords must be exact, but a repeated chord can be re-struck with one fret.
        Track chords;
        for (int i = 0; i < 3; ++i) {
            Note n = ns[0];
            n.time = i * .4;
            n.mask = 7; // green + red + yellow
            n.kind = Kind::Strum;
            n.end = {};
            n.phrase = -1;
            chords.notes.push_back(n);
        }
        Session wrong(a, chords);
        wrong.update(0, 15, false); // green red yellow blue
        check(wrong.hits == 0, "an extra fret does not play the chord");
        wrong.update(.02, 7, false);
        check(wrong.hits == 1, "the exact chord plays");
        Session repeated(a, chords);
        repeated.update(0, 7, false);
        repeated.update(.2, 3, false);  // yellow lifts, green and red stay down
        repeated.update(.4, 7, false);  // only yellow is pressed again
        check(repeated.hits == 2, "a repeated chord counts when one fret is re-pressed");
        Track mixed;
        mixed.notes = {ns[0], chords.notes[0]};
        mixed.notes[0].mask = 1; // a green sustain running under the chord
        mixed.notes[0].end = {};
        mixed.notes[0].end[0] = 2;
        mixed.notes[0].phrase = -1;
        mixed.notes[1].time = .5;
        mixed.notes[1].mask = 6; // red + yellow
        Session sustainChord(a, mixed);
        sustainChord.update(0, 1, false);
        sustainChord.update(.5, 7, false);
        check(sustainChord.hits == 2, "a chord plays over a sustain held on another fret");
        Track later = meterTrack;
        for (auto &n : later.notes)
            n.time += 5; // nothing is due yet, so the strum hits nothing
        Session overstrum(a, later);
        overstrum.gamepadMode = false;
        overstrum.update(.2, 0, true);
        near(overstrum.meter, .5, "a strum is given a moment to find its note");
        overstrum.update(.2 + Session::strumLeniency + .01, 0, false);
        near(overstrum.meter, .5 - Session::meterStep * 3, "overstrumming costs three steps");
        check(overstrum.misses == 0, "overstrumming is not counted as a missed note");
        // Strumming slightly early still plays the note when it arrives.
        Track soon = meterTrack;
        for (auto &n : soon.notes)
            n.time += 1;
        Session early2(a, soon);
        early2.gamepadMode = false;
        early2.update(1 - Session::strumLeniency / 2, 1, true);
        check(early2.hits == 1, "a strum just before the note still counts");
        near(early2.meter, .5 + Session::meterStep, "an early strum is not punished");
        // Strumming a hammer-on that fretting already played must not be punished.
        Track hopos;
        for (int i = 0; i < 2; ++i) {
            Note n = ns[0];
            n.time = i * .3;
            n.mask = uint8_t(1 << i);
            n.kind = i ? Kind::Hopo : Kind::Strum;
            n.end = {};
            n.phrase = -1;
            hopos.notes.push_back(n);
        }
        Session hammer(a, hopos);
        hammer.gamepadMode = false;
        hammer.update(0, 1, true);          // strum the first note
        hammer.update(.3, 2, false);        // hammer on to the second
        check(hammer.hits == 2, "a hammer-on plays without a strum");
        hammer.update(.32, 2, true);        // ... and the strum lands just after
        hammer.update(.5, 2, false);
        check(hammer.misses == 0, "strumming a hammer-on already played is not an error");
        near(hammer.meter, .5 + Session::meterStep * 2, "strumming a hammer-on costs nothing");
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
