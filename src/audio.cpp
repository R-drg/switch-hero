#include "audio.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

#ifndef __SWITCH__
#include <sndfile.h>
#else
#include <cstdlib>
#include <malloc.h>
#include <mpg123.h>
#include <opus/opusfile.h>
#include <switch.h>
#include <vorbis/vorbisfile.h>
#endif

namespace fret {
void Decoder::seek(double seconds) {
    std::vector<float> scratch(4096 * size_t(std::max(1, channels)));
    for (auto left = uint64_t(std::max(0.0, seconds) * rate); left > 0;) {
        const size_t n = read(scratch.data(), size_t(std::min<uint64_t>(left, 4096)));
        if (!n)
            break;
        left -= n;
    }
}
#ifndef __SWITCH__
class FileDecoder : public Decoder {
    SNDFILE *file = nullptr;

  public:
    explicit FileDecoder(const fs::path &p) {
        SF_INFO info{};
        file = sf_open(p.string().c_str(), SFM_READ, &info);
        if (!file)
            throw std::runtime_error("Audio: " + p.filename().string() + ": " + sf_strerror(nullptr));
        rate = info.samplerate;
        channels = info.channels;
        duration = double(info.frames) / rate;
        if (channels < 1 || channels > 2) {
            sf_close(file);
            file = nullptr;
            throw std::runtime_error("Only mono/stereo audio stems supported");
        }
    }
    ~FileDecoder() {
        if (file)
            sf_close(file);
    }
    size_t read(float *out, size_t frames) override {
        // libsndfile's Ogg Opus reader flags pages whose timestamps do not add up
        // as malformed, then decodes straight on past them. Charted stems are
        // often re-encoded with exactly that quirk, so only a read that yields
        // nothing at all counts as a failure.
        for (int attempt = 0; attempt < 4; ++attempt) {
            const auto n = sf_readf_float(file, out, sf_count_t(frames));
            if (n > 0)
                return size_t(n);
            if (n == 0 && !sf_error(file))
                return 0; // end of the stem
        }
        throw std::runtime_error("Audio decode error");
    }
    void seek(double seconds) override {
        if (sf_seek(file, sf_count_t(std::max(0.0, seconds) * rate), SEEK_SET) < 0)
            Decoder::seek(seconds);
    }
};
std::unique_ptr<Decoder> openDecoder(const fs::path &p) { return std::make_unique<FileDecoder>(p); }
#else
class VorbisDecoder : public Decoder {
    OggVorbis_File file{};
    bool opened = false;
    int holes = 0; // damaged pages skipped; past a few dozen the file is just broken

  public:
    explicit VorbisDecoder(const fs::path &p) {
        if (ov_fopen(p.string().c_str(), &file))
            throw std::runtime_error("Cannot open Vorbis: " + p.string());
        opened = true;
        auto info = ov_info(&file, -1);
        rate = int(info->rate);
        channels = info->channels;
        duration = ov_time_total(&file, -1);
        if (channels < 1 || channels > 2) {
            ov_clear(&file);
            opened = false;
            throw std::runtime_error("Only mono/stereo Vorbis supported");
        }
    }
    ~VorbisDecoder() {
        if (opened)
            ov_clear(&file);
    }
    size_t read(float *out, size_t frames) override {
        size_t got = 0;
        while (got < frames) {
            float **pcm = nullptr;
            int section = 0;
            long n = ov_read_float(&file, &pcm, int(frames - got), &section);
            if (n == 0)
                break;
            // A hole is a damaged or missing page; decoding carries on after it.
            if (n == OV_HOLE && ++holes < 64)
                continue;
            if (n < 0)
                throw std::runtime_error("Vorbis decode error");
            auto info = ov_info(&file, section);
            if (info->channels != channels || info->rate != rate)
                throw std::runtime_error("Chained Vorbis changes format");
            for (long i = 0; i < n; ++i)
                for (int c = 0; c < channels; ++c)
                    out[(got + i) * channels + c] = pcm[c][i];
            got += n;
        }
        return got;
    }
    void seek(double seconds) override {
        if (ov_time_seek(&file, std::max(0.0, seconds)))
            throw std::runtime_error("Vorbis seek failed");
    }
};
class OpusDecoder : public Decoder {
    OggOpusFile *file = nullptr;
    int holes = 0; // damaged pages skipped; past a few dozen the file is just broken

