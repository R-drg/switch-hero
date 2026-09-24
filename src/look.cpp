#include "look.hpp"
#include "background.hpp"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"
#pragma GCC diagnostic pop
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <array>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fret::look {
SDL_Renderer *renderer = nullptr;
const std::vector<Palette> &palettes() {
    // Six colours each: green, red, yellow, blue and orange frets, then open
    // notes. Palettes need not follow the frets: lane position alone tells
    // the notes apart, so a palette may repeat a colour or be one colour.
    auto one = [](SDL_Color c, SDL_Color open) { return std::array<SDL_Color, 6>{c, c, c, c, c, open}; };
    auto two = [](SDL_Color a, SDL_Color b, SDL_Color open) { return std::array<SDL_Color, 6>{a, b, a, b, a, open}; };
    static const std::vector<Palette> list = {
        {"CLASSIC",
         {SDL_Color{40, 210, 70, 255}, {232, 36, 44, 255}, {250, 208, 28, 255}, {38, 112, 246, 255}, {255, 132, 18, 255},
          {178, 76, 255, 255}}},
        // Soft and milky, but still five hues far enough apart to read at speed:
        // mint, rose, butter, baby blue, peach, and lavender for open notes.
        {"PASTEL",
         {SDL_Color{140, 226, 170, 255}, {255, 150, 172, 255}, {255, 232, 140, 255}, {148, 196, 255, 255},
          {255, 186, 138, 255}, {200, 166, 255, 255}}},
        {"NEON",
         {SDL_Color{57, 255, 20, 255}, {255, 20, 110, 255}, {255, 240, 0, 255}, {0, 210, 255, 255}, {255, 110, 0, 255},
          {196, 60, 255, 255}}},
        // Okabe-Ito colours, which stay distinct with the common colour blindnesses.
        {"COLORBLIND",
         {SDL_Color{0, 158, 115, 255}, {213, 94, 0, 255}, {240, 228, 66, 255}, {86, 180, 233, 255}, {230, 159, 0, 255},
          {204, 121, 167, 255}}},
        // One colour for every fret.
        {"BLOOD", one({196, 8, 18, 255}, {110, 0, 10, 255})},
        {"BONE", one({232, 222, 196, 255}, {170, 158, 132, 255})},
        {"OBSIDIAN", one({52, 40, 74, 255}, {120, 60, 160, 255})},
        {"GOLD RECORD", one({255, 190, 30, 255}, {255, 236, 160, 255})},
        {"PLATINUM", one({206, 212, 226, 255}, {150, 160, 180, 255})},
        {"RADIOACTIVE", one({120, 255, 40, 255}, {230, 255, 60, 255})},
        {"GHOST", one({190, 214, 236, 255}, {240, 248, 255, 255})},
        // Two colours, alternating across the neck.
        {"BUMBLEBEE", two({255, 206, 0, 255}, {34, 32, 30, 255}, {255, 236, 120, 255})},
        {"CANDY CANE", two({230, 20, 40, 255}, {244, 244, 240, 255}, {120, 220, 120, 255})},
        {"VAPORWAVE", two({255, 106, 200, 255}, {70, 230, 255, 255}, {180, 120, 255, 255})},
        {"ROYALTY", two({120, 40, 200, 255}, {255, 196, 40, 255}, {250, 240, 220, 255})},
        {"CYBERPUNK", two({255, 230, 0, 255}, {255, 0, 150, 255}, {0, 255, 240, 255})},
        // Gradients across the neck.
        {"INFERNO",
         {SDL_Color{255, 40, 0, 255}, {255, 100, 0, 255}, {255, 190, 0, 255}, {255, 100, 0, 255}, {255, 40, 0, 255},
          {255, 240, 190, 255}}},
        {"GLACIER",
         {SDL_Color{150, 236, 255, 255}, {80, 190, 255, 255}, {220, 250, 255, 255}, {40, 130, 230, 255},
          {170, 220, 255, 255}, {255, 255, 255, 255}}},
        {"SUNSET",
         {SDL_Color{255, 90, 120, 255}, {255, 130, 90, 255}, {255, 190, 90, 255}, {200, 90, 200, 255},
          {120, 70, 200, 255}, {255, 220, 180, 255}}},
        {"DEEP SEA",
         {SDL_Color{0, 200, 180, 255}, {0, 140, 200, 255}, {60, 230, 220, 255}, {20, 80, 180, 255}, {0, 170, 140, 255},
          {140, 255, 240, 255}}},
        {"RASTA",
         {SDL_Color{0, 160, 60, 255}, {255, 210, 0, 255}, {220, 20, 30, 255}, {255, 210, 0, 255}, {0, 160, 60, 255},
          {40, 30, 20, 255}}},
        {"ARCADE",
         {SDL_Color{0, 228, 54, 255}, {255, 0, 77, 255}, {255, 236, 39, 255}, {41, 173, 255, 255}, {255, 163, 0, 255},
          {255, 119, 168, 255}}},
        {"CAMO",
         {SDL_Color{96, 112, 52, 255}, {136, 110, 70, 255}, {70, 88, 44, 255}, {160, 144, 100, 255}, {84, 70, 50, 255},
          {186, 176, 130, 255}}},
        {"SAKURA",
         {SDL_Color{255, 196, 214, 255}, {240, 110, 160, 255}, {255, 236, 244, 255}, {200, 80, 140, 255},
          {255, 160, 190, 255}, {140, 200, 120, 255}}},
    };
    return list;
}
std::array<SDL_Color, 6> lanes = palettes()[0].lanes;
namespace {
constexpr float Tau = 6.2831853f;
bool grainOn = true;

// ---------------------------------------------------------------- utilities
struct Canvas {
    int w, h;
    std::vector<Uint8> rgba;
    Canvas(int w, int h) : w(w), h(h), rgba(size_t(w) * h * 4) {}
    void set(int x, int y, SDL_Color c) {
        auto *p = &rgba[(size_t(y) * w + x) * 4];
        p[0] = c.r, p[1] = c.g, p[2] = c.b, p[3] = c.a;
    }
};
SDL_Texture *upload(Canvas c, SDL_BlendMode blend, SDL_ScaleMode scale = SDL_ScaleModeLinear) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(c.rgba.data(), c.w, c.h, 32, c.w * 4, SDL_PIXELFORMAT_RGBA32);
    if (!s)
        return nullptr;
    SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, s);
    SDL_FreeSurface(s);
    if (t) {
        SDL_SetTextureBlendMode(t, blend);
        SDL_SetTextureScaleMode(t, scale);
    }
    return t;
}
// Pixel functions are pure, so a big canvas is split into bands of rows,
// painted by this thread and two helpers on the spare cores at once. The
// start-up textures took over a second of the console's main thread in a row.
Canvas paint(int w, int h, const std::function<SDL_Color(int, int)> &pixel) {
    Canvas c(w, h);
    auto rows = [&](int from, int to) {
        for (int y = from; y < to; ++y)
            for (int x = 0; x < w; ++x)
                c.set(x, y, pixel(x, y));
    };
    constexpr int Bands = 3;
    if (w * h < 16384) {
        rows(0, h);
        return c;
    }
    std::array<std::thread, Bands - 1> helpers;
    for (int b = 1; b < Bands; ++b)
        helpers[size_t(b - 1)] = std::thread([&, b] {
            runInBackground();
            rows(h * b / Bands, h * (b + 1) / Bands);
        });
    rows(0, h / Bands);
    for (auto &t : helpers)
        t.join();
    return c;
}
Uint8 u8(float v) { return Uint8(std::clamp(v, 0.0f, 255.0f)); }
float smooth(float t) { return t * t * (3 - 2 * t); }
float valueNoise(float x, float y, int period, uint32_t seed) {
    int xi = int(std::floor(x)), yi = int(std::floor(y));
    float fx = smooth(x - xi), fy = smooth(y - yi);
    auto at = [&](int a, int b) {
        a = ((a % period) + period) % period, b = ((b % period) + period) % period;
        return hash01(uint32_t(a) * 73856093u ^ uint32_t(b) * 19349663u ^ seed);
    };
    float top = at(xi, yi) + (at(xi + 1, yi) - at(xi, yi)) * fx;
    float bottom = at(xi, yi + 1) + (at(xi + 1, yi + 1) - at(xi, yi + 1)) * fx;
    return top + (bottom - top) * fy;
}
float fbm(float x, float y, int period, uint32_t seed, int octaves = 4) {
    float sum = 0, amp = .5f, norm = 0;
    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise(x, y, period, seed + i * 101) * amp;
        norm += amp;
        x *= 2, y *= 2, period *= 2, amp *= .5f;
    }
    return sum / norm;
}
// Draws are batched: consecutive geometry with the same texture (or none) is
// collected into one mesh and sent as a single draw call. Each call costs the
// console's driver real CPU time, and split screen drew four highways' worth of
// them, several hundred a frame. Anything that changes renderer state flushes
// first, so the order of what reaches the screen never changes.
struct Batch {
    SDL_Texture *texture = nullptr;
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
} batch;
void flushBatch() {
    if (!batch.indices.empty())
        SDL_RenderGeometry(renderer, batch.texture, batch.vertices.data(), int(batch.vertices.size()),
                           batch.indices.data(), int(batch.indices.size()));
    batch.vertices.clear(), batch.indices.clear();
}
void geometry(SDL_Texture *t, const SDL_Vertex *v, size_t vn, const int *idx, size_t in) {
    if (!vn || !in)
        return;
    if (t != batch.texture || batch.vertices.size() + vn > 60000) {
        flushBatch();
        batch.texture = t;
    }
    const int base = int(batch.vertices.size());
    batch.vertices.insert(batch.vertices.end(), v, v + vn);
    for (size_t i = 0; i < in; ++i)
        batch.indices.push_back(base + idx[i]);
}
void geometry(SDL_Texture *t, const std::vector<SDL_Vertex> &v, const std::vector<int> &idx) {
    geometry(t, v.data(), v.size(), idx.data(), idx.size());
}
// One textured quad, corners clockwise from the top left, tinted by `c`.
void texturedQuad(SDL_Texture *t, const SDL_FPoint (&p)[4], SDL_Color c) {
    const SDL_Vertex v[4] = {{p[0], c, {0, 0}}, {p[1], c, {1, 0}}, {p[2], c, {1, 1}}, {p[3], c, {0, 1}}};
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    geometry(t, v, 4, idx, 6);
}
SDL_FPoint rotate(SDL_FPoint p, SDL_FPoint around, float radians) {
    float c = std::cos(radians), s = std::sin(radians), x = p.x - around.x, y = p.y - around.y;
    return {around.x + x * c - y * s, around.y + x * s + y * c};
}

// ---------------------------------------------------------------- fonts
struct Glyph {
    int16_t x, y, w, h, xoff, yoff;
    uint16_t advance;
};
struct Font {
    int atlasW = 0, atlasH = 0, px = 1, line = 1, ascent = 1, pad = 0;
    std::vector<Glyph> glyphs;
    std::vector<Uint8> alpha, halo;
    SDL_Texture *alphaTexture = nullptr, *haloTexture = nullptr;
};
std::array<Font, 3> fonts;

