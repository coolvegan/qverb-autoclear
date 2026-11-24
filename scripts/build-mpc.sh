#!/usr/bin/env bash
# Build QVerb for Akai MPC: native host test, ARM64 + ARM32 cross-compile,
# MPC touch UI, validation and a ready-to-copy plugin folder.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD="$ROOT/build"
PKG="$ROOT/dist/mpc/Tilr - VST - QVerb"
IMAGE="qverb-mpc-cross"
CXX="${CXX:-g++}"
PY="${PYTHON:-python3}"

command -v docker >/dev/null || { echo "docker is required" >&2; exit 1; }
command -v "$PY" >/dev/null || { echo "python3 is required" >&2; exit 1; }

echo "== 1/6  native build + MPC host simulator test"
mkdir -p "$BUILD/native"
"$CXX" -std=c++17 -O2 -fPIC -shared -fvisibility=hidden -fvisibility-inlines-hidden \
    -I "$ROOT/framework" -I "$ROOT/src" \
    -Wl,--version-script="$ROOT/tools/exports.map" \
    -o "$BUILD/native/qverb.so" "$ROOT/src/QVerbMpc.cpp"

"$CXX" -std=c++17 -O2 -o "$BUILD/host_sim" "$ROOT/tools/host-sim/host_sim.cpp"
"$BUILD/host_sim" "$BUILD/native/qverb.so" test --seconds 3 --wav "$BUILD/qverb-test.wav"
"$BUILD/host_sim" "$BUILD/native/qverb.so" dump > "$ROOT/params.json"

echo "== 2/6  generate the MPC touch UI"
"$PY" "$ROOT/tools/gen_ui.py" \
    --params "$ROOT/params.json" \
    --ui "$ROOT/ui.json" \
    --assets-dir "$ROOT/assets" \
    --out "$BUILD/Plugin Skins" \
    --kit-cache "$BUILD"
cp "$ROOT/assets/qverb-bg.png" "$BUILD/Plugin Skins/"

echo "== 3/6  build the cross-compiler image"
docker build -q -t "$IMAGE" -f "$ROOT/docker/Dockerfile.mpc-cross" "$ROOT"

echo "== 4/6  cross-compile ARM64 and ARM32"
mkdir -p "$BUILD/cross/arm64" "$BUILD/cross/arm32"
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$ROOT":/work/src -w /work/src "$IMAGE" bash -lc '
set -euo pipefail
CFLAGS="-std=c++17 -O2 -fPIC -shared -fvisibility=hidden -fvisibility-inlines-hidden \
 -ffunction-sections -fdata-sections -I framework -I src"
LDFLAGS="-Wl,--gc-sections -Wl,-s -Wl,--version-script=tools/exports.map \
 -static-libstdc++ -static-libgcc -Wl,--as-needed"

aarch64-linux-gnu-g++ $CFLAGS -march=armv8-a $LDFLAGS \
  -o build/cross/arm64/qverb-arm64.so src/QVerbMpc.cpp

arm-linux-gnueabihf-g++ $CFLAGS -mcpu=cortex-a17 -mfpu=neon-vfpv4 -mfloat-abi=hard $LDFLAGS \
  -o build/cross/arm32/qverb-arm32.so src/QVerbMpc.cpp
'

echo "== 5/6  package"
rm -rf "$PKG"
mkdir -p "$PKG/VST" "$PKG/Plugin Skins"
cp "$ROOT/plugin.json" "$ROOT/version.xml" "$PKG/"
cp "$ROOT/LICENSE" "$PKG/LICENSE.txt"
cp "$ROOT/MVERB-LICENSE.txt" "$PKG/MVERB-LICENSE.txt"
cp "$BUILD/cross/arm64/qverb-arm64.so" "$PKG/VST/"
cp "$BUILD/cross/arm32/qverb-arm32.so" "$PKG/VST/"
cp "$BUILD/Plugin Skins/"* "$PKG/Plugin Skins/"
cp "$ROOT/assets/qverb-bg.png" "$PKG/Plugin Skins/"

cat > "$PKG/INSTALL.txt" <<'EOF'
QVerb for Akai MPC - installation
=================================

1. Copy the whole folder

       Tilr - VST - QVerb

   into a folder named exactly

       VST2 Plugins

   at the root of a USB drive, SD card or the MPC's internal drive.

2. On the MPC enable:
       Preferences -> Enhancements (Beta) -> Enable Enhancements
       Preferences -> Enhancements (Beta) -> External Plugin Support

3. Restart or rescan. QVerb appears under External Plugins.

The arm64 build is for 64-bit MPC OS devices (MPC Live III / XL and later).
The arm32 build is for Gen1 devices (Live / Live II / One / One+ / X / Key /
Force) running MPC OS 3.10.1 or later.

The UI is the single VERB page: Mix, Decay, Tone, Pre-Delay, Width, Clear,
the Auto Clear switch and the OS 2x switch. Clear at 100% is a hard kill.
The bottom waveform is the design reference, not a live scope.
EOF

cat > "$PKG/README.txt" <<'EOF'
QVerb 1.1.0 - Auto-Clear vocal reverb for Akai MPC
===================================================

QVerb is a compact vocal reverb built around a Dattorro/MVerb core, with a
musical vocal-keyed Auto Clear stage.

Controls
--------
Mix        dry/wet
Decay      reverb time (0.3 - 12 s)
Tone       dark / airy damping and bandwidth
Pre-Delay  0 - 120 ms
Width      mono to extra wide
Clear      Auto Clear depth (0 - 100%)
Auto Clear on/off
OS 2x      2x oversampling around the output limiter (on by default);
           adds 6 samples of latency, reported to the host

Auto Clear for vocals
---------------------
MPC effect plugins never receive MIDI. The trigger comes from the audio of the
voice. QVerb uses an adaptive onset/level detector and a smooth vocal ducker.

  0 - 99 %: the reverb is reduced under the voice and blooms again after the
            phrase. This is the setting for breathy, airy vocals.
  100 %:    hard clear. Once the wet path has faded, the complete reverb tank
            is wiped and refilled by the new vocal; the old tail is gone.

The dry vocal is never filtered or ducked.

Programs
--------
Vocal Air, Close Vocal, Hazy Pad, Dub Vocal, Wide Hall, Room, Init,
Silky Drums.

Signal path
-----------
Input -> 80 Hz high-pass -> Pre-Delay -> Dattorro/MVerb reverb
      -> tone/damping -> stereo width -> Auto Clear -> dry/wet
      -> 2x limiter (knee -0.35 dBFS)
EOF

echo "== 6/6  validate and zip"
"$PY" "$ROOT/tools/validate.py" "$PKG" --params "$ROOT/params.json"

"$PY" - "$PKG" <<'PY'
import os, shutil, sys
pkg = sys.argv[1]
root = os.path.dirname(os.path.dirname(pkg))
archive = os.path.join(root, "Tilr - VST - QVerb-1.1.0")
shutil.make_archive(archive, "zip", os.path.dirname(pkg), os.path.basename(pkg))
print("wrote " + archive + ".zip")
PY

echo
echo "MPC plugin folder: $PKG"
file "$PKG/VST/qverb-arm64.so" "$PKG/VST/qverb-arm32.so"