  public:
    explicit OpusDecoder(const fs::path &p) {
        int e = 0;
        file = op_open_file(p.string().c_str(), &e);
        if (!file)
            throw std::runtime_error("Cannot open Opus: " + p.string());
        rate = 48000;
        channels = 2;
        duration = double(op_pcm_total(file, -1)) / 48000;
    }
    ~OpusDecoder() {
        if (file)
            op_free(file);
    }
    size_t read(float *out, size_t frames) override {
        size_t got = 0;
        while (got < frames) {
            int n = op_read_float_stereo(file, out + got * 2, int((frames - got) * 2));
            if (n == 0)
                break;
            // A hole is a damaged or missing page; decoding carries on after it.
            if (n == OP_HOLE && ++holes < 64)
                continue;
            if (n < 0)
                throw std::runtime_error("Opus decode error");
            got += n;
        }
        return got;
    }
    void seek(double seconds) override {
        if (op_pcm_seek(file, ogg_int64_t(std::max(0.0, seconds) * 48000)))
            throw std::runtime_error("Opus seek failed");
    }
};
// A Xing/Info/VBRI header in the first frame (after any ID3v2 tag) carries the
// track length and a seek table, so the file need not be scanned end to end.
static bool mp3HasSeekHeader(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    unsigned char id3[10] = {};
    f.read(reinterpret_cast<char *>(id3), 10);
    std::streamoff start = 0;
    if (f.gcount() == 10 && id3[0] == 'I' && id3[1] == 'D' && id3[2] == '3')
        start = 10 +
                ((std::streamoff(id3[6] & 0x7f) << 21) | (std::streamoff(id3[7] & 0x7f) << 14) |
                 (std::streamoff(id3[8] & 0x7f) << 7) | std::streamoff(id3[9] & 0x7f)) +
                ((id3[5] & 0x10) ? 10 : 0);
    f.clear();
    f.seekg(start);
    std::string head(4096, '\0');
    f.read(head.data(), std::streamsize(head.size()));
    head.resize(size_t(std::max<std::streamsize>(0, f.gcount())));
    return head.find("Xing") != std::string::npos || head.find("Info") != std::string::npos ||
           head.find("VBRI") != std::string::npos;
}
class Mp3Decoder : public Decoder {
    mpg123_handle *file = nullptr;

  public:
    explicit Mp3Decoder(const fs::path &p) {
        static const int init = mpg123_init();
        if (init != MPG123_OK)
            throw std::runtime_error("MP3 init failed");
        const bool seekHeader = mp3HasSeekHeader(p);
        int e = 0;
        file = mpg123_new(nullptr, &e);
        if (!file)
            throw std::runtime_error("MP3 allocation failed");
        try {
            // With a seek header, fuzzy seeking uses its table instead of an
            // exact frame index built by reading the whole file.
            mpg123_param(file, MPG123_ADD_FLAGS, MPG123_QUIET | (seekHeader ? MPG123_FUZZY : 0), 0);
            // Float output has to be the only format allowed *before* opening.
            // Narrowing it after the first getformat left libmpg123 emitting
            // 16-bit samples, which were then read as floats: half-length noise.
            mpg123_format_none(file);
            const long *rates = nullptr;
            size_t rateCount = 0;
            mpg123_rates(&rates, &rateCount);
            for (size_t i = 0; i < rateCount; ++i)
                mpg123_format(file, rates[i], MPG123_MONO | MPG123_STEREO, MPG123_ENC_FLOAT_32);
            if (mpg123_open(file, p.string().c_str()) != MPG123_OK)
                throw std::runtime_error("Cannot open MP3");
            long r;
            int encoding;
            if (mpg123_getformat(file, &r, &channels, &encoding) != MPG123_OK)
                throw std::runtime_error("Cannot read MP3 format");
            if (encoding != MPG123_ENC_FLOAT_32)
                throw std::runtime_error("MP3 float output unavailable");
            rate = int(r);
            if (channels < 1 || channels > 2)
                throw std::runtime_error("Only mono/stereo MP3 supported");
            // Without a header the length estimate is wrong for VBR files and
            // seeks land in the wrong place, so those still get scanned once.
            off_t length = seekHeader ? mpg123_length(file) : 0;
            if (length <= 0) {
                mpg123_scan(file);
                length = mpg123_length(file);
            }
            duration = double(std::max<off_t>(0, length)) / rate;
        } catch (...) {
            mpg123_delete(file);
            file = nullptr;
            throw;
        }
    }
    ~Mp3Decoder() {
        if (file)
            mpg123_delete(file);
    }
    size_t read(float *out, size_t frames) override {
        size_t got = 0;
        int e = mpg123_read(file, reinterpret_cast<unsigned char *>(out), frames * channels * sizeof(float),
                            &got);
        if (e == MPG123_NEW_FORMAT) {
            long r;
            int c, enc;
            mpg123_getformat(file, &r, &c, &enc);
            if (r != rate || c != channels || enc != MPG123_ENC_FLOAT_32)
                throw std::runtime_error("MP3 format changed");
            return read(out, frames);
        }
        if (e != MPG123_OK && e != MPG123_DONE)
            throw std::runtime_error("MP3 decode failed");
        return got / (channels * sizeof(float));
    }
    void seek(double seconds) override {
        if (mpg123_seek(file, off_t(std::max(0.0, seconds) * rate), SEEK_SET) < 0)
            throw std::runtime_error("MP3 seek failed");
    }
};
class WavDecoder : public Decoder {
    std::ifstream f;
    uint64_t remaining = 0, dataSize = 0;
    std::streamoff dataStart = 0;
    int bits = 0, type = 0;
    std::vector<uint8_t> bytes;
    uint32_t le(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            int b = f.get();
            if (b < 0)
                throw std::runtime_error("Truncated WAV");
            v |= uint32_t(b) << (i * 8);
        }
        return v;
    }