// A file from the assets folder (the NRO's RomFS on the console), by its path
// inside it, such as "fonts/body.font". Empty when it cannot be found.
std::vector<Uint8> readAsset(const std::string &relative) {
    std::vector<std::string> candidates;
#ifdef __SWITCH__
    candidates.push_back("romfs:/" + relative);
    candidates.push_back("sdmc:/switch/switch-hero/" + relative);
#else
    candidates.push_back("assets/" + relative);
    if (char *base = SDL_GetBasePath()) {
        candidates.push_back(std::string(base) + "assets/" + relative);
        candidates.push_back(std::string(base) + "../assets/" + relative);
        SDL_free(base);
    }
#endif
    for (auto &path : candidates)
        if (SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb")) {
            std::vector<Uint8> data(size_t(std::max<Sint64>(0, SDL_RWsize(rw))));
            size_t got = data.empty() ? 0 : SDL_RWread(rw, data.data(), 1, data.size());
            SDL_RWclose(rw);
            if (got == data.size() && !data.empty())
                return data;
        }
    return {};
}
std::vector<Uint8> readFile(const std::string &name) {
    auto data = readAsset("fonts/" + name);
    if (data.empty())
        throw std::runtime_error("Missing font asset " + name + " (expected in assets/fonts)");
    return data;
}
void loadFont(Font &f, const std::string &name) {
    auto data = readFile(name);
    size_t at = 0;
    auto u16 = [&]() {
        if (at + 2 > data.size())
            throw std::runtime_error("Truncated font " + name);
        uint16_t v = uint16_t(data[at] | data[at + 1] << 8);
        at += 2;
        return v;
    };
    if (data.size() < 4 || std::string(data.begin(), data.begin() + 4) != "FNT1")
        throw std::runtime_error("Invalid font " + name);
    at = 4;
    f.atlasW = u16(), f.atlasH = u16(), f.px = u16(), f.line = u16(), f.ascent = u16(), f.pad = u16();
    f.glyphs.resize(u16());
    for (auto &g : f.glyphs) {
        g.x = int16_t(u16()), g.y = int16_t(u16()), g.w = int16_t(u16()), g.h = int16_t(u16());
        g.xoff = int16_t(u16()), g.yoff = int16_t(u16()), g.advance = u16();
    }
    const size_t pixels = size_t(f.atlasW) * f.atlasH;
    if (at + pixels * 2 != data.size())
        throw std::runtime_error("Corrupt font " + name);
    f.alpha.assign(data.begin() + at, data.begin() + at + pixels);
    f.halo.assign(data.begin() + at + pixels, data.end());
}
SDL_Texture *alphaTexture(const std::vector<Uint8> &a, int w, int h) {
    if (a.empty() || w <= 0 || h <= 0)
        return nullptr;
    Canvas c(w, h);
    for (size_t i = 0; i < a.size(); ++i) {
        auto *p = &c.rgba[i * 4];
        p[0] = p[1] = p[2] = 255, p[3] = a[i];
    }
    return upload(c, SDL_BLENDMODE_BLEND);
}
// Glyphs are indexed by Latin-1 code. Fonts baked before the Latin-1 range
// was added stop at '~'; there, and on the empty control slots, draw '?'.
const Glyph *glyphFor(const Font &f, unsigned char ch) {
    if (ch < 32 || ch - 32 >= int(f.glyphs.size()) || (ch >= 127 && ch < 160))
        ch = '?';
    return &f.glyphs[ch - 32];
}
// The baked fonts hold printable ASCII and Latin-1, which covers Portuguese
// and most chart titles. Text arrives as UTF-8, so turn it into one Latin-1
// byte per character, fold typographic punctuation to its plain form and show
// anything else as a single '?'.
std::string sanitize(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char ch = s[i];
        if (ch < 128) {
            out += ch < 32 ? ' ' : char(ch);
            ++i;
            continue;
        }
        const int extra = ch >= 0xF0 ? 3 : ch >= 0xE0 ? 2 : ch >= 0xC0 ? 1 : 0;
        uint32_t cp = extra == 3 ? ch & 0x07 : extra == 2 ? ch & 0x0F : ch & 0x1F;
        size_t j = i + 1;
        for (int k = 0; k < extra && j < s.size() && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80; ++k, ++j)
            cp = (cp << 6) | (static_cast<unsigned char>(s[j]) & 0x3F);
        if (extra == 0 || j - i != size_t(extra + 1))
            cp = 0; // stray or truncated byte
        i = j;
        if (cp == 0x00AD)
            continue; // soft hyphen: invisible, and the fonts have no glyph for it
        if (cp > 0xA0 && cp <= 0xFF && cp != 0xB4)
            out += char(cp);
        else if (cp == 0x2018 || cp == 0x2019 || cp == 0x00B4)
            out += '\'';
        else if (cp == 0x201C || cp == 0x201D)
            out += '"';
        else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2010)
            out += '-';
        else if (cp == 0x2026)
            out += "...";
        else if (cp == 0x00A0)
            out += ' ';
        else if (cp == 0x0152 || cp == 0x0153)
            out += cp == 0x0152 ? "OE" : "oe";
        else
            out += '?';
    }
    return out;
}

// ---------------------------------------------------------------- baked art
SDL_Texture *glowTex = nullptr, *wallTex = nullptr, *vignetteTex = nullptr, *scanTex = nullptr, *tapeTex = nullptr,
            *metalTex = nullptr, *grainTex = nullptr, *ringTex = nullptr, *sparkTex = nullptr, *flareTex = nullptr;
std::array<SDL_Texture *, BoardCount> boardTex{};
constexpr int FlameFrames = 8;
std::array<std::array<SDL_Texture *, FlameFrames>, 2> flameTex{};
constexpr int SpriteW = 256, SpriteH = 192;
constexpr float SpriteCx = 128, SpriteCy = 72, SpriteRx = 116, SpriteRy = 50;
// Gem and fret-button textures, one set per note colour palette. Split screen
// can show a different palette on every board, so the sets are kept rather
// than rebuilt on each switch; the least recently used go past a handful.
struct PaletteSet {
    size_t palette = 0;
    uint64_t used = 0;
    std::array<std::array<SDL_Texture *, GemStyles>, 6> gems{};
    std::array<std::array<SDL_Texture *, 2>, 5> receptors{};
    void destroy() {
        for (auto &set : gems)
            for (auto *&t : set)
                SDL_DestroyTexture(t), t = nullptr;
        for (auto &set : receptors)
            for (auto *&t : set)
                SDL_DestroyTexture(t), t = nullptr;
    }
};
constexpr size_t maxPaletteSets = 6; // four players, plus room to browse
std::vector<PaletteSet> paletteSets;
size_t activePalette = 0;
uint64_t paletteClock = 0;
bool texturesReady = false; // createAll() has run, so fret buttons can be baked
// The set for the palette in use, made (evicting the stalest) if missing.
// The reference is only good until the next call.
PaletteSet &paletteSet() {
    for (auto &set : paletteSets)
        if (set.palette == activePalette) {
            set.used = ++paletteClock;
            return set;
        }
    if (paletteSets.size() >= maxPaletteSets) {
        auto oldest = std::min_element(paletteSets.begin(), paletteSets.end(),
                                       [](const PaletteSet &a, const PaletteSet &b) { return a.used < b.used; });
        flushBatch(); // queued draws may still use its textures
        oldest->destroy();
        paletteSets.erase(oldest);
    }
    paletteSets.push_back({});
    paletteSets.back().palette = activePalette;
    paletteSets.back().used = ++paletteClock;
    return paletteSets.back();
}

