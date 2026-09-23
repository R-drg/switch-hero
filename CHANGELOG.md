# Changelog

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
