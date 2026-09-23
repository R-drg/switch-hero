# Changelog
## Unreleased

### Split-screen multiplayer
- Two to four players on one console, each on their own board, all playing the
  same song. Two players get side-by-side halves; three or four get quadrants,
  with the fourth left empty when three are playing.
- **Docked only.** The Multiplayer entry refuses to open in handheld mode and
  says why before it refuses, and undocking mid-song pauses rather than playing
  on for three players who can no longer see a board.
- A lobby where every seat is driven by its own controller at once: A joins, B
  leaves, left/right picks the instrument, up/down the difficulty, Plus starts.
  Difficulty stepping skips tiers the chart does not carry, so a seat can never
  sit on an empty one. Each player gets the hit window their own difficulty
  earns, so an expert and a beginner can share a song.
- Songs count in 3-2-1 before the first note and again after a pause, with the
  lead-in drumstick clicks, rather than dropping four players straight into
  notes.
- Pause is a menu, as in single player: resume, restart, quit to song list.
  There is no change-difficulty row, since up to four difficulties are in play
  and no one of them to change.
- Every board calls its own timing - GOOD, GREAT, PERFECT with the early/late
  marker - and shouts its own streak milestones and star power. A streak
  belongs to whoever earned it and says nothing to the other three. The
  dropped-note screech keeps one shared rate limit so four players fumbling at
  once cannot machine-gun it.
- The song is named once for the room: title, artist, elapsed time and a
  progress bar across the top of the screen rather than repeated on every
  board.
- Results are a comparative table: rank, score, accuracy, best streak and full
  combo per player, with ties sharing a rank. A replays the same line-up.
- Multiplayer forces no-fail. Dropping a failed player would leave a dead
  quarter of the screen for the rest of the song; the rock meter still moves
  and still reads red, it just stops being an eject button.
- The guitar stem is never ducked in multiplayer. It is one shared audio
  stream, so muting it because one of four players is in the red would punish
  the other three for that player's mistake.
- Multiplayer never writes a high score. `scores.cfg` is keyed by song, part
  and difficulty with no room for a player, and four runs would fight over one
  record, so the standings are shown instead. The file format is unchanged.

### Input
- The Switch input thread samples four pads instead of one, each with its own
  press timestamp on the `now()` clock. A single shared stamp would hand one
  player's press time to another player's judgement. Player one keeps the
  default pad - handheld as well as No1 - and the Wii guitar, so single player
  is unchanged.
- Audio and video offsets stay shared across seats on purpose: they describe
  the console and the television, not a controller, so player one's calibration
  is the right answer for all four.
- The desktop backend opens up to four controllers, so split screen can be
  developed and played without a console.

### Rendering
- `look::setViewport` places a pane by composing a scale and viewport on top of
  the logical-size letterbox, re-derived on every call so it survives the
  console changing output resolution on its way into the dock. Draw code still
  works in 1280x720 units and needs to know nothing about the split.
- `highway()` now takes its board geometry. Single player keeps the proportions
  it had; split screen passes a wider, taller board, because the single-player
  margins that make room for a full-width HUD left the board a narrow ribbon in
  a half or quarter pane. The fret buttons scale with it.

### Performance
- **Options -> Audio / video sync -> Film grain** turns off the grain loop in
  `look::grade()` on its own. It is roughly fifteen tiled fullscreen blits per
  frame and the cheapest third of the pass to lose, and split screen multiplies
  everything else on screen, so it is the first thing to reach for if frames
  drop.

### Everything else

- Customization: Options -> Customize, with a live preview that plays a short
  chart by itself as you change settings. The number beside each option shows
  where you are in the list.
  - **13 highways**: Grip tape (the original), Rosewood (a real fretboard:
    nickel frets, strings, pearl inlays, bone binding), Synthwave (neon grid,
    striped sun), Pastel dream, Hellfire (charred rock split by lava veins,
    embers rising), Diamond plate (tread-plate steel with hazard-stripe
    rails), Carbon fiber (twill weave, red racing lines), Thunderstorm (storm
    clouds with lightning striking down the board), Toxic waste (glowing
    sludge and hazard rails), Zebra, Checkerboard (scuffed punk-stage
    checkers), Nebula (gas clouds and stars) and Frostbite (cracked ice).
    Surfaces are painted the first time they're shown, so start-up doesn't
    pay for thirteen.
  - **24 note color palettes**. They don't have to follow the fret colors:
    - Four-color sets: Classic, Pastel, Neon and Colorblind.
    - One color on every fret: Blood, Bone, Obsidian, Gold record, Platinum,
      Radioactive and Ghost.
    - Two colors alternating: Bumblebee, Candy cane, Vaporwave, Royalty and
      Cyberpunk.
    - Gradients and sets: Inferno, Glacier, Sunset, Deep sea, Rasta, Arcade,
      Camo and Sakura.
    Each palette recolors the gems, fret buttons, sustains, flames and lane
    labels (dark ones are lightened so the labels stay readable). Star power
    stays blue.
  Both are saved with the settings. Reset all options returns them to Grip
  tape and Classic.