void fan(const std::vector<SDL_FPoint> &edge, SDL_FPoint centre, SDL_Color inner, SDL_Color outer) {
    std::vector<SDL_Vertex> v{{centre, inner, {0, 0}}};
    std::vector<int> idx;
    for (size_t i = 0; i < edge.size(); ++i) {
        v.push_back({edge[i], outer, {0, 0}});
        idx.insert(idx.end(), {0, int(i + 1), int((i + 1) % edge.size() + 1)});
    }
    geometry(nullptr, v, idx);
}
void extrude(const std::vector<SDL_FPoint> &edge, float depth, SDL_Color upper, SDL_Color lower) {
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    for (size_t i = 0; i < edge.size(); ++i) {
        v.push_back({edge[i], upper, {0, 0}});
        v.push_back({{edge[i].x, edge[i].y + depth}, lower, {0, 0}});
        int a = int(i * 2), b = int((i + 1) % edge.size() * 2);
        idx.insert(idx.end(), {a, a + 1, b, b, a + 1, b + 1});
    }
    geometry(nullptr, v, idx);
}
std::vector<SDL_FPoint> outline(float cx, float cy, float rx, float ry, bool starShape, int sides = 48) {
    std::vector<SDL_FPoint> pts;
    const int n = starShape ? 10 : sides;
    for (int i = 0; i < n; ++i) {
        float a = -Tau / 4 + float(i) * Tau / n;
        float r = starShape ? (i % 2 ? .47f : 1.1f) : 1;
        pts.push_back({cx + std::cos(a) * rx * r, cy + std::sin(a) * ry * r});
    }
    return pts;
}
SDL_Color shade(SDL_Color c, float k, int add = 0) {
    auto f = [&](Uint8 v) { return Uint8(std::clamp(int(v * k) + add, 0, 255)); };
    return {f(c.r), f(c.g), f(c.b), c.a};
}
// The band between two outlines of the same point count, shaded from the outer
// edge to the inner one.
void ring(const std::vector<SDL_FPoint> &outer, const std::vector<SDL_FPoint> &inner, SDL_Color out, SDL_Color in) {
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    for (size_t i = 0; i < outer.size(); ++i) {
        v.push_back({outer[i], out, {0, 0}});
        v.push_back({inner[i], in, {0, 0}});
        int a = int(i * 2), b = int((i + 1) % outer.size() * 2);
        idx.insert(idx.end(), {a, a + 1, b, b, a + 1, b + 1});
    }
    geometry(nullptr, v, idx);
}
void flat(const std::vector<SDL_FPoint> &edge, SDL_Color c) {
    SDL_FPoint centre{0, 0};
    for (auto p : edge)
        centre.x += p.x / edge.size(), centre.y += p.y / edge.size();
    fan(edge, centre, c, c);
}
const SDL_Color paper{246, 243, 234, 255}, paperEdge{176, 172, 162, 255}, inkBlack{14, 14, 16, 255};
// The one highlight a printed fret button gets: a hard-edged flat shape, not a
// gradient, in keeping with their die-cut look.
void shine(float cx, float cy, float rx, float ry) {
    flat(outline(cx - rx * .34f, cy - ry * .42f, rx * .3f, ry * .15f, false, 24), {255, 255, 255, 235});
    flat(outline(cx + rx * .02f, cy - ry * .5f, rx * .06f, ry * .06f, false, 12), {255, 255, 255, 235});
}
// Fret buttons are printed die-cut rings: a white border, black ink,
// a band of lane colour and a black well. Pressed, the well fills with colour.
void bakeReceptor(SDL_Color c, bool pressed) {
    auto shape = [&](float s, float dy = 0) { return outline(SpriteCx, SpriteCy + dy, SpriteRx * s, SpriteRy * s, false); };
    const float lift = 10;
    flat(outline(SpriteCx + 6, SpriteCy + lift + 10, SpriteRx, SpriteRy, false), {0, 0, 0, 130});
    extrude(shape(1), lift, paperEdge, shade(paperEdge, .6f));
    flat(shape(1), paper);
    flat(shape(.88f), inkBlack);
    flat(shape(.8f), c);
    flat(shape(.66f), inkBlack);
    if (pressed) {
        flat(shape(.56f, 2), shade(c, .62f));
        flat(outline(SpriteCx, SpriteCy - 1, SpriteRx * .52f, SpriteRy * .46f, false), c);
        shine(SpriteCx, SpriteCy, SpriteRx * .52f, SpriteRy * .52f);
    } else
        // An empty well still shows its depth: the back wall catches a little light.
        ring(shape(.66f), shape(.5f, 6), {60, 60, 68, 255}, inkBlack);
}
// ---------------------------------------------------------------- 3D gems
// Gems are rendered, not drawn: each sprite pixel is ray-marched once at start
// up against a signed distance field of the gem, lit by one key light and the
// room, with soft shadows and ambient occlusion, then flattened into a texture.
// Each gem is a cut jewel in a gunmetal bezel: eight crown facets rising to a
// flat table. Star notes are cut stars; hammer-ons light the table up white,
// which stays readable at play size where a small mark would not.
namespace gem3d {
struct V3 {
    float x, y, z;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 norm(V3 a) { return a * (1 / std::sqrt(std::max(1e-12f, dot(a, a)))); }
enum Material { Bezel, Table, Jewel };
// The view matches the sprite: the top face reads as an ellipse Ry/Rx tall, so
// the board is seen from asin(Ry/Rx) above; one unit is SpriteRx pixels.
const float SinView = SpriteRy / SpriteRx, CosView = std::sqrt(1 - SinView * SinView);
const V3 ToCamera{0, SinView, CosView};
const V3 KeyLight = norm({-.55f, .85f, .5f});
constexpr float Anchor = .44f; // height that lands on the sprite's anchor point
// Profiles in the board plane (x, z); negative inside.
float roundProfile(float x, float z, float r) { return std::sqrt(x * x + z * z) - r; }
float starProfile(float x, float z, float r) {
    // Five-point star pointing down the highway (-z), after Inigo Quilez.
    const float k1x = .809016994f, k1y = -.587785252f, rf = .5f;
    float px = std::abs(x), py = -z;
    float d = std::max(k1x * px + k1y * py, 0.0f);
    px -= 2 * d * k1x, py -= 2 * d * k1y;
    d = std::max(-k1x * px + k1y * py, 0.0f);
    px -= 2 * d * -k1x, py -= 2 * d * k1y;
    px = std::abs(px);
    py -= r;
    const float bax = rf * -k1y, bay = rf * k1x - 1;
    const float h = std::clamp((px * bax + py * bay) / (bax * bax + bay * bay), 0.0f, r);
    const float ex = px - bax * h, ey = py - bay * h;
    return std::sqrt(ex * ex + ey * ey) * ((py * bax - px * bay) > 0 ? 1.0f : -1.0f);
}
// A profile extruded between two heights with its edges rounded by `r`.
float extrude(float profile, float y, float y0, float y1, float r) {
    const float wx = profile + r, wy = std::abs(y - (y0 + y1) / 2) - (y1 - y0) / 2 + r;
    return std::min(std::max(wx, wy), 0.0f) + std::sqrt(std::max(wx, 0.0f) * std::max(wx, 0.0f) +
                                                         std::max(wy, 0.0f) * std::max(wy, 0.0f)) - r;
}
struct Shape {
    bool star, hammer;
    float map(V3 p, Material *m = nullptr) const {
        const float bezel = star ? extrude(starProfile(p.x, p.z, 1.05f), p.y, 0, .22f, .05f)
                                 : extrude(roundProfile(p.x, p.z, 1), p.y, 0, .22f, .05f);
        float jewel;
        if (star) {
            // A cut star: flanks bevelled up from the edge to a flat table.
            const float edge = starProfile(p.x, p.z, .9f);
            const float top = .28f + std::min(.16f, .75f * std::max(0.0f, -edge));
            jewel = std::max({edge, .12f - p.y, (p.y - top) * .8f});
        } else {
            // A round brilliant, simplified: a girdle, eight crown facets and a
            // steeper ring below them, all meeting a flat table.
            jewel = std::max({roundProfile(p.x, p.z, .8f), .12f - p.y, p.y - .44f});
            for (int k = 0; k < 8; ++k) {
                const float a = (k + .5f) * Tau / 8, phi = .84f; // facet tilt from vertical
                const V3 n{std::cos(a) * std::sin(phi), std::cos(phi), std::sin(a) * std::sin(phi)};
                jewel = std::max(jewel, dot(p, n) - (.8f * std::sin(phi) + .26f * std::cos(phi)));
                const float b = k * Tau / 8, psi = 1.1f;
                const V3 q{std::cos(b) * std::sin(psi), std::cos(psi), std::sin(b) * std::sin(psi)};
                jewel = std::max(jewel, dot(p, q) - (.8f * std::sin(psi) + .2f * std::cos(psi)));
            }
        }
        const float d = std::min(bezel, jewel);
        if (m)
            *m = d == bezel ? Bezel : (hammer && p.y > (star ? .40f : .435f)) ? Table : Jewel;
        return d;
    }
    V3 normal(V3 p) const {
        const float e = .0015f;
        const V3 a{1, -1, -1}, b{-1, -1, 1}, c{-1, 1, -1}, d{1, 1, 1};
        return norm(a * map(p + a * e) + b * map(p + b * e) + c * map(p + c * e) + d * map(p + d * e));
    }
    float occlusion(V3 p, V3 n) const {
        float o = 0, weight = 1;
        for (int i = 1; i <= 5; ++i) {
            const float h = .03f * float(i);
            o += (h - map(p + n * h)) * weight;
            weight *= .6f;
        }
        return std::clamp(1 - 2.2f * o, 0.0f, 1.0f);
    }
    float shadow(V3 p) const {
        float lit = 1, t = .02f;
        for (int i = 0; i < 32 && t < 2.5f; ++i) {
            const float h = map(p + KeyLight * t);
            if (h < .0005f)
                return 0;
            lit = std::min(lit, 10 * h / t);
            t += std::clamp(h, .01f, .2f);
        }
        return std::clamp(lit, 0.0f, 1.0f);
    }
};
// What the gems reflect: a dim room with a lighter ceiling, a bright stage
// softbox up the highway behind them (it is what puts a highlight on the tops),
// and a weaker fill off to the right. Reflections are what make lacquer read as
// lacquer and metal as metal; direct lights alone gave plastic toys.
float environment(V3 r) {
    float e = r.y > 0 ? .06f + .45f * std::pow(r.y, .7f) : .015f;
    const V3 softbox = norm({-.18f, .45f, -.87f}), fill = norm({.85f, .35f, .2f});
    e += std::pow(std::max(0.0f, dot(r, softbox)), 70.0f) * 7;
    e += std::pow(std::max(0.0f, dot(r, fill)), 16.0f) * .6f;
    return e;
}
// Per pixel, lighting kept as terms so one render serves every lane colour:
// colour = laneColour * cap + rest + white * spec, all in linear light.
struct Texel {
    float cap = 0, rest[3] = {0, 0, 0}, spec = 0, cover = 0, shadow = 0;
};
constexpr int Super = 2; // 2x2 samples per pixel for clean edges
std::vector<Texel> render(bool star, bool hammer) {
    const Shape shape{star, hammer};
    std::vector<Texel> out(size_t(SpriteW) * SpriteH);
    const V3 right{1, 0, 0}, up{0, CosView, -SinView}, dir = ToCamera * -1;
    for (int py = 0; py < SpriteH; ++py)
        for (int px = 0; px < SpriteW; ++px) {
            Texel t;
            int hits = 0, misses = 0;
            float shadowSum = 0;
            for (int sy = 0; sy < Super; ++sy)
                for (int sx = 0; sx < Super; ++sx) {
                    const float u = (px + (sx + .5f) / Super - SpriteCx) / SpriteRx;
                    const float v = -(py + (sy + .5f) / Super - SpriteCy) / SpriteRx;
                    // Start well in front of the gem and march back along the view.
                    V3 p = right * u + up * v + V3{0, Anchor, 0} + ToCamera * 3;
                    float travelled = 0;
                    bool hit = false;
                    if (std::abs(u) < 1.2f)
                        for (int i = 0; i < 96 && travelled < 6; ++i) {
                            const float d = shape.map(p);
                            if (d < .0008f) {
                                hit = true;
                                break;
                            }
                            p = p + dir * (d * .85f), travelled += d * .85f;
                        }
                    if (!hit) {
                        // The board: where the ray meets y = 0, shade it by the
                        // gem's shadow and by how close the gem sits (contact).
                        V3 q = right * u + up * v + V3{0, Anchor, 0} + ToCamera * 3;
                        const float tGround = q.y / SinView;
                        q = q + dir * tGround;
                        float a = 0;
                        if (std::abs(q.x) < 1.8f && std::abs(q.z) < 1.8f) {
                            const float contact = 1 - std::clamp(shape.map(q) / .35f, 0.0f, 1.0f);
                            const float cast = 1 - shape.shadow(q + V3{0, .002f, 0});
                            a = std::max(cast * .45f, contact * contact * .6f);
                        }
                        shadowSum += a, ++misses;
                        continue;
                    }
                    ++hits;
                    Material m;
                    shape.map(p, &m);
                    const V3 n = shape.normal(p);
                    const float ao = shape.occlusion(p, n);
                    const float lit = shape.shadow(p + n * .004f);
                    const float nl = std::max(0.0f, dot(n, KeyLight));
                    const V3 h = norm(KeyLight + ToCamera);
                    const float nh = std::max(0.0f, dot(n, h)), nv = std::max(0.0f, dot(n, ToCamera));
                    const float sky = .5f + .5f * n.y; // hemisphere ambient
                    const float ambient = (.12f + .38f * sky) * ao;
                    const float diffuse = ambient + nl * lit * .95f;
                    const V3 r = n * (2 * nv) - ToCamera; // the view reflected off the surface
                    const float reflected = environment(r) * (.35f + .65f * ao);
                    // Schlick's approximation: reflections strengthen at grazing angles.
                    const float schlick = std::pow(1 - nv, 5.0f);
                    if (m == Jewel) {
                        // Light seen through the stone: the view bent into it by the
                        // facets picks up the room, so each facet glows differently.
                        const float eta = 1 / 1.6f, cosi = nv;
                        const float k = 1 - eta * eta * (1 - cosi * cosi);
                        const V3 inward = norm(ToCamera * -eta + n * (eta * cosi - std::sqrt(std::max(0.0f, k))));
                        const V3 bounced = inward * -1; // out the back and up again, roughly
                        t.cap += diffuse * .5f + std::min(environment({bounced.x, std::abs(bounced.y), bounced.z}), 1.2f) * .38f;
                        // The surface reflection stays a sparkle, capped, so the flat
                        // table cannot wash the colour out to a pastel.
                        t.spec += std::min(reflected, 2.0f) * (.02f + .55f * schlick);
                    } else if (m == Table) {
                        const float w = diffuse * .8f + .16f; // a little glow of its own
                        for (float &c : t.rest)
                            c += w;
                        t.spec += reflected * (.04f + .5f * schlick);
                    } else {
                        // Gunmetal: mostly reflection, tinted cool, with little diffuse.
                        const float tint[3] = {.50f, .53f, .60f};
                        for (int c = 0; c < 3; ++c)
                            t.rest[c] += tint[c] * reflected * .32f + .03f * diffuse;
                        t.spec += std::pow(nh, 30.0f) * .12f * lit;
                    }
                }
            if (hits) {
                t.cap /= hits, t.spec /= hits;
                for (float &c : t.rest)
                    c /= hits;
            }
            t.cover = float(hits) / (Super * Super);
            t.shadow = misses ? shadowSum / misses : 0;
            out[size_t(py) * SpriteW + px] = t;
        }
    return out;
}
using Renders = std::array<std::vector<Texel>, GemStyles>;
Renders renderAll(bool background) {
    Renders out;
    std::array<std::thread, GemStyles> workers;
    for (int style = 0; style < GemStyles; ++style)
        workers[size_t(style)] = std::thread([&out, style, background] {
            if (background)
                runInBackground(Work::Bulk); // off the render core, behind any chart load
            out[size_t(style)] = render(style == GemStar || style == GemStarHopo, style == GemHopo || style == GemStarHopo);
        });
    for (auto &w : workers)
        w.join();
    return out;
}

// The renders ship pre-baked in assets/gems.bin: ray-marching them took about
// three CPU-seconds on a desktop, over ten on the console, at every launch.
// Bump RenderVersion whenever render() changes, and rebake with
// `switch-hero --bake-gems assets/gems.bin`; the gems test fails until then.
//
// Format, little endian: "GEMS", u32 RenderVersion, u16 width, u16 height,
// u16 styles; per style, a min and max float for each of the seven channels,
// then runs of u32 empty-texel count, u32 filled count, and that many texels
// of seven u16 channels, each quantized between its min and max.
constexpr uint32_t RenderVersion = 1;
constexpr int Channels = 7;
float &channel(Texel &t, int c) {
    return c == 0 ? t.cap : c < 4 ? t.rest[c - 1] : c == 4 ? t.spec : c == 5 ? t.cover : t.shadow;
}
bool empty(const Texel &t) {
    Texel copy = t;
    for (int c = 0; c < Channels; ++c)
        if (channel(copy, c) != 0)
            return false;
    return true;
}
std::vector<Uint8> encode(const Renders &renders) {
    std::vector<Uint8> out;
    auto put = [&](uint64_t v, int bytes) {
        for (int i = 0; i < bytes; ++i)
            out.push_back(Uint8(v >> (8 * i)));
    };
    auto putFloat = [&](float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        put(bits, 4);
    };
    out.insert(out.end(), {'G', 'E', 'M', 'S'});
    put(RenderVersion, 4), put(SpriteW, 2), put(SpriteH, 2), put(GemStyles, 2);
    for (const auto &texels : renders) {
        std::array<float, Channels> lo, hi;
        lo.fill(0), hi.fill(0);
        for (Texel t : texels)
            for (int c = 0; c < Channels; ++c)
                lo[size_t(c)] = std::min(lo[size_t(c)], channel(t, c)), hi[size_t(c)] = std::max(hi[size_t(c)], channel(t, c));
        for (int c = 0; c < Channels; ++c)
            putFloat(lo[size_t(c)]), putFloat(hi[size_t(c)]);
        for (size_t i = 0; i < texels.size();) {
            size_t gap = 0, run = 0;
            while (i + gap < texels.size() && empty(texels[i + gap]))
                ++gap;
            while (i + gap + run < texels.size() && !empty(texels[i + gap + run]))
                ++run;
            put(gap, 4), put(run, 4);
            for (size_t k = 0; k < run; ++k) {
                Texel t = texels[i + gap + k];
                for (int c = 0; c < Channels; ++c) {
                    const float span = hi[size_t(c)] - lo[size_t(c)];
                    const float q = span > 0 ? (channel(t, c) - lo[size_t(c)]) / span : 0;
                    put(uint16_t(std::lround(std::clamp(q, 0.0f, 1.0f) * 65535)), 2);
                }
            }
            i += gap + run;
        }
    }
    return out;
}
// False when the data is missing, damaged or from another RenderVersion.
bool decode(const std::vector<Uint8> &data, Renders &out) {
    size_t at = 0;
    auto get = [&](int bytes, uint64_t &v) {
        if (at + size_t(bytes) > data.size())
            return false;
        v = 0;
        for (int i = 0; i < bytes; ++i)
            v |= uint64_t(data[at + size_t(i)]) << (8 * i);
        at += size_t(bytes);
        return true;
    };
    uint64_t version, w, h, styles;
    if (data.size() < 4 || std::memcmp(data.data(), "GEMS", 4) != 0)
        return false;
    at = 4;
    if (!get(4, version) || !get(2, w) || !get(2, h) || !get(2, styles) || version != RenderVersion ||
        w != SpriteW || h != SpriteH || styles != GemStyles)
        return false;
    Renders result;
    for (auto &texels : result) {
        std::array<float, Channels> lo, hi;
        for (int c = 0; c < Channels; ++c) {
            uint64_t a, b;
            if (!get(4, a) || !get(4, b))
                return false;
            const uint32_t la = uint32_t(a), lb = uint32_t(b);
            std::memcpy(&lo[size_t(c)], &la, 4), std::memcpy(&hi[size_t(c)], &lb, 4);
        }
        texels.assign(size_t(SpriteW) * SpriteH, Texel{});
        for (size_t i = 0; i < texels.size();) {
            uint64_t gap, run;
            if (!get(4, gap) || !get(4, run) || gap + run == 0 || i + gap + run > texels.size())
                return false;
            i += size_t(gap);
            for (uint64_t k = 0; k < run; ++k, ++i)
                for (int c = 0; c < Channels; ++c) {
                    uint64_t q;
                    if (!get(2, q))
                        return false;
                    channel(texels[i], c) = lo[size_t(c)] + (hi[size_t(c)] - lo[size_t(c)]) * float(q) / 65535;
                }
        }
    }
    if (at != data.size())
        return false;
    out = std::move(result);
    return true;
}

// Loaded from the baked file; rendered here only if it is missing or stale.
// A render device reset only uploads them again. start() kicks this off at
// launch, and the first gem drawn waits for it if it is somehow not done.
Renders &renders() {
    static Renders cache;
    static std::once_flag once;
    std::call_once(once, [] {
        if (decode(readAsset("gems.bin"), cache))
            return;
        SDL_Log("Switch Hero: assets/gems.bin missing or out of date; rendering the gems (slow)");
        cache = renderAll(true);
    });
    return cache;
}
void start() {
    static std::future<void> job = std::async(std::launch::async, [] { renders(); });
}
float toLinear(Uint8 c) { return std::pow(c / 255.0f, 2.2f); }
Uint8 toSrgb(float c) { return u8(std::pow(std::clamp(c, 0.0f, 1.0f), 1 / 2.2f) * 255); }
SDL_Texture *texture(SDL_Color lane, GemStyle style) {
    const auto &texels = renders()[size_t(style)];
    const float lin[3] = {toLinear(lane.r), toLinear(lane.g), toLinear(lane.b)};
    return upload(paint(SpriteW, SpriteH,
                        [&](int x, int y) {
                            const Texel &t = texels[size_t(y) * SpriteW + x];
                            const float alphaOut = t.cover + (1 - t.cover) * t.shadow;
                            if (alphaOut <= 0)
                                return SDL_Color{0, 0, 0, 0};
                            // Straight alpha: covered samples carry the gem colour,
                            // uncovered ones only darken (the shadow is black).
                            const float k = t.cover / alphaOut;
                            SDL_Color c;
                            c.r = toSrgb((lin[0] * t.cap + t.rest[0] + t.spec) * k);
                            c.g = toSrgb((lin[1] * t.cap + t.rest[1] + t.spec) * k);
                            c.b = toSrgb((lin[2] * t.cap + t.rest[2] + t.spec) * k);
                            c.a = u8(alphaOut * 255);
                            return c;
                        }),
                  SDL_BLENDMODE_BLEND);
}
} // namespace gem3d
SDL_Texture *bakeTarget(const std::function<void()> &draw) {
    SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SpriteW, SpriteH);
    if (!t)
        return nullptr;
    flushBatch();
    auto *previous = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, t)) {
        SDL_DestroyTexture(t);
        return nullptr;
    }
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    draw();
    flushBatch();
    SDL_SetRenderTarget(renderer, previous);
    return t;
}

