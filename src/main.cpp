#include "audio.hpp"
#include "background.hpp"
#include "calibration.hpp"
#include "clock.hpp"
#include "downloader.hpp"
#include "game.hpp"
#include "guitar_input.hpp"
#include "lang.hpp"
#include "look.hpp"
#include "scores.hpp"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <cstdio>
#include <sstream>
#include <vector>
#ifndef SWITCH_HERO_VERSION
#define SWITCH_HERO_VERSION "dev"
#endif
#ifdef __SWITCH__
#include <switch.h>
#endif

using namespace fret;
using namespace fret::look;
using fret::lang::tr;
namespace {
using look::Align;
using look::Face;
using look::Style;

Style stencil(float size, SDL_Color top = ink::chrome, SDL_Color bottom = ink::steel) {
    Style s;
    s.face = Face::Stencil, s.size = size, s.top = top, s.bottom = bottom;
    s.outline = {0, 0, 0, 220}, s.outlineWidth = std::max(2.0f, size / 22), s.shear = .1f;
    return s;
}
Style marker(float size, SDL_Color c = ink::marker, float angle = 0) {
    Style s;
    s.face = Face::Marker, s.size = size, s.top = s.bottom = c, s.angle = angle, s.wobble = 1.2f;
    return s;
}
// The largest size up to `size` at which `s` fits in `width`, for numbers that
// keep growing, like a score.
float fitSize(const std::string &s, const Style &style, float width) {
    const float w = measure(s, style.face, style.size, style.tracking) + style.outlineWidth * 2;
    return w > width ? style.size * width / w : style.size;
}
Style body(float size, SDL_Color c = ink::white) {
    Style s;
    s.face = Face::Body, s.size = size, s.top = s.bottom = c;
    return s;
}
// 1234567 -> "1,234,567" ("1.234.567" in Portuguese)
std::string grouped(int n) {
    std::string digits = std::to_string(std::abs(n)), out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0)
            out += lang::portuguese() ? '.' : ',';
        out += digits[i];
    }
    return (n < 0 ? "-" : "") + out;
}
std::string timeText(double t) {
    int sec = std::max(0, int(t));
    return std::to_string(sec / 60) + ":" + (sec % 60 < 10 ? "0" : "") + std::to_string(sec % 60);
}
uint64_t bit(int b) { return uint64_t(1) << b; }
bool fretBindingAllowed(int b) {
    return b == 0 || b == 2 || b == 7 || b == 8 || b == 9 || b == 10 || b == 13 || b == 14 || b == 32 ||
           b == 33;
}
struct Settings {
    std::array<int, 5> bindings = {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 32,
                                   SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 33, SDL_CONTROLLER_BUTTON_A};
    // A shorter lookahead puts fewer notes on the board at once; 1.8s packed
    // dense charts in far tighter than they could be read.
    double audioMs = 0, videoMs = 0, travel = 1.2;
    bool gamepad = true;
    bool wiiGuitar = false;
    bool noFail = false;
    bool lefty = false;
    bool timingOverlay = false; // frame timing read-out, for checking pacing on the console
    int sortMode = 0;           // song list order: 0 title, 1 artist, 2 best stars
    int hitWindow = 1;          // 0 strict, 1 normal, 2 lenient
    // Volumes run 0 to 10.
    int musicVolume = 10, sfxVolume = 8;
    // The last part and difficulty played, so the next song opens on them, and
    // the folder of the last song, so the list opens where it was left.
    int part = 0, difficulty = 3;
    std::string lastSong;
    // A lang::Language, or -1 until the player picks one on first start.
    int language = -1;
    void load(const fs::path &path) {
        std::ifstream f(path);
        std::string line;
        // Clamped as a double first: converting an out-of-range double to int
        // is undefined, and a damaged file must not be able to trigger that.
        auto whole = [](double v, int lo, int hi) { return int(std::clamp(v, double(lo), double(hi))); };
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back(); // a file edited on Windows
            std::istringstream in(line);
            std::string k;
            if (!(in >> k))
                continue;
            if (k == "last_song") {
                // A folder name, spaces and all: the rest of the line.
                std::getline(in >> std::ws, lastSong);
                continue;
            }
            double v;
            if (!(in >> v) || !std::isfinite(v))
                continue;
            if (k == "language")
                language = whole(v, 0, lang::Count - 1);
            if (k == "lefty")
                lefty = v != 0;
            if (k == "timing_overlay")
                timingOverlay = v != 0;
            if (k == "sort_mode")
                sortMode = whole(v, 0, 2);
            if (k == "hit_window")
                hitWindow = whole(v, 0, 2);
            if (k == "music_volume")
                musicVolume = whole(v, 0, 10);
            if (k == "sfx_volume")
                sfxVolume = whole(v, 0, 10);
            if (k == "audio_ms")
                audioMs = std::clamp(v, -500.0, 500.0);
            if (k == "video_ms")
                videoMs = std::clamp(v, -500.0, 500.0);
            if (k == "travel")
                travel = std::clamp(v, 0.75, 3.0);
            if (k == "wii_guitar")
                wiiGuitar = v != 0;
            if (k == "gamepad")
                gamepad = v != 0;
            if (k == "no_fail")
                noFail = v != 0;
            if (k == "part")
                part = whole(v, 0, 4);
            if (k == "difficulty")
                difficulty = whole(v, 0, 3);
            for (int i = 0; i < 5; ++i)
                if (k == "fret" + std::to_string(i) && v >= 0 && v <= 33 && int(v) == v &&
                    fretBindingAllowed(int(v)))
                    bindings[i] = int(v);
        }
        auto sorted = bindings;
        std::sort(sorted.begin(), sorted.end());
        if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
            bindings = Settings{}.bindings;
    }
    void save(const fs::path &path) {
        std::ofstream f(path);
        if (!f)
            throw std::runtime_error("Cannot save settings: " + path.string());
        f << "audio_ms " << audioMs << "\nvideo_ms " << videoMs << "\ntravel " << travel << "\ngamepad "
          << gamepad << "\nwii_guitar " << wiiGuitar << "\nno_fail " << noFail << "\npart " << part
          << "\ndifficulty " << difficulty << "\nlefty " << lefty << "\ntiming_overlay " << timingOverlay
          << "\nsort_mode " << sortMode << "\nhit_window " << hitWindow << "\nmusic_volume " << musicVolume
          << "\nsfx_volume " << sfxVolume << '\n';
        if (language >= 0)
            f << "language " << language << '\n';
        if (!lastSong.empty())
            f << "last_song " << lastSong << '\n';
        for (int i = 0; i < 5; ++i)
            f << "fret" << i << ' ' << bindings[i] << '\n';
    }
};
std::string bindingName(int n) {
#ifdef __SWITCH__
    if (n == 0)
        return "B";
    if (n == 1)
        return "A";
    if (n == 2)
        return "Y";
    if (n == 3)
        return "X";
    if (n == 9)
        return "L";
    if (n == 10)
        return "R";
    if (n == 32)
        return "ZL";
    if (n == 33)
        return "ZR";
#else
    if (n == 0)
        return "SOUTH";
    if (n == 1)
        return "EAST";
    if (n == 2)
        return "WEST";
    if (n == 3)
        return "NORTH";
    if (n == 9)
        return "LB";
    if (n == 10)
        return "RB";
    if (n == 32)
        return "LT";
    if (n == 33)
        return "RT";
#endif
    return "BTN " + std::to_string(n);
}
class Controller {
    SDL_GameController *pad = nullptr;
#ifdef __SWITCH__
    // Buttons are sampled on their own thread about once a millisecond, so a
    // press is timed when it arrives rather than rounded to the next frame
    // (up to 16.7 ms late). The controller's own report rate still applies.
    struct Snapshot {
        uint64_t standard = 0, guitar = 0;
        bool standardOn = false, guitarOn = false;
        uint32_t standardStyle = 0, guitarStyle = 0;
        // Whammy sources: the further-pushed stick of the normal controller,
        // and the guitar's bar, which the MissionControl patch sends as right
        // stick X. Both 0 to 1.
        float standardStick = 0, guitarWhammy = 0;
    };
    PadState nx{}, nxGuitar{};
    std::thread poller;
    std::atomic<bool> stopping{false};
    std::mutex mutex;
    Snapshot snap;
    double pressAt = -1; // earliest press not yet handed to a frame
    bool guitarInput = false, guitarConnected = false;
    static double seconds() { return double(SDL_GetPerformanceCounter()) / SDL_GetPerformanceFrequency(); }
    void poll() {
        // The buttons the game reads; stick directions are left out so a
        // drifting stick cannot stamp a press that never happened.
        const uint64_t watched = HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y |
                                 HidNpadButton_L | HidNpadButton_R | HidNpadButton_ZL | HidNpadButton_ZR |
                                 HidNpadButton_Plus | HidNpadButton_Minus | HidNpadButton_Up | HidNpadButton_Down |
                                 HidNpadButton_Left | HidNpadButton_Right | HidNpadButton_StickL | HidNpadButton_StickR;
        // Off the main thread's core; the audio mixer prefers core 2, so try 1 first.
        u64 cores = 0;
        if (R_SUCCEEDED(svcGetInfo(&cores, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)))
            for (int core : {1, 2})
                if (cores & BIT(core)) {
                    svcSetThreadCoreMask(threadGetCurHandle(), core, u32(BIT(core)));
                    break;
                }
        uint64_t previous = 0;
        while (!stopping) {
            padUpdate(&nx);
            padUpdate(&nxGuitar);
            Snapshot s;
            s.standard = padGetButtons(&nx), s.guitar = padGetButtons(&nxGuitar);
            s.standardOn = padIsConnected(&nx), s.guitarOn = padIsConnected(&nxGuitar);
            s.standardStyle = padGetStyleSet(&nx), s.guitarStyle = padGetStyleSet(&nxGuitar);
            auto deflection = [](HidAnalogStickState st) {
                return std::min(1.0f, std::hypot(float(st.x), float(st.y)) / JOYSTICK_MAX);
            };
            s.standardStick = std::max(deflection(padGetStickPos(&nx, 0)), deflection(padGetStickPos(&nx, 1)));
            s.guitarWhammy = std::clamp(float(padGetStickPos(&nxGuitar, 1).x) / JOYSTICK_MAX, 0.0f, 1.0f);
            const uint64_t down = (s.standard | s.guitar) & watched;
            const bool rising = (down & ~previous) != 0;
            previous = down;
            const double t = seconds();
            {
                std::lock_guard<std::mutex> lock(mutex);
                snap = s;
                if (rising && pressAt < 0)
                    pressAt = t;
            }
            svcSleepThread(1'000'000);
        }
    }
#endif
  public:
    Controller() {
#ifdef __SWITCH__
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&nx);
        padInitialize(&nxGuitar, HidNpadIdType_No1);
        poller = std::thread([this] { poll(); });
#else
        connect();
#endif
    }
    ~Controller() { close(); }
    // Released before SDL_Quit so no SDL object outlives the library.
    void close() {
#ifdef __SWITCH__
        stopping = true;
        if (poller.joinable())
            poller.join();
#endif
        if (pad)
            SDL_GameControllerClose(pad);
        pad = nullptr;
    }
    void connect() {
#ifndef __SWITCH__
        if (pad && SDL_GameControllerGetAttached(pad))
            return;
        if (pad) {
            SDL_GameControllerClose(pad);
            pad = nullptr;
        }
        for (int i = 0; i < SDL_NumJoysticks(); ++i)
            if (SDL_IsGameController(i)) {
                pad = SDL_GameControllerOpen(i);
                break;
            }
#endif
    }
    // Live view of both input sources for the controller test panel.
    struct Probe {
        bool standard = false, guitar = false;
        uint32_t standardStyle = 0, guitarStyle = 0;
        uint64_t standardRaw = 0, guitarRaw = 0;
        std::string name;
    };
    Probe probe() {
        Probe p;
#ifdef __SWITCH__
        Snapshot s;
        {
            std::lock_guard<std::mutex> lock(mutex);
            s = snap;
        }
        p.standard = s.standardOn, p.guitar = s.guitarOn;
        p.standardStyle = s.standardStyle, p.guitarStyle = s.guitarStyle;
        p.standardRaw = s.standard, p.guitarRaw = s.guitar;
        p.name = p.guitar ? "player one npad" : "no player-one controller";
#else
        connect();
        p.standard = pad != nullptr;
        p.name = pad ? SDL_GameControllerName(pad) : tr("no controller");
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
            if (pad && SDL_GameControllerGetButton(pad, SDL_GameControllerButton(b)))
                p.standardRaw |= bit(b);
#endif
        return p;
    }
    bool connected() {
#ifdef __SWITCH__
        std::lock_guard<std::mutex> lock(mutex);
        return guitarInput ? snap.guitarOn : snap.standardOn;
#else
        return pad && SDL_GameControllerGetAttached(pad);
#endif
    }
    // `pressedAt` receives when this frame's earliest new press arrived, on the
    // now() clock, or stays negative when the input layer cannot say.
    // `whammy` receives the whammy bar or stick position, 0 at rest to 1.
    uint64_t read(bool wiiGuitar, bool playing, double *pressedAt = nullptr, float *whammy = nullptr) {
        uint64_t out = 0;
#ifdef __SWITCH__
        Snapshot s;
        {
            // Buttons and the press stamp are taken together, so a press can
            // never land in one frame's buttons and the next frame's stamp.
            std::lock_guard<std::mutex> lock(mutex);
            s = snap;
            if (pressedAt)
                *pressedAt = pressAt;
            pressAt = -1;
        }
        guitarConnected = s.guitarOn;
        guitarInput = exclusiveGuitarInput(wiiGuitar, playing, guitarConnected);
        if (whammy)
            *whammy = guitarInput ? s.guitarWhammy : s.standardStick;
        // Plus and Minus always answer from the normal controller.
        auto h = controllerButtons(wiiGuitar, playing, guitarConnected, s.standard, s.guitar,
                                   HidNpadButton_Plus | HidNpadButton_Minus);
        const std::pair<uint64_t, int> map[] = {
            {HidNpadButton_B, 0},      {HidNpadButton_A, 1},      {HidNpadButton_Y, 2},
            {HidNpadButton_X, 3},      {HidNpadButton_Minus, 4},  {HidNpadButton_Plus, 6},
            {HidNpadButton_StickL, 7}, {HidNpadButton_StickR, 8}, {HidNpadButton_L, 9},
            {HidNpadButton_R, 10},     {HidNpadButton_Up, 11},    {HidNpadButton_Down, 12},
            {HidNpadButton_Left, 13},  {HidNpadButton_Right, 14}, {HidNpadButton_ZL, 32},
            {HidNpadButton_ZR, 33}};
        for (auto [mask, b] : map)
            if (h & mask)
                out |= bit(b);
#else
        (void)wiiGuitar;
        (void)playing;
        if (pressedAt)
            *pressedAt = -1; // desktop presses are stamped from SDL events instead
        connect();
        if (whammy) {
            *whammy = 0;
            if (pad)
                for (auto [x, y] : {std::pair{SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY},
                                    std::pair{SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY}})
                    *whammy = std::max(*whammy, std::min(1.0f, std::hypot(float(SDL_GameControllerGetAxis(pad, x)),
                                                                         float(SDL_GameControllerGetAxis(pad, y))) /
                                                                  32767.0f));
        }
        if (pad) {
            for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
                if (SDL_GameControllerGetButton(pad, SDL_GameControllerButton(b)))
                    out |= bit(b);
            if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000)
                out |= bit(32);
            if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000)
                out |= bit(33);
        }
#endif
        return out;
    }
};
struct Entry {
    fs::path folder;
    std::string name, artist, error;
};
// A song read off the SD card away from the main thread: the parsed chart and
// the decoded cover, ready to install if the cursor is still on it.
struct Loaded {
    fs::path folder;
    std::unique_ptr<Song> song;
    std::string error;
    look::Image art;
};
Loaded loadEntry(fs::path folder) {
    Loaded l;
    l.folder = std::move(folder);
    // The cover decodes on the other spare core while this thread parses the chart.
    auto art = std::async(std::launch::async, [folder = l.folder] {
        runInBackground();
        return look::decodeArtwork(folder);
    });
    try {
        l.song = std::make_unique<Song>(loadSong(l.folder));
    } catch (const std::exception &e) {
        // The list only peeked at metadata, so real problems surface here.
        l.error = e.what();
    }
    l.art = art.get();
    return l;
}
double longestSustain = 0;
std::string plainTitle(const std::string &s) {
    std::string result;
    bool tag = false;
    for (char c : s) {
        if (c == '<') tag = true;
        else if (c == '>') tag = false;
        else if (!tag) result += c;
    }
    return result;
}
double now() { return double(SDL_GetPerformanceCounter()) / SDL_GetPerformanceFrequency(); }
#ifdef __SWITCH__
// The system keyboard. Blocks until the player closes it; false on cancel.
bool askText(const char *guide, std::string &text) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0)))
        return false;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetInitialText(&kbd, text.c_str());
    swkbdConfigSetStringLenMax(&kbd, 100);
    char out[512] = {};
    const Result rc = swkbdShow(&kbd, out, sizeof out);
    swkbdClose(&kbd);
    if (R_FAILED(rc))
        return false;
    text = out;
    return true;
}
#endif
// 1 right after an event, easing to 0 over `life` seconds; 0 before it happened.
float decay(double age, double life) {
    if (age < 0 || age > life)
        return 0;
    const float t = float(1 - age / life);
    return t * t;
}

