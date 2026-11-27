#!/usr/bin/env bash
# Builds the 4coder core (build/4ed_app.so + build/4ed) from code/. Linux only.
# usage: ./build_core.sh [debug|ship]
#   (default)  -g -O2, asserts + GL debug output on (FRED_INTERNAL/FRED_SUPER, like DEV_BUILD)
#   debug      -g -O0, same defines (what code/bin/build-linux.sh produces by default)
#   ship       -g -O2 + SHIP_MODE: asserts compiled out, no synchronous GL debug output
#
# Unlike code/bin/build-linux.sh this only builds the two core binaries: build-linux.sh also
# wipes build/themes (re-copied from code/ship_files/themes, which lacks the nasr themes) and
# overwrites build/custom_4coder.so with the stock default bindings. Build the custom layer
# with ./build.sh.
#
# Needs g++, X11, Xfixes, GL, freetype, fontconfig. On NixOS:
#   nix-shell -p gcc libx11 libxfixes libGL freetype fontconfig --run ./build_core.sh
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
CODE="$ROOT/code"
OUT="${OUT:-$ROOT/build}"   # override to build somewhere else, e.g. OUT=/tmp/x ./build_core.sh

if [ "$(uname -s)" != "Linux" ]; then
    echo "build_core.sh: Linux only (use code/bin/build.bat / build-mac.sh elsewhere)"; exit 1
fi

defines="-DFRED_INTERNAL -DFRED_SUPER"
case "$1" in
    "")    opt="-g -O2" ;;
    debug) opt="-g -O0" ;;
    ship)  opt="-g -O2"; defines="-DFRED_SUPER -DSHIP_MODE" ;;
    *) echo "usage: $0 [debug|ship]"; exit 1 ;;
esac

# same options as GCC_OPTS in code/bin/4ed_build.cpp
opts="-nostdinc++ -Wno-write-strings -D_GNU_SOURCE -fPIC -fno-threadsafe-statics -pthread -Wno-unused-result -Wno-switch -std=c++11"
inc="-I$CODE -I$CODE/custom -I$ROOT/non-source/foreign/freetype2"
libs="-lX11 -lpthread -lm -lrt -lGL -ldl -lXfixes -lfreetype -lfontconfig"

cd "$CODE"
echo "4ed_app.so ($opt $defines)"
g++ $opts $opt $defines $inc -shared 4ed_app_target.cpp -o "$OUT/4ed_app.so"
echo "4ed ($opt $defines)"
g++ $opts $opt $defines $inc -I"$CODE/platform_all" -I"$CODE/platform_unix" platform_linux/linux_4ed.cpp -o "$OUT/4ed" $libs
