#include "audio.hpp"
#include "font8x8_basic.h"
#include "game.hpp"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#ifdef __SWITCH__
#include <switch.h>
#endif

using namespace fret;
namespace {
constexpr int W = 1280, H = 720;
const SDL_Color ink = {229, 235, 246, 255}, muted = {132, 148, 174, 255}, mint = {104, 240, 199, 255};
const std::array<SDL_Color, 6> colors = {SDL_Color{101, 228, 128, 255}, {255, 105, 120, 255},
                                         {248, 207, 91, 255},           {95, 165, 255, 255},
                                         {251, 157, 86, 255},           {205, 139, 250, 255}};
SDL_Renderer *renderer = nullptr;
void color(SDL_Color c) { SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a); }
void rect(float x, float y, float w, float h, SDL_Color c) {
    color(c);
    SDL_FRect r{x, y, w, h};
    SDL_RenderFillRectF(renderer, &r);
}
void line(float x, float y, float x2, float y2, SDL_Color c) {
    color(c);
    SDL_RenderDrawLineF(renderer, x, y, x2, y2);
}
void text(float x, float y, std::string value, int scale = 2, SDL_Color c = ink, int max = 100) {
    color(c);
    int count = 0;
    for (size_t i = 0; i < value.size() && count < max; ++i) {
        unsigned char ch = value[i];
        if (ch >= 128) {
            if ((ch & 0xc0) == 0x80)
                continue;
            ch = '?';
        }
        if (ch < 32)
            ch = ' ';
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 8; ++col)
                if (font8x8_basic[ch][row] & (1 << col)) {
                    SDL_Rect r{int(x + col * scale), int(y + row * scale), scale, scale};
                    SDL_RenderFillRect(renderer, &r);
                }
        x += 8 * scale;
        ++count;
    }
}
void quad(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, SDL_Color c) {
    SDL_Vertex v[4] = {
        {{x1, y1}, c, {0, 0}}, {{x2, y2}, c, {0, 0}}, {{x3, y3}, c, {0, 0}}, {{x4, y4}, c, {0, 0}}};
    int indices[] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, nullptr, v, 4, indices, 6);
}
void background() {
    color({12, 17, 26, 255});
    SDL_RenderClear(renderer);
    for (int y = 0; y < H; y += 3)
        rect(0, float(y), W, 3, {uint8_t(12 + y / 120), uint8_t(17 + y / 130), uint8_t(26 + y / 70), 255});
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
    double audioMs = 0, videoMs = 0, travel = 1.8;
    bool gamepad = true;
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
            if (k == "gamepad")
                gamepad = v != 0;
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
          << gamepad << '\n';
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
    PadState nx{};
#endif
  public:
    Controller() {
#ifdef __SWITCH__
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&nx);
#else
        connect();
#endif
    }
    ~Controller() {
        if (pad)
            SDL_GameControllerClose(pad);
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
    bool connected() const {
#ifdef __SWITCH__
        return padIsConnected(&nx);
#else
        return pad && SDL_GameControllerGetAttached(pad);
#endif
    }
    uint64_t read() {
        uint64_t out = 0;
#ifdef __SWITCH__
        padUpdate(&nx);
        auto h = padGetButtons(&nx);
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
void highway(const Song &song, const Session &session, const Settings &settings, double time, uint8_t held) {
    const float top = 137, bottom = 609;
    auto width = [](float y) { return 144.0f + (y - 137) / (609 - 137) * 430; };
    auto xx = [&](float lane, float y) { return 640 + (lane / 5 - 0.5f) * width(y); };
    auto yy = [&](double noteTime) {
        return bottom - float((noteTime - time) / settings.travel) * (bottom - top);
    };
    quad(xx(0, top), top, xx(5, top), top, xx(5, 650), 650, xx(0, 650), 650, {19, 26, 39, 255});
    for (int lane = 0; lane <= 5; ++lane)
        line(xx(float(lane), top), top, xx(float(lane), 650), 650, {49, 61, 81, 255});
    double first = std::max(0.0, std::floor(song.tickAt(time) / song.resolution));
    for (int i = 0; i < 40; ++i) {
        float y = yy(song.seconds(Tick((first + i) * song.resolution)));
        if (y < top)
            break;
        if (y > 650)
            continue;
        line(xx(0, y), y, xx(5, y), y, {63, 73, 91, 255});
    }
    const auto &notes = session.track->notes;
    auto begin = std::lower_bound(notes.begin(), notes.end(), time - 30,
                                  [](const Note &n, double t) { return n.time < t; });
    // Include long sustains even when their head is offscreen.
    for (auto it = notes.begin(); it != notes.end(); ++it) {
        const auto &n = *it;
        size_t idx = size_t(it - notes.begin());
        if (n.time > time + settings.travel)
            break;
        float y = yy(n.time);
        auto state = session.state[idx];
        for (int l = 0; l < 6; ++l)
            if (n.mask & (1 << l)) {
                float tail = std::max(top, yy(n.end[l]));
                float head = std::min(bottom, y);
                if (n.end[l] > time && n.end[l] > n.time && head > tail) {
                    SDL_Color c = colors[l];
                    c.a = state.result < 0 ? 40 : 130;
                    float lane = l == 5 ? 2.5f : l + 0.5f;
                    float tailX = xx(lane, tail), headX = xx(lane, head);
                    quad(tailX - 3, tail, tailX + 3, tail, headX + 7, head, headX - 7, head, c);
                }
            }
        if (it < begin || state.result == 1 || y < top || y > 650)
            continue;
        if (n.mask == 32) {
            float w = width(y);
            rect(640 - w / 2 + 8, y - 6, w - 16, 12, colors[5]);
            continue;
        }
        for (int l = 0; l < 5; ++l)
            if (n.mask & (1 << l)) {
                float x = xx(l + 0.5f, y), size = width(y) / 5 * 0.7f;
                SDL_Color c = colors[l];
                if (state.result < 0)
                    c.a = 70;
                rect(x - size / 2, y - 9, size, 18, {5, 9, 14, 255});
                rect(x - size / 2 + 2, y - 7, size - 4, 12, c);
                if (n.kind != Kind::Strum)
                    rect(x - 4, y - 5, 8, 8, ink);
                if (n.phrase >= 0)
                    rect(x - size / 2, y - 12, size, 2, mint);
            }
    }
    for (int l = 0; l < 5; ++l) {
        float x = xx(l + 0.5f, bottom);
        rect(x - 42, bottom - 9, 84, 22, held & (1 << l) ? colors[l] : SDL_Color{50, 58, 76, 255});
        rect(x - 38, bottom - 5, 76, 3, colors[l]);
        text(x - 32, 651, bindingName(settings.bindings[l]), 1, colors[l], 8);
    }
    line(xx(0, bottom), bottom, xx(5, bottom), bottom, ink);
}
void screenshot(const std::string &path) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface) {
        if (SDL_RenderReadPixels(renderer, nullptr, surface->format->format, surface->pixels,
                                 surface->pitch) == 0)
            SDL_SaveBMP(surface, path.c_str());
        SDL_FreeSurface(surface);
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
#ifdef __SWITCH__
        fs::path root = "sdmc:/switch/fretboard/songs", config = "sdmc:/switch/fretboard/settings.cfg";
#else
        fs::path root = "songs", config = "settings.cfg";
#endif
        bool inspect = false, smoke = false;
        std::string shot = "fretboard.bmp";
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--inspect")
                inspect = true;
            else if (a == "--smoke")
                smoke = true;
            else if (a == "--screenshot" && i + 1 < argc)
                shot = argv[++i];
            else if (a == "--songs" && i + 1 < argc)
                root = argv[++i];
            else if (a == "--help") {
                std::cout << "fretboard [--songs FOLDER] [--inspect] [--smoke --screenshot FILE.bmp]\n";
                return 0;
            } else
                throw std::runtime_error("Unknown argument: " + a);
        }
        auto folders = scanSongs(root);
        std::vector<Entry> entries;
        for (auto &f : folders) {
            Entry e;
            e.folder = f;
            try {
                auto s = loadSong(f);
                e.name = s.name;
                e.artist = s.artist;
                if (inspect) {
                    std::cout << s.name << " | " << s.artist << " | " << s.chart.filename()
                              << " | offset=" << s.offset << "s | stems=" << s.audio.size() << '\n';
                    for (auto &t : s.tracks)
                        std::cout << "  " << t.instrument << " " << difficultyName(t.difficulty) << ": "
                                  << t.notes.size() << " note groups\n";
                    for (auto &w : s.warnings)
                        std::cout << "  WARNING: " << w << '\n';
                }
            } catch (const std::exception &ex) {
                e.name = f.filename().string();
                e.error = ex.what();
                if (inspect)
                    std::cerr << "ERROR " << f << ": " << e.error << '\n';
            }
            entries.push_back(e);
        }
        if (inspect) {
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
            SDL_CreateWindow("Fretboard - Controller rhythm game", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (!window)
            throw std::runtime_error(SDL_GetError());
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer)
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer)
            throw std::runtime_error(SDL_GetError());
        SDL_RenderSetLogicalSize(renderer, W, H);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
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
                ",Fretboard test "
                "controller,a:b0,b:b1,x:b2,y:b3,back:b4,start:b6,leftshoulder:b9,rightshoulder:b10,dpup:b11,"
                "dpdown:b12,dpleft:b13,dpright:b14,lefttrigger:a4,righttrigger:a5,";
            if (SDL_GameControllerAddMapping(mapping.c_str()) < 0)
                throw std::runtime_error(SDL_GetError());
            SDL_JoystickSetVirtualAxis(virtualPad, 4, -32768);
            SDL_JoystickSetVirtualAxis(virtualPad, 5, -32768);
        }
        Controller controller;
        Audio audio;
        enum class Screen { Library, Playing, Paused, Results, Settings };
        Screen screen = Screen::Library;
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
        auto loadSelected = [&]() {
            song.reset();
            trackIndex = 0;
            if (entries.empty())
                return;
            try {
                song = std::make_unique<Song>(loadSong(entries[selected].folder));
                message.clear();
            } catch (const std::exception &e) {
                message = e.what();
            }
        };
        auto start = [&]() {
            if (!song)
                return;
            try {
                audio.load(*song);
                session = std::make_unique<Session>(*song, song->tracks[trackIndex]);
                session->gamepadMode = settings.gamepad;
                audio.pause(false);
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
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT)
                    running = false;
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
                        if (note.mask == 32)
                            SDL_JoystickSetVirtualButton(virtualPad, 1, 1);
                        for (int l = 0; l < 5; ++l)
                            if (note.mask & (1 << l)) {
                                int b = settings.bindings[l];
                                if (b >= 32)
                                    SDL_JoystickSetVirtualAxis(virtualPad, b - 28, 32767);
                                else
                                    SDL_JoystickSetVirtualButton(virtualPad, b, 1);
                            }
                    }
                }
                SDL_JoystickUpdate();
            }
            uint64_t buttons = controller.read(), pressed = buttons & ~previousButtons;
            const auto *keys = SDL_GetKeyboardState(nullptr);
            auto key = [&](SDL_Scancode k) { return keys[k] && !previousKeys[k]; };
            auto press = [&](int b) { return bool(pressed & bit(b)); };
            bool up = press(11) || key(SDL_SCANCODE_UP), down = press(12) || key(SDL_SCANCODE_DOWN),
                 left = press(13) || key(SDL_SCANCODE_LEFT), right = press(14) || key(SDL_SCANCODE_RIGHT);
            bool accept = press(1) || key(SDL_SCANCODE_RETURN), back = press(4) || key(SDL_SCANCODE_ESCAPE),
                 pause = press(6) || key(SDL_SCANCODE_P);
            uint8_t frets = 0;
            SDL_Scancode fretKeys[] = {SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F,
                                       SDL_SCANCODE_G};
            for (int i = 0; i < 5; ++i)
                if ((buttons & bit(settings.bindings[i])) || keys[fretKeys[i]])
                    frets |= uint8_t(1 << i);
            if (screen == Screen::Library) {
                if (back)
                    running = false;
                if (!entries.empty()) {
                    if (up) {
                        selected = (selected + entries.size() - 1) % entries.size();
                        loadSelected();
                    }
                    if (down) {
                        selected = (selected + 1) % entries.size();
                        loadSelected();
                    }
                    if (song) {
                        if (press(9) || left)
                            trackIndex = (trackIndex + song->tracks.size() - 1) % song->tracks.size();
                        if (press(10) || right)
                            trackIndex = (trackIndex + 1) % song->tracks.size();
                        if (accept)
                            start();
                    }
                }
                if (press(2) || key(SDL_SCANCODE_TAB))
                    screen = Screen::Settings;
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
                    if (up)
                        settingRow = (settingRow + 9) % 10;
                    if (down)
                        settingRow = (settingRow + 1) % 10;
                    double delta = right ? 1 : left ? -1 : 0;
                    if (settingRow == 0 && (delta || accept))
                        settings.gamepad = !settings.gamepad;
                    if (settingRow == 1)
                        settings.audioMs = std::clamp(settings.audioMs + delta * 5, -500.0, 500.0);
                    if (settingRow == 2)
                        settings.videoMs = std::clamp(settings.videoMs + delta * 5, -500.0, 500.0);
                    if (settingRow == 3)
                        settings.travel = std::clamp(settings.travel + delta * 0.05, 0.75, 3.0);
                    if (settingRow >= 4 && settingRow <= 8 && accept)
                        remapping = settingRow - 4;
                    if (settingRow == 9 && accept)
                        settings = Settings{};
                    if (back) {
                        settings.save(config);
                        message.clear();
                        screen = Screen::Library;
                    }
                }
            } else if (screen == Screen::Playing) {
                double time = audio.position() - song->offset - settings.audioMs / 1000;
                if (pause || back) {
                    frozen = time;
                    audio.pause(true);
                    screen = Screen::Paused;
                } else {
                    if (press(3) || key(SDL_SCANCODE_LSHIFT))
                        session->activate();
                    session->update(time, frets, up || down || press(1) || key(SDL_SCANCODE_SPACE),
                                    press(1) || key(SDL_SCANCODE_SPACE));
                    if (auto e = audio.error(); !e.empty()) {
                        audio.pause(true);
                        screen = Screen::Paused;
                        message = e;
                    } else if (audio.position() > audio.duration() + 0.3) {
                        audio.pause(true);
                        frozen = time;
                        screen = Screen::Results;
                    }
                }
            } else if (screen == Screen::Paused) {
                if (back) {
                    audio.stop();
                    screen = Screen::Library;
                    message.clear();
                } else if (press(2) || key(SDL_SCANCODE_R))
                    start();
                else if (accept || pause) {
                    if (audio.error().empty()) {
                        audio.pause(false);
                        screen = Screen::Playing;
                        message.clear();
                    }
                }
            } else if (screen == Screen::Results) {
                if (back || accept) {
                    audio.stop();
                    screen = Screen::Library;
                }
                if (press(2) || key(SDL_SCANCODE_R))
                    start();
            }
            background();
            if (screen == Screen::Library) {
                text(60, 48, "FRETBOARD", 5);
                text(63, 104, "PICK A SONG. MAKE SOME NOISE.", 2, muted);
                text(955, 57, "PROTOTYPE 0.1", 2, mint);
                rect(60, 148, 1160, 2, {53, 68, 87, 255});
                if (entries.empty()) {
                    text(64, 210, "NO SONGS FOUND", 3);
                    text(64, 265, "Copy extracted Clone Hero song folders into:", 2, muted);
                    text(64, 305, root.string(), 2, mint, 68);
                    text(64, 367, "Each folder needs notes.chart or notes.mid + audio.", 2, muted);
                } else {
                    int first = std::max(0, int(selected) - 3);
                    for (int row = 0; row < 7 && first + row < int(entries.size()); ++row) {
                        int index = first + row;
                        float y = 180 + row * 59;
                        if (size_t(index) == selected) {
                            rect(60, y - 9, 770, 53, {32, 47, 62, 255});
                            rect(60, y - 9, 4, 53, mint);
                        }
                        text(81, y, entries[index].name, 2, size_t(index) == selected ? ink : muted, 43);
                        text(81, y + 24,
                             entries[index].error.empty() ? entries[index].artist
                                                          : "UNSUPPORTED / INVALID SONG",
                             1, entries[index].error.empty() ? muted : colors[1], 80);
                    }
                    rect(865, 177, 355, 400, {20, 29, 42, 255});
                    text(887, 200, "READY TO PLAY", 2, mint);
                    if (song) {
                        text(887, 255, song->tracks[trackIndex].instrument, 3);
                        text(887, 296, difficultyName(song->tracks[trackIndex].difficulty), 2, colors[2]);
                        text(887, 345, std::to_string(song->tracks[trackIndex].notes.size()) + " NOTES", 2);
                        text(887, 380, std::to_string(song->audio.size()) + " AUDIO STEMS", 2, muted);
                        text(887, 418, settings.gamepad ? "PRESS-TO-HIT" : "STRUM MODE", 2, mint);
                        text(887, 460, "L/R: change track", 1, muted);
                        text(887, 489, "A / Enter: play", 2);
                        if (!song->warnings.empty())
                            text(887, 535, "See --inspect for warnings", 1, colors[2]);
                    }
                }
                if (!message.empty())
                    text(64, 603, message, 1, colors[1], 140);
                text(63, 660, "UP/DOWN  Browse     A/ENTER  Play     Y/TAB  Settings     MINUS/ESC  Exit", 1,
                     muted);
            } else if (screen == Screen::Settings) {
                text(60, 45, "SETTINGS", 4);
                text(64, 98, "LEFT/RIGHT TO ADJUST. A/ENTER TO REMAP. MINUS/ESC TO SAVE.", 1, muted);
                std::vector<std::string> rows = {
                    std::string("Controller mode: ") + (settings.gamepad ? "Press-to-hit" : "Strum"),
                    "Audio/input offset: " + std::to_string(int(settings.audioMs)) + " ms",
                    "Visual offset: " + std::to_string(int(settings.videoMs)) + " ms",
                    "Highway travel: " + std::to_string(int(settings.travel * 1000)) + " ms"};
                for (int i = 0; i < 5; ++i)
                    rows.push_back("Fret " + std::to_string(i + 1) + ": " +
                                   bindingName(settings.bindings[i]));
                rows.push_back("Reset defaults");
                for (int i = 0; i < 10; ++i) {
                    float y = 145 + i * 43;
                    if (i == settingRow)
                        rect(60, y - 7, 1030, 34, {32, 47, 62, 255});
                    text(80, y, rows[i], 2, i == settingRow ? mint : ink);
                }
                if (remapping >= 0)
                    text(80, 595, "PRESS A CONTROLLER BUTTON OR TRIGGER...", 2, colors[2]);
                else
                    text(80, 595, "Positive audio offset: notes are judged later. Start at 0 ms.", 1, muted);
                if (!message.empty())
                    text(80, 630, message, 1, colors[1]);
            } else if (session && song) {
                double time = screen == Screen::Playing
                                  ? audio.position() - song->offset - settings.audioMs / 1000
                                  : frozen;
                text(45, 34, song->name, 3, ink, 37);
                text(47, 72, song->artist + " / " + difficultyName(session->track->difficulty), 1, muted, 75);
                text(1000, 40, timeText(audio.position()) + " / " + timeText(audio.duration()), 2, muted);
                highway(*song, *session, settings, time - settings.videoMs / 1000, frets);
                text(62, 206, "SCORE", 2, muted);
                text(62, 242, std::to_string(int(session->score)), 4);
                text(62, 322, "COMBO", 2, muted);
                text(62, 358, std::to_string(session->combo), 4, mint);
                text(1010, 206, "MULTIPLIER", 2, muted);
                text(1030, 246, "x" + std::to_string(session->multiplier()), 5, colors[2]);
                text(990, 359, "STAR POWER", 2, muted);
                rect(990, 397, 220, 15, {37, 49, 63, 255});
                rect(990, 397, float(220 * session->power), 15, mint);
                text(995, 436, "X / LSHIFT", 1, mint);
                text(44, 681, "PLUS/P: pause   A/SPACE: open note   Press fret buttons on the beat", 1,
                     muted);
                if (time < 0)
                    text(589, 331, std::to_string(int(std::ceil(-time))), 6, mint);
                if (screen == Screen::Paused || screen == Screen::Results) {
                    rect(0, 0, W, H, {4, 8, 14, 210});
                    text(400, 200, screen == Screen::Paused ? "PAUSED" : "SONG COMPLETE", 4, mint);
                    if (screen == Screen::Results) {
                        text(400, 290, "SCORE  " + std::to_string(int(session->score)), 3);
                        text(400, 345,
                             "HIT " + std::to_string(session->hits) + " / " +
                                 std::to_string(session->state.size()),
                             2);
                        text(400, 389, "BEST COMBO  " + std::to_string(session->maxCombo), 2);
                    } else
                        text(400, 305, "A / ENTER: RESUME", 2);
                    text(400, 470, "Y / R: RESTART", 2);
                    text(400, 515, "MINUS / ESC: SONG LIST", 2, muted);
                    if (!message.empty())
                        text(120, 590, message, 1, colors[1], 130);
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
            previousButtons = buttons;
            for (int i = 0; i < SDL_NUM_SCANCODES; ++i)
                previousKeys[i] = keys[i];
            SDL_Delay(smoke ? 16 : 1);
        }
        audio.stop();
        if (!smoke)
            settings.save(config);
        if (virtualPad)
            SDL_JoystickClose(virtualPad);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        // Controller destructor runs before process exit; leave SDL lifetime to OS here.
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Fretboard: " << e.what() << '\n';
        return 1;
    }
}
