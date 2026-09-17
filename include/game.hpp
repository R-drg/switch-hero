#pragma once
#include "song.hpp"
#include <algorithm>
#include <cmath>

namespace fret {
struct NoteState {
    int result = 0;
    uint8_t held = 0;
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
    double window = 0.10;
    uint8_t previous = 0;
    Session(const Song &s, const Track &t)
        : song(&s), track(&t), state(t.notes.size()), phraseRemaining(t.phrases.size()),
          phraseFailed(t.phrases.size(), false) {
        for (auto &n : t.notes)
            if (n.phrase >= 0)
                ++phraseRemaining[n.phrase];
    }
    int multiplier() const { return std::min(4, 1 + combo / 10) * (powerActive ? 2 : 1); }
    void activate() {
        if (!powerActive && power >= 0.5)
            powerActive = true;
    }
    void broken() { combo = 0; }
    void settle(size_t i, bool hit) {
        auto &st = state[i];
        if (st.result)
            return;
        auto &n = track->notes[i];
        st.result = hit ? 1 : -1;
        if (hit) {
            ++hits;
            int count = 0;
            for (int l = 0; l < 6; ++l)
                if (n.mask & (1 << l))
                    ++count;
            score += 50 * count * multiplier();
            ++combo;
            maxCombo = std::max(maxCombo, combo);
            st.held = n.mask;
        } else {
            ++misses;
            broken();
        }
        if (n.phrase >= 0) {
            auto p = size_t(n.phrase);
            if (!hit)
                phraseFailed[p] = true;
            if (--phraseRemaining[p] == 0 && !phraseFailed[p])
                power = std::min(1.0, power + 0.25);
        }
    }
    void update(double time, uint8_t held, bool strum, bool openPress = false) {
        if (time < lastTime)
            time = lastTime;
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
        while (next < state.size() && track->notes[next].time < time - window) {
            settle(next, false);
            ++next;
        }
        if (next < state.size()) {
            auto &n = track->notes[next];
            uint8_t rising = held & uint8_t(~previous);
            // Extended sustains may remain held beneath a new note on another fret.
            bool match = n.mask == 32
                             ? held == 0
                             : (held & n.mask) == n.mask && (held & uint8_t(~(n.mask | sustainedFrets))) == 0;
            // In strum mode allow lower-fret anchoring for single fretted notes.
            if (!gamepadMode && n.mask != 32 && (n.mask & (n.mask - 1)) == 0)
                match = match || ((held & n.mask) && held < (n.mask << 1));
            bool action = gamepadMode
                              ? (n.mask == 32 ? openPress : bool(rising & n.mask))
                              : (strum || ((n.kind == Kind::Tap || (n.kind == Kind::Hopo && combo > 0)) &&
                                           held != previous));
            if (std::abs(n.time - time) <= window && match && action) {
                settle(next, true);
                ++next;
            } else if (!gamepadMode && strum)
                broken();
        } else if (!gamepadMode && strum)
            broken();
        previous = held;
        lastTime = time;
    }
    bool complete(double time) const { return next == state.size() && time > song->duration + 1; }
};
} // namespace fret
