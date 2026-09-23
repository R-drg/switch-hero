# Switch Hero 0.2

A five-fret rhythm game for Nintendo Switch homebrew, written in C++17 and SDL2.
It plays Clone Hero-style song folders, downloads charts from
[Chorus Encore](https://www.enchor.us/) in-game, and works with Joy-Cons, a Pro
Controller or a Wii guitar. It is an independent implementation, not a port of
Clone Hero's proprietary source.

![Gameplay](docs/gameplay.jpg)

| Title menu | Song list |
|---|---|
| ![Title menu](docs/title.jpg) | ![Song list](docs/song-list.jpg) |
| **Difficulty select** | **Results** |
| ![Difficulty select](docs/difficulty.jpg) | ![Results](docs/results.jpg) |

What changed in this release is in [CHANGELOG.md](CHANGELOG.md).

**Status:** the desktop build and all automated tests pass, and the Switch build
cross-compiles with devkitPro into `build-switch/switch-hero.nro`. Early console
tests confirmed startup and song discovery. The latest build still needs a full
run on hardware, covering audio, smooth scrolling, calibration and chart
downloads. New in 0.2 and still untested on a console: the input thread's
press timing, the MP3 decode fix, frame pacing with the new effects (turn on
the timing overlay to check), and how long the startup gem render takes. Don't
treat the desktop tests as a Switch performance benchmark.

## Install on a Switch

The ready-to-copy build is in [`release/`](release/). Copy `release/switch-hero`
into the `switch` folder at the root of the SD card:

```text
sd:/switch/switch-hero/
  switch-hero.nro
  songs/First-Light/     (demo song)
```

Launch it from the Homebrew Menu. Hold R while starting any game to open the
Homebrew Menu with full memory. That's recommended for big songs and needed if
the download keyboard won't open. Settings are saved to
`sd:/switch/switch-hero/settings.cfg` and high scores to
`sd:/switch/switch-hero/scores.cfg`. [`release/INSTALL.txt`](release/INSTALL.txt)
has the same steps for the SD card.

Coming from the prototype when it was called Fretboard? Move your songs and
`settings.cfg` from `sd:/switch/fretboard/` to `sd:/switch/switch-hero/`, then
delete the old folder so the Homebrew Menu only lists Switch Hero.

## Features

- **Smooth, steady scrolling.** The song clock runs on the real-time timer and
  slowly leans towards the audio's average position, changing speed by at most
  1% (the approach YARG uses). Notes don't wobble with the audio backend's
  buffer steps. Each note sits exactly where the highway's perspective puts it,
  and notes fade in from the far end of the board.
- **Hit windows and timing grades.** How far off a note can be and still
  count depends on the difficulty: on the Normal setting, +-85 ms on Expert,
  92 on Hard, 100 on Medium and 110 on Easy. Options -> Gameplay -> Hit window
  makes that Strict (about the old fixed 70 ms on Expert) or Lenient. Inside the
  window, **perfect** is the closest ~36% of it, **great** the closest ~64%,
  and the rest is **good**, worth 1.35x, 1.15x and 1x. Presses are judged at
  their input timestamps (SDL events on desktop, the input thread on Switch)
  rather than at the frame that reads them.
- **Timing tips.** If a song's hits were consistently early or late (the
  median of at least 12 hits is 6 ms or more off), the results screen says so
  and offers **Fix timing**, which moves the audio offset by that amount.
