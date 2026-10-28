#!/usr/bin/env bash
# LegendFrame installer for the Steam Frame (aarch64 SteamOS). No sudo. Safe to run again.
#
# Installs a built or packaged LegendFrame (the Tomb Raider: Legend VR mod) into the Steam copy of
# Tomb Raider: Legend, tunes the game for the Frame and queues a "Tomb Raider: Legend VR" shortcut.
# See docs/FRAME-INSTALL.md for the full story.
#
#   install.sh [--payload DIR] [--steam-dir DIR] [--game-dir DIR] [--userid N] [options]
#
#   --payload DIR       folder with the built files (default: ./dist next to this script)
#   --steam-dir DIR     Steam root (default ~/.local/share/Steam)
#   --game-dir DIR      the game folder, instead of looking it up from appmanifest_7000.acf
#   --userid N          Steam user id, when more than one account has logged in on this Frame
#   --with-openxr       also copy openxr_loader.dll if the payload has it (not used on the Frame)
#   --edit-vrsettings   when SteamVR is not running, write the per-app SteamVR settings straight
#                       into steamvr.vrsettings instead of only printing how
#   --no-registry | --no-vrsettings | --no-shortcut     skip that step
#   -h, --help
set -euo pipefail

APPID=7000
SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STEAM_DIR="$HOME/.local/share/Steam"
PAYLOAD=""
GAME=""
USERID=""
WITH_OPENXR=0 EDIT_VRSETTINGS=0 DO_REGISTRY=1 DO_VRSETTINGS=1 DO_SHORTCUT=1
VRCMD=/opt/steamvr/bin/linuxarm64/vrcmd
SCRIPT_HOME="$HOME/.local/share/legendframe/shortcut"
DROPIN_DIR="$HOME/.config/systemd/user/steam.service.d"
REQUIRED=(d3d9.dll d3d9_dxvk.dll openvr_api.dll trlvr_launcher.exe trlvr_actions.json)

say()  { printf '%s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die()  { printf 'error: %s\n' "$*" >&2; exit 1; }

usage() { sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d' | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
  case "$1" in
    --payload) PAYLOAD="${2:?}"; shift 2 ;;
    --steam-dir) STEAM_DIR="${2:?}"; shift 2 ;;
    --game-dir) GAME="${2:?}"; shift 2 ;;
    --userid) USERID="${2:?}"; shift 2 ;;
    --with-openxr) WITH_OPENXR=1; shift ;;
    --edit-vrsettings) EDIT_VRSETTINGS=1; shift ;;
    --no-registry) DO_REGISTRY=0; shift ;;
    --no-vrsettings) DO_VRSETTINGS=0; shift ;;
    --no-shortcut) DO_SHORTCUT=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option: $1 (try --help)" ;;
  esac
done

# --- 0. the game must not be running --------------------------------------------------------
if pgrep trl_vr >/dev/null 2>&1; then
  die "Tomb Raider: Legend VR is running (trl_vr). Quit the game and run this again."
fi

# --- 1. payload -----------------------------------------------------------------------------
if [ -z "$PAYLOAD" ]; then
  for d in "$SELF_DIR/dist" "$SELF_DIR/../dist"; do
    if [ -f "$d/d3d9.dll" ]; then PAYLOAD="$d"; break; fi
  done
  [ -n "$PAYLOAD" ] || die "no payload found. Build first (see toolchain/) or pass --payload DIR (looked in $SELF_DIR/dist and $SELF_DIR/../dist)."
fi
[ -d "$PAYLOAD" ] || die "payload folder not found: $PAYLOAD"
PAYLOAD="$(cd "$PAYLOAD" && pwd)"
for f in "${REQUIRED[@]}"; do
  [ -f "$PAYLOAD/$f" ] || die "payload is missing $f ($PAYLOAD)"
