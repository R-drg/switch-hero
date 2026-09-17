# Fretboard 0.1

A controller-first, five-fret rhythm-game prototype in C++17/SDL2, targeting
Nintendo Switch homebrew. Loads extracted Clone Hero-style song folders.
It is an independent implementation, not a port of Clone Hero's proprietary source.

**Status:** desktop build and automated gameplay tests pass. Switch-specific
controller/audio code and the NRO build target are included, but have not been
cross-compiled or tested on a Switch. This package does not contain a verified NRO.
Do not treat the desktop test as a Switch performance benchmark.

![Desktop virtual-controller test](docs/preview.png)

## Quick desktop build (Ubuntu)

```bash
sudo apt update
sudo apt install build-essential cmake libsdl2-dev libsndfile1-dev python3 ffmpeg
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
./build/fretboard --songs ./songs
```

Use libsndfile 1.1 or newer for MP3 support. The included `First Light` song is
an original generated exercise, with a tempo change from 120 to 150 BPM. To
regenerate it:

```bash
python3 tools/make_demo.py songs/First-Light
```

## Switch build

Install the official devkitPro toolchain using its installation documentation:
<https://devkitpro.org/wiki/Getting_Started>.
In an environment with devkitPro's `dkp-pacman` configured, install:

```bash
sudo dkp-pacman -S --needed switch-dev switch-sdl2 switch-libvorbis switch-opusfile switch-mpg123
bash tools/build-switch.sh
```

If your devkitPro environment uses `pacman` directly, use that executable in
place of `dkp-pacman`. The build script expects CMake on PATH and defaults
`DEVKITPRO` to `/opt/devkitpro`. The `switch-dev` group supplies `switch-cmake`.

The intended output is `build-switch/fretboard.nro`. Once successfully built:

- Put the NRO in `sd:/switch/fretboard/`.
- Put extracted song folders in `sd:/switch/fretboard/songs/`.
- Launch through your existing homebrew setup with full application memory.
- Settings are stored at `sd:/switch/fretboard/settings.cfg`.

First hardware gate: boot, play the included demo using both triggers, pause and
resume, and complete the song. Then verify a real imported song with several
audio stems and calibrate audio/input latency. Native linking, GPU output, input
latency, and performance remain unverified until this gate is run.

## Controls

Default Switch layout uses shoulders/triggers so chords can be played without
trying to press several face buttons with one thumb.

| Action | Switch | Desktop keyboard |
|---|---|---|
| Green / red / yellow / blue / orange | L / ZL / R / ZR / B | A / S / D / F / G |
| Open note | A with frets released | Space |
| Strum, if enabled | D-pad up/down | Up/down or Space |
| Star power | X | Left Shift |
| Pause / resume | Plus | P |
| Menu confirm / resume | A | Enter |
| Back / exit song | Minus | Escape |
| Settings from song list | Y | Tab |
| Restart while paused / at results | Y | R |
| Choose song | D-pad up/down | Up/down |
| Choose instrument/difficulty | L/R or D-pad left/right | Left/right |

On desktop gamepads, directions refer to physical button positions:
south = Switch B / Xbox A; east = Switch A / Xbox B; west = Switch Y / Xbox X;
north = Switch X / Xbox Y. Analog trigger threshold is roughly half travel.

Default **press-to-hit** mode needs a fresh press of at least one required fret
for each note/chord, with all required frets held. Notes do not auto-play from
holding a button. Open notes need a separate open-note press. Frets belonging to
an ongoing extended sustain may remain held underneath new notes.

Optional strum mode supports HOPO/tap transitions and lower-fret anchoring for
single notes. Judgement and scoring are this prototype's rules, not a promise of
exact Clone Hero score parity. There is no fail meter in this build.

Settings include controller fret remapping, press-to-hit/strum mode, audio/input
offset, visual offset, and highway travel speed. Start with offsets at zero;
positive audio/input offset schedules judgement later relative to audio.

## Importing Clone Hero songs

Copy an **extracted song directory**, without editing the chart:

```text
songs/
  Artist - Song/
    song.ini
    notes.chart          (or notes.mid)
    song.ogg             (or song.opus / song.mp3 / guitar.ogg, etc.)
    guitar.ogg           (optional separate stem)
    drums_1.ogg          (optional separate stem)
```

Subdirectories are scanned recursively, to a maximum depth of 16. Standard
file names are recognized case-insensitively. When both `notes.mid` and
`notes.chart` exist, **MIDI takes precedence**. Arbitrary chart filenames must
be renamed to `notes.chart` before import.

