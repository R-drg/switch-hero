# Codex handoff

## Goal
Continue Switch Hero, an independent controller-first five-fret rhythm game for
Nintendo Switch homebrew, compatible with extracted Clone Hero song folders.
Read README.md and docs/verification.md first. This is a prototype, not a port
of Clone Hero or Guitar Hero source. Do not claim full format or score parity.

## 0.2 state (2026-09-23)
Tagged `0.2`. What changed is in CHANGELOG.md. Desktop verification: 123
core checks, 1,213 timing checks, audio codec + seek + preview tests (the
local ffmpeg lacks libvorbis, so the Vorbis fixture is made with its native
encoder), guitar and enchor tests, and both smoke runs. The Switch
cross-build passes with no warnings. Details in docs/verification.md.

Unverified on hardware, in priority order:
- **Switch input thread** (`Controller` in `src/main.cpp`). Buttons are
  sampled every ~1 ms on core 1, and presses are stamped on the `now()` clock
  and fed to `pressTime()`. Check press timing with the results screen's timing
  tip, and that the pad still works after sleep/resume.
- **MP3 decoding** (`Mp3Decoder` in `src/audio.cpp`). Float output is now set
  before `mpg123_open`; the old order produced 16-bit samples read as float
  (half-length noise) with desktop libmpg123 1.33. Headerless files are still
  scanned once.
- **Frame pacing.** Turn on Options -> Audio / video sync -> Timing overlay.
  The worst frame goes red above 20 ms. `look::grade()`'s grain loop is still
  the first thing to cut.
- **Gem sprites** ship pre-rendered in `assets/gems.bin`. Ray-marching them
  at every launch cost several seconds of both spare cores on the console and
  starved everything else there. Change `gem3d::render()`, then bump
  `RenderVersion` and run `switch-hero --bake-gems assets/gems.bin`. The
  `gems` CTest fails until you do. Without the file the game still renders
  them itself, just slowly. `prepareGems()` colours and uploads them when a
  song or video calibration starts.

Design decisions from the owner to keep:
- Gems are cut jewels in a gunmetal bezel, with a white table for hammer-ons
  and cut stars for star notes. The owner picked this from rendered options,
  after rejecting glossy chrome ("AI slop") and flat stickers ("flash game").
- The fret buttons and highway were approved as they are.
- B backs out of menus, except where B is a fret or being tested.

## Verified state (0.1)
Desktop Release build passed. CTest passed fixture generation, 64 core checks,
and five audio codec tests plus playback/pause/restart. SDL virtual-controller
smoke test passed. The original First Light demo is bundled.
The Switch backend and NRO target cross-compiled successfully in the
`devkitpro/devkita64:latest` image with devkitA64 GCC 15.2.0. The generated
`build-switch/switch-hero.nro` has a valid `NRO0` header. Initial hardware testing
found two Switch-only issues: libstdc++ canonicalization rejected `sdmc:/`
paths, and float32 output was misinterpreted by the PCM16 audio backend. Both
are corrected in the latest NRO; hardware retesting remains outstanding.

## Latest user environment
The user successfully pulled devkitpro/devkita64:latest with image digest
sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282.
Inside the container, switch-dev, switch-sdl2, switch-libvorbis,
switch-opusfile and switch-mpg123 were already installed. Mounting the repository
at `/work` and setting that as the container working directory resolved the
earlier pre-compilation path failure.

## Rebuilding on the user's machine
Run from the cloned repository root (the directory containing CMakeLists.txt):

```bash
docker run --rm -v "$PWD:/work" -w /work devkitpro/devkita64:latest \
  bash -lc 'set -o pipefail; bash tools/build-switch.sh 2>&1 | tee switch-build.log'
```

If this fresh container lacks a required portlib, install the packages in that
same container before building. Container package modifications disappear on
exit with --rm; the /work bind-mounted source and build output persist.
Fix actual compiler/linker errors, preserve desktop behavior, and rerun relevant
tests. The intended output is build-switch/switch-hero.nro. Native input uses
libnx; graphics and audio use SDL2; Switch decoders use Vorbis, Opus, mpg123
and a small WAV reader. Desktop uses libsndfile.

The next gate is hardware testing: startup, Joy-Con/Pro Controller frets and
chords, audio sync/calibration, pause/resume, completion, and a real song with
multiple stems. Physical console verification must be reported separately from
compilation and desktop checks.

## Scope to preserve
The Guitar Hero II Deluxe texture import was removed: the game must keep its own
2000s "older brother" rock look (burned CD-R and Sharpie, duct tape, grip tape,
spray-paint stencil, chrome, CRT glow) and never ship third-party game art. All
textures are generated procedurally in `src/look.cpp`; the only assets are the
three OFL/Apache fonts in `assets/fonts`, embedded in RomFS.
Full desktop CTest and virtual-controller smoke passed after the UI changes;
the latter hit 12 notes. Font atlas, bounded note traversal, and visual clock
interpolation reduce rendering overhead/stutter.

Judgement was then reworked: the hit window tightened from 100 ms to 70 ms and
`press()` now takes the note nearest in time rather than the earliest in the
window. The old pair let a dropped note in a fast run swallow the next press and
put every press after it a note behind; `core_tests` covers this and fails
against the previous logic. Hits are graded perfect/great/good with a score
bonus and on-screen feedback. Desktop core checks are at 94 and the smoke test
now hits 17 notes.

**Unverified on hardware:** `look::grade()` adds roughly three extra fullscreen
alpha passes per frame (vignette, scanlines, and ~15 tiled grain blits). This is
free on desktop but has not been measured on console, and frame pacing there was
already untested. If the Switch build drops frames, the grain loop in
`src/look.cpp` is the first thing to cut — it is self-contained and removing it
costs the least of the three.

Controller first: L/ZL/R/ZR/B frets, A open, X star power, Plus pause.
Press-to-hit default, optional strum mode; remapping and latency settings.
.chart / .mid / song.ini; five-fret tracks; chords, open notes, tempo changes,
sustains and star-power phrases. No direct .sng/archive opening. Switch FLAC
is not supported. Keep imported user songs out of git. The original demo is
safe to track. See README for precise compatibility limits.