  public:
    explicit WavDecoder(const fs::path &p) : f(p, std::ios::binary) {
        char id[4];
        f.read(id, 4);
        if (std::string(id, 4) != "RIFF")
            throw std::runtime_error("Unsupported WAV container");
        le(4);
        f.read(id, 4);
        if (std::string(id, 4) != "WAVE")
            throw std::runtime_error("Invalid WAV");
        while (f.read(id, 4)) {
            auto n = le(4);
            if (std::string(id, 4) == "fmt ") {
                if (n < 16)
                    throw std::runtime_error("Invalid WAV format");
                type = le(2);
                channels = le(2);
                rate = le(4);
                le(4);
                le(2);
                bits = le(2);
                f.seekg(n - 16 + (n & 1), std::ios::cur);
            } else if (std::string(id, 4) == "data") {
                remaining = dataSize = n;
                dataStart = f.tellg();
                break;
            } else
                f.seekg(n + (n & 1), std::ios::cur);
        }
        if ((type != 1 || bits != 16) && !(type == 3 && bits == 32))
            throw std::runtime_error("Switch WAV supports PCM16 or float32");
        if (channels < 1 || channels > 2 || rate <= 0)
            throw std::runtime_error("Invalid WAV channels/rate");
        duration = double(remaining) / (rate * channels * (bits / 8));
    }
    size_t read(float *out, size_t frames) override {
        const size_t sampleBytes = bits / 8;
        size_t count = std::min<uint64_t>(frames, remaining / (channels * sampleBytes));
        bytes.resize(count * channels * sampleBytes);
        if (!f.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size())))
            throw std::runtime_error("Truncated WAV");
        for (size_t i = 0; i < count * channels; ++i) {
            const uint8_t *b = &bytes[i * sampleBytes];
            if (type == 1)
                out[i] = int16_t(uint16_t(b[0] | b[1] << 8)) / 32768.0f;
            else {
                uint32_t v = uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
                std::memcpy(&out[i], &v, 4);
            }
        }
        remaining -= bytes.size();
        return count;
    }
    void seek(double seconds) override {
        const uint64_t frameBytes = uint64_t(channels) * (bits / 8);
        const uint64_t offset = std::min(uint64_t(std::max(0.0, seconds) * rate), dataSize / frameBytes) * frameBytes;
        f.clear();
        f.seekg(dataStart + std::streamoff(offset));
        remaining = dataSize - offset;
    }
};
std::unique_ptr<Decoder> openDecoder(const fs::path &p) {
    auto ext = lower(p.extension().string());
    if (ext == ".opus")
        return std::make_unique<OpusDecoder>(p);
    if (ext == ".ogg") {
        std::ifstream f(p, std::ios::binary);
        std::array<char, 256> head{};
        f.read(head.data(), head.size());
        if (std::string(head.data(), size_t(f.gcount())).find("OpusHead") != std::string::npos)
            return std::make_unique<OpusDecoder>(p);
        return std::make_unique<VorbisDecoder>(p);
    }
    if (ext == ".mp3")
        return std::make_unique<Mp3Decoder>(p);
    if (ext == ".wav")
        return std::make_unique<WavDecoder>(p);
    throw std::runtime_error("Switch audio format unsupported: " + ext + "; convert FLAC to OGG first");
}
#endif

struct Audio::Stream {
    std::unique_ptr<Decoder> decoder;
    std::array<float, 8192> buffer{};
    size_t available = 0, cursor = 0;
    std::array<float, 2> a{}, b{};
    float gain = 1;
    bool guitar = false;
    double phase = 0;
    bool primed = false, ended = false;
    explicit Stream(const fs::path &p) : Stream(openDecoder(p)) {}
    explicit Stream(std::unique_ptr<Decoder> d) : decoder(std::move(d)) {
        if (decoder->rate < 8000 || decoder->rate > 192000)
            throw std::runtime_error("Unsupported audio sample rate");
    }
    bool frame(std::array<float, 2> &dst) {
        if (cursor == available) {
            available = decoder->read(buffer.data(), buffer.size() / decoder->channels);
            cursor = 0;
            if (!available) {
                dst = {0, 0};
                return false;
            }
        }
        dst[0] = buffer[cursor * decoder->channels];
        dst[1] = buffer[cursor * decoder->channels + (decoder->channels == 2 ? 1 : 0)];
        ++cursor;
        return true;
    }
    // Gain ramps over ~80 ms so ducking the guitar never clicks.
    void mix(float *out, size_t n, float target) {
        const float step = 1.0f / (48000 * .08f);
        if (!primed) {
            ended = !frame(a);
            frame(b);
            primed = true;
        }
        for (size_t i = 0; i < n; ++i) {
            if (ended)
                break;
            gain += std::clamp(target - gain, -step, step);
            out[2 * i] += gain * float(a[0] + (b[0] - a[0]) * phase);
            out[2 * i + 1] += gain * float(a[1] + (b[1] - a[1]) * phase);
            phase += double(decoder->rate) / 48000;
            while (phase >= 1) {
                phase -= 1;
                a = b;
                if (!frame(b)) {
                    if (a[0] == 0 && a[1] == 0)
                        ended = true;
                }
            }
        }
    }
};

