#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace fret {
// Tap-along offset calibration. The player taps on a steady beat; each tap is
// scored against the nearest beat, and the median of those errors is the
// offset. The median rather than the mean, so one fumbled tap cannot drag the
// result.
struct TapCalibration {
    double period = .5;   // seconds between beats
    double warmup = 2;    // taps before this song time only settle the player in
    size_t needed = 12;   // taps that count
    std::vector<double> errors;

    bool done() const { return errors.size() >= needed; }
    // Returns false when the tap was ignored (count-in, or already done).
    bool tap(double time) {
        if (time < warmup || done())
            return false;
        errors.push_back(time - std::round(time / period) * period);
        return true;
    }
    static double medianOf(std::vector<double> v) {
        if (v.empty())
            return 0;
        std::sort(v.begin(), v.end());
        const size_t m = v.size() / 2;
        return v.size() % 2 ? v[m] : (v[m - 1] + v[m]) / 2;
    }
    double median() const { return medianOf(errors); }
    // Median distance from the median: how steady the taps were.
    double spread() const {
        std::vector<double> d;
        const double m = median();
        for (double e : errors)
            d.push_back(std::abs(e - m));
        return medianOf(d);
    }
};
} // namespace fret
