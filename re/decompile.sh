#!/bin/sh
# Rebuild the Ghidra project and decompiled output from ../part2.exe.
set -e
cd "$(dirname "$0")"
export JAVA_HOME=/opt/homebrew/opt/openjdk@21
export PATH="$JAVA_HOME/bin:$PATH"
mkdir -p build out ghidra_proj
python3 tools/le_flatten.py ../part2.exe build/part2
/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless ghidra_proj terrafire \
  -import build/part2.bin -overwrite \
  -loader BinaryLoader -loader-baseAddr 0x10000 -processor x86:LE:32:default \
  -scriptPath ghidra_scripts \
  -preScript LeSetup.java "$PWD/build/part2.coderefs.txt" \
  -postScript FillGaps.java \
  -postScript FixParams.java \
  -postScript ExportDecomp.java "$PWD/out"
