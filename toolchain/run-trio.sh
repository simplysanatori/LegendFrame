#!/bin/bash
# usage: run-trio.sh <label> <dist dir> [key=value ini overrides...]
# Swaps the trio into the game dir, launches headless-style (DISPLAY=:1, 90 s), kills, restores.
LABEL=$1; DIST=$(readlink -f $2); shift 2
G="/home/steamos/.local/share/Steam/steamapps/common/Tomb Raider Legend"; ST=/home/steamos/.local/share/Steam
B=/home/steamos/Games/LegendFrame/build-m1; L=$B/logs/$LABEL; mkdir -p $L
pgrep trl_vr && { echo "game running"; exit 1; }
# verify shipped state
(cd $B/shipped/1.0.0 && grep -E " (d3d9.dll|d3d9_dxvk.dll|trlvr_launcher.exe|openvr_api.dll|trl_vr.exe)$" SHA256SUMS | sed "s# # $G/#;s#  *$G/# $G/#" >/dev/null)
for f in d3d9.dll d3d9_dxvk.dll trlvr_launcher.exe; do
  want=$(grep " $f\$" $B/shipped/1.0.0/SHA256SUMS | cut -d' ' -f1); have=$(sha256sum "$G/$f" | cut -d' ' -f1)
  [ "$want" = "$have" ] || { echo "game-dir $f is not the shipped one; abort"; exit 2; }
done
mkdir -p $B/shipped/backup-$LABEL; cp -p "$G"/trlvr.ini "$G"/trlvr.log "$G"/trlvr.log.prev "$G"/trlvr_launcher.log $B/shipped/backup-$LABEL/ 2>/dev/null
restore() {
  cp -p $B/shipped/1.0.0/d3d9.dll $B/shipped/1.0.0/d3d9_dxvk.dll $B/shipped/1.0.0/trlvr_launcher.exe "$G/"
  cp -p $B/shipped/backup-$LABEL/trlvr.ini "$G/trlvr.ini"
  rm -f "$G/openxr_loader.dll" "$G/trlvr_crash.dmp"
  [ -f $B/shipped/backup-$LABEL/trlvr.log ] && cp -p $B/shipped/backup-$LABEL/trlvr.log "$G/"
  [ -f $B/shipped/backup-$LABEL/trlvr.log.prev ] && cp -p $B/shipped/backup-$LABEL/trlvr.log.prev "$G/"
  [ -f $B/shipped/backup-$LABEL/trlvr_launcher.log ] && cp -p $B/shipped/backup-$LABEL/trlvr_launcher.log "$G/"
  (cd "$G" && sha256sum d3d9.dll d3d9_dxvk.dll trlvr_launcher.exe trlvr.ini) > $L/restore-sha.txt
}
trap restore EXIT
for kv in "$@"; do
  k=${kv%%=*}; v=${kv#*=}
  if grep -q "^$k=" "$G/trlvr.ini"; then sed -i "s|^$k=.*|$k=$v|" "$G/trlvr.ini"; else sed -i "/^\[vr\]/a $k=$v" "$G/trlvr.ini"; fi
done
cp $DIST/d3d9.dll $DIST/d3d9_dxvk.dll $DIST/trlvr_launcher.exe "$G/"
[ -f $DIST/openxr_loader.dll ] && cp $DIST/openxr_loader.dll "$G/"
T0=$(date +%s)
SP=$(pgrep -x steam | head -1)
(
  while IFS= read -r -d '' kv; do export "$kv"; done < /proc/$SP/environ
  export DISPLAY=:1 STEAM_COMPAT_DATA_PATH=$ST/steamapps/compatdata/7000 STEAM_COMPAT_CLIENT_INSTALL_PATH=$ST
  export WINEDLLOVERRIDES="d3d9=n,b" SteamAppId=7000 SteamGameId=7000
  timeout 90 $ST/steamapps/common/SteamLinuxRuntime_4-arm64/_v2-entry-point --verb=waitforexitandrun -- "$ST/steamapps/common/Proton 11.0 (ARM64)/proton" waitforexitandrun "$G/trlvr_launcher.exe" > $L/run.out 2>&1
)
pkill trl_vr; pkill trlvr_launch; sleep 3
cp "$G"/trlvr.log "$G"/trlvr_launcher.log $L/ 2>/dev/null; cp "$G"/trl_vr_d3d9.log $L/ 2>/dev/null; cp "$G"/trlvr_crash.dmp $L/ 2>/dev/null
for f in vrserver vrcompositor; do awk -v t="$(date -d @$T0 '+%b %d %H:%M:%S')" '1' ~/.local/share/Steam/logs/$f.txt | tail -n 4000 > $L/$f.tail.txt; done
echo "ran $((`date +%s`-T0)) s"