- **Offset calibration.** Tap along to set the audio and visual offsets (see
  [Calibration](#calibration)).
- **In-game chart downloads** from Chorus Encore (see
  [Downloading charts](#downloading-charts)).
- **Cut-jewel gems and classic fire.** Gems are rendered in 3D once at
  startup, on background threads. A ray marcher lights each sprite pixel of a
  cut jewel in a gunmetal bezel: eight crown facets rise to a flat table, and
  each facet catches the room and the stage light up the highway differently.
  The result is saved as textures, so a gem costs one sprite per frame. Gems
  flatten slightly toward the far end of the board, as the view angle there is
  shallower. Hammer-ons light the table up white, which stays readable at play
  size, and star notes are cut stars. Hits throw orange fire, a shockwave, and spark streaks. Perfect hits burn
  bigger with a star glint, and star power burns blue-white. The strike line
  pulses with the beat. It's all baked into textures at startup, so it costs
  little per frame.
- **High scores.** Your best score, stars and full combos are saved for each
  song, part and difficulty. They show on the song list and difficulty screen,
  and the results screen calls out a new best.
- **Smooth browsing.** Charts and covers load on a background thread once the
  cursor rests, and previews open their audio off the main thread too, so
  scrolling a big library never hitches. The song list sorts by title, artist
  or best stars (ZR), and left/right jump between letters, or between star
  counts when sorted by stars.
- **Song previews.** Rest on a song and it fades in from its
  `preview_start_time` (or 30% of the way in), then loops.
- **Rock meter, star power and no-fail mode.** A miss drains the meter three
  times as fast as a hit refills it. Star power pulls it back twice as hard.
- **Three controller modes:** press-to-hit for Joy-Cons and Pro Controllers,
  strum, and Wii guitar over Bluetooth.
- **Its own look.** A 2000s bedroom full of burned CD-Rs, duct tape and grip
  tape, with every texture generated in code (see [Look](#look)).
- **Split-screen multiplayer**, two to four players, docked only (see
  [Multiplayer](#multiplayer)).

## Multiplayer

Two to four players share one console and one song, each on their own board.
Pick **Multiplayer** on the title menu, choose a song, and the lobby opens.

**Docked only.** Four boards on the handheld screen would be unreadable, and
players two to four have no way to hold a Joy-Con that is attached to the
console. The menu entry says so before it refuses, and undocking during a song
pauses it rather than playing on.

In the lobby every seat is driven by its own controller, all at once:

| Action | Button |
| --- | --- |
| Join / leave | A / B |
| Instrument | Left / Right |
| Difficulty | Up / Down |
| Start | Plus |

Each player picks their own instrument and difficulty, and gets the hit window
that difficulty earns, so an expert and a beginner can share a song. Difficulty
stepping skips tiers the chart does not carry.

The screen splits side by side for two players and into quadrants for three or
four; with three, the fourth quadrant is left empty. Each board keeps its own
score, streak, multiplier, star power and rock meter — nothing is shared but the
clock and the audio.

Three rules differ from single player, all for the same reason: four people are
sharing one screen and one speaker.

- **No-fail is forced.** Dropping a failed player would leave a dead quarter of
  the screen for the rest of the song. The rock meter still moves and still
  reads red; it just stops ejecting anyone.
- **The guitar stem is never ducked.** There is one shared audio stream, so
  muting it because one player is in the red would punish the other three.
- **No high scores are written.** `scores.cfg` is keyed by song, part and
  difficulty with no room for a player, so four runs would fight over one
  record. The final standings are shown instead: rank, score, accuracy, best
  streak and full combo, with ties sharing a rank.

If frames drop with four boards on screen, turn off **Options -> Audio / video
sync -> Film grain** first — it is the most expensive effect and the cheapest to
lose.

## Controls

The default Switch layout uses the shoulders and triggers, so chords don't need
several face buttons under one thumb.

| Action | Switch | Desktop keyboard |
|---|---|---|
| Green / red / yellow / blue / orange | L / ZL / R / ZR / B | A / S / D / F / G |
| Open note (press-to-hit) | Any fret, or A | Any fret, or Space |
| Strum, if enabled | D-pad up/down | Up/down or Space |
| Star power | X | Left Shift |
| Whammy | Wiggle either stick | Hold W |
| Pause / resume | Plus | P |
| Move through menus | D-pad up/down | Up/down |
| Jump to the next/previous letter in the song list | D-pad left/right | Left/right |
| Change the song list order (title, artist, stars) | ZR | Z |
| Select / confirm | A | Enter |
| Back (menus) | B or Minus | Escape |
| Options from the song list | Y | Tab |
| Download charts from the song list | Plus | O |
| Delete a song from the song list | X | Delete or X |
| Toggle no-fail on the difficulty screen | X | N |

Holding Minus for two seconds returns to the song list from any screen.

B backs out of every menu. It's ignored wherever B is being pressed as a
button rather than to leave: during a song and the resume count-in (it's the
orange fret), while calibration is taking taps, while rebinding a fret, in the
controller test, and in Wii guitar mode (the red fret is back there). Minus
always works as back.

### Menus

The game follows the Guitar Hero flow. The title menu offers **Quickplay**,
**Download songs**, **Options** and **Quit**. Quickplay opens the song list.
Picking a song asks for an **instrument** (skipped when the chart only has
one part), then a **difficulty**. All four difficulties are always listed,
and the ones the chart doesn't have are greyed out and skipped. The song list
shows each part's charted difficulties as E/M/H/X badges. The game remembers
the last part and difficulty you played and opens the next song on them.

**Pause** (Plus) offers resume, restart, change difficulty and quit to the song
list. B, Minus or Plus resumes, so backing out of a pause never throws the song
away. Resuming counts 3-2-1 over the frozen highway first, and Plus or Minus
during the count goes back to the pause menu. The results screen offers
continue, retry and change difficulty. It ignores buttons for its first
second, so frets still being played as a song ends or fails (B is the orange
fret) can't skip past it; the button hints appear once it's listening. The song list opens on the last song
you played.

**Options** has four pages, and each row explains itself at the bottom of the
panel:

- **Gameplay:** controller mode, no fail, note speed, lefty flip (mirrors
  the highway so green is on the right).
- **Audio:** music volume (songs and previews) and sound effects volume.
- **Audio / video sync:** calibrate audio, audio offset, calibrate video,
  visual offset, and a timing overlay that shows average and worst frame
  times over each second (plus audio clock drift during a song), for checking
  smoothness on the console.
- **Controls:** fret buttons, controller test, reset all options (press A
  twice to confirm).

Options are saved when you leave the options screen.

On desktop gamepads, directions refer to physical button positions:
south = Switch B / Xbox A; east = Switch A / Xbox B; west = Switch Y / Xbox X;
north = Switch X / Xbox Y. Analog triggers count as pressed at about half travel.

On the Switch, buttons are sampled on their own thread about once a
millisecond, so a press is judged when it arrived rather than rounded to the
next frame. That removes up to 16.7 ms of rounding, but not the controller's
own report interval.

### Press-to-hit mode

The default mode needs a fresh press for each note. Notes never play
themselves while a button is held.

- **Single notes** are forgiving: the note's fret has to go down, and a fret
  still held from the previous note in a fast run doesn't spoil the hit.
- **Chords** must be fretted exactly. An extra fret makes a wrong chord, not a
  sloppy right one. Any change that lands on the exact shape counts, so a
  repeated chord can be re-struck by re-pressing one fret while the others stay
  down, and a stray finger can be lifted to fix the chord.
- **Open notes** take any fret, or the dedicated open button.
- Frets held down by an ongoing extended sustain aren't part of the shape and
  never count against a chord.
- Two notes pressed within the same frame both count.
- A press takes the note it fits that is **nearest in time**, not the earliest
  one still inside the window. Otherwise, in a fast run, a dropped note would
  swallow the next press, and every press after it would land a note behind.

### Strum mode

Strum mode, which Wii guitars use, supports HOPO and tap transitions, and lower-fret
anchoring for single notes. It has the leniency real guitar games have:
- A strum just before its note still counts.
- A stray strum only costs you once it has had its chance to land.
- Strumming a hammer-on you already played is ignored rather than punished.

Hammer-ons show a white-hot core. Star-power notes are star-shaped, with the
same core when they're hammer-ons. Miss any note of a star phrase and the
phrase is broken: the rest of its notes turn back into regular gems and it
earns no star power. Whammying a held star-phrase sustain (moving the whammy
bar, or either stick on a controller) fills star power while it rings, and
stretches an active star power. Holding the bar still earns nothing. The
song's pitch doesn't bend; only the sustain wobbles, and it glows blue while
it's filling the meter. Judgement and scoring are this game's own
rules, not a promise of exact Clone Hero score parity.

### Customization

Options -> Customize picks the **highway** and the **note colors**.

- **Highways (13):** Grip tape, Rosewood, Synthwave, Pastel dream, Hellfire,
  Diamond plate, Carbon fiber, Thunderstorm, Toxic waste, Zebra,
  Checkerboard, Nebula and Frostbite. Some have their own effects: lightning,
  embers, hazard-stripe rails, neon grid lines or a retro sun.
- **Note colors (24):** from the four-color sets (Classic, Pastel, Neon,
  Colorblind) to single-color ones like Blood or Gold record, alternating
  ones like Bumblebee or Candy cane, and gradients like Inferno or Sunset.

A small preview beside the options plays a chart by itself, so each choice
can be seen moving before a song. Highways and colors mix freely.

### Wii guitar over Bluetooth

Select **WII GUITAR** in Options → Gameplay → Controller mode. This uses fixed five-fret
bindings, strum judgement, guitar menu shortcuts, Minus for star power and the
whammy bar.
Bluetooth needs the supplied **MissionControl guitar-extension patch** from
[`integrations/missioncontrol/`](integrations/missioncontrol/README.md).
Installing the game alone, or using stock MissionControl, isn't enough.

Guitar mode only takes over player one while a controller is connected there.
Plus and Minus always respond from the normal controller, so guitar mode can't
lock you out of the game. **Options → Controls → Controller test** shows live input for
diagnosing a guitar. Tilt, whammy effects and touch-strip inputs aren't
implemented.

## Calibration

Options → Audio / video sync has an **audio / input offset** and a **visual
offset**. Select **Calibrate audio** or **Calibrate video** and press A (green
on a Wii guitar) to calibrate by tapping along:

1. **Audio / input offset:** a click track plays through the song channel, so
   it has the same latency as the music. Tap any fret or strum on every click
   for 12 taps. Nothing on screen moves with the beat, so only your ears set
   the timing.
2. **Visual offset:** do the audio offset first. The clicks go silent and notes
   scroll down the highway. Tap as each note crosses the line.

Each tap is scored against the nearest beat, and the median error becomes the
offset. The median means one fumbled tap can't throw it off. The result screen
shows the old and new values and warns you when your taps were uneven. A keeps
the new value, Y retries, and B or Minus cancels. Left/right on either offset row
fine-tunes it in 5 ms steps. A positive audio offset judges notes later.

## Song library

Switch Hero reads songs from `sd:/switch/switch-hero/songs` (on desktop,
`./songs` or `--songs FOLDER`). At startup a loading screen counts the songs as
it finds them. The scan reads only each song's title and artist; the chart is
parsed when you select the song, and any problems are reported then.

### Downloading charts

Choose **Download songs** on the title menu, or press **Plus** on the song list
(**O** on a keyboard), to browse Chorus Encore, the community Clone Hero chart index. The screen opens on the newest
charts. Only charts with a lead guitar part are listed.

- **Y** searches by song, artist or charter. The Switch opens its system
  keyboard. On desktop, type the search and press Enter.
- **Up/down** or strum moves through the results. More results load as you
  near the end of the list.
- **A** downloads the selected chart into the songs folder, as
  `Artist - Name (Charter)`.
- **B** (or Minus) cancels a download that's running. Otherwise it goes back. If
  anything was downloaded, it goes to the song list, which rescans and selects
  the newest download.

Each row shows the guitar intensity rating (six pips, or `?` when the chart
has no rating), the song length, and **in library** once you have it.

Charts arrive as `.sng` packages. The game unpacks them in a staging folder and
moves them into place only when complete, so a failed or cancelled download
never leaves half a song behind. Background videos are skipped, because the
game doesn't play them. The package's metadata becomes `song.ini`.

The Switch needs an internet connection. Networking only starts the first time
you open the download screen. HTTPS uses the console's own SSL service.
Requests identify the game as `switch-hero/0.2`.

### Deleting songs

Press **X** on the song list (the blue fret on a Wii guitar, Delete on a
keyboard) to delete the selected song. The game asks first, starting on
**Keep it**, and names the folder it will remove. Deleting removes that song's
folder and everything in it from the SD card, along with its high scores. It
can't be undone. The game only ever deletes a folder inside the songs folder,
never the songs folder itself.

### Copying songs yourself

Copy an **extracted song folder** without editing the chart:

```text
songs/
  Artist - Song/
    song.ini
    notes.chart          (or notes.mid)
    song.ogg             (or song.opus / song.mp3 / guitar.ogg, etc.)
    guitar.ogg           (optional separate stem)
    drums_1.ogg          (optional separate stem)
```

Subfolders are scanned recursively, up to 16 levels deep. Standard file names
are recognized case-insensitively. When both `notes.mid` and `notes.chart`
exist, **MIDI takes precedence**. Other chart file names must be renamed to
`notes.chart`.

Check a library's parsing and detected stems:

```bash
./build/switch-hero --songs /path/to/song-library --inspect
```

This reports metadata, tracks, note counts, offsets and parsing warnings, and
fails if any song fails to parse. It doesn't decode audio; launching a song
checks its audio.

### Compatibility

| Feature | Status |
|---|---|
| `notes.chart`, `song.ini` | Implemented; UTF-8 BOM and UTF-16 BOM supported |
| `notes.mid` | PPQN format 0/1, running status, tempo map, note-on/off |
| Five-fret Guitar/Bass/Rhythm/Co-op/Keys | Implemented, Easy through Expert when charted |
| Tempo changes, offsets | Implemented; `song.ini delay` takes precedence over `.chart Offset` |
| Chords, open notes, disjoint/extended sustains | Implemented |
| Natural and forced HOPOs, tap notes | Parsed; used by strum mode |
| MIDI enhanced opens and Phase Shift open/tap SysEx | Implemented, including inclusive tap-end behavior |
| Star-power phrases | Implemented, including MIDI 103/116 selection |
| OGG Vorbis, Opus, MP3 | Desktop tested; Switch decoders built in, awaiting a full hardware run |
| Separate stems | Synchronized streaming mix; numbered drums/vocals replace corresponding combined stems |
| WAV | Desktop tested; Switch supports mono/stereo PCM16 and float32 |
| FLAC | Desktop only; convert to OGG before Switch use |
| `.sng` packages | Unpacked by the in-game downloader; copied `.sng` files aren't opened directly |
| ZIP, RAR archives | Not opened; extract to a song folder first |
| Accented titles | Shown without accents (the fonts are ASCII); other scripts show as `?` |
| Video/background art, lyrics, solos/BRE special scoring | Not implemented; gameplay still reads regular notes |
| Six-fret guitars, drums, vocals, pro instruments | Not playable in this version |
| Multiplayer, practice speed, saved high scores | Not implemented |
| Exact Clone Hero engine/score parity | Not claimed |

## Sound

Audio is mixed at 48 kHz stereo on a worker thread, with a bounded queue and
small per-stem decode buffers. Mono and stereo stems at 8–192 kHz are
resampled, and streams are never loaded in full. The desktop decoder is
libsndfile. The Switch uses libvorbisfile, libopusfile, mpg123 and a small WAV
reader. Minor damage in a stem, such as an Ogg page with bad timestamps, is
skipped rather than stopping the song.

The game ships no sound-effect files: every interface sound is synthesised in
code when the mixer starts (`buildSfx` in `src/audio.cpp`):
- a pick scrape for moving through the list
- a distorted power chord for confirming
- drumstick clicks counting the song in
- a dead-string screech when a note is dropped
- a riser into star power
- a shimmer for streak milestones
- chords for finishing or failing a set

One mixer runs for the whole session. The song plays on its own pausable
channel, and effects play on a second channel that is always live, so menus and
the pause screen still make noise. If a second output can't be opened, the song
still plays, just without effects.

## Rock meter

A song is scored against a rock meter that works like a tug of war. Every note
hit pulls the needle one step toward green, while a missed note or a strum at
nothing drags it three steps toward red. Star power pulls twice as hard per
note. Deep in the red the meter flashes, the screen pulses red, and the guitar
stem drops out of the mix while the rest of the band keeps playing. The stem
comes back when you recover. If the meter empties, the song ends in failure.
Songs with a single mixed audio file keep playing normally in the red, since
there's no separate guitar to mute.

Turn the meter off with **No fail** in Options → Gameplay, or with X (N on a
keyboard, blue fret on a Wii guitar) on the difficulty screen, which shows
whether it's on.

## Look

Switch Hero has its own art direction: a 2000s older-brother bedroom, all burned
CD-Rs and Sharpie, duct tape, skate grip tape, spray-paint stencil lettering,
chrome and a CRT glow. Every texture is generated in code at startup: grip
tape, tape strips, brushed metal, the wall, gems, fret buttons and fire. The
only art files are three fonts in `assets/fonts`. They're embedded in the
Switch NRO's RomFS, and loaded from the working directory on desktop. A song's
own `album.jpg`/`album.png` (or `cover.*`) is shown beside the disc in the song
list, decoded with the vendored public-domain `stb_image.h`.

| Font | Use | License |
|---|---|---|
| Black Ops One | stencil headings, score | SIL Open Font License 1.1 |
| Permanent Marker | Sharpie handwriting | Apache License 2.0 |
| Russo One | body text | SIL Open Font License 1.1 |

The frame is finished with a single grade pass (vignette, CRT scanlines and a
tiling film grain), drawn by `look::grade()` **after everything else**, once,
just before the frame is presented. That way the highway, gems and HUD share one
light instead of each carrying its own, and the image reads as one photograph
rather than pasted-together parts. Anything added to a screen has to draw before
the grade to sit under it.

Gems cast a contact shadow on the board, a held fret spills its colour back up
the lane, and the far end of the highway is hazed over, with notes fading in as
they leave it. The gameplay read-outs are bolted to a brushed-metal faceplate
with engraved rules, and a long score shrinks to fit it.

The board is only lightly foreshortened, and the default lookahead is 1.2
seconds. A harder taper or a longer lookahead crushes most of the chart into the
top of the highway. If fast runs are hard to read, raise **Note speed**
in Options → Gameplay.

`tools/make_fonts.py` re-bakes the `.font` atlases from the TTFs (needs Pillow).

## Icon

`icon.jpg` (256x256) is the NRO icon shown in the Homebrew Menu, supplied by the
project owner.

## Building

### Desktop (Ubuntu)

```bash
sudo apt update
sudo apt install build-essential cmake libsdl2-dev libsndfile1-dev libcurl4-openssl-dev python3 ffmpeg
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
./build/switch-hero --songs ./songs
```

Use libsndfile 1.1 or newer for MP3 support. The included `First Light` song is
an original generated exercise, with a tempo change from 120 to 150 BPM. To
regenerate it:

```bash
python3 tools/make_demo.py songs/First-Light
```

`--timing` shows frame time, how far the song clock sits from the audio
(drift), and the clock's running speed (rate) while playing. Healthy playback
keeps drift within a few ms and rate within 1 ± 0.01.

### Switch

Install the official devkitPro toolchain following
<https://devkitpro.org/wiki/Getting_Started>. With `dkp-pacman` configured:

```bash
sudo dkp-pacman -S --needed switch-dev switch-sdl2 switch-libvorbis switch-opusfile switch-mpg123 switch-curl
bash tools/build-switch.sh
```

If your devkitPro setup uses `pacman` directly, use that in place of
`dkp-pacman`. The build script expects CMake on PATH and defaults `DEVKITPRO` to
`/opt/devkitpro`. The `switch-dev` group supplies `switch-cmake`. Without a local
toolchain, build in Docker:

```bash
docker run --rm -v "$PWD:/work" -w /work devkitpro/devkita64:latest \
  bash -lc 'bash tools/build-switch.sh'
```

The output is `build-switch/switch-hero.nro`. Copy it over
`release/switch-hero/switch-hero.nro` to refresh the release.

First hardware check:
1. Boot the game and play the demo song with both triggers.
2. Pause, resume and finish the song.
3. Play a downloaded song with several audio stems.
4. Calibrate both offsets.
5. Play a dense chart with steady scrolling.

## Tests

`ctest` runs:

- **core**: tempo changes, chart/MIDI equivalence, offset precedence, note
  modifiers, SysEx boundaries, malformed input, stem selection, hit rules,
  extended sustains, star power and the rock meter. Its fixtures are generated
  by `tests/make_fixtures.py`, which needs ffmpeg built with libvorbis.
- **audio**: WAV, OGG, Opus, MP3 and FLAC decoding, plus playback, pause and restart.
- **timing**: the song clock against a stepped audio clock (speed, settling,
  jumps, never running backwards), press timestamps, and tap calibration.
- **enchor**: search requests and responses, folder names, and `.sng`
  unpacking, including path traversal, truncated and bogus packages.
- **guitar**: Wii guitar input routing.

A real SDL virtual-controller test plays the demo through the normal input path:

```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/switch-hero --songs ./songs --smoke --screenshot preview.bmp
```

It fails if fewer than two notes are hit or playback is interrupted. It uses
default bindings and doesn't save its settings. The dummy audio driver proves
the software works together, not physical sound or output latency.

## Source map

- `src/song.cpp`: chart/MIDI parsing, metadata, tempo conversions, stem discovery.
- `include/game.hpp`: timing judgement, sustains, combo, star power and rock meter.
- `include/clock.hpp`: the song clock.
- `include/calibration.hpp`: tap-along offset calibration.
- `src/audio.cpp`: streaming decoders, mixing, sound effects and the calibration click track.
- `src/enchor.cpp`: Chorus Encore requests and responses, and `.sng` unpacking.
- `src/downloader.cpp`: the background download thread (curl; libnx sockets on Switch).
- `src/main.cpp`: screens, highway layout, menus, controller mapping, Switch libnx input.
- `src/look.cpp`: fonts, procedural textures, gems, fret buttons, fire, UI pieces.
- `include/guitar_input.hpp`, `integrations/missioncontrol/`: Wii guitar support.
- `tools/make_fonts.py`: bakes `assets/fonts/*.font` atlases from the bundled TTFs.
- `switch-hero --bake-gems assets/gems.bin` (desktop build): re-renders the
  jewel gem sprites the game loads at start-up. Rerun it after changing the gem
  renderer; the `gems` test checks the file still matches.
- `tools/build-switch.sh`: devkitPro CMake/NRO build.
- `tests/`: fixtures and parser, gameplay, audio, timing and download tests.
- `release/`: the ready-to-copy Switch build and install instructions.

## References and licenses

The implementation follows publicly documented formats. No Clone Hero or Guitar
Hero game code, graphics or music is included.

- Chart formats: <https://github.com/TheNathannator/GuitarGame_ChartFormats>
- `.sng` format: <https://github.com/mdsitton/SngFileFormat>
- Chorus Encore: <https://www.enchor.us/>. The download endpoints match its
  official client, [Bridge](https://github.com/Geomitron/Bridge). No Bridge code
  is used.
- Timing model inspired by [YARG](https://github.com/YARC-Official/YARG)
- SDL: <https://github.com/libsdl-org/SDL>
- libnx: <https://github.com/switchbrew/libnx>
- devkitPro portlibs/build definitions: <https://github.com/devkitPro/pacman-packages>
- libsndfile: <https://github.com/libsndfile/libsndfile>
- curl: <https://curl.se/> (curl license)
- nlohmann/json 3.11.3, vendored as `vendor/json.hpp`: MIT
- stb_image, vendored as `vendor/stb_image.h`: public domain / MIT

Project source: MIT. Dependencies keep their own licenses. Fonts are covered
by the licenses in `assets/fonts`.
