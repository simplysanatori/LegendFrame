#!/bin/bash
# Runs INSIDE the legendframe-mingw container. Invoked by build.sh.
#   /src     read-only source tree (TRLVR layout: src/, dxvk-trlvr/, third_party/)
#   /out     writable output dir
#   /shims   read-only toolchain/shims
#   /fixes   optional read-only dir of *.patch files applied to the COPY (patch -p1)
# Env: JOBS, WHAT=all|dxvk|proxy
#
# Headset in use: if ~/.cache/headset-in-use exists (override: HEADSET_FLAG=path), this script
# waits for it to disappear before building, and a background watcher checks every 60 s
# (HEADSET_POLL=seconds, for tests) while the build runs: when the file appears it SIGSTOPs every
# process this script started (ninja, compilers, ld) and prints "paused: headset in use"; when it
# disappears it SIGCONTs them and prints "resumed". No sudo. Inside the container the file is only
# visible if build.sh's caller bind-mounts it and sets HEADSET_FLAG; build.sh itself already pauses
# the whole container from the host, so by default this is a no-op there and matters when this
# script is run directly on a machine.
set -euo pipefail
JOBS=${JOBS:-$(nproc)}
WHAT=${WHAT:-all}
W=/out/work
mkdir -p "$W" /out/dist /out/logs

ts() { date +%s; }

# --- pause while the headset is in use -------------------------------------------------------
FLAG="${HEADSET_FLAG:-$HOME/.cache/headset-in-use}"
POLL="${HEADSET_POLL:-60}"
if [ -e "$FLAG" ]; then
  echo "paused: headset in use ($FLAG exists); waiting before the build starts"
  while [ -e "$FLAG" ]; do sleep "$POLL"; done
  echo "resumed"
fi
MAIN=$$
# all descendants of $1 (not $1 itself), from /proc
descendants() {
  grep -H '^PPid:' /proc/[0-9]*/status 2>/dev/null | awk -F'[/:[:space:]]+' -v root="$1" '
    { par[$3] = $(NF) }
    END { n = 1; q[1] = root
          for (i = 1; i <= n; i++) for (p in par) if (par[p] == q[i]) { q[++n] = p; print p } }'
}
watcher() {
  local paused=0 me=$BASHPID p
  while kill -0 "$MAIN" 2>/dev/null; do
    sleep "$POLL" || true
    if { [ -e "$FLAG" ] && [ "$paused" = 0 ]; } || { [ ! -e "$FLAG" ] && [ "$paused" = 1 ]; }; then
      local sig=STOP; [ "$paused" = 1 ] && sig=CONT
      local skip=" $me $(descendants "$me" | tr '\n' ' ')"
      for p in $(descendants "$MAIN"); do
        case "$skip" in *" $p "*) continue ;; esac
        kill -"$sig" "$p" 2>/dev/null || true
      done
      if [ "$paused" = 0 ]; then paused=1; echo "paused: headset in use"; else paused=0; echo "resumed"; fi
    fi
  done
}
watcher & WATCHER=$!; disown "$WATCHER"
trap 'kill "$WATCHER" 2>/dev/null; for p in $(descendants "$MAIN"); do kill -CONT "$p" 2>/dev/null; done; true' EXIT

if [ "$WHAT" = all ] || [ "$WHAT" = dxvk ]; then
  t0=$(ts)
  echo "=== DXVK: copy tree"
  rm -rf "$W/dxvk-trlvr" "$W/b-dxvk"
  cp -r /src/dxvk-trlvr "$W/dxvk-trlvr"; rm -rf "$W/dxvk-trlvr/build-win32"
  for p in /fixes/dxvk-*.patch; do [ -f "$p" ] && (cd "$W/dxvk-trlvr" && patch -p1 < "$p"); done || true
  cd "$W/dxvk-trlvr"
  # Same options as build_dxvk.bat (d3d9 only), mingw cross file, asserts off.
  meson setup --backend ninja --cross-file build-win32.txt --buildtype release \
      -Denable_d3d8=false -Denable_d3d10=false -Denable_d3d11=false -Denable_dxgi=false \
      -Db_ndebug=if-release "$W/b-dxvk" .
  ninja -C "$W/b-dxvk" -j"$JOBS"
  i686-w64-mingw32-strip -o /out/dist/d3d9_dxvk.dll "$W/b-dxvk/src/d3d9/d3d9.dll"
  echo "DXVK_SECONDS=$(( $(ts) - t0 ))"
fi

