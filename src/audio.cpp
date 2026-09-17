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
#include <mpg123.h>
#include <opus/opusfile.h>
#include <vorbis/vorbisfile.h>
#endif

namespace fret {
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
        auto n = sf_readf_float(file, out, sf_count_t(frames));
        if (n < 0 || sf_error(file))
            throw std::runtime_error("Audio decode error");
        return size_t(n);
    }
};
std::unique_ptr<Decoder> openDecoder(const fs::path &p) { return std::make_unique<FileDecoder>(p); }
#else
class VorbisDecoder : public Decoder {
    OggVorbis_File file{};
    bool opened = false;

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
};
class OpusDecoder : public Decoder {
    OggOpusFile *file = nullptr;

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
            if (n < 0)
                throw std::runtime_error("Opus decode error");
            got += n;
        }
        return got;
    }
};
class Mp3Decoder : public Decoder {
    mpg123_handle *file = nullptr;

  public:
    explicit Mp3Decoder(const fs::path &p) {
        static const int init = mpg123_init();
        if (init != MPG123_OK)
            throw std::runtime_error("MP3 init failed");
        int e = 0;
        file = mpg123_new(nullptr, &e);
        if (!file)
            throw std::runtime_error("MP3 allocation failed");
        try {
            if (mpg123_open(file, p.string().c_str()) != MPG123_OK)
                throw std::runtime_error("Cannot open MP3");
            long r;
            int encoding;
            if (mpg123_getformat(file, &r, &channels, &encoding) != MPG123_OK)
                throw std::runtime_error("Cannot read MP3 format");
            rate = int(r);
            if (channels < 1 || channels > 2)
                throw std::runtime_error("Only mono/stereo MP3 supported");
            mpg123_format_none(file);
            if (mpg123_format(file, r, channels, MPG123_ENC_FLOAT_32) != MPG123_OK)
                throw std::runtime_error("MP3 float output unavailable");
            mpg123_scan(file);
            duration = double(mpg123_length(file)) / rate;
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
};
class WavDecoder : public Decoder {
    std::ifstream f;
    uint64_t remaining = 0;
    int bits = 0, type = 0;
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
                remaining = n;
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
        size_t count = std::min<uint64_t>(frames, remaining / (channels * (bits / 8)));
        for (size_t i = 0; i < count * channels; ++i) {
            if (type == 1)
                out[i] = int16_t(le(2)) / 32768.0f;
            else {
                uint32_t v = le(4);
                std::memcpy(&out[i], &v, 4);
            }
        }
        remaining -= count * channels * (bits / 8);
        return count;
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
    double phase = 0;
    bool primed = false, ended = false;
    explicit Stream(const fs::path &p) : decoder(openDecoder(p)) {
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
    void mix(float *out, size_t n) {
        if (!primed) {
            ended = !frame(a);
            frame(b);
            primed = true;
        }
        for (size_t i = 0; i < n; ++i) {
            if (ended)
                break;
            out[2 * i] += float(a[0] + (b[0] - a[0]) * phase);
            out[2 * i + 1] += float(a[1] + (b[1] - a[1]) * phase);
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
Audio::Audio() = default;
Audio::~Audio() { stop(); }
void Audio::stop() {
    stopping = true;
    if (worker.joinable())
        worker.join();
    if (device) {
        SDL_CloseAudioDevice(device);
        device = 0;
    }
    streams.clear();
    submitted = 0;
}
void Audio::load(const Song &song) {
    stop();
    failed = false;
    failure.clear();
    stopping = false;
    paused = true;
    leadIn = std::max(2.0, 2.0 - song.offset);
    totalDuration = song.duration + song.offset + 2;
    for (auto &p : song.audio) {
        auto stream = std::make_unique<Stream>(p);
        totalDuration = std::max(totalDuration, stream->decoder->duration);
        streams.push_back(std::move(stream));
    }
    SDL_AudioSpec want{};
    want.freq = 48000;
    want.channels = 2;
    want.format = AUDIO_F32SYS;
    want.samples = 512;
    device = SDL_OpenAudioDevice(nullptr, 0, &want, &format, 0);
    if (!device)
        throw std::runtime_error(SDL_GetError());
    worker = std::thread(&Audio::pump, this);
    // Prime before playback so slow storage cannot consume the initial silence early.
    for (int i = 0; i < 1000; ++i) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (submitted >= 4096)
                break;
        }
        if (failed)
            throw std::runtime_error(error());
        SDL_Delay(1);
    }
}
void Audio::pause(bool v) {
    paused = v;
    if (device)
        SDL_PauseAudioDevice(device, v ? 1 : 0);
}
double Audio::position() const {
    if (!device)
        return -leadIn;
    std::lock_guard<std::mutex> lock(queueMutex);
    auto pending = SDL_GetQueuedAudioSize(device) / (2 * sizeof(float));
    uint64_t played = submitted > pending ? submitted - pending : 0;
    double t = double(played > format.samples ? played - format.samples : 0) / 48000;
    return t - leadIn;
}
std::string Audio::error() const {
    if (!failed)
        return {};
    std::lock_guard<std::mutex> lock(errorMutex);
    return failure;
}
void Audio::pump() {
    try {
        const size_t block = 2048;
        std::array<float, block * 2> mix{};
        uint64_t generated = 0;
        const auto leadFrames = uint64_t(std::llround(leadIn * 48000));
        while (!stopping) {
            if (SDL_GetQueuedAudioSize(device) > 48000 * 2 * sizeof(float) / 4) {
                SDL_Delay(2);
                continue;
            }
            mix.fill(0);
            size_t silence =
                generated < leadFrames ? size_t(std::min<uint64_t>(block, leadFrames - generated)) : 0;
            if (silence < block)
                for (auto &stream : streams)
                    stream->mix(mix.data() + silence * 2, block - silence);
            // Preserve stem balance; clamp only overshoots of full-scale mixes.
            for (auto &v : mix)
                v = std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f;
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                if (SDL_QueueAudio(device, mix.data(), sizeof(mix)))
                    throw std::runtime_error(SDL_GetError());
                submitted += block;
            }
            generated += block;
        }
    } catch (const std::exception &e) {
        std::lock_guard<std::mutex> lock(errorMutex);
        failure = e.what();
        failed = true;
    }
}
} // namespace fret
