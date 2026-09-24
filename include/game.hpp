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
    // Before the start of a practice loop: never played, never judged, not drawn.
    bool skipped = false;
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
    // Whammy, as in the originals: bending a sustain from an intact star
    // phrase fills star power while it rings, and keeps an active star power
    // going longer. Only movement counts; a bar or stick held still earns
    // nothing, so resting on it, or a drifting stick, is not rewarded. The
    // song itself is left untouched.
    static constexpr double whammyPowerPerBeat = 1.0 / 30; // a little faster than star power drains
    static constexpr double whammyStep = .06, whammyHold = .15;
    double whammyAnchor = -1, whammyUntil = -1e9, lastWhammyGain = -1e9;
    // Feed the bar or stick position each frame, 0 at rest to 1 fully in,
    // before update(). A move of whammyStep counts as whammying for whammyHold.
    void whammy(double position, double time) {
        if (whammyAnchor < 0 || std::abs(position - whammyAnchor) >= whammyStep) {
            if (whammyAnchor >= 0)
                whammyUntil = time + whammyHold;
            whammyAnchor = position;
        }
    }
    bool whammying(double time) const { return time < whammyUntil; }
    // Whether note i is a star-phrase sustain still being held, so whammying it pays.
    bool whammyable(size_t i) const {
        const auto &n = track->notes[i];
        return n.phrase >= 0 && !phraseFailed[size_t(n.phrase)] && state[i].result == 1 && state[i].held;
    }
    int tierFor(double err) const {
        const double d = std::abs(err);
        return d <= window * perfectShare ? 3 : d <= window * greatShare ? 2 : 1;
    }
    // Solos, as in the originals: every note hit inside one is worth a flat
    // 100 more when the solo ends, whatever the multiplier.
    static constexpr double soloNoteBonus = 100;
    std::vector<int> soloHit, soloTotal, soloJudged;
    std::vector<double> soloStart, soloEnd; // first and last note times
    struct SoloResult {
        int index = -1, hit = 0, total = 0;
        double bonus = 0, at = -1e9; // `at` is the song time it ended
    } lastSolo;
    double soloBonus = 0; // all solo bonuses so far
    Session(const Song &s, const Track &t)
        : song(&s), track(&t), state(t.notes.size()), phraseRemaining(t.phrases.size()),
          phraseFailed(t.phrases.size(), false), soloHit(t.solos.size()), soloTotal(t.solos.size()),
          soloJudged(t.solos.size()), soloStart(t.solos.size(), 1e300), soloEnd(t.solos.size(), -1e300) {
        for (auto &n : t.notes) {
            if (n.phrase >= 0)
                ++phraseRemaining[n.phrase];
            if (n.solo >= 0 && size_t(n.solo) < soloTotal.size()) {
                auto k = size_t(n.solo);
                ++soloTotal[k];
                soloStart[k] = std::min(soloStart[k], n.time), soloEnd[k] = std::max(soloEnd[k], n.time);
            }
        }
    }
    // The solo being played at `time`, for the live counter, or -1.
    int activeSolo(double time) const {
        for (size_t k = 0; k < soloTotal.size(); ++k)
            if (soloTotal[k] > 0 && soloJudged[k] < soloTotal[k] && time >= soloStart[k] - 1 && time <= soloEnd[k] + .3)
                return int(k);
        return -1;
    }
    // Practice starts part-way in: every note before `time` is set aside
    // rather than missed, so it breaks no combo, drains no meter and never
    // fails the run. A star phrase or solo cut short cannot pay out in full,
    // so phrases lose their payout and solos count only what is left of them.
    void startAt(double time) {
        size_t i = 0;
        for (; i < state.size() && track->notes[i].time < time; ++i) {
            auto &st = state[i];
            st.result = 1, st.held = 0, st.judgedAt = -1, st.skipped = true;
            const auto &n = track->notes[i];
            if (n.phrase >= 0)
                phraseFailed[size_t(n.phrase)] = true;
            if (n.solo >= 0 && size_t(n.solo) < soloTotal.size()) {
                auto k = size_t(n.solo);
                --soloTotal[k];
                soloStart[k] = 1e300;
            }
        }
        // Solos that begin before `time` start counting from the first note left.
        for (size_t j = i; j < state.size(); ++j) {
            const auto &n = track->notes[j];
            if (n.solo >= 0 && size_t(n.solo) < soloStart.size())
                soloStart[size_t(n.solo)] = std::min(soloStart[size_t(n.solo)], n.time);
        }
        next = std::max(next, i);
        lastTime = std::max(lastTime, time);
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
        if (n.solo >= 0 && size_t(n.solo) < soloTotal.size()) {
            auto k = size_t(n.solo);
            soloHit[k] += hit ? 1 : 0;
            if (++soloJudged[k] == soloTotal[k]) {
                const double bonus = soloNoteBonus * soloHit[k];
                score += bonus, soloBonus += bonus;
                lastSolo = {int(k), soloHit[k], soloTotal[k], bonus, now};
            }
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
                // Once per note, not per lane: a whammied chord is one bend.
                double bendFrom = 1e300, bendTo = -1e300;
                const bool bending = whammying(time) && whammyable(i);
                for (int l = 0; l < 6; ++l)
                    if (state[i].held & (1 << l)) {
                        bool down = l == 5 ? held == 0 : bool(held & (1 << l));
                        if (!down) {
                            state[i].held &= uint8_t(~(1 << l));
                            continue;
                        }
                        double a = std::max(lastTime, n.time), b = std::min(time, n.end[l]);
                        if (b > a) {
                            score += 25 * std::max(0.0, song->tickAt(b) - song->tickAt(a)) /
                                     song->resolution * multiplier();
                            bendFrom = std::min(bendFrom, a), bendTo = std::max(bendTo, b);
                        }
                        if (time >= n.end[l])
                            state[i].held &= uint8_t(~(1 << l));
                        else if (l < 5)
                            sustainedFrets |= uint8_t(1 << l);
                    }
                if (bending && bendTo > bendFrom) {
                    power = std::min(1.0, power + std::max(0.0, song->tickAt(bendTo) - song->tickAt(bendFrom)) /
                                                      song->resolution * whammyPowerPerBeat);
                    lastWhammyGain = time;
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

// How a run went section by section, for the results screen: notes hit out
// of notes judged in each named section. Notes before the first section count
// towards it; sections with no notes are left out.
struct SectionStat {
    std::string name;
    int hit = 0, total = 0;
};
inline std::vector<SectionStat> sectionStats(const Song &song, const Session &session) {
    std::vector<SectionStat> out;
    const auto &sections = song.sections;
    if (sections.empty())
        return out;
    for (const auto &s : sections)
        out.push_back({s.name, 0, 0});
    size_t k = 0;
    for (size_t i = 0; i < session.state.size(); ++i) {
        const auto &st = session.state[i];
        if (!st.result || st.skipped)
            continue;
        const double t = session.track->notes[i].time;
        while (k + 1 < sections.size() && t >= sections[k + 1].time)
            ++k;
        ++out[k].total;
        out[k].hit += st.result == 1 ? 1 : 0;
    }
    out.erase(std::remove_if(out.begin(), out.end(), [](const SectionStat &s) { return s.total == 0; }), out.end());
    return out;
}
} // namespace fret
