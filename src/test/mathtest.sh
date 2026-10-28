#!/bin/bash
# Native (Linux host, e.g. the Steam Frame's aarch64) build and run of
# mathtest -- the same tests mathtest.bat runs under MSVC, no Windows, no
# game, no headset. Windows-only headers are replaced by src/test/host/.
#
#   src/test/mathtest.sh            vr_math / gesture / physics tests
#   src/test/mathtest.sh session    + the per-eye substitution suite (M4):
#                                   vr_session.cpp built several times, in
#                                   separate namespaces, against stubs of the
#                                   game state, and the dual-eye function
#                                   compared bit for bit with single calls
#                                   (and with the steam-frame original, when
#                                   git can provide it: REF=<rev>, default
#                                   steam-frame; REF=none skips it).
#   C0_CAPTURE=<c0_capture.bin>     also replay a capture from the game
#                                   ([vr] vr_capture_c0 = 1) in session mode.
#
# -ffp-contract=off matters: GCC on aarch64 fuses a*b+c by default, which
# makes float results depend on how code is inlined. MSVC/mingw x86 never
# fuse, so this is also the closer model of the shipped DLL.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$SRC/.." && pwd)
OUT=${OUT:-$ROOT/build/test-host}
CXX=${CXX:-g++}
MODE=${1:-base}
mkdir -p "$OUT"
FLAGS=(-std=gnu++17 -O2 -ffp-contract=off -fno-strict-aliasing -w
       -I "$HERE/host" -include "$HERE/host/host_prelude.h")
BASE_SRCS=(vr/vr_math.cpp vr/vr_gesture.cpp
           vr/physics/body_push.cpp vr/physics/body_volumes.cpp
           vr/godmode/god_carry.cpp vr/godmode/god_pluck.cpp
           vr/physics/soft_body.cpp vr/physics/rear_jiggle.cpp
           vr/physics/rear_shape.cpp vr/godmode/board_leash.cpp
           vr/menu_panel.cpp vr/finger_input.cpp)

if [ "$MODE" = base ]; then
    (cd "$SRC" && "$CXX" "${FLAGS[@]}" test/mathtest.cpp "${BASE_SRCS[@]}" \
        -o "$OUT/mathtest")
    exec "$OUT/mathtest"
fi
echo "unknown mode '$MODE' (base | session)" >&2
exit 2
