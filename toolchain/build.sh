#!/bin/bash
# Reproducible mingw build of the TRL VR mod (DXVK fork + d3d9 proxy + launcher).
# usage: build.sh <source-tree> <output-dir> [dxvk|proxy|all]
#   source-tree: TRLVR layout (src/, dxvk-trlvr/, third_party/), mounted read-only.
#   output-dir : receives dist/{d3d9.dll,d3d9_dxvk.dll,openxr_loader.dll,trlvr_launcher.exe}, work/, logs/
# Env: FIXES=<dir with dxvk-*.patch / proxy-*.patch applied to the in-container copy>
#      JOBS, EXTRA_CXXFLAGS, EXTRA_LIBS. Needs podman and image legendframe-mingw
#      (podman build -t legendframe-mingw <this dir>).
#
# Headset in use: builds must not load the Frame while it is being used for VR. If
# ~/.cache/headset-in-use exists (override with HEADSET_FLAG=path), this script waits for it to
# disappear before it starts a build, and while the build runs a background watcher checks every
# 60 s (HEADSET_POLL=seconds, for tests): when the file appears it pauses the build (SIGSTOP to the
# podman process group, and `podman pause` of the container, which freezes ninja and the compilers
# inside it) and prints "paused: headset in use"; when the file is gone it resumes (SIGCONT,
# `podman unpause`) and prints "resumed". No sudo. Ctrl-C or kill always resumes first.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(readlink -f "${1:?source tree}"); OUT=$(readlink -m "${2:?output dir}"); WHAT=${3:-all}
FLAG="${HEADSET_FLAG:-$HOME/.cache/headset-in-use}"
POLL="${HEADSET_POLL:-60}"
NAME="legendframe-build-$$"
mkdir -p "$OUT"
FIXMNT=()
if [ -n "${FIXES:-}" ]; then FIXMNT=(-v "$(readlink -f "$FIXES"):/fixes:ro,Z"); fi

# before starting: wait while the headset is in use
if [ -e "$FLAG" ]; then
  echo "paused: headset in use ($FLAG exists); waiting before the build starts"
  while [ -e "$FLAG" ]; do sleep "$POLL"; done
  echo "resumed"
fi
podman image exists localhost/legendframe-mingw || podman build -t legendframe-mingw "$HERE"

# the build runs in its own session/process group so SIGSTOP/SIGCONT reach only it
setsid podman run --rm --name "$NAME" -e JOBS="${JOBS:-}" -e WHAT="$WHAT" -e EXTRA_CXXFLAGS="${EXTRA_CXXFLAGS:-}" -e EXTRA_LIBS="${EXTRA_LIBS:-}" \
  -v "$SRC:/src:ro,Z" -v "$OUT:/out:Z" -v "$HERE/shims:/shims:ro,Z" -v "$HERE/container-build.sh:/container-build.sh:ro,Z" \
  "${FIXMNT[@]}" localhost/legendframe-mingw bash /container-build.sh &
BUILD=$!

PAUSED=0
pause_build()  { kill -STOP -- "-$BUILD" 2>/dev/null || true; podman pause "$NAME" >/dev/null 2>&1 || true; }
resume_build() { podman unpause "$NAME" >/dev/null 2>&1 || true; kill -CONT -- "-$BUILD" 2>/dev/null || true; }
watcher() {
  local paused=0
  while kill -0 "$BUILD" 2>/dev/null; do
    sleep "$POLL"
    if [ -e "$FLAG" ] && [ "$paused" = 0 ]; then pause_build; paused=1; echo "paused: headset in use"
    elif [ ! -e "$FLAG" ] && [ "$paused" = 1 ]; then resume_build; paused=0; echo "resumed"; fi
  done
}
watcher & WATCH=$!
cleanup() { kill "$WATCH" 2>/dev/null || true; pkill -P "$WATCH" 2>/dev/null || true; resume_build; }
trap 'cleanup; kill -TERM -- "-$BUILD" 2>/dev/null || true; exit 143' INT TERM
trap cleanup EXIT

rc=0; wait "$BUILD" || rc=$?
exit "$rc"
