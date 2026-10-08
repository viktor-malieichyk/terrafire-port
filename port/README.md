# TerraFire port

Native macOS/Windows port of TerraFire (DOS, Owen Thomas) on SDL3. Needs the
original `DATA.PAK` (6,582,958 bytes); the original executables are not used.

## Build

macOS (Homebrew):

    brew install cmake ninja sdl3
    cmake -S . -B build -G Ninja
    cmake --build build

Windows (Visual Studio 2022 or newer, from a developer prompt):

    cmake -S . -B build
    cmake --build build --config Release

If SDL3 isn't installed, CMake downloads and builds it automatically
(`-DTF_VENDOR_SDL=ON` forces that).

## Run

    build/terrafire [directory containing DATA.PAK]

`--viewer` opens the asset viewer: Left/Right change entry, Up/Down change
frame, Shift+arrows scroll, Enter toggles tile sets / sprite banks, P cycles
palettes, Space plays the next sound, Esc quits.

Windows:

    cmake -S . -B build                    # Visual Studio 2022 (SDL3 is fetched and built)
    cmake --build build --config Release   # -> build/Release/terrafire.exe + SDL3.dll

Or cross-compile from macOS / Linux with MinGW-w64 (`brew install mingw-w64`):

    tools/package-windows.sh               # -> dist/terrafire-windows.zip

Put terrafire.exe and SDL3.dll in the game folder (with DATA.PAK) and run it;
settings and saves stay in that folder. The Windows build is a GUI program
(no console); configure with -DTF_WINDOWS_CONSOLE=ON to see its log output.
`.github/workflows/build.yml` builds Windows (MSVC and MinGW) and macOS on
GitHub Actions.

Play:

    build/terrafire ..                               # the game: splash, title, main menu
    build/terrafire --play ..                        # straight into a new game, difficulty 1, mission 1
    build/terrafire --play 0 5 ..                    # difficulty 0-3, start at mission 1-27

Controls come from SETTINGS.CFG (default: Left/Right turn, Up thrust, Space
fire), P pauses, Esc opens the in-game menu. Options are saved to
SETTINGS.CFG. Sound effects play if enabled in SETTINGS.CFG. Gamepads work as the original's
joystick: in a mission the stick (or D-pad) turns and thrusts and A/X fires;
in menus the stick moves and A/X selects; B or Start is Esc. Plugging one in
turns Options > Control > JOYSTICK on.
Saved games are GAME0001.SAV-GAME0005.SAV next to SETTINGS.CFG, in the original's format (the
DOS game loads them too). Testing hooks: TF_KEYS="frame:code,frame:-code,..." injects key
make/break events, TF_SHOT_PRESENT=n:file.bmp saves the n-th shown frame,
TF_SAVE_TICK=n acts as if a save point were collected at game tick n,
TF_KILL_TICK=n loses the last life at tick n, TF_CHEATS=1 enables F5-F8,
TF_PAD="from-to:x,y,fire,back;..." simulates a gamepad (frames count calls
to the frame presenter, as for TF_KEYS and TF_SHOT_PRESENT).

Checks:

    build/terrafire --selftest ..                    # decode every pak entry
    build/terrafire --demo-check ..                  # replay demo input timelines
    build/terrafire --attract 300 ..                 # attract mode, stop after 300 ticks
    build/terrafire --load-mission 0 out.bmp ..      # load a mission's resources and check them
    build/terrafire --object-check ..                # object list, movement and callback rules
    build/terrafire --render-test 0 out.bmp ..       # draw a test scene with the renderer
    build/terrafire --first-frame 1 out.bmp ..       # a demo's real start-up and first frame
    build/terrafire --view-object 3 74 out.bmp ..    # after start-up, centre the view on an object
    build/terrafire --demo-run 3 3000 out.bmp ..     # play a demo for N ticks, report input/heading
    build/terrafire --trace 3 600 port.csv ..        # per-step state trace (see below)
    build/terrafire --screenshot 30 0 out.bmp ..     # render one tile-set frame or sprite to BMP

