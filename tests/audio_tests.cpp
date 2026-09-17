#include "audio.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace fret;
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("fixture path required");
        fs::path root = argv[1];
        int tested = 0;
        for (auto p : {root / "chart" / "song.wav", root / "audio.ogg", root / "audio.opus",
                       root / "audio.mp3", root / "audio.flac"}) {
            if (!fs::exists(p))
                throw std::runtime_error("Missing codec test fixture: " + p.string());
            auto d = openDecoder(p);
            std::array<float, 4096> buf{};
            size_t total = 0;
            double energy = 0;
            for (;;) {
                size_t n = d->read(buf.data(), buf.size() / d->channels);
                if (!n)
                    break;
                total += n;
                for (size_t i = 0; i < n * d->channels; ++i) {
                    if (!std::isfinite(buf[i]))
                        throw std::runtime_error("Nonfinite audio sample");
                    energy += buf[i] * buf[i];
                }
            }
            if (total < size_t(d->rate * .9) || total > size_t(d->rate * 1.1) || energy < 1)
                throw std::runtime_error("Audio duration or waveform mismatch");
            std::cout << p.extension() << ": " << total << " frames at " << d->rate << " Hz\n";
            ++tested;
        }
        if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER))
            throw std::runtime_error(SDL_GetError());
        {
            Audio a;
            auto song = loadSong(root / "chart");
            a.load(song);
            double p = a.position();
            SDL_Delay(70);
            if (std::abs(p - a.position()) > .001)
                throw std::runtime_error("Paused clock moves");
            a.pause(false);
            SDL_Delay(120);
            if (a.position() <= p + .05)
                throw std::runtime_error("Playback clock did not advance");
            a.pause(true);
            p = a.position();
            SDL_Delay(70);
            if (std::abs(p - a.position()) > .001)
                throw std::runtime_error("Pause does not freeze clock");
            a.load(song);
            if (std::abs(a.position() + 2) > .001)
                throw std::runtime_error("Restart does not reset clock");
            a.stop();
        }
        SDL_Quit();
        std::cout << tested << " codecs and playback/pause/restart passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
