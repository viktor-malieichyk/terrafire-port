# TerraFire port

A source port of **TerraFire** (Owen Thomas, 1997, DOS) to macOS and Windows
with SDL3, rebuilt from the original PART2.EXE. The whole game is ported and
checked frame by frame against the original code run in an emulator, so
physics, enemies and demos behave exactly as in the DOS version.

Additions over the original: gamepads, fullscreen, crisp 4:3 scaling on
modern displays (SHARP / INTEGER / FILL), Space as the default fire key, a
DEFAULT KEYS button, and the help page showing your actual key bindings.
Saves and settings stay compatible with the DOS game.

## Playing

You need the original game: this repository and its releases contain only
the port, not TerraFire's data. Download the build for your system from
[Releases](../../releases), put it in your TerraFire folder (the one
containing DATA.PAK) and run it. See the README.txt inside for controls. On
macOS the app is not notarized: the first time, allow it in System Settings >
Privacy & Security > Open Anyway.

## Repository

- `port/`: the port (C11 + SDL3, CMake). See [port/README.md](port/README.md)
  for building, options and test tools.
- `re/`: reverse-engineering tools: executable flattening and Ghidra scripts,
  DATA.PAK extraction, table generators, and an emulator harness that runs
  the original code to produce the reference traces in `re/traces/`. See
  [re/README.md](re/README.md).

It should also build on Linux (SDL3 is portable), but that is untested.

## License

The port's code and tools are MIT licensed ([LICENSE](LICENSE)). TerraFire
itself, its data, and the text, tables and icon extracted from it remain the
property of their original rights holders. This project is not affiliated
with them.