void destroyAll() {
    for (auto &f : fonts) {
        SDL_DestroyTexture(f.alphaTexture);
        SDL_DestroyTexture(f.haloTexture);
        f.alphaTexture = f.haloTexture = nullptr;
    }
    for (auto *&t : boardTex)
        SDL_DestroyTexture(t), t = nullptr;
    for (auto **t : {&glowTex, &wallTex, &vignetteTex, &scanTex, &tapeTex, &metalTex, &grainTex, &ringTex, &sparkTex,
                     &flareTex}) {
        SDL_DestroyTexture(*t);
        *t = nullptr;
    }
    for (auto &set : flameTex)
        for (auto *&t : set)
            SDL_DestroyTexture(t), t = nullptr;
    for (auto &set : paletteSets)
        set.destroy();
    paletteSets.clear();
    texturesReady = false;
}
// A highway surface, 256 px square. Each tiles top to bottom, since it scrolls
// down the board: every noise period and pattern cell divides 256. Nothing
// needs to tile across, as the board shows it once wide.
Canvas paintBoard(Board surface) {
    auto ridge = [](float n, float sharp) { return std::pow(1 - std::abs(n * 2 - 1), sharp); };
    auto spark = [](int x, int y, uint32_t seed, float odds) { return hash01(uint32_t(x * 7331 + y * 2711) ^ seed) > odds; };
    switch (surface) {
    case Board::Rosewood:
        // Grain running along the neck, with open pores.
        return paint(256, 256, [&](int x, int y) {
            const float lines = valueNoise(x / 3.0f, 0, 4096, 41) * .55f + valueNoise(x / 11.0f, 0, 4096, 43) * .45f;
            const float figure = fbm(x / 32.0f, y / 32.0f, 8, 47, 4);
            const float grain = std::clamp(lines * .7f + figure * .5f - .1f, 0.0f, 1.0f);
            const float k = spark(x, y, 0, .985f) ? .55f : 1;
            return SDL_Color{u8((52 + grain * 46) * k), u8((26 + grain * 20) * k), u8((20 + grain * 12) * k), 255};
        });
    case Board::Synthwave:
        // Deep violet with a faint haze and a few stars; the grid goes on top.
        return paint(256, 256, [&](int x, int y) {
            const float n = fbm(x / 32.0f, y / 32.0f, 8, 53, 3);
            const float star = spark(x, y, 5, .997f) ? 60 : 0;
            return SDL_Color{u8(18 + n * 16 + star), u8(8 + n * 6 + star), u8(34 + n * 24 + star), 255};
        });
    case Board::Pastel:
        // Drifting lilac, pink and baby-blue clouds with the odd sparkle.
        return paint(256, 256, [&](int x, int y) {
            const float a = fbm(x / 64.0f, y / 64.0f, 4, 61, 4), b = fbm(x / 48.0f, y / 64.0f + 7, 4, 67, 3);
            const SDL_Color lilac{222, 206, 246, 255}, pink{250, 212, 232, 255}, blue{206, 226, 252, 255};
            const SDL_Color c = mix(mix(lilac, pink, smooth(std::clamp(a * 1.8f - .4f, 0.0f, 1.0f))), blue,
                                    smooth(std::clamp(b * 1.8f - .5f, 0.0f, 1.0f)));
            return spark(x, y, 9, .996f) ? SDL_Color{255, 255, 255, 255} : c;
        });
    case Board::Hellfire:
        // Charred rock split by glowing lava veins, white-hot at their cores.
        return paint(256, 256, [&](int x, int y) {
            const float rock = fbm(x / 16.0f, y / 16.0f, 16, 71, 3);
            const float vein = ridge(fbm(x / 32.0f, y / 32.0f, 8, 73, 4), 14);
            const float heat = std::clamp(vein * 1.3f, 0.0f, 1.0f);
            const SDL_Color base{u8(22 + rock * 22), u8(12 + rock * 10), u8(10 + rock * 8), 255};
            return mix(mix(base, {230, 60, 10, 255}, heat), {255, 214, 120, 255}, std::clamp(heat * 2 - 1.2f, 0.0f, 1.0f));
        });
    case Board::DiamondPlate:
        // Industrial tread plate: brushed steel with raised diamonds that
        // alternate direction from cell to cell.
        return paint(256, 256, [&](int x, int y) {
            const float brush = valueNoise(x / 2.0f, y / 64.0f, 4096, 79) * 14 + fbm(x / 32.0f, y / 32.0f, 8, 83, 3) * 16;
            SDL_Color c{u8(78 + brush), u8(82 + brush), u8(90 + brush), 255};
            const int cx = x % 32, cy = y % 32;
            const bool flip = ((x / 32) + (y / 32)) % 2;
            const float u = (cx - 15.5f) / 16, v = (cy - 15.5f) / 16;
            const float a = flip ? u + v : u - v, b = flip ? u - v : u + v; // rotated 45 degrees
            const float d = std::abs(a) * .35f + std::abs(b);
            if (d < .32f) {
                const float lit = std::clamp(.5f - (a + b) * .9f, 0.0f, 1.0f); // light from the top left
                c = mix(mix(c, {40, 42, 48, 255}, .5f), {220, 224, 232, 255}, lit * (1 - d / .32f) + .15f);
            }
            return c;
        });
    case Board::CarbonFiber:
        // A 2x2 twill weave: tows of fibre crossing over and under, each with
        // a sheen across its width.
        return paint(256, 256, [&](int x, int y) {
            const int tx = x / 8, ty = y / 8;
            const bool across = ((tx + ty) / 2) % 2 == 0;
            const float t = across ? (y % 8) / 7.0f : (x % 8) / 7.0f;
            const float sheen = std::pow(std::sin(t * 3.14159f), 2.0f);
            const float v = 16 + sheen * (across ? 44 : 30);
            return SDL_Color{u8(v), u8(v), u8(v * 1.1f + 2), 255};
        });
    case Board::Thunderstorm:
        // Slate storm clouds with faint electric veins.
        return paint(256, 256, [&](int x, int y) {
            const float cloud = fbm(x / 64.0f, y / 64.0f, 4, 89, 5);
            const float charge = ridge(fbm(x / 32.0f, y / 32.0f, 8, 97, 3), 20) * .6f;
            return mix(SDL_Color{u8(18 + cloud * 34), u8(22 + cloud * 38), u8(32 + cloud * 48), 255},
                       {150, 200, 255, 255}, charge);
        });
    case Board::ToxicWaste:
        // Black sludge with glowing green seams and bubbles.
        return paint(256, 256, [&](int x, int y) {
            const float sludge = fbm(x / 32.0f, y / 32.0f, 8, 101, 4);
            const float seam = ridge(fbm(x / 64.0f, y / 64.0f, 4, 103, 3), 24) * .75f;
            const int cx = x % 32, cy = y % 32;
            const float bx = 6 + hash01(uint32_t(x / 32 * 31 + y / 32 * 17)) * 20, by = 6 + hash01(uint32_t(x / 32 * 13 + y / 32 * 41)) * 20;
            const float bubble = std::hypot(cx - bx, cy - by);
            const bool rim = bubble > 3 && bubble < 4.5f && hash01(uint32_t(x / 32 * 7 + y / 32 * 3)) > .5f;
            SDL_Color c{u8(8 + sludge * 14), u8(20 + sludge * 30), u8(8 + sludge * 10), 255};
            c = mix(c, {120, 255, 40, 255}, std::clamp(seam * 1.2f, 0.0f, 1.0f));
            return rim ? mix(c, {170, 255, 90, 255}, .8f) : c;
        });
    case Board::Zebra:
        // Wavy black stripes on off-white, the way a zebra hide runs.
        return paint(256, 256, [&](int x, int y) {
            const float warp = fbm(x / 32.0f, y / 32.0f, 8, 107, 3) * 7;
            const float stripe = std::sin(x * .085f + y * 3.14159f * 4 / 256 + warp);
            const float grain = fbm(x / 16.0f, y / 16.0f, 16, 109, 2) * 18;
            return stripe > .1f ? SDL_Color{u8(14 + grain * .4f), u8(14 + grain * .4f), u8(16 + grain * .4f), 255}
                                : SDL_Color{u8(222 - grain), u8(220 - grain), u8(210 - grain), 255};
        });
    case Board::Checkerboard:
        // Scuffed punk checkers, the kind painted on a stage floor.
        return paint(256, 256, [&](int x, int y) {
            const bool light = ((x / 32) + (y / 32)) % 2;
            const float wear = fbm(x / 32.0f, y / 32.0f, 8, 113, 4);
            const bool scratch = ridge(fbm(x / 16.0f, y / 64.0f, 16, 127, 2), 40) > .6f;
            const float v = light ? 214 - wear * 70 : 20 + wear * 22;
            return scratch ? SDL_Color{u8(v * .7f + 40), u8(v * .7f + 40), u8(v * .7f + 44), 255}
                           : SDL_Color{u8(v), u8(v), u8(v * .98f), 255};
        });
    case Board::Nebula:
        // Deep space: violet and teal gas clouds and a field of stars.
        return paint(256, 256, [&](int x, int y) {
            const float a = fbm(x / 64.0f, y / 64.0f, 4, 131, 5), b = fbm(x / 32.0f, y / 32.0f, 8, 137, 4);
            SDL_Color c{6, 6, 16, 255};
            c = mix(c, {120, 40, 170, 255}, smooth(std::clamp(a * 2 - .7f, 0.0f, 1.0f)) * .8f);
            c = mix(c, {20, 150, 170, 255}, smooth(std::clamp(b * 2 - .9f, 0.0f, 1.0f)) * .6f);
            if (spark(x, y, 11, .994f))
                c = mix(c, {255, 255, 255, 255}, .5f + hash01(uint32_t(x * 17 + y)) * .5f);
            return c;
        });
    case Board::Frostbite:
        // Blue ice cracked with white frost.
        return paint(256, 256, [&](int x, int y) {
            const float depth = fbm(x / 64.0f, y / 64.0f, 4, 139, 4);
            const float crack = ridge(fbm(x / 32.0f, y / 32.0f, 8, 149, 4), 30);
            SDL_Color c = mix(SDL_Color{28, 70, 120, 255}, {120, 190, 230, 255}, depth);
            c = mix(c, {196, 232, 255, 255}, std::clamp(crack * .8f, 0.0f, 1.0f));
            return spark(x, y, 13, .993f) ? SDL_Color{255, 255, 255, 255} : c;
        });
    case Board::GripTape:
    default:
        // Skateboard grip tape: near-black grit.
        return paint(256, 256, [&](int x, int y) {
            const float g = hash01(uint32_t(x * 5471 + y * 9133));
            const float v = 20 + (g > .82f ? (g - .82f) * 160 : 0) + fbm(x / 32.0f, y / 32.0f, 8, 3, 3) * 10;
            return SDL_Color{u8(v), u8(v), u8(v + 2), 255};
        });
    }
}
void bakeReceptors(); // the fret buttons, in the current palette
void createAll() {
    destroyAll();
    for (auto &f : fonts) {
        f.alphaTexture = alphaTexture(f.alpha, f.atlasW, f.atlasH);
        f.haloTexture = alphaTexture(f.halo, f.atlasW, f.atlasH);
    }
    glowTex = upload(paint(64, 64,
                           [](int x, int y) {
                               float d = std::hypot((x + .5f) / 32 - 1, (y + .5f) / 32 - 1);
                               return SDL_Color{255, 255, 255, u8(std::pow(std::clamp(1 - d, 0.0f, 1.0f), 2.2f) * 255)};
                           }),
                     SDL_BLENDMODE_ADD);
    // Painted cinder-block wall: blotchy concrete, grime streaks and scratches.
    wallTex = upload(paint(640, 360,
                           [](int x, int y) {
                               float n = fbm(x / 64.0f, y / 64.0f, 10, 7, 5);
                               float grit = hash01(uint32_t(x * 7919 + y * 104729)) * 10;
                               float streak = std::pow(fbm(x / 9.0f, y / 140.0f, 72, 31, 3), 3.0f) * 26;
                               float block = (y % 90 < 2 || (x + (y / 90 % 2) * 110) % 220 < 2) ? -9 : 0;
                               float v = 17 + n * 26 + grit - streak + block;
                               return SDL_Color{u8(v), u8(v), u8(v * 1.08f + 2), 255};
                           }),
                      SDL_BLENDMODE_BLEND);
    vignetteTex = upload(paint(160, 90,
                               [](int x, int y) {
                                   float dx = (x + .5f) / 80 - 1, dy = (y + .5f) / 45 - 1;
                                   float d = std::sqrt(dx * dx * .8f + dy * dy);
                                   return SDL_Color{0, 0, 0, u8(std::pow(std::clamp(d, 0.0f, 1.3f), 2.4f) * 190)};
                               }),
                         SDL_BLENDMODE_BLEND);
    scanTex = upload(paint(2, 360, [](int, int y) { return SDL_Color{0, 0, 0, Uint8(y % 2 ? 26 : 0)}; }),
                     SDL_BLENDMODE_BLEND, SDL_ScaleModeNearest);
    // Film grain for the grade pass. Sparse and slightly warm, so it reads as
    // emulsion and old CRT noise rather than as a uniform static wash.
    grainTex = upload(paint(256, 256,
                            [](int x, int y) {
                                float g = hash01(uint32_t(x * 6151 + y * 31337));
                                float s = g > .70f ? (g - .70f) / .30f : 0;
                                return SDL_Color{255, 244, 228, u8(std::pow(s, 1.7f) * 255)};
                            }),
                      SDL_BLENDMODE_ADD, SDL_ScaleModeNearest);
    // Duct tape: woven silver cloth with torn ends.
    tapeTex = upload(paint(320, 72,
                           [](int x, int y) {
                               float jagL = 5 + hash01(uint32_t(y / 3) * 977u) * 11;
                               float jagR = 5 + hash01(uint32_t(y / 3) * 613u + 5) * 11;
                               if (x < jagL || 319 - x < jagR)
                                   return SDL_Color{0, 0, 0, 0};
                               float weave = (x % 4 == 0 ? -10 : 0) + (y % 5 == 0 ? -8 : 0);
                               float n = fbm(x / 40.0f, y / 20.0f, 8, 11, 3) * 30;
                               float sheen = std::exp(-std::pow((y - 20) / 10.0f, 2.0f)) * 22;
                               float edge = (y < 2 || y > 69) ? -30 : 0;
                               float v = 128 + n + weave + sheen + edge + hash01(uint32_t(x * 31 + y * 131)) * 8;
                               return SDL_Color{u8(v), u8(v), u8(v + 4), 250};
                           }),
                      SDL_BLENDMODE_BLEND);
    metalTex = upload(paint(320, 160,
                            [](int x, int y) {
                                float brush = fbm(x / 90.0f, y / 1.5f, 4, 23, 3) * 26;
                                float light = (1 - float(y) / 160) * 20;
                                float v = 26 + brush + light + hash01(uint32_t(x * 13 + y * 7177)) * 5;
                                return SDL_Color{u8(v), u8(v + 1), u8(v + 6), 255};
                            }),
                      SDL_BLENDMODE_BLEND);
    // Highway surfaces are painted when first shown (see board()): a dozen of
    // them at start-up would cost the console over a second.
    // Flames: a tapering tongue whose edge is torn by noise that scrolls up
    // through the frames. The noise tiles vertically over the whole cycle, so
    // the last frame runs straight back into the first.
    for (int set = 0; set < 2; ++set)
        for (int frame = 0; frame < FlameFrames; ++frame) {
            const float scroll = frame * 6.0f / FlameFrames;
            flameTex[set][frame] = upload(
                paint(64, 128,
                      [&](int px, int py) {
                          const float u = (px + .5f) / 64, v = (py + .5f) / 128; // v = 0 at the tip
                          const float n = fbm(u * 3, v * 3 + scroll, 6, 71, 4);
                          const float lick = fbm(u * 6 + 11, v * 6 + scroll * 2, 12, 17, 3);
                          const float body = .5f * std::pow(v, .8f);
                          const float bend = (n - .5f) * .3f * (1.1f - v);
                          const float d = std::abs(u - .5f - bend) / std::max(body, .001f);
                          float heat = (1 - d * d) * (.7f + .6f * lick);
                          heat -= (1 - v) * .22f;                         // thins out towards the tip
                          heat *= std::clamp((1 - v) * 9, 0.0f, 1.0f);    // soft base
                          heat = std::clamp(heat, 0.0f, 1.0f);
                          // Classic ramp: deep red edge, orange, yellow, white-hot core.
                          static const SDL_Color fireRamp[] = {{120, 12, 0, 255}, {255, 96, 6, 255}, {255, 196, 46, 255},
                                                               {255, 250, 226, 255}};
                          static const SDL_Color powerRamp[] = {{10, 30, 150, 255}, {40, 130, 255, 255}, {150, 220, 255, 255},
                                                                {240, 252, 255, 255}};
                          const SDL_Color *ramp = set ? powerRamp : fireRamp;
                          const float k = std::min(heat * 2.7f, 2.999f);
                          const SDL_Color c = mix(ramp[int(k)], ramp[int(k) + 1], k - int(k));
                          return SDL_Color{c.r, c.g, c.b, u8(std::min(1.0f, heat * 2.0f) * 255)};
                      }),
                SDL_BLENDMODE_ADD);
        }
    // A soft annulus for shockwaves and the fret buttons' hit flash.
    ringTex = upload(paint(128, 128,
                           [](int x, int y) {
                               const float d = std::hypot((x + .5f) / 64 - 1, (y + .5f) / 64 - 1);
                               return SDL_Color{255, 255, 255, u8(std::exp(-std::pow((d - .78f) / .09f, 2.0f)) * 255)};
                           }),
                     SDL_BLENDMODE_ADD);
    // A spark streak, head at the right, for blitting along its velocity.
    sparkTex = upload(paint(64, 16,
                            [](int x, int y) {
                                const float along = (x + .5f) / 64, across = (y + .5f - 8) / 3.2f;
                                const float head = std::exp(-std::pow((along - .88f) / .08f, 2.0f));
                                const float a = (std::pow(along, 1.6f) * .7f + head) * std::exp(-across * across);
                                return SDL_Color{255, 255, 255, u8(std::min(1.0f, a) * 255)};
                            }),
                      SDL_BLENDMODE_ADD);
    // A four-point star glint for perfect hits.
    flareTex = upload(paint(128, 128,
                            [](int x, int y) {
                                const float dx = std::abs((x + .5f) / 64 - 1), dy = std::abs((y + .5f) / 64 - 1);
                                const float rays = std::exp(-dx * 26) * std::exp(-dy * 2.6f) + std::exp(-dy * 26) * std::exp(-dx * 2.6f);
                                const float core = std::exp(-std::hypot(dx, dy) * 7);
                                return SDL_Color{255, 255, 255, u8(std::min(1.0f, rays + core) * 255)};
                            }),
                      SDL_BLENDMODE_ADD);
    // Gem textures are uploaded on first use (see gem()); only start rendering.
    gem3d::start();
    texturesReady = true;
    bakeReceptors();
}
// The current palette's fret buttons. This switches render targets, so it
// must not run while a split-screen pane's viewport is set.
void bakeReceptors() {
    for (size_t i = 0; i < 5; ++i)
        for (int pressed = 0; pressed < 2; ++pressed) {
            auto &slot = paletteSet().receptors[i][size_t(pressed)];
            SDL_DestroyTexture(slot);
            SDL_Texture *t = bakeTarget([&] { bakeReceptor(lanes[i], pressed); });
            paletteSet().receptors[i][size_t(pressed)] = t;
        }
}
// Sprites go through the batch too, tinted by vertex colour rather than the
// texture's colour mod, so a run of the same sprite (flames, sparks, glows)
// is one draw call.
void blit(SDL_Texture *t, float x, float y, float w, float h, SDL_Color c = {255, 255, 255, 255}) {
    if (!t)
        return;
    const SDL_FPoint p[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    texturedQuad(t, p, c);
}
void blitRotated(SDL_Texture *t, float cx, float cy, float w, float h, float angleDeg, SDL_Color c) {
    if (!t)
        return;
    const float a = angleDeg * Tau / 360;
    const SDL_FPoint centre{cx, cy};
    const SDL_FPoint p[4] = {rotate({cx - w / 2, cy - h / 2}, centre, a), rotate({cx + w / 2, cy - h / 2}, centre, a),
                             rotate({cx + w / 2, cy + h / 2}, centre, a), rotate({cx - w / 2, cy + h / 2}, centre, a)};
    texturedQuad(t, p, c);
}
} // namespace

