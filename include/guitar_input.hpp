#pragma once
#include <array>
#include <cstdint>

namespace fret {
// Menus must remain usable from handheld controls after changing guitar mode,
// including when no guitar is paired. Player one is isolated only while playing,
// and only when a guitar is actually connected: otherwise guitar mode would take
// every button away and leave no way to pause or quit.
inline constexpr bool exclusiveGuitarInput(bool wiiGuitar, bool playing, bool guitarConnected) {
    return wiiGuitar && playing && guitarConnected;
}
// `passThrough` names buttons that always come from the normal controller, so
// pause and back can never be lost to a silent guitar.
inline constexpr std::uint64_t controllerButtons(bool wiiGuitar, bool playing, bool guitarConnected,
                                                 std::uint64_t standard, std::uint64_t guitar,
                                                 std::uint64_t passThrough = 0) {
    return exclusiveGuitarInput(wiiGuitar, playing, guitarConnected) ? guitar | (standard & passThrough)
                                                                    : standard;
}
// Button indices in the game's normalized SDL/libnx input representation.
inline constexpr std::array<int, 5> wiiGuitarBindings = {9, 32, 10, 33, 0};
inline std::uint8_t wiiGuitarFrets(std::uint64_t buttons) {
    std::uint8_t frets = 0;
    for (int i = 0; i < 5; ++i)
        if (buttons & (std::uint64_t{1} << wiiGuitarBindings[i]))
            frets |= 1u << i;
    return frets;
}
} // namespace fret
