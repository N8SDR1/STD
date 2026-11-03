#!/bin/sh
# Build std_rade.dll against freedv/rade_c c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9.
# Usage: build_rade_shim.sh <dest-dir> <std_rade_shim.c>
set -eu
export PATH="/mingw64/bin:/usr/bin:$PATH"
DEST="$(cygpath -u "$1")"
SHIM="$(cygpath -u "$2")"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHECK="$(cygpath -u "$ROOT/scripts/rade_shim_check.c")"
RADE="/c/Users/N8SDR/src/rade_c"
PIN="c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9"
GIT="/c/Program Files/Git/cmd/git.exe"
if [ ! -x "$GIT" ]; then
  echo "Windows git is missing" >&2
  exit 1
fi
HEAD="$("$GIT" -C "$RADE" rev-parse HEAD)"
if [ "$HEAD" != "$PIN" ]; then
  echo "rade_c is $HEAD, want $PIN" >&2
  exit 1
fi
DLL="$RADE/build-mingw/src/librade.dll"
IMPLIB="$RADE/build-mingw/src/librade.dll.a"
OPUS="$RADE/build-mingw/build_opus-prefix/src/build_opus/.libs/libopus.a"
if [ ! -f "$DLL" ] || [ ! -f "$IMPLIB" ] || [ ! -f "$OPUS" ]; then
  echo "librade build products are missing under $RADE/build-mingw" >&2
  exit 1
fi
mkdir -p "$DEST"
gcc -O2 -shared -fno-stack-protector \
  -I"$RADE/build-mingw/build_opus-prefix/src/build_opus/dnn" \
  -I"$RADE/build-mingw/build_opus-prefix/src/build_opus/include" \
  -I"$RADE/build-mingw/build_opus-prefix/src/build_opus/celt" \
  -o "$DEST/std_rade.dll" "$SHIM" "$IMPLIB" "$OPUS" -lm
cp -f "$DLL" "$DEST/librade.dll"
for dep in libgcc_s_seh-1.dll libwinpthread-1.dll libstdc++-6.dll; do
  src="$(gcc -print-file-name="$dep")"
  if [ -f "$src" ]; then
    cp -f "$src" "$DEST/$dep"
  fi
done
gcc -O2 -o "$DEST/rade_shim_check.exe" "$CHECK" "$DEST/std_rade.dll"
(cd "$DEST" && ./rade_shim_check.exe)
rm -f "$DEST/rade_shim_check.exe"
echo "std_rade.dll ready in $DEST"