done
shopt -s nullglob
BINDINGS=("$PAYLOAD"/bindings_*.json)
shopt -u nullglob
[ ${#BINDINGS[@]} -gt 0 ] || die "payload has no bindings_*.json ($PAYLOAD)"
FILES=("${REQUIRED[@]}")
for b in "${BINDINGS[@]}"; do FILES+=("$(basename "$b")"); done
if [ "$WITH_OPENXR" = 1 ]; then
  [ -f "$PAYLOAD/openxr_loader.dll" ] && FILES+=(openxr_loader.dll) || warn "--with-openxr given but the payload has no openxr_loader.dll"
fi

# --- 2. find the game -----------------------------------------------------------------------
if [ -z "$GAME" ]; then
  LIBVDF="$STEAM_DIR/steamapps/libraryfolders.vdf"
  [ -f "$LIBVDF" ] || die "cannot find $LIBVDF (is Steam installed for this user? try --steam-dir)"
  while IFS= read -r lib; do
    mf="$lib/steamapps/appmanifest_$APPID.acf"
    if [ -f "$mf" ]; then
      dir="$(sed -n 's/^[[:space:]]*"installdir"[[:space:]]*"\(.*\)".*/\1/p' "$mf" | head -n1)"
      if [ -n "$dir" ] && [ -d "$lib/steamapps/common/$dir" ]; then GAME="$lib/steamapps/common/$dir"; break; fi
    fi
  done < <(sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)".*/\1/p' "$LIBVDF")
  [ -n "$GAME" ] || die "Tomb Raider: Legend (app $APPID) is not installed in any Steam library. Install it from Steam first."
fi
[ -f "$GAME/trl.exe" ] || die "trl.exe not found in $GAME"
GAME="$(cd "$GAME" && pwd)"
LIBRARY="$(dirname "$(dirname "$GAME")")"      # .../steamapps
PREFIX="$LIBRARY/compatdata/$APPID/pfx"
say "Game folder: $GAME"
say "Payload:     $PAYLOAD"

# --- 3. back up, copy, manifest -------------------------------------------------------------
MANIFEST="$GAME/.legendframe-files.txt"
TS="$(date +%Y%m%d-%H%M%S)"
BACKUP_NAME=".legendframe-backup-$TS"
PREV_FILES=() PREV_BACKUPS=() SEEDED_INI=0
if [ -f "$MANIFEST" ]; then
  while IFS=' ' read -r kind val; do
    case "$kind" in
      F) PREV_FILES+=("$val") ;;
      B) PREV_BACKUPS+=("$val") ;;
      I) SEEDED_INI=1 ;;
    esac
  done < "$MANIFEST"
fi
was_ours() { local x; for x in "${PREV_FILES[@]+"${PREV_FILES[@]}"}"; do [ "$x" = "$1" ] && return 0; done; return 1; }

BACKED_UP=0
for f in "${FILES[@]}"; do
  if [ -e "$GAME/$f" ] && ! was_ours "$f"; then
    mkdir -p "$GAME/$BACKUP_NAME"
    cp -p "$GAME/$f" "$GAME/$BACKUP_NAME/$f"
    BACKED_UP=$((BACKED_UP + 1))
  fi
done
[ "$BACKED_UP" -gt 0 ] && say "Backed up $BACKED_UP existing file(s) to $GAME/$BACKUP_NAME"

for f in "${FILES[@]}"; do
  cp -f "$PAYLOAD/$f" "$GAME/$f.legendframe-tmp"
  mv -f "$GAME/$f.legendframe-tmp" "$GAME/$f"
done

# --- 4. trlvr.ini defaults for the Frame ----------------------------------------------------
if [ ! -e "$GAME/trlvr.ini" ]; then
  cat > "$GAME/trlvr.ini" <<'INI'
; Seeded by LegendFrame (frame/install.sh) with Steam Frame defaults.
; The mod adds every other setting, with its explanation, the first time it runs.
[vr]
fsaa=off
msaa=2
comfort_vignette=off
cull_fov_degrees=110
INI
  SEEDED_INI=1
  say "Seeded trlvr.ini (fsaa=off, msaa=2, comfort_vignette=off, cull_fov_degrees=110)"
