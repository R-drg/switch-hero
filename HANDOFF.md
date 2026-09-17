# Codex handoff

## Goal
Continue Fretboard, an independent controller-first five-fret rhythm game for
Nintendo Switch homebrew, compatible with extracted Clone Hero song folders.
Read README.md and docs/verification.md first. This is a prototype, not a port
of Clone Hero or Guitar Hero source. Do not claim full format or score parity.

## Verified state
Desktop Release build passed. CTest passed fixture generation, 64 core checks,
and five audio codec tests plus playback/pause/restart. SDL virtual-controller
smoke test passed. The original First Light demo is bundled.
Switch backend and NRO target exist but have never been cross-compiled or
hardware tested. No verified NRO exists yet.

## Latest user environment and blocker
The user successfully pulled devkitpro/devkita64:latest with image digest
sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282.
Inside the container, switch-dev, switch-sdl2, switch-libvorbis,
switch-opusfile and switch-mpg123 were already installed.
The attempted build failed BEFORE compilation:
`bash: tools/build-switch.sh: No such file or directory`.
This was a working-directory/mount issue. Do not reinstall working dependencies
as the first troubleshooting step.

## Next action on the user's machine
Run from the cloned repository root (the directory containing CMakeLists.txt):

```bash
docker run --rm -v "$PWD:/work" -w /work devkitpro/devkita64:latest \
  bash -lc 'set -o pipefail; bash tools/build-switch.sh 2>&1 | tee switch-build.log'
```

If this fresh container lacks a required portlib, install the packages in that
same container before building. Container package modifications disappear on
exit with --rm; the /work bind-mounted source and build output persist.
Fix actual compiler/linker errors, preserve desktop behavior, and rerun relevant
tests. The intended output is build-switch/fretboard.nro. Native input uses
libnx; graphics and audio use SDL2; Switch decoders use Vorbis, Opus, mpg123
and a small WAV reader. Desktop uses libsndfile.

After a successful build, ask the user to test the NRO on hardware: startup,
Joy-Con/Pro Controller frets and chords, audio sync/calibration, pause/resume,
completion, and a real song with multiple stems. Physical console verification
must be reported separately from compilation and desktop checks.

## Scope to preserve
Controller first: L/ZL/R/ZR/B frets, A open, X star power, Plus pause.
Press-to-hit default, optional strum mode; remapping and latency settings.
.chart / .mid / song.ini; five-fret tracks; chords, open notes, tempo changes,
sustains and star-power phrases. No direct .sng/archive opening. Switch FLAC
is not supported. Keep imported user songs out of git. The original demo is
safe to track. See README for precise compatibility limits.
