#pragma once
// Switch Hero's visual kit: a 2000s older-brother's-bedroom rock look built from
// baked procedural textures (grip tape, duct tape, brushed metal, fire), jewel
// gems ray-marched at start-up, and pre-rasterised TTF fonts. Everything draws
// in 1280x720 logical units.
#include "song.hpp"
#include <SDL.h>
#include <array>
#include <string>
#include <vector>

namespace fret::look {
constexpr int W = 1280, H = 720;
extern SDL_Renderer *renderer;

namespace ink {
const SDL_Color white = {236, 236, 240, 255}, dim = {150, 150, 162, 255}, faint = {90, 90, 102, 255},
                acid = {168, 255, 62, 255}, blood = {226, 32, 44, 255}, crt = {70, 150, 255, 255},
                marker = {18, 18, 22, 255}, chrome = {250, 250, 255, 255}, steel = {120, 124, 136, 255},
                power = {90, 200, 255, 255};
}
// Fret lane colours (green, red, yellow, blue, orange, open) of the palette
// in use. setPalette() changes them.
extern std::array<SDL_Color, 6> lanes;
// Note colour palettes the player can pick from. The name is English and is
// translated for display.
struct Palette {
    const char *name;
    std::array<SDL_Color, 6> lanes;
};
const std::vector<Palette> &palettes();
// Recolours the gems, fret buttons, sustains and flames. Call on the main thread.
void setPalette(size_t index);

void init(SDL_Renderer *r);
void rebuild(); // after SDL_RENDER_DEVICE_RESET / SDL_RENDER_TARGETS_RESET
void shutdown();

// Split-screen. Between setViewport() and clearViewport() everything still
// draws in the usual 1280x720 units; SDL scales and offsets it into that
// player's share of the screen, so no draw code needs to know it is in a split.
//
// The share is fitted with its aspect kept, so a two-player split is a pair of
// tall 640x720 panes with the 16:9 frame letterboxed inside each. That costs
// screen height, but it is the one layout where nothing is cropped away and
// every player sees an identical board.
//
// Anything that must cover the whole screen - the wall behind, and grade()
// above all - belongs outside the pair, or it is drawn once per player and the
// seams show.
void setViewport(size_t player, size_t players);
void clearViewport();
// Where player `p`'s pane sits, in screen units, for drawing the dividers.
SDL_FRect paneRect(size_t player, size_t players);

enum class Face { Stencil, Marker, Body };
enum class Align { Left, Center, Right };
struct Style {
    Face face = Face::Body;
    float size = 24;                  // cap-to-descender pixel size
    SDL_Color top = ink::white, bottom = ink::white; // vertical gradient
    SDL_Color outline{0, 0, 0, 0};
    float outlineWidth = 0;
    SDL_Color glow{0, 0, 0, 0};       // soft halo (black makes a drop shadow)
    float glowSpread = 1;
    float shear = 0;                  // italic slant
    float angle = 0;                  // degrees, around the anchor point
    float wobble = 0;                 // per-letter handwriting tilt, degrees
    float tracking = 0;
    Align align = Align::Left;
    float maxWidth = 0;               // truncate with "..." when set
};
float measure(const std::string &s, Face face, float size, float tracking = 0);
void text(float x, float y, const std::string &s, const Style &style);

// Primitives.
void rect(float x, float y, float w, float h, SDL_Color c);
void quad(SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FPoint d, SDL_Color ca, SDL_Color cb, SDL_Color cc,
          SDL_Color cd);
void quad(SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FPoint d, SDL_Color col);
void thickLine(float x1, float y1, float x2, float y2, float width, SDL_Color c);
void disc(float cx, float cy, float rx, float ry, SDL_Color inner, SDL_Color outer, int segments = 40);
void glow(float x, float y, float w, float h, SDL_Color c); // additive soft light
SDL_Color mix(SDL_Color a, SDL_Color b, float t);
SDL_Color alpha(SDL_Color c, float a);
float hash01(uint32_t v);

// Set pieces.
void wall(double time, SDL_Color light); // grungy wall, CRT light, dust
// Vignette, scanlines and film grain over the finished frame. Call last, once,
// after every other draw: the grade has to fall across the whole image for it to
// look like one photograph instead of stacked layers.
void grade(double time);
// Turns the film grain inside grade() off. It is the most expensive part of the
// pass - around fifteen tiled fullscreen blits a frame - and the cheapest of the
// three to lose, so it is the first thing to drop when frames are tight.
void setGrain(bool on);
void tape(float cx, float cy, float w, float h, float angleDeg, float shade = 1);
void plate(float x, float y, float w, float h); // brushed metal panel with screws
void burnedCd(float cx, float cy, float r, double time);
// Album art, if the song folder ships any. Decoding touches no SDL state, so
// it can run on a loader thread; uploading must happen on the main thread.
struct Image {
    int w = 0, h = 0;
    std::vector<Uint8> rgba;
};
Image decodeArtwork(const fs::path &folder);
// A JPEG or PNG in memory, scaled down to at most 512 px a side. Empty on failure.
Image decodeImage(const std::string &bytes);
SDL_Texture *uploadArtwork(const Image &image); // null for an empty image
SDL_Texture *loadArtwork(const fs::path &folder);
void photo(SDL_Texture *art, float cx, float cy, float size, float angleDeg, float shade = 1);
void sticker(float cx, float cy, float r, float angleDeg, SDL_Color fill, const std::string &label);
void star(float cx, float cy, float r, float angleDeg, SDL_Color fill, SDL_Color edge);
void button(float x, float y, const std::string &glyph, const std::string &label); // controller hint
// The same hint as a fret-coloured button, for guitar controls.
void fretButton(float x, float y, size_t lane, const std::string &label);
void ledMeter(float x, float y, float w, float h, int segments, float fill, bool active, double time);
// Rock meter: a vertical red/amber/green gauge with a chrome needle.
void rockMeter(float x, float y, float w, float h, float value, double time, bool danger);

// Highway pieces.
enum GemStyle { GemNormal, GemHopo, GemStar, GemStarHopo, GemStyles };
constexpr size_t PowerColor = 5;
// Top face centre at (x, y) with face width w.
// `squash` flattens it vertically: gems further up the highway are seen at a
// shallower angle, so they are drawn a little flatter than the near ones.
void gem(size_t color, GemStyle style, float x, float y, float w, Uint8 alpha, float squash = 1);
// Uploads every gem texture now (waiting for the background render if needed),
// so none is created mid-song. Call while a song or the calibration loads.
void prepareGems();
// Renders the gems and writes them for assets/gems.bin (a desktop tool: the
// game loads that file instead of rendering at every launch).
void bakeGems(const std::string &path);
// Whether assets/gems.bin still matches the renderer; `problem` says why not.
bool checkGems(std::string &problem);
// `hitFlash` runs 1 to 0 just after a note is hit on this fret.
void receptor(size_t lane, float x, float y, float w, bool pressed, bool power, float hitFlash = 0);
// Highway surfaces. Each tiles along the board, so it scrolls with the notes.
enum class Board {
    GripTape, Rosewood, Synthwave, Pastel, Hellfire, DiamondPlate, CarbonFiber, Thunderstorm, ToxicWaste, Zebra,
    Checkerboard, Nebula, Frostbite
};
constexpr int BoardCount = 13;
// One strip of highway surface: corners clockwise from the far left, texture
// rows v0 to v1 (0 to 1), multiplied by tint.
void board(Board surface, const std::array<SDL_FPoint, 4> &corners, float v0, float v1, SDL_Color tint);
struct Fire {
    float x;
    int lane;
    double age; // seconds since the note was hit
    bool sustain;
    uint32_t seed;
    int tier = 0; // hit timing, 1 good to 3 perfect; bigger bursts for tighter hits
};
void fire(const Fire &f, float strike, double time, bool power);
} // namespace fret::look
