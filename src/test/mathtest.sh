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
#                                   (and with the pre-M4 original 3104624, when
#                                   git can provide it: REF=<rev>, default
#                                   3104624; REF=none skips it).
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
if [ "$MODE" != session ]; then
    echo "unknown mode '$MODE' (base | session)" >&2
    exit 2
fi

# Session suite: six copies of the session, each in its own namespace.
OBJ="$OUT/session-obj"
rm -rf "$OBJ"
mkdir -p "$OBJ"
REF=${REF:-3104624}   # pre-M4 steam-frame: the 1.1.0 session maths
REF_SRC="$SRC"
REF_NOTE="this tree (REF=none)"
if [ "$REF" != none ]; then
    if git -C "$ROOT" rev-parse -q --verify "$REF^{commit}" >/dev/null; then
        # Only the files under test come from the reference revision; every
        # other header is this tree's, so struct layouts (Config!) match the
        # stubs, which are built against this tree.
        rm -rf "$OUT/ref"
        mkdir -p "$OUT/ref/src"
        cp -r "$SRC/vr" "$SRC/common" "$OUT/ref/src/"
        for f in vr/vr_session.cpp vr/vr_session.h vr/vr_math.cpp vr/vr_math.h; do
            git -C "$ROOT" show "$REF:src/$f" > "$OUT/ref/src/$f"
        done
        REF_SRC="$OUT/ref/src"
        REF_NOTE="$REF ($(git -C "$ROOT" rev-parse --short "$REF"))"
    else
        echo "mathtest: no git revision '$REF'; reference = this tree" >&2
    fi
fi
echo "session reference: $REF_NOTE"

pids=()
build_copy() {   # name, source root, extra defines...
    local name=$1 root=$2
    shift 2
    local ns=(-Dtrlvr=trlvr_$name)
    "$CXX" "${FLAGS[@]}" "${ns[@]}" -c "$root/vr/vr_session.cpp" \
        -o "$OBJ/${name}_session.o" &
    pids+=($!)
    "$CXX" "${FLAGS[@]}" "${ns[@]}" -c "$root/vr/vr_math.cpp" \
        -o "$OBJ/${name}_math.o" &
    pids+=($!)
    "$CXX" "${FLAGS[@]}" "${ns[@]}" -DMATHTEST_SESSION_STUBS \
        -DMATHTEST_COPY=$name "$@" -c "$SRC/test/mathtest.cpp" \
        -o "$OBJ/${name}_stubs.o" &
    pids+=($!)
}
build_copy ref "$REF_SRC" -DMATHTEST_REFERENCE
for c in s sc dn dc lg; do build_copy $c "$SRC"; done
(cd "$SRC" && "$CXX" "${FLAGS[@]}" -DMATHTEST_SESSION -c test/mathtest.cpp \
    -o "$OBJ/driver.o") &
pids+=($!)
for s in "${BASE_SRCS[@]}"; do
    (cd "$SRC" && "$CXX" "${FLAGS[@]}" -c "$s" -o "$OBJ/base_$(basename "$s" .cpp).o") &
    pids+=($!)
done
fail=0
for p in "${pids[@]}"; do wait "$p" || fail=1; done
[ $fail = 0 ] || { echo "mathtest: build failed" >&2; exit 1; }
"$CXX" -o "$OUT/mathtest-session" "$OBJ"/*.o
MATHTEST_OUT="$OUT/" exec "$OUT/mathtest-session"
