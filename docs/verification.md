# Verification — 2026-09-17

UI rebuild: the imported GH II Deluxe textures were deleted and the game
redesigned around its own 2000s rock look, with every texture generated in code
and three bundled OFL/Apache fonts baked into `.font` atlases. An Ubuntu 24.04
container Release build passed all four CTest cases (fixtures, core checks
including fast-run judgement, and audio codec/playback tests), and the
virtual-controller smoke test hit notes and captured screenshots of the song
library, settings, gameplay, star power and results screens. The rock meter, no-fail mode
and album art landed with unit tests for meter gain, the triple penalty for
misses and overstrums, star-power recovery, failure and no-fail, plus captured
screenshots of the song list with cover art, the meter in play, the red danger
state and the failure screen. Strum judgement gained leniency windows (early strum, strum after a hammer-on),
covered by new core tests, the missed-note sound is rate limited, and star
hammer-ons have their own gem. The MissionControl patch now keeps a guitar out of MotionPlus passthrough,
which otherwise delivered only the Wii Remote's own buttons; the patched module
was rebuilt from the pinned revision (dist ZIP produced) but has not been tested
against a physical guitar. Guitar mode no longer discards normal
controller input when no guitar is connected, Plus/Minus always pass through,
and a controller test panel reports live input; guitar_tests covers the new
routing. Synthesised interface sounds were added on a second always-live mixer channel;
every effect was rendered offline and checked for level and clipping (peaks
0.13-0.58, none silent). A release polish pass added screen fades, score and
multiplier pops, star-power ignition flash and rim light, a counted-in
countdown, a ranked results screen with stats on tape, and song-list detailing;
screenshots of the song list, gameplay, danger, pause, results and failure were
reviewed. The game was renamed to Switch Hero with the
owner's icon embedded in the NRO (verified 256x256 JPEG in the asset section),
and startup now scans metadata only behind a loading screen. The devkitPro
cross-build produced a valid NRO. Physical Switch frame pacing, gem/fire
rendering cost, font legibility on a TV, and guitar-stem muting on a real
multi-stem song have not been measured.

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

The Switch target cross-compiled successfully in the
`devkitpro/devkita64:latest` container with devkitA64 GCC 15.2.0. The resulting
`build-switch/switch-hero.nro` has a valid `NRO0` header. Initial console testing
confirmed startup and song discovery, and exposed distorted, half-speed audio
from requesting float32 samples through the Switch PCM16 backend. The output
queue now uses 48 kHz stereo PCM16; that correction, Joy-Con/Pro Controller
input, timing, pause/resume, multi-stem playback and song completion still
require hardware verification before considering this a usable Switch release.

Follow-up console testing reported choppy, slow playback. devkitPro's SDL2
Switch backend blocks until each 512-sample buffer finishes before submitting
the next, so every buffer leaves a gap in the 5 ms audio renderer frame. The
Switch build now bypasses SDL audio and drives libnx `audrv` directly with six
pre-queued 2048-frame PCM16 buffers, reports position from the voice's played
sample count, and moves decoding off the rendering core. The NRO rebuilds and
desktop tests pass; playback on hardware is not yet re-verified.
