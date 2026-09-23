// Multiplayer is four independent Sessions over one song. What can go wrong is
// not the scoring — that is the same code single player already exercises — but
// leakage: one player's press, combo, star power or rock meter reaching another.
// These tests interleave players deliberately to catch exactly that.
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

// A plain four-on-the-floor run of single notes, one every 100 ms.
Track runOf(int count, int difficulty) {
    Track t;
    t.instrument = "guitar";
    t.difficulty = difficulty;
    for (int i = 0; i < count; ++i) {
        Note n;
        n.time = i * .1;
        n.tick = i * 192;
        n.mask = uint8_t(1 << (i % 5));
        n.kind = Kind::Strum;
        n.phrase = -1;
        t.notes.push_back(n);
    }
    return t;
}

int main() {
    try {
        Song song;
        song.resolution = 192;
        song.duration = 10;
        song.tempos.push_back({0, 120, 0});
        song.tracks.push_back(runOf(20, 3));
        song.tracks.push_back(runOf(20, 0));

        {
            // Four players, same track. Only player two plays; nobody else moves.
            std::vector<const Track *> tracks(4, &song.tracks[0]);
            MultiSession m(song, tracks, 1);
            check(m.size() == 4, "four players");
            for (size_t p = 0; p < 4; ++p)
                check(m[p].noFail, "multiplayer forces no-fail on every player");
            for (int i = 0; i < 20; ++i) {
                const uint8_t mask = song.tracks[0].notes[size_t(i)].mask;
                m[1].update(i * .1, mask, false, false, i * .1);
                m[1].update(i * .1 + .05, 0, false, false, i * .1 + .05);
            }
            for (size_t p = 0; p < 4; ++p)
                m[p].update(3, 0, false);
            check(m[1].hits == 20 && m[1].misses == 0, "the player who played hit everything");
            check(m[1].combo == 20 && m[1].score > 0, "their combo and score are their own");
            for (size_t p : {size_t(0), size_t(2), size_t(3)}) {
                check(m[p].hits == 0, "an idle player hits nothing");
                check(m[p].score == 0, "an idle player scores nothing");
                check(m[p].combo == 0, "an idle player has no combo");
                check(m[p].misses == 20, "an idle player misses every note");
            }
            check(m[1].meter > m[0].meter, "the rock meters moved apart");
            check(!m[0].failed, "no-fail keeps a drained player in the song");
        }
        {
            // Interleaved presses in the same instant: two players pressing the
            // same fret in one frame must each take their own note.
            std::vector<const Track *> tracks(2, &song.tracks[0]);
            MultiSession m(song, tracks, 1);
            for (int i = 0; i < 10; ++i) {
                const uint8_t mask = song.tracks[0].notes[size_t(i)].mask;
                m[0].update(i * .1, mask, false, false, i * .1);
                m[1].update(i * .1, mask, false, false, i * .1);
                m[0].update(i * .1 + .05, 0, false, false, i * .1 + .05);
                m[1].update(i * .1 + .05, 0, false, false, i * .1 + .05);
            }
            check(m[0].hits == 10 && m[1].hits == 10, "simultaneous presses both count");
            near(m[0].score, m[1].score, "identical play scores identically");
        }
        {
            // Star power is per player: one activating must not touch the other.
            std::vector<const Track *> tracks(2, &song.tracks[0]);
            MultiSession m(song, tracks, 1);
            m[0].power = 1;
            m[0].activate();
            check(m[0].powerActive, "player one activated star power");
            check(!m[1].powerActive, "player two's star power is untouched");
            check(m[0].multiplier() > m[1].multiplier(), "only the active player doubles");
        }
        {
            // Different difficulties per player: each gets their own hit window,
            // which is the whole point of letting players pick independently.
            std::vector<const Track *> tracks{&song.tracks[0], &song.tracks[1]};
            MultiSession m(song, tracks, 1);
            check(m[1].window > m[0].window, "the easier chart gets the wider window");
            near(m[0].window, Session::windowFor(3, 1), "expert window");
            near(m[1].window, Session::windowFor(0, 1), "easy window");
        }
        {
            // Standings: order, and ties sharing a rank.
            std::vector<const Track *> tracks(4, &song.tracks[0]);
            MultiSession m(song, tracks, 1);
            m[0].score = 100, m[1].score = 300, m[2].score = 300, m[3].score = 50;
            auto s = m.standings();
            check(s.size() == 4, "one standing per player");
            check(s[0].score == 300 && s[1].score == 300, "best scores lead");
            check(s[0].rank == 1 && s[1].rank == 1, "a tie shares the rank");
            check(s[2].rank == 3, "the rank after a two-way tie is third");
            check(s[3].player == 3 && s[3].rank == 4, "last place");
        }
        {
            // A player who never plays is still ranked, not dropped.
            std::vector<const Track *> tracks(2, &song.tracks[0]);
            MultiSession m(song, tracks, 1);
            m[0].score = 10;
            auto s = m.standings();
            check(s.size() == 2 && s[1].player == 1 && s[1].score == 0, "a silent player still places");
            near(s[1].accuracy, 0, "and scores zero accuracy");
        }
        std::cout << checks << " multiplayer checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
