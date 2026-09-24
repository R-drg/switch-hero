#pragma once
#include <cstddef>
#include <cstdint>

// Guitar Hero Wii extension protocol, as decoded by WiitarThing/Nintroller.
// Input is six unencrypted extension bytes (not the entire HID report).
namespace fret {
struct WiiGuitarState {
    std::uint8_t frets = 0;
    bool up = false, down = false, plus = false, minus = false;
    bool left = false, right = false;
    float whammy = 0; // 0 with the bar at rest, 1 pushed all the way down
};
inline WiiGuitarState decodeWiiGuitar(const std::uint8_t* data, std::size_t size) {
    WiiGuitarState state;
    if (!data || size < 6)
        return state;
    // All-FF is an unavailable extension, not a joystick pushed right.
    bool unavailable = true;
    for (std::size_t i = 0; i < 6; ++i)
        unavailable = unavailable && data[i] == 0xff;
    if (unavailable)
        return state;
    constexpr std::uint8_t masks[] = {0x10, 0x40, 0x08, 0x20, 0x80};
    for (int i = 0; i < 5; ++i)
        if (!(data[5] & masks[i]))
            state.frets |= 1u << i;
    state.up = !(data[5] & 0x01);
    state.down = !(data[4] & 0x40);
    state.plus = !(data[4] & 0x04);
    state.minus = !(data[4] & 0x10);
    // Whammy: five bits, about 0x10 at rest to 0x1B fully down. Guitars
    // differ a little, so the ends are clamped rather than trusted.
    const float bar = (float(data[3] & 0x1f) - 0x10) / (0x1b - 0x10);
    state.whammy = bar < 0 ? 0 : bar > 1 ? 1 : bar;
    // Strip model flag bits. Only horizontal navigation is mapped to avoid
    // creating strums from the guitar joystick's vertical axis.
    const int x = data[0] & 0x3f;
    if (data[0] || data[1]) {
        state.left = x < 16;
        state.right = x > 48;
    }
    return state;
}
} // namespace fret
