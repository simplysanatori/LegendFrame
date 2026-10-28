#!/bin/bash
# Build and run the seh_compat runtime check headless under Proton (scratch prefix, no game).
# usage: seh_test.sh <output dir>
set -uo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(readlink -m "${1:?output dir}"); mkdir -p "$OUT"
podman image exists localhost/legendframe-mingw || podman build -t legendframe-mingw "$HERE"
podman run --rm -v "$HERE:/tc:ro,Z" -v "$OUT:/out:Z" localhost/legendframe-mingw \
  i686-w64-mingw32-g++ -std=gnu++17 -O2 -DWIN32 -DNDEBUG -include /tc/shims/seh_compat.h \
    -static -static-libgcc -static-libstdc++ -o /out/seh_test.exe /tc/test/seh_test.cpp || exit 70
ST=$HOME/.local/share/Steam; SP=$(pgrep -x steam | head -1)
[ -n "$SP" ] || { echo "steam not running" >&2; exit 71; }
PFX=$HOME/Games/LegendFrame/build-m2c/pfx; mkdir -p "$PFX"
rm -f "$OUT/seh_test.log"
(
  while IFS= read -r -d '' kv; do export "$kv"; done < "/proc/$SP/environ"
  export DISPLAY=:1 STEAM_COMPAT_DATA_PATH="$PFX" STEAM_COMPAT_CLIENT_INSTALL_PATH="$ST" SteamAppId=0 SteamGameId=0
  cd "$OUT"
  timeout "${TIMEOUT:-120}" "$ST/steamapps/common/SteamLinuxRuntime_4-arm64/_v2-entry-point" --verb=waitforexitandrun -- \
    "$ST/steamapps/common/Proton 11.0 (ARM64)/proton" waitforexitandrun "$OUT/seh_test.exe" "Z:/${OUT#/}/seh_test.log" > "$OUT/seh_test.out" 2>&1
  echo $? > "$OUT/exitcode"
)
cat "$OUT/seh_test.log" 2>/dev/null || tail -20 "$OUT/seh_test.out"
echo "exit code $(cat "$OUT/exitcode")"
exit "$(cat "$OUT/exitcode")"