// ------------------------------------------------------------- split screen
SDL_FRect paneRect(size_t player, size_t players) {
    if (players <= 1)
        return {0, 0, float(W), float(H)};
    if (players == 2)
        // Side by side. A highway is a tall shape, so splitting the width
        // suits it far better than stacking two 1280x360 letterbox slots.
        return {float(player) * W / 2, 0, float(W) / 2, float(H)};
    // Three or four share quadrants; with three, the fourth is left empty.
    return {float(player % 2) * W / 2, float(player / 2) * H / 2, float(W) / 2, float(H) / 2};
}

void flush() { flushBatch(); }
void mesh(SDL_Texture *t, const std::vector<SDL_Vertex> &v, const std::vector<int> &idx) { geometry(t, v, idx); }
void clearViewport() {
    flushBatch(); // what is queued belongs to the pane it was drawn for
    SDL_RenderSetViewport(renderer, nullptr);
    // Re-asserting the logical size makes SDL recompute the letterbox scale
    // from the window's *current* size. Caching that scale instead would go
    // stale the moment the window is resized or the console changes output
    // resolution on its way into the dock - which is exactly when multiplayer
    // starts.
    SDL_RenderSetLogicalSize(renderer, W, H);
}

void setViewport(size_t player, size_t players) {
    clearViewport(); // flushes, then starts from a known full-screen scale
    float baseScale = 1, ignored = 1;
    SDL_RenderGetScale(renderer, &baseScale, &ignored);
    if (!(baseScale > 0))
        baseScale = 1;
    const SDL_FRect pane = paneRect(player, players);
    // Fit 1280x720 inside the pane without distorting it, then centre it.
    const float s = std::min(pane.w / W, pane.h / H);
    const float ox = pane.x + (pane.w - W * s) / 2, oy = pane.y + (pane.h - H * s) / 2;
    SDL_RenderSetScale(renderer, baseScale * s, baseScale * s);
    // Viewport units are pre-scale, so the offset divides by the same scale.
    const SDL_Rect vp{int(std::lround(ox / s)), int(std::lround(oy / s)), W, H};
    SDL_RenderSetViewport(renderer, &vp);
}