- Portuguese (Brazilian) translation. The first start asks for a language
  (English or Português), preselecting the console's own language. It can be
  changed any time under Options -> Language.
- The download screen shows the highlighted chart in a detail panel: cover
  art, album, year, genre, length and charter, then every five-fret part with
  its intensity and note count per difficulty. Below that, the guitar's peak
  notes per second, whether it has solos, open notes or taps, and any parts
  the game can't play (drums, vocals). Covers load on their own thread once
  the cursor rests, so scrolling and downloads never wait on them.
- Chart details, covers and previews on the song list load much faster on the
  console *(hardware)*. Background work used to start on core 0 beside the
  renderer, and Horizon doesn't time-slice equal-priority threads, so it
  only ran while a frame waited on vsync. Loaders, cover decoding, preview
  stems, gem renders and downloads now run on the spare cores at a lower
  priority. The chart and its cover also load in parallel.
- Whammy. Moving the Wii guitar's whammy bar, or either stick on a
  controller (W on a keyboard), while holding a sustain from an intact star
  phrase fills star power, about a bar every 30 beats, and extends an active
  star power. Only movement counts, so a resting bar or a drifting stick
  earns nothing. The audio isn't bent; the sustain wobbles, turns blue while
  it charges, and the star power meter glows. The MissionControl patch now
  sends the whammy bar as right stick X.
- No more freeze after the start-up scan *(hardware)*. The game used to load
  the selected chart right after the scan and wait on it. Its cover decode
  could sit behind the start-up gem renders at the same priority, which
  Horizon never switches between. The title menu now opens at once and the
  chart loads in the background. Gem renders run below every loader, so a
  load always goes first.
- The jewel gems ship pre-rendered in `assets/gems.bin` (1.6 MB) *(hardware)*.
  Every launch used to ray-march them on four threads. That's 0.6 CPU-seconds
  on a desktop and several seconds of both spare cores on the console, so the
  first scan, the background library check and chart loads crawled behind
  it. It could also hold off the input thread, which froze the main menu. The
  file now loads in about 5 ms. A `gems` test fails if it stops matching the
  renderer.
- The input thread and audio mixer run one step above the main thread, so
  background work sharing their cores can never starve them.
- Start-up textures are painted on three cores at once (0.14 s to 0.08 s on
  a desktop).
- Start-up no longer waits for a scan *(hardware)*. The song list is cached
  in `library.cache` next to the settings, so the title menu opens with the
  last known library right away. The SD card is checked behind the menus, and
  added or removed songs appear once the check finishes. Only the first start
  (or a lost cache) shows the scan. A folder whose file names haven't changed
  keeps its cached title; one exception: an edit to `song.ini` alone isn't
  picked up.
- Each song folder is listed once instead of over and over. The scan used to
  list a folder four or five times per song, and loading a chart tried every
  stem name and extension with a fresh listing each time. Measured on
  165 typical song folders: the scan went from 2,499 file system calls to 841,
  and loading every chart from about 297 calls per song to 11. On the console
  each call is a round trip to the SD card.
- The start-up scan runs on a spare core while the main thread only draws its
  progress. It used to stop on every loading-screen redraw; in the software
  renderer that turned 0.1 s of scanning 1,500 songs into 55 s.
- Holding up/down (or the strum bar) or left/right in a menu repeats the move:
  first after 0.35 s, then every 90 ms, speeding up to 45 ms after 1.6 s held.
- The fonts now include the Latin-1 range, so accented letters draw as
  themselves instead of being folded to plain ASCII. Chart titles benefit too.

