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
cp res/release-readme.txt dist/terrafire-windows/README.txt
(cd dist && rm -f terrafire-windows.zip && zip -qr terrafire-windows.zip terrafire-windows)
echo "dist/terrafire-windows.zip"