// ---------------------------------------------------------------- lifetime
void bakeGems(const std::string &path) {
    const auto data = gem3d::encode(gem3d::renderAll(false));
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
    if (!out)
        throw std::runtime_error("Cannot write " + path);
}
bool checkGems(std::string &problem) {
    gem3d::Renders baked;
    if (!gem3d::decode(readAsset("gems.bin"), baked)) {
        problem = "assets/gems.bin is missing or from another RenderVersion";
        return false;
    }
    const auto fresh = gem3d::renderAll(false);
    for (size_t style = 0; style < fresh.size(); ++style) {
        std::array<float, gem3d::Channels> lo{}, hi{};
        for (auto t : fresh[style])
            for (int c = 0; c < gem3d::Channels; ++c)
                lo[size_t(c)] = std::min(lo[size_t(c)], gem3d::channel(t, c)),
                hi[size_t(c)] = std::max(hi[size_t(c)], gem3d::channel(t, c));
        for (size_t i = 0; i < fresh[style].size(); ++i) {
            auto a = fresh[style][i], b = baked[style][i];
            for (int c = 0; c < gem3d::Channels; ++c) {
                // Within a few quantization steps: the file is 16-bit, and
                // another compiler may round the last float bit differently.
                const float step = (hi[size_t(c)] - lo[size_t(c)]) / 65535;
                if (std::abs(gem3d::channel(a, c) - gem3d::channel(b, c)) > step * 4 + 1e-6f) {
                    problem = "assets/gems.bin no longer matches the gem renderer; rebake it with "
                              "switch-hero --bake-gems assets/gems.bin";
                    return false;
                }
            }
        }
    }
    return true;
}
void setPalette(size_t index) {
    index = std::min(index, palettes().size() - 1);
    activePalette = index;
    lanes = palettes()[index].lanes;
    // Switching back to a palette already shown costs nothing. A new one has
    // its fret buttons baked now; its gems are made on first draw.
    if (renderer && texturesReady && !paletteSet().receptors[0][0])
        bakeReceptors();
}
size_t currentPalette() { return activePalette; }
void preparePalette(size_t index) {
    setPalette(index);
    prepareGems();
}
void prepareBoard(Board surface) {
    auto &texture = boardTex[size_t(std::clamp(int(surface), 0, BoardCount - 1))];
    if (!texture)
        texture = upload(paintBoard(surface), SDL_BLENDMODE_BLEND);
}
void init(SDL_Renderer *r) {
    renderer = r;
    // A missing font must never take the game down: it draws without text.
    const std::array<std::pair<Face, const char *>, 3> files = {
        std::pair{Face::Stencil, "stencil.font"}, {Face::Marker, "marker.font"}, {Face::Body, "body.font"}};
    for (auto [face, name] : files)
        try {
            loadFont(fonts[int(face)], name);
        } catch (const std::exception &e) {
            SDL_Log("Switch Hero: %s", e.what());
            fonts[int(face)] = Font{};
        }
    createAll();
}
void rebuild() { createAll(); }
void shutdown() { destroyAll(); }

// ---------------------------------------------------------------- helpers
float hash01(uint32_t v) {
    v ^= v >> 16, v *= 0x7feb352d, v ^= v >> 15, v *= 0x846ca68b, v ^= v >> 16;
    return float(v & 0xffffff) / float(0x1000000);
}
SDL_Color mix(SDL_Color a, SDL_Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto l = [&](Uint8 x, Uint8 y) { return Uint8(x + (y - x) * t); };
    return {l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), l(a.a, b.a)};
}
SDL_Color alpha(SDL_Color c, float a) { return {c.r, c.g, c.b, u8(c.a * a)}; }
void rect(float x, float y, float w, float h, SDL_Color c) {
    // A flat quad in the batch, not a fill call of its own.
    const SDL_Vertex v[4] = {{{x, y}, c, {0, 0}}, {{x + w, y}, c, {0, 0}}, {{x + w, y + h}, c, {0, 0}}, {{x, y + h}, c, {0, 0}}};
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    geometry(nullptr, v, 4, idx, 6);
}
void quad(SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FPoint d, SDL_Color ca, SDL_Color cb, SDL_Color cc,
          SDL_Color cd) {
    geometry(nullptr, {{a, ca, {0, 0}}, {b, cb, {0, 0}}, {c, cc, {0, 0}}, {d, cd, {0, 0}}}, {0, 1, 2, 0, 2, 3});
}
void quad(SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FPoint d, SDL_Color col) { quad(a, b, c, d, col, col, col, col); }
void thickLine(float x1, float y1, float x2, float y2, float width, SDL_Color c) {
    float dx = x2 - x1, dy = y2 - y1, len = std::max(.001f, std::hypot(dx, dy));
    float nx = -dy / len * width / 2, ny = dx / len * width / 2;
    quad({x1 + nx, y1 + ny}, {x2 + nx, y2 + ny}, {x2 - nx, y2 - ny}, {x1 - nx, y1 - ny}, c);
}
void disc(float cx, float cy, float rx, float ry, SDL_Color inner, SDL_Color outer, int segments) {
    fan(outline(cx, cy, rx, ry, false, segments), {cx, cy}, inner, outer);
}
void glow(float x, float y, float w, float h, SDL_Color c) { blit(glowTex, x - w / 2, y - h / 2, w, h, c); }

// ---------------------------------------------------------------- text
// Width of text already through sanitize(): Latin-1, one byte per glyph. It
// must not be sanitized twice, since Latin-1 bytes are not valid UTF-8.
float measureClean(const std::string &clean, Face face, float size, float tracking) {
    const Font &f = fonts[int(face)];
    if (f.glyphs.empty())
        return 0;
    float scale = size / f.px, width = 0;
    for (unsigned char ch : clean)
        width += glyphFor(f, ch)->advance / 16.0f * scale + tracking;
    return clean.empty() ? 0 : width - tracking;
}
float measure(const std::string &s, Face face, float size, float tracking) {
    return measureClean(sanitize(s), face, size, tracking);
}
void text(float x, float y, const std::string &raw, const Style &st) {
    const Font &f = fonts[int(st.face)];
    if (!f.alphaTexture || f.glyphs.empty())
        return;
    const float scale = st.size / f.px;
    std::string s = sanitize(raw);
    if (st.maxWidth > 0 && measureClean(s, st.face, st.size, st.tracking) > st.maxWidth) {
        while (!s.empty() && measureClean(s + "...", st.face, st.size, st.tracking) > st.maxWidth)
            s.pop_back();
        s += "...";
    }
    const float width = measureClean(s, st.face, st.size, st.tracking);
    const float startX = st.align == Align::Center ? -width / 2 : st.align == Align::Right ? -width : 0;
    const float lineH = f.line * scale, baseline = f.ascent * scale;
    const float angle = st.angle * Tau / 360;

    // Every pass is appended to one reused buffer and drawn in a single call per
    // texture. A stencil string with an outline used to be fourteen separate
    // draws; now it is two (halo, then outline and fill together, in order).
    static std::vector<SDL_Vertex> v;
    static std::vector<int> idx;
    v.clear(), idx.clear();
    auto pass = [&](bool halo, float ox, float oy, bool gradient, SDL_Color flat) {
        float pen = startX;
        uint32_t i = 0;
        for (unsigned char ch : s) {
            const Glyph &g = *glyphFor(f, ch);
            float advance = g.advance / 16.0f * scale + st.tracking;
            if (g.w > 0 && g.h > 0) {
                const float grow = halo ? f.pad : 0;
                float ax = g.x - grow, ay = g.y - grow, aw = g.w + grow * 2, ah = g.h + grow * 2;
                float lx = pen + (g.xoff - grow) * scale, ly = (g.yoff - grow) * scale;
                float w = aw * scale, h = ah * scale;
                std::array<SDL_FPoint, 4> p = {SDL_FPoint{lx, ly}, {lx + w, ly}, {lx + w, ly + h}, {lx, ly + h}};
                SDL_FPoint centre{lx + w / 2, ly + h / 2};
                for (auto &q : p) {
                    if (halo && st.glowSpread != 1)
                        q = {centre.x + (q.x - centre.x) * st.glowSpread, centre.y + (q.y - centre.y) * st.glowSpread};
                    if (st.wobble != 0)
                        q = rotate(q, centre, (hash01(i * 2654435761u + uint32_t(ch)) - .5f) * 2 * st.wobble * Tau / 360);
                    q.x -= (q.y - baseline) * st.shear;
                    q.x += ox, q.y += oy;
                    q = rotate(q, {0, 0}, angle);
                    q.x += x, q.y += y;
                }
                const float uvx[4] = {ax, ax + aw, ax + aw, ax}, uvy[4] = {ay, ay, ay + ah, ay + ah};
                const float ly4[4] = {ly, ly, ly + h, ly + h};
                const int base = int(v.size());
                for (int k = 0; k < 4; ++k) {
                    SDL_Color c = gradient ? mix(st.top, st.bottom, ly4[k] / lineH) : flat;
                    v.push_back({p[k], c, {uvx[k] / f.atlasW, uvy[k] / f.atlasH}});
                }
                idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            }
            pen += advance;
            ++i;
        }
    };
    if (st.glow.a) {
        pass(true, 0, 0, false, st.glow);
        geometry(f.haloTexture, v, idx);
        v.clear(), idx.clear();
    }
    if (st.outline.a && st.outlineWidth > 0) {
        const int steps = st.outlineWidth > 2.5f ? 12 : 8;
        for (int k = 0; k < steps; ++k) {
            float a = k * Tau / steps;
            pass(false, std::cos(a) * st.outlineWidth, std::sin(a) * st.outlineWidth, false, st.outline);
        }
    }
    pass(false, 0, 0, true, {});
    geometry(f.alphaTexture, v, idx);
}

