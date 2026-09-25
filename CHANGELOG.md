# Changelog
## Unreleased

### Play in a browser
- A web build made with Emscripten (`tools/build-web.sh`): the same game,
  menus and look, playable with a keyboard or gamepads in Chrome, Edge or
  Firefox. `.github/workflows/web.yml` publishes it to GitHub Pages.
- **Add songs** on the page copies song folders into the browser's storage;
  they join the song list without a restart. Songs, settings and scores
  persist per browser.
- WAV, OGG Vorbis, Opus and MP3 stems all play. Opus and opusfile are built
  from source, since Emscripten has no port for them.
- The Download screen isn't available in the browser and says to use Add
  songs instead.
- Tested in headless Chromium with software WebGL: menus, song import, all four
  codecs, persistence across reloads, and quitting. Not yet played with a
  gamepad or checked for audio sync on real hardware.

## 0.3

The 0.3 release adds split-screen multiplayer, practice mode, solos, a
Portuguese translation and a customization menu with 13 highways and 24 note
palettes, and makes start-up, the song list and split screen much faster.
Everything below passes the desktop tests and the Switch cross-build. The
items marked *(hardware)* still need a run on a console.

### Split-screen multiplayer
- Two to four players on one console, each on their own board, all playing the
  same song. Two players get side-by-side halves; three or four get quadrants,
  with the fourth left empty when three are playing. Contributed by RonyAbreu.
- **Docked only.** The Multiplayer entry refuses to open in handheld mode and
  says why before it refuses, and undocking mid-song pauses rather than playing
  on for three players who can no longer see a board.
- A lobby where every seat is driven by its own controller at once: A joins, B
  leaves, up/down (or the strum bar) picks a row and left/right changes it, and
  Plus starts. The rows are **part, difficulty, highway and note colours**, so
  every player plays on their own board style, and the choices are kept between
  songs. Difficulty stepping skips tiers the chart does not carry. Each player
  gets the hit window their own difficulty earns, so an expert and a beginner
  can share a song.
- Songs count in 3-2-1 before the first note and again after a pause.
- Pause is a menu: resume, restart, quit to song list.
- Every board calls its own timing and shouts its own streak milestones and
  star power. The dropped-note screech keeps one shared rate limit.
- The song is named once for the room across the top of the screen.
- Results are a comparative table: rank, score, accuracy, best streak and full
  combo per player, with ties sharing a rank. A replays the same line-up.
- Multiplayer forces no-fail, never ducks the (shared) guitar stem and never
  writes a high score; `scores.cfg` has no player dimension.
- The Switch input thread samples four pads, each with its own press
  timestamp, so one player's press time is never judged against another's.
  Player one keeps the default pad and the Wii guitar. Audio and video offsets
  stay shared: they describe the console and the television.
- `look::setViewport` places each pane on top of the logical-size letterbox;
  `highway()` takes its board shape, so split screen gets a wider, taller board.

### Practice, solos and sections
- **Practice mode** (title menu -> Practice): pick a song, part and
  difficulty, then the section the loop starts at and the one it ends at. The
  loop plays with a count-in, restarts instantly (the open audio streams are
  seeked rather than reopened), and shows each loop's accuracy. Practice
  cannot fail and records no scores. Charts without named sections are split
  into eight-measure parts.
- **Sections** are read from `.chart` `[Events]` and MIDI EVENTS text,
  including Rock Band `prc_` names. The results screen has a **Sections**
  view (Y) with an accuracy bar per section and the weakest one marked.
- **Solos** are read from `E solo`/`soloend` and MIDI note 103 (when 116 carries
  star power). A running solo shows a live counter and turns the rails blue,
  and its end calls out "SOLO 94%" or "PERFECT SOLO!" with a bonus of 100 per
  note hit.

### Customization
- Options -> Customize, with a live preview that plays a short chart by itself.
  - **13 highways**: Grip tape (the original), Rosewood (nickel frets,
    strings, pearl inlays, bone binding), Synthwave (neon grid, striped sun),
    Pastel dream, Hellfire (lava veins, rising embers), Diamond plate
    (hazard-stripe rails), Carbon fiber, Thunderstorm (lightning), Toxic
    waste, Zebra, Checkerboard, Nebula and Frostbite.
  - **24 note colour palettes**, which need not follow the fret colours:
    Classic, Pastel, Neon and Colorblind; one colour on every fret (Blood,
    Bone, Obsidian, Gold record, Platinum, Radioactive, Ghost); two colours
    alternating (Bumblebee, Candy cane, Vaporwave, Royalty, Cyberpunk); and
    gradients and sets (Inferno, Glacier, Sunset, Deep sea, Rasta, Arcade,
    Camo, Sakura). Star power stays blue.

### Language
- Portuguese (Brazilian) translation. The first start asks for English or
  Português, preselecting the console's language, and Options -> Language
  changes it. Fonts now carry Latin-1, so accented letters (and accented chart
  titles) draw as themselves.

### Gameplay
- **Whammy.** Moving the Wii guitar's whammy bar, or either stick on a
  controller (W on a keyboard), while holding a star-phrase sustain fills star
  power and stretches an active one. Only movement counts. The audio is not
  bent. The MissionControl patch sends the whammy bar as right stick X.
- **Rumble** *(hardware)*: a buzz on a missed note, a pulse when star power
  kicks in and a tap after a solo. Options -> Gameplay -> Rumble.
- **Hit ratings** can be turned off: Options -> Gameplay -> Hit ratings hides
  the PERFECT/GREAT/GOOD pop-up and its early/late marker.
- Holding a direction (or the strum bar) in a menu repeats the move, speeding
  up the longer it is held.

### Downloads
- The highlighted chart has a detail panel: cover art, album, year, genre,
  length, charter, every five-fret part with its intensity and note counts per
  difficulty, the guitar's peak notes per second, solos/open notes/taps, and
  parts the game cannot play.
- **A download queue.** A queues or unqueues a chart, X cancels everything, and
  B leaves while the queue keeps downloading; finished songs join the list in
  the background.

### Performance and loading *(hardware)*
- Draws are batched: consecutive draws with the same texture go out as one
  call. Measured on desktop, four players went from 554-786 draw calls a frame
  to 140-146 and single player from 204-262 to 46-49.
- Background work (chart loads, covers, preview stems, downloads, scans) runs
  on the spare cores at a lower priority, instead of waiting on the render
  core; the input thread and mixer run above the main thread.
- The jewel gems ship pre-rendered in `assets/gems.bin`; rendering them at
  every launch cost several seconds of both spare cores and could freeze the
  menu's input. A `gems` test keeps the file in step with the renderer.
- The song list is cached in `library.cache`, so the title menu opens with the
  last library at once and the card is checked in the background. Each song
  folder is listed once: on 165 folders the scan went from 2,499 file system
  calls to 841, and a chart load from about 297 calls to 11.
- Start-up textures paint on three cores; highway surfaces are painted when
  first shown; Film grain (Options -> Audio / video sync) can be turned off.

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
