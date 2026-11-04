#!/bin/sh
# Build librnnoise.a for std_rade.dll from freedv/rnnoise
# 8b12912d0e4a4503ca7d83fd1111f00f7e8298e8, the commit the FreeDV app pins,
# with the model named in its model_version file. Same compile flags as its
# ./configure --enable-rtcd: SSE4.1 and AVX2 kernels picked at run time.
# Usage: build_rnnoise.sh   (prints the path of the library)
set -eu
export PATH="/mingw64/bin:/usr/bin:$PATH"
RN="/c/Users/N8SDR/src/rnnoise"
PIN="8b12912d0e4a4503ca7d83fd1111f00f7e8298e8"
GIT="/c/Program Files/Git/cmd/git.exe"
OUT="$RN/build-mingw"
LIB="$OUT/librnnoise.a"
if [ ! -d "$RN" ]; then
  "$GIT" clone -q https://github.com/freedv/rnnoise.git "$RN" >&2
fi
HEAD="$("$GIT" -C "$RN" rev-parse HEAD)"
if [ "$HEAD" != "$PIN" ]; then
  "$GIT" -C "$RN" checkout -q "$PIN" >&2
fi
HASH="$(cat "$RN/model_version")"
if [ ! -f "$RN/src/rnnoise_data.c" ]; then
  TGZ="$RN/rnnoise_data-$HASH.tar.gz"
  if [ ! -f "$TGZ" ]; then
    curl -sSLo "$TGZ" "https://media.xiph.org/rnnoise/models/rnnoise_data-$HASH.tar.gz" >&2
  fi
  GOT="$(sha256sum "$TGZ" | awk '{print $1}')"
  if [ "$GOT" != "$HASH" ]; then
    echo "rnnoise model checksum is $GOT, want $HASH" >&2
    exit 1
  fi
  (cd "$RN" && tar xomf "$TGZ")
fi
if [ -f "$LIB" ] && [ -f "$OUT/pin" ] && [ "$(cat "$OUT/pin")" = "$PIN $HASH" ]; then
  echo "$LIB"
  exit 0
fi
mkdir -p "$OUT"
DEFS="-DRNNOISE_BUILD -DDISABLE_DEBUG_FLOAT -DRNN_ENABLE_X86_RTCD -DCPU_INFO_BY_ASM"
INC="-I$RN/include -I$RN/src"
OBJS=""
for f in denoise rnn pitch kiss_fft celt_lpc nnet nnet_default parse_lpcnet_weights \
         rnnoise_data rnnoise_tables x86/x86_dnn_map x86/x86cpu; do
  o="$OUT/$(basename "$f").o"
  gcc -O2 $DEFS $INC -c "$RN/src/$f.c" -o "$o"
  OBJS="$OBJS $o"
done
gcc -O2 -msse4.1 $DEFS $INC -c "$RN/src/x86/nnet_sse4_1.c" -o "$OUT/nnet_sse4_1.o"
gcc -O2 -mavx -mfma -mavx2 $DEFS $INC -c "$RN/src/x86/nnet_avx2.c" -o "$OUT/nnet_avx2.o"
rm -f "$LIB"
ar rcs "$LIB" $OBJS "$OUT/nnet_sse4_1.o" "$OUT/nnet_avx2.o"
echo "$PIN $HASH" > "$OUT/pin"
echo "$LIB"
