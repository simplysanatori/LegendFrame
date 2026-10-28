#!/bin/bash
# Reproducible mingw build of the TRL VR mod (DXVK fork + d3d9 proxy + launcher).
# usage: build.sh <source-tree> <output-dir> [dxvk|proxy|all]
#   source-tree: TRLVR layout (src/, dxvk-trlvr/, third_party/), mounted read-only.
#   output-dir : receives dist/{d3d9.dll,d3d9_dxvk.dll,trlvr_launcher.exe}, work/, logs/
# Env: FIXES=<dir with dxvk-*.patch / proxy-*.patch applied to the in-container copy>
#      JOBS, EXTRA_CXXFLAGS, EXTRA_LIBS. Needs podman and image legendframe-mingw
#      (podman build -t legendframe-mingw <this dir>).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(readlink -f "${1:?source tree}"); OUT=$(readlink -m "${2:?output dir}"); WHAT=${3:-all}
mkdir -p "$OUT"
FIXMNT=()
if [ -n "${FIXES:-}" ]; then FIXMNT=(-v "$(readlink -f "$FIXES"):/fixes:ro,Z"); fi
podman image exists localhost/legendframe-mingw || podman build -t legendframe-mingw "$HERE"
exec podman run --rm -e JOBS="${JOBS:-}" -e WHAT="$WHAT" -e EXTRA_CXXFLAGS="${EXTRA_CXXFLAGS:-}" -e EXTRA_LIBS="${EXTRA_LIBS:-}" \
  -v "$SRC:/src:ro,Z" -v "$OUT:/out:Z" -v "$HERE/shims:/shims:ro,Z" -v "$HERE/container-build.sh:/container-build.sh:ro,Z" \
  "${FIXMNT[@]}" localhost/legendframe-mingw bash /container-build.sh