// ---------------------------------------------------------------- set pieces
void wall(double time, SDL_Color light) {
    flushBatch();
    SDL_SetRenderDrawColor(renderer, 8, 8, 10, 255);
    SDL_RenderClear(renderer);
    blit(wallTex, 0, 0, W, H);
    // A TV left on in a dark room, plus a warm lamp in the far corner.
    float flicker = .92f + .08f * std::sin(float(time) * 7.3f) * std::sin(float(time) * 2.1f);
    glow(170, 820, 1500, 1100, alpha(light, .42f * flicker));
    glow(1180, -80, 900, 620, {200, 40, 30, 70});
    // Dust drifting through the light.
    for (uint32_t i = 0; i < 22; ++i) {
        float speed = 8 + hash01(i * 3) * 14;
        float px = hash01(i * 7 + 1) * W + std::sin(float(time) * .3f + i) * 30;
        float py = H + 20 - float(std::fmod(time * speed + hash01(i * 11) * (H + 40), H + 40));
        float s = 3 + hash01(i * 13) * 5;
        glow(px, py, s, s, {255, 230, 200, Uint8(40 + hash01(i * 17) * 50)});
    }
}
// The grade, over the finished frame. This used to be blitted as part of the
// wall, which put it *behind* the highway, the gems and the HUD: every layer
// then carried its own separate lighting and the image read as pasted-together
// parts. Run last, the same vignette, scanlines and grain fall across all of it
// at once, which is most of what makes a frame look photographed rather than
// assembled.
void grade(double time) {
    // Softer than when this sat behind the scene: over the HUD a heavy vignette
    // eats the controller hints along the bottom edge.
    blit(vignetteTex, 0, 0, W, H, {255, 255, 255, 200});
    blit(scanTex, 0, 0, W, H);
    // Tile the grain at 1:1. Stretching one tile over the screen turns the noise
    // into visible blocks, which looks like a compression artefact rather than
    // emulsion. Jump a whole tile each frame so it never sits still, and keep it
    // faint: grain should be felt, not seen.
    // The grain is ~15 tiled fullscreen blits every frame. It is free on desktop
    // and has never been measured on console, so it is the one effect that can
    // be turned off on its own when frames are tight - split-screen above all.
    if (!grainOn)
        return;
    const auto step = uint32_t(time * 24);
    const float ox = hash01(step * 2 + 1) * 256, oy = hash01(step * 2 + 7) * 256;
    for (float ty = -oy; ty < H; ty += 256)
        for (float tx = -ox; tx < W; tx += 256)
            blit(grainTex, tx, ty, 256, 256, {255, 255, 255, 22});
    flushBatch(); // the frame is complete: everything must reach the screen
}
void setGrain(bool on) { grainOn = on; }
void tape(float cx, float cy, float w, float h, float angleDeg, float shade) {
    blitRotated(tapeTex, cx + 3, cy + 5, w, h, angleDeg, {0, 0, 0, 110});
    const Uint8 v = u8(255 * shade);
    blitRotated(tapeTex, cx, cy, w, h, angleDeg, {v, v, v, 255});
}
void plate(float x, float y, float w, float h) {
    rect(x + 6, y + 8, w, h, {0, 0, 0, 120});
    blit(metalTex, x, y, w, h);
    rect(x, y, w, 2, {210, 212, 222, 160});
    rect(x, y + h - 3, w, 3, {0, 0, 0, 170});
    rect(x, y, 2, h, {160, 162, 172, 90});
    rect(x + w - 2, y, 2, h, {0, 0, 0, 120});
    for (auto [sx, sy] : {std::pair{x + 16, y + 16}, {x + w - 16, y + 16}, {x + 16, y + h - 16}, {x + w - 16, y + h - 16}}) {
        disc(sx + 1, sy + 2, 8, 8, {0, 0, 0, 150}, {0, 0, 0, 0}, 16);
        disc(sx, sy, 7, 7, {200, 202, 212, 255}, {70, 72, 82, 255}, 16);
        float a = hash01(uint32_t(sx * 31 + sy)) * Tau;
        thickLine(sx - std::cos(a) * 5, sy - std::sin(a) * 5, sx + std::cos(a) * 5, sy + std::sin(a) * 5, 2, {30, 30, 36, 255});
    }
}
void burnedCd(float cx, float cy, float r, double time) {
    disc(cx + 10, cy + 14, r * 1.04f, r * 1.04f, {0, 0, 0, 160}, {0, 0, 0, 0}, 64);
    disc(cx, cy, r, r, {210, 212, 222, 255}, {168, 170, 182, 255}, 64);
    // Recordable dye with a rainbow sheen that sweeps as the disc spins.
    const int n = 96;
    const float rot = float(time) * .9f, outer = r * .965f, inner = r * .34f;
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    for (int i = 0; i <= n; ++i) {
        float a = i * Tau / n;
        float sheen = std::pow(std::abs(std::cos(a - rot)), 6.0f);
        float hue = a * 2 + rot * 2;
        SDL_Color rainbow{u8(127 + 127 * std::sin(hue)), u8(127 + 127 * std::sin(hue + 2.1f)),
                          u8(127 + 127 * std::sin(hue + 4.2f)), 255};
        SDL_Color dye = mix({92, 70, 118, 255}, {150, 172, 190, 255}, .35f + .3f * std::sin(a * 3 + rot));
        SDL_Color c = mix(dye, rainbow, sheen * .55f);
        c = mix(c, {255, 255, 255, 255}, sheen * sheen * .45f);
        v.push_back({{cx + std::cos(a) * outer, cy + std::sin(a) * outer}, c, {0, 0}});
        v.push_back({{cx + std::cos(a) * inner, cy + std::sin(a) * inner}, shade(c, .8f), {0, 0}});
        if (i < n) {
            int b = i * 2;
            idx.insert(idx.end(), {b, b + 1, b + 2, b + 2, b + 1, b + 3});
        }
    }
    geometry(nullptr, v, idx);
    // Burn line where the data stops.
    for (int ring = 0; ring < 2; ++ring) {
        float rr = r * (.74f + ring * .005f);
        std::vector<SDL_FPoint> pts = outline(cx, cy, rr, rr, false, 96);
        for (size_t i = 0; i < pts.size(); ++i)
            thickLine(pts[i].x, pts[i].y, pts[(i + 1) % pts.size()].x, pts[(i + 1) % pts.size()].y, 1.5f, {40, 30, 60, 70});
    }
    disc(cx, cy, inner, inner, {226, 228, 236, 235}, {196, 198, 210, 235}, 48);
    disc(cx, cy, r * .2f, r * .2f, {120, 122, 134, 255}, {176, 178, 190, 255}, 40);
    disc(cx, cy, r * .085f, r * .085f, {8, 8, 10, 255}, {16, 16, 20, 255}, 32);
}
Image decodeArtwork(const fs::path &folder) {
    // Clone Hero folders keep cover art next to the chart under a handful of
    // names. One listing answers all of them; a stat per name is slow on the SD card.
    const auto files = FolderFiles::list(folder);
    for (const char *name : {"album.png", "album.jpg", "album.jpeg", "cover.png", "cover.jpg", "cover.jpeg"}) {
        const fs::path file = files.find(name);
        if (file.empty())
            continue;
        SDL_RWops *rw = SDL_RWFromFile(file.string().c_str(), "rb");
        if (!rw)
            continue;
        std::vector<Uint8> data(size_t(std::max<Sint64>(0, SDL_RWsize(rw))));
        const size_t got = data.empty() ? 0 : SDL_RWread(rw, data.data(), 1, data.size());
        SDL_RWclose(rw);
        if (got != data.size() || data.empty())
            continue;
        Image image = decodeImage(std::string(data.begin(), data.end()));
        if (!image.rgba.empty())
            return image;
    }
    return {};
}
Image decodeImage(const std::string &bytes) {
    if (bytes.empty() || bytes.size() > (16u << 20))
        return {};
    int w = 0, h = 0, channels = 0;
    Uint8 *pixels = stbi_load_from_memory(reinterpret_cast<const Uint8 *>(bytes.data()), int(bytes.size()), &w, &h,
                                          &channels, 4);
    if (!pixels)
        return {};
    // Covers are drawn at a few hundred pixels at most; a 3000 px scan would
    // only cost upload time and texture memory. Box-filter it down.
    const int factor = std::max(1, (std::max(w, h) + 511) / 512);
    Image image;
    image.w = w / factor, image.h = h / factor;
    image.rgba.resize(size_t(image.w) * size_t(image.h) * 4);
    for (int y = 0; y < image.h; ++y)
        for (int x = 0; x < image.w; ++x)
            for (int c = 0; c < 4; ++c) {
                unsigned sum = 0;
                for (int dy = 0; dy < factor; ++dy)
                    for (int dx = 0; dx < factor; ++dx)
                        sum += pixels[(size_t(y * factor + dy) * size_t(w) + size_t(x * factor + dx)) * 4 + size_t(c)];
                image.rgba[(size_t(y) * size_t(image.w) + size_t(x)) * 4 + size_t(c)] = Uint8(sum / unsigned(factor * factor));
            }
    stbi_image_free(pixels);
    if (image.w <= 0 || image.h <= 0)
        return {};
    return image;
}
SDL_Texture *uploadArtwork(const Image &image) {
    if (image.rgba.empty())
        return nullptr;
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<Uint8 *>(image.rgba.data()), image.w, image.h, 32,
                                                              image.w * 4, SDL_PIXELFORMAT_RGBA32);
    SDL_Texture *t = surface ? SDL_CreateTextureFromSurface(renderer, surface) : nullptr;
    SDL_FreeSurface(surface);
    if (t) {
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    }
    return t;
}
SDL_Texture *loadArtwork(const fs::path &folder) { return uploadArtwork(decodeArtwork(folder)); }
// Album art as a printed square, the way a cover sits in a CD case.
void photo(SDL_Texture *art, float cx, float cy, float size, float angleDeg, float shade) {
    if (!art)
        return;
    flushBatch(); // the cover is drawn directly, with its own colour mod
    const float border = std::max(6.0f, size * .05f), outer = size + border * 2;
    const float a = angleDeg * Tau / 360;
    auto corner = [&](float dx, float dy) { return rotate({cx + dx, cy + dy}, {cx, cy}, a); };
    const float half = outer / 2;
    quad(corner(-half + 5, -half + 9), corner(half + 5, -half + 9), corner(half + 5, half + 9),
         corner(-half + 5, half + 9), {0, 0, 0, 150});
    quad(corner(-half, -half), corner(half, -half), corner(half, half), corner(-half, half),
         {242, 242, 236, 255}, {242, 242, 236, 255}, {206, 206, 200, 255}, {214, 214, 208, 255});
    const Uint8 v = u8(255 * shade);
    SDL_SetTextureColorMod(art, v, v, v);
    SDL_SetTextureAlphaMod(art, 255);
    SDL_FRect dest{cx - size / 2, cy - size / 2, size, size};
    SDL_RenderCopyExF(renderer, art, nullptr, &dest, angleDeg, nullptr, SDL_FLIP_NONE);
}
void star(float cx, float cy, float r, float angleDeg, SDL_Color fill, SDL_Color edge) {
    auto pts = [&](float s) {
        std::vector<SDL_FPoint> p;
        for (int i = 0; i < 10; ++i) {
            float a = -Tau / 4 + i * Tau / 10 + angleDeg * Tau / 360;
            float rr = (i % 2 ? .45f : 1) * r * s;
            p.push_back({cx + std::cos(a) * rr, cy + std::sin(a) * rr});
        }
        return p;
    };
    fan(pts(1.25f), {cx, cy}, edge, edge);
    fan(pts(1), {cx - r * .2f, cy - r * .25f}, shade(fill, 1, 60), fill);
}
void sticker(float cx, float cy, float r, float angleDeg, SDL_Color fill, const std::string &label) {
    disc(cx + 4, cy + 6, r * 1.02f, r * 1.02f, {0, 0, 0, 140}, {0, 0, 0, 0});
    disc(cx, cy, r, r, {246, 246, 240, 255}, {214, 214, 206, 255});
    disc(cx, cy, r * .86f, r * .86f, shade(fill, 1, 40), shade(fill, .75f));
    Style s;
    s.face = Face::Stencil, s.size = r * .95f, s.align = Align::Center, s.angle = angleDeg;
    s.top = s.bottom = {255, 255, 255, 255};
    s.outline = {0, 0, 0, 200}, s.outlineWidth = 2;
    float a = angleDeg * Tau / 360, lift = r * .52f;
    text(cx + std::sin(a) * lift, cy - std::cos(a) * lift, label, s);
}
void button(float x, float y, const std::string &glyph, const std::string &label) {
    disc(x + 14, y + 15, 15, 15, {0, 0, 0, 160}, {0, 0, 0, 0}, 20);
    disc(x + 13, y + 13, 14, 14, {210, 212, 222, 255}, {90, 92, 104, 255}, 20);
    disc(x + 13, y + 13, 11, 11, {30, 30, 36, 255}, {12, 12, 16, 255}, 20);
    Style g;
    g.face = Face::Body, g.size = glyph.size() > 1 ? 11 : 15, g.align = Align::Center;
    text(x + 13, y + (glyph.size() > 1 ? 7 : 4), glyph, g);
    Style l;
    l.face = Face::Body, l.size = 17, l.top = l.bottom = ink::dim;
    text(x + 34, y + 3, label, l);
}
void fretButton(float x, float y, size_t lane, const std::string &label) {
    const SDL_Color c = lanes[std::min(lane, lanes.size() - 1)];
    disc(x + 14, y + 15, 15, 15, {0, 0, 0, 160}, {0, 0, 0, 0}, 20);
    disc(x + 13, y + 13, 14, 14, {210, 212, 222, 255}, {90, 92, 104, 255}, 20);
    disc(x + 13, y + 13, 11, 11, mix(c, {255, 255, 255, 255}, .25f), mix(c, {0, 0, 0, 255}, .45f), 20);
    disc(x + 11, y + 9, 5, 3, {255, 255, 255, 110}, {255, 255, 255, 0}, 12);
    if (!label.empty()) {
        Style l;
        l.face = Face::Body, l.size = 17, l.top = l.bottom = ink::dim;
        text(x + 34, y + 3, label, l);
    }
}
void ledMeter(float x, float y, float w, float h, int segments, float fill, bool active, double time) {
    rect(x - 6, y - 6, w + 12, h + 12, {6, 6, 8, 230});
    rect(x - 6, y - 6, w + 12, 1, {120, 122, 134, 160});
    const float gap = 3, seg = (w - gap * (segments - 1)) / segments;
    const float pulse = active ? .7f + .3f * std::sin(float(time) * 12) : 1;
    for (int i = 0; i < segments; ++i) {
        float sx = x + i * (seg + gap);
        bool lit = fill * segments > i + .01f;
        SDL_Color c = mix({40, 150, 255, 255}, {220, 250, 255, 255}, float(i) / (segments - 1));
        if (lit) {
            glow(sx + seg / 2, y + h / 2, seg * 3, h * 3, alpha(c, .35f * pulse));
            rect(sx, y, seg, h, alpha(c, pulse));
            rect(sx, y, seg, h * .35f, {255, 255, 255, 60});
        } else
            rect(sx, y, seg, h, {26, 30, 40, 255});
    }
}

