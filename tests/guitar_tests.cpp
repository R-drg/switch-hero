#include "guitar_input.hpp"
#include "wii_guitar.hpp"
#include <array>
#include <cstdlib>
#include <iostream>

void check(bool ok, const char* message) {
    if (!ok) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
int main() {
    constexpr std::uint64_t save = 1ull << 4, navigate = 1ull << 12;
    // Reproduce changing mode using handheld Joy-Cons with no player-one pad.
    check(fret::controllerButtons(false, false, true, save, 0) == save, "normal menu save");
    check(fret::controllerButtons(true, false, true, save, 0) == save,
          "enabling guitar mode must preserve handheld save");
    check(fret::controllerButtons(true, false, false, navigate, 0) == navigate,
          "guitar menus remain navigable without a guitar");
    check(fret::controllerButtons(true, true, false, save | navigate, 0) == (save | navigate),
          "without a connected guitar, play keeps normal controls");
    check(fret::controllerButtons(true, true, true, save | navigate, navigate, save) == (navigate | save),
          "pause and back always answer from the normal controller");
    check(fret::controllerButtons(true, true, true, save | navigate, navigate) == navigate,
          "guitar gameplay otherwise isolates player one");
    check(fret::controllerButtons(true, false, true, save, 0) == save,
          "paused guitar gameplay restores handheld menu input");
    std::array<std::uint8_t, 6> report{32, 32, 15, 16, 255, 255};
    auto idle = fret::decodeWiiGuitar(report.data(), report.size());
    check(!idle.frets && !idle.up && !idle.down && !idle.plus && !idle.minus && !idle.left && !idle.right &&
              idle.whammy == 0,
          "neutral report");
    {
        auto bar = report;
        bar[3] = 0x1b;
        check(fret::decodeWiiGuitar(bar.data(), 6).whammy == 1, "whammy fully down");
        bar[3] = 0x80 | 0x1f; // high bits belong to other fields; past the range clamps
        check(fret::decodeWiiGuitar(bar.data(), 6).whammy == 1, "whammy clamps high");
        bar[3] = 0x0e;
        check(fret::decodeWiiGuitar(bar.data(), 6).whammy == 0, "whammy clamps low");
        bar[3] = 0x15;
        const float half = fret::decodeWiiGuitar(bar.data(), 6).whammy;
        check(half > .4f && half < .5f, "whammy halfway");
    }
    // Every chord, with both strum directions and active-low fret ordering.
    constexpr unsigned masks[] = {0x10, 0x40, 0x08, 0x20, 0x80};
    for (unsigned chord = 0; chord < 32; ++chord) {
        report[5] = 255;
        std::uint64_t buttons = 0;
        for (int lane = 0; lane < 5; ++lane) {
            if (chord & (1u << lane)) {
                report[5] &= ~masks[lane];
                buttons |= std::uint64_t{1} << fret::wiiGuitarBindings[lane];
            }
        }
        check(fret::decodeWiiGuitar(report.data(), 6).frets == chord, "chord decode");
        check(fret::wiiGuitarFrets(buttons) == chord, "game mapping");
        report[5] &= ~1u;
        auto up = fret::decodeWiiGuitar(report.data(), 6);
        check(up.up && !up.down && up.frets == chord, "up strum");
        report[5] |= 1u;
        report[4] = 0xbf;
        auto down = fret::decodeWiiGuitar(report.data(), 6);
        check(!down.up && down.down && down.frets == chord, "down strum");
        report[4] = 255;
    }
    report = {32, 32, 15, 16, 0xeb, 255};
    auto menu = fret::decodeWiiGuitar(report.data(), 6);
    check(menu.plus && menu.minus && !menu.frets, "plus/minus");
    report[0] = 0x80 | 32;
    check(!fret::decodeWiiGuitar(report.data(), 6).right, "GH3 model bit ignored");
    report[0] = 0x80 | 63;
    check(fret::decodeWiiGuitar(report.data(), 6).right, "joystick right");
    report[0] = 0;
    check(fret::decodeWiiGuitar(report.data(), 6).left, "joystick left");
    report[1] = 0;
    check(!fret::decodeWiiGuitar(report.data(), 6).left, "unavailable stick");
    for (std::size_t n = 0; n < 6; ++n)
        check(!fret::decodeWiiGuitar(report.data(), n).frets, "short packet");
    check(!fret::decodeWiiGuitar(nullptr, 6).frets, "null packet");
    report.fill(255);
    auto absent = fret::decodeWiiGuitar(report.data(), 6);
    check(!absent.frets && !absent.right && absent.whammy == 0, "unavailable extension");
    check(!fret::wiiGuitarFrets((1ull << 3) | (1ull << 6) | (1ull << 11)), "actions aren't frets");
    std::cout << "Wii guitar input tests passed\n";
}
