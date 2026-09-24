# Wii guitar Bluetooth for Switch

Switch Hero's Wii guitar mode requires the **patched MissionControl system module**
built here. An NRO alone cannot pair a Wii Remote or decode its extension through
normal libnx pad input. Stock MissionControl at the revision below does not decode
the guitar extension. WiitarThing itself runs on Windows, not on Switch.

## Build

With git and the devkitPro Switch toolchain installed:

```sh
bash tools/prepare-wii-guitar.sh
make -C build-missioncontrol -j4 dist
bash tools/build-switch.sh
```

The preparation script clones MissionControl at
`d3941d433f15827de8aea116d61ea17bb61d0bcc` (0.15.2 plus firmware 22.5.0 support),
checks out its pinned submodules, applies `wii-guitar.patch`, and installs the
shared `wii_guitar.hpp` decoder. It refuses to overwrite an existing directory.
Pass another destination as its first argument if needed. Build requirements and
firmware compatibility follow that revision's README: notably Atmosphere 1.11.2+
for firmware 22.5.0+. Do not substitute a different upstream revision without
reviewing and rebuilding the patch.

Output: `build-missioncontrol/dist/MissionControl-*.zip` and
`build-switch/switch-hero.nro`. The module is a replacement for MissionControl,
not a second module to install alongside it. Preserve your existing MissionControl
configuration when installing its `atmosphere` and `config` directories at the
SD root. Put the NRO in `sd:/switch/switch-hero/` and reboot Atmosphere.

## Pair and play

1. Connect a five-fret **Guitar Hero Wii guitar** to an official Wii Remote.
   Rock Band wireless guitars use their own USB receivers and are not supported
   by this extension decoder.
2. The module now keeps MotionPlus out of the way by itself: a guitar is never
   put behind MotionPlus passthrough, because that format cannot be decoded, so
   `enable_motion` can stay at its default. If a guitar still does nothing, set
   `enable_motion=false` under `[general]` in
   `sd:/config/MissionControl/missioncontrol.ini` and reboot as a fallback (that
   setting disables motion for every controller). Connect the guitar directly,
   without an external MotionPlus adapter.
3. Open Switch **Controllers → Change Grip/Order**, press the Wii Remote's red
   SYNC button, and wait for it to pair. Assign it to **player one**. Pairing/reconnection is handled by
   MissionControl; Switch Hero does not have a Bluetooth pairing screen.
4. Launch Switch Hero. With a normal controller, open Settings and cycle
   **Controller mode** to **WII GUITAR**, then save. With only the guitar,
   Wii Remote **1** opens Settings, its **A** cycles the mode, and its **Minus**
   saves. The guitar strum bar moves between settings.
5. Select a song and play. Guitar mode automatically enables strum judgement and
   uses fixed fret bindings without overwriting your custom gamepad bindings.
   During guitar gameplay, only player one is read. Menus also accept handheld
   Joy-Cons, so selecting guitar mode cannot disable navigation or saving.
   Use Minus to save settings even without a connected guitar.

| Guitar control | Playing | Menus |
|---|---|---|
| Green / red / yellow / blue / orange | Five frets | Green confirm, red back, yellow settings/restart |
| Strum up/down | Strum; no frets held hits open notes | Previous/next row or song |
| Stick left/right | No scoring action | Track selection / setting adjustment |
| Plus | Pause/resume | Resume when paused |
| Minus | Star power | No action |
| Whammy bar | Fills star power on star sustains | No action |
| Wii Remote A / 1 / Minus | A also strums; Minus pauses | Confirm / settings or restart / back |

Guitar mode only takes over player one while a controller is actually connected
there. With no guitar paired the game keeps normal controls, and Plus and Minus
always answer from the normal controller, so a silent guitar can never leave the
game without input. Holding Minus for two seconds returns to the song list and
resets the controller mode.

**Settings -> Controller test** shows live input: whether a player-one
controller is connected, its style, the raw buttons, and the decoded frets and
strum. If pressing the guitar changes nothing there, the guitar is not reaching
the game at all — check that the patched module above is installed, that
`enable_motion=false`, and that the remote is assigned to player one.

Transport mapping is green=L, red=ZL, yellow=R, blue=ZR, orange=B,
strum=D-pad up/down, stick=D-pad left/right, Plus=Plus, guitar Minus=X,
whammy=right stick X (centred at rest, full right when pushed down).
Only joystick horizontal movement is mapped, so moving the stick vertically
cannot accidentally strum. Tilt and the World Tour touch strip are not
implemented. The game pauses on a controller disconnect. Extension
changes clear MissionControl's cached state so removed frets do not remain held.

## MotionPlus and guitars

A Wii Remote with MotionPlus (including Wii Remote Plus units) otherwise routes
any non-nunchuk extension through MotionPlus *classic passthrough*, which
reports a layout this decoder cannot read: the remote's own buttons arrive and
the frets never do. The patch therefore initializes the extension before
reading its ID (an uninitialized guitar can report a garbled ID), skips
activating MotionPlus while a guitar is attached, and switches an already-active
passthrough back to the guitar's own six-byte format. Turning MotionPlus off
briefly reports the guitar as unplugged; the module re-checks instead of
reactivating MotionPlus, which previously left the frets dead. A classic controller behind MotionPlus is
re-identified once when it attaches, then continues to use passthrough as
before.

## Verification and limits

Verified locally: patched MissionControl cross-build and distribution ZIP,
Switch game cross-build, all four CTest suites, address/undefined-behavior
sanitizers for the decoder, and the SDL guitar-mode gameplay smoke test.
Physical Switch/Wii guitar pairing and play have **not** been tested.


`guitar_tests` covers all 32 fret combinations, both strum directions,
Plus/Minus, model flag masking, stick thresholds, and truncated/absent data.
The same decoder header is compiled into the patched module.

```sh
ctest --test-dir build-desktop-linux --output-on-failure
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build-desktop-linux/switch-hero --songs ./songs --smoke-wii-guitar
```

The virtual-controller smoke check uses the same normalized button mapping and
strum judgement as Switch. Neither it nor a successful cross-build proves radio
pairing, latency, or physical guitar compatibility. Hardware testing must check
pair/reconnect, all five frets together, alternating strums, open notes, menu
shortcuts, star power, pause/resume, and guitar unplug/replug. Wii Remote battery
removal should pause gameplay. Avoid combining other active controllers into
player one's input while testing.

## Sources and licenses

- [WiitarThing](https://github.com/Meowmaritus/WiitarThing), revision
  `1f2118bcb46f317e31b33ed1893f1ecb58b01220`, especially
  `Nintroller/WiiGuitar.cs` and `Nintroller/Enumerators.cs`: extension ID,
  active-low fret/strum masks and six-bit joystick fields. See
  `WiitarThing-LICENSE` (MIT, copyright 2018 Meowmaritus).
- [MissionControl](https://github.com/ndeadly/MissionControl), pinned revision
  above: Switch Bluetooth pairing, extension initialization/report selection,
  and virtual Pro Controller transport. `wii-guitar.patch` modifies GPL-2.0-or-later
  source and is distributed under those terms; see `MissionControl-LICENSE`.
  It is built separately and is not linked into the MIT-licensed game.
- `wii_guitar.hpp` and the game integration use this project's MIT license.

When redistributing the modified module, provide its corresponding source,
including the pinned upstream/submodules, patch, and decoder, under the applicable
licenses. No upstream binaries are bundled in the tracked repository.
