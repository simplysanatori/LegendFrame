#!/usr/bin/env bash
# LegendFrame uninstaller for the Steam Frame. No sudo.
#
# Removes the files frame/install.sh copied (from <game>/.legendframe-files.txt), restores whatever
# it backed up to <game>/.legendframe-backup-*/, removes the files the mod generated at run time,
# and queues removal of the "Tomb Raider: Legend VR" shortcut for the next Steam start.
# The game registry values and SteamVR per-app settings are left in place; the end of this
# script's output says how to revert them.
#
#   uninstall.sh [--steam-dir DIR] [--game-dir DIR] [--userid N] [--keep-shortcut]
set -euo pipefail

APPID=7000
SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STEAM_DIR="$HOME/.local/share/Steam"
GAME=""
USERID=""
DO_SHORTCUT=1
SCRIPT_HOME="$HOME/.local/share/legendframe/shortcut"
DROPIN_DIR="$HOME/.config/systemd/user/steam.service.d"
# Written by the game or the mod at run time, never by the installer.
GENERATED=(trl_vr.exe trlvr.log trlvr.log.prev trlvr_launcher.log trl_vr_d3d9.log trlvr_crash.dmp trlvr_crash.dmp.prev)

say()  { printf '%s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die()  { printf 'error: %s\n' "$*" >&2; exit 1; }
usage() { sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d' | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
  case "$1" in
    --steam-dir) STEAM_DIR="${2:?}"; shift 2 ;;
    --game-dir) GAME="${2:?}"; shift 2 ;;
    --userid) USERID="${2:?}"; shift 2 ;;
    --keep-shortcut) DO_SHORTCUT=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option: $1 (try --help)" ;;
  esac
done

if pgrep trl_vr >/dev/null 2>&1; then
  die "Tomb Raider: Legend VR is running (trl_vr). Quit the game and run this again."
fi

if [ -z "$GAME" ]; then
  LIBVDF="$STEAM_DIR/steamapps/libraryfolders.vdf"
  [ -f "$LIBVDF" ] || die "cannot find $LIBVDF (try --steam-dir)"
  while IFS= read -r lib; do
    mf="$lib/steamapps/appmanifest_$APPID.acf"
    if [ -f "$mf" ]; then
      dir="$(sed -n 's/^[[:space:]]*"installdir"[[:space:]]*"\(.*\)".*/\1/p' "$mf" | head -n1)"
      if [ -n "$dir" ] && [ -d "$lib/steamapps/common/$dir" ]; then GAME="$lib/steamapps/common/$dir"; break; fi
    fi
  done < <(sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)".*/\1/p' "$LIBVDF")
  [ -n "$GAME" ] || die "Tomb Raider: Legend (app $APPID) is not installed in any Steam library."
fi
GAME="$(cd "$GAME" && pwd)" || die "game folder not found"
LIBRARY="$(dirname "$(dirname "$GAME")")"
MANIFEST="$GAME/.legendframe-files.txt"
[ -f "$MANIFEST" ] || die "no manifest at $MANIFEST: LegendFrame was not installed here by frame/install.sh."

safe_name() { case "$1" in ''|*/*|.|..|.*) return 1 ;; *) return 0 ;; esac; }
safe_backup() { case "$1" in */*) return 1 ;; .legendframe-backup-*) return 0 ;; *) return 1 ;; esac; }

FILES=() BACKUPS=() SEEDED_INI=0
while IFS=' ' read -r kind val; do
  case "$kind" in
    F) safe_name "$val" && FILES+=("$val") || warn "ignoring odd manifest entry: $val" ;;
    B) safe_backup "$val" && BACKUPS+=("$val") || warn "ignoring odd backup entry: $val" ;;
    I) SEEDED_INI=1 ;;
  esac
done < "$MANIFEST"