else
  say "trlvr.ini already exists; left as it is."
fi

{
  echo "# LegendFrame install manifest. Written by frame/install.sh; read by frame/uninstall.sh."
  echo "# updated $TS"
  for b in "${PREV_BACKUPS[@]+"${PREV_BACKUPS[@]}"}"; do echo "B $b"; done
  [ -d "$GAME/$BACKUP_NAME" ] && echo "B $BACKUP_NAME"
  [ "$SEEDED_INI" = 1 ] && echo "I trlvr.ini"
  for f in "${PREV_FILES[@]+"${PREV_FILES[@]}"}" "${FILES[@]}"; do echo "F $f"; done | sort -u -k2
} > "$MANIFEST.tmp"
mv -f "$MANIFEST.tmp" "$MANIFEST"
say "Installed ${#FILES[@]} file(s); manifest: $MANIFEST"

# --- 5. game registry -----------------------------------------------------------------------
if [ "$DO_REGISTRY" = 1 ]; then
  USERREG="$PREFIX/user.reg"
  in_use=0
  for e in /proc/[0-9]*/environ; do
    if grep -qaF "compatdata/$APPID" "$e" 2>/dev/null; then in_use=1; break; fi
  done
  if [ ! -f "$USERREG" ]; then
    say "Registry: the Wine prefix does not exist yet ($PREFIX)."
    say "  Run Tomb Raider: Legend once from the Steam store entry, accept or close its Setup dialog,"
    say "  quit, then run this installer again to seed the graphics registry values."
  elif [ "$in_use" = 1 ]; then
    say "Registry: skipped, a Wine process is using the Tomb Raider: Legend prefix. Quit it and run this again."
  else
    rc=0
    python3 -I "$SELF_DIR/seed-registry.py" "$USERREG" || rc=$?
    if [ "$rc" = 3 ]; then
      say "  Run the game once from the Steam store entry (the Setup dialog writes the section), quit, and run this installer again."
    elif [ "$rc" != 0 ]; then
      warn "registry seeding failed (exit $rc)"
    fi
  fi
fi

# --- 6. SteamVR per-app settings ------------------------------------------------------------
manual_vrsettings() {
  say "  Set them by hand: SteamVR Settings > Video > per-application (Tomb Raider: Legend): refresh 72 Hz, resolution 1440;"
  say "  or, with SteamVR closed, add to ~/.config/openvr/config/steamvr.vrsettings:"
  say "    \"steam.app.$APPID\" : { \"preferredRefreshRate\" : 72, \"resolutionOverride\" : 1440 }"
  say "  or, with SteamVR running: $VRCMD --set-settings-int steam.app.$APPID.preferredRefreshRate 72"
  say "                            $VRCMD --set-settings-int steam.app.$APPID.resolutionOverride 1440"
}
if [ "$DO_VRSETTINGS" = 1 ]; then
  if [ -x "$VRCMD" ] && pgrep -x vrserver >/dev/null 2>&1; then
    if timeout 15 "$VRCMD" --set-settings-int "steam.app.$APPID.preferredRefreshRate" 72 >/dev/null 2>&1 &&
       timeout 15 "$VRCMD" --set-settings-int "steam.app.$APPID.resolutionOverride" 1440 >/dev/null 2>&1; then
      say "SteamVR: per-app settings set live (72 Hz, resolution 1440)."
    else
      warn "vrcmd failed"; manual_vrsettings
    fi
  elif [ "$EDIT_VRSETTINGS" = 1 ] && ! pgrep -x vrserver >/dev/null 2>&1; then
    python3 -I - "$HOME/.config/openvr/config/steamvr.vrsettings" "steam.app.$APPID" <<'PY' || manual_vrsettings
import json, os, shutil, sys, time
p, sec = sys.argv[1], sys.argv[2]
if not os.path.isfile(p):
    print("  no steamvr.vrsettings yet (start SteamVR once)"); sys.exit(1)
