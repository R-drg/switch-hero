#pragma once
#include <algorithm>
#include <cmath>

namespace fret {
// The song clock the highway and judgement run on.
//
// The audio position only moves when the backend reports played samples, and
// on desktop that is a whole queued chunk at a time (10-80 ms). Chasing it frame
// by frame turned that sawtooth into a speed wobble on the highway. Instead the
// clock runs on the realtime timer, the way YARG runs its song clock off the
// input timer, and only leans towards the audio's *average* position: the error
// is low-passed and may change the running speed by at most 1%, which is
// invisible, while real clock drift between the two is around 0.01%.
struct SmoothClock {
    // Past this the clock re-anchors outright: resume, seek or an audio stall.
    static constexpr double snap = .1;
    // How far the running speed may lean towards the audio, and how hard.
    static constexpr double maxLean = .01, gain = 1;
    // Time constant of the error filter; long enough to average out the chunks.
    static constexpr double settle = .5;

    double time = 0, last = 0, rate = 1, drift = 0;
    bool valid = false;

    void reset() { valid = false; }
    // audioTime is the song position the audio reports; now is the realtime
    // timer in seconds. Call once per frame.
    double sync(double audioTime, double now) {
        if (!valid) {
            anchor(audioTime, now);
            valid = true;
            return time;
        }
        const double dt = std::clamp(now - last, 0.0, .1);
        last = now;
        time += dt * rate;
        const double error = audioTime - time;
        if (std::abs(error) > snap) {
            anchor(audioTime, now);
            return time;
        }
        drift += (error - drift) * (1 - std::exp(-dt / settle));
        rate = 1 + std::clamp(drift * gain, -maxLean, maxLean);
        return time;
    }

  private:
    void anchor(double audioTime, double now) {
        time = audioTime, last = now, rate = 1, drift = 0;
    }
};
} // namespace fret
