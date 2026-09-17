# Verification — 2026-09-17

The desktop C++17/SDL2 release build completed with GCC 13.3.
CMake/CTest completed all three tests: fixture generation, 64 parser/gameplay
checks, and audio tests covering WAV, OGG, Opus, MP3 and FLAC plus playback,
pause and restart.

The SDL virtual-controller smoke test completed with simulated shoulders,
triggers and face-button input through the normal input path. The accompanying
preview is captured from that run using SDL dummy video/audio drivers.

Fixtures and the included original demonstration song were used. No claim is
made that every community song works. There has been no large-library stress
test or physical controller/audio latency measurement.

## Outstanding Switch gate

This environment has no devkitPro Switch cross-toolchain. The Switch-specific
code and CMake NRO target have not been compiled or executed. There is no NRO
in this package. Build using the README instructions, then verify startup,
Joy-Con/Pro Controller input, audio, timing, pause/resume, multi-stem playback
and song completion on hardware. Correct any compiler/linker errors before
considering this a usable Switch release.
