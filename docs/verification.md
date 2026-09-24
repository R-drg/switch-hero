# Verification — 2026-09-24 (0.3)

Desktop (Linux, in a Debian bookworm container with SDL 2.26) Release build,
and the devkitPro `devkita64:latest` cross-build of the NRO (no warnings).
Every commit of the 0.3 series was built for the Switch on its own.

**Automated tests** (`ctest`, 8 suites)
- `core_tests`: 155 checks. New this release: the translation table and
  placeholder filling; section and solo parsing from `.chart` and MIDI
  (including Rock Band `prc_` names and note 103 beside 116), readable section
  names and the eight-measure fallback; solo bonuses; `Session::startAt` for
  practice (no misses, no fail, phrases and solos cut short); `sectionStats`;
  whammy (only movement on an intact star sustain pays); the song-list cache
  (signatures, reuse, rescans, round trip, damaged files).
- `enchor_tests`: 33 checks, including per-part ratings, note counts, peak
  NPS, feature flags and cover URLs from real Chorus Encore responses.
- `audio_tests`: five codecs with seeks, previews, and practice's `loadAt` and
  `rewind` clocks.
- `timing_tests` (1,213 checks), `guitar_tests` (including the whammy bar),
  `multiplayer_tests` (four isolated sessions over one chart, standings), and
  `gems` (the shipped `assets/gems.bin` matches the renderer).
- Smoke: `--smoke` and `--smoke-wii-guitar` hit notes through the SDL virtual
  controller.

**Targeted checks with temporary hooks (removed afterwards)**
- Draw calls per frame, counted by wrapping every SDL render call: four
  players 554-786 before batching, 140-146 after; single player 204-262 before,
  46-49 after. Per-player highways and palettes add almost nothing.
- Start-up: a 1,500-song library went from 55 s of scanning to 0.2 s in the
  software renderer once the scan moved off the render thread; with the cache
  the title menu's first frame is about 0.1 s after launch. File system calls,
  counted with `strace` on 165 typical song folders: scan 2,499 -> 841, and a
  chart load about 297 -> 11.
- Gem sprites load from `assets/gems.bin` in about 5 ms; `look::init`'s
  textures fell from 0.14 s to 0.08 s with three painting cores.
- Screenshots of every new screen (language picker, Customize with preview,
  13 highways, palettes, download panel and queue tags, practice sections,
  lobby rows, two- and four-player boards, hit ratings option) were reviewed in
  English and Portuguese.

**Hardware**
- Split-screen multiplayer was played start to finish on a docked console
  with two and four players (by the contributor). Everything else in 0.3 is
  unverified on a console; HANDOFF.md lists what to check first.