Audit parsing and detected stems:

```bash
./build/fretboard --songs /path/to/song-library --inspect
```

This command reports metadata, tracks, note counts, offsets, and parsing
warnings. It returns failure if any discovered song fails to parse. It does not
decode audio; launching a song checks its audio decoder.

### Compatibility boundary

| Feature | Status |
|---|---|
| `notes.chart`, `song.ini` | Implemented; UTF-8 BOM and UTF-16 BOM supported |
| `notes.mid` | PPQN format 0/1, running status, tempo map, note-on/off |
| Five-fret Guitar/Bass/Rhythm/Co-op/Keys | Implemented, Easy through Expert when charted |
| Tempo changes, offsets | Implemented; `song.ini delay` takes precedence over `.chart Offset` |
| Chords, open notes, disjoint/extended sustains | Implemented |
| Natural and forced HOPOs, tap notes | Parsed; used by optional strum mode |
| MIDI enhanced opens and Phase Shift open/tap SysEx | Implemented, including inclusive tap-end behavior |
| Star-power phrases | Implemented, including MIDI 103/116 selection |
| OGG Vorbis, Opus, MP3 | Desktop tested; Switch decoder implementations await hardware/build verification |
| Separate stems | Synchronized streaming mix; numbered drums/vocals replace corresponding combined stems |
| WAV | Desktop tested; Switch implementation supports mono/stereo PCM16 and float32 |
| FLAC | Desktop only; convert to OGG before Switch use |
| Video/background art, lyrics, solos/BRE special scoring | Not implemented; gameplay still reads regular notes |
| `.sng`, ZIP, RAR archives | Not opened directly; extract to a song folder first |
| Six-fret guitars, drums, vocals, pro instruments | Not playable in this version |
| Multiplayer, practice speed, saved highscores | Not implemented |
| Exact Clone Hero engine/score parity | Not claimed |

Audio is mixed at 48 kHz stereo in a worker thread with a bounded queue and
small per-stem decode buffers. Mono/stereo stems at 8–192 kHz are resampled.
The desktop decoder is libsndfile; Switch uses libvorbisfile, libopusfile,
mpg123, and a small WAV reader. Streams are not preloaded in full. Song time
comes from consumed audio samples, with a configurable latency correction;
the exact hardware-output latency still requires calibration.

## Tests

`ctest` generates independent synthetic .chart/MIDI fixtures and codec audio
fixtures using ffmpeg. Tests cover tempo changes, chart/MIDI equivalence,
offset precedence, note modifiers, SysEx boundaries, malformed input,
stem selection, controller-mode hit rules, extended sustains, star power,
audio decoding, and playback clock/pause/restart behavior.

A real SDL virtual-controller integration test (desktop) exercises shoulders,
triggers and a face button through the normal controller input path:

```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/fretboard --songs ./songs --smoke --screenshot preview.bmp
```

It fails if fewer than two notes are hit or playback is interrupted. The smoke
test uses default bindings and does not save its settings. The dummy audio
driver proves software integration, not physical sound/output latency.

## Source map

- `src/song.cpp`: chart/MIDI parsing, metadata, tempo conversions, stem discovery.
- `include/game.hpp`: timing judgement, sustained notes, combo and star power.
- `src/audio.cpp`: streaming decoders, mixing, queue and audio clock.
- `src/main.cpp`: SDL rendering, menus, controller mapping, Switch libnx input.
- `tests/`: generated fixtures and parser/gameplay/audio tests.
- `tools/build-switch.sh`: devkitPro CMake/NRO build.

## References and licenses

The implementation follows publicly documented formats; no Clone Hero or
Guitar Hero game code, graphics, or music is included.

- Format reference: <https://github.com/TheNathannator/GuitarGame_ChartFormats>
- SDL: <https://github.com/libsdl-org/SDL>
- libnx: <https://github.com/switchbrew/libnx>
- devkitPro portlibs/build definitions: <https://github.com/devkitPro/pacman-packages>
- libsndfile: <https://github.com/libsndfile/libsndfile>
- Bitmap font: <https://github.com/dhepper/font8x8>, public domain. The bundled
  header changes its array element type to `unsigned char` for C++ narrowing rules.

Project source: MIT. Dependencies retain their own licenses; their source and
libraries are not bundled in this source archive. See the font header for its
public-domain attribution.
