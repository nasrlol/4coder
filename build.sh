#!/usr/bin/env bash
# Builds the custom layer (layer/4coder_byp.cpp) into build/custom_4coder.so.
# usage: ./build.sh [release]
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"

case "$(uname -s)" in
    Linux)  script=buildsuper_x64-linux.sh ;;
    Darwin) script=buildsuper_x64-mac.sh ;;
    *) echo "build.sh: unsupported OS '$(uname -s)' (use build.bat on Windows)"; exit 1 ;;
esac

cd "$ROOT/build"
"$ROOT/layer/custom/bin/$script" "$ROOT/layer/4coder_byp.cpp" "$1"