## 0.2

The 0.2 release reworks the menus around a Guitar Hero-style flow, replaces
the gems with rendered jewels, and adds high scores, song previews and a lot of
smoothing. Everything below passes the desktop tests and the Switch cross-build.
The items marked *(hardware)* still need a run on a console.

### Menus and flow
- A title menu: Quickplay, Download songs, Options, Quit.
- Picking a song asks for the instrument (skipped when there's only one), then
  the difficulty. All four difficulties are listed, and the ones the chart
  lacks are greyed out. The game remembers the last part and difficulty.
- Options are split into Gameplay, Audio, Audio / video sync and Controls
  pages. Every row explains itself, adjustable rows show `< >`, and
  calibration has its own rows.
- Pause is a menu: resume, restart, change difficulty, quit. Resuming counts
  3-2-1 over the frozen highway.
- Results offer continue, retry and change difficulty.
- B backs out of every menu, and Minus still works everywhere. B is ignored
  where it's a fret or being tested: during play, the count-in, calibration
  taps, fret rebinding, the controller test and Wii guitar mode.
- Wii guitar mode shows coloured fret buttons in the hints instead of labels.

### Look
- Gems are cut jewels in a gunmetal bezel, ray-marched in 3D once at startup
  on background threads, with soft shadows, ambient occlusion and reflections.
  Star notes are cut stars. Hammer-ons light the flat top white, which reads
  at play size where the old small mark didn't.
- Gems flatten slightly toward the far end of the highway.
- Hits throw orange fire with a shockwave, spark streaks and embers. Perfect
  hits burn bigger with a star glint, and star power burns blue-white. The
  fret buttons flash on a hit, and the strike line pulses with the beat.
- Missing a note of a star phrase turns the rest of it into regular gems.
- Lefty flip mirrors the highway.

### Gameplay and timing
- Hit windows scale with difficulty (+-85 ms on Expert to +-110 ms on Easy),
  and Options -> Gameplay -> Hit window adds Strict and Lenient. The
  perfect/great/good grades scale with the window.
- If a song's hits were consistently early or late, results offer **Fix
  timing**, which moves the audio offset by the median error.
- High scores: best score, stars and full combo per song, part and difficulty
  (`scores.cfg`), shown on the song list, the difficulty screen and results.
- Music and sound-effect volume.
- A timing overlay (Options -> Audio / video sync) shows average and worst
  frame times and audio clock drift.

### Library
- Song previews fade in on the song list from `preview_start_time`, then loop.
- Sort by title, artist or best stars (ZR), and jump between letters with
  left/right. The list opens on the last song played.
- Delete songs from the song list (X), with a confirmation that starts on
  "Keep it" and a guard that only ever deletes inside the songs folder.

### Performance
- Charts, covers and preview audio load on background threads once the cursor
  rests. The main-thread cost of a cursor step fell from 8.2 ms to 0.003 ms,
  and starting a preview from 2-8 ms to under 0.01 ms (desktop measurements).
- A cover texture is no longer destroyed mid-frame, which had stalled the
  renderer for a frame on every step.
- Outlined text draws in one pass instead of up to fourteen, and the highway no
  longer allocates memory every frame.
- On the Switch, buttons are sampled on their own thread about once a
  millisecond, so presses are judged when they arrived, not rounded to the
  frame *(hardware)*.
- MP3s with a Xing/Info header open without reading the whole file
  *(hardware)*.

### Fixes
- MP3s decoded as half-length noise on the Switch: float output was requested
  after libmpg123 had already fixed 16-bit output *(hardware; confirmed with
  desktop libmpg123)*.
- A B press as a song ends or fails no longer skips the results screen; it
  ignores input for its first second.
- Rescanning the library (after a download) no longer leaves it unsorted.
- A damaged `settings.cfg` can no longer trigger undefined integer conversion,
  and `last_song` survives Windows line endings.
- In Wii guitar mode, the guitar's Minus (which arrives as X) no longer opens
  the delete dialog or toggles no-fail.
- Reset all options keeps the remembered song and sort order.
- Success messages show in green rather than error red.

## 0.1

First release: a five-fret highway with Clone Hero song folders, press-to-hit,
strum and Wii guitar modes, star power, rock meter, offset calibration, chart
downloads from Chorus Encore, and the game's own 2000s rock look.