// ---------------------------------------------------------------- sound effects
namespace {
constexpr float Pi = 3.14159265f, SampleRate = 48000;
struct Synth {
    std::vector<float> data; // stereo interleaved
    uint32_t seed = 12345;
    explicit Synth(double seconds) : data(size_t(seconds * SampleRate) * 2, 0.0f) {}
    size_t frames() const { return data.size() / 2; }
    float noise() {
        seed = seed * 1664525u + 1013904223u;
        return float(int32_t(seed >> 8) % 2001 - 1000) / 1000.0f;
    }
    void add(size_t frame, float left, float right) {
        if (frame < frames())
            data[frame * 2] += left, data[frame * 2 + 1] += right;
    }
    // Exponential envelope with a short fade-in so nothing clicks.
    static float envelope(float t, float attack, float decay) {
        if (t < 0)
            return 0;
        return std::min(1.0f, t / std::max(attack, .0005f)) * std::exp(-t / decay);
    }
    void tone(double start, double length, float freq, float gain, float decay, float bend = 1, int shape = 0,
              float pan = 0) {
        double phase = 0;
        for (size_t i = size_t(start * SampleRate); i < frames() && i < size_t((start + length) * SampleRate); ++i) {
            const float t = float(i / SampleRate - start);
            const float f = freq * std::pow(bend, t);
            phase += f / SampleRate;
            const float x = float(phase - std::floor(phase));
            float v = shape == 0 ? std::sin(x * 2 * Pi)               // sine
                                 : shape == 1 ? 2 * x - 1             // saw
                                              : x < .5f ? 1.0f : -1.0f; // square
            v *= envelope(t, .004f, decay) * gain;
            add(i, v * (1 - std::max(0.0f, pan)), v * (1 + std::min(0.0f, pan)));
        }
    }
    // Distorted power chord: root, fifth and octave, the 2000s rock staple.
    void chord(double start, float root, float gain, float decay, float drive = 3.5f, float bend = 1) {
        const float ratios[] = {1, 1.4983f, 2, 2.9966f};
        const size_t begin = size_t(start * SampleRate);
        std::vector<double> phase(4, 0);
        for (size_t i = begin; i < frames(); ++i) {
            const float t = float(i / SampleRate - start);
            const float env = envelope(t, .006f, decay);
            if (env < .0005f && t > decay)
                break;
            float v = 0;
            for (int p = 0; p < 4; ++p) {
                const float detune = p == 1 ? 1.004f : p == 3 ? .997f : 1.0f;
                phase[p] += root * ratios[p] * detune * std::pow(bend, t) / SampleRate;
                const float x = float(phase[p] - std::floor(phase[p]));
                v += (2 * x - 1) * (p == 0 ? 1.0f : .7f);
            }
            v = std::tanh(v * drive) * env * gain;
            add(i, v, v * .96f);
        }
    }
    void hiss(double start, double length, float gain, float decay, float cutoff, float sweep = 1) {
        float low = 0, band = 0;
        for (size_t i = size_t(start * SampleRate); i < frames() && i < size_t((start + length) * SampleRate); ++i) {
            const float t = float(i / SampleRate - start);
            const float f = std::clamp(cutoff * std::pow(sweep, t), 20.0f, 16000.0f);
            const float k = std::clamp(2 * Pi * f / SampleRate, 0.0f, 1.0f);
            const float input = noise();
            low += k * band;
            band += k * (input - low - band * .7f);
            const float v = band * envelope(t, .01f, decay) * gain;
            add(i, v, v * .9f);
        }
    }
};
std::vector<float> buildSfx(Sfx sound) {
    switch (sound) {
    case Sfx::Move: { // pick scrape across one string
        Synth s(.09);
        s.tone(0, .08, 520, .16f, .02f, .55f, 2);
        s.hiss(0, .05, .05f, .02f, 3000);
        return std::move(s.data);
    }
    case Sfx::Select: { // power chord stab
        Synth s(.65);
        s.chord(0, 164.81f, .3f, .28f); // E3
        s.hiss(0, .05, .12f, .03f, 5000, .4f);
        return std::move(s.data);
    }
    case Sfx::Back: {
        Synth s(.3);
        s.tone(0, .25, 120, .28f, .09f, .5f);
        s.hiss(0, .06, .07f, .03f, 900);
        return std::move(s.data);
    }
    case Sfx::Toggle: {
        Synth s(.16);
        s.tone(0, .06, 740, .14f, .03f, 1, 2);
        s.tone(.05, .1, 1100, .12f, .04f, 1, 2);
        return std::move(s.data);
    }
    case Sfx::Count: { // drumstick click
        Synth s(.09);
        s.hiss(0, .05, .5f, .012f, 2600, 1.6f);
        s.tone(0, .03, 1800, .1f, .008f, .4f, 2);
        return std::move(s.data);
    }
    case Sfx::Miss: { // dead strings and a dying bend
        Synth s(.36);
        s.chord(0, 146.83f, .2f, .16f, 5.5f, .55f);
        s.tone(0, .3, 233.08f, .12f, .12f, .6f, 1);
        s.hiss(0, .18, .22f, .07f, 1800, .25f);
        return std::move(s.data);
    }
    case Sfx::StarPower: { // riser into a bright chord
        Synth s(1.1);
        s.hiss(0, .42, .3f, .5f, 300, 40.0f);
        s.chord(.36, 220, .3f, .5f, 3.0f);
        s.tone(.36, .6, 880, .1f, .3f);
        s.tone(.36, .6, 1320, .08f, .25f);
        return std::move(s.data);
    }
    case Sfx::Streak: { // cymbal shimmer with a lift
        Synth s(.8);
        s.hiss(0, .6, .22f, .3f, 5200, 1.8f);
        s.tone(0, .5, 660, .1f, .22f, 1.02f);
        s.tone(.06, .5, 990, .08f, .2f, 1.02f);
        return std::move(s.data);
    }
    case Sfx::Fail: { // the set collapses
        Synth s(1.6);
        s.chord(0, 196, .3f, .9f, 4.0f, .52f);
        s.hiss(.05, 1.2, .16f, .7f, 700, .5f);
        s.tone(.1, 1.2, 98, .18f, .6f, .6f, 1);
        return std::move(s.data);
    }
    case Sfx::Win: { // two chords to close the set
        Synth s(1.7);
        s.chord(0, 164.81f, .3f, .32f);
        s.chord(.26, 220, .32f, .95f);
        s.hiss(.26, .3, .12f, .12f, 6000, .5f);
        return std::move(s.data);
    }
    default:
        return {};
    }
}
} // namespace

