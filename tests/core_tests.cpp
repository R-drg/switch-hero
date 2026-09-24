#include "game.hpp"
#include "lang.hpp"
#include "library.hpp"
#include "scores.hpp"
#include <fstream>
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
        check(sortKey("The Band") == "band" && sortKey("  <color=#f00>Zebra</color> ") == "zebra" &&
                  sortKey("Theory") == "theory",
              "sort keys drop tags, case and a leading the");
        check(jumpLetter(sortKey("the Offspring")) == 'O' && jumpLetter("3 doors") == '#' && jumpLetter("") == '#',
              "jump letters");
        {
            // Interface text: English passes through, Portuguese is looked up by
            // the English text, placeholders fill in order, unknown text is kept.
            using lang::tr;
            lang::set(lang::Language::English);
            check(std::string(tr("QUIT")) == "QUIT" && tr("{} notes", 12) == "12 notes", "english text");
            lang::set(lang::Language::Portuguese);
            check(std::string(tr("QUIT")) == "SAIR" && tr("{} notes", 12) == "12 notas", "portuguese text");
            check(tr("not played on {} {}", tr(std::string("Bass")), tr("Easy")) == "ainda não jogada: Baixo Fácil",
                  "portuguese placeholders");
            check(std::string(tr("Through the Fire and Flames")) == "Through the Fire and Flames" &&
                      lang::fill("{} / {}", {"1"}) == "1 / {}",
                  "untranslated text and missing arguments pass through");
            lang::set(lang::Language::English);
        }
        {
            // High scores: stars and rank thresholds, best-only keeping, and a
            // round trip through the file with a folder name full of spaces.
            check(starsFor(.97, false) == 5 && starsFor(.9, false) == 4 && starsFor(.5, false) == 1,
                  "star thresholds");
            check(starsFor(1, true) == 0 && rankFor(1, true) == 'F' && rankFor(.95, false) == 'A', "rank thresholds");
            Scores scores;
            const std::string folder = "Some Band - A Song (Charter)";
            check(scores.find(folder, "Guitar", 3) == nullptr && scores.bestStars(folder) == -1, "no record yet");
            check(scores.submit(folder, "Guitar", 3, {1000, 3, 82, false}), "first clear is a best");
            check(!scores.submit(folder, "Guitar", 3, {900, 2, 70, true}), "a lower score is not a best");
            check(scores.find(folder, "Guitar", 3)->score == 1000 && scores.find(folder, "Guitar", 3)->fullCombo,
                  "a lower full combo still earns the badge");
            check(scores.submit(folder, "Guitar", 3, {1500, 4, 91, false}), "a higher score replaces");
            check(scores.find(folder, "Guitar", 3)->fullCombo, "the full combo badge is kept");
            scores.submit(folder, "Bass", 1, {200, 5, 99, true});
            check(scores.bestStars(folder) == 5, "best stars across charts");
            const fs::path file = root / "scores-test.cfg";
            scores.save(file);
            {
                std::ofstream junk(file, std::ios::app);
                junk << "broken line\n" << folder << "\tGuitar\tnine\t1\t1\t1\t0\n";
            }
            Scores loaded;
            loaded.load(file);
            const auto *r = loaded.find(folder, "Guitar", 3);
            check(r && r->score == 1500 && r->stars == 4 && r->accuracy == 91 && r->fullCombo, "scores round trip");
            check(loaded.find(folder, "Bass", 1) != nullptr && loaded.bestStars("Some Band") == -1,
                  "records keyed by the whole folder name");
            loaded.submit("Other", "Guitar", 3, {10, 1, 50, false});
            loaded.forget(folder);
            check(loaded.bestStars(folder) == -1 && loaded.find("Other", "Guitar", 3), "forget drops only that song");
        }
        {
            // Deleting a song removes exactly its folder and never escapes the library.
            const fs::path lib = root / "delete-test";
            fs::remove_all(lib);
            fs::create_directories(lib / "Band - Song" / "sub");
            fs::create_directories(lib / "Keep");
            std::ofstream(lib / "Band - Song" / "notes.chart") << "x";
            std::ofstream(lib / "Band - Song" / "sub" / "a.ogg") << "x";
            rejects([&] { deleteSong(lib, lib); }, "never deletes the library itself");
            rejects([&] { deleteSong(lib, lib / "."); }, "never deletes the library via dot");
            rejects([&] { deleteSong(lib, lib / "Band - Song" / ".." / ".."); }, "never escapes the library");
            rejects([&] { deleteSong(lib / "Keep", lib / "Band - Song"); }, "never deletes a sibling");
            check(fs::exists(lib / "Band - Song" / "notes.chart"), "rejected deletes leave files alone");
            deleteSong(lib, lib / "Band - Song");
            check(!fs::exists(lib / "Band - Song") && fs::exists(lib / "Keep"), "deletes only the song folder");
            fs::remove_all(lib);
        }
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
        {
            // Sections and solos, from both chart formats.
            for (const Song *song : {&a, &b}) {
                check(song->sections.size() == 2 && song->sections[0].name == "Intro" &&
                          song->sections[1].name == "Verse 1a",
                      "sections parsed with readable names");
                near(song->sections[1].time, 1, "section time follows the tempo map");
                const auto &notes = song->tracks[0].notes;
                check(song->tracks[0].solos.size() == 1 && notes[2].solo == -1 && notes[3].solo == 0 &&
                          notes[4].solo == 0 && notes[5].solo == -1,
                      "solo range covers its notes, end inclusive");
            }
            check(b.tracks[0].phrases.size() == 1, "note 103 is a solo, not star power, when 116 is present");
            check(prettySection("section verse_2b") == "Verse 2b" && prettySection("prc_gtr_solo") == "Gtr Solo" &&
                      prettySection("  ") == "Section",
                  "section names");
            Song bare = a;
            bare.sections.clear();
            const auto chunks = practiceSections(bare);
            check(!chunks.empty() && chunks[0].name == "Part 1" && chunks[0].tick == 0,
                  "songs without sections still offer practice chunks");
            check(practiceSections(a).size() == 2, "named sections are used for practice");
        }
        {
            // A solo pays a flat bonus per note hit when its last note is judged.
            Session solo(a, a.tracks[0]);
            solo.gamepadMode = true;
            for (size_t i = 0; i < ns.size(); ++i) {
                const bool play = i != 4; // miss one solo note
                const uint8_t frets = ns[i].mask == 32 ? 0 : ns[i].mask;
                solo.update(ns[i].time, play ? frets : 0, false, play && ns[i].mask == 32);
                solo.update(ns[i].time + .01, 0, false);
            }
            solo.update(3, 0, false);
            check(solo.lastSolo.index == 0 && solo.lastSolo.total == 2 && solo.lastSolo.hit == 1 &&
                      std::abs(solo.soloBonus - 100) < 1e-9,
                  "solo result and bonus");
            // The section breakdown splits the run at the second section.
            const auto stats = sectionStats(a, solo);
            check(stats.size() == 2 && stats[0].name == "Intro" && stats[0].total == 4 && stats[1].total == 2 &&
                      stats[1].hit == 1,
                  "section breakdown");
        }
        {
            // Practice starts part-way in: earlier notes are set aside, not missed.
            Session practice(a, a.tracks[0]);
            practice.noFail = false;
            practice.startAt(.9);
            practice.update(.95, 0, false); // before note 4 (at 1 s) is due
            check(practice.misses == 0 && practice.combo == 0 && !practice.failed && practice.state[0].skipped &&
                      practice.state[3].skipped && !practice.state[4].skipped,
                  "practice skips earlier notes without misses");
            check(practice.phraseFailed[0], "a star phrase cut short cannot pay out");
            check(practice.soloTotal[0] == 1, "a solo cut short counts what is left");
            check(sectionStats(a, practice).empty(), "skipped notes are left out of the breakdown");
        }
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
        {
            // The song list cache: unchanged folders reuse what was read before,
            // changed ones are read again, and the file round-trips.
            FolderFiles x, y;
            x.add("Song.ini"), x.add("notes.chart"), y.add("NOTES.CHART"), y.add("song.ini");
            check(folderSignature(x) == folderSignature(y), "signatures ignore order and case");
            y.add("guitar.ogg");
            check(folderSignature(x) != folderSignature(y), "a new file changes the signature");

            auto first = fret::scanLibrary(root, {});
            check(first.size() == 8 && first[0].signature != 0, "library scan finds every song");
            auto known = first;
            known[0].name = "Cached title"; // same files: the cached title must be reused as is
            known[1].name = "Stale title", known[1].signature ^= 1; // files changed: read again
            size_t calls = 0;
            auto second = fret::scanLibrary(root, known, [&](size_t done, size_t total, const std::string &) {
                calls += done <= total;
            });
            check(second[0].name == "Cached title" && second[1].name == first[1].name && calls == 8,
                  "unchanged folders reuse the cache, changed ones are peeked");
            std::atomic<bool> stop{true};
            check(fret::scanLibrary(root, {}, {}, &stop).empty(), "a cancelled scan stops");

            const fs::path cache = root / "library-test.cache";
            auto odd = first;
            odd[0].name = "Tab\there", odd[0].artist = "", odd[0].error = "";
            odd[1].error = "Broken\nchart";
            saveLibraryCache(cache, odd);
            auto back = loadLibraryCache(cache);
            check(back.size() == odd.size() && back[0].name == "Tab here" && back[0].artist.empty() &&
                      back[1].error == "Broken chart" && back[2] == odd[2] && back[0].signature == odd[0].signature,
                  "the library cache round-trips");
            {
                std::ofstream damaged(cache, std::ios::app);
                damaged << "not a valid line\n";
            }
            check(loadLibraryCache(cache).empty() && loadLibraryCache(root / "missing.cache").empty(),
                  "a damaged or missing cache is ignored");
            fs::remove(cache);
        }
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
        {
            // A star phrase pays out only if every note in it is hit; one miss
            // breaks it for good, and the rest of it draws as ordinary notes.
            Track phrase;
            phrase.phrases = {{0, 1000}};
            for (int i = 0; i < 3; ++i) {
                Note n;
                n.time = 1 + i, n.mask = 1, n.phrase = 0;
                n.end.fill(n.time);
                phrase.notes.push_back(n);
            }
            Session clean(a, phrase);
            for (int i = 0; i < 3; ++i)
                clean.update(1 + i, 1, false), clean.update(1.2 + i, 0, false);
            check(!clean.phraseFailed[0] && std::abs(clean.power - .25) < 1e-9, "a clean phrase earns star power");
            Session broken(a, phrase);
            broken.update(1.2, 0, false); // the first note goes by unplayed
            check(broken.phraseFailed[0], "a missed note breaks its phrase");
            for (int i = 1; i < 3; ++i)
                broken.update(1 + i, 1, false), broken.update(1.2 + i, 0, false);
            check(broken.hits == 2 && broken.power == 0, "a broken phrase earns nothing");
        }
        {
            // Whammy: bending a held star-phrase sustain fills star power; a
            // still bar, an ordinary sustain or a broken phrase earns nothing.
            Song steady; // 120 BPM throughout, so four beats last two seconds
            steady.tempos = {{0, 120, 0}};
            Track bend;
            bend.phrases = {{0, 1000}};
            Note n;
            n.time = 1, n.mask = 1, n.phrase = 0;
            n.end.fill(n.time);
            n.end[0] = 3; // four beats at 120 BPM
            bend.notes = {n};
            auto play = [&](const Track &track, bool wiggle, bool miss) {
                Session s(steady, track);
                if (!miss)
                    s.update(1, 1, false);
                for (int f = 1; f <= 120; ++f) {
                    const double t = 1 + f / 60.0;
                    s.whammy(wiggle ? (f / 6) % 2 : 0, t);
                    s.update(t, miss ? 0 : 1, false);
                }
                return s;
            };
            const auto still = play(bend, false, false), bent = play(bend, true, false);
            check(std::abs(still.power - .25) < 1e-9, "a still bar adds nothing beyond the phrase");
            const double gained = bent.power - still.power;
            check(gained > 3.5 / 30 && gained < 4.0 / 30 + 1e-9, "whammy fills star power per beat bent");
            Track plain = bend;
            plain.phrases.clear();
            plain.notes[0].phrase = -1;
            check(play(plain, true, false).power == 0, "whammy on an ordinary sustain earns nothing");
            check(play(bend, true, true).power == 0, "whammy on a missed note earns nothing");
            Session drift(steady, bend);
            drift.update(1, 1, false);
            for (int f = 1; f <= 60; ++f)
                drift.whammy(.5 + .01 * (f % 2), 1 + f / 60.0), drift.update(1 + f / 60.0, 1, false);
            check(std::abs(drift.power - .25) < 1e-9, "stick jitter below the step is not whammying");
        }
        {
            // Hit windows widen on easier difficulties and with the lenient
            // setting; strict expert keeps roughly the old 70 ms.
            check(Session::windowFor(0, 1) > Session::windowFor(3, 1) &&
                      Session::windowFor(3, 2) > Session::windowFor(3, 1) &&
                      std::abs(Session::windowFor(3, 0) - .07) < .002,
                  "window scales by difficulty and leniency");
            Track one;
            Note n;
            n.time = 1, n.mask = 1;
            n.end.fill(n.time);
            one.notes = {n};
            Session easy(a, one);
            easy.window = Session::windowFor(0, 1);
            easy.update(1.095, 1, false);
            check(easy.hits == 1 && easy.state[0].tier == 1, "a 95 ms late press still counts on easy");
            Session strict(a, one);
            strict.window = Session::windowFor(3, 0);
            strict.update(1.095, 1, false);
            check(strict.hits == 0 && strict.misses == 1, "the same press misses on strict expert");
            Session tiers(a, one);
            tiers.window = .1;
            tiers.update(1.03, 1, false);
            check(tiers.state[0].tier == 3, "tiers scale with the window (30 ms is perfect in a 100 ms window)");
        }
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
        // A fast strummed run: every strum lands inside the hammer-on grace of the
        // note before it, and every one of them must still count.
        Track strumRun;
        for (int i = 0; i < 8; ++i) {
            Note n = ns[0];
            n.time = i * .1;
            n.mask = uint8_t(1 << (i % 3));
            n.kind = Kind::Strum;
            n.end = {};
            n.phrase = -1;
            strumRun.notes.push_back(n);
        }
        Session strummed(a, strumRun);
        strummed.gamepadMode = false;
        for (int i = 0; i < 8; ++i) {
            strummed.update(i * .1 - .02, strumRun.notes[size_t(i)].mask, false);
            strummed.update(i * .1, strumRun.notes[size_t(i)].mask, true);
        }
        strummed.update(1, 0, false);
        check(strummed.hits == 8 && strummed.misses == 0, "every strum of a fast run counts");
        // The strum note straight after a hammer-on is strummed inside the grace too.
        Track afterHammer = hopos;
        afterHammer.notes.push_back(hopos.notes[0]);
        afterHammer.notes[2].time = .38;
        Session pickUp(a, afterHammer);
        pickUp.gamepadMode = false;
        pickUp.update(0, 1, true);
        pickUp.update(.3, 2, false);  // hammer on
        pickUp.update(.36, 1, false); // fret the next note
        pickUp.update(.38, 1, true);  // and strum it 80 ms after the hammer-on
        pickUp.update(.6, 1, false);
        check(pickUp.hits == 3 && pickUp.misses == 0, "a strum right after a hammer-on plays its own note");
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
