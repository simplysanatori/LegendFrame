#!/bin/bash
# Build and run the single-pass stereo harness (src/test/stereotest.cpp).
# usage: stereotest.sh <dir containing d3d9_dxvk.dll> <output dir> [-- stereotest args...]
#   Builds stereotest.exe (i686 mingw, podman image legendframe-mingw), copies it and the
#   DLL into <output dir>/run, runs it headless under Proton 11 ARM64 on the Frame with a
#   scratch prefix (never the game prefix), and leaves in <output dir>:
#     stereotest.log         case table (the exe also writes it to stdout -> stereotest.out, which
#                            proton swallows; the log file is authoritative)
#     trl_dxvk/stereotest_d3d9.log   DXVK log (DXVK_LOG_LEVEL=info)
#     *.ppm                  dumps of mismatching cases (or all, with --dump)
#   Exit: stereotest's exit code (0 all pass, 1 failure, 2 some case not available).
# Env: SKIP_BUILD=1 reuse <output dir>/stereotest.exe; SKIP_RUN=1 build only;
#      STEREOTEST_IID={guid} IID of IDirect3DVRStereo9 while the header is not frozen
#      (ignored when d3d9_vr.h already declares it); TIMEOUT=seconds (default 240).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
DLLDIR=$(readlink -f "${1:?dir containing d3d9_dxvk.dll}")
OUT=$(readlink -m "${2:?output dir}")
shift 2
[ "${1:-}" = "--" ] && shift
EXTRA_ARGS=("$@")
[ -f "$DLLDIR/d3d9_dxvk.dll" ] || { echo "no d3d9_dxvk.dll in $DLLDIR" >&2; exit 64; }
mkdir -p "$OUT/run" "$OUT/trl_dxvk"

# ---- build (same flag family as toolchain/container-build.sh; no d3d9 import lib: the
#      exe LoadLibrary()s the fork)
if [ -z "${SKIP_BUILD:-}" ]; then
  DEFS=""; INC=""
  if grep -q IDirect3DVRStereo9 "$ROOT/dxvk-trlvr/src/d3d9/d3d9_vr.h" 2>/dev/null; then
    DEFS="-DSTEREOTEST_USE_DXVK_HEADER"
    INC="-I/src/dxvk-trlvr/src/d3d9 -I/src/dxvk-trlvr/include/vulkan/include -I/src/dxvk-trlvr/include/vulkan"
    echo "[stereotest] using IDirect3DVRStereo9 from d3d9_vr.h"
  else
    echo "[stereotest] d3d9_vr.h has no IDirect3DVRStereo9 yet: local contract copy (IID via STEREOTEST_IID)"
  fi
  podman image exists localhost/legendframe-mingw || podman build -t legendframe-mingw "$HERE"
  podman run --rm -v "$ROOT:/src:ro,Z" -v "$OUT:/out:Z" localhost/legendframe-mingw \
    i686-w64-mingw32-g++ -std=gnu++17 -O2 -DWIN32 -DNDEBUG -D_CRT_SECURE_NO_WARNINGS $DEFS $INC \
      -static -static-libgcc -static-libstdc++ -Wl,--large-address-aware \
      -o /out/stereotest.exe /src/src/test/stereotest.cpp -luser32 -lgdi32 -lkernel32 \
    || { echo "[stereotest] BUILD FAILED" >&2; exit 70; }
fi
[ -n "${SKIP_RUN:-}" ] && exit 0

# ---- run on the Frame under Proton 11 ARM64, headless (no headset, no SteamVR)
ST=$HOME/.local/share/Steam
SP=$(pgrep -x steam | head -1)
[ -n "$SP" ] || { echo "steam is not running (need its environment)" >&2; exit 71; }
cp -f "$OUT/stereotest.exe" "$OUT/run/"
cp -f "$DLLDIR/d3d9_dxvk.dll" "$OUT/run/"
PFX=$HOME/Games/LegendFrame/build-m2c/pfx
mkdir -p "$PFX"
rm -f "$OUT/stereotest.out" "$OUT/stereotest.log" "$OUT"/trl_dxvk/*.log
(
  while IFS= read -r -d '' kv; do export "$kv"; done < "/proc/$SP/environ"
  export DISPLAY=:1
  export STEAM_COMPAT_DATA_PATH="$PFX" STEAM_COMPAT_CLIENT_INSTALL_PATH="$ST"
  export SteamAppId=0 SteamGameId=0
  export DXVK_LOG_LEVEL=info DXVK_LOG_PATH="$OUT/trl_dxvk" DXVK_STATE_CACHE=0
  export STEREOTEST_IID="${STEREOTEST_IID:-}"
  WOUT=$(echo "$OUT" | sed 's#^/#Z:/#')
  cd "$OUT/run"
  timeout "${TIMEOUT:-240}" "$ST/steamapps/common/SteamLinuxRuntime_4-arm64/_v2-entry-point" --verb=waitforexitandrun -- \
    "$ST/steamapps/common/Proton 11.0 (ARM64)/proton" waitforexitandrun "$OUT/run/stereotest.exe" \
    --out "$WOUT" "${EXTRA_ARGS[@]}" > "$OUT/stereotest.out" 2>&1
  echo "$?" > "$OUT/exitcode"
)
RC=$(cat "$OUT/exitcode" 2>/dev/null || echo 99)
cat "$OUT/stereotest.log" 2>/dev/null || { echo "(no stereotest.log; raw output follows)"; tail -40 "$OUT/stereotest.out"; }
echo "---- DXVK log lines of interest ($OUT/trl_dxvk)"
grep -h -E "D3D9VR|tereo|^(warn|err):" "$OUT"/trl_dxvk/*.log 2>/dev/null | head -40
echo "[stereotest] exit code $RC"
exit "$RC"