if [ "$WHAT" = all ] || [ "$WHAT" = proxy ]; then
  t0=$(ts)
  echo "=== proxy + launcher: copy tree"
  rm -rf "$W/src" "$W/obj" "$W/third_party"; mkdir -p "$W/obj" "$W/third_party"
  cp -r /src/src "$W/src"
  for d in openvr openxr imgui; do [ -d /src/third_party/$d ] && cp -r /src/third_party/$d "$W/third_party/"; done
  for p in /fixes/proxy-*.patch; do [ -f "$p" ] && (cd "$W" && patch -p1 < "$p"); done || true
  cd "$W"
  # Struct-returning OpenVR virtuals: MSVC and GCC disagree on thiscall+sret, so route the
  # calls through trlvr_abi::call_sret (see shims/seh_compat.h). Mechanical rewrite, diffed.
  mkdir -p "$W/abi-orig"; rm -rf "$W/abi-orig/src"; cp -r "$W/src" "$W/abi-orig/src"
  SRET='GetEyeToHeadTransform|GetProjectionMatrix|GetSeatedZeroPoseToStandingAbsoluteTrackingPose|GetRawZeroPoseToStandingAbsoluteTrackingPose|GetMatrix34TrackedDeviceProperty|GetHiddenAreaMesh|GetCurrentFadeColor|GetHeadsetViewMode'
  find "$W/src" -name '*.cpp' -o -name '*.h' | xargs sed -i -E \
    "s/\b([A-Za-z_][A-Za-z0-9_]*)->($SRET)\(\)/trlvr_abi::call_sret(\1, \&std::remove_pointer_t<decltype(\1)>::\2)/g; \
     s/\b([A-Za-z_][A-Za-z0-9_]*)->($SRET)\(/trlvr_abi::call_sret(\1, \&std::remove_pointer_t<decltype(\1)>::\2, /g"
  (cd "$W" && diff -ru abi-orig/src src > /out/abi-fix.diff) || true
  echo "ABI rewrite sites: $(grep -c '^+.*call_sret' /out/abi-fix.diff || true)"
  # Source list, defines and include dirs come straight from build.bat.
  SRCS=$(grep -E '^set SRC=%SRC% "%ROOT%[^"]+\.cpp"' /src/build.bat | sed -E 's/.*%ROOT%([^"]+)"/\1/; s#\\#/#g')
  IMGUI_SRCS=""
  if [ -d "$W/third_party/imgui" ]; then
    for f in $(grep -oE 'for %%F in \(([a-z0-9_ ]+)\)' /src/build.bat | sed -E 's/.*\((.*)\)/\1/'); do IMGUI_SRCS="$IMGUI_SRCS third_party/imgui/$f.cpp"; done
  fi
  DEFS=$(grep -E '^set CFLAGS=' /src/build.bat | grep -oE '/D[A-Za-z_0-9]+' | sed 's#^/D#-D#' | tr '\n' ' ')
  INC="-I$W/third_party/openvr/headers -I$W/third_party/openxr/include -I$W/third_party/imgui -I/src/dxvk-trlvr/src/d3d9 -I/src/dxvk-trlvr/include/vulkan/include"
  # -include seh_compat.h: MSVC __try/__except shim + _ReturnAddress + sret helper; sources stay unmodified.
  # MSVC x86 defaults to SSE2 math; i686 GCC defaults to x87, which is slow under FEX. -mstackrealign:
  # the game (MSVC, 4-byte stack alignment) calls into the proxy, and GCC's SSE spills assume 16.
  FPFLAGS="-msse2 -mfpmath=sse -mstackrealign"
  CXXFLAGS="-std=gnu++17 -O2 $FPFLAGS $DEFS -fno-strict-aliasing -include /shims/seh_compat.h $INC ${EXTRA_CXXFLAGS:-}"
  echo "sources: $(echo $SRCS $IMGUI_SRCS | wc -w); defs: $DEFS"
  objs=(); pids=()
  for s in $SRCS $IMGUI_SRCS; do
    s=${s%.cpp}
    o="$W/obj/$(echo $s | tr / _).o"; objs+=("$o")
    f="$W/$s.cpp"; [ -f "$f" ] || f="$W/src/${s#src/}.cpp"
    i686-w64-mingw32-g++ $CXXFLAGS -c "$f" -o "$o" > "${o%.o}.log" 2>&1 &
    pids+=($!)
    while [ "$(jobs -r | wc -l)" -ge "$JOBS" ]; do sleep 0.2; done
  done
  rc=0; for p in "${pids[@]}"; do wait "$p" || rc=1; done
  cat "$W"/obj/*.log | grep -E "error|Error" | head -80 || true
  [ $rc = 0 ] || { echo "PROXY COMPILE FAILED"; exit 1; }
  # GNU ld wants a name after a bare LIBRARY keyword (MSVC link does not).
  sed 's/^LIBRARY[[:space:]]*$/LIBRARY "d3d9.dll"/' "$W/src/proxy/d3d9.def" > "$W/d3d9.mingw.def"
  XRLIB=""; [ -f "$W/third_party/openxr/lib/win32/openxr_loader.lib" ] && XRLIB="$W/third_party/openxr/lib/win32/openxr_loader.lib"
  # Not linking d3d9: this DLL is d3d9.dll. --kill-at = undecorated stdcall exports like MSVC.
  i686-w64-mingw32-g++ -shared -static -static-libgcc -static-libstdc++ -s \
      -o /out/dist/d3d9.dll "${objs[@]}" "$W/d3d9.mingw.def" $XRLIB \
      -Wl,--kill-at -Wl,--enable-stdcall-fixup -Wl,--subsystem,windows -Wl,--large-address-aware \
      -Wl,-Map,/out/dist/d3d9.map \
      -ldxguid -luser32 -lkernel32 -ladvapi32 -lgdi32 -ldwmapi ${EXTRA_LIBS:-}
  # Launcher: its own exe (large-address-aware bit must be in the file).
  i686-w64-mingw32-g++ -std=gnu++17 -O2 $FPFLAGS -DWIN32 -DNDEBUG -D_CRT_SECURE_NO_WARNINGS \
      -include /shims/seh_compat.h -static -static-libgcc -static-libstdc++ -s \
      -o /out/dist/trlvr_launcher.exe "$W/src/launcher/main.cpp" \
      -mwindows -municode -Wl,--large-address-aware -luser32 -lkernel32 -lshell32
  # d3d9.dll links openxr_loader statically (MSVC delay-loads it): the loader must ship beside it.
  if [ -f /src/third_party/openxr/bin/win32/openxr_loader.dll ]; then
    cp /src/third_party/openxr/bin/win32/openxr_loader.dll /out/dist/
    echo "NOTE: dist/openxr_loader.dll must be installed next to d3d9.dll (static import)"
  fi
  echo "PROXY_SECONDS=$(( $(ts) - t0 ))"
fi
ls -l /out/dist
