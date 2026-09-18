#include "look.hpp"
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
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fret::look {
SDL_Renderer *renderer = nullptr;
const std::array<SDL_Color, 6> lanes = {SDL_Color{40, 210, 70, 255}, {232, 36, 44, 255}, {250, 208, 28, 255},
                                        {38, 112, 246, 255},          {255, 132, 18, 255}, {178, 76, 255, 255}};
namespace {
constexpr float Tau = 6.2831853f;

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
Canvas paint(int w, int h, const std::function<SDL_Color(int, int)> &pixel) {
    Canvas c(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            c.set(x, y, pixel(x, y));
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
void geometry(SDL_Texture *t, const std::vector<SDL_Vertex> &v, const std::vector<int> &idx) {
    if (!v.empty())
        SDL_RenderGeometry(renderer, t, v.data(), int(v.size()), idx.data(), int(idx.size()));
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

std::vector<Uint8> readFile(const std::string &name) {
    std::vector<std::string> candidates;
#ifdef __SWITCH__
    candidates.push_back("romfs:/fonts/" + name);
    candidates.push_back("sdmc:/switch/switch-hero/fonts/" + name);
    candidates.push_back("sdmc:/switch/fretboard/fonts/" + name);
#else
    candidates.push_back("assets/fonts/" + name);
    if (char *base = SDL_GetBasePath()) {
        candidates.push_back(std::string(base) + "assets/fonts/" + name);
        candidates.push_back(std::string(base) + "../assets/fonts/" + name);
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
    throw std::runtime_error("Missing font asset " + name + " (expected in assets/fonts)");
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
const Glyph *glyphFor(const Font &f, unsigned char ch) {
    if (ch < 32 || ch - 32 >= int(f.glyphs.size()))
        ch = '?';
    return &f.glyphs[ch - 32];
}
// The baked fonts only hold printable ASCII. Chart titles are UTF-8, so fold
// accented Latin letters and typographic punctuation to their plain forms and
// show anything else as a single '?'.
std::string sanitize(const std::string &s) {
    // Latin-1 letters U+00C0..U+00FF, in order ('?' where there is no letter).
    static const char *const latin1[64] = {
        "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
        "D", "N", "O", "O", "O", "O", "O", "x", "O", "U", "U", "U", "U", "Y", "Th", "ss",
        "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
        "d", "n", "o", "o", "o", "o", "o", "/", "o", "u", "u", "u", "u", "y", "th", "y"};
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
        if (cp >= 0xC0 && cp <= 0xFF)
            out += latin1[cp - 0xC0];
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
            *metalTex = nullptr, *gripTex = nullptr, *grainTex = nullptr;
constexpr int FlameFrames = 4;
std::array<std::array<SDL_Texture *, FlameFrames>, 2> flameTex{};
constexpr int SpriteW = 256, SpriteH = 192;
constexpr float SpriteCx = 128, SpriteCy = 72, SpriteRx = 116, SpriteRy = 50;
std::array<std::array<SDL_Texture *, GemStyles>, 6> gemTex{};
std::array<std::array<SDL_Texture *, 2>, 5> receptorTex{};

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
SDL_FPoint spritePoint(float scale, float angle, float dy = 0) {
    return {SpriteCx + std::cos(angle) * SpriteRx * scale, SpriteCy + dy + std::sin(angle) * SpriteRy * scale};
}
void glint(float scale, float dy) {
    SDL_FPoint c{SpriteCx - SpriteRx * .28f * scale, SpriteCy + dy - SpriteRy * .34f * scale};
    fan(outline(c.x, c.y, SpriteRx * .22f * scale, SpriteRy * .13f * scale, false, 20), c, {255, 255, 255, 200},
        {255, 255, 255, 0});
}
void bolts(float radius, float dy = 0) {
    for (int i = 0; i < 4; ++i) {
        SDL_FPoint p = spritePoint(radius, Tau / 8 + i * Tau / 4, dy);
        fan(outline(p.x, p.y + 1.5f, 8, 5, false, 6), {p.x, p.y + 1.5f}, {10, 10, 12, 255}, {10, 10, 12, 255});
        fan(outline(p.x, p.y, 7, 4.4f, false, 6), {p.x - 2, p.y - 1}, {170, 172, 184, 255}, {58, 60, 68, 255});
    }
}
// Chrome-rimmed, bolted gems with a glossy candy-paint cap.
void bakeGem(SDL_Color c, GemStyle style) {
    const bool starShape = style == GemStar || style == GemStarHopo;
    const bool hammer = style == GemHopo || style == GemStarHopo;
    auto shape = [&](float s, float dy = 0) {
        return outline(SpriteCx, SpriteCy + dy, SpriteRx * s, SpriteRy * s, starShape);
    };
    SDL_FPoint centre{SpriteCx, SpriteCy};
    const float depth = 34;
    fan(outline(SpriteCx, SpriteCy + depth + 8, SpriteRx * 1.08f, SpriteRy * 1.08f, false), {SpriteCx, SpriteCy + depth + 8},
        {0, 0, 0, 170}, {0, 0, 0, 0});
    extrude(shape(1), depth, {58, 60, 68, 255}, {10, 10, 14, 255});
    extrude(shape(1), depth * .38f, {236, 238, 246, 255}, {104, 108, 120, 255});
    fan(shape(1), {SpriteCx - 24, SpriteCy - 18}, {255, 255, 255, 255}, {132, 136, 150, 255});
    fan(shape(.93f), {SpriteCx + 30, SpriteCy + 14}, {150, 154, 166, 255}, {214, 216, 226, 255});
    if (!starShape)
        bolts(.93f);
    fan(shape(.86f), centre, {6, 6, 8, 255}, {24, 24, 30, 255});
    extrude(shape(.8f, -8), 8, shade(c, .5f), shade(c, .26f));
    fan(shape(.8f, -8), {SpriteCx - 24, SpriteCy - 26}, shade(c, 1, 95), shade(c, .58f));
    if (hammer) {
        // Hammer-ons glow white-hot in the middle, star notes included.
        fan(shape(.4f, -8), {SpriteCx, SpriteCy - 8}, {255, 255, 255, 255}, shade(c, 1, 140));
        fan(shape(.22f, -8), {SpriteCx, SpriteCy - 8}, {255, 255, 255, 255}, {255, 255, 255, 255});
    }
    glint(.8f, -8);
}
// Fret buttons: bolted chrome ring around a dark well with a neon lining.
void bakeReceptor(SDL_Color c, bool pressed) {
    auto shape = [&](float s, float dy = 0) { return outline(SpriteCx, SpriteCy + dy, SpriteRx * s, SpriteRy * s, false); };
    const float depth = 24;
    fan(outline(SpriteCx, SpriteCy + depth + 8, SpriteRx * 1.06f, SpriteRy * 1.06f, false), {SpriteCx, SpriteCy + depth + 8},
        {0, 0, 0, 180}, {0, 0, 0, 0});
    extrude(shape(1), depth, {70, 72, 82, 255}, {12, 12, 16, 255});
    fan(shape(1), {SpriteCx - 24, SpriteCy - 18}, {236, 238, 246, 255}, {96, 100, 112, 255});
    fan(shape(.93f), {SpriteCx + 30, SpriteCy + 14}, {110, 114, 126, 255}, {190, 192, 204, 255});
    bolts(.93f);
    fan(shape(.84f), {SpriteCx, SpriteCy + 10}, {2, 2, 3, 255}, {20, 20, 26, 255});
    fan(shape(.78f), {SpriteCx, SpriteCy}, pressed ? SDL_Color{255, 255, 255, 255} : shade(c, 1, 60),
        pressed ? shade(c, 1, 120) : shade(c, .8f));
    fan(shape(.66f), {SpriteCx, SpriteCy + 6}, {12, 12, 16, 255}, {26, 26, 32, 255});
    if (pressed) {
        extrude(shape(.62f, 2), 5, shade(c, .55f), shade(c, .32f));
        fan(shape(.62f, 2), {SpriteCx - 20, SpriteCy - 16}, shade(c, 1, 130), shade(c, .78f));
        glint(.62f, 2);
    }
}
SDL_Texture *bakeTarget(const std::function<void()> &draw) {
    SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SpriteW, SpriteH);
    if (!t)
        return nullptr;
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
    SDL_SetRenderTarget(renderer, previous);
    return t;
}

void destroyAll() {
    for (auto &f : fonts) {
        SDL_DestroyTexture(f.alphaTexture);
        SDL_DestroyTexture(f.haloTexture);
        f.alphaTexture = f.haloTexture = nullptr;
    }
    for (auto **t : {&glowTex, &wallTex, &vignetteTex, &scanTex, &tapeTex, &metalTex, &gripTex, &grainTex}) {
        SDL_DestroyTexture(*t);
        *t = nullptr;
    }
    for (auto &set : flameTex)
        for (auto *&t : set)
            SDL_DestroyTexture(t), t = nullptr;
    for (auto &set : gemTex)
        for (auto *&t : set)
            SDL_DestroyTexture(t), t = nullptr;
    for (auto &set : receptorTex)
        for (auto *&t : set)
            SDL_DestroyTexture(t), t = nullptr;
}
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
    // Skateboard grip tape: near-black grit.
    gripTex = upload(paint(256, 256,
                           [](int x, int y) {
                               float g = hash01(uint32_t(x * 5471 + y * 9133));
                               float v = 20 + (g > .82f ? (g - .82f) * 160 : 0) + fbm(x / 32.0f, y / 32.0f, 8, 3, 3) * 10;
                               return SDL_Color{u8(v), u8(v), u8(v + 2), 255};
                           }),
                     SDL_BLENDMODE_BLEND);
    for (int set = 0; set < 2; ++set)
        for (int frame = 0; frame < FlameFrames; ++frame) {
            const float seed = frame * 1.7f;
            flameTex[set][frame] = upload(
                paint(64, 128,
                      [&](int px, int py) {
                          float u = (px + .5f) / 64, v = (py + .5f) / 128; // v = 0 at the tip
                          float body = .47f * std::pow(v, .55f);
                          float sway = (std::sin(v * 6 + seed) * .08f + std::sin(v * 15 + seed * 2.3f) * .04f) * (1 - v);
                          float tongues = 1 + .35f * (1 - v) * std::sin(u * 18 + seed * 4 + v * 5);
                          float d = std::abs(u - .5f - sway) / std::max(body * tongues, .001f);
                          float heat = std::pow(std::clamp(1 - d, 0.0f, 1.0f), .9f);
                          heat *= .85f + .15f * std::sin(u * 23 + v * 31 + seed * 3);
                          heat *= std::clamp((1 - v) * 7, 0.0f, 1.0f) * std::clamp(v * 1.3f, 0.0f, 1.0f);
                          heat = std::clamp(heat * (.6f + .6f * v), 0.0f, 1.0f);
                          float hot = std::min(1.0f, heat * 2.2f), mid = std::clamp(heat * 1.9f - .3f, 0.0f, 1.0f),
                                core = std::clamp(heat * 2.4f - 1.4f, 0.0f, 1.0f);
                          float r = hot, g = mid, b = core;
                          if (set == 1) // star power burns blue-white
                              r = core, g = std::max(mid * .9f, core), b = hot;
                          return SDL_Color{u8(r * 255), u8(g * 255), u8(b * 255), u8(std::min(1.0f, heat * 1.8f) * 255)};
                      }),
                SDL_BLENDMODE_ADD);
        }
    for (size_t i = 0; i < gemTex.size(); ++i)
        for (int style = 0; style < GemStyles; ++style)
            gemTex[i][style] = bakeTarget([&] { bakeGem(i == PowerColor ? ink::power : lanes[i], GemStyle(style)); });
    for (size_t i = 0; i < receptorTex.size(); ++i)
        for (int pressed = 0; pressed < 2; ++pressed)
            receptorTex[i][pressed] = bakeTarget([&] { bakeReceptor(lanes[i], pressed); });
}
void blit(SDL_Texture *t, float x, float y, float w, float h, SDL_Color c = {255, 255, 255, 255}) {
    if (!t)
        return;
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, c.a);
    SDL_FRect r{x, y, w, h};
    SDL_RenderCopyF(renderer, t, nullptr, &r);
}
void blitRotated(SDL_Texture *t, float cx, float cy, float w, float h, float angleDeg, SDL_Color c) {
    if (!t)
        return;
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, c.a);
    SDL_FRect r{cx - w / 2, cy - h / 2, w, h};
    SDL_RenderCopyExF(renderer, t, nullptr, &r, angleDeg, nullptr, SDL_FLIP_NONE);
}
} // namespace

// ---------------------------------------------------------------- lifetime
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
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_FRect r{x, y, w, h};
    SDL_RenderFillRectF(renderer, &r);
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
float measure(const std::string &s, Face face, float size, float tracking) {
    const Font &f = fonts[int(face)];
    if (f.glyphs.empty())
        return 0;
    float scale = size / f.px, width = 0;
    const std::string clean = sanitize(s);
    for (unsigned char ch : clean)
        width += glyphFor(f, ch)->advance / 16.0f * scale + tracking;
    return clean.empty() ? 0 : width - tracking;
}
void text(float x, float y, const std::string &raw, const Style &st) {
    const Font &f = fonts[int(st.face)];
    if (!f.alphaTexture || f.glyphs.empty())
        return;
    const float scale = st.size / f.px;
    std::string s = sanitize(raw);
    if (st.maxWidth > 0 && measure(s, st.face, st.size, st.tracking) > st.maxWidth) {
        while (!s.empty() && measure(s + "...", st.face, st.size, st.tracking) > st.maxWidth)
            s.pop_back();
        s += "...";
    }
    const float width = measure(s, st.face, st.size, st.tracking);
    const float startX = st.align == Align::Center ? -width / 2 : st.align == Align::Right ? -width : 0;
    const float lineH = f.line * scale, baseline = f.ascent * scale;
    const float angle = st.angle * Tau / 360;

    auto pass = [&](SDL_Texture *tex, bool halo, float ox, float oy, bool gradient, SDL_Color flat) {
        std::vector<SDL_Vertex> v;
        std::vector<int> idx;
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
        geometry(tex, v, idx);
    };
    if (st.glow.a)
        pass(f.haloTexture, true, 0, 0, false, st.glow);
    if (st.outline.a && st.outlineWidth > 0) {
        const int steps = st.outlineWidth > 2.5f ? 12 : 8;
        for (int k = 0; k < steps; ++k) {
            float a = k * Tau / steps;
            pass(f.alphaTexture, false, std::cos(a) * st.outlineWidth, std::sin(a) * st.outlineWidth, false, st.outline);
        }
    }
    pass(f.alphaTexture, false, 0, 0, true, {});
}

// ---------------------------------------------------------------- set pieces
void wall(double time, SDL_Color light) {
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
    const auto step = uint32_t(time * 24);
    const float ox = hash01(step * 2 + 1) * 256, oy = hash01(step * 2 + 7) * 256;
    for (float ty = -oy; ty < H; ty += 256)
        for (float tx = -ox; tx < W; tx += 256)
            blit(grainTex, tx, ty, 256, 256, {255, 255, 255, 22});
}
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
SDL_Texture *loadArtwork(const fs::path &folder) {
    // Clone Hero folders keep cover art next to the chart under a handful of names.
    for (const char *name : {"album.png", "album.jpg", "album.jpeg", "cover.png", "cover.jpg", "cover.jpeg"}) {
        std::error_code ec;
        const fs::path file = folder / name;
        if (!fs::is_regular_file(file, ec))
            continue;
        SDL_RWops *rw = SDL_RWFromFile(file.string().c_str(), "rb");
        if (!rw)
            continue;
        std::vector<Uint8> data(size_t(std::max<Sint64>(0, SDL_RWsize(rw))));
        const size_t got = data.empty() ? 0 : SDL_RWread(rw, data.data(), 1, data.size());
        SDL_RWclose(rw);
        if (got != data.size() || data.empty())
            continue;
        int w = 0, h = 0, channels = 0;
        Uint8 *pixels = stbi_load_from_memory(data.data(), int(data.size()), &w, &h, &channels, 4);
        if (!pixels)
            continue;
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
        SDL_Texture *t = surface ? SDL_CreateTextureFromSurface(renderer, surface) : nullptr;
        SDL_FreeSurface(surface);
        stbi_image_free(pixels);
        if (t) {
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
            return t;
        }
    }
    return nullptr;
}
// Album art as a printed square, the way a cover sits in a CD case.
void photo(SDL_Texture *art, float cx, float cy, float size, float angleDeg, float shade) {
    if (!art)
        return;
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
void gem(size_t color, GemStyle style, float x, float y, float w, Uint8 a) {
    const float scale = w / (SpriteRx * 2);
    blit(gemTex[color][style], x - SpriteCx * scale, y - SpriteCy * scale, SpriteW * scale, SpriteH * scale, {255, 255, 255, a});
}
void receptor(size_t lane, float x, float y, float w, bool pressed, bool power) {
    const float scale = w / (SpriteRx * 2);
    SDL_Color c = power ? ink::power : lanes[lane];
    if (pressed)
        glow(x, y + 10, w * 1.9f, w * .75f, alpha(c, .8f)); // neon underglow
    blit(receptorTex[lane][pressed], x - SpriteCx * scale, y - SpriteCy * scale, SpriteW * scale, SpriteH * scale);
}
void gripTape(const std::array<SDL_FPoint, 4> &p, float v0, float v1, SDL_Color tint) {
    geometry(gripTex,
             {{p[0], tint, {0, v0}}, {p[1], tint, {1, v0}}, {p[2], tint, {1, v1}}, {p[3], tint, {0, v1}}},
             {0, 1, 2, 0, 2, 3});
}
void fire(const Fire &f, float strike, double time, bool power) {
    const auto &flames = flameTex[power ? 1 : 0];
    const SDL_Color laneGlow = power ? ink::power : lanes[size_t(f.lane)];
    const Uint8 sparkG = power ? 240 : 200, sparkB = power ? 255 : 110;
    const int frame = int(time * 24 + f.seed % 7) % FlameFrames;
    if (f.sustain) {
        float flicker = .85f + .15f * std::sin(float(time) * 41 + float(f.seed));
        glow(f.x, strike, 150, 80, alpha(laneGlow, .6f));
        blit(flames[frame], f.x - 40, strike - 46 * flicker - 55 * flicker, 80, 110 * flicker, {255, 255, 255, 210});
        for (int i = 0; i < 3; ++i) {
            double cycle = time * 2.6 + hash01(f.seed + i);
            float life = float(cycle - std::floor(cycle));
            float sx = f.x + (hash01(f.seed * 3 + i + uint32_t(cycle)) - .5f) * 50;
            glow(sx, strike - 10 - life * 90, 9, 9, {255, sparkG, sparkB, u8(255 * (1 - life))});
        }
        return;
    }
    const float t = float(f.age / .38);
    if (t >= 1)
        return;
    const float fade = 1 - t, rise = 1 - fade * fade * fade;
    glow(f.x, strike, 120 + 90 * rise, 60 + 40 * rise, alpha(laneGlow, fade));
    glow(f.x, strike, 70 * fade, 34 * fade, {255, 255, 255, u8(255 * fade)});
    float h = 70 + 110 * rise, w = 130 - 30 * rise;
    blit(flames[frame], f.x - w / 2, strike - h * .42f - h / 2, w, h, {255, 255, 255, u8(255 * fade)});
    float h2 = 50 + 150 * rise;
    blit(flames[(frame + 2) % FlameFrames], f.x - 35, strike - h2 * .45f - 10 * rise - h2 / 2, 70, h2, {255, 255, 255, u8(200 * fade)});
    for (int i = 0; i < 10; ++i) {
        float angle = -Tau / 4 + (hash01(f.seed + i * 13) - .5f) * 2.2f;
        float speed = 160 + 260 * hash01(f.seed * 7 + i), age = float(f.age);
        float sx = f.x + std::cos(angle) * speed * age;
        float sy = strike + std::sin(angle) * speed * age + 520 * age * age;
        float size = 10 + 10 * hash01(f.seed + i * 29);
        glow(sx, sy, size, size, {255, sparkG, sparkB, u8(255 * fade)});
    }
}
} // namespace fret::look
