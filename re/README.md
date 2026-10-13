# Reverse engineering

Tools used to take PART2.EXE (Watcom C, DOS/4GW LE executable) apart and to
check the port against it. They expect the original game files in the
repository root (`../part2.exe`, `../data.pak`); outputs go to the ignored
folders `build/`, `out/`, `assets/` and `ghidra_proj/`.

Python 3 with `pip install unicorn capstone pillow` (a `.venv` here is what
the scripts call); Ghidra 11+ with Java 21 for the decompile.

## Decompile

    re/decompile.sh            # JAVA_HOME / GHIDRA_HOME override the Homebrew defaults

1. `tools/le_flatten.py` turns the LE executable into a relocated flat image
   (`build/part2.bin`) plus its layout and relocation targets.
2. Ghidra imports it headless and runs, in order, `ghidra_scripts/`
   `LeSetup.java` (memory blocks, Watcom register calling convention, entry
   and relocation seeds), `FillGaps.java` (functions analysis missed),
   `FixParams.java` (recovers register parameters by a bottom-up fixpoint)
   and `ExportDecomp.java` (C for every function into `out/`).
3. `ghidra_scripts/ExportStrRefs.java` is run by hand when needed: every
   string with the functions that use it.

`tools/disasm.py START END` disassembles part of the flat image (capstone)
where the decompiler output is unclear.

## Data and generated tables

- `tools/pak_extract.py`: unpacks DATA.PAK (the table of contents lives in
  the exe); `tools/tf_gfx.py` decodes the graphics-set format.
- `tools/tf_state.py`: the game-state block, saves (with checksum) and
  SETTINGS.CFG; `tools/state_fields.py` maps which code uses each field.
- `tools/gen_tables.py`, `tools/gen_objects.py`, `tools/gen_missions.py`
  write `port/src/*_data.c` from the exe. Regenerate rather than edit those.

## Verifying the port

`tools/trace_original.py DEMO STEPS OUT.csv` runs the original code under
the Unicorn emulator with DOS, VGA and sound calls replaced, and writes one
row of game state per frame; `port/build/terrafire --trace` writes the same
from the port and `tools/trace_compare.py` reports the first difference.

- `traces/`: 31 reference traces from the original: the four demos, and
  demo input replayed with invulnerability on every mission.
- `tools/regress.sh [binary]`: checks the port against all of them.
- `tools/gen_refs.sh MISSION...`: makes new reference traces.