void rockMeter(float x, float y, float w, float h, float value, double time, bool danger) {
    const float pulse = danger ? .55f + .45f * std::sin(float(time) * 14) : 1;
    rect(x - 7, y - 7, w + 14, h + 14, {6, 6, 8, 235});
    rect(x - 7, y - 7, w + 14, 1, {130, 132, 146, 150});
    // Zones run red at the bottom through amber to green at the top.
    const int cells = 26;
    for (int i = 0; i < cells; ++i) {
        float t = 1 - float(i) / (cells - 1); // 0 at the bottom
        SDL_Color zone = t < .28f ? SDL_Color{226, 34, 34, 255}
                                  : t < .62f ? mix({236, 150, 30, 255}, {240, 210, 40, 255}, (t - .28f) / .34f)
                                             : SDL_Color{92, 226, 74, 255};
        const bool lit = value >= t - .5f / cells;
        float cy = y + h * float(i) / cells, ch = h / cells - 2;
        if (lit) {
            if (t < .28f)
                zone = alpha(zone, pulse);
            glow(x + w / 2, cy + ch / 2, w * 2.4f, ch * 3, alpha(zone, .3f));
            rect(x, cy, w, ch, zone);
            rect(x, cy, w * .34f, ch, {255, 255, 255, 46});
        } else
            rect(x, cy, w, ch, alpha(zone, .22f));
    }
    // Needle riding the current value.
    const float ny = y + h * (1 - std::clamp(value, 0.0f, 1.0f));
    glow(x + w / 2, ny, w * 3, 30, alpha(danger ? SDL_Color{255, 60, 60, 255} : ink::chrome, .5f * pulse));
    quad({x - 11, ny - 9}, {x + 1, ny}, {x - 11, ny + 9}, {x - 15, ny}, ink::chrome);
    quad({x + w + 11, ny - 9}, {x + w - 1, ny}, {x + w + 11, ny + 9}, {x + w + 15, ny}, ink::chrome);
    rect(x, ny - 1.5f, w, 3, alpha(ink::chrome, .9f));
}
// ---------------------------------------------------------------- highway
void prepareGems() {
    auto &gems = paletteSet().gems;
    for (size_t i = 0; i < gems.size(); ++i)
        for (int style = 0; style < GemStyles; ++style)
            if (!gems[i][size_t(style)])
                gems[i][size_t(style)] = gem3d::texture(i == PowerColor ? ink::power : lanes[i], GemStyle(style));
}
void gem(size_t color, GemStyle style, float x, float y, float w, Uint8 a, float squash) {
    auto &tex = paletteSet().gems[color][size_t(style)];
    if (!tex)
        tex = gem3d::texture(color == PowerColor ? ink::power : lanes[color], style);
    const float scale = w / (SpriteRx * 2);
    blit(tex, x - SpriteCx * scale, y - SpriteCy * scale * squash, SpriteW * scale, SpriteH * scale * squash,
         {255, 255, 255, a});
}
void receptor(size_t lane, float x, float y, float w, bool pressed, bool power, float hitFlash) {
    const float scale = w / (SpriteRx * 2);
    SDL_Color c = power ? ink::power : lanes[lane];
    if (pressed)
        glow(x, y + 10, w * 1.9f, w * .75f, alpha(c, .8f)); // neon underglow
    blit(paletteSet().receptors[lane][pressed ? 1 : 0], x - SpriteCx * scale, y - SpriteCy * scale, SpriteW * scale,
         SpriteH * scale);
    if (hitFlash > 0) {
        // A hit, not just a press: the neon lining flares white-hot.
        const float ring = w * (.95f + .25f * (1 - hitFlash));
        blit(ringTex, x - ring / 2, y - ring * .215f, ring, ring * .43f, {255, 255, 255, u8(255 * hitFlash)});
        glow(x, y, w * 1.7f, w * .7f, alpha(c, .9f * hitFlash));
    }
}
void board(Board surface, const std::array<SDL_FPoint, 4> &p, float v0, float v1, SDL_Color tint) {
    auto &texture = boardTex[size_t(std::clamp(int(surface), 0, BoardCount - 1))];
    if (!texture)
        texture = upload(paintBoard(surface), SDL_BLENDMODE_BLEND);
    geometry(texture,
             {{p[0], tint, {0, v0}}, {p[1], tint, {1, v0}}, {p[2], tint, {1, v1}}, {p[3], tint, {0, v1}}},
             {0, 1, 2, 0, 2, 3});
}
void fire(const Fire &f, float strike, double time, bool power) {
    const auto &flames = flameTex[power ? 1 : 0];
    const SDL_Color laneGlow = power ? ink::power : lanes[size_t(f.lane)];
    const SDL_Color spark = power ? SDL_Color{190, 236, 255, 255} : SDL_Color{255, 196, 84, 255};
    const int frame = int(time * 30 + f.seed % FlameFrames) % FlameFrames;
    if (f.sustain) {
        const float flicker = .88f + .12f * std::sin(float(time) * 37 + float(f.seed));
        glow(f.x, strike, 170, 90, alpha(laneGlow, .55f * flicker));
        glow(f.x, strike, 64, 30, {255, 255, 255, 150});
        const float h = 116 * flicker;
        blit(flames[frame], f.x - 55, strike - h * .9f, 110, h, {255, 255, 255, 235});
        blit(flames[(frame + 3) % FlameFrames], f.x - 38, strike - h * .8f, 76, h * .78f, {255, 255, 255, 170});
        // Sparks streaming up off the held note.
        for (int i = 0; i < 4; ++i) {
            const double cycle = time * 1.9 + hash01(f.seed + uint32_t(i) * 7);
            const float life = float(cycle - std::floor(cycle));
            const uint32_t id = uint32_t(std::floor(cycle)) * 17 + uint32_t(i);
            const float lean = (hash01(f.seed * 3 + id) - .5f) * 40;
            const float sx = f.x + (hash01(f.seed * 5 + id) - .5f) * 50 + lean * life;
            const float sy = strike - 10 - life * 150;
            blitRotated(sparkTex, sx, sy, 24, 6, -90 + lean * .6f, alpha(spark, 1 - life));
        }
        return;
    }
    const float t = float(f.age / .42);
    if (t >= 1)
        return;
    const float fade = 1 - t, rise = 1 - fade * fade * fade;
    // Tighter hits burn bigger.
    const float boost = f.tier == 3 ? 1.25f : f.tier == 2 ? 1.1f : 1;
    // Shockwave rolling out across the board.
    const float rw = (50 + 130 * rise) * boost;
    blit(ringTex, f.x - rw / 2, strike - rw * .175f, rw, rw * .35f, alpha(laneGlow, fade * .9f));
    glow(f.x, strike, 140 + 100 * rise, 70 + 40 * rise, alpha(laneGlow, fade * .9f));
    glow(f.x, strike, 80 * fade, 40 * fade, {255, 255, 255, u8(255 * fade)});
    const float h = (70 + 110 * rise) * boost, w = 150 - 30 * rise;
    blit(flames[frame], f.x - w / 2, strike - h * .92f, w, h, {255, 255, 255, u8(255 * fade)});
    const float h2 = (50 + 140 * rise) * boost;
    blit(flames[(frame + 4) % FlameFrames], f.x - 45, strike - h2 * .95f - 8 * rise, 90, h2,
         {255, 255, 255, u8(190 * fade)});
    if (f.tier == 3) {
        const float fs = 170 * (1 - t * .5f);
        blitRotated(flareTex, f.x, strike - 6, fs, fs, float(f.seed % 30) + t * 25, {255, 250, 230, u8(255 * fade)});
    }
    // Sparks thrown up and falling back, drawn as streaks along their path.
    const float age = float(f.age);
    for (int i = 0; i < 12; ++i) {
        const float angle = -Tau / 4 + (hash01(f.seed + uint32_t(i) * 13) - .5f) * 2.4f;
        const float speed = 200 + 320 * hash01(f.seed * 7 + uint32_t(i));
        const float vx = std::cos(angle) * speed, vy = std::sin(angle) * speed + 900 * age;
        const float sx = f.x + std::cos(angle) * speed * age;
        const float sy = strike + std::sin(angle) * speed * age + 450 * age * age;
        const float len = 14 + std::hypot(vx, vy) * .045f;
        blitRotated(sparkTex, sx, sy, len, 6, std::atan2(vy, vx) * 360 / Tau, alpha(spark, fade));
    }
    // A few slow embers drifting up after the burst.
    for (int i = 0; i < 4; ++i) {
        const float ex = f.x + (hash01(f.seed * 11 + uint32_t(i)) - .5f) * 70 + std::sin(age * 9 + float(i)) * 8;
        const float ey = strike - 20 - age * 170 * (.6f + hash01(f.seed * 13 + uint32_t(i)));
        glow(ex, ey, 8, 8, alpha(spark, fade));
    }
}
} // namespace fret::look