#ifdef __SWITCH__
// Horizon does not time-slice threads of equal priority on one core, so a
// mixer sharing core 0 with rendering stalls until the main thread yields.
static void moveToSpareCore() {
    // One step above the main thread: background work that shares this core
    // must never delay a buffer, even if lowering its own priority did not take.
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x2B);
    u64 mask = 0;
    if (R_FAILED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)))
        return;
    for (int core = 2; core >= 1; --core)
        if (mask & BIT(core)) {
            svcSetThreadCoreMask(threadGetCurHandle(), core, u32(BIT(core)));
            return;
        }
}
#endif

// ---------------------------------------------------------------- mixer
struct Audio::Engine {
    static constexpr size_t songBlock = 2048, songBuffers = 6, sfxBlock = 1024, sfxBuffers = 4;
    Audio &owner;
    std::thread worker;
    std::atomic<bool> stopping{false}, songActive{false};
    mutable std::mutex mutex; // guards the device state and the playing sounds
    std::array<std::vector<float>, size_t(Sfx::Count_)> bank;
    struct Playing {
        const std::vector<float> *buffer;
        size_t frame;
        float gain;
    };
    std::vector<Playing> playing;
    uint64_t songSubmitted = 0;
    bool sfxReady = false; // the song still plays if the effects channel is unavailable

#ifdef __SWITCH__
    static constexpr size_t songBytes = songBlock * 2 * sizeof(int16_t), sfxBytes = sfxBlock * 2 * sizeof(int16_t);
    static constexpr size_t poolSize = (songBytes * songBuffers + sfxBytes * sfxBuffers + 0xFFF) & ~size_t(0xFFF);
    AudioDriver driver{};
    std::array<AudioDriverWaveBuf, songBuffers> songWave{};
    std::array<AudioDriverWaveBuf, sfxBuffers> sfxWave{};
    void *pool = nullptr;
    bool renderer = false, created = false;
    uint32_t songBase = 0;
#else
    SDL_AudioDeviceID songDevice = 0, sfxDevice = 0;
    SDL_AudioSpec songFormat{}, sfxFormat{};
#endif

