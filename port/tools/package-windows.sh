#!/bin/sh
# Cross-compile the Windows build with MinGW-w64 and zip it with SDL3.dll.
# Usage (from port/): tools/package-windows.sh   ->  dist/terrafire-windows.zip
# Unzip next to the game's DATA.PAK (the folder with SETTINGS.CFG) and run
# terrafire.exe.
set -e
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-win --parallel
x86_64-w64-mingw32-strip build-win/terrafire.exe
rm -rf dist/terrafire-windows && mkdir -p dist/terrafire-windows
cp build-win/terrafire.exe build-win/SDL3.dll dist/terrafire-windows/
cat > dist/terrafire-windows/README.txt <<'TXT'
TerraFire for Windows (port of the 1997 DOS game by Owen Thomas)

Copy terrafire.exe and SDL3.dll into the folder of your TerraFire installation
(the one containing DATA.PAK and SETTINGS.CFG) and run terrafire.exe.
Settings and saved games (GAME0001.SAV ...) are kept in that folder and are
compatible with the DOS version.

Controls: as set in Options > Control (default arrows + Ctrl), P pause,
Esc menu. Gamepads work too (stick + A to fire, B or Start for the menu).
TXT
(cd dist && rm -f terrafire-windows.zip && zip -qr terrafire-windows.zip terrafire-windows)
echo "dist/terrafire-windows.zip"