## Checking against the original

`re/tools/trace_original.py` runs the original PART2.EXE code under the
Unicorn CPU emulator (Python: `pip install unicorn`) and writes the same
per-step trace as `--trace`, so the two can be compared:

    ../re/.venv/bin/python ../re/tools/trace_original.py 3 600 orig.csv ..
    build/terrafire --trace 3 600 port.csv ..
    python3 ../re/tools/trace_compare.py orig.csv port.csv

Reference traces for all four demos, and demo 0's input replayed on every
bonus mission, are kept in `re/traces/`; check the port against all of them
with (from the repository root):

    re/tools/regress.sh

`TF_DEMO_MISSION=n` (both tools) replays a demo's input on mission n, and
`TF_INVULNERABLE=1` sets the original's no-collision flag, to test missions
that have no demo.

Only DOS-facing routines are replaced in the emulated run (file I/O,
allocation, clock, vsync, sound, text output); the game logic runs as
compiled. The comparison reports the first frame and field that differ.

`--attract` runs the ported demo/session/mission start-up code and logs each
original routine it reaches that isn't ported yet (`unported.c`).

## Layout

- `src/platform.h`, `src/platform_sdl.c`: replaces the DOS hardware: VGA
  framebuffer and palette, int 9 keyboard (XT scancodes), timer, sound,
  gamepads.
- `src/pak.c`: DATA.PAK access (table of contents taken from PART2.EXE).
- `src/assets.c`: palette, DWD sound, tile set and sprite bank decoders.
- `src/viewer.c`: asset viewer (`--viewer`).
- `src/kbd.c`, `src/timing.c`, `src/palette.c`: the original's keyboard
  latches, frame pacing and palette fades; `src/joystick.c` turns a gamepad
  into key presses as the original's joystick code did.
- `src/demo.c`, `src/session.c`: demo input timeline, attract loop, new game,
  session loop, mission start and end. `src/game.h` holds the game state;
  `src/state.c` converts it to and from the original's state block and
  reads/writes SETTINGS.CFG.
- `src/res.c`: the engine's resource tables (frame slots, tile chunks, sprite
  banks with 8 orientation views) and their garbage collector.
- `src/levels.c`: level, bonus-level and HUD loaders, background tile animation.
- `src/objects.c`: the 652-object table, z-sorted active list, animations,
  callback timers and per-frame update.
- `src/render.c`: off-screen buffer, camera and view, occlusion grid, tile and
  sprite drawing (orientations, blending), point/fill/line, the beam effect.
- `src/frame.c`: per-frame routines for each screen mode, starfield, world
  wrap-around.
- `src/spawn.c`: level object setup from the placement map (frame slot 3) and
  all the spawners.
- `src/collision.c`: pixel collision against sprites and tile collision shapes.
- `src/player.c`: player input (the exact 15-query order demos depend on),
  rotation, movement, the per-frame player callback and the request handler.
- `src/hits.c`, `src/weapons.c`, `src/effects.c`, `src/pickups.c`: collisions
  and damage, firing and bullets, explosions and debris, pods and pickups.
- `src/tow.c`: towing physics (the original's x87 code) and the self-destruct.
- `src/enemies.c`, `src/bonus.c`: normal-mission enemies, bosses and level
  machinery; the bonus levels.
- `src/hud.c`, `src/sound.c`: status bar, text and messages; sound effects.
- `src/menu.c`, `src/save.c`, `src/screens.c`: menus and info pages, saved
  games (the original's format), briefing / mission complete / game over /
  high scores / ending / splash / title.
- Generated from PART2.EXE: `src/tables_data.c` (`re/tools/gen_tables.py`:
  trig and random tables, weapon banks, sound table, message texts, key
  names), `src/anims_data.c`, `src/callbacks_data.c`
  (`re/tools/gen_objects.py`) and `src/missions_data.c`
  (`re/tools/gen_missions.py`).

Reverse-engineering notes and tools are in `../re/`.
