#include "audio.hpp"
#include "calibration.hpp"
#include "clock.hpp"
#include "downloader.hpp"
#include "game.hpp"
#include "guitar_input.hpp"
#include "look.hpp"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <cstdio>
#include <sstream>
#include <vector>
#ifdef __SWITCH__
#include <switch.h>
#endif

using namespace fret;
using namespace fret::look;
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
    void load(const fs::path &path) {
        std::ifstream f(path);
        std::string k;
        double v;
        while (f >> k >> v) {
            if (!std::isfinite(v))
                continue;
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
          << gamepad << "\nwii_guitar " << wiiGuitar << "\nno_fail " << noFail << '\n';
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
    PadState nx{}, nxGuitar{};
    bool guitarInput = false, guitarConnected = false;
#endif
  public:
    Controller() {
#ifdef __SWITCH__
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&nx);
        padInitialize(&nxGuitar, HidNpadIdType_No1);
#else
        connect();
#endif
    }
    ~Controller() { close(); }
    // Released before SDL_Quit so no SDL object outlives the library.
    void close() {
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
        p.standard = padIsConnected(&nx), p.guitar = padIsConnected(&nxGuitar);
        p.standardStyle = padGetStyleSet(&nx), p.guitarStyle = padGetStyleSet(&nxGuitar);
        p.standardRaw = padGetButtons(&nx), p.guitarRaw = padGetButtons(&nxGuitar);
        p.name = p.guitar ? "player one npad" : "no player-one controller";
#else
        connect();
        p.standard = pad != nullptr;
        p.name = pad ? SDL_GameControllerName(pad) : "no controller";
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
            if (pad && SDL_GameControllerGetButton(pad, SDL_GameControllerButton(b)))
                p.standardRaw |= bit(b);
#endif
        return p;
    }
    bool connected() const {
#ifdef __SWITCH__
        return padIsConnected(guitarInput ? &nxGuitar : &nx);
#else
        return pad && SDL_GameControllerGetAttached(pad);
#endif
    }
    uint64_t read(bool wiiGuitar, bool playing) {
        uint64_t out = 0;
#ifdef __SWITCH__
        padUpdate(&nx);
        padUpdate(&nxGuitar);
        guitarConnected = padIsConnected(&nxGuitar);
        guitarInput = exclusiveGuitarInput(wiiGuitar, playing, guitarConnected);
        // Plus and Minus always answer from the normal controller.
        auto h = controllerButtons(wiiGuitar, playing, guitarConnected, padGetButtons(&nx),
                                   padGetButtons(&nxGuitar), HidNpadButton_Plus | HidNpadButton_Minus);
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
        connect();
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
    auto xx = [&](float lane, float y) { return 640 + (lane / 5 - .5f) * width(y); };
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
        float lane = side ? 5.0f : 0.0f, dir = side ? 1.0f : -1.0f;
        SDL_Color hot = power ? ink::power : SDL_Color{228, 230, 240, 255};
        SDL_Color far{62, 66, 78, 255};
        quad({xx(lane, top), top}, {xx(lane, top) + dir * 4, top}, {xx(lane, edge) + dir * 13, edge},
             {xx(lane, edge), edge}, far, mix(far, {20, 20, 26, 255}, .6f), mix(hot, {30, 32, 40, 255}, .45f), hot);
    }
    // Darken the very front so the fret buttons read against the board.
    quad({xx(0, fretZone), fretZone}, {xx(5, fretZone), fretZone}, {xx(5, edge), edge}, {xx(0, edge), edge},
         {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 190}, {0, 0, 0, 190});

    const auto &notes = session.track->notes;
    std::vector<look::Fire> fires;
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
                SDL_Color c = state.result < 0 ? SDL_Color{110, 112, 124, 255} : power ? ink::power : lanes[size_t(l)];
                float lane = l == 5 ? 2.5f : l + .5f;
                auto tube = [&](float scale, SDL_Color col) {
                    std::vector<SDL_Vertex> v;
                    std::vector<int> idxs;
                    const int steps = 18;
                    for (int j = 0; j <= steps; ++j) {
                        float ty = head + (tail - head) * j / steps;
                        float depth = (ty - top) / (bottom - top);
                        float sway = holding ? std::sin(ty * .07f - float(ui) * 16) * 4 * depth : 0;
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
                    if (age < .38)
                        fires.push_back({x, lane, age, false, seed});
                    if (sustaining)
                        fires.push_back({x, lane, age, true, seed});
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
        GemStyle style = n.phrase >= 0 ? (hammer ? GemStarHopo : GemStar) : hammer ? GemHopo : GemNormal;
        for (int l = 0; l < 5; ++l)
            if (n.mask & (1 << l)) {
                // Contact shadow on the board. Without one the gems look pasted onto
                // the highway instead of standing on it; it costs one ellipse and it
                // is most of what makes them feel like objects.
                const float gw = width(y) / 5 * .82f;
                if (state.result >= 0)
                    disc(xx(l + .5f, y), y + gw * .30f, gw * .42f, gw * .17f, {0, 0, 0, Uint8(120 * appear)},
                         {0, 0, 0, 0}, 20);
                gem(power ? PowerColor : size_t(l), style, xx(l + .5f, y), y, gw,
                    Uint8((state.result < 0 ? 80 : 255) * appear));
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
    for (int l = 0; l < 5; ++l) {
        float x = xx(l + .5f, bottom);
        // A held fret spills its colour back up the board, so the lane you are on
        // is lit rather than merely outlined.
        if (held & (1 << l))
            glow(x, bottom - 40, width(bottom) / 5 * 1.6f, 300, alpha(power ? ink::power : lanes[size_t(l)], .16f));
        receptor(size_t(l), x, bottom, 106, held & (1 << l), power);
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
    text(640, 470, count > 0 ? "found " + std::to_string(count) + (count == 1 ? " song" : " songs") : "reading your sd card",
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
} // namespace
int main(int argc, char **argv) {
    try {
#ifdef __SWITCH__
        // The fonts live in the NRO's RomFS, which has to be mounted first.
        if (R_FAILED(romfsInit()))
            std::cerr << "Switch Hero: RomFS unavailable; falling back to sdmc fonts\n";
        // Keep using an existing sd:/switch/fretboard install so songs and
        // settings survive the rename to Switch Hero.
        std::error_code ec;
        const bool legacy = !fs::is_directory("sdmc:/switch/switch-hero", ec) &&
                            fs::is_directory("sdmc:/switch/fretboard", ec);
        const std::string home = legacy ? "sdmc:/switch/fretboard" : "sdmc:/switch/switch-hero";
        fs::path root = home + "/songs", config = home + "/settings.cfg";
#else
        fs::path root = "songs", config = "settings.cfg";
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
        std::sort(entries.begin(), entries.end(),
                  [](const Entry &a, const Entry &b) { return lower(a.name) < lower(b.name); });
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
        Settings settings;
        settings.load(config);
        SDL_Joystick *virtualPad = nullptr;
        if (smoke) {
            settings = Settings{};
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
        enum class Screen { Library, Playing, Paused, Results, Settings, Calibrate, Download };
        Screen screen = Screen::Library;
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
        int settingRow = 0, remapping = -1;
        uint64_t previousButtons = 0;
        std::array<bool, SDL_NUM_SCANCODES> previousKeys{};
        bool running = true;
        std::string message;
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
        auto readClock = [&](double audioTime) {
            if (!clockRead) {
                frameTime = clock.sync(audioTime, now());
                previousClockTicks = clockTicks;
                clockTicks = SDL_GetTicks();
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
        std::vector<bool> owned; // which results are already in the library
        size_t ownedFor = 0;
        int ownedAt = -1;
        auto openSearch = [&]() {
#ifdef __SWITCH__
            if (askText("Song, artist or charter", query)) {
                downloader->search(query);
                downloadRow = 0;
            }
#else
            typing = true;
            SDL_StartTextInput();
#endif
        };
        SDL_Texture *albumArt = nullptr;
        auto loadSelected = [&]() {
            song.reset();
            trackIndex = 0;
            SDL_DestroyTexture(albumArt);
            albumArt = nullptr;
            if (entries.empty())
                return;
            albumArt = look::loadArtwork(entries[selected].folder);
            try {
                song = std::make_unique<Song>(loadSong(entries[selected].folder));
                longestSustain = 0;
                for (const auto &track : song->tracks)
                    for (const auto &note : track.notes)
                        for (double end : note.end)
                            longestSustain = std::max(longestSustain, end - note.time);
                message.clear();
            } catch (const std::exception &e) {
                // The list only peeked at metadata, so real problems surface here.
                message = e.what();
                if (selected < entries.size())
                    entries[selected].error = e.what();
            }
        };
        auto start = [&]() {
            if (!song)
                return;
            try {
                audio.load(*song);
                session = std::make_unique<Session>(*song, song->tracks[trackIndex]);
                session->gamepadMode = settings.gamepad && !settings.wiiGuitar;
                session->noFail = settings.noFail;
                audio.pause(false);
                restartClock();
                callout.clear(), calloutAt = -1, milestone = 0, wasPower = false;
                shownMisses = 0, shownCount = -1, shownScore = 0, shownMultiplier = 1;
                screen = Screen::Playing;
                message.clear();
            } catch (const std::exception &e) {
                audio.stop();
                message = e.what();
                screen = Screen::Library;
            }
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
                    message = "Controller disconnected";
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
            uint64_t buttons = controller.read(settings.wiiGuitar, screen == Screen::Playing || screen == Screen::Calibrate),
                     pressed = buttons & ~previousButtons;
            if (wasConnected && !controller.connected() && screen == Screen::Playing) {
                frozen = audio.position() - song->offset;
                audio.pause(true);
                screen = Screen::Paused;
                message = "Controller disconnected";
            }
            const auto *keys = SDL_GetKeyboardState(nullptr);
            auto key = [&](SDL_Scancode k) { return keys[k] && !previousKeys[k]; };
            auto press = [&](int b) { return bool(pressed & bit(b)); };
            bool up = press(11) || key(SDL_SCANCODE_UP), down = press(12) || key(SDL_SCANCODE_DOWN),
                 left = press(13) || key(SDL_SCANCODE_LEFT), right = press(14) || key(SDL_SCANCODE_RIGHT);
            bool accept = press(1) || key(SDL_SCANCODE_RETURN), back = press(4) || key(SDL_SCANCODE_ESCAPE),
                 pause = press(6) || key(SDL_SCANCODE_P);
            // Guitar frets act as menu shortcuts only outside gameplay.
            // Holding Minus for two seconds always gets back to the song list on
            // normal controls, whatever the controller mode is set to.
            if (buttons & bit(4)) {
                if (!escapeStart)
                    escapeStart = SDL_GetTicks64();
                else if (SDL_GetTicks64() - escapeStart > 2000 && screen != Screen::Library) {
                    escapeStart = 0;
                    settings.wiiGuitar = false;
                    audio.stop();
                    audio.playSfx(Sfx::Back);
                    remapping = -1;
                    message = "Controller mode reset to press-to-hit";
                    screen = Screen::Library;
                }
            } else
                escapeStart = 0;
            // While calibration is taking taps the guitar is an instrument, not a menu.
            bool guitarMenu = settings.wiiGuitar && screen != Screen::Playing &&
                              !(screen == Screen::Calibrate && !calibration.done());
            accept = accept || (guitarMenu && press(9));
            back = back || (guitarMenu && press(32));
            bool secondary = press(2) || (guitarMenu && press(10));
            uint8_t frets = settings.wiiGuitar ? wiiGuitarFrets(buttons) : 0;
            SDL_Scancode fretKeys[] = {SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F,
                                       SDL_SCANCODE_G};
            for (int i = 0; i < 5; ++i)
                if ((!settings.wiiGuitar && (buttons & bit(settings.bindings[i]))) || keys[fretKeys[i]])
                    frets |= uint8_t(1 << i);
            if (screen == Screen::Library) {
                if (back)
                    running = false;
                if (!entries.empty()) {
                    if (up || down) {
                        selected = (selected + entries.size() + (down ? 1 : -1)) % entries.size();
                        loadSelected();
                        audio.playSfx(Sfx::Move);
                    }
                    if (song) {
                        if ((!settings.wiiGuitar && press(9)) || left)
                            trackIndex = (trackIndex + song->tracks.size() - 1) % song->tracks.size();
                        if ((!settings.wiiGuitar && press(10)) || right)
                            trackIndex = (trackIndex + 1) % song->tracks.size();
                        if (accept) {
                            audio.playSfx(Sfx::Select);
                            start();
                        }
                    }
                }
                if (press(3) || (guitarMenu && press(0)) || key(SDL_SCANCODE_N)) {
                    settings.noFail = !settings.noFail;
                    audio.playSfx(Sfx::Toggle);
                }
                if (secondary || key(SDL_SCANCODE_TAB)) {
                    audio.playSfx(Sfx::Select, .7f);
                    screen = Screen::Settings;
                }
                if (press(6) || key(SDL_SCANCODE_O)) {
                    audio.playSfx(Sfx::Select, .7f);
                    try {
                        if (!downloader) {
                            downloader = std::make_unique<Downloader>(root);
                            downloader->search(""); // newest charts until the player searches
                        }
                        downloadsSeen = downloader->view().downloads;
                        message.clear();
                        screen = Screen::Download;
                    } catch (const std::exception &e) {
                        message = e.what();
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
                            if (downloads.downloads != downloadsSeen) {
                                scanLibrary();
                                for (size_t i = 0; i < entries.size(); ++i)
                                    if (entries[i].folder.filename() == downloads.lastFolder)
                                        selected = i;
                                selected = entries.empty() ? 0 : std::min(selected, entries.size() - 1);
                                loadSelected();
                            }
                            screen = Screen::Library;
                        }
                    }
                }
            } else if (screen == Screen::Settings) {
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
                                    message = "Already assigned to another fret";
                                else {
                                    settings.bindings[remapping] = b;
                                    message.clear();
                                    remapping = -1;
                                }
                                break;
                            }
                    }
                } else {
                    if (up || down) {
                        settingRow = (settingRow + 12 + (down ? 1 : -1)) % 12;
                        audio.playSfx(Sfx::Move);
                    }
                    double delta = right ? 1 : left ? -1 : 0;
                    if (delta || accept)
                        audio.playSfx(Sfx::Toggle, .8f);
                    if (settingRow == 0 && (delta || accept)) {
                        int mode = settings.wiiGuitar ? 2 : settings.gamepad ? 0 : 1;
                        mode = (mode + (delta < 0 ? 2 : 1)) % 3;
                        settings.wiiGuitar = mode == 2;
                        settings.gamepad = mode == 0;
                    }
                    if (settingRow == 1 && (delta || accept))
                        settings.noFail = !settings.noFail;
                    if (settingRow == 2)
                        settings.audioMs = std::clamp(settings.audioMs + delta * 5, -500.0, 500.0);
                    if (settingRow == 3)
                        settings.videoMs = std::clamp(settings.videoMs + delta * 5, -500.0, 500.0);
                    if ((settingRow == 2 || settingRow == 3) && accept)
                        startCalibration(settingRow == 3);
                    if (settingRow == 4)
                        settings.travel = std::clamp(settings.travel + delta * 0.05, 0.75, 3.0);
                    if (!settings.wiiGuitar && settingRow >= 5 && settingRow <= 9 && accept)
                        remapping = settingRow - 5;
                    if (settingRow == 10 && accept) {
                        diagnostics = !diagnostics;
                        audio.playSfx(Sfx::Toggle);
                    }
                    if (settingRow == 11 && accept)
                        settings = Settings{};
                    if (back) {
                        if (diagnostics) {
                            diagnostics = false; // the panel closes first
                            audio.playSfx(Sfx::Back);
                        } else {
                            settings.save(config);
                            message.clear();
                            audio.playSfx(Sfx::Back);
                            screen = Screen::Library;
                        }
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
                        callout = std::to_string(milestone * 50) + " NOTE STREAK!";
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
                    }
                }
            } else if (screen == Screen::Paused) {
                if (back) {
                    audio.stop();
                    audio.playSfx(Sfx::Back);
                    screen = Screen::Library;
                    message.clear();
                } else if (secondary || key(SDL_SCANCODE_R)) {
                    audio.playSfx(Sfx::Select);
                    start();
                } else if (accept || pause) {
                    if (audio.error().empty()) {
                        restartClock();
                        audio.pause(false);
                        audio.playSfx(Sfx::Select, .7f);
                        screen = Screen::Playing;
                        message.clear();
                    }
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
            } else if (screen == Screen::Results) {
                if (back || accept) {
                    audio.stop();
                    audio.playSfx(Sfx::Back);
                    screen = Screen::Library;
                }
                if (secondary || key(SDL_SCANCODE_R)) {
                    audio.playSfx(Sfx::Select);
                    start();
                }
            }
            const double ui = now();
            if (screen != previousScreen)
                previousScreen = screen, screenChangedAt = ui;
            if (session) {
                if (session->score > shownScore)
                    scorePopAt = ui;
                if (session->multiplier() != shownMultiplier)
                    multiplierPopAt = ui, shownMultiplier = session->multiplier();
                shownScore = session->score;
            }
            if (screen == Screen::Library) {
                wall(ui, ink::crt);
                auto title = stencil(74, ink::chrome, {116, 122, 138, 255});
                title.glow = alpha(ink::crt, .55f), title.glowSpread = 1.2f;
                text(58, 14, "SWITCH", title);
                auto burning = stencil(74, {255, 196, 60, 255}, {206, 26, 20, 255});
                burning.glow = alpha(ink::blood, .85f), burning.glowSpread = 1.25f;
                text(74, 76, "HERO", burning);
                text(84, 146, "burned mixtape vol. 1", marker(28, {198, 200, 212, 255}, -3));
                text(470, 132, "\\m/", marker(34, {150, 152, 166, 255}, -14));
                star(1246, 186, 16, 12, {255, 198, 44, 255}, {18, 18, 22, 255});
                star(28, 322, 13, -8, {226, 32, 44, 255}, {18, 18, 22, 255});
                tape(1128, 74, 210, 54, 3.5f);
                {
                    auto s = marker(26, ink::marker, 3.5f);
                    s.align = Align::Center;
                    text(1128, 58, std::to_string(entries.size()) + (entries.size() == 1 ? " SONG" : " SONGS"), s);
                }
                if (entries.empty()) {
                    plate(230, 250, 820, 230);
                    text(640, 285, "NO SONGS FOUND", [] { auto s = stencil(52); s.align = Align::Center; return s; }());
                    auto line1 = body(20, ink::dim);
                    line1.align = Align::Center;
                    text(640, 360, "Copy extracted Clone Hero song folders into", line1);
                    auto path = marker(24, ink::acid, -1);
                    path.align = Align::Center;
                    text(640, 395, root.string(), path);
                    text(640, 440, "Each folder needs notes.chart or notes.mid plus audio", line1);
                    auto line2 = body(20, ink::white);
                    line2.align = Align::Center;
                    text(640, 468, "or press + to download charts", line2);
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
                        auto name = marker(30, ink::marker, angle);
                        name.maxWidth = 520;
                        text(cx - 286, cy - 36, plainTitle(entries[index].name), name);
                        auto sub = body(17, {58, 58, 64, 255});
                        sub.angle = angle, sub.maxWidth = 520;
                        text(cx - 284, cy + 4, entries[index].error.empty() ? entries[index].artist : "UNSUPPORTED SONG", sub);
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
                    if (song) {
                        auto disc = marker(28, ink::marker, -8);
                        disc.align = Align::Center, disc.maxWidth = 210;
                        text(985, 158, plainTitle(song->name), disc);
                        auto sub = marker(20, {70, 60, 80, 255}, -8);
                        sub.align = Align::Center, sub.maxWidth = 200;
                        text(985, 196, song->artist, sub);
                        plate(812, 452, 350, 176);
                        text(838, 470, song->tracks[trackIndex].instrument, stencil(30));
                        text(838, 512, difficultyName(song->tracks[trackIndex].difficulty), marker(26, ink::acid, -2));
                        for (int i = 0; i < 4; ++i)
                            star(1060 + i * 26, 524, 11, float(i) * 7,
                                 i <= song->tracks[trackIndex].difficulty ? SDL_Color{255, 196, 40, 255}
                                                                          : SDL_Color{52, 52, 60, 255},
                                 {18, 18, 22, 255});
                        text(838, 556, std::to_string(song->tracks[trackIndex].notes.size()) + " NOTES   " +
                                           std::to_string(song->audio.size()) + " STEMS",
                             body(18, ink::dim));
                        text(838, 584, settings.wiiGuitar ? "WII GUITAR" : settings.gamepad ? "PRESS-TO-HIT" : "STRUM MODE",
                             body(18, ink::acid));
                        {
                            auto flag = marker(20, settings.noFail ? ink::acid : ink::faint, -3);
                            flag.align = Align::Right;
                            text(1140, 580, settings.noFail ? "no fail" : "can fail", flag);
                        }
                        if (!song->warnings.empty())
                            text(838, 608, "chart warnings: see --inspect", body(14, ink::blood));
                    }
                }
                if (!message.empty())
                    text(60, 644, message, marker(22, ink::blood, -1));
                button(58, 676, settings.wiiGuitar ? "G" : "A", "PLAY");
                button(186, 676, settings.wiiGuitar ? "YEL" : "Y", "SETTINGS");
                button(348, 676, settings.wiiGuitar ? "STK" : "LR", "TRACK");
                button(486, 676, settings.wiiGuitar ? "BLU" : "X", "NO FAIL");
                button(624, 676, settings.wiiGuitar ? "RED" : "-", "EXIT");
                button(740, 676, "+", "DOWNLOAD");
            } else if (screen == Screen::Settings) {
                wall(ui, ink::crt);
                text(58, 24, "SETTINGS", stencil(78, ink::chrome, {116, 122, 138, 255}));
                text(72, 118, "tune your rig", marker(26, {198, 200, 212, 255}, -3));
                plate(60, 166, 1160, 462);
                const std::array<std::pair<std::string, std::string>, 12> rows = {
                    std::pair{"Controller mode", settings.wiiGuitar ? "WII GUITAR" : settings.gamepad ? "PRESS-TO-HIT" : "STRUM"},
                    {"No-fail mode", settings.noFail ? "ON - never fail" : "OFF - rock meter can fail you"},
                    {"Audio / input offset", std::to_string(int(settings.audioMs)) + " ms"},
                    {"Visual offset", std::to_string(int(settings.videoMs)) + " ms"},
                    {"Highway travel", std::to_string(int(settings.travel * 1000)) + " ms"},
                    {"Fret 1", bindingName(settings.wiiGuitar ? wiiGuitarBindings[0] : settings.bindings[0])},
                    {"Fret 2", bindingName(settings.wiiGuitar ? wiiGuitarBindings[1] : settings.bindings[1])},
                    {"Fret 3", bindingName(settings.wiiGuitar ? wiiGuitarBindings[2] : settings.bindings[2])},
                    {"Fret 4", bindingName(settings.wiiGuitar ? wiiGuitarBindings[3] : settings.bindings[3])},
                    {"Fret 5", bindingName(settings.wiiGuitar ? wiiGuitarBindings[4] : settings.bindings[4])},
                    {"Controller test", diagnostics ? "OPEN" : "check your guitar"},
                    {"Reset defaults", ""}};
                for (int i = 0; i < int(rows.size()); ++i) {
                    const float y = 192 + i * 40;
                    const bool on = i == settingRow;
                    if (on)
                        rect(80, y - 8, 1120, 40, {0, 0, 0, 120});
                    if (on)
                        glow(98, y + 12, 34, 34, alpha(ink::acid, .8f));
                    disc(98, y + 12, 6, 6, on ? ink::acid : SDL_Color{44, 46, 54, 255}, on ? mix(ink::acid, SDL_Color{0, 0, 0, 255}, .45f) : SDL_Color{26, 26, 32, 255});
                    text(126, y, rows[i].first, body(24, on ? ink::white : SDL_Color{182, 186, 198, 255}));
                    auto value = marker(26, on ? ink::acid : SDL_Color{214, 216, 226, 255}, -1);
                    value.align = Align::Right;
                    text(1180, y - 4, rows[i].second, value);
                }
                if (remapping >= 0)
                    text(640, 648, "PRESS A BUTTON OR TRIGGER", [] {
                        auto s = marker(26, ink::acid, -1);
                        s.align = Align::Center;
                        return s;
                    }());
                else if (!message.empty())
                    text(60, 648, message, marker(22, ink::blood, -1));
                else
                    text(60, 650,
                         settingRow == 2 || settingRow == 3
                             ? std::string("Press ") + (settings.wiiGuitar ? "GREEN" : "A") +
                                   " to calibrate by tapping along. Left/right fine-tunes."
                             : "Positive audio offset judges notes later. Start at 0 ms.",
                         body(17, settingRow == 2 || settingRow == 3 ? ink::dim : ink::faint));
                if (diagnostics) {
                    // Live input, so a silent guitar can be told apart from a
                    // wrong mapping without guessing.
                    rect(0, 0, W, H, {5, 5, 8, 226});
                    plate(150, 120, 980, 470);
                    auto head = stencil(44);
                    head.align = Align::Center;
                    text(640, 140, "CONTROLLER TEST", head);
                    const auto probe = controller.probe();
                    const uint64_t normalized = buttons;
                    auto lines = std::vector<std::pair<std::string, std::string>>{
                        {"normal controller", probe.standard ? "connected" : "not connected"},
                        {"player one (guitar)", probe.guitar ? "connected" : "not connected"},
                        {"guitar mode", settings.wiiGuitar ? "on" : "off"},
                    };
#ifdef __SWITCH__
                    char styles[64];
                    std::snprintf(styles, sizeof(styles), "0x%08x / 0x%08x", probe.standardStyle, probe.guitarStyle);
                    lines.push_back({"styles (normal/player one)", styles});
                    char raw[64];
                    std::snprintf(raw, sizeof(raw), "0x%08llx / 0x%08llx", (unsigned long long)probe.standardRaw,
                                  (unsigned long long)probe.guitarRaw);
                    lines.push_back({"raw buttons", raw});
#else
                    lines.push_back({"controller", probe.name});
#endif
                    std::string pressed;
                    for (int b = 0; b <= 33; ++b)
                        if (normalized & bit(b))
                            pressed += (pressed.empty() ? "" : " ") + bindingName(b);
                    lines.push_back({"buttons seen by the game", pressed.empty() ? "none" : pressed});
                    const uint8_t testFrets = settings.wiiGuitar ? wiiGuitarFrets(normalized) : frets;
                    std::string fretText;
                    const char *fretNames[] = {"green", "red", "yellow", "blue", "orange"};
                    for (int i = 0; i < 5; ++i)
                        if (testFrets & (1 << i))
                            fretText += (fretText.empty() ? "" : " ") + std::string(fretNames[i]);
                    lines.push_back({"frets", fretText.empty() ? "none" : fretText});
                    lines.push_back({"strum", (normalized & (bit(11) | bit(12))) ? "yes" : "no"});
                    for (size_t i = 0; i < lines.size(); ++i) {
                        const float y = 210 + i * 46;
                        text(200, y, lines[i].first, body(22, ink::dim));
                        auto value = marker(26, ink::acid, -1);
                        value.align = Align::Right;
                        text(1080, y - 4, lines[i].second, value);
                    }
                    auto hint = body(18, ink::faint);
                    hint.align = Align::Center;
                    text(640, 556, "Press the guitar's frets and strum. Nothing here means the guitar is not "
                                   "reaching the game.", hint);
                    button(556, 620, "-", "CLOSE");
                }
                button(1050, 676, settings.wiiGuitar ? "-/RED" : "-", "SAVE");
            } else if (screen == Screen::Download) {
                wall(ui, ink::crt);
                text(58, 24, "DOWNLOAD", stencil(78, ink::chrome, {116, 122, 138, 255}));
                text(72, 118, "charts from chorus encore", marker(26, {198, 200, 212, 255}, -3));
                // Search field.
                rect(60, 160, 1160, 52, {0, 0, 0, 150});
                rect(60, 211, 1160, 1, {176, 180, 192, 42});
                {
                    const bool empty = query.empty();
                    std::string shown = empty && !typing ? "newest charts - press Y to search" : query;
                    if (typing && std::fmod(ui, 1.0) < .55)
                        shown += "_";
                    auto field = body(24, empty && !typing ? ink::faint : ink::white);
                    field.maxWidth = 1100;
                    text(84, 172, shown, field);
                }
                plate(60, 226, 1160, 380);
                const auto &results = downloads.results;
                const int rows = 6;
                const int first =
                    std::clamp(int(downloadRow) - rows / 2, 0, std::max(0, int(results.size()) - rows));
                for (int i = 0; i < rows && first + i < int(results.size()); ++i) {
                    const auto &c = results[size_t(first + i)];
                    const bool on = size_t(first + i) == downloadRow;
                    const bool have = size_t(first + i) < owned.size() && owned[size_t(first + i)];
                    const float y = 246 + i * 58;
                    if (on) {
                        rect(80, y - 6, 1120, 54, {0, 0, 0, 120});
                        glow(98, y + 20, 34, 34, alpha(ink::acid, .8f));
                    }
                    disc(98, y + 20, 6, 6, on ? ink::acid : SDL_Color{44, 46, 54, 255},
                         on ? mix(ink::acid, SDL_Color{0, 0, 0, 255}, .45f) : SDL_Color{26, 26, 32, 255});
                    auto name = body(22, have ? ink::dim : on ? ink::white : SDL_Color{200, 204, 214, 255});
                    name.maxWidth = 760;
                    text(122, y, c.name, name);
                    auto by = body(15, ink::dim);
                    by.maxWidth = 760;
                    text(122, y + 26, c.artist + (c.charter.empty() ? "" : "   charted by " + c.charter), by);
                    // Intensity: six pips, as Clone Hero rates guitar parts.
                    for (int p = 0; p < 6; ++p) {
                        const bool lit = p < c.guitarDifficulty;
                        disc(930 + p * 16, y + 14, 5, 5, lit ? SDL_Color{255, 198, 44, 255} : SDL_Color{70, 72, 84, 255},
                             lit ? SDL_Color{150, 90, 10, 255} : SDL_Color{38, 40, 48, 255}, 12);
                    }
                    if (c.guitarDifficulty < 0)
                        text(1022, y + 3, "?", body(16, ink::faint));
                    auto right = body(16, ink::dim);
                    right.align = Align::Right;
                    text(1180, y + 4, c.seconds > 0 ? timeText(c.seconds) : "", right);
                    if (have) {
                        auto tag = marker(18, ink::acid, -2);
                        tag.align = Align::Right;
                        text(1180, y + 26, "in library", tag);
                    }
                }
                if (results.empty() && downloads.state == Downloader::State::Idle && downloads.error.empty()) {
                    auto none = body(22, ink::dim);
                    none.align = Align::Center;
                    text(640, 390, downloads.page ? "No guitar charts matched." : "", none);
                }
                // Status line: progress, errors, or what the list holds.
                if (downloads.state == Downloader::State::Downloading) {
                    ledMeter(66, 628, 420, 14, 28, downloads.progress, true, ui);
                    char line[96];
                    std::snprintf(line, sizeof line, "%s   %.1f MB", downloads.status.c_str(), downloads.megabytes);
                    text(510, 622, line, body(18, ink::dim));
                } else if (!downloads.error.empty())
                    text(62, 620, downloads.error, marker(22, ink::blood, -1));
                else if (downloads.state == Downloader::State::Searching)
                    text(62, 622, "searching...", body(18, ink::dim));
                else if (downloads.downloads != downloadsSeen && !downloads.lastFolder.empty())
                    text(62, 620, "added " + downloads.lastFolder, marker(22, ink::acid, -1));
                else if (downloads.page)
                    text(62, 622, std::to_string(downloads.found) + " guitar charts", body(18, ink::faint));
                if (typing) {
                    button(58, 676, "", "ENTER  SEARCH");
                    button(260, 676, "", "ESC  CANCEL");
                } else {
                    button(58, 676, settings.wiiGuitar ? "GREEN" : "A", "DOWNLOAD");
                    button(230, 676, settings.wiiGuitar ? "YEL" : "Y", "SEARCH");
                    button(380, 676, settings.wiiGuitar ? "RED" : "-",
                           downloads.state == Downloader::State::Downloading ? "CANCEL" : "BACK");
                }
            } else if (screen == Screen::Calibrate) {
                const double time = calibrationTime();
                const bool done = calibration.done();
                wall(ui, ink::crt);
                if (calibratingVideo && !done)
                    highway(calibrationSong, *calibrationSession, settings, time - settings.videoMs / 1000, frets, ui);
                text(46, 24, "CALIBRATE", stencil(56, ink::chrome, {116, 122, 138, 255}));
                text(52, 92, calibratingVideo ? "visual offset" : "audio offset", marker(24, {198, 200, 212, 255}, -3));
                // Taps panel, where the score plate sits in a song.
                plate(38, 158, 252, 330);
                text(64, 184, "TAPS", stencil(22, ink::steel, {80, 84, 96, 255}));
                text(64, 212,
                     std::to_string(calibration.errors.size()) + " / " + std::to_string(calibration.needed),
                     stencil(52));
                // Each tap's error on a +-150 ms rule, so a drifting or uneven
                // hand is visible, not just a number.
                const float ruleX = 64, ruleW = 200, ruleY = 330, rangeMs = 150;
                rect(ruleX, ruleY, ruleW, 2, {70, 72, 84, 255});
                rect(ruleX + ruleW / 2 - 1, ruleY - 14, 2, 30, {110, 114, 128, 255});
                text(ruleX, ruleY + 22, "early", body(14, ink::faint));
                auto lateLabel = body(14, ink::faint);
                lateLabel.align = Align::Right;
                text(ruleX + ruleW, ruleY + 22, "late", lateLabel);
                for (size_t i = 0; i < calibration.errors.size(); ++i) {
                    const float ms = float(std::clamp(calibration.errors[i] * 1000, -double(rangeMs), double(rangeMs)));
                    const float x = ruleX + ruleW / 2 + ms / rangeMs * ruleW / 2;
                    const bool latest = i + 1 == calibration.errors.size();
                    if (latest)
                        glow(x, ruleY + 1, 30, 30, alpha(ink::acid, .7f));
                    rect(x - 1.5f, ruleY - 10, 3, 22, latest ? ink::acid : alpha(ink::acid, .45f));
                }
                if (!calibration.errors.empty()) {
                    char median[48];
                    std::snprintf(median, sizeof median, "median %+d ms", int(std::round(calibration.median() * 1000)));
                    text(64, 400, median, body(20, ink::dim));
                }
                if (!done) {
                    if (!calibratingVideo) {
                        // Nothing on screen moves with the beat: the ears alone set the taps.
                        auto big = stencil(64);
                        big.align = Align::Center;
                        text(700, 280, time < calibration.warmup ? "LISTEN" : "TAP", big);
                        auto hint = body(22, ink::dim);
                        hint.align = Align::Center;
                        text(700, 370, "Tap any fret or strum on every click.", hint);
                        text(700, 402, "Close your eyes if it helps.", hint);
                    } else {
                        auto hint = body(20, ink::dim);
                        hint.align = Align::Center;
                        text(1100, 250, "Clicks are muted.", hint);
                        text(1100, 280, "Tap as each note", hint);
                        text(1100, 306, "crosses the line.", hint);
                    }
                    button(58, 676, "-", "CANCEL");
                } else {
                    rect(0, 0, W, H, {5, 5, 8, 150});
                    plate(390, 190, 620, 330);
                    auto head = stencil(40);
                    head.align = Align::Center;
                    text(700, 214, calibratingVideo ? "VISUAL OFFSET" : "AUDIO / INPUT OFFSET", head);
                    const double was = calibratingVideo ? settings.videoMs : settings.audioMs;
                    auto value = stencil(72, ink::acid, mix(ink::acid, SDL_Color{20, 40, 10, 255}, .5f));
                    value.align = Align::Center;
                    text(700, 280, std::to_string(int(calibrationResult())) + " ms", value);
                    auto sub = body(20, ink::dim);
                    sub.align = Align::Center;
                    text(700, 380, "was " + std::to_string(int(was)) + " ms", sub);
                    // A wide spread means the median is a guess; say so rather than
                    // quietly saving it.
                    if (calibration.spread() > .025) {
                        auto warn = marker(22, ink::blood, -1);
                        warn.align = Align::Center;
                        text(700, 420, "taps were uneven - retry for a steadier read", warn);
                    }
                    button(430, 470, settings.wiiGuitar ? "GREEN" : "A", "KEEP");
                    button(600, 470, settings.wiiGuitar ? "YEL" : "Y", "RETRY");
                    button(770, 470, "-", "CANCEL");
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
                        text(640, 432 - 24 * pop, names[session->lastTier], s);
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
                            text(640, 506, session->lastError < 0 ? "EARLY" : "LATE", l);
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
                text(46, 96, song->artist + "  /  " + difficultyName(session->track->difficulty), body(17, ink::dim));
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
                text(62, 186, "SCORE", stencil(20, ink::dim, ink::faint));
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
                text(64, 292, "streak", marker(26, ink::dim, -3));
                text(58, 314, std::to_string(session->combo), stencil(60, session->combo ? ink::acid : ink::faint,
                                                                       session->combo ? SDL_Color{110, 190, 30, 255} : ink::faint));
                engrave(392);
                const int judged = session->hits + session->misses;
                text(62, 412, (judged ? std::to_string(session->hits * 100 / judged) : std::string("100")) + "% HIT",
                     body(24, ink::white));
                // Clear of the panel's bottom screws.
                text(62, 442, std::to_string(session->misses) + " missed", body(17, ink::faint));
                {
                    const float pop = 1 + .3f * decay(ui - multiplierPopAt, .35);
                    const float spin = 9 + 14 * decay(ui - multiplierPopAt, .35);
                    if (session->multiplier() >= 4)
                        glow(1104, 226, 220 * pop, 220 * pop, alpha(session->powerActive ? ink::power : ink::blood, .45f));
                    sticker(1104, 226, 56 * pop, spin, session->powerActive ? ink::power : ink::blood,
                            "x" + std::to_string(session->multiplier()));
                }
                text(940, 372, "STAR POWER", stencil(20, ink::dim, ink::faint));
                ledMeter(940, 404, 232, 20, 12, float(session->power), session->powerActive, ui);
                if (session->powerActive)
                    text(940, 442, "BURNING", marker(24, ink::power, -2));
                else if (session->power >= .5 && std::fmod(ui, 1.0) < .6)
                    text(940, 442, "hit X !", marker(24, ink::acid, -3));
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
                        text(1232, 580, "no fail", safe);
                    } else if (danger && std::fmod(ui, .7) < .45) {
                        auto warn = marker(22, ink::blood, -5);
                        warn.align = Align::Center;
                        text(1232, 580, "danger!", warn);
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
                    text(640, 430, "get ready", ready);
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
                text(46, 686, "Plus pause    any fret hits open notes    hold sustains", body(16, ink::dim));
                if (screen == Screen::Paused || screen == Screen::Results) {
                    rect(0, 0, W, H, {5, 5, 8, 238});
                    auto heading = stencil(96, ink::chrome, {116, 122, 138, 255});
                    heading.align = Align::Center, heading.glow = alpha(ink::blood, .8f), heading.glowSpread = 1.2f;
                    const bool failed = session->failed;
                    if (failed)
                        heading.glow = alpha(ink::blood, .95f);
                    text(640, 76, screen == Screen::Paused ? "PAUSED" : failed ? "SONG FAILED" : "SONG COMPLETE",
                         heading);
                    if (screen == Screen::Results) {
                        const float accuracy = session->state.empty() ? 1 : float(session->hits) / session->state.size();
                        const int stars = failed ? 0
                                                 : accuracy >= .97f ? 5 : accuracy >= .9f ? 4 : accuracy >= .8f ? 3
                                                                                              : accuracy >= .65f ? 2 : 1;
                        const char *rank = failed ? "F" : accuracy >= .97f ? "S" : accuracy >= .9f ? "A"
                                                        : accuracy >= .8f ? "B" : accuracy >= .65f ? "C" : "D";
                        // Big stamped rank, like a grade scrawled on the CD sleeve.
                        glow(354, 306, 300, 300, alpha(failed ? ink::blood : ink::acid, .3f));
                        auto rankStyle = stencil(190, failed ? SDL_Color{240, 90, 90, 255} : ink::chrome,
                                                 failed ? SDL_Color{120, 20, 20, 255} : SDL_Color{116, 122, 138, 255});
                        rankStyle.align = Align::Center;
                        rankStyle.glow = alpha(failed ? ink::blood : ink::crt, .8f), rankStyle.glowSpread = 1.25f;
                        text(354, 206, rank, rankStyle);
                        const std::string value = std::to_string(int(session->score));
                        const float size = fitSize(value, stencil(84), 440);
                        auto score = stencil(size);
                        score.align = Align::Center;
                        text(820, 200 + (84 - size) * .5f, value, score);
                        auto scoreLabel = stencil(20, ink::dim, ink::faint);
                        scoreLabel.align = Align::Center;
                        text(820, 176, "FINAL SCORE", scoreLabel);
                        for (int i = 0; i < 5; ++i)
                            star(680 + i * 70, 322, 28, (hash01(uint32_t(i) * 7) - .5f) * 16,
                                 i < stars ? SDL_Color{255, 198, 44, 255} : SDL_Color{48, 48, 56, 255}, {14, 14, 18, 255});
                        auto verdict = marker(40, failed ? ink::blood : ink::acid, -4);
                        verdict.align = Align::Center;
                        text(820, 362,
                             failed ? "the crowd is booing"
                                    : accuracy >= .97f ? "flawless!" : accuracy >= .9f ? "shredded it" :
                                      accuracy >= .8f ? "solid set" : accuracy >= .65f ? "not bad" : "keep practicing",
                             verdict);
                        // Stats scrawled on strips of tape.
                        const std::array<std::pair<std::string, std::string>, 4> stats = {
                            std::pair{"notes hit", std::to_string(session->hits) + " / " + std::to_string(session->state.size())},
                            {"best streak", std::to_string(session->maxCombo)},
                            // How clean the run was, not just how much of it landed.
                            {"timing", std::to_string(session->tierCounts[3]) + " perfect  " +
                                           std::to_string(session->tierCounts[2]) + " great  " +
                                           std::to_string(session->tierCounts[1]) + " good"},
                            {"accuracy", std::to_string(int(accuracy * 100)) + "%"}};
                        for (int i = 0; i < 4; ++i) {
                            const float ty = 444 + i * 56, angle = (hash01(uint32_t(i) * 31) - .5f) * 3;
                            tape(640, ty, 460, 50, angle);
                            auto key = marker(24, ink::marker, angle);
                            text(444, ty - 24, stats[i].first, key);
                            auto value = marker(26, {40, 40, 46, 255}, angle);
                            value.align = Align::Right;
                            text(834, ty - 26, stats[i].second, value);
                        }
                    } else {
                        tape(640, 250, 520, 60, -1.2f);
                        auto line = marker(30, ink::marker, -1.2f);
                        line.align = Align::Center, line.maxWidth = 440;
                        text(640, 232, plainTitle(song->name), line);
                        auto held = body(20, ink::dim);
                        held.align = Align::Center;
                        text(640, 312, "the amp is still humming", held);
                    }
                    const float row = screen == Screen::Results ? 566 : 386;
                    if (screen == Screen::Paused)
                        button(556, row, settings.wiiGuitar ? "GREEN" : "A", "RESUME");
                    button(556, row + 44, settings.wiiGuitar ? "YEL" : "Y", "RESTART");
                    button(556, row + 88, "-", "SONG LIST");
                    if (!message.empty()) {
                        auto m = marker(22, ink::blood, -1);
                        m.align = Align::Center;
                        text(640, row + 140, message, m);
                    }
                }
            }

            // Quick fade whenever the screen changes.
            if (const float fade = decay(ui - screenChangedAt, .22); fade > 0)
                rect(0, 0, W, H, {0, 0, 0, Uint8(210 * fade)});
            // Grade the finished frame before anything reads it back, so a
            // screenshot shows what the player actually sees.
            grade(ui);
            if (timing && session && screen == Screen::Playing) {
                // Dev read-out for checking the clock on hardware: how far the song
                // clock sits from the audio, and how hard it is leaning to close it.
                static double lastFrame = now();
                const double frameMs = (now() - lastFrame) * 1000;
                lastFrame = now();
                char line[96];
                std::snprintf(line, sizeof line, "frame %5.1f ms   drift %+5.1f ms   rate %.4f", frameMs,
                              clock.drift * 1000, clock.rate);
                text(490, 16, line, body(16, ink::acid));
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