    explicit Engine(Audio &owner) : owner(owner) {
        for (size_t i = 0; i < bank.size(); ++i)
            bank[i] = buildSfx(Sfx(i));
        playing.reserve(8);
        open();
        worker = std::thread(&Engine::pump, this);
    }
    ~Engine() {
        stopping = true;
        if (worker.joinable())
            worker.join();
        close();
    }

#ifdef __SWITCH__
    void voiceSetup(int id) {
        if (!audrvVoiceInit(&driver, id, 2, PcmFormat_Int16, 48000))
            throw std::runtime_error("Audio voice creation failed");
        audrvVoiceSetDestinationMix(&driver, id, AUDREN_FINAL_MIX_ID);
        audrvVoiceSetMixFactor(&driver, id, 1.0f, 0, 0);
        audrvVoiceSetMixFactor(&driver, id, 0.0f, 0, 1);
        audrvVoiceSetMixFactor(&driver, id, 0.0f, 1, 0);
        audrvVoiceSetMixFactor(&driver, id, 1.0f, 1, 1);
    }
    void open() {
        try {
            // Matches SDL's working Switch backend. Voices also size the channel
            // table (a stereo voice needs two) and final mix needs two buffers.
            static const AudioRendererConfig config = {AudioRendererOutputRate_48kHz, 24, 0, 1, 1, 2};
            if (R_FAILED(audrenInitialize(&config)))
                throw std::runtime_error("Audio renderer initialization failed");
            renderer = true;
            if (R_FAILED(audrvCreate(&driver, &config, 2)))
                throw std::runtime_error("Audio driver creation failed");
            created = true;
            // Both channels share one memory pool and are addressed by sample
            // offsets, as SDL's backend does.
            pool = memalign(0x1000, poolSize);
            if (!pool)
                throw std::runtime_error("Audio buffer allocation failed");
            std::memset(pool, 0, poolSize);
            int id = audrvMemPoolAdd(&driver, pool, poolSize);
            if (id < 0 || !audrvMemPoolAttach(&driver, id))
                throw std::runtime_error("Audio memory pool failed");
            for (size_t i = 0; i < songBuffers; ++i) {
                songWave[i].data_raw = pool;
                songWave[i].size = poolSize;
                songWave[i].start_sample_offset = int32_t(i * songBlock);
                songWave[i].end_sample_offset = int32_t((i + 1) * songBlock);
            }
            for (size_t i = 0; i < sfxBuffers; ++i) {
                sfxWave[i].data_raw = pool;
                sfxWave[i].size = poolSize;
                sfxWave[i].start_sample_offset = int32_t(songBlock * songBuffers + i * sfxBlock);
                sfxWave[i].end_sample_offset = int32_t(songBlock * songBuffers + (i + 1) * sfxBlock);
            }
            static const u8 sink[2] = {0, 1};
            if (audrvDeviceSinkAdd(&driver, AUDREN_DEFAULT_DEVICE_NAME, 2, sink) < 0)
                throw std::runtime_error("Audio sink creation failed");
            if (R_FAILED(audrenStartAudioRenderer()))
                throw std::runtime_error("Audio renderer start failed");
            voiceSetup(0);
            audrvVoiceSetPaused(&driver, 0, true);
            try {
                voiceSetup(1);
                audrvVoiceStart(&driver, 1);
                sfxReady = true;
            } catch (const std::exception &) {
                sfxReady = false;
            }
            if (R_FAILED(audrvUpdate(&driver)))
                throw std::runtime_error("Audio driver update failed");
        } catch (...) {
            close();
            throw;
        }
    }
    void close() {
        if (created)
            audrvClose(&driver);
        if (renderer)
            audrenExit();
        created = renderer = false;
        std::free(pool);
        pool = nullptr;
    }
    // Drops anything still queued for the song and restarts its clock.
    void resetSong() {
        std::lock_guard<std::mutex> lock(mutex);
        voiceSetup(0);
        audrvVoiceSetPaused(&driver, 0, true);
        for (auto &b : songWave)
            b.state = AudioDriverWaveBufState_Free;
        audrvUpdate(&driver);
        songBase = audrvVoiceGetPlayedSampleCount(&driver, 0);
        songSubmitted = 0;
    }
    void setPaused(bool value) {
        std::lock_guard<std::mutex> lock(mutex);
        audrvVoiceSetPaused(&driver, 0, value);
        audrvUpdate(&driver);
    }
    double songSeconds() {
        std::lock_guard<std::mutex> lock(mutex);
        const uint32_t played = audrvVoiceGetPlayedSampleCount(&driver, 0);
        return double(played - songBase) / 48000;
    }
    static void toPcm(const float *in, int16_t *out, size_t samples) {
        for (size_t i = 0; i < samples; ++i)
            out[i] = in[i] <= -1.0f ? INT16_MIN : int16_t(std::lrint(std::clamp(in[i], -1.0f, 1.0f) * INT16_MAX));
    }
    void pump() {
        try {
            moveToSpareCore();
            std::vector<float> mix(songBlock * 2);
            while (!stopping) {
                AudioDriverWaveBuf *song = nullptr, *sfx = nullptr;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (R_FAILED(audrvUpdate(&driver)))
                        throw std::runtime_error("Audio driver update failed");
                    auto freeOf = [](auto &set) -> AudioDriverWaveBuf * {
                        for (auto &b : set)
                            if (b.state == AudioDriverWaveBufState_Free || b.state == AudioDriverWaveBufState_Done)
                                return &b;
                        return nullptr;
                    };
                    if (songActive)
                        song = freeOf(songWave);
                    if (sfxReady)
                        sfx = freeOf(sfxWave);
                }
                if (song) {
                    // Free and completed buffers are not referenced by the renderer.
                    mix.assign(songBlock * 2, 0.0f);
                    owner.renderSong(mix.data(), songBlock);
                    auto *pcm = song->data_pcm16 + size_t(song->start_sample_offset) * 2;
                    toPcm(mix.data(), pcm, songBlock * 2);
                    armDCacheFlush(pcm, songBytes);
                    std::lock_guard<std::mutex> lock(mutex);
                    if (!audrvVoiceAddWaveBuf(&driver, 0, song))
                        throw std::runtime_error("Audio buffer submission failed");
                    songSubmitted += songBlock;
                }
                if (sfx) {
                    mix.assign(sfxBlock * 2, 0.0f);
                    renderSfx(mix.data(), sfxBlock);
                    auto *pcm = sfx->data_pcm16 + size_t(sfx->start_sample_offset) * 2;
                    toPcm(mix.data(), pcm, sfxBlock * 2);
                    armDCacheFlush(pcm, sfxBytes);
                    std::lock_guard<std::mutex> lock(mutex);
                    if (!audrvVoiceAddWaveBuf(&driver, 1, sfx))
                        throw std::runtime_error("Audio buffer submission failed");
                }
                if (!song && !sfx)
                    audrenWaitFrame();
            }
        } catch (const std::exception &e) {
            owner.fail(e.what());
        }
    }
