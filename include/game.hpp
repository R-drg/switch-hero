#pragma once
#include "song.hpp"
#include <algorithm>
#include <cmath>

namespace fret {
struct NoteState {
    int result = 0;
    uint8_t held = 0;
    double judgedAt = -1;
    // How far off the press was, in seconds: negative early, positive late.
    // Only meaningful once result == 1.
    double error = 0;
    int tier = 0; // 0 none, 1 good, 2 great, 3 perfect
}; // 0 pending, 1 hit, -1 missed
struct Session {
    const Song *song;
    const Track *track;
    std::vector<NoteState> state;
    std::vector<int> phraseRemaining;
    std::vector<bool> phraseFailed;
    size_t next = 0;
    int combo = 0, maxCombo = 0, hits = 0, misses = 0;
    double score = 0, power = 0, lastTime = -10000;
    bool powerActive = false, gamepadMode = true;
    // Rock meter: a tug of war that punishes mistakes three times harder than it
    // rewards hits, so sloppy runs drain it fast. Star power pulls back twice as
    // quickly, the emergency rescue for a section you keep fumbling.
    static constexpr double meterStep = .018, meterRed = .28, meterGreen = .62;
    double meter = .5;
    bool noFail = false, failed = false;
    // Strum leniency, as real guitar games have it: a strum that lands just
    // before its note still counts, and a strum right after a hammer-on you
    // already played is not punished if it finds no note of its own.
    static constexpr double strumLeniency = .09, hammerGrace = .16;
    double pendingStrum = -1e9, lastHammer = -1e9;
    bool strumAfterHammer = false;
    // The outer window is what still counts as a hit at all. It has to stay
    // comfortably tighter than the gap between notes in a fast run, or a late
    // press gets eaten by the note before the one it was aimed at and every
    // press after it lands a note behind.
    double window = 0.07;
    // Inside it, how close you were decides the tier, and the tier decides both
    // the score and how hard the hit reads on screen. Without this a note nailed
    // dead-on and one scraped in at the edge of the window are the same event.
    // The tiers scale with the window (25 and 45 ms of the original 70), so a
    // wider window on an easier difficulty is wider all the way through.
    static constexpr double perfectShare = .025 / .07, greatShare = .045 / .07;
    static constexpr double tierBonus[4] = {1, 1, 1.15, 1.35};
    int tierCounts[4] = {0, 0, 0, 0};
    int lastTier = 0;
    double lastError = 0, lastTierAt = -1e9;
    uint8_t previous = 0;
    int tierFor(double err) const {
        const double d = std::abs(err);
        return d <= window * perfectShare ? 3 : d <= window * greatShare ? 2 : 1;
    }
    Session(const Song &s, const Track &t)
        : song(&s), track(&t), state(t.notes.size()), phraseRemaining(t.phrases.size()),
          phraseFailed(t.phrases.size(), false) {
        for (auto &n : t.notes)
            if (n.phrase >= 0)
                ++phraseRemaining[n.phrase];
    }
    // How wide the hit window is for a difficulty (0 easy to 3 expert) and the
    // player's choice (0 strict, 1 normal, 2 lenient). Easier charts forgive
    // more, as the originals do; strict expert keeps the old 70 ms.
    static double windowFor(int difficulty, int leniency) {
        static constexpr double base[4] = {.110, .100, .092, .085};
        static constexpr double scale[3] = {.82, 1, 1.25};
        return base[std::clamp(difficulty, 0, 3)] * scale[std::clamp(leniency, 0, 2)];
    }
    int multiplier() const { return std::min(4, 1 + combo / 10) * (powerActive ? 2 : 1); }
    void activate() {
        if (!powerActive && power >= 0.5)
            powerActive = true;
    }
    void broken() { combo = 0; }
    void reward() { meter = std::min(1.0, meter + meterStep * (powerActive ? 2 : 1)); }
    // Missing a note, or strumming at nothing, costs three steps.
    void penalise() {
        broken();
        meter = std::max(0.0, meter - meterStep * 3);
        if (meter <= 0 && !noFail)
            failed = true;
    }
    bool inRed() const { return meter < meterRed; }
    void settle(size_t i, bool hit, double now) {
        auto &st = state[i];
        if (st.result)
            return;
        auto &n = track->notes[i];
        st.result = hit ? 1 : -1;
        st.judgedAt = now;
        if (hit)
            pendingStrum = -1e9; // a hit always spends the strum
        if (hit) {
            ++hits;
            int count = 0;
            for (int l = 0; l < 6; ++l)
                if (n.mask & (1 << l))
                    ++count;
            // Precision pays: a note taken dead-on is worth a third more than one
            // scraped in at the edge of the window.
            st.error = now - n.time;
            st.tier = tierFor(st.error);
            ++tierCounts[st.tier];
            lastTier = st.tier, lastError = st.error, lastTierAt = now;
            score += 50 * count * multiplier() * tierBonus[st.tier];
            ++combo;
            maxCombo = std::max(maxCombo, combo);
            st.held = n.mask;
            reward();
        } else {
            ++misses;
            penalise();
        }
        if (n.phrase >= 0) {
            auto p = size_t(n.phrase);
            if (!hit)
                phraseFailed[p] = true;
            if (--phraseRemaining[p] == 0 && !phraseFailed[p])
                power = std::min(1.0, power + 0.25);
        }
    }
    // Can this press complete note `n`?
    bool completes(const Note &n, uint8_t held, uint8_t rising, bool open, uint8_t sustained,
                   bool changed) const {
        if (n.mask == 32)
            // Open notes take any new press: the dedicated open button or any fret.
            return open || rising != 0;
        if (n.mask & (n.mask - 1))
            // Chords must be fretted exactly: an extra fret is a wrong chord, not a
            // sloppy right one. Any change that lands on the exact shape counts, so a
            // repeated chord can be re-struck with one fret while the rest stay down,
            // and a stray finger can be lifted to correct the chord. Frets held by a
            // running sustain are not part of the shape.
            return changed && (held & n.mask) == n.mask && (held & uint8_t(~(n.mask | sustained))) == 0;
        // Single notes stay forgiving: a fret still held from the previous note in
        // a fast run does not spoil the next hit.
        return (rising & n.mask) && (held & n.mask) == n.mask;
    }
    // Press-to-hit judgement. Every new press takes the note it fits that lies
    // *nearest in time*, so frets still held from the previous note in a fast run
    // are ignored, two notes pressed within one frame both count, and an open note
    // accepts any fret as well as the open button.
    //
    // Nearest rather than earliest matters in fast runs: a press aimed at the note
    // in front of you must not be swallowed by a still-pending note behind it, or
    // the whole rest of the run lands one note late.
    void press(double time, uint8_t held, uint8_t rising, bool open, uint8_t sustained, bool changed) {
        while (rising || open || changed) {
            size_t best = state.size();
            double bestError = 0;
            for (size_t i = next; i < state.size(); ++i) {
                const auto &n = track->notes[i];
                if (n.time > time + window)
                    break;
                if (!state[i].result && completes(n, held, rising, open, sustained, changed)) {
                    const double error = std::abs(n.time - time);
                    if (best == state.size() || error < bestError)
                        best = i, bestError = error;
                }
                // A partially pressed chord claims the press rather than letting it
                // run on to a later note. Single notes do not claim it that way: that
                // is what used to feed a late press to the note behind the target.
                if (n.mask != 32 && (n.mask & (n.mask - 1)) && (rising & n.mask))
                    break;
                // A press never reaches past a note that is not yet due.
                if (n.time > time)
                    break;
            }
            if (best == state.size())
                break;
            const auto &n = track->notes[best];
            // Notes skipped to reach this one were due and went unplayed.
            for (size_t j = next; j < best; ++j)
                settle(j, false, time);
            settle(best, true, time);
            next = best + 1;
            // One press, one note.
            if (n.mask == 32)
                open = false, rising = 0;
            else
                rising &= uint8_t(~n.mask);
            changed = false;
        }
    }
    // `actionTime` is when this frame's press or strum actually happened, if the
    // input layer knows (event timestamps). Judging at the frame instead would
    // shift every hit by up to a frame depending on where it fell in it.
    void update(double time, uint8_t held, bool strum, bool openPress = false, double actionTime = 1e300) {
        if (time < lastTime)
            time = lastTime;
        const double at = std::clamp(actionTime, lastTime, time);
        double dt = lastTime < -9999 ? 0 : time - lastTime;
        if (powerActive) {
            power = std::max(0.0, power - std::max(0.0, song->tickAt(time) - song->tickAt(time - dt)) /
                                              (song->resolution * 32.0));
            if (power <= 0)
                powerActive = false;
        }
        // Sustain points use elapsed beats, not frame count. A released sustain cannot be reacquired.
        uint8_t sustainedFrets = 0;
        for (size_t i = 0; i < next; ++i)
            if (state[i].result == 1 && state[i].held) {
                auto &n = track->notes[i];
                for (int l = 0; l < 6; ++l)
                    if (state[i].held & (1 << l)) {
                        bool down = l == 5 ? held == 0 : bool(held & (1 << l));
                        if (!down) {
                            state[i].held &= uint8_t(~(1 << l));
                            continue;
                        }
                        double a = std::max(lastTime, n.time), b = std::min(time, n.end[l]);
                        if (b > a)
                            score += 25 * std::max(0.0, song->tickAt(b) - song->tickAt(a)) /
                                     song->resolution * multiplier();
                        if (time >= n.end[l])
                            state[i].held &= uint8_t(~(1 << l));
                        else if (l < 5)
                            sustainedFrets |= uint8_t(1 << l);
                    }
            }
        // Sweep only up to the press, so a note it landed on is not written off
        // first; anything later is swept on the next frame.
        while (next < state.size() && track->notes[next].time < at - window) {
            settle(next, false, at);
            ++next;
        }
        if (gamepadMode)
            press(at, held, held & uint8_t(~previous), openPress, sustainedFrets, held != previous);
        else {
            // Every strum is held briefly in case its note has not arrived yet.
            // It must never be thrown away outright: in a fast run the next note
            // is often due well inside the hammer-on grace, and dropping the strum
            // there missed every other note of any run faster than the grace.
            if (strum)
                pendingStrum = at, strumAfterHammer = at - lastHammer <= hammerGrace;
            const bool strumming = at - pendingStrum <= strumLeniency;
            if (next < state.size()) {
                auto &n = track->notes[next];
                // Extended sustains may remain held beneath a new note on another fret.
                bool match = n.mask == 32
                                 ? held == 0
                                 : (held & n.mask) == n.mask && (held & uint8_t(~(n.mask | sustainedFrets))) == 0;
                // In strum mode allow lower-fret anchoring for single fretted notes.
                if (n.mask != 32 && (n.mask & (n.mask - 1)) == 0)
                    match = match || ((held & n.mask) && held < (n.mask << 1));
                const bool action = strumming || ((n.kind == Kind::Tap || (n.kind == Kind::Hopo && combo > 0)) &&
                                                  held != previous);
                if (std::abs(n.time - at) <= window && match && action) {
                    if (!strumming)
                        lastHammer = at;
                    settle(next, true, at);
                    ++next;
                    pendingStrum = -1e9; // the strum was spent on this note
                }
            }
            // A strum only costs the player once it has had its chance to land.
            // Strumming along with a hammer-on is how the song is usually played,
            // not a mistake, so that one is let go.
            if (pendingStrum > -1e8 && time - pendingStrum > strumLeniency) {
                if (!strumAfterHammer)
                    penalise(); // overstrum
                pendingStrum = -1e9;
            }
        }
        previous = held;
        lastTime = time;
    }
    bool complete(double time) const { return next == state.size() && time > song->duration + 1; }
};
} // namespace fret
