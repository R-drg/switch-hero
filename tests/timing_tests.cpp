#include "calibration.hpp"
#include "clock.hpp"
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
// The audio clock as the desktop backend reports it: the true position, rounded
// down to whole queued chunks, running slightly fast against the realtime timer.
double stepped(double t, double chunk, double skew) { return std::floor(t * skew / chunk) * chunk; }

int main() {
    try {
        {
            // A 40 ms chunked audio clock at 60 fps must not turn into a speed wobble.
            SmoothClock c;
            const double frame = 1 / 60.0, chunk = .04, skew = 1.0002;
            double previous = c.sync(0, 0), worstStep = 0, worstLateError = 0;
            for (int i = 1; i < 60 * 20; ++i) {
                const double now = i * frame, t = c.sync(stepped(now, chunk, skew), now);
                check(t >= previous, "clock never runs backwards");
                worstStep = std::max(worstStep, std::abs((t - previous) / frame - 1));
                // The chunked clock trails the true position by half a chunk on average.
                if (i > 60 * 10)
                    worstLateError = std::max(worstLateError, std::abs(t - (now * skew - chunk / 2)));
                previous = t;
            }
            check(worstStep <= SmoothClock::maxLean + 1e-9, "per-frame speed stays within the lean limit");
            check(worstLateError < .005, "clock settles on the audio's average position");
        }
        {
            // A real jump (resume, seek, stall) re-anchors instead of drifting over.
            SmoothClock c;
            c.sync(0, 0);
            c.sync(1 / 60.0, 1 / 60.0);
            check(std::abs(c.sync(.25, 2 / 60.0) - .25) < 1e-9, "large error re-anchors");
            c.reset();
            check(c.sync(5, 3 / 60.0) == 5, "reset anchors on the next read");
        }
        {
            // A press stamped inside the window is a hit even when the frame that
            // reads it lands after the window has closed.
            Song song;
            song.tempos = {{0, 120, 0}};
            Track track;
            Note n;
            n.mask = 1, n.time = 1;
            track.notes = {n};
            Session s(song, track);
            s.update(.9, 0, false);
            s.update(1.08, 1, false, false, 1.01);
            check(s.hits == 1 && s.misses == 0, "stamped press judged at its own time");
            check(std::abs(s.state[0].error - .01) < 1e-9, "tier error uses the press time");
            Session late(song, track);
            late.update(.9, 0, false);
            late.update(1.08, 1, false);
            check(late.hits == 0, "unstamped press is judged at the frame");
            Session early(song, track);
            early.update(.95, 0, false);
            early.update(1.0, 1, false, false, .5);
            check(early.hits == 1 && std::abs(early.state[0].error + .05) < 1e-9,
                  "press time is clamped to the previous frame");
        }
        {
            // Taps 40 ms late with one fumble: the median ignores the fumble.
            TapCalibration c;
            check(!c.tap(1.04), "count-in taps are ignored");
            for (int beat = 4; !c.done(); ++beat)
                c.tap(beat * c.period + (beat == 7 ? .2 : .04));
            check(std::abs(c.median() - .04) < 1e-9, "median is the steady offset");
            check(c.spread() < 1e-9, "one fumble does not read as an uneven hand");
            check(!c.tap(100), "no taps after it is done");
            // Early taps are scored against the beat they were aimed at, not the last one.
            TapCalibration early;
            for (int beat = 4; !early.done(); ++beat)
                early.tap(beat * early.period - .03);
            check(std::abs(early.median() + .03) < 1e-9, "early taps give a negative offset");
            check(TapCalibration::medianOf({3, 1, 2, 10}) == 2.5, "even-sized median");
        }
        std::cout << checks << " timing checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
