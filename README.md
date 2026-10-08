# TerraFire port

A source port of **TerraFire** (Owen Thomas, 1997, DOS) to modern systems
(macOS, Windows, Linux) with SDL3, rebuilt from the original PART2.EXE.

- `port/` — the port (C11 + SDL3, CMake). See [port/README.md](port/README.md)
  for building, playing and the test tools.
- `re/` — reverse-engineering tools: EXE flattening and Ghidra scripts,
  DATA.PAK extraction, table generators, and an emulator harness
  (`re/tools/trace_original.py`) that runs the original code to produce
  reference traces (`re/traces/`); `re/tools/regress.sh` checks the port
  against them frame by frame.

The game's data is not included: you need an original TerraFire
installation (DATA.PAK; the generators also read PART2.EXE).