say "Game folder: $GAME"
for f in "${FILES[@]+"${FILES[@]}"}"; do rm -f "$GAME/$f"; done
say "Removed ${#FILES[@]} installed file(s)."
for f in "${GENERATED[@]}"; do rm -f "$GAME/$f"; done
if [ "$SEEDED_INI" = 1 ]; then
  rm -f "$GAME/trlvr.ini"
  say "Removed trlvr.ini (the installer created it)."
elif [ -e "$GAME/trlvr.ini" ]; then
  say "Kept trlvr.ini (it was there before the installer). Delete it yourself if you want."
fi

RESTORED=0
for b in "${BACKUPS[@]+"${BACKUPS[@]}"}"; do
  [ -d "$GAME/$b" ] || { warn "backup folder $b is missing"; continue; }
  for p in "$GAME/$b"/*; do
    [ -e "$p" ] || continue
    cp -p "$p" "$GAME/$(basename "$p")"
    rm -f "$p"
    RESTORED=$((RESTORED + 1))
  done
  rmdir "$GAME/$b" 2>/dev/null || warn "left $GAME/$b (not empty)"
done
[ "$RESTORED" -gt 0 ] && say "Restored $RESTORED file(s) from backup."
rm -f "$MANIFEST"

if [ "$DO_SHORTCUT" = 1 ]; then
  CFG="$SCRIPT_HOME/trlvr-config.py"
  if [ ! -f "$CFG" ]; then
    [ -f "$SELF_DIR/shortcut/trlvr-config.py" ] || die "missing $SELF_DIR/shortcut/trlvr-config.py"
    mkdir -p "$SCRIPT_HOME"
    cp -f "$SELF_DIR/shortcut/trlvr-config.py" "$SELF_DIR/shortcut/vdf.py" "$SCRIPT_HOME/"
  fi
  UID_FOUND="$(python3 -I "$CFG" --steam-dir "$STEAM_DIR" ${USERID:+--userid "$USERID"} --detect 2>&1)" || die "$UID_FOUND"
  rm -f "$DROPIN_DIR/60-legendframe.conf"
  sdq() { local s="${1//%/%%}"; s="${s//\$/\$\$}"; printf '"%s"' "$s"; }
  DROPIN="$DROPIN_DIR/61-legendframe-remove.conf"
  mkdir -p "$DROPIN_DIR"
  {
    echo "# Written by LegendFrame (frame/uninstall.sh). Removes the Tomb Raider: Legend VR shortcut before"
    echo "# Steam's next start and then deletes itself."
    echo "[Service]"
    printf 'ExecStartPre=-/usr/bin/timeout -k 5 30 /usr/bin/python3 -I %s --remove --steam-dir %s --game-dir %s --userid %s --force --dropin %s\n' \
      "$(sdq "$CFG")" "$(sdq "$STEAM_DIR")" "$(sdq "$GAME")" "$(sdq "$UID_FOUND")" "$(sdq "$DROPIN")"
  } > "$DROPIN"
  systemctl --user daemon-reload || warn "systemctl --user daemon-reload failed"
  say "Shortcut removal queued: $DROPIN"
  say "Restart Steam (Power > Exit Steam, then start it again) to apply it. This script never restarts Steam."
  say "Afterwards you can delete $(dirname "$SCRIPT_HOME")."
fi

cat <<TXT

Left in place on purpose:
  - Game registry values (EnableDepthOfField, EnableFullscreenEffects, EnableVSync, EnableShadows,
    EnableReflection, EnableWaterFX, Fullscreen, all set to 0). To revert, either change them back in
    the game's Setup dialog, or quit every Wine process that uses the prefix and copy a
    user.reg.legendframe-bak-* backup over
      $LIBRARY/compatdata/$APPID/pfx/user.reg
  - SteamVR per-app settings for app $APPID (72 Hz, resolution 1440). To revert, use SteamVR
    Settings > Video > per-application, or with SteamVR closed delete the "steam.app.$APPID" section
    from ~/.config/openvr/config/steamvr.vrsettings.
  - The compatibility tool mapping for app $APPID (Proton 11 ARM64): Steam > Tomb Raider: Legend >
    Properties > Compatibility.
TXT