#else
    SDL_AudioDeviceID openDevice(SDL_AudioSpec &format, int samples) {
        SDL_AudioSpec want{};
        want.freq = 48000;
        want.channels = 2;
        want.format = AUDIO_F32SYS;
        want.samples = uint16_t(samples);
        SDL_AudioDeviceID id = SDL_OpenAudioDevice(nullptr, 0, &want, &format, 0);
        if (!id)
            throw std::runtime_error(SDL_GetError());
        if (format.freq != want.freq || format.channels != want.channels || format.format != want.format) {
            SDL_CloseAudioDevice(id);
            throw std::runtime_error("Unexpected SDL audio format");
        }
        return id;
    }
    void open() {
        songDevice = openDevice(songFormat, 512);
        try {
            // A second device is a nicety: without it the game simply has no
            // interface sounds (some SDL drivers only allow one).
            sfxDevice = openDevice(sfxFormat, 512);
            SDL_PauseAudioDevice(sfxDevice, 0); // interface sounds are always live
            sfxReady = true;
        } catch (const std::exception &) {
            sfxDevice = 0;
            sfxReady = false;
        }
    }
    void close() {
        if (songDevice)
            SDL_CloseAudioDevice(songDevice);
        if (sfxDevice)
            SDL_CloseAudioDevice(sfxDevice);
        songDevice = sfxDevice = 0;
    }
    size_t queued(SDL_AudioDeviceID device) const { return SDL_GetQueuedAudioSize(device) / (2 * sizeof(float)); }
    void resetSong() {
        std::lock_guard<std::mutex> lock(mutex);
        SDL_ClearQueuedAudio(songDevice);
        songSubmitted = 0;
    }
    void setPaused(bool value) { SDL_PauseAudioDevice(songDevice, value ? 1 : 0); }
    double songSeconds() const {
        std::lock_guard<std::mutex> lock(mutex);
        const uint64_t pending = queued(songDevice);
        const uint64_t played = songSubmitted > pending ? songSubmitted - pending : 0;
        return double(played > songFormat.samples ? played - songFormat.samples : 0) / songFormat.freq;
    }
    void pump() {
        try {
            std::vector<float> mix(songBlock * 2);
            while (!stopping) {
                bool worked = false;
                if (songActive && queued(songDevice) < 12000) {
                    mix.assign(songBlock * 2, 0.0f);
                    owner.renderSong(mix.data(), songBlock);
                    std::lock_guard<std::mutex> lock(mutex);
                    if (SDL_QueueAudio(songDevice, mix.data(), songBlock * 2 * sizeof(float)))
                        throw std::runtime_error(SDL_GetError());
                    songSubmitted += songBlock;
                    worked = true;
                }
                if (sfxReady && queued(sfxDevice) < 3000) {
                    mix.assign(sfxBlock * 2, 0.0f);
                    renderSfx(mix.data(), sfxBlock);
                    std::lock_guard<std::mutex> lock(mutex);
                    if (SDL_QueueAudio(sfxDevice, mix.data(), sfxBlock * 2 * sizeof(float)))
                        throw std::runtime_error(SDL_GetError());
                    worked = true;
                }
                if (!worked)
                    SDL_Delay(2);
            }
        } catch (const std::exception &e) {
            owner.fail(e.what());
        }
    }
#endif
    void play(Sfx sound, float gain) {
        const auto &buffer = bank[size_t(sound)];
        if (!sfxReady || buffer.empty())
            return;
        std::lock_guard<std::mutex> lock(mutex);
        if (playing.size() >= 8)
            playing.erase(playing.begin());
        playing.push_back({&buffer, 0, gain});
    }
    void renderSfx(float *out, size_t frames) {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto &voice : playing) {
            const size_t available = std::min(frames, voice.buffer->size() / 2 - voice.frame);
            for (size_t i = 0; i < available; ++i) {
                out[i * 2] += (*voice.buffer)[(voice.frame + i) * 2] * voice.gain;
                out[i * 2 + 1] += (*voice.buffer)[(voice.frame + i) * 2 + 1] * voice.gain;
            }
            voice.frame += available;
        }
        playing.erase(std::remove_if(playing.begin(), playing.end(),
                                     [](const Playing &v) { return v.frame * 2 >= v.buffer->size(); }),
                      playing.end());
        for (size_t i = 0; i < frames * 2; ++i)
            out[i] = std::clamp(out[i], -1.0f, 1.0f);
    }
};