// The highway: a strip of grip tape running off into the dark, chrome rails with
// a flame job at the near end, and bolted fret buttons on the player's edge.
void highway(const Song &song, const Session &session, const Settings &settings, double time, uint8_t held,
             double ui) {
    // farWidth sets how hard the board foreshortens. At 236 the far half of the
    // lookahead was crushed into the top ~29% of the highway, so any fast run
    // arrived as an unreadable blob; 330 spreads that to ~36% while keeping the
    // board tapered.
    const float top = 150, bottom = 600, edge = 664, nearWidth = 580, farWidth = 330;
    const float k = nearWidth / farWidth - 1;
    const bool power = session.powerActive;
    auto width = [&](float y) { return farWidth + (y - top) / (bottom - top) * (nearWidth - farWidth); };
    // Lefty flip mirrors the lanes here, so everything placed by lane follows.
    auto xx = [&](float lane, float y) {
        if (settings.lefty)
            lane = 5 - lane;
        return 640 + (lane / 5 - .5f) * width(y);
    };
    // Project onto the tilted plane that width() describes, so notes keep pace
    // with the surface: slow in the distance, faster as they approach.
    auto yy = [&](double noteTime) {
        float z = std::max(float((noteTime - time) / settings.travel), -.9f / k);
        return top + (bottom - top) * (1 - z) / (1 + k * z);
    };
    auto zAt = [&](float y) {
        float r = (y - top) / (bottom - top);
        return (1 - r) / (1 + r * k);
    };
    auto corners = [&](float y0, float y1) {
        return std::array<SDL_FPoint, 4>{SDL_FPoint{xx(0, y0), y0}, {xx(5, y0), y0}, {xx(5, y1), y1}, {xx(0, y1), y1}};
    };
    const SDL_Color surfaceTint = power ? SDL_Color{150, 200, 255, 255} : SDL_Color{255, 255, 255, 255};

    // Grip tape surface, scrolling with the notes so the whole board moves as one.
    const float period = float(settings.travel) * .4f;
    const int strips = 30;
    for (int i = 0; i < strips; ++i) {
        float y0 = top + (edge - top) * i / strips, y1 = top + (edge - top) * (i + 1) / strips;
        float v0 = float((time + zAt(y0) * settings.travel) / period), v1 = float((time + zAt(y1) * settings.travel) / period);
        float base = std::floor(v0);
        if (std::floor(v1) != base && v0 - v1 > .0001f) {
            // Split the strip where the texture wraps; SDL cannot repeat UVs.
            float cut = std::clamp((v0 - base) / (v0 - v1), 0.0f, 1.0f);
            float ym = y0 + (y1 - y0) * cut;
            gripTape(corners(y0, ym), v0 - base, 0, surfaceTint);
            gripTape(corners(ym, y1), 1, v1 - base + 1, surfaceTint);
        } else
            gripTape(corners(y0, y1), v0 - base, v1 - base, surfaceTint);
    }
    // Lane separators and beat lines stop short of the fret buttons.
    const float fretZone = bottom - 54;
    for (int lane = 1; lane < 5; ++lane)
        thickLine(xx(float(lane), top), top, xx(float(lane), fretZone), fretZone, 1.5f, {120, 130, 150, 40});
    double firstBeat = std::max(0.0, std::floor(song.tickAt(time) / song.resolution));
    for (int i = 0; i < 48; ++i) {
        float y = yy(song.seconds(Tick((firstBeat + i) * song.resolution)));
        if (y < top)
            break;
        if (y > fretZone)
            continue;
        float fade = (y - top) / (fretZone - top);
        thickLine(xx(0, y), y, xx(5, y), y, 1 + fade, {150, 160, 180, Uint8(14 + 40 * fade)});
    }
    // Chrome side rails with a flame job painted on the near end.
    for (int side = 0; side < 2; ++side) {
        float lane = side ? 5.0f : 0.0f, dir = xx(lane, top) > 640 ? 1.0f : -1.0f;
        SDL_Color hot = power ? ink::power : SDL_Color{228, 230, 240, 255};
        SDL_Color far{62, 66, 78, 255};
        quad({xx(lane, top), top}, {xx(lane, top) + dir * 4, top}, {xx(lane, edge) + dir * 13, edge},
             {xx(lane, edge), edge}, far, mix(far, {20, 20, 26, 255}, .6f), mix(hot, {30, 32, 40, 255}, .45f), hot);
    }
    // Darken the very front so the fret buttons read against the board.
    quad({xx(0, fretZone), fretZone}, {xx(5, fretZone), fretZone}, {xx(5, edge), edge}, {xx(0, edge), edge},
         {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 190}, {0, 0, 0, 190});

    const auto &notes = session.track->notes;
    // Reused every frame rather than allocated: the highway runs 60 times a
    // second and heap churn is the kind of cost that shows up as uneven frames.
    static std::vector<look::Fire> fires;
    fires.clear();
    // How recently each fret landed a hit, for the fret buttons' flash.
    std::array<float, 5> hitFlash{};
    auto begin = std::lower_bound(notes.begin(), notes.end(), time - longestSustain - .25,
                                  [](const Note &n, double t) { return n.time < t; });
    // Include long sustains even when their head is offscreen.
    for (auto it = begin; it != notes.end(); ++it) {
        const auto &n = *it;
        size_t idx = size_t(it - notes.begin());
        if (n.time > time + settings.travel)
            break;
        // Every note sits exactly where the projection puts it. Nudging far notes
        // apart for readability made them snap back into place mid-board.
        const float y = yy(n.time);
        const auto state = session.state[idx];
        // Sustains: neon tubing that wavers while it is being held down.
        for (int l = 0; l < 6; ++l)
            if (n.mask & (1 << l)) {
                float tail = std::max(top, yy(n.end[l])), head = std::min(bottom, y);
                if (n.end[l] <= time || n.end[l] <= n.time || head <= tail)
                    continue;
                const bool holding = state.result == 1 && (state.held & (1 << l));
                // A whammied sustain bends harder; on a star phrase it glows
                // star-power blue while it fills the meter.
                const bool bending = holding && session.whammying(session.lastTime);
                const bool charging = bending && session.whammyable(idx);
                SDL_Color c = state.result < 0 ? SDL_Color{110, 112, 124, 255} : power ? ink::power : lanes[size_t(l)];
                if (charging)
                    c = mix(c, ink::power, .6f);
                float lane = l == 5 ? 2.5f : l + .5f;
                auto tube = [&](float scale, SDL_Color col) {
                    static std::vector<SDL_Vertex> v;
                    static std::vector<int> idxs;
                    v.clear(), idxs.clear();
                    const int steps = 18;
                    for (int j = 0; j <= steps; ++j) {
                        float ty = head + (tail - head) * j / steps;
                        float depth = (ty - top) / (bottom - top);
                        float sway = bending   ? std::sin(ty * .11f - float(ui) * 30) * 9 * depth
                                     : holding ? std::sin(ty * .07f - float(ui) * 16) * 4 * depth
                                               : 0;
                        float cx = xx(lane, ty) + sway, half = (2 + 6 * depth) * scale;
                        v.push_back({{cx - half, ty}, col, {0, 0}});
                        v.push_back({{cx + half, ty}, col, {0, 0}});
                        if (j < steps) {
                            int b = j * 2;
                            idxs.insert(idxs.end(), {b, b + 1, b + 2, b + 2, b + 1, b + 3});
                        }
                    }
                    SDL_RenderGeometry(renderer, nullptr, v.data(), int(v.size()), idxs.data(), int(idxs.size()));
                };
                tube(2.6f, alpha(c, state.result < 0 ? .12f : holding ? .4f : .28f));
                tube(1, alpha(c, state.result < 0 ? .45f : 1));
                if (state.result >= 0)
                    tube(.34f, {255, 255, 255, Uint8(holding ? 230 : 150)});
            }
        if (state.result < 0 && state.judgedAt >= 0 && time >= state.judgedAt && time - state.judgedAt < .25) {
            float fade = 1 - float((time - state.judgedAt) / .25);
            for (int l = 0; l < 6; ++l)
                if (n.mask & (1 << l)) {
                    float lane = l == 5 ? 2.5f : l + .5f;
                    look::glow(xx(lane, bottom), bottom, l == 5 ? width(bottom) : 150, 80, alpha(ink::blood, fade * .8f));
                }
        }
        if (state.result == 1 && state.judgedAt >= 0 && time >= state.judgedAt)
            for (int l = 0; l < 6; ++l) {
                if (!(n.mask & (1 << l)))
                    continue;
                // An open note lights the whole board, so it burns on every lane.
                const bool openNote = l == 5;
                const double age = time - state.judgedAt;
                const bool sustaining = (state.held & (1 << l)) && time < n.end[l] && n.end[l] > n.time;
                if (openNote && (age < .38 || sustaining)) {
                    float fade = sustaining ? .55f : 1 - float(age / .38);
                    look::glow(640, bottom - 6, width(bottom) * 1.1f, 120,
                               alpha(power ? ink::power : lanes[5], fade * .7f));
                }
                for (int slot = 0; slot < (openNote ? 5 : 1); ++slot) {
                    const int lane = openNote ? 5 : l;
                    const float x = xx((openNote ? slot : l) + .5f, bottom);
                    const auto seed = uint32_t(idx * 7 + slot + l);
                    const size_t fret = size_t(openNote ? slot : l);
                    hitFlash[fret] = std::max(hitFlash[fret], decay(age, .16));
                    if (age < .42)
                        fires.push_back({x, lane, age, false, seed, state.tier});
                    if (sustaining)
                        fires.push_back({x, lane, age, true, seed, 0});
                }
            }
        if (it < begin || state.result == 1 || y < top || y > edge)
            continue;
        // Gems are born centred on the far edge, so half of each one sits above
        // the haze. Fade them in over the first stretch of board instead.
        const float appear = std::clamp((y - top) / ((bottom - top) * .14f), 0.0f, 1.0f);
        if (n.mask == 32) {
            // Open notes: a lit bar across the whole board.
            float w = width(y) * .92f, h = 4 + 8 * (y - top) / (bottom - top);
            SDL_Color c = power ? ink::power : lanes[5];
            look::glow(640, y, w * 1.1f, h * 5, alpha(c, (state.result < 0 ? .2f : .55f) * appear));
            rect(640 - w / 2, y - h / 2, w, h, alpha(c, (state.result < 0 ? .35f : 1) * appear));
            rect(640 - w / 2, y - h / 2, w, h * .4f, {255, 255, 255, Uint8(120 * appear)});
            continue;
        }
        const bool hammer = n.kind != Kind::Strum;
        // Missing any note of a star phrase breaks it: the rest of its notes
        // turn back into ordinary gems, since the phrase can no longer pay out.
        const bool starNote = n.phrase >= 0 && !session.phraseFailed[size_t(n.phrase)];
        GemStyle style = starNote ? (hammer ? GemStarHopo : GemStar) : hammer ? GemHopo : GemNormal;
        for (int l = 0; l < 5; ++l)
            if (n.mask & (1 << l)) {
                // Contact shadow on the board. Without one the gems look pasted onto
                // the highway instead of standing on it; it costs one ellipse and it
                // is most of what makes them feel like objects.
                // Gems are stickers with their own printed shadow, so nothing
                // else is drawn under them.
                const float gw = width(y) / 5 * .82f;
                const float gx = xx(l + .5f, y);
                // The sprite is rendered for the near end's viewing angle; further
                // up the board the view is shallower, so the gem sits flatter.
                const float squash = .8f + .2f * std::clamp((y - top) / (bottom - top), 0.0f, 1.0f);
                gem(power ? PowerColor : size_t(l), style, gx, y, gw, Uint8((state.result < 0 ? 80 : 255) * appear), squash);
            }
    }
    // Haze over the far end, on top of the notes. Without it the board stops at a
    // hard line and gems snap into existence on it; with it they resolve out of
    // the dark, which is what sells the highway as receding rather than as a
    // texture that happens to be narrower at one end.
    {
        const float hazeTo = top + (bottom - top) * .34f;
        quad({xx(0, top), top}, {xx(5, top), top}, {xx(5, hazeTo), hazeTo}, {xx(0, hazeTo), hazeTo},
             SDL_Color{7, 8, 11, 255}, SDL_Color{7, 8, 11, 255}, SDL_Color{7, 8, 11, 0}, SDL_Color{7, 8, 11, 0});
    }
    // The strike line breathes with the beat, so the tempo is in the board
    // itself and not only in your ears.
    {
        const double beats = song.tickAt(time) / song.resolution;
        const float pulse = std::pow(1 - float(beats - std::floor(beats)), 3.0f);
        glow(640, bottom, width(bottom) * 1.15f, 70,
             alpha(power ? ink::power : SDL_Color{255, 170, 70, 255}, .08f + .16f * pulse));
    }
    for (int l = 0; l < 5; ++l) {
        float x = xx(l + .5f, bottom);
        // A held fret spills its colour back up the board, so the lane you are on
        // is lit rather than merely outlined.
        if (held & (1 << l))
            glow(x, bottom - 40, width(bottom) / 5 * 1.6f, 300, alpha(power ? ink::power : lanes[size_t(l)], .16f));
        receptor(size_t(l), x, bottom, 106, held & (1 << l), power, hitFlash[size_t(l)]);
        auto label = body(16, alpha(lanes[size_t(l)], .85f));
        label.align = Align::Center;
        text(x, edge + 2, bindingName(settings.wiiGuitar ? wiiGuitarBindings[l] : settings.bindings[l]), label);
    }
    // Fire sits above the fret buttons; sustain flames first so bursts flare over them.
    std::stable_sort(fires.begin(), fires.end(), [](const look::Fire &a, const look::Fire &b) { return a.sustain > b.sustain; });
    for (const auto &f : fires)
        fire(f, bottom - 8, time, power);
}
// Startup scan: the mixtape is still being burned.
void loadingScreen(const std::string &label, float progress, int count, double ui) {
    wall(ui, ink::crt);
    auto title = stencil(66, ink::chrome, {116, 122, 138, 255});
    title.align = Align::Center, title.glow = alpha(ink::crt, .5f), title.glowSpread = 1.2f;
    text(640, 96, "SWITCH", title);
    auto burning = stencil(66, {255, 196, 60, 255}, {206, 26, 20, 255});
    burning.align = Align::Center, burning.glow = alpha(ink::blood, .85f), burning.glowSpread = 1.25f;
    text(654, 152, "HERO", burning);
    burnedCd(640, 356, 104, ui);
    auto note = marker(28, {206, 208, 218, 255}, -2);
    note.align = Align::Center;
    text(640, 470, count > 0 ? tr(count == 1 ? "found {} song" : "found {} songs", count) : tr("reading your sd card"),
         note);
    auto name = body(19, ink::faint);
    name.align = Align::Center, name.maxWidth = 760;
    text(640, 516, label, name);
    const float w = 560, x = 640 - w / 2, y = 566;
    rect(x, y, w, 6, {34, 36, 46, 255});
    if (progress >= 0) {
        rect(x, y, w * std::clamp(progress, 0.0f, 1.0f), 6, ink::acid);
        glow(x + w * std::clamp(progress, 0.0f, 1.0f), y + 3, 34, 34, alpha(ink::acid, .9f));
    } else {
        // Nothing to measure yet: sweep a marker while the card is walked.
        const float sweep = float(.5 - .5 * std::cos(ui * 3));
        rect(x + (w - 90) * sweep, y, 90, 6, ink::acid);
    }
    grade(ui);
    SDL_RenderPresent(renderer);
}
void screenshot(const std::string &path) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface) {
        if (SDL_RenderReadPixels(renderer, nullptr, surface->format->format, surface->pixels, surface->pitch) == 0)
            SDL_SaveBMP(surface, path.c_str());
        SDL_FreeSurface(surface);
    }
}

// Parts in the order loadSong sorts them; Settings::part indexes this.
const std::array<const char *, 5> partNames = {"Guitar", "Bass", "Rhythm", "Co-op", "Keys"};
int partIndex(const std::string &instrument) {
    for (size_t i = 0; i < partNames.size(); ++i)
        if (instrument == partNames[i])
            return int(i);
    return 0;
}
std::vector<std::string> songParts(const Song &song) {
    std::vector<std::string> parts;
    for (const auto &t : song.tracks)
        if (std::find(parts.begin(), parts.end(), t.instrument) == parts.end())
            parts.push_back(t.instrument);
    return parts;
}
// The track for one part and difficulty, or -1 when the chart has none.
int findTrack(const Song &song, const std::string &instrument, int difficulty) {
    for (size_t i = 0; i < song.tracks.size(); ++i)
        if (song.tracks[i].instrument == instrument && song.tracks[i].difficulty == difficulty)
            return int(i);
    return -1;
}
// The charted difficulty nearest `want`, taking the easier one on a tie.
int nearestDifficulty(const Song &song, const std::string &instrument, int want) {
    for (int step = 0; step < 4; ++step)
        for (int d : {want - step, want + step})
            if (d >= 0 && d < 4 && findTrack(song, instrument, d) >= 0)
                return d;
    return want;
}
// The one-letter difficulty tags, easy to expert, in the current language.
const char *const *difficultyLetters() {
    static const char *const english[] = {"E", "M", "H", "X"}, *const portuguese[] = {"F", "M", "D", "X"};
    return lang::portuguese() ? portuguese : english;
}
// Guitar frets as hint glyphs ("#0" green to "#4" orange). They draw as a
// fret-coloured button rather than a word, which is how a guitar player reads them.
std::string fretGlyph(int lane) { return "#" + std::to_string(lane); }
void hintButton(float x, float y, const std::string &glyph, const std::string &label) {
    if (glyph.size() == 2 && glyph[0] == '#')
        fretButton(x, y, size_t(glyph[1] - '0'), label);
    else
        button(x, y, glyph, label);
}
// The glyphs for the menu actions in the current controller mode.
std::string acceptGlyph(bool guitar) { return guitar ? fretGlyph(0) : "A"; }
std::string backGlyph(bool guitar) { return guitar ? fretGlyph(1) : "B"; }
std::string altGlyph(bool guitar) { return guitar ? fretGlyph(2) : "Y"; }
// A saved best as a compact line: stars, score and a full combo tag.
void bestLine(float x, float y, const Record &r, SDL_Color ink, float size = 15) {
    for (int i = 0; i < 5; ++i)
        star(x + 7 + i * 16, y + size * .55f, 6.5f, 0, i < r.stars ? SDL_Color{255, 196, 40, 255} : SDL_Color{70, 70, 80, 255},
             {18, 18, 22, 255});
    const std::string label = tr("BEST {}", grouped(r.score));
    text(x + 88, y, label, body(size, ink));
    if (r.fullCombo)
        text(x + 88 + measure(label, Face::Body, size) + 12, y - 2, "FC", marker(size + 3, ::fret::look::ink::acid, -4));
}
// Controller hints along the bottom edge, spaced by their widths.
void hints(const std::vector<std::pair<std::string, std::string>> &items, float x = 58, float y = 676) {
    for (const auto &[glyph, label] : items) {
        hintButton(x, y, glyph, label);
        x += 34 + measure(label, Face::Body, 17) + 36;
    }
}
// A vertical menu. The selected item is written in Sharpie on a strip of
// tape; the rest are stamped in chrome, dimmed when they cannot be picked.
void menu(float cx, float y, float spacing, const std::vector<std::string> &items, int selected, double ui,
          float width = 440, float size = 34, const std::vector<bool> &enabled = {}) {
    for (int i = 0; i < int(items.size()); ++i) {
        const float cy = y + i * spacing;
        const bool on = i == selected, live = enabled.empty() || enabled[size_t(i)];
        if (on) {
            const float angle = (hash01(uint32_t(i) * 131) - .5f) * 3;
            glow(cx, cy, width * 1.25f, spacing * 1.8f, alpha(ink::acid, .2f));
            tape(cx, cy, width, spacing * .86f, angle);
            auto s = marker(size, live ? ink::marker : SDL_Color{110, 110, 118, 255}, angle);
            s.align = Align::Center, s.maxWidth = width - 70;
            text(cx, cy - size * .62f, items[size_t(i)], s);
            const float bob = 4 * std::sin(float(ui) * 6);
            text(cx - width / 2 - 34 + bob, cy - size * .7f, ">", marker(size * 1.2f, ink::acid, angle));
        } else {
            auto s = stencil(size * .8f, live ? ink::chrome : ink::faint, live ? ink::steel : ink::faint);
            s.align = Align::Center, s.maxWidth = width - 40;
            text(cx, cy - size * .5f, items[size_t(i)], s);
        }
    }
}

// The options screens: a short list of categories, each opening its own page,
// so every setting sits under a name that says what it is for.
enum class Opt { Mode, NoFail, HitWindow, Speed, Lefty, TimingOverlay, Music, Effects, CalibrateAudio, AudioOffset, CalibrateVideo, VideoOffset, Fret, Test, Reset, Language };
struct OptionRow {
    Opt id;
    int fret = 0;
    std::string label, value, help;
    bool adjust = false; // left/right changes it
    std::string glyph;   // the button to press, drawn before the value
};
const std::array<std::pair<const char *, const char *>, 5> optionPages = {
    std::pair{"GAMEPLAY", "controller mode, no fail, note speed, lefty"},
    {"AUDIO", "music and sound effect volume"},
    {"AUDIO / VIDEO SYNC", "calibrate if notes feel early or late"},
    {"CONTROLS", "fret buttons and controller test"},
    {"LANGUAGE", "English or Portuguese"}};
