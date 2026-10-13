#!/bin/sh
# Rebuild the Ghidra project and decompiled output from ../part2.exe.
# Defaults are Homebrew's openjdk@21 and ghidra; override with JAVA_HOME and
# GHIDRA_HOME (the folder containing support/analyzeHeadless).
set -e
cd "$(dirname "$0")"
export JAVA_HOME="${JAVA_HOME:-/opt/homebrew/opt/openjdk@21}"
export PATH="$JAVA_HOME/bin:$PATH"
GHIDRA_HOME="${GHIDRA_HOME:-/opt/homebrew/opt/ghidra/libexec}"
mkdir -p build out ghidra_proj
python3 tools/le_flatten.py ../part2.exe build/part2
"$GHIDRA_HOME/support/analyzeHeadless" ghidra_proj terrafire \
  -import build/part2.bin -overwrite \
  -loader BinaryLoader -loader-baseAddr 0x10000 -processor x86:LE:32:default \
  -scriptPath ghidra_scripts \
  -preScript LeSetup.java "$PWD/build/part2.coderefs.txt" \
  -postScript FillGaps.java \
  -postScript FixParams.java \
  -postScript ExportDecomp.java "$PWD/out"