Audio::Audio() = default;
Audio::~Audio() {
    stop();
    engine.reset();
}
void Audio::start() {
    if (!engine)
        engine = std::make_unique<Engine>(*this);
}
void Audio::fail(const std::string &message) {
    std::lock_guard<std::mutex> lock(errorMutex);
    failure = message;
    failed = true;
}
void Audio::playSfx(Sfx sound, float gain) {
    if (engine && sfxVolume > 0)
        engine->play(sound, gain * sfxVolume);
}
void Audio::stop() {
    if (engine) {
        engine->songActive = false;
        engine->setPaused(true);
        engine->resetSong();
    }
    std::lock_guard<std::mutex> lock(streamMutex);
    streams.clear();
    generated = 0;
    paused = true;
}
void Audio::load(const Song &song) {
    songGain = 1;
    loadStreams(song, std::max(2.0, 2.0 - song.offset), 0, true);
}
struct Audio::Prepared {
    std::vector<std::unique_ptr<Stream>> streams;
    double duration = 0;
};
std::shared_ptr<Audio::Prepared> Audio::prepare(std::vector<fs::path> stems, double songDuration, double startSeconds) {
    auto p = std::make_shared<Prepared>();
    p->duration = songDuration + 2;
    for (auto &path : stems) {
        auto stream = std::make_unique<Stream>(path);
        if (startSeconds > 0)
            stream->decoder->seek(startSeconds);
        p->duration = std::max(p->duration, stream->decoder->duration);
        p->streams.push_back(std::move(stream));
    }
    return p;
}
void Audio::playPrepared(std::shared_ptr<Prepared> prepared) {
    start();
    stop();
    failed = false;
    {
        std::lock_guard<std::mutex> lock(errorMutex);
        failure.clear();
    }
    leadIn = 0;
    totalDuration = prepared->duration;
    guitarStem = false, duckGuitar = false, silent = false;
    for (auto &stream : prepared->streams)
        stream->gain = songGain * musicVolume; // start at the level it will ramp to
    {
        std::lock_guard<std::mutex> lock(streamMutex);
        streams = std::move(prepared->streams);
        generated = 0;
    }
    engine->songActive = true;
    pause(false);
}
void Audio::preview(const Song &song, double startSeconds) {
    playPrepared(prepare(song.audio, song.duration + song.offset, std::max(0.0, startSeconds)));
}
void Audio::loadStreams(const Song &song, double lead, double startSeconds, bool prime) {
    start();
    stop();
    failed = false;
    {
        std::lock_guard<std::mutex> lock(errorMutex);
        failure.clear();
    }
    leadIn = lead;
    totalDuration = song.duration + song.offset + 2;
    std::vector<std::unique_ptr<Stream>> loaded;
    bool anyGuitar = false;
    for (auto &p : song.audio) {
        auto stream = std::make_unique<Stream>(p);
        if (startSeconds > 0)
            stream->decoder->seek(startSeconds);
        stream->gain = songGain * musicVolume; // start at the level it will ramp to

        totalDuration = std::max(totalDuration, stream->decoder->duration);
        // Clone Hero stems are named by instrument, so the lead guitar can be
        // muted on its own when the rock meter bottoms out.
        stream->guitar = lower(p.filename().string()).rfind("guitar", 0) == 0;
        anyGuitar = anyGuitar || stream->guitar;
        loaded.push_back(std::move(stream));
    }
    // Never mute a single-stem song: that would silence the whole track.
    guitarStem = anyGuitar && loaded.size() > 1;
    duckGuitar = false;
    silent = false;
    {
        std::lock_guard<std::mutex> lock(streamMutex);
        streams = std::move(loaded);
        generated = 0;
    }
    engine->songActive = true;
    // Prime before playback so slow storage cannot consume the initial silence early.
    for (int i = 0; prime && i < 1000; ++i) {
        {
            std::lock_guard<std::mutex> lock(engine->mutex);
            if (engine->songSubmitted >= 4096)
                break;
        }
        if (failed)
            throw std::runtime_error(error());
        SDL_Delay(1);
    }
}
namespace {
// An endless metronome: a short woodblock-ish tick on every beat, accented on
// the one, starting exactly on sample zero so beat k is at k * 60 / bpm.
class ClickDecoder : public Decoder {
    uint64_t frame = 0;
    const double beat;

  public:
    explicit ClickDecoder(double bpm) : beat(60.0 / bpm) {
        rate = 48000, channels = 2, duration = 1e9;
    }
    size_t read(float *samples, size_t frames) override {
        for (size_t i = 0; i < frames; ++i, ++frame) {
            const double t = double(frame) / rate;
            const auto k = uint64_t(t / beat);
            const double age = t - double(k) * beat;
            float v = 0;
            if (age < .04) {
                const double pitch = k % 4 ? 1400 : 2100;
                v = float(std::sin(2 * 3.14159265358979 * pitch * age) * std::exp(-age * 90) * .7);
            }
            samples[2 * i] = samples[2 * i + 1] = v;
        }
        return frames;
    }
};
} // namespace
void Audio::loadClicks(double bpm) {
    start();
    stop();
    failed = false;
    {
        std::lock_guard<std::mutex> lock(errorMutex);
        failure.clear();
    }
    leadIn = 1;
    totalDuration = 1e9;
    songGain = 1;
    guitarStem = false;
    duckGuitar = false;
    silent = false;
    {
        std::lock_guard<std::mutex> lock(streamMutex);
        streams.push_back(std::make_unique<Stream>(std::make_unique<ClickDecoder>(bpm)));
        generated = 0;
    }
    engine->songActive = true;
    pause(false);
}
void Audio::pause(bool v) {
    paused = v;
    if (engine)
        engine->setPaused(v);
}
double Audio::position() const {
    if (!engine || !engine->songActive)
        return -leadIn;
    return engine->songSeconds() - leadIn;
}
std::string Audio::error() const {
    if (!failed)
        return {};
    std::lock_guard<std::mutex> lock(errorMutex);
    return failure;
}
bool Audio::renderSong(float *out, size_t frames) {
    std::lock_guard<std::mutex> lock(streamMutex);
    if (streams.empty())
        return false;
    const auto leadFrames = uint64_t(std::llround(leadIn * 48000));
    const size_t silence = generated < leadFrames ? size_t(std::min<uint64_t>(frames, leadFrames - generated)) : 0;
    const float level = songGain * musicVolume;
    if (silence < frames)
        for (auto &stream : streams)
            stream->mix(out + silence * 2, frames - silence, silent || (stream->guitar && duckGuitar) ? 0.0f : level);
    // Preserve stem balance; clamp only overshoots of full-scale mixes.
    for (size_t i = 0; i < frames * 2; ++i)
        out[i] = std::isfinite(out[i]) ? std::clamp(out[i], -1.0f, 1.0f) : 0.0f;
    generated += frames;
    return true;
}
} // namespace fret