std::vector<OptionRow> optionRows(int page, const Settings &s, bool confirmReset) {
    const std::string a = acceptGlyph(s.wiiGuitar);
    auto ms = [](double v) { return (v > 0 ? "+" : "") + std::to_string(int(v)) + " ms"; };
    std::vector<OptionRow> rows;
    if (page == 0) {
        rows.push_back({Opt::Mode, 0, tr("Controller mode"),
                        tr(s.wiiGuitar ? "WII GUITAR" : s.gamepad ? "PRESS-TO-HIT" : "STRUM"),
                        tr(s.wiiGuitar ? "Wii guitar over Bluetooth (needs the MissionControl patch). Fixed frets, strum bar strums."
                           : s.gamepad ? "Press each fret as its note arrives. Best on Joy-Cons and the Pro Controller."
                                       : "Hold the frets and strum with the D-pad. Hammer-ons and taps need no strum."),
                        true});
        rows.push_back({Opt::NoFail, 0, tr("No fail"), tr(s.noFail ? "ON" : "OFF"),
                        tr(s.noFail ? "The rock meter can never fail you. Good for learning a song."
                                    : "Miss too much and the crowd boos you off stage."),
                        true});
        static const char *const windows[] = {"STRICT", "NORMAL", "LENIENT"};
        const int expertMs = int(std::round(Session::windowFor(3, s.hitWindow) * 1000));
        const int easyMs = int(std::round(Session::windowFor(0, s.hitWindow) * 1000));
        rows.push_back({Opt::HitWindow, 0, tr("Hit window"), tr(windows[s.hitWindow]),
                        tr("How far off a note can be and still count: +-{} ms on Expert, +-{} ms on Easy.", expertMs,
                           easyMs),
                        true});
        char speed[16];
        std::snprintf(speed, sizeof speed, "%.2fx", 1.2 / s.travel);
        rows.push_back({Opt::Speed, 0, tr("Note speed"), speed,
                        tr("Faster notes spread out a busy chart. Each note is on screen for {} ms.",
                           int(s.travel * 1000)),
                        true});
        rows.push_back({Opt::Lefty, 0, tr("Lefty flip"), tr(s.lefty ? "ON" : "OFF"),
                        tr("Mirrors the highway so green is on the right, for left-handed players."), true});
    } else if (page == 1) {
        auto meter = [](int v) { return std::string(size_t(v), '|') + std::string(size_t(10 - v), '.'); };
        rows.push_back({Opt::Music, 0, tr("Music volume"), meter(s.musicVolume) + "  " + std::to_string(s.musicVolume * 10) + "%",
                        tr("The song and its previews on the song list."), true});
        rows.push_back({Opt::Effects, 0, tr("Sound effects volume"),
                        meter(s.sfxVolume) + "  " + std::to_string(s.sfxVolume * 10) + "%",
                        tr("Menu sounds, the count-in, misses and star power."), true});
    } else if (page == 2) {
        rows.push_back({Opt::CalibrateAudio, 0, tr("Calibrate audio"), tr("TO START"),
                        tr("Do this first. Tap along to a click track by ear to measure your audio delay."), false, a});
        rows.push_back({Opt::AudioOffset, 0, tr("Audio / input offset"), ms(s.audioMs),
                        tr("Fine-tune by hand in 5 ms steps. Positive judges notes later."), true});
        rows.push_back({Opt::CalibrateVideo, 0, tr("Calibrate video"), tr("TO START"),
                        tr("Tap as silent notes cross the line to line the highway up with the music."), false, a});
        rows.push_back({Opt::VideoOffset, 0, tr("Visual offset"), ms(s.videoMs),
                        tr("Fine-tune by hand in 5 ms steps. Positive draws notes later."), true});
        rows.push_back({Opt::TimingOverlay, 0, tr("Timing overlay"), tr(s.timingOverlay ? "ON" : "OFF"),
                        tr("Shows frame times and audio clock drift in the corner. For checking smoothness."), true});
    } else if (page == 4) {
        // Each language is shown in its own name, so a player who cannot read
        // the current one still recognises theirs.
        rows.push_back({Opt::Language, 0, tr("Language"),
                        lang::nativeName(lang::Language(std::max(0, s.language))),
                        tr("Menus and messages. Song titles stay as they are."), true});
    } else {
        static const char *const names[] = {"Green fret", "Red fret", "Yellow fret", "Blue fret", "Orange fret"};
        for (int i = 0; i < 5; ++i)
            rows.push_back({Opt::Fret, i, tr(names[i]),
                            bindingName(s.wiiGuitar ? wiiGuitarBindings[size_t(i)] : s.bindings[size_t(i)]),
                            tr(s.wiiGuitar ? "Fixed in Wii guitar mode." : "Press A, then the button or trigger to use.")});
        rows.push_back({Opt::Test, 0, tr("Controller test"), tr("TO OPEN"),
                        tr("Shows every button the game sees. Use it when a fret or guitar is not responding."), false, a});
        rows.push_back({Opt::Reset, 0, tr("Reset all options"), confirmReset ? tr("AGAIN TO CONFIRM") : "",
                        tr("Puts every option, offset and button back to its default."), false, confirmReset ? a : ""});
    }
    return rows;
}
// The console's (or desktop's) language, preselected on the first-start picker.
lang::Language systemLanguage() {
    lang::Language result = lang::Language::English;
#ifdef __SWITCH__
    if (R_SUCCEEDED(setInitialize())) {
        u64 code = 0;
        SetLanguage language;
        if (R_SUCCEEDED(setGetSystemLanguage(&code)) && R_SUCCEEDED(setMakeLanguage(code, &language)) &&
            (language == SetLanguage_PT || language == SetLanguage_PTBR))
            result = lang::Language::Portuguese;
        setExit();
    }
#else
    if (SDL_Locale *locales = SDL_GetPreferredLocales()) {
        if (locales[0].language && std::string(locales[0].language) == "pt")
            result = lang::Language::Portuguese;
        SDL_free(locales);
    }
#endif
    return result;
}
} // namespace
int main(int argc, char **argv) {
    try {
#ifdef __SWITCH__
        // The fonts live in the NRO's RomFS, which has to be mounted first.
        if (R_FAILED(romfsInit()))
            std::cerr << "Switch Hero: RomFS unavailable; falling back to sdmc fonts\n";
        const std::string home = "sdmc:/switch/switch-hero";
        fs::path root = home + "/songs", config = home + "/settings.cfg", scoresPath = home + "/scores.cfg";
#else
        fs::path root = "songs", config = "settings.cfg", scoresPath = "scores.cfg";
#endif
        bool inspect = false, smoke = false, smokeGuitar = false, timing = false;
        std::string shot = "switch-hero.bmp";
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--inspect")
                inspect = true;
            else if (a == "--smoke")
                smoke = true;
            else if (a == "--smoke-wii-guitar")
                smoke = smokeGuitar = true;
            else if (a == "--timing")
                timing = true;
            else if (a == "--screenshot" && i + 1 < argc)
                shot = argv[++i];
            else if (a == "--songs" && i + 1 < argc)
                root = argv[++i];
            else if (a == "--help") {
                std::cout << "switch-hero [--songs FOLDER] [--inspect] [--smoke | --smoke-wii-guitar] [--timing] [--screenshot FILE.bmp]\n";
                return 0;
            } else
                throw std::runtime_error("Unknown argument: " + a);
        }
        std::vector<Entry> entries;
        if (inspect) {
            for (auto &f : scanSongs(root)) {
                Entry e;
                e.folder = f;
                try {
                    auto song = loadSong(f);
                    e.name = song.name;
                    e.artist = song.artist;
                    std::cout << song.name << " | " << song.artist << " | " << song.chart.filename()
                              << " | offset=" << song.offset << "s | stems=" << song.audio.size() << '\n';
                    for (auto &t : song.tracks)
                        std::cout << "  " << t.instrument << " " << difficultyName(t.difficulty) << ": "
                                  << t.notes.size() << " note groups\n";
                    for (auto &w : song.warnings)
                        std::cout << "  WARNING: " << w << '\n';
                } catch (const std::exception &ex) {
                    e.name = f.filename().string();
                    e.error = ex.what();
                    std::cerr << "ERROR " << f << ": " << e.error << '\n';
                }
                entries.push_back(e);
            }
            if (entries.empty()) {
                std::cerr << "No song folders found\n";
                return 1;
            }
            return std::any_of(entries.begin(), entries.end(),
                               [](const Entry &e) { return !e.error.empty(); })
                       ? 1
                       : 0;
        }
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER))
            throw std::runtime_error(SDL_GetError());
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        SDL_Window *window =
            SDL_CreateWindow("Switch Hero - Controller rhythm game", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (!window)
            throw std::runtime_error(SDL_GetError());
        look::renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer)
            look::renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer)
            throw std::runtime_error(SDL_GetError());
        SDL_RenderSetLogicalSize(renderer, W, H);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        look::init(renderer);
        Settings settings;
        settings.load(config);
        // Until the player picks a language on first start, follow the system.
        lang::set(settings.language >= 0 ? lang::Language(settings.language) : systemLanguage());
        // A big library takes a while to walk, so show progress instead of a
        // black screen. Metadata only: charts are parsed when a song is picked.
        auto scanLibrary = [&]() {
            entries.clear();
            loadingScreen("looking for songs", -1, 0, now());
            auto folders = scanSongs(root);
            entries.reserve(folders.size());
            uint64_t lastFrame = 0;
            for (size_t i = 0; i < folders.size(); ++i) {
                Entry e;
                e.folder = folders[i];
                auto brief = peekSong(folders[i]);
                e.name = brief.name, e.artist = brief.artist, e.error = brief.error;
                entries.push_back(e);
                const uint64_t ticks = SDL_GetTicks64();
                if (ticks - lastFrame >= 33 || i + 1 == folders.size()) {
                    lastFrame = ticks;
                    SDL_PumpEvents();
                    loadingScreen(plainTitle(e.name), float(i + 1) / folders.size(), int(i + 1), now());
                }
            }
        };
        scanLibrary();
        Scores scores;
        scores.load(scoresPath);
        // How the last finished run compared with the stored best.
        bool newBest = false;
        int previousBest = -1;
        // A run that was consistently early or late suggests an offset fix on the
        // results screen, where the player notices it, rather than sending them
        // back to calibration. 0 when there is nothing worth fixing.
        int offsetTip = 0;
        bool offsetTipApplied = false;
        SDL_Joystick *virtualPad = nullptr;
        if (smoke) {
            settings = Settings{};
            settings.language = int(lang::Language::English);
            lang::set(lang::Language::English);
            int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 0);
            if (index < 0)
                throw std::runtime_error(SDL_GetError());
            virtualPad = SDL_JoystickOpen(index);
            if (!virtualPad)
                throw std::runtime_error(SDL_GetError());
            char guid[33];
            SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(virtualPad), guid, sizeof(guid));
            std::string mapping =
                std::string(guid) +
                ",Switch Hero test "
                "controller,a:b0,b:b1,x:b2,y:b3,back:b4,start:b6,leftshoulder:b9,rightshoulder:b10,dpup:b11,"
                "dpdown:b12,dpleft:b13,dpright:b14,lefttrigger:a4,righttrigger:a5,";
            if (SDL_GameControllerAddMapping(mapping.c_str()) < 0)
                throw std::runtime_error(SDL_GetError());
            SDL_JoystickSetVirtualAxis(virtualPad, 4, -32768);
            SDL_JoystickSetVirtualAxis(virtualPad, 5, -32768);
        }
        Controller controller;
        if (smokeGuitar)
            settings.wiiGuitar = true;
        Audio audio;
        audio.start(); // interface sounds work from the song list onward
        enum class Screen { Language, Main, Library, Select, Playing, Paused, Countdown, Results, Settings, Calibrate, Download };
        // The first start asks for a language before anything else.
        Screen screen = settings.language < 0 ? Screen::Language : Screen::Main;
        int languageRow = int(lang::current());
        std::string callout;
        double calloutAt = -1;
        Screen previousScreen = Screen::Library;
        double screenChangedAt = -1, scorePopAt = -1, multiplierPopAt = -1, powerFlashAt = -1;
        double shownScore = 0;
        int shownMultiplier = 1;
        int shownMisses = 0, shownCount = -1;
        double lastMissSound = -1e9;
        uint64_t escapeStart = 0;
        bool diagnostics = false;
        int milestone = 0;
        bool wasPower = false;
        std::unique_ptr<Song> song;
        std::unique_ptr<Session> session;
        size_t selected = 0, trackIndex = 0;
        // Song list order, and the groups left/right jump between: first letters
        // of the title or artist, or star counts when sorted by best stars.
        static const char *const sortNames[] = {"TITLE", "ARTIST", "STARS"};
        auto groupOf = [&](const Entry &e) -> std::string {
            if (settings.sortMode == 2) {
                const int stars = scores.bestStars(e.folder.filename().string());
                return stars < 0 ? "NEW" : std::to_string(stars) + "*";
            }
            return std::string(1, jumpLetter(sortKey(settings.sortMode == 1 ? e.artist : e.name)));
        };
        auto sortEntries = [&]() {
            const fs::path keep = selected < entries.size() ? entries[selected].folder : fs::path();
            struct Keyed {
                std::string primary, title;
                int stars;
                Entry entry;
            };
            std::vector<Keyed> keyed;
            keyed.reserve(entries.size());
            for (auto &e : entries)
                keyed.push_back({sortKey(settings.sortMode == 1 ? e.artist : e.name), sortKey(e.name),
                                 settings.sortMode == 2 ? scores.bestStars(e.folder.filename().string()) : 0, std::move(e)});
            std::stable_sort(keyed.begin(), keyed.end(), [&](const Keyed &a, const Keyed &b) {
                if (a.stars != b.stars)
                    return a.stars > b.stars; // most stars first, unplayed last
                if (a.primary != b.primary)
                    return a.primary < b.primary;
                return a.title < b.title;
            });
            entries.clear();
            for (auto &k : keyed)
                entries.push_back(std::move(k.entry));
            for (size_t i = 0; i < entries.size(); ++i)
                if (entries[i].folder == keep)
                    selected = i;
        };
        std::string jumpLabel;
        double jumpShownAt = -1;
        sortEntries();
        // Open the list on the song played last.
        for (size_t i = 0; i < entries.size(); ++i)
            if (!settings.lastSong.empty() && entries[i].folder.filename().string() == settings.lastSong)
                selected = i;
        int settingRow = 0, remapping = -1;
        // Menu cursors. optionsPage is -1 on the category list.
        int mainRow = 0, pauseRow = 0, resultRow = 0, optionsPage = -1, optionsTop = 0;
        bool confirmReset = false;
        // Deleting a song from the list asks first; row 0 keeps it.
        bool confirmDelete = false;
        int deleteRow = 0;
        // Resuming from pause counts 3-2-1 first, from here.
        double countdownAt = -1;
        // How long the results screen ignores buttons after a song ends.
        constexpr double resultsLockout = 1.0;
        int countdownShown = 0;
        // Where the options and download screens hand back to.
        Screen optionsReturn = Screen::Main, downloadReturn = Screen::Main;
        // Song setup, as Guitar Hero asks it: which part, then how hard.
        std::vector<std::string> parts;
        int selectStep = 1, partRow = 0, diffRow = 3;
        uint64_t previousButtons = 0;
        // Holding a direction (or the strum bar) in a menu repeats it: once
        // after a pause, then faster the longer it is held. When each held
        // direction started, and when it next repeats; since < 0 when released.
        struct Repeat {
            double since = -1, next = 0;
        };
        std::array<Repeat, 4> repeats{};
        std::array<bool, SDL_NUM_SCANCODES> previousKeys{};
        bool running = true;
        std::string message;
        // The last message that reported success rather than a problem. It
        // draws green while it is still the message shown; anything else is red.
        std::string goodNews;
        auto messageInk = [&] { return message == goodNews ? ink::acid : ink::blood; };
        int smokeFrames = 0;
        double frozen = 0;
        SmoothClock clock;
        // The song clock is read once per frame so judgement and drawing agree.
        bool clockRead = false;
        double frameTime = 0;
        uint32_t clockTicks = 0, previousClockTicks = 0;
        // When this frame's presses really happened. Only SDL events carry a
        // timestamp; polled libnx input is judged at the frame.
        uint32_t pressedAt = 0;
        bool pressStamped = false;
        // The Switch input thread stamps presses on the now() clock instead.
        double pressedAtSeconds = -1, clockSeconds = 0, previousClockSeconds = 0;
        auto readClock = [&](double audioTime) {
            if (!clockRead) {
                frameTime = clock.sync(audioTime, now());
                previousClockTicks = clockTicks;
                clockTicks = SDL_GetTicks();
                previousClockSeconds = clockSeconds;
                clockSeconds = now();
                clockRead = true;
            }
            return frameTime;
        };
        auto songTime = [&]() { return readClock(audio.position() - song->offset - settings.audioMs / 1000); };
        auto restartClock = [&]() {
            clock.reset();
            clockRead = false;
        };
        // A stamp from before the previous clock read belongs to an earlier
        // frame; clamp the age so a stalled event queue cannot throw a press far
        // into the past.
        auto pressTime = [&](double time) {
            if (pressedAtSeconds >= 0 && pressedAtSeconds >= previousClockSeconds)
                return time - std::min(.05, std::max(0.0, clockSeconds - pressedAtSeconds));
            if (pressStamped && !SDL_TICKS_PASSED(previousClockTicks, pressedAt))
                return time - std::min(.05, (clockTicks - pressedAt) / 1000.0);
            return time;
        };
        // Offset calibration. Audio: tap along to clicks with nothing on screen
        // to follow. Video: the clicks go silent and notes scroll down a highway;
        // tap as they cross the line.
        TapCalibration calibration;
        bool calibratingVideo = false;
        uint8_t calibrationFrets = 0;
        Song calibrationSong;
        calibrationSong.tempos = {{0, 120, 0}};
        Track calibrationTrack;
        for (int beat = 0; beat < 600; ++beat) {
            Note n;
            n.tick = beat * calibrationSong.resolution;
            n.time = beat * .5;
            n.mask = uint8_t(1 << (beat % 4 == 0 ? 2 : beat % 2 ? 1 : 3));
            n.end.fill(n.time);
            calibrationTrack.notes.push_back(n);
        }
        std::unique_ptr<Session> calibrationSession;
        auto startCalibration = [&](bool video) {
            calibratingVideo = video;
            calibration = TapCalibration{};
            calibration.period = .5; // 120 BPM
            calibrationFrets = 0;
            calibrationSession = std::make_unique<Session>(calibrationSong, calibrationTrack);
            look::prepareGems();
            try {
                audio.loadClicks(120);
                audio.silence(video);
                restartClock();
                message.clear();
                screen = Screen::Calibrate;
            } catch (const std::exception &e) {
                audio.stop();
                message = e.what();
            }
        };
        // Audio is measured on the raw song clock, so its result is the offset
        // itself. Video is measured with the audio offset applied, so its result
        // corrects the current visual offset.
        auto calibrationTime = [&]() {
            return readClock(audio.position() - (calibratingVideo ? settings.audioMs / 1000 : 0));
        };
        auto calibrationResult = [&]() {
            const double ms = std::round(calibration.median() * 1000);
            return std::clamp(calibratingVideo ? settings.videoMs - ms : ms, -500.0, 500.0);
        };
        // Chart downloads from Chorus Encore. Created on first use, which is also
        // when networking starts.
        std::unique_ptr<Downloader> downloader;
        Downloader::View downloads;
        std::string query;
        bool typing = false; // desktop only; the Switch uses the system keyboard
        size_t downloadRow = 0;
        int downloadsSeen = 0;
        // Cover of the highlighted chart: requested once the cursor rests on it,
        // decoded off the main thread, then uploaded here.
        std::string artUrl;
        double artUrlAt = 0;
        bool artRequested = false, artSettled = false;
        SDL_Texture *downloadArt = nullptr;
        std::future<look::Image> artDecoding;
        std::vector<std::future<look::Image>> staleArt;
        std::vector<bool> owned; // which results are already in the library
        size_t ownedFor = 0;
        int ownedAt = -1;
        auto openSearch = [&]() {
#ifdef __SWITCH__
            if (askText(tr("Song, artist or charter"), query)) {
                downloader->search(query);
                downloadRow = 0;
            }
#else
            typing = true;
            SDL_StartTextInput();
#endif
        };
        SDL_Texture *albumArt = nullptr;
        // Covers that are done with. Destroying a texture that this frame's
        // queued draws still use makes the renderer stop and flush, which cost a
        // whole frame per cursor step; they go after the frame is presented.
        std::vector<SDL_Texture *> retired;
        auto retire = [&](SDL_Texture *&t) {
            if (t)
                retired.push_back(t);
            t = nullptr;
        };
        // Song preview on the song list: which song is playing, since when, and
        // when the cursor last moved (a preview waits for it to settle).
        std::string previewFolder;
        bool previewActive = false;
        // Stems for the next preview, opened and seeked off the main thread.
        std::future<std::shared_ptr<Audio::Prepared>> previewLoading;
        std::string previewLoadingFolder;
        std::vector<std::future<std::shared_ptr<Audio::Prepared>>> stalePreviews;
        double previewStartedAt = 0, selectionChangedAt = 0;
        // Charts load in the background. Moving the cursor only asks for the
        // song; once it rests for a moment a loader thread parses the chart and
        // decodes the cover, and the result is installed if it is still wanted.
        // Parsing on every step used to hitch the list on each move.
        std::future<Loaded> loading;
        std::vector<std::future<Loaded>> staleLoads; // superseded; dropped once done
        bool loadPending = false, messageFromLoad = false;
        auto install = [&](Loaded &&l) {
            retire(albumArt);
            albumArt = look::uploadArtwork(l.art);
            if (l.song) {
                song = std::move(l.song);
                longestSustain = 0;
                for (const auto &track : song->tracks)
                    for (const auto &note : track.notes)
                        for (double end : note.end)
                            longestSustain = std::max(longestSustain, end - note.time);
            } else {
                message = l.error, messageFromLoad = true;
                if (selected < entries.size())
                    entries[selected].error = l.error;
            }
        };
        // The cursor moved: drop the old song and queue the new one.
        auto selectEntry = [&]() {
            selectionChangedAt = now();
            song.reset();
            trackIndex = 0;
            retire(albumArt);
            if (messageFromLoad)
                message.clear(), messageFromLoad = false;
            if (loading.valid())
                staleLoads.push_back(std::move(loading));
            loadPending = !entries.empty();
        };
        // Loads the selected song right now, for the places that need it at once.
        auto loadSelected = [&]() {
            selectEntry();
            loadPending = false;
            if (!entries.empty())
                install(loadEntry(entries[selected].folder));
        };
        auto loadInFlight = [&]() { return loadPending || loading.valid(); };
        // Finishes whatever load the selection is waiting on, blocking if it must.
        auto finishLoad = [&]() {
            if (loadPending)
                loadSelected();
            else if (loading.valid()) {
                auto l = loading.get();
                if (selected < entries.size() && entries[selected].folder == l.folder && !song)
                    install(std::move(l));
            }
        };
        auto start = [&]() {
            if (!song)
                return;
            try {
                look::prepareGems();
                audio.load(*song);
                session = std::make_unique<Session>(*song, song->tracks[trackIndex]);
                session->gamepadMode = settings.gamepad && !settings.wiiGuitar;
                session->noFail = settings.noFail;
                session->window = Session::windowFor(session->track->difficulty, settings.hitWindow);
                previewActive = false, previewFolder.clear(); // load() replaced the preview
                settings.lastSong = song->folder.filename().string();
                audio.pause(false);
                restartClock();
                callout.clear(), calloutAt = -1, milestone = 0, wasPower = false;
                newBest = false, previousBest = -1;
                shownMisses = 0, shownCount = -1, shownScore = 0, shownMultiplier = 1;
                screen = Screen::Playing;
                message.clear();
            } catch (const std::exception &e) {
                audio.stop();
                message = e.what();
                screen = Screen::Library;
            }
        };
        // The difficulty rows run easy to expert, top to bottom.
        auto selectedPart = [&]() { return parts.empty() ? std::string() : parts[size_t(partRow)]; };
        auto openSelect = [&](bool askPart) {
            if (!song)
                return;
            parts = songParts(*song);
            partRow = 0;
            for (size_t i = 0; i < parts.size(); ++i)
                if (partIndex(parts[i]) == settings.part)
                    partRow = int(i);
            diffRow = nearestDifficulty(*song, selectedPart(), settings.difficulty);
            // A song with one part goes straight to its difficulties.
            selectStep = askPart && parts.size() > 1 ? 0 : 1;
            message.clear();
            screen = Screen::Select;
        };
        loadSelected();
        if (smoke) {
            if (!song)
                throw std::runtime_error("Smoke test needs a valid song");
            start();
            if (screen != Screen::Playing)
                throw std::runtime_error(message);
        }
        while (running) {
#ifdef __SWITCH__
            if (!appletMainLoop())
                break;
#endif
            clockRead = false;
            pressStamped = false;
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                const bool pressEvent =
                    event.type == SDL_CONTROLLERBUTTONDOWN || (event.type == SDL_KEYDOWN && !event.key.repeat) ||
                    (event.type == SDL_CONTROLLERAXISMOTION && event.caxis.value > 16000 &&
                     (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
                      event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
                if (pressEvent && (!pressStamped || SDL_TICKS_PASSED(pressedAt, event.common.timestamp)))
                    pressedAt = event.common.timestamp, pressStamped = true;
                if (typing && event.type == SDL_TEXTINPUT && query.size() < 100)
                    query += event.text.text;
                if (typing && event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_BACKSPACE)
                    while (!query.empty()) {
                        // Drop one whole UTF-8 character.
                        const bool lead = (static_cast<unsigned char>(query.back()) & 0xC0) != 0x80;
                        query.pop_back();
                        if (lead)
                            break;
                    }
                if (event.type == SDL_QUIT)
                    running = false;
                if (event.type == SDL_RENDER_TARGETS_RESET || event.type == SDL_RENDER_DEVICE_RESET)
                    look::rebuild();
                if (!smoke && event.type == SDL_WINDOWEVENT &&
                    event.window.event == SDL_WINDOWEVENT_FOCUS_LOST && screen == Screen::Playing) {
                    frozen = audio.position() - song->offset;
                    audio.pause(true);
                    screen = Screen::Paused;
                }
                if (event.type == SDL_CONTROLLERDEVICEREMOVED && screen == Screen::Playing) {
                    frozen = audio.position() - song->offset;
                    audio.pause(true);
                    screen = Screen::Paused;
                    message = tr("Controller disconnected");
                }
            }
            if (virtualPad && session && screen == Screen::Playing) {
                for (int b = 0; b < 15; ++b)
                    SDL_JoystickSetVirtualButton(virtualPad, b, 0);
                SDL_JoystickSetVirtualAxis(virtualPad, 4, -32768);
                SDL_JoystickSetVirtualAxis(virtualPad, 5, -32768);
                if (session->next < session->track->notes.size()) {
                    const auto &note = session->track->notes[session->next];
                    double t = audio.position() - song->offset;
                    if (t >= note.time - .015 && t <= note.time + .08) {
                        if (smokeGuitar)
                            SDL_JoystickSetVirtualButton(virtualPad, 11, 1);
                        else if (note.mask == 32)
                            SDL_JoystickSetVirtualButton(virtualPad, 1, 1);
                        for (int l = 0; l < 5; ++l)
                            if (note.mask & (1 << l)) {
                                int b = settings.wiiGuitar ? wiiGuitarBindings[l] : settings.bindings[l];
                                if (b >= 32)
                                    SDL_JoystickSetVirtualAxis(virtualPad, b - 28, 32767);
                                else
                                    SDL_JoystickSetVirtualButton(virtualPad, b, 1);
                            }
                    }
                }
                SDL_JoystickUpdate();
            }
            const bool wasConnected = controller.connected();
            float whammyInput = 0;
            uint64_t buttons = controller.read(settings.wiiGuitar, screen == Screen::Playing || screen == Screen::Calibrate,
                                               &pressedAtSeconds, &whammyInput),
                     pressed = buttons & ~previousButtons;
            if (wasConnected && !controller.connected() && screen == Screen::Playing) {
                frozen = audio.position() - song->offset;
                audio.pause(true);
                screen = Screen::Paused;
                message = tr("Controller disconnected");
            }
            const auto *keys = SDL_GetKeyboardState(nullptr);
            auto key = [&](SDL_Scancode k) { return keys[k] && !previousKeys[k]; };
            auto press = [&](int b) { return bool(pressed & bit(b)); };
            bool up = press(11) || key(SDL_SCANCODE_UP), down = press(12) || key(SDL_SCANCODE_DOWN),
                 left = press(13) || key(SDL_SCANCODE_LEFT), right = press(14) || key(SDL_SCANCODE_RIGHT);
            {
                // Not where up and down strum (a song, the count-in, calibration),
                // and not on the delete prompt, where holding would flip the answer.
                const bool menu = screen != Screen::Playing && screen != Screen::Countdown &&
                                  screen != Screen::Calibrate && !confirmDelete && remapping < 0 && !typing;
                const std::array<std::pair<int, SDL_Scancode>, 4> held = {
                    std::pair{11, SDL_SCANCODE_UP}, {12, SDL_SCANCODE_DOWN}, {13, SDL_SCANCODE_LEFT}, {14, SDL_SCANCODE_RIGHT}};
                bool *fire[4] = {&up, &down, &left, &right};
                const double t = now();
                for (size_t i = 0; i < 4; ++i) {
                    auto &r = repeats[i];
                    if (!menu || !((buttons & bit(held[i].first)) || keys[held[i].second])) {
                        r.since = -1;
                        continue;
                    }
                    if (r.since < 0) {
                        // The press itself already counted; the first repeat waits.
                        r.since = t, r.next = t + .35;
                    } else if (t >= r.next) {
                        *fire[i] = true;
                        r.next = t + (t - r.since > 1.6 ? .045 : .09);
                    }
                }
            }
            bool accept = press(1) || key(SDL_SCANCODE_RETURN), back = press(4) || key(SDL_SCANCODE_ESCAPE),
                 pause = press(6) || key(SDL_SCANCODE_P);
            // Guitar frets act as menu shortcuts only outside gameplay.
            // Holding Minus for two seconds always gets back to the song list on
            // normal controls, whatever the controller mode is set to.
            if (buttons & bit(4)) {
                if (!escapeStart)
                    escapeStart = SDL_GetTicks64();
                else if (SDL_GetTicks64() - escapeStart > 2000 && screen != Screen::Library &&
                         screen != Screen::Main && screen != Screen::Language) {
                    escapeStart = 0;
                    settings.wiiGuitar = false;
                    audio.stop();
                    audio.playSfx(Sfx::Back);
                    remapping = -1;
                    message = goodNews = tr("Controller mode reset to press-to-hit");
                    screen = Screen::Library;
                }
            } else
                escapeStart = 0;
            // While calibration is taking taps the guitar is an instrument, not a menu.
            bool guitarMenu = settings.wiiGuitar && screen != Screen::Playing &&
                              !(screen == Screen::Calibrate && !calibration.done());
            accept = accept || (guitarMenu && press(9));
            back = back || (guitarMenu && press(32));
            // B backs out of menus, as everywhere else on the Switch (button 0 is
            // the B position on both backends). Not where B is a fret: during
            // play and the resume count-in, while calibration takes taps, while
            // rebinding a fret (B may be the button being bound), in the
            // controller test (B is pressed to test it), or in Wii guitar mode,
            // where it is the orange fret. Minus still works everywhere.
            const bool bBacks = !settings.wiiGuitar && screen != Screen::Playing && screen != Screen::Countdown &&
                                !(screen == Screen::Calibrate && !calibration.done()) && remapping < 0 && !diagnostics;
            back = back || (bBacks && press(0));
            bool secondary = press(2) || (guitarMenu && press(10));
            uint8_t frets = settings.wiiGuitar ? wiiGuitarFrets(buttons) : 0;
            SDL_Scancode fretKeys[] = {SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F,
                                       SDL_SCANCODE_G};
            for (int i = 0; i < 5; ++i)
                if ((!settings.wiiGuitar && (buttons & bit(settings.bindings[i]))) || keys[fretKeys[i]])
                    frets |= uint8_t(1 << i);
            auto openDownloads = [&]() {
                audio.playSfx(Sfx::Select, .7f);
                try {
                    if (!downloader) {
                        downloader = std::make_unique<Downloader>(root);
                        downloader->search(""); // newest charts until the player searches
                    }
                    downloadsSeen = downloader->view().downloads;
                    downloadReturn = screen;
                    message.clear();
                    screen = Screen::Download;
                } catch (const std::exception &e) {
                    message = e.what();
                }
            };
            auto openOptions = [&]() {
                audio.playSfx(Sfx::Select, .7f);
                optionsReturn = screen;
                optionsPage = -1, confirmReset = false;
                message.clear();
                screen = Screen::Settings;
            };
            if (screen == Screen::Language) {
                // Moving the cursor switches the language at once, so each
                // choice previews itself. There is nothing to back out to.
                if (up || down) {
                    languageRow = (languageRow + lang::Count + (down ? 1 : -1)) % lang::Count;
                    lang::set(lang::Language(languageRow));
                    audio.playSfx(Sfx::Move);
                }
                if (accept) {
                    settings.language = languageRow;
                    try {
                        settings.save(config);
                    } catch (const std::exception &e) {
                        SDL_Log("Switch Hero: %s", e.what());
                    }
                    audio.playSfx(Sfx::Select, .7f);
                    screen = Screen::Main;
                }
            } else if (screen == Screen::Main) {
                // Quickplay, download, options, quit.
                if (up || down) {
                    mainRow = (mainRow + 4 + (down ? 1 : -1)) % 4;
                    audio.playSfx(Sfx::Move);
                }
                // Back walks to Quit rather than quitting, so mashing back never exits.
                if (back && mainRow != 3) {
                    mainRow = 3;
                    audio.playSfx(Sfx::Move);
                } else if (accept || (back && mainRow == 3)) {
                    if (mainRow == 0) {
                        audio.playSfx(Sfx::Select, .7f);
                        message.clear();
                        screen = Screen::Library;
                    } else if (mainRow == 1)
                        openDownloads();
                    else if (mainRow == 2)
                        openOptions();
                    else
                        running = false;
                }
            } else if (screen == Screen::Library && confirmDelete) {
                if (up || down || left || right) {
                    deleteRow = 1 - deleteRow;
                    audio.playSfx(Sfx::Move);
                }
                if (back || (accept && deleteRow == 0)) {
                    confirmDelete = false;
                    audio.playSfx(Sfx::Back);
                } else if (accept && selected < entries.size()) {
                    confirmDelete = false;
                    const auto doomed = entries[selected];
                    // Close everything reading from the folder before removing it:
                    // the Switch will not delete files that are still open.
                    audio.stop();
                    previewActive = false, previewFolder.clear();
                    song.reset();
                    // Background loads may still have the folder's files open.
                    if (loading.valid())
                        loading.wait();
                    if (previewLoading.valid())
                        previewLoading.wait();
                    for (auto &f : staleLoads)
                        f.wait();
                    for (auto &f : stalePreviews)
                        f.wait();
                    loading = {}, previewLoading = {};
                    staleLoads.clear(), stalePreviews.clear();
                    loadPending = false;
                    try {
                        deleteSong(root, doomed.folder);
                        const std::string name = doomed.folder.filename().string();
                        scores.forget(name);
                        try {
                            scores.save(scoresPath);
                        } catch (const std::exception &) {
                        }
                        if (settings.lastSong == name)
                            settings.lastSong.clear();
                        entries.erase(entries.begin() + std::ptrdiff_t(selected));
                        selected = entries.empty() ? 0 : std::min(selected, entries.size() - 1);
                        ownedAt = -1; // the download list rechecks what is in the library
                        loadSelected();
                        message = goodNews = tr("Deleted {}", plainTitle(doomed.name));
                        audio.playSfx(Sfx::Select, .7f);
                    } catch (const std::exception &e) {
                        loadSelected();
                        message = e.what();
                        audio.playSfx(Sfx::Back);
                    }
                }
            } else if (screen == Screen::Library) {
                // In Wii guitar mode X is the guitar's Minus (the patch maps it
                // there for star power), so only the blue fret deletes.
                if (!entries.empty() && ((!settings.wiiGuitar && press(3)) || (guitarMenu && press(33)) ||
                                         key(SDL_SCANCODE_DELETE) ||
                                         key(SDL_SCANCODE_X))) {
                    confirmDelete = true, deleteRow = 0;
                    audio.playSfx(Sfx::Toggle);
                } else if (back) {
                    audio.playSfx(Sfx::Back);
                    message.clear();
                    screen = Screen::Main;
                } else if (!entries.empty()) {
                    if (up || down) {
                        const size_t n = entries.size();
                        selected = (selected + n + (down ? 1 : n - 1)) % n;
                        selectEntry();
                        audio.playSfx(Sfx::Move);
                    }
                    // Left/right jump between letter groups (or star groups): right
                    // to the start of the next one, left back to the start of this
                    // one, or of the previous one when already there.
                    if (left || right) {
                        const size_t n = entries.size();
                        auto startOf = [&](size_t i) {
                            const std::string g = groupOf(entries[i]);
                            while (i > 0 && groupOf(entries[i - 1]) == g)
                                --i;
                            return i;
                        };
                        size_t target = selected;
                        if (right) {
                            const std::string g = groupOf(entries[selected]);
                            target = selected;
                            while (target < n && groupOf(entries[target]) == g)
                                ++target;
                            if (target == n)
                                target = 0;
                        } else {
                            target = startOf(selected);
                            if (target == selected)
                                target = startOf(selected == 0 ? n - 1 : selected - 1);
                        }
                        if (target != selected) {
                            selected = target;
                            selectEntry();
                        }
                        jumpLabel = tr(groupOf(entries[selected])), jumpShownAt = now();
                        audio.playSfx(Sfx::Move);
                    }
                    if (press(settings.wiiGuitar ? 0 : 33) || key(SDL_SCANCODE_Z)) {
                        settings.sortMode = (settings.sortMode + 1) % 3;
                        sortEntries(); // the same song stays selected
                        jumpLabel = tr(sortNames[settings.sortMode]), jumpShownAt = now();
                        audio.playSfx(Sfx::Toggle);
                    }
                    if (accept) {
                        finishLoad(); // pressing A never waits on the rest delay
                        if (song) {
                            audio.playSfx(Sfx::Select);
                            openSelect(true);
                        } else
                            audio.playSfx(Sfx::Back);
                    }
                }
                if (screen == Screen::Library && (secondary || key(SDL_SCANCODE_TAB)))
                    openOptions();
                if (screen == Screen::Library && (press(6) || key(SDL_SCANCODE_O)))
                    openDownloads();
            } else if (screen == Screen::Select) {
                if ((!settings.wiiGuitar && press(3)) || (guitarMenu && press(33)) || key(SDL_SCANCODE_N)) {
                    settings.noFail = !settings.noFail;
                    audio.playSfx(Sfx::Toggle);
                }
                if (selectStep == 0) {
                    if (up || down) {
                        const int n = int(parts.size());
                        partRow = (partRow + n + (down ? 1 : -1)) % n;
                        audio.playSfx(Sfx::Move);
                    }
                    if (accept) {
                        diffRow = nearestDifficulty(*song, selectedPart(), settings.difficulty);
                        selectStep = 1;
                        audio.playSfx(Sfx::Select, .7f);
                    } else if (back) {
                        audio.playSfx(Sfx::Back);
                        screen = Screen::Library;
                    }
                } else {
                    // Uncharted difficulties are shown but skipped over.
                    if (up || down) {
                        for (int d = diffRow + (down ? 1 : -1); d >= 0 && d < 4; d += down ? 1 : -1)
                            if (findTrack(*song, selectedPart(), d) >= 0) {
                                diffRow = d;
                                audio.playSfx(Sfx::Move);
                                break;
                            }
                    }
                    if (accept) {
                        const int t = findTrack(*song, selectedPart(), diffRow);
                        if (t >= 0) {
                            trackIndex = size_t(t);
                            settings.part = partIndex(selectedPart());
                            settings.difficulty = diffRow;
                            try {
                                settings.save(config);
                            } catch (const std::exception &e) {
                                SDL_Log("Switch Hero: %s", e.what());
                            }
                            audio.playSfx(Sfx::Select);
                            start();
                        }
                    } else if (back) {
                        audio.playSfx(Sfx::Back);
                        if (parts.size() > 1)
                            selectStep = 0;
                        else
                            screen = Screen::Library;
                    }
                }
            } else if (screen == Screen::Download) {
                downloads = downloader->view();
                if (downloads.results.size() != ownedFor || downloads.downloads != ownedAt) {
                    // Checking the SD card per row per frame is slow; do it when the list changes.
                    owned.clear();
                    for (const auto &c : downloads.results)
                        owned.push_back(downloader->inLibrary(c));
                    ownedFor = downloads.results.size(), ownedAt = downloads.downloads;
                }
                const size_t count = downloads.results.size();
                downloadRow = count ? std::min(downloadRow, count - 1) : 0;
                {
                    const std::string url = count ? enchor::albumArtUrl(downloads.results[downloadRow]) : std::string();
                    if (url != artUrl) {
                        retire(downloadArt);
                        if (artDecoding.valid())
                            staleArt.push_back(std::move(artDecoding));
                        artUrl = url, artUrlAt = now();
                        artRequested = false, artSettled = url.empty();
                    }
                    // Wait for the cursor to rest, so scrolling past covers never fetches them.
                    if (!artRequested && !artUrl.empty() && now() - artUrlAt > .25) {
                        downloader->wantArt(artUrl);
                        artRequested = true;
                    }
                    if (artRequested && !artSettled && !artDecoding.valid())
                        if (auto bytes = downloader->art(artUrl)) {
                            if (bytes->empty())
                                artSettled = true; // no cover; the panel shows a disc instead
                            else
                                artDecoding = std::async(std::launch::async,
                                                         [bytes] {
                                                             runInBackground();
                                                             return look::decodeImage(*bytes);
                                                         });
                        }
                    if (artDecoding.valid() &&
                        artDecoding.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                        downloadArt = look::uploadArtwork(artDecoding.get());
                        artSettled = true;
                    }
                    staleArt.erase(std::remove_if(staleArt.begin(), staleArt.end(),
                                                  [](auto &f) {
                                                      return f.wait_for(std::chrono::seconds(0)) ==
                                                             std::future_status::ready;
                                                  }),
                                   staleArt.end());
                }
                if (typing) {
                    if (key(SDL_SCANCODE_RETURN) || key(SDL_SCANCODE_KP_ENTER)) {
                        typing = false;
                        downloader->search(query);
                        downloadRow = 0;
                        audio.playSfx(Sfx::Select, .7f);
                    } else if (key(SDL_SCANCODE_ESCAPE))
                        typing = false;
                } else {
                    if (count && (up || down)) {
                        downloadRow = (downloadRow + count + (down ? 1 : -1)) % count;
                        audio.playSfx(Sfx::Move);
                    }
                    // Fetch the next page before the player reaches the end of this one.
                    if (count && downloadRow + 4 >= count && downloads.more())
                        downloader->nextPage();
                    if (accept && count && downloads.state != Downloader::State::Downloading) {
                        if (owned[downloadRow])
                            audio.playSfx(Sfx::Back);
                        else {
                            downloader->download(downloads.results[downloadRow]);
                            audio.playSfx(Sfx::Select);
                        }
                    }
                    if (secondary || key(SDL_SCANCODE_SLASH)) {
                        audio.playSfx(Sfx::Select, .7f);
                        openSearch();
                    }
                    if (back) {
                        audio.playSfx(Sfx::Back);
                        if (downloads.state == Downloader::State::Downloading)
                            downloader->cancel();
                        else {
                            // Pick up what was downloaded, and land on the newest one.
                            screen = downloadReturn;
                            retire(downloadArt);
                            artUrl.clear();
                            if (downloads.downloads != downloadsSeen) {
                                scanLibrary();
                                sortEntries();
                                for (size_t i = 0; i < entries.size(); ++i)
                                    if (entries[i].folder.filename() == downloads.lastFolder)
                                        selected = i;
                                selected = entries.empty() ? 0 : std::min(selected, entries.size() - 1);
                                loadSelected();
                                screen = Screen::Library;
                            }
                        }
                    }
                }
            } else if (screen == Screen::Settings) {
                auto leaveOptions = [&]() {
                    try {
                        settings.save(config);
                        message.clear();
                    } catch (const std::exception &e) {
                        message = e.what();
                    }
                    audio.playSfx(Sfx::Back);
                    screen = optionsReturn;
                };
                if (remapping >= 0) {
                    if (back)
                        remapping = -1;
                    else if (pressed) {
                        for (int b = 0; b <= 33; ++b)
                            if (press(b) && fretBindingAllowed(b)) {
                                bool duplicate = false;
                                for (int i = 0; i < 5; ++i)
                                    if (i != remapping && settings.bindings[i] == b)
                                        duplicate = true;
                                if (duplicate)
                                    message = tr("Already assigned to another fret");
                                else {
                                    settings.bindings[remapping] = b;
                                    message.clear();
                                    remapping = -1;
                                }
                                break;
                            }
                    }
                } else if (diagnostics) {
                    if (back || accept) {
                        diagnostics = false; // the panel closes first
                        audio.playSfx(Sfx::Back);
                    }
                } else if (optionsPage < 0) {
                    const int count = int(optionPages.size()) + 1; // the pages, then Back
                    if (up || down) {
                        optionsTop = (optionsTop + count + (down ? 1 : -1)) % count;
                        audio.playSfx(Sfx::Move);
                    }
                    if (back || (accept && optionsTop == count - 1))
                        leaveOptions();
                    else if (accept) {
                        optionsPage = optionsTop, settingRow = 0, confirmReset = false;
                        message.clear();
                        audio.playSfx(Sfx::Select, .7f);
                    }
                } else {
                    const auto rows = optionRows(optionsPage, settings, confirmReset);
                    const int count = int(rows.size());
                    settingRow = std::clamp(settingRow, 0, count - 1);
                    if (up || down) {
                        settingRow = (settingRow + count + (down ? 1 : -1)) % count;
                        confirmReset = false;
                        message.clear();
                        audio.playSfx(Sfx::Move);
                    }
                    const auto &row = rows[size_t(settingRow)];
                    const int delta = right ? 1 : left ? -1 : 0;
                    if (delta && row.adjust)
                        audio.playSfx(Sfx::Toggle, .8f);
                    switch (row.id) {
                    case Opt::Mode:
                        if (delta || accept) {
                            int mode = settings.wiiGuitar ? 2 : settings.gamepad ? 0 : 1;
                            mode = (mode + (delta < 0 ? 2 : 1)) % 3;
                            settings.wiiGuitar = mode == 2;
                            settings.gamepad = mode == 0;
                            if (accept)
                                audio.playSfx(Sfx::Toggle, .8f);
                        }
                        break;
                    case Opt::NoFail:
                        if (delta || accept) {
                            settings.noFail = !settings.noFail;
                            if (accept)
                                audio.playSfx(Sfx::Toggle, .8f);
                        }
                        break;
                    case Opt::HitWindow:
                        if (accept) {
                            settings.hitWindow = (settings.hitWindow + 1) % 3;
                            audio.playSfx(Sfx::Toggle, .8f);
                        } else if (delta)
                            settings.hitWindow = std::clamp(settings.hitWindow + delta, 0, 2);
                        break;
                    case Opt::Speed:
                        // Right is faster, which is a shorter trip down the board.
                        settings.travel = std::clamp(settings.travel - delta * 0.05, 0.75, 3.0);
                        break;
                    case Opt::Lefty:
                        if (delta || accept) {
                            settings.lefty = !settings.lefty;
                            if (accept)
                                audio.playSfx(Sfx::Toggle, .8f);
                        }
                        break;
                    case Opt::Music:
                        settings.musicVolume = std::clamp(settings.musicVolume + delta, 0, 10);
                        break;
                    case Opt::Effects:
                        settings.sfxVolume = std::clamp(settings.sfxVolume + delta, 0, 10);
                        break;
                    case Opt::AudioOffset:
                        settings.audioMs = std::clamp(settings.audioMs + delta * 5, -500.0, 500.0);
                        break;
                    case Opt::VideoOffset:
                        settings.videoMs = std::clamp(settings.videoMs + delta * 5, -500.0, 500.0);
                        break;
                    case Opt::TimingOverlay:
                        if (delta || accept) {
                            settings.timingOverlay = !settings.timingOverlay;
                            if (accept)
                                audio.playSfx(Sfx::Toggle, .8f);
                        }
                        break;
                    case Opt::CalibrateAudio:
                    case Opt::CalibrateVideo:
                        if (accept) {
                            audio.playSfx(Sfx::Select);
                            startCalibration(row.id == Opt::CalibrateVideo);
                        }
                        break;
                    case Opt::Fret:
                        if (accept && !settings.wiiGuitar) {
                            audio.playSfx(Sfx::Select, .7f);
                            remapping = row.fret;
                        }
                        break;
                        if (delta || accept) {
                            settings.language =
                                (std::max(0, settings.language) + (delta < 0 ? lang::Count - 1 : 1)) % lang::Count;
                            lang::set(lang::Language(settings.language));
                            if (accept)
                                audio.playSfx(Sfx::Toggle, .8f);
                        }
                        break;
                    case Opt::Test:
                        if (accept) {
                            diagnostics = true;
                            audio.playSfx(Sfx::Toggle);
                        }
                        break;
                    case Opt::Reset:
                        if (accept && !confirmReset) {
                            confirmReset = true;
                            audio.playSfx(Sfx::Toggle);
                        } else if (accept) {
                            // Keep what the game remembers rather than what the player
                            // set: last part, difficulty and song, and the list order.
                            const Settings kept = settings;
                            settings = Settings{};
                            settings.part = kept.part, settings.difficulty = kept.difficulty;
                            settings.lastSong = kept.lastSong, settings.sortMode = kept.sortMode;
                            settings.language = kept.language;
                            confirmReset = false;
                            message = goodNews = tr("Options reset to defaults");
                            audio.playSfx(Sfx::Select);
                        }
                        break;
                    }
                    if (back) {
                        optionsPage = -1, confirmReset = false;
                        message.clear();
                        audio.playSfx(Sfx::Back);
                    }
                }
            } else if (screen == Screen::Playing) {
                double time = songTime();
                if (pause || back) {
                    frozen = time;
                    audio.pause(true);
                    audio.playSfx(Sfx::Back);
                    screen = Screen::Paused;
                } else {
                    if (press(3) || key(SDL_SCANCODE_LSHIFT))
                        session->activate();
                    // On a keyboard, holding W wobbles a virtual bar.
                    if (keys[SDL_SCANCODE_W])
                        whammyInput = float(.5 + .5 * std::sin(now() * 25));
                    session->whammy(whammyInput, time);
                    session->update(time, frets, up || down || press(1) || key(SDL_SCANCODE_SPACE),
                                    press(1) || key(SDL_SCANCODE_SPACE), pressTime(time));
                    if (time < 0) {
                        // Count the song in with drumstick clicks.
                        const int count = int(std::ceil(-time));
                        if (count != shownCount) {
                            shownCount = count;
                            audio.playSfx(Sfx::Count, .8f);
                        }
                    }
                    if (session->misses > shownMisses) {
                        // One screech per fumble, not one per note: a dropped run
                        // would otherwise machine-gun the sound.
                        if (now() - lastMissSound > .4) {
                            audio.playSfx(Sfx::Miss, .75f);
                            lastMissSound = now();
                        }
                        shownMisses = session->misses;
                    }
                    // Shout out long streaks and star power, the moments worth celebrating.
                    if (session->combo / 50 > milestone) {
                        milestone = session->combo / 50;
                        callout = tr("{} NOTE STREAK!", milestone * 50);
                        calloutAt = now();
                        audio.playSfx(Sfx::Streak);
                    } else if (session->combo < milestone * 50)
                        milestone = session->combo / 50;
                    if (session->powerActive && !wasPower) {
                        callout = "STAR POWER!";
                        calloutAt = now();
                        powerFlashAt = now();
                        audio.playSfx(Sfx::StarPower);
                    }
                    wasPower = session->powerActive;
                    // Guitar cuts out while the meter is in the red, as in the originals.
                    audio.muteGuitar(session->inRed());
                    if (session->failed) {
                        audio.pause(true);
                        audio.muteGuitar(false);
                        audio.playSfx(Sfx::Fail);
                        frozen = time;
                        screen = Screen::Results;
                    } else if (auto e = audio.error(); !e.empty()) {
                        audio.pause(true);
                        screen = Screen::Paused;
                        message = e;
                    } else if (audio.position() > audio.duration() + 0.3) {
                        audio.pause(true);
                        audio.playSfx(Sfx::Win);
                        frozen = time;
                        screen = Screen::Results;
                        // Only a finished song is recorded; a failed one never is.
                        const double accuracy =
                            session->state.empty() ? 1 : double(session->hits) / double(session->state.size());
                        const std::string folder = song->folder.filename().string();
                        const auto *before = scores.find(folder, session->track->instrument, session->track->difficulty);
                        previousBest = before ? before->score : -1;
                        newBest = scores.submit(folder, session->track->instrument, session->track->difficulty,
                                                {int(session->score), starsFor(accuracy, false), int(accuracy * 100),
                                                 session->misses == 0});
                        try {
                            scores.save(scoresPath);
                        } catch (const std::exception &e) {
                            message = e.what();
                        }
                    }
                }
            } else if (screen == Screen::Paused) {
                // Resume, restart, change difficulty, quit. Back and Plus resume, so
                // backing out of a pause can never throw the song away.
                if (up || down) {
                    pauseRow = (pauseRow + 4 + (down ? 1 : -1)) % 4;
                    audio.playSfx(Sfx::Move);
                }
                const bool resume = pause || back || (accept && pauseRow == 0);
                if (resume) {
                    if (audio.error().empty()) {
                        // Count back in rather than dropping the player straight into notes.
                        countdownAt = now(), countdownShown = 0;
                        message.clear();
                        screen = Screen::Countdown;
                    } else if (back) {
                        audio.stop();
                        audio.playSfx(Sfx::Back);
                        screen = Screen::Library;
                    }
                } else if (accept && pauseRow == 1) {
                    audio.playSfx(Sfx::Select);
                    start();
                } else if (accept && pauseRow == 2) {
                    audio.stop();
                    audio.playSfx(Sfx::Select, .7f);
                    openSelect(false);
                } else if (accept && pauseRow == 3) {
                    audio.stop();
                    audio.playSfx(Sfx::Back);
                    screen = Screen::Library;
                    message.clear();
                }
            } else if (screen == Screen::Countdown) {
                const double left = 3 - (now() - countdownAt);
                if (pause || back) {
                    audio.playSfx(Sfx::Back);
                    screen = Screen::Paused;
                } else if (left <= 0) {
                    restartClock();
                    audio.pause(false);
                    screen = Screen::Playing;
                } else if (int(std::ceil(left)) != countdownShown) {
                    countdownShown = int(std::ceil(left));
                    audio.playSfx(Sfx::Count, .8f);
                }
            } else if (screen == Screen::Calibrate) {
                const double time = calibrationTime();
                if (!calibration.done()) {
                    const uint8_t rising = frets & uint8_t(~calibrationFrets);
                    calibrationFrets = frets;
                    if (back) {
                        audio.stop();
                        audio.playSfx(Sfx::Back);
                        screen = Screen::Settings;
                    } else if (rising || up || down || press(1) || key(SDL_SCANCODE_SPACE)) {
                        calibration.tap(pressTime(time));
                        if (calibration.done()) {
                            audio.stop();
                            audio.playSfx(Sfx::Streak);
                        }
                    }
                } else if (accept) {
                    (calibratingVideo ? settings.videoMs : settings.audioMs) = calibrationResult();
                    settings.save(config);
                    audio.playSfx(Sfx::Select);
                    screen = Screen::Settings;
                } else if (secondary || key(SDL_SCANCODE_R)) {
                    audio.playSfx(Sfx::Select, .7f);
                    startCalibration(calibratingVideo);
                } else if (back) {
                    audio.playSfx(Sfx::Back);
                    screen = Screen::Settings;
                }
            } else if (screen == Screen::Results && now() - screenChangedAt < resultsLockout) {
                // The song can end, or fail, in the middle of a run of notes, and
                // B is the orange fret. Presses aimed at the song must not land on
                // this menu and throw the results away, so it waits a moment.
            } else if (screen == Screen::Results) {
                // Continue, retry, change difficulty.
                if (up || down) {
                    const int rows = offsetTip ? 4 : 3;
                    resultRow = (resultRow + rows + (down ? 1 : -1)) % rows;
                    audio.playSfx(Sfx::Move);
                }
                if (back || (accept && resultRow == 0)) {
                    audio.stop();
                    audio.playSfx(Sfx::Back);
                    screen = Screen::Library;
                } else if (accept && resultRow == 1) {
                    audio.playSfx(Sfx::Select);
                    start();
                } else if (accept && resultRow == 2) {
                    audio.stop();
                    audio.playSfx(Sfx::Select, .7f);
                    openSelect(false);
                } else if (accept && resultRow == 3 && offsetTip && !offsetTipApplied) {
                    // Late hits mean the song should be judged later: a positive
                    // audio offset does exactly that, and moves the notes with it.
                    settings.audioMs = std::clamp(settings.audioMs + offsetTip, -500.0, 500.0);
                    offsetTipApplied = true;
                    try {
                        settings.save(config);
                    } catch (const std::exception &e) {
                        message = e.what();
                    }
                    audio.playSfx(Sfx::Select);
                }
            }
            // Background chart loads: start one once the cursor rests, install it
            // when it lands, and let superseded ones finish and drop.
            staleLoads.erase(std::remove_if(staleLoads.begin(), staleLoads.end(),
                                            [](std::future<Loaded> &f) {
                                                return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                                            }),
                             staleLoads.end());
            if (loadPending && now() - selectionChangedAt > .15 && selected < entries.size()) {
                loading = std::async(std::launch::async, [folder = entries[selected].folder] {
                    runInBackground();
                    return loadEntry(folder);
                });
                loadPending = false;
            }
            if (loading.valid() && loading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                auto l = loading.get();
                if (selected < entries.size() && entries[selected].folder == l.folder && !song)
                    install(std::move(l));
            }
            // The preview plays only while picking a song. It starts once the cursor
            // settles, fades in, and fades out again before looping, so browsing
            // quickly never stutters audio on and off.
            audio.setVolumes(settings.musicVolume / 10.0f, settings.sfxVolume / 10.0f);
            if (screen == Screen::Library || screen == Screen::Select) {
                const std::string folder = song ? song->folder.string() : std::string();
                if (previewActive && previewFolder != folder) {
                    audio.stop();
                    previewActive = false;
                }
                const double age = now() - previewStartedAt;
                if (previewActive && age > 26.5)
                    previewActive = false, previewFolder.clear(); // loop from the start point
                if (!previewActive && song && folder != previewFolder && now() - selectionChangedAt > .6) {
                    previewFolder = folder;
                    double at = song->duration * .3;
                    if (auto it = song->metadata.find("preview_start_time"); it != song->metadata.end())
                        try {
                            if (const double ms = std::stod(it->second); ms > 0)
                                at = ms / 1000;
                        } catch (const std::exception &) {
                        }
                    at = std::clamp(at, 0.0, std::max(0.0, song->duration - 10));
                    if (previewLoading.valid())
                        stalePreviews.push_back(std::move(previewLoading));
                    previewLoadingFolder = folder;
                    previewLoading = std::async(std::launch::async,
                                                [stems = song->audio, length = song->duration + song->offset, at] {
                                                    runInBackground();
                                                    return Audio::prepare(stems, length, at);
                                                });
                }
                if (previewLoading.valid() &&
                    previewLoading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    try {
                        auto prepared = previewLoading.get();
                        if (previewLoadingFolder == folder) {
                            audio.setSongGain(0);
                            audio.playPrepared(std::move(prepared));
                            previewActive = true;
                            previewStartedAt = now();
                        }
                    } catch (const std::exception &) {
                        // A song that cannot preview still plays or reports its
                        // error when started; the list just stays quiet.
                    }
                }
                if (previewActive)
                    audio.setSongGain(float(std::clamp(std::min(age / 1.0, (26.5 - age) / 1.5), 0.0, 1.0)));
            } else if (previewActive || previewLoading.valid()) {
                if (previewActive)
                    audio.stop();
                if (previewLoading.valid())
                    stalePreviews.push_back(std::move(previewLoading));
                previewActive = false;
                previewFolder.clear();
            }
            stalePreviews.erase(std::remove_if(stalePreviews.begin(), stalePreviews.end(),
                                               [](auto &f) {
                                                   return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                                               }),
                                stalePreviews.end());
            const double ui = now();
            if (screen != previousScreen) {
                if (screen == Screen::Paused)
                    pauseRow = 0;
                if (screen == Screen::Results) {
                    resultRow = 0, offsetTip = 0, offsetTipApplied = false;
                    // The median of the hits' timing errors: one fluffed note
                    // cannot drag it, and it needs enough hits to mean anything.
                    std::vector<double> errors;
                    for (const auto &st : session->state)
                        if (st.result == 1)
                            errors.push_back(st.error);
                    if (errors.size() >= 12) {
                        std::nth_element(errors.begin(), errors.begin() + std::ptrdiff_t(errors.size() / 2), errors.end());
                        const int ms = int(std::lround(errors[errors.size() / 2] * 1000));
                        if (std::abs(ms) >= 6)
                            offsetTip = ms;
                    }
                }
                previousScreen = screen, screenChangedAt = ui;
            }
            if (session) {
                if (session->score > shownScore)
                    scorePopAt = ui;
                if (session->multiplier() != shownMultiplier)
                    multiplierPopAt = ui, shownMultiplier = session->multiplier();
                shownScore = session->score;
            }
            const std::string glyphAccept = acceptGlyph(settings.wiiGuitar), glyphBack = backGlyph(settings.wiiGuitar),
                              glyphAlt = altGlyph(settings.wiiGuitar);
            if (screen == Screen::Language) {
                wall(ui, ink::crt);
                auto title = stencil(66, ink::chrome, {116, 122, 138, 255});
                title.align = Align::Center, title.glow = alpha(ink::crt, .5f), title.glowSpread = 1.2f;
                text(640, 70, "SWITCH", title);
                auto burning = stencil(66, {255, 196, 60, 255}, {206, 26, 20, 255});
                burning.align = Align::Center, burning.glow = alpha(ink::blood, .85f), burning.glowSpread = 1.25f;
                text(654, 126, "HERO", burning);
                auto head = stencil(44);
                head.align = Align::Center;
                text(640, 262, tr("CHOOSE YOUR LANGUAGE"), head);
                std::vector<std::string> names;
                for (int i = 0; i < lang::Count; ++i)
                    names.push_back(lang::nativeName(lang::Language(i)));
                menu(640, 390, 74, names, languageRow, ui, 420, 36);
                auto sub = body(18, ink::dim);
                sub.align = Align::Center;
                text(640, 560, tr("you can change it later in options"), sub);
                hints({{glyphAccept, tr("SELECT")}});
            } else if (screen == Screen::Main) {
                wall(ui, ink::crt);
                auto title = stencil(112, ink::chrome, {116, 122, 138, 255});
                title.glow = alpha(ink::crt, .55f), title.glowSpread = 1.2f;
                text(70, 40, "SWITCH", title);
                auto burning = stencil(112, {255, 196, 60, 255}, {206, 26, 20, 255});
                burning.glow = alpha(ink::blood, .85f), burning.glowSpread = 1.25f;
                text(94, 134, "HERO", burning);
                text(110, 246, tr("burned mixtape vol. 1"), marker(30, {198, 200, 212, 255}, -3));
                text(560, 226, "\\m/", marker(38, {150, 152, 166, 255}, -14));
                star(1210, 120, 18, 12, {255, 198, 44, 255}, {18, 18, 22, 255});
                star(40, 560, 13, -8, {226, 32, 44, 255}, {18, 18, 22, 255});
                burnedCd(930, 360, 200, ui);
                tape(930, 610, 230, 56, -3);
                {
                    auto s = marker(26, ink::marker, -3);
                    s.align = Align::Center;
                    text(930, 592, tr(entries.size() == 1 ? "{} SONG" : "{} SONGS", entries.size()), s);
                }
                menu(330, 364, 66, {tr("QUICKPLAY"), tr("DOWNLOAD SONGS"), tr("OPTIONS"), tr("QUIT")}, mainRow, ui, 400, 32);
                static const char *const blurbs[] = {"pick a song from your library", "grab charts from chorus encore",
                                                     "controls, calibration and gameplay", "back to the homebrew menu"};
                auto blurb = body(18, ink::dim);
                blurb.align = Align::Center;
                text(330, 620, tr(blurbs[mainRow]), blurb);
                if (!message.empty())
                    text(60, 644, message, marker(22, messageInk(), -1));
                hints({{glyphAccept, tr("SELECT")}, {glyphBack, tr("QUIT")}});
                {
                    auto version = body(15, ink::faint);
                    version.align = Align::Right;
                    text(1236, 684, "v" SWITCH_HERO_VERSION, version);
                }
            } else if (screen == Screen::Select && song) {
                wall(ui, ink::crt);
                const bool askingPart = selectStep == 0;
                text(58, 24, tr(askingPart ? "SELECT INSTRUMENT" : "SELECT DIFFICULTY"), stencil(60, ink::chrome, {116, 122, 138, 255}));
                {
                    auto sub = marker(26, {198, 200, 212, 255}, -2);
                    sub.maxWidth = 760;
                    text(72, 104, plainTitle(song->name) + (askingPart || parts.size() < 2 ? "" : "  /  " + tr(selectedPart())),
                         sub);
                }
                photo(albumArt, 250, 380, 250, -7 + hash01(uint32_t(selected) * 977) * 5);
                burnedCd(372, 350, 142, ui);
                {
                    auto name = marker(28, {226, 228, 236, 255}, -2);
                    name.align = Align::Center, name.maxWidth = 460;
                    text(300, 540, plainTitle(song->name), name);
                    auto by = body(19, ink::dim);
                    by.align = Align::Center, by.maxWidth = 460;
                    text(300, 584, song->artist, by);
                }
                plate(600, 150, 620, 480);
                // One row per choice: the selected one is a strip of tape with the
                // name in Sharpie, the rest stamped on the plate.
                auto row = [&](float cy, bool on, bool live, const std::string &label, int i) {
                    const float angle = (hash01(uint32_t(i) * 131 + 7) - .5f) * 2.5f;
                    if (on) {
                        glow(910, cy, 700, 120, alpha(ink::acid, .22f));
                        tape(910, cy, 580, 78, angle);
                        text(660, cy - 26, label, marker(38, ink::marker, angle));
                        text(616 + 3 * std::sin(float(ui) * 6), cy - 30, ">", marker(44, ink::acid, angle));
                    } else
                        text(664, cy - 20, label, stencil(32, live ? ink::chrome : SDL_Color{70, 72, 82, 255},
                                                          live ? ink::steel : SDL_Color{50, 52, 60, 255}));
                };
                const char *const *letters = difficultyLetters();
                if (askingPart) {
                    const float spacing = parts.size() > 4 ? 88 : 100;
                    for (int i = 0; i < int(parts.size()); ++i) {
                        const float cy = 214 + i * spacing;
                        const bool on = i == partRow;
                        row(cy, on, true, tr(parts[size_t(i)]), i);
                        for (int d = 0; d < 4; ++d) {
                            const bool has = findTrack(*song, parts[size_t(i)], d) >= 0;
                            const float lx = 1044 + d * 36;
                            disc(lx, cy, 14, 14, has ? SDL_Color{255, 196, 40, 255} : SDL_Color{40, 40, 48, 255},
                                 has ? SDL_Color{160, 96, 10, 255} : SDL_Color{26, 26, 32, 255}, 20);
                            auto l = body(15, has ? ink::marker : ink::faint);
                            l.align = Align::Center;
                            text(lx, cy - 9, letters[d], l);
                        }
                    }
                } else {
                    for (int d = 0; d < 4; ++d) {
                        const float cy = 222 + d * 108;
                        const int t = findTrack(*song, selectedPart(), d);
                        const bool on = d == diffRow, live = t >= 0;
                        row(cy, on, live, tr(difficultyName(d)), d);
                        for (int i = 0; i < 4; ++i)
                            star(1060 + i * 34, cy - (on ? 8 : 4), 13, float(i) * 7,
                                 !live ? SDL_Color{40, 40, 48, 255}
                                 : i <= d ? SDL_Color{255, 196, 40, 255} : SDL_Color{62, 62, 72, 255},
                                 {18, 18, 22, 255});
                        if (live)
                            if (const auto *best = scores.find(song->folder.filename().string(), selectedPart(), d))
                                bestLine(666, cy + 16, *best, on ? SDL_Color{50, 48, 56, 255} : ink::dim);
                        auto count = body(16, on ? SDL_Color{60, 58, 66, 255} : live ? ink::dim : ink::faint);
                        count.align = Align::Right;
                        text(1150, cy + 12, live ? tr("{} notes", song->tracks[size_t(t)].notes.size())
                                                 : std::string(tr("not charted")),
                             count);
                    }
                }
                if (settings.noFail) {
                    tape(1150, 150, 170, 46, 7);
                    auto nf = marker(24, ink::marker, 7);
                    nf.align = Align::Center;
                    text(1150, 134, tr("NO FAIL"), nf);
                }
                if (!message.empty())
                    text(600, 640, message, marker(22, messageInk(), -1));
                hints({{glyphAccept, tr(askingPart ? "SELECT" : "PLAY")},
                       {glyphBack, tr("BACK")},
                       {settings.wiiGuitar ? fretGlyph(3) : "X", tr(settings.noFail ? "NO FAIL: ON" : "NO FAIL: OFF")}});
                auto mode = body(16, ink::dim);
                mode.align = Align::Right;
                text(1222, 680, tr(settings.wiiGuitar ? "wii guitar mode" : settings.gamepad ? "press-to-hit mode" : "strum mode"),
                     mode);
            } else if (screen == Screen::Library) {
                wall(ui, ink::crt);
                text(58, 24, "QUICKPLAY", stencil(74, ink::chrome, {116, 122, 138, 255}));
                text(72, 110, "pick a track off the mixtape", marker(26, {198, 200, 212, 255}, -3));
                text(470, 118, "\\m/", marker(34, {150, 152, 166, 255}, -14));
                if (!entries.empty()) {
                    // Current order, on a strip of tape like a label on the case.
                    tape(690, 66, 230, 46, -2.5f);
                    auto by = marker(22, ink::marker, -2.5f);
                    by.align = Align::Center;
                    text(690, 50, tr("by {}", tr(sortNames[settings.sortMode])), by);
                }
                star(1246, 186, 16, 12, {255, 198, 44, 255}, {18, 18, 22, 255});
                star(28, 322, 13, -8, {226, 32, 44, 255}, {18, 18, 22, 255});
                tape(1128, 74, 210, 54, 3.5f);
                {
                    auto s = marker(26, ink::marker, 3.5f);
                    s.align = Align::Center;
                    text(1128, 58, tr(entries.size() == 1 ? "{} SONG" : "{} SONGS", entries.size()), s);
                }
                if (entries.empty()) {
                    plate(230, 250, 820, 230);
                    text(640, 285, tr("NO SONGS FOUND"), [] { auto s = stencil(52); s.align = Align::Center; return s; }());
                    auto line1 = body(20, ink::dim);
                    line1.align = Align::Center;
                    text(640, 360, tr("Copy extracted Clone Hero song folders into"), line1);
                    auto path = marker(24, ink::acid, -1);
                    path.align = Align::Center;
                    text(640, 395, root.string(), path);
                    text(640, 440, tr("Each folder needs notes.chart or notes.mid plus audio"), line1);
                    auto line2 = body(20, ink::white);
                    line2.align = Align::Center;
                    text(640, 468, tr("or press + to download charts"), line2);
                } else {
                    const int rows = 6;
                    int first = std::clamp(int(selected) - rows / 2, 0, std::max(0, int(entries.size()) - rows));
                    for (int row = 0; row < rows && first + row < int(entries.size()); ++row) {
                        const int index = first + row;
                        const bool on = size_t(index) == selected;
                        const float cy = 228 + row * 72, cx = 380 + (on ? 22 : 0);
                        const float angle = (hash01(uint32_t(index) * 61) - .5f) * 3;
                        if (on)
                            glow(cx, cy, 760, 130, alpha(ink::acid, .22f));
                        tape(cx, cy, 620, 66, angle, on ? 1.0f : .66f);
                        const int best = scores.bestStars(entries[index].folder.filename().string());
                        if (best >= 0)
                            for (int i = 0; i < 5; ++i)
                                star(cx + 190 + i * 20, cy - 16, 8, angle,
                                     i < best ? SDL_Color{255, 196, 40, 255} : SDL_Color{96, 96, 104, 255},
                                     {18, 18, 22, 255});
                        auto name = marker(30, ink::marker, angle);
                        name.maxWidth = best >= 0 ? 460 : 520;
                        text(cx - 286, cy - 36, plainTitle(entries[index].name), name);
                        auto sub = body(17, {58, 58, 64, 255});
                        sub.angle = angle, sub.maxWidth = 520;
                        text(cx - 284, cy + 4, entries[index].error.empty() ? entries[index].artist : tr("UNSUPPORTED SONG"), sub);
                        if (on) {
                            text(cx - 340, cy - 34, ">", marker(48, ink::acid, angle));
                            const float wobble = 1 + .04f * std::sin(float(ui) * 4);
                            thickLine(cx - 286, cy + 33, cx - 286 + 300 * wobble, cy + 36, 3, alpha(ink::acid, .85f));
                        }
                    }
                    if (first > 0)
                        text(380, 176, "^", marker(28, ink::faint));
                    if (first + rows < int(entries.size()))
                        text(380, 632, "v", marker(28, ink::faint));
                    {
                        auto pos = marker(22, ink::faint, -2);
                        pos.align = Align::Right;
                        text(692, 636, std::to_string(selected + 1) + " / " + std::to_string(entries.size()), pos);
                    }
                    // Cover art tucked behind the disc, like a sleeve in the case.
                    photo(albumArt, 878, 286, 236, -9 + hash01(uint32_t(selected) * 977) * 7);
                    burnedCd(1010, 262, 150, ui);
                    {
                        // The disc is labelled from the list, so it never waits on the chart.
                        auto disc = marker(28, ink::marker, -8);
                        disc.align = Align::Center, disc.maxWidth = 210;
                        text(985, 158, plainTitle(entries[selected].name), disc);
                        auto sub = marker(20, {70, 60, 80, 255}, -8);
                        sub.align = Align::Center, sub.maxWidth = 200;
                        text(985, 196, entries[selected].artist, sub);
                    }
                    if (!song && loadInFlight()) {
                        plate(812, 520, 350, 108);
                        const int dots = int(std::fmod(ui * 3, 4.0));
                        text(838, 560, tr("reading chart") + std::string(size_t(dots), '.'), body(18, ink::dim));
                    }
                    if (song) {
                        // What the chart offers: each part with its difficulties,
                        // lit when charted, as the setup screen will ask for them.
                        const auto songPartList = songParts(*song);
                        const int shownParts = std::min(3, int(songPartList.size()));
                        const float plateH = 100 + shownParts * 38;
                        plate(812, 628 - plateH, 350, plateH);
                        float py = 628 - plateH + 24;
                        const char *const *letters = difficultyLetters();
                        for (int i = 0; i < shownParts; ++i, py += 38) {
                            const auto &part = songPartList[size_t(i)];
                            text(838, py, tr(part), stencil(22));
                            for (int d = 0; d < 4; ++d) {
                                const bool has = findTrack(*song, part, d) >= 0;
                                const float lx = 1010 + d * 34;
                                look::disc(lx + 12, py + 13, 14, 14, has ? SDL_Color{255, 196, 40, 255} : SDL_Color{40, 40, 48, 255},
                                     has ? SDL_Color{160, 96, 10, 255} : SDL_Color{26, 26, 32, 255}, 20);
                                auto l = body(15, has ? ink::marker : ink::faint);
                                l.align = Align::Center;
                                text(lx + 12, py + 4, letters[d], l);
                            }
                        }
                        if (int(songPartList.size()) > shownParts)
                            text(838, py - 6, tr("+ {} more", songPartList.size() - size_t(shownParts)),
                                 body(15, ink::dim));
                        text(838, py + 2, timeText(song->duration) + "   " +
                                              tr(song->audio.size() == 1 ? "{} STEM" : "{} STEMS", song->audio.size()),
                             body(17, ink::dim));
                        if (!song->warnings.empty()) {
                            auto w = body(14, ink::blood);
                            w.align = Align::Right;
                            text(1140, py + 4, tr("chart warnings"), w);
                        }
                        // The best on the part and difficulty the setup screen will open on.
                        std::string part = songPartList.front();
                        for (const auto &p : songPartList)
                            if (partIndex(p) == settings.part)
                                part = p;
                        const int diff = nearestDifficulty(*song, part, settings.difficulty);
                        if (const auto *best = scores.find(song->folder.filename().string(), part, diff))
                            bestLine(838, py + 32, *best, ink::white, 16);
                        else
                            text(838, py + 32, tr("not played on {} {}", tr(part), tr(difficultyName(diff))),
                                 body(16, ink::faint));
                    }
                }
                if (!message.empty())
                    text(60, 644, message, marker(22, messageInk(), -1));
                // The group a jump landed on, or the new order, stamped over the list.
                if (const float pop = decay(ui - jumpShownAt, .7); pop > 0 && !confirmDelete) {
                    auto big = stencil(jumpLabel.size() > 2 ? 90.0f : 150.0f, ink::chrome, {116, 122, 138, 255});
                    big.align = Align::Center, big.glow = alpha(ink::blood, .8f * pop), big.glowSpread = 1.25f;
                    big.top = alpha(big.top, std::min(1.0f, pop * 2)), big.bottom = alpha(big.bottom, std::min(1.0f, pop * 2));
                    big.outline = alpha(big.outline, std::min(1.0f, pop * 2));
                    text(380, 330 - 30 * (1 - pop), jumpLabel, big);
                }
                if (confirmDelete && selected < entries.size()) {
                    // Deleting is permanent, so it says exactly what goes and
                    // starts on the safe choice.
                    rect(0, 0, W, H, {5, 5, 8, 220});
                    plate(290, 150, 700, 420);
                    auto head = stencil(46, ink::chrome, {116, 122, 138, 255});
                    head.align = Align::Center, head.glow = alpha(ink::blood, .8f), head.glowSpread = 1.2f;
                    text(640, 176, tr("DELETE SONG?"), head);
                    tape(640, 276, 560, 58, -1.2f);
                    auto name = marker(28, ink::marker, -1.2f);
                    name.align = Align::Center, name.maxWidth = 500;
                    text(640, 258, plainTitle(entries[selected].name), name);
                    auto line = body(18, ink::dim);
                    line.align = Align::Center, line.maxWidth = 620;
                    text(640, 322, tr("Removes this folder and everything in it from the SD card."), line);
                    text(640, 348, tr("This can't be undone."), line);
                    auto path = body(15, ink::faint);
                    path.align = Align::Center, path.maxWidth = 620;
                    text(640, 378, entries[selected].folder.filename().string(), path);
                    menu(640, 448, 62, {tr("KEEP IT"), tr("DELETE")}, deleteRow, ui, 360, 30);
                    hints({{glyphAccept, tr("CONFIRM")}, {glyphBack, tr("CANCEL")}});
                } else
                    hints({{glyphAccept, tr("SELECT SONG")},
                           {glyphBack, tr("BACK")},
                           {glyphAlt, tr("OPTIONS")},
                           {"+", tr("DOWNLOAD")},
                           {settings.wiiGuitar ? fretGlyph(3) : "X", tr("DELETE")},
                           {settings.wiiGuitar ? fretGlyph(4) : "ZR", tr("SORT")}});
            } else if (screen == Screen::Settings) {
                wall(ui, ink::crt);
                text(58, 24, tr("OPTIONS"), stencil(78, ink::chrome, {116, 122, 138, 255}));
                text(72, 118, tr(optionsPage < 0 ? "tune your rig" : optionPages[size_t(optionsPage)].first),
                     marker(26, {198, 200, 212, 255}, -3));
                plate(60, 166, 1160, 462);
                // A plate row: LED, name on the left, value on the right.
                auto optionRow = [&](float y, bool on, const std::string &label, const std::string &value,
                                     const std::string &note, bool arrows, const std::string &glyph = "") {
                    if (on) {
                        rect(80, y - 10, 1120, 50, {0, 0, 0, 120});
                        glow(98, y + 14, 34, 34, alpha(ink::acid, .8f));
                    }
                    look::disc(98, y + 14, 6, 6, on ? ink::acid : SDL_Color{44, 46, 54, 255},
                               on ? mix(ink::acid, SDL_Color{0, 0, 0, 255}, .45f) : SDL_Color{26, 26, 32, 255});
                    text(126, y, label, body(26, on ? ink::white : SDL_Color{182, 186, 198, 255}));
                    if (!note.empty())
                        text(126 + measure(label, Face::Body, 26) + 24, y + 7, note, body(17, ink::faint));
                    auto v = marker(28, on ? ink::acid : SDL_Color{214, 216, 226, 255}, -1);
                    v.align = Align::Right;
                    // Arrows mark the values that left/right changes.
                    const float right = arrows ? 1146 : 1180;
                    text(right, y - 5, value, v);
                    if (!glyph.empty())
                        hintButton(right - measure(value, Face::Marker, 28) - 40, y - 1, glyph, "");
                    if (arrows) {
                        auto arrow = body(22, on ? ink::acid : ink::faint);
                        arrow.align = Align::Center;
                        text(right - measure(value, Face::Marker, 28) - 24, y, "<", arrow);
                        text(1170, y, ">", arrow);
                    }
                };
                std::string help;
                if (optionsPage < 0) {
                    for (int i = 0; i <= int(optionPages.size()); ++i) {
                        const bool back = i == int(optionPages.size());
                        optionRow(196 + i * 54, i == optionsTop, tr(back ? "BACK" : optionPages[size_t(i)].first), "",
                                  back ? "" : tr(optionPages[size_t(i)].second), false);
                    }
                    help = tr(optionsTop < int(optionPages.size()) ? "Options save when you leave this screen."
                                                                   : "Save and go back.");
                } else {
                    const auto rows = optionRows(optionsPage, settings, confirmReset);
                    const float spacing = rows.size() > 5 ? 56 : 70;
                    for (int i = 0; i < int(rows.size()); ++i) {
                        const auto &r = rows[size_t(i)];
                        const bool on = i == settingRow;
                        const bool waiting = r.id == Opt::Fret && remapping == r.fret;
                        optionRow(192 + i * spacing, on, r.label, waiting ? tr("PRESS A BUTTON...") : r.value, "",
                                  r.adjust, waiting ? "" : r.glyph);
                    }
                    help = rows[size_t(std::clamp(settingRow, 0, int(rows.size()) - 1))].help;
                }
                if (remapping >= 0)
                    help = tr("Press the button or trigger for this fret. Minus cancels.");
                // Help for the selected row, engraved along the bottom of the plate.
                rect(84, 566, 1112, 1, {0, 0, 0, 150});
                rect(84, 567, 1112, 1, {176, 180, 192, 42});
                if (!message.empty())
                    text(98, 578, message, marker(24, messageInk(), -1));
                else {
                    auto h = body(20, {206, 208, 218, 255});
                    h.maxWidth = 1090;
                    text(98, 582, help, h);
                }
                if (diagnostics) {
                    // Live input, so a silent guitar can be told apart from a
                    // wrong mapping without guessing.
                    rect(0, 0, W, H, {5, 5, 8, 226});
                    plate(150, 120, 980, 470);
                    auto head = stencil(44);
                    head.align = Align::Center;
                    text(640, 140, tr("CONTROLLER TEST"), head);
                    const auto probe = controller.probe();
                    const uint64_t normalized = buttons;
                    auto lines = std::vector<std::pair<std::string, std::string>>{
                        {tr("normal controller"), tr(probe.standard ? "connected" : "not connected")},
                        {tr("player one (guitar)"), tr(probe.guitar ? "connected" : "not connected")},
                        {tr("guitar mode"), tr(settings.wiiGuitar ? "on" : "off")},
                    };
#ifdef __SWITCH__
                    char styles[64];
                    std::snprintf(styles, sizeof(styles), "0x%08x / 0x%08x", probe.standardStyle, probe.guitarStyle);
                    lines.push_back({tr("styles (normal/player one)"), styles});
                    char raw[64];
                    std::snprintf(raw, sizeof(raw), "0x%08llx / 0x%08llx", (unsigned long long)probe.standardRaw,
                                  (unsigned long long)probe.guitarRaw);
                    lines.push_back({tr("raw buttons"), raw});
#else
                    lines.push_back({tr("controller"), probe.name});
#endif
                    std::string pressed;
                    for (int b = 0; b <= 33; ++b)
                        if (normalized & bit(b))
                            pressed += (pressed.empty() ? "" : " ") + bindingName(b);
                    lines.push_back({tr("buttons seen by the game"), pressed.empty() ? tr("none") : pressed});
                    const uint8_t testFrets = settings.wiiGuitar ? wiiGuitarFrets(normalized) : frets;
                    std::string fretText;
                    const char *fretNames[] = {"green", "red", "yellow", "blue", "orange"};
                    for (int i = 0; i < 5; ++i)
                        if (testFrets & (1 << i))
                            fretText += (fretText.empty() ? "" : " ") + std::string(tr(fretNames[i]));
                    lines.push_back({tr("frets"), fretText.empty() ? tr("none") : fretText});
                    lines.push_back({tr("strum"), tr((normalized & (bit(11) | bit(12))) ? "yes" : "no")});
                    for (size_t i = 0; i < lines.size(); ++i) {
                        const float y = 210 + i * 46;
                        text(200, y, lines[i].first, body(22, ink::dim));
                        auto value = marker(26, ink::acid, -1);
                        value.align = Align::Right;
                        text(1080, y - 4, lines[i].second, value);
                    }
                    auto hint = body(18, ink::faint);
                    hint.align = Align::Center;
                    text(640, 556, tr("Press the guitar's frets and strum. Nothing here means the guitar is not "
                                      "reaching the game."), hint);
                    // B is one of the buttons being tested, so Minus closes the panel.
                    hintButton(556, 620, settings.wiiGuitar ? glyphBack : "-", tr("CLOSE"));
                } else if (optionsPage < 0)
                    hints({{glyphAccept, tr("OPEN")}, {glyphBack, tr("SAVE & BACK")}});
                else
                    hints({{glyphAccept, tr("SELECT")}, {"<>", tr("CHANGE")}, {glyphBack, tr("BACK")}});
            } else if (screen == Screen::Download) {
                wall(ui, ink::crt);
                text(58, 24, tr("DOWNLOAD"), stencil(78, ink::chrome, {116, 122, 138, 255}));
                text(72, 118, tr("charts from chorus encore"), marker(26, {198, 200, 212, 255}, -3));
                // Search field.
                rect(60, 160, 1160, 52, {0, 0, 0, 150});
                rect(60, 211, 1160, 1, {176, 180, 192, 42});
                {
                    const bool empty = query.empty();
                    std::string shown = empty && !typing ? tr("newest charts - press Y to search") : query;
                    if (typing && std::fmod(ui, 1.0) < .55)
                        shown += "_";
                    auto field = body(24, empty && !typing ? ink::faint : ink::white);
                    field.maxWidth = 1100;
                    text(84, 172, shown, field);
                }
                // The results on the left; everything known about the highlighted
                // chart on the right, so it can be judged before it is fetched.
                plate(60, 226, 644, 380);
                const auto &results = downloads.results;
                const int rows = 6;
                const int first =
                    std::clamp(int(downloadRow) - rows / 2, 0, std::max(0, int(results.size()) - rows));
                // Intensity: six pips, as Clone Hero rates a part. -1 is unrated.
                auto pips = [](float x, float y, int intensity, float r, float step) {
                    for (int p = 0; p < 6; ++p) {
                        const bool lit = p < intensity;
                        disc(x + p * step, y, r, r, lit ? SDL_Color{255, 198, 44, 255} : SDL_Color{70, 72, 84, 255},
                             lit ? SDL_Color{150, 90, 10, 255} : SDL_Color{38, 40, 48, 255}, 12);
                    }
                    if (intensity < 0)
                        text(x + 6 * step - 2, y - 11, "?", body(16, ink::faint));
                };
                for (int i = 0; i < rows && first + i < int(results.size()); ++i) {
                    const auto &c = results[size_t(first + i)];
                    const bool on = size_t(first + i) == downloadRow;
                    const bool have = size_t(first + i) < owned.size() && owned[size_t(first + i)];
                    const float y = 246 + i * 58;
                    if (on) {
                        rect(80, y - 6, 604, 54, {0, 0, 0, 120});
                        glow(98, y + 20, 34, 34, alpha(ink::acid, .8f));
                    }
                    disc(98, y + 20, 6, 6, on ? ink::acid : SDL_Color{44, 46, 54, 255},
                         on ? mix(ink::acid, SDL_Color{0, 0, 0, 255}, .45f) : SDL_Color{26, 26, 32, 255});
                    auto name = body(22, have ? ink::dim : on ? ink::white : SDL_Color{200, 204, 214, 255});
                    name.maxWidth = 420;
                    text(122, y, c.name, name);
                    auto by = body(15, ink::dim);
                    by.maxWidth = 420;
                    text(122, y + 26, c.artist, by);
                    pips(566, y + 14, c.guitarDifficulty, 4.5f, 13);
                    if (have) {
                        auto tag = marker(18, ink::acid, -2);
                        tag.align = Align::Right;
                        text(684, y + 22, tr("in library"), tag);
                    }
                }
                plate(720, 226, 500, 380);
                if (downloadRow < results.size()) {
                    const auto &c = results[downloadRow];
                    // Cover, or a blank disc while it loads or when there is none.
                    if (downloadArt)
                        photo(downloadArt, 800, 312, 118, -4);
                    else
                        burnedCd(800, 312, 60, ui);
                    const float tx = 878, tw = 322;
                    auto title = marker(26, {232, 234, 240, 255}, -1.5f);
                    title.maxWidth = tw;
                    text(tx, 244, c.name, title);
                    auto artist = body(18, ink::white);
                    artist.maxWidth = tw;
                    text(tx, 284, c.artist, artist);
                    auto small = body(15, ink::dim);
                    small.maxWidth = tw;
                    std::string album = c.album;
                    if (!c.year.empty())
                        album += album.empty() ? c.year : " (" + c.year + ")";
                    text(tx, 310, album, small);
                    std::string about = c.genre;
                    if (c.seconds > 0)
                        about += (about.empty() ? "" : "  /  ") + timeText(c.seconds);
                    text(tx, 330, about, small);
                    if (!c.charter.empty())
                        text(tx, 350, tr("charted by {}", c.charter), small);

                    // Parts: name, intensity, then notes per difficulty (a dash
                    // where the part has no chart for it).
                    const float colX[4] = {1026, 1074, 1122, 1170};
                    const char *const *letters = difficultyLetters();
                    rect(740, 382, 460, 1, {0, 0, 0, 150});
                    rect(740, 383, 460, 1, {176, 180, 192, 42});
                    text(744, 392, tr("part"), body(13, ink::faint));
                    text(896, 392, tr("intensity"), body(13, ink::faint));
                    for (int d = 0; d < 4; ++d) {
                        disc(colX[d], 400, 10, 10, {255, 196, 40, 255}, {160, 96, 10, 255}, 16);
                        auto l = body(12, ink::marker);
                        l.align = Align::Center;
                        text(colX[d], 393, letters[d], l);
                    }
                    const float rowH = c.parts.size() > 4 ? 28 : 32;
                    float py = 420;
                    for (const auto &part : c.parts) {
                        auto label = stencil(19);
                        label.maxWidth = 146;
                        text(744, py, tr(part.instrument), label);
                        pips(902, py + 11, part.intensity, 5, 13);
                        for (int d = 0; d < 4; ++d) {
                            auto n = body(14, part.charted(d) ? ink::white : ink::faint);
                            n.align = Align::Center;
                            text(colX[d], py + 3, part.charted(d) ? std::to_string(part.notes[size_t(d)]) : "-", n);
                        }
                        py += rowH;
                    }
                    if (c.parts.empty())
                        text(744, py, tr("no part details"), body(15, ink::faint));

                    // What the chart asks of the player, and what else it carries.
                    std::vector<std::string> features;
                    for (const auto &part : c.parts)
                        if (part.instrument == "Guitar" && part.peakNps[3] > 0)
                            features.push_back(tr("peak {} notes/s", int(std::lround(part.peakNps[3]))));
                    if (c.solos)
                        features.push_back(tr("solos"));
                    if (c.openNotes)
                        features.push_back(tr("open notes"));
                    if (c.tapNotes)
                        features.push_back(tr("tap notes"));
                    std::string featureLine;
                    for (const auto &f : features)
                        featureLine += (featureLine.empty() ? "" : "   ") + f;
                    auto featureStyle = body(15, ink::acid);
                    featureStyle.maxWidth = 460;
                    text(744, 546, featureLine, featureStyle);
                    if (!c.otherParts.empty()) {
                        std::string others;
                        for (const auto &o : c.otherParts)
                            others += (others.empty() ? "" : ", ") + tr(o);
                        auto otherStyle = body(14, ink::faint);
                        otherStyle.maxWidth = 460;
                        text(744, 568, tr("also has {} (not playable)", others), otherStyle);
                    }
                }
                if (results.empty() && downloads.state == Downloader::State::Idle && downloads.error.empty()) {
                    auto none = body(22, ink::dim);
                    none.align = Align::Center;
                    text(382, 390, downloads.page ? tr("No guitar charts matched.") : "", none);
                }
                // Status line: progress, errors, or what the list holds.
                if (downloads.state == Downloader::State::Downloading) {
                    ledMeter(66, 628, 420, 14, 28, downloads.progress, true, ui);
                    char line[96];
                    std::snprintf(line, sizeof line, "%s   %.1f MB", downloads.status.c_str(), downloads.megabytes);
                    text(510, 622, line, body(18, ink::dim));
                } else if (!downloads.error.empty())
                    text(62, 620, tr(downloads.error), marker(22, ink::blood, -1));
                else if (downloads.state == Downloader::State::Searching)
                    text(62, 622, tr("searching..."), body(18, ink::dim));
                else if (downloads.downloads != downloadsSeen && !downloads.lastFolder.empty())
                    text(62, 620, tr("added {}", downloads.lastFolder), marker(22, ink::acid, -1));
                else if (downloads.page)
                    text(62, 622, tr(downloads.found == 1 ? "{} guitar chart" : "{} guitar charts", downloads.found),
                         body(18, ink::faint));
                if (typing) {
                    hints({{"ENT", tr("SEARCH")}, {"ESC", tr("CANCEL")}}); // desktop typing only
                } else {
                    hints({{glyphAccept, "DOWNLOAD"},
                           {glyphAlt, "SEARCH"},
                           {glyphBack, downloads.state == Downloader::State::Downloading ? "CANCEL" : "BACK"}});
                }
            } else if (screen == Screen::Calibrate) {
                const double time = calibrationTime();
                const bool done = calibration.done();
                wall(ui, ink::crt);
                if (calibratingVideo && !done)
                    highway(calibrationSong, *calibrationSession, settings, time - settings.videoMs / 1000, frets, ui);
                text(46, 24, tr("CALIBRATE"), stencil(56, ink::chrome, {116, 122, 138, 255}));
                text(52, 92, tr(calibratingVideo ? "visual offset" : "audio offset"), marker(24, {198, 200, 212, 255}, -3));
                // Taps panel, where the score plate sits in a song.
                plate(38, 158, 252, 330);
                text(64, 184, tr("TAPS"), stencil(22, ink::steel, {80, 84, 96, 255}));
                text(64, 212,
                     std::to_string(calibration.errors.size()) + " / " + std::to_string(calibration.needed),
                     stencil(52));
                // Each tap's error on a +-150 ms rule, so a drifting or uneven
                // hand is visible, not just a number.
                const float ruleX = 64, ruleW = 200, ruleY = 330, rangeMs = 150;
                rect(ruleX, ruleY, ruleW, 2, {70, 72, 84, 255});
                rect(ruleX + ruleW / 2 - 1, ruleY - 14, 2, 30, {110, 114, 128, 255});
                text(ruleX, ruleY + 22, tr("early"), body(14, ink::faint));
                auto lateLabel = body(14, ink::faint);
                lateLabel.align = Align::Right;
                text(ruleX + ruleW, ruleY + 22, tr("late"), lateLabel);
                for (size_t i = 0; i < calibration.errors.size(); ++i) {
                    const float ms = float(std::clamp(calibration.errors[i] * 1000, -double(rangeMs), double(rangeMs)));
                    const float x = ruleX + ruleW / 2 + ms / rangeMs * ruleW / 2;
                    const bool latest = i + 1 == calibration.errors.size();
                    if (latest)
                        glow(x, ruleY + 1, 30, 30, alpha(ink::acid, .7f));
                    rect(x - 1.5f, ruleY - 10, 3, 22, latest ? ink::acid : alpha(ink::acid, .45f));
                }
                if (!calibration.errors.empty()) {
                    char median[16];
                    std::snprintf(median, sizeof median, "%+d", int(std::round(calibration.median() * 1000)));
                    text(64, 400, tr("median {} ms", median), body(20, ink::dim));
                }
                if (!done) {
                    if (!calibratingVideo) {
                        // Nothing on screen moves with the beat: the ears alone set the taps.
                        auto big = stencil(64);
                        big.align = Align::Center;
                        text(700, 280, tr(time < calibration.warmup ? "LISTEN" : "TAP"), big);
                        auto hint = body(22, ink::dim);
                        hint.align = Align::Center;
                        text(700, 370, tr("Tap any fret or strum on every click."), hint);
                        text(700, 402, tr("Close your eyes if it helps."), hint);
                    } else {
                        auto hint = body(20, ink::dim);
                        hint.align = Align::Center;
                        text(1100, 250, tr("Clicks are muted."), hint);
                        text(1100, 280, tr("Tap as each note"), hint);
                        text(1100, 306, tr("crosses the line."), hint);
                    }
                    button(58, 676, "-", tr("CANCEL"));
                } else {
                    rect(0, 0, W, H, {5, 5, 8, 150});
                    plate(390, 190, 620, 330);
                    auto head = stencil(40);
                    head.align = Align::Center;
                    text(700, 214, tr(calibratingVideo ? "VISUAL OFFSET" : "AUDIO / INPUT OFFSET"), head);
                    const double was = calibratingVideo ? settings.videoMs : settings.audioMs;
                    auto value = stencil(72, ink::acid, mix(ink::acid, SDL_Color{20, 40, 10, 255}, .5f));
                    value.align = Align::Center;
                    text(700, 280, std::to_string(int(calibrationResult())) + " ms", value);
                    auto sub = body(20, ink::dim);
                    sub.align = Align::Center;
                    text(700, 380, tr("was {} ms", int(was)), sub);
                    // A wide spread means the median is a guess; say so rather than
                    // quietly saving it.
                    if (calibration.spread() > .025) {
                        auto warn = marker(22, ink::blood, -1);
                        warn.align = Align::Center;
                        text(700, 420, tr("taps were uneven - retry for a steadier read"), warn);
                    }
                    hints({{glyphAccept, tr("KEEP")}, {glyphAlt, tr("RETRY")}, {glyphBack, tr("CANCEL")}}, 430, 470);
                }
            } else if (session && song) {
                double time = screen == Screen::Playing ? songTime() : frozen;
                wall(ui, session->powerActive ? ink::power : ink::crt);
                highway(*song, *session, settings, time - settings.videoMs / 1000, frets, ui);
                // Call the timing. A hit that the game cannot tell you was tight is a
                // hit that feels the same as one scraped in at the edge of the window,
                // which is why landing notes read as mushy.
                if (session->lastTierAt > -1e8) {
                    const float pop = decay(time - session->lastTierAt, .45);
                    if (pop > 0) {
                        static const char *const names[4] = {"", "GOOD", "GREAT", "PERFECT"};
                        const SDL_Color tint = session->lastTier == 3 ? SDL_Color{255, 214, 84, 255}
                                               : session->lastTier == 2 ? ink::acid
                                                                        : SDL_Color{170, 176, 190, 255};
                        auto s = stencil(30 + 14 * pop, tint, mix(tint, SDL_Color{30, 30, 36, 255}, .5f));
                        s.align = Align::Center;
                        s.glow = alpha(tint, .55f * pop), s.glowSpread = 1.2f;
                        text(640, 432 - 24 * pop, tr(names[session->lastTier]), s);
                        // Anything short of perfect also says which side of the beat you
                        // were on, so the miss is correctable rather than mysterious.
                        if (session->lastTier < 3) {
                            const float span = 150;
                            const float off =
                                std::clamp(float(session->lastError / session->window), -1.0f, 1.0f);
                            rect(640 - span / 2, 494, span, 2, alpha(SDL_Color{120, 126, 142, 255}, pop * .8f));
                            rect(640 + off * span / 2 - 2, 488, 4, 14, alpha(tint, pop));
                            auto l = body(15, alpha(SDL_Color{170, 176, 190, 255}, pop * .9f));
                            l.align = Align::Center;
                            text(640, 506, tr(session->lastError < 0 ? "EARLY" : "LATE"), l);
                        }
                    }
                }
                // Song label on a strip of tape, as if written on the CD case.
                tape(250, 52, 450, 60, -1.5f);
                {
                    auto s = marker(28, ink::marker, -1.5f);
                    s.align = Align::Center, s.maxWidth = 390;
                    text(250, 34, plainTitle(song->name), s);
                }
                text(46, 96, song->artist + "  /  " + tr(session->track->instrument) + " " +
                                 tr(difficultyName(session->track->difficulty)),
                     body(17, ink::dim));
                {
                    auto t = body(19, ink::dim);
                    t.align = Align::Right;
                    text(1236, 30, timeText(audio.position()) + " / " + timeText(audio.duration()), t);
                }
                const float progress = float(std::clamp(audio.position() / std::max(1.0, audio.duration()), 0.0, 1.0));
                rect(960, 66, 276, 3, {60, 62, 74, 255});
                rect(960, 66, 276 * progress, 3, ink::chrome);
                glow(960 + 276 * progress, 67, 26, 26, alpha(ink::blood, .9f));
                // The read-outs are bolted to an amp faceplate rather than floating on
                // the wall. Unanchored text sitting in the dark is what makes a HUD
                // read as debug output with a nice font; a panel gives the numbers a
                // surface, a shadow and a shared light.
                plate(38, 158, 252, 330);
                // Engraved rules, cut into the panel: a dark score with a lit lower
                // lip, the way a stamped line catches light from above.
                auto engrave = [](float y) {
                    rect(62, y, 204, 1, {0, 0, 0, 150});
                    rect(62, y + 1, 204, 1, {176, 180, 192, 42});
                };
                text(62, 186, tr("SCORE"), stencil(20, ink::dim, ink::faint));
                {
                    // Shrink long scores to the plate rather than running off it, and
                    // keep them centred on the same line.
                    const std::string value = std::to_string(int(session->score));
                    const float size = fitSize(value, stencil(62), 204);
                    const float pop = 1 + .12f * decay(ui - scorePopAt, .22);
                    auto s = stencil(size * pop);
                    if (session->powerActive)
                        s.glow = alpha(ink::power, .5f), s.glowSpread = 1.2f;
                    text(58, 208 + (62 - size) * .5f - size * (pop - 1) * .5f, value, s);
                }
                engrave(282);
                text(64, 292, tr("streak"), marker(26, ink::dim, -3));
                text(58, 314, std::to_string(session->combo), stencil(60, session->combo ? ink::acid : ink::faint,
                                                                       session->combo ? SDL_Color{110, 190, 30, 255} : ink::faint));
                engrave(392);
                const int judged = session->hits + session->misses;
                text(62, 412, tr("{}% HIT", judged ? session->hits * 100 / judged : 100), body(24, ink::white));
                // Clear of the panel's bottom screws.
                text(62, 442, tr("{} missed", session->misses), body(17, ink::faint));
                {
                    const float pop = 1 + .3f * decay(ui - multiplierPopAt, .35);
                    const float spin = 9 + 14 * decay(ui - multiplierPopAt, .35);
                    if (session->multiplier() >= 4)
                        glow(1104, 226, 220 * pop, 220 * pop, alpha(session->powerActive ? ink::power : ink::blood, .45f));
                    sticker(1104, 226, 56 * pop, spin, session->powerActive ? ink::power : ink::blood,
                            "x" + std::to_string(session->multiplier()));
                }
                text(940, 372, "STAR POWER", stencil(20, ink::dim, ink::faint));
                if (const float fill = decay(time - session->lastWhammyGain, .25); fill > 0)
                    glow(1056, 414, 300, 80, alpha(ink::power, .5f * fill)); // the bar is filling from a whammy
                ledMeter(940, 404, 232, 20, 12, float(session->power), session->powerActive, ui);
                if (session->powerActive)
                    text(940, 442, tr("BURNING"), marker(24, ink::power, -2));
                else if (session->power >= .5 && std::fmod(ui, 1.0) < .6)
                    text(940, 442, tr(settings.wiiGuitar ? "hit MINUS !" : "hit X !"), marker(24, ink::acid, -3));
                // Rock meter: the tug of war that decides whether the set survives.
                {
                    const bool danger = session->inRed() && !session->noFail;
                    auto label = stencil(18, danger ? ink::blood : ink::dim, danger ? ink::blood : ink::faint);
                    label.align = Align::Center;
                    text(1232, 128, "ROCK", label);
                    rockMeter(1218, 154, 30, 410, float(session->meter), ui, danger);
                    if (session->noFail) {
                        auto safe = marker(20, ink::acid, -4);
                        safe.align = Align::Center;
                        text(1232, 580, tr("no fail"), safe);
                    } else if (danger && std::fmod(ui, .7) < .45) {
                        auto warn = marker(22, ink::blood, -5);
                        warn.align = Align::Center;
                        text(1232, 580, tr("danger!"), warn);
                    }
                    if (danger) // the room goes red as the crowd turns
                        rect(0, 0, W, H, {200, 20, 20, Uint8(10 + 14 * (.5f + .5f * std::sin(float(ui) * 14)))});
                }
                if (session->powerActive) {
                    // Rim light: the stage rig kicks in while star power burns.
                    const float pulse = .6f + .4f * std::sin(float(ui) * 6);
                    glow(0, 360, 620, 1000, alpha(ink::power, .3f * pulse));
                    glow(W, 360, 620, 1000, alpha(ink::power, .3f * pulse));
                }
                if (const float flash = decay(ui - powerFlashAt, .3); flash > 0)
                    rect(0, 0, W, H, {210, 240, 255, Uint8(150 * flash)});
                if (time < 0) {
                    // Each count lands with a thump.
                    const float beat = float(std::ceil(-time) + time); // 0 at the flip, 1 just before
                    const float pop = 1 + .35f * decay(1 - beat, .35);
                    auto s = stencil(150 * pop, ink::chrome, {150, 40, 40, 255});
                    s.align = Align::Center, s.glow = alpha(ink::blood, .85f), s.glowSpread = 1.25f;
                    text(640, 232 - 150 * (pop - 1) * .5f, std::to_string(int(std::ceil(-time))), s);
                    auto ready = marker(30, {206, 208, 218, 255}, -3);
                    ready.align = Align::Center;
                    text(640, 430, tr("get ready"), ready);
                }
                if (calloutAt > 0 && ui - calloutAt < 1.4) {
                    const float t = float((ui - calloutAt) / 1.4);
                    const float pop = 1 + .45f * std::pow(1 - std::min(t * 4, 1.0f), 3.0f);
                    auto s = stencil(52 * pop, ink::acid, {255, 210, 40, 255});
                    s.align = Align::Center, s.glow = alpha(ink::acid, .7f * (1 - t)), s.glowSpread = 1.3f;
                    s.top = alpha(s.top, std::min(1.0f, (1 - t) * 3)), s.bottom = alpha(s.bottom, std::min(1.0f, (1 - t) * 3));
                    text(640, 196 - 30 * t, callout, s);
                }
                // Sits in the corner the vignette darkens most, so it needs to start
                // brighter than ink::faint to stay readable through the grade.
                text(46, 686,
                     tr(settings.wiiGuitar ? "Plus pause    strum with no frets for open notes    Minus star power"
                        : settings.gamepad ? "Plus pause    any fret hits open notes    X star power"
                                           : "Plus pause    strum with no frets for open notes    X star power"),
                     body(16, ink::dim));
                if (screen == Screen::Countdown) {
                    // The frozen board stays visible so the player can see what is coming.
                    rect(0, 0, W, H, {5, 5, 8, 120});
                    const double left = std::max(0.0, 3 - (ui - countdownAt));
                    const float beat = float(std::ceil(left) - left); // 0 at each new number
                    const float pop = 1 + .35f * decay(beat, .35);
                    auto s = stencil(150 * pop, ink::chrome, {150, 40, 40, 255});
                    s.align = Align::Center, s.glow = alpha(ink::blood, .85f), s.glowSpread = 1.25f;
                    text(640, 232 - 150 * (pop - 1) * .5f, std::to_string(std::max(1, int(std::ceil(left)))), s);
                    auto ready = marker(30, {206, 208, 218, 255}, -3);
                    ready.align = Align::Center;
                    text(640, 430, tr("get ready"), ready);
                }
                if (screen == Screen::Paused || screen == Screen::Results) {
                    rect(0, 0, W, H, {5, 5, 8, 238});
                    auto heading = stencil(96, ink::chrome, {116, 122, 138, 255});
                    heading.align = Align::Center, heading.glow = alpha(ink::blood, .8f), heading.glowSpread = 1.2f;
                    const bool failed = session->failed;
                    if (failed)
                        heading.glow = alpha(ink::blood, .95f);
                    text(640, 76, tr(screen == Screen::Paused ? "PAUSED" : failed ? "SONG FAILED" : "SONG COMPLETE"),
                         heading);
                    if (screen == Screen::Results) {
                        const float accuracy = session->state.empty() ? 1 : float(session->hits) / session->state.size();
                        const int stars = starsFor(accuracy, failed);
                        const std::string rank(1, rankFor(accuracy, failed));
                        // Big stamped rank, like a grade scrawled on the CD sleeve.
                        glow(354, 306, 300, 300, alpha(failed ? ink::blood : ink::acid, .3f));
                        auto rankStyle = stencil(190, failed ? SDL_Color{240, 90, 90, 255} : ink::chrome,
                                                 failed ? SDL_Color{120, 20, 20, 255} : SDL_Color{116, 122, 138, 255});
                        rankStyle.align = Align::Center;
                        rankStyle.glow = alpha(failed ? ink::blood : ink::crt, .8f), rankStyle.glowSpread = 1.25f;
                        text(354, 206, rank, rankStyle);
                        auto played = marker(24, {198, 200, 212, 255}, -2);
                        played.align = Align::Center;
                        text(354, 400, tr(session->track->instrument) + "  /  " + tr(difficultyName(session->track->difficulty)),
                             played);
                        if (!failed && session->misses == 0 && session->hits > 0)
                            sticker(470, 336, 40, -12, {255, 196, 40, 255}, "FC");
                        if (!failed && newBest) {
                            glow(1120, 240, 240, 120, alpha(ink::acid, .3f + .15f * std::sin(float(ui) * 6)));
                            tape(1120, 240, 210, 56, 8);
                            auto nb = marker(28, ink::blood, 8);
                            nb.align = Align::Center;
                            text(1120, 222, tr("NEW BEST!"), nb);
                        }
                        if (!failed && previousBest >= 0) {
                            auto was = body(17, ink::dim);
                            was.align = Align::Center;
                            text(1120, 300, tr(newBest ? "was {}" : "best {}", grouped(newBest ? previousBest : std::max(previousBest, 0))),
                                 was);
                        }
                        const std::string value = grouped(int(session->score));
                        const float size = fitSize(value, stencil(84), 440);
                        auto score = stencil(size);
                        score.align = Align::Center;
                        text(840, 200 + (84 - size) * .5f, value, score);
                        auto scoreLabel = stencil(20, ink::dim, ink::faint);
                        scoreLabel.align = Align::Center;
                        text(840, 176, tr("FINAL SCORE"), scoreLabel);
                        for (int i = 0; i < 5; ++i)
                            star(700 + i * 70, 322, 28, (hash01(uint32_t(i) * 7) - .5f) * 16,
                                 i < stars ? SDL_Color{255, 198, 44, 255} : SDL_Color{48, 48, 56, 255}, {14, 14, 18, 255});
                        auto verdict = marker(40, failed ? ink::blood : ink::acid, -4);
                        verdict.align = Align::Center;
                        text(840, 362,
                             tr(failed ? "the crowd is booing"
                                       : accuracy >= .97f ? "flawless!" : accuracy >= .9f ? "shredded it" :
                                         accuracy >= .8f ? "solid set" : accuracy >= .65f ? "not bad" : "keep practicing"),
                             verdict);
                        // Stats scrawled on strips of tape.
                        const std::array<std::pair<std::string, std::string>, 4> stats = {
                            std::pair{std::string(tr("notes hit")), std::to_string(session->hits) + " / " + std::to_string(session->state.size())},
                            {tr("best streak"), std::to_string(session->maxCombo)},
                            // How clean the run was, not just how much of it landed.
                            {tr("perfect / great / good"), std::to_string(session->tierCounts[3]) + " / " +
                                                           std::to_string(session->tierCounts[2]) + " / " +
                                                           std::to_string(session->tierCounts[1])},
                            {tr("accuracy"), std::to_string(int(accuracy * 100)) + "%"}};
                        for (int i = 0; i < 4; ++i) {
                            const float ty = 444 + i * 56, angle = (hash01(uint32_t(i) * 31) - .5f) * 3;
                            tape(840, ty, 520, 50, angle);
                            auto key = marker(24, ink::marker, angle);
                            text(606, ty - 24, stats[i].first, key);
                            auto value = marker(26, {40, 40, 46, 255}, angle);
                            value.align = Align::Right, value.maxWidth = 330;
                            text(1074, ty - 26, stats[i].second, value);
                        }
                    } else {
                        tape(640, 250, 520, 60, -1.2f);
                        auto line = marker(30, ink::marker, -1.2f);
                        line.align = Align::Center, line.maxWidth = 440;
                        text(640, 232, plainTitle(song->name), line);
                        auto held = body(20, ink::dim);
                        held.align = Align::Center;
                        text(640, 312, tr("the amp is still humming"), held);
                    }
                    if (screen == Screen::Paused) {
                        menu(640, 392, 62, {"RESUME", "RESTART", "CHANGE DIFFICULTY", "QUIT TO SONG LIST"}, pauseRow,
                             ui, 460, 32, {audio.error().empty(), true, true, true});
                        hints({{glyphAccept, "SELECT"}, {glyphBack, "RESUME"}});
                    } else {
                        std::vector<std::string> items = {tr("CONTINUE"), tr("RETRY"), tr("CHANGE DIFFICULTY")};
                        if (offsetTip)
                            items.push_back(offsetTipApplied ? tr("TIMING FIXED")
                                                             : tr("FIX TIMING ({} MS)", (offsetTip > 0 ? "+" : "") +
                                                                                            std::to_string(offsetTip)));
                        menu(354, 478, offsetTip ? 52 : 62, items, resultRow, ui, 400, 30,
                             offsetTip ? std::vector<bool>{true, true, true, !offsetTipApplied} : std::vector<bool>{});
                        if (offsetTip) {
                            auto why = body(17, ink::dim);
                            why.align = Align::Center;
                            text(840, 646,
                                 offsetTipApplied ? tr("audio offset now {} ms", int(settings.audioMs))
                                                  : tr(offsetTip > 0 ? "you hit {} ms late on average"
                                                                     : "you hit {} ms early on average",
                                                       std::abs(offsetTip)),
                                 why);
                        }
                        if (ui - screenChangedAt >= resultsLockout) // hints appear once presses count
                            hints({{glyphAccept, tr("SELECT")}, {glyphBack, tr("CONTINUE")}});
                    }
                    if (!message.empty()) {
                        auto m = marker(22, messageInk(), -1);
                        m.align = Align::Center;
                        text(640, 644, message, m);
                    }
                }
            }

            // Quick fade whenever the screen changes.
            if (const float fade = decay(ui - screenChangedAt, .22); fade > 0)
                rect(0, 0, W, H, {0, 0, 0, Uint8(210 * fade)});
            // Grade the finished frame before anything reads it back, so a
            // screenshot shows what the player actually sees.
            grade(ui);
            {
                // Frame timing, summed over each second so it can be read off a TV:
                // the average says how heavy frames are, the worst says whether
                // anything hitched. During a song it adds how far the song clock
                // sits from the audio and how hard it is leaning to close the gap.
                static double lastFrame = now(), windowStart = now(), sum = 0, worst = 0, shownAvg = 0, shownWorst = 0;
                static int frames = 0;
                const double t = now(), frameMs = (t - lastFrame) * 1000;
                lastFrame = t, sum += frameMs, worst = std::max(worst, frameMs), ++frames;
                if (t - windowStart >= 1) {
                    shownAvg = sum / frames, shownWorst = worst;
                    sum = worst = 0, frames = 0, windowStart = t;
                }
                if (timing || settings.timingOverlay) {
                    char line[128];
                    int n = std::snprintf(line, sizeof line, "frame avg %.1f  worst %.1f ms", shownAvg, shownWorst);
                    if (session && screen == Screen::Playing)
                        std::snprintf(line + n, sizeof line - size_t(n), "   drift %+.1f ms  rate %.4f", clock.drift * 1000,
                                      clock.rate);
                    const auto style = body(15, shownWorst > 20 ? ink::blood : ink::acid);
                    rect(474, 8, measure(line, Face::Body, 15) + 20, 26, {0, 0, 0, 190});
                    text(484, 12, line, style);
                }
            }
            if (smoke && ++smokeFrames == 360) {
                if (screen != Screen::Playing || session->hits < 2 || !audio.error().empty())
                    throw std::runtime_error("Virtual controller gameplay smoke failed");
                screenshot(shot);
                std::cout << "Smoke: " << session->hits
                          << " notes hit through SDL virtual controller; screenshot " << shot << '\n';
                running = false;
            }
            SDL_RenderPresent(renderer);
            for (auto *t : retired)
                SDL_DestroyTexture(t);
            retired.clear();
            previousButtons = buttons;
            for (int i = 0; i < SDL_NUM_SCANCODES; ++i)
                previousKeys[i] = keys[i];
            if (smoke) SDL_Delay(16);
        }
        // Shut down in the reverse order things were created: the console fatals
        // if the process returns with the audio or graphics devices still open.
        audio.stop();
        if (!smoke)
            try {
                settings.save(config);
            } catch (const std::exception &e) {
                SDL_Log("Switch Hero: %s", e.what());
            }
        if (virtualPad)
            SDL_JoystickClose(virtualPad);
        controller.close();
        SDL_DestroyTexture(albumArt);
        for (auto *t : retired)
            SDL_DestroyTexture(t);
        look::shutdown();
        SDL_DestroyRenderer(renderer);
        look::renderer = nullptr;
        SDL_DestroyWindow(window);
        SDL_Quit();
#ifdef __SWITCH__
        romfsExit();
#endif
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Switch Hero: " << e.what() << '\n';
        SDL_Quit();
#ifdef __SWITCH__
        romfsExit();
#endif
        return 1;
    }
}