d = json.load(open(p, encoding="utf-8"))
s = d.get(sec) if isinstance(d.get(sec), dict) else {}
want = {"preferredRefreshRate": 72, "resolutionOverride": 1440}
if all(s.get(k) == v for k, v in want.items()):
    print("SteamVR: per-app settings already in steamvr.vrsettings."); sys.exit(0)
shutil.copy2(p, p + ".legendframe-bak-" + time.strftime("%Y%m%d-%H%M%S"))
s.update(want); d[sec] = s
json.dump(d, open(p + ".tmp", "w", encoding="utf-8"), indent=3)
os.replace(p + ".tmp", p)
print("SteamVR: per-app settings written to steamvr.vrsettings (72 Hz, resolution 1440).")
PY
  else
    say "SteamVR: not running (or vrcmd missing), so the per-app settings were not set."
    manual_vrsettings
  fi
fi

# --- 7. the Steam shortcut ------------------------------------------------------------------
if [ "$DO_SHORTCUT" = 1 ]; then
  CFG="$SELF_DIR/shortcut/trlvr-config.py"
  [ -f "$CFG" ] || die "missing $CFG"
  mkdir -p "$SCRIPT_HOME"
  cp -f "$SELF_DIR/shortcut/trlvr-config.py" "$SELF_DIR/shortcut/vdf.py" "$SCRIPT_HOME/"
  ARGS=(--steam-dir "$STEAM_DIR" --game-dir "$GAME")
  [ -n "$USERID" ] && ARGS+=(--userid "$USERID")
  UID_FOUND="$(python3 -I "$SCRIPT_HOME/trlvr-config.py" --steam-dir "$STEAM_DIR" ${USERID:+--userid "$USERID"} --detect 2>&1)" ||
    die "$UID_FOUND"
  rm -f "$DROPIN_DIR/61-legendframe-remove.conf"
  if python3 -I "$SCRIPT_HOME/trlvr-config.py" "${ARGS[@]}" --check; then
    say "Shortcut: \"Tomb Raider: Legend VR\" is already in place."
  else
    # systemd treats % and $ specially in command lines
    sdq() { local s="${1//%/%%}"; s="${s//\$/\$\$}"; printf '"%s"' "$s"; }
    DROPIN="$DROPIN_DIR/60-legendframe.conf"
    mkdir -p "$DROPIN_DIR"
    {
      echo "# Written by LegendFrame (frame/install.sh). Adds the Tomb Raider: Legend VR shortcut before"
      echo "# Steam's next start and then deletes itself."
      echo "[Service]"
      printf 'ExecStartPre=-/usr/bin/timeout -k 5 30 /usr/bin/python3 -I %s --steam-dir %s --game-dir %s --userid %s --force --dropin %s\n' \
        "$(sdq "$SCRIPT_HOME/trlvr-config.py")" "$(sdq "$STEAM_DIR")" "$(sdq "$GAME")" "$(sdq "$UID_FOUND")" "$(sdq "$DROPIN")"
    } > "$DROPIN"
    systemctl --user daemon-reload || warn "systemctl --user daemon-reload failed"
    if ! systemctl --user cat steam.service >/dev/null 2>&1; then
      warn "this system has no user steam.service; the shortcut will not be added by itself."
      say "  With Steam closed, run: python3 -I \"$SCRIPT_HOME/trlvr-config.py\" --steam-dir \"$STEAM_DIR\" --game-dir \"$GAME\" --userid $UID_FOUND"
    fi
    say "Shortcut queued: $DROPIN"
  fi
  say ""
  say "Restart Steam (Power > Exit Steam, then start it again) to add the shortcut, then launch"
  say "\"Tomb Raider: Legend VR\", not the store entry. This installer never restarts Steam."
fi
say ""
say "First run: if Tomb Raider: Legend's Setup dialog appears, turn Next Generation Content off. See docs/FRAME-INSTALL.md."
