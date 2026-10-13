TerraFire - source port of the 1997 DOS game by Owen Thomas
https://github.com/viktor-malieichyk/terrafire-port

You need the original game: this package contains only the port, not the
game's data. Copy the program into the folder of your TerraFire installation
(the one containing DATA.PAK) and run it there.

  Windows: copy terrafire.exe and SDL3.dll, run terrafire.exe.
  macOS:   copy TerraFire.app. It is not signed, so the first time
           right-click it and choose Open (or run
           xattr -dr com.apple.quarantine TerraFire.app).

Settings (SETTINGS.CFG) and saved games (GAME0001.SAV ...) are kept in that
folder and are compatible with the DOS version. Display options are in
PORT.CFG.

Controls: Left/Right rotate, Up thrust, Space fire (change them in
Options > Control > Set Keys), P pause, Esc menu, F1 instructions.
Gamepads: stick or D-pad to steer and thrust, A to fire, B or Start for the
menu. Options > Graphics has FULLSCREEN and SCALING (SHARP keeps the
original 4:3 picture crisp; INTEGER uses whole-pixel sizes; FILL stretches).

The port's code is MIT licensed (see LICENSE in the repository). TerraFire
and its data belong to their original rights holders.
