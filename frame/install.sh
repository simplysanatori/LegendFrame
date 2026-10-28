#!/usr/bin/env bash
# LegendFrame installer for the Steam Frame (aarch64 SteamOS). No sudo. Safe to run again.
#
# Installs a built or packaged LegendFrame (the Tomb Raider: Legend VR mod) into the Steam copy of
# Tomb Raider: Legend, tunes the game for the Frame and queues a "Tomb Raider: Legend VR" shortcut.
# See docs/FRAME-INSTALL.md for the full story.
#
#   install.sh [--payload DIR|ZIP] [--steam-dir DIR] [--game-dir DIR] [--userid N] [options]
#
#   --payload DIR|ZIP   folder with the built files, or the release zip
#                       (legendframe-<version>-payload.zip); default: ./dist next to this script
#   --steam-dir DIR     Steam root (default ~/.local/share/Steam)
#   --game-dir DIR      the game folder, instead of looking it up from appmanifest_7000.acf
#   --userid N          Steam user id, when more than one account has logged in on this Frame
#   --check             print the installed LegendFrame version and exit (changes nothing)
#   --without-openxr    do not copy openxr_loader.dll (it is copied by default: the mingw-built
#                       d3d9.dll imports it statically, so the game will not start without it)
#   --vrsettings "refresh=72,res=1440"
#                       SET the per-app SteamVR refresh rate / resolution to these values, even if
#                       the owner already has others. Without this option only values that are
#                       absent are written (72 Hz / 1440); existing ones are kept and printed.
#   --edit-vrsettings   when SteamVR is not running, write the per-app SteamVR settings straight
#                       into steamvr.vrsettings instead of only printing how
#   --force-registry    overwrite the game registry values even if they already exist
#                       (default: only values that are absent are seeded)
#   --no-registry | --no-vrsettings | --no-shortcut     skip that step
#
# The payload must contain d3d9.dll, d3d9_dxvk.dll, openvr_api.dll (from the upstream TRLVR
# release zip), trlvr_launcher.exe, trlvr_actions.json and bindings_*.json; openxr_loader.dll and
# VERSION are optional (VERSION is copied into the game folder as .legendframe-version).
# --check prints the installed version marker and exits without changing anything.
#   -h, --help
set -euo pipefail

APPID=7000
SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STEAM_DIR="$HOME/.local/share/Steam"
PAYLOAD=""
GAME=""
USERID=""
WITH_OPENXR=1 EDIT_VRSETTINGS=0 DO_REGISTRY=1 DO_VRSETTINGS=1 DO_SHORTCUT=1 FORCE_REGISTRY=0 CHECK=0
VR_REFRESH=72 VR_RES=1440 VR_FORCE=0
REG_ARGS=()
VRSERVER="${LEGENDFRAME_VRSERVER:-vrserver}"   # env overrides (this and LEGENDFRAME_VRCMD) are for tests
VRCMD="${LEGENDFRAME_VRCMD:-/opt/steamvr/bin/linuxarm64/vrcmd}"
SCRIPT_HOME="$HOME/.local/share/legendframe/shortcut"
DROPIN_DIR="$HOME/.config/systemd/user/steam.service.d"
REQUIRED=(d3d9.dll d3d9_dxvk.dll openvr_api.dll trlvr_launcher.exe trlvr_actions.json)
VERSION_FILE=.legendframe-version

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
    --without-openxr) WITH_OPENXR=0; shift ;;
    --vrsettings)
      v="${2:?}"; shift 2
      VR_FORCE=1; IFS=',' read -r -a kv <<< "$v"
      for p in "${kv[@]}"; do
        case "$p" in
          refresh=[0-9]*) VR_REFRESH="${p#refresh=}" ;;
          res=[0-9]*) VR_RES="${p#res=}" ;;
          *) die "bad --vrsettings entry: $p (use refresh=72,res=1440)" ;;
        esac
      done
      case "$VR_REFRESH$VR_RES" in *[!0-9]*) die "bad --vrsettings value: $v" ;; esac ;;
    --force-registry) FORCE_REGISTRY=1; REG_ARGS+=(--force); shift ;;
    --check) CHECK=1; shift ;;
    --edit-vrsettings) EDIT_VRSETTINGS=1; shift ;;
    --no-registry) DO_REGISTRY=0; shift ;;
    --no-vrsettings) DO_VRSETTINGS=0; shift ;;
    --no-shortcut) DO_SHORTCUT=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option: $1 (try --help)" ;;
  esac
done

# --- 0. the game must not be running --------------------------------------------------------
if [ "$CHECK" = 0 ] && pgrep trl_vr >/dev/null 2>&1; then
  die "Tomb Raider: Legend VR is running (trl_vr). Quit the game and run this again."
fi

# --- 1. payload -----------------------------------------------------------------------------
if [ "$CHECK" = 0 ]; then
  if [ -z "$PAYLOAD" ]; then
    for d in "$SELF_DIR/dist" "$SELF_DIR/../dist"; do
      if [ -f "$d/d3d9.dll" ]; then PAYLOAD="$d"; break; fi
    done
    [ -n "$PAYLOAD" ] || die "no payload found. Build first (see toolchain/) or pass --payload DIR (looked in $SELF_DIR/dist and $SELF_DIR/../dist)."
  fi
  if [ -f "$PAYLOAD" ]; then
    case "$PAYLOAD" in *.zip|*.ZIP) ;; *) die "payload must be a folder or a .zip file: $PAYLOAD" ;; esac
    ZIP_TMP="$(mktemp -d "${TMPDIR:-/tmp}/legendframe-payload.XXXXXX")"
    trap 'rm -rf "$ZIP_TMP"' EXIT
    python3 -I -m zipfile -e "$PAYLOAD" "$ZIP_TMP" || die "could not unpack $PAYLOAD"
    if [ -f "$ZIP_TMP/d3d9.dll" ]; then PAYLOAD="$ZIP_TMP"
    else
      sub=("$ZIP_TMP"/*/d3d9.dll)
      [ -f "${sub[0]}" ] || die "no d3d9.dll in $PAYLOAD"
      PAYLOAD="$(dirname "${sub[0]}")"
    fi
    say "Unpacked the payload zip to $PAYLOAD"
  fi
  [ -d "$PAYLOAD" ] || die "payload folder not found: $PAYLOAD"
  PAYLOAD="$(cd "$PAYLOAD" && pwd)"
  for f in "${REQUIRED[@]}"; do
    if [ -f "$PAYLOAD/$f" ]; then continue; fi
    if [ "$f" = openvr_api.dll ]; then
      die "payload is missing openvr_api.dll ($PAYLOAD). It is not built here: copy it from the upstream TRLVR release zip (RototRobot, TRLVR 1.0.0) into the payload folder."
    fi
    die "payload is missing $f ($PAYLOAD)"
  done
  shopt -s nullglob
  BINDINGS=("$PAYLOAD"/bindings_*.json)
  shopt -u nullglob
  [ ${#BINDINGS[@]} -gt 0 ] || die "payload has no bindings_*.json ($PAYLOAD)"
  FILES=("${REQUIRED[@]}")
  for b in "${BINDINGS[@]}"; do FILES+=("$(basename "$b")"); done
  if [ "$WITH_OPENXR" = 1 ]; then
    if [ -f "$PAYLOAD/openxr_loader.dll" ]; then FILES+=(openxr_loader.dll)
    else warn "the payload has no openxr_loader.dll; a mingw-built d3d9.dll will not load without it (--without-openxr silences this)"; fi
  fi
  PAYLOAD_VERSION=unknown
  if [ -f "$PAYLOAD/VERSION" ]; then
    PAYLOAD_VERSION="$(head -n1 "$PAYLOAD/VERSION" | tr -d '\r' | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')"
    [ -n "$PAYLOAD_VERSION" ] || PAYLOAD_VERSION=unknown
  fi
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
if [ "$CHECK" = 1 ]; then
  if [ -f "$GAME/$VERSION_FILE" ]; then say "LegendFrame installed: $(head -n1 "$GAME/$VERSION_FILE")"
  elif [ -f "$GAME/.legendframe-files.txt" ]; then say "LegendFrame installed: unknown version (installed before version markers)"
  else say "LegendFrame is not installed in this game folder."; fi
  exit 0
fi
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
# Only keys that are absent are added (under [vr]); existing keys and every other line stay as
# they are, including CRLF line endings and a UTF-8 BOM.
INI_NEW=0
if [ ! -e "$GAME/trlvr.ini" ]; then
  printf '; Seeded by LegendFrame (frame/install.sh) with Steam Frame defaults.\n; The mod adds every other setting, with its explanation, the first time it runs.\n[vr]\n' > "$GAME/trlvr.ini"
  INI_NEW=1 SEEDED_INI=1
fi
python3 -I - "$GAME/trlvr.ini" <<'PY' || warn "could not seed trlvr.ini"
import os, sys
path = sys.argv[1]
WANT = [("fsaa", "off"), ("msaa", "2"), ("comfort_vignette", "off"), ("cull_fov_degrees", "110"),
        ("stereo_mode", "singlepass"), ("stereo_stats", "0"), ("vr_runtime", "openvr"),
        ("submit_wait", "0"), ("god_carry_fix", "1"), ("hud_offset_x", "-0.08"), ("hud_offset_y", "0.08")]
raw = open(path, "r", encoding="utf-8", errors="surrogateescape", newline="").read()
eol = "\r" if "\r\n" in raw else ""
lines = raw.split("\n")          # lines keep their own \r, so untouched lines stay byte-identical
def sec(l):
    l = l.lstrip("\ufeff").strip()
    return l[1:l.index("]")].strip().lower() if l.startswith("[") and "]" in l else None
start = next((i for i, l in enumerate(lines) if sec(l) == "vr"), None)
if start is None:
    while lines and lines[-1] in ("", "\r"): lines.pop()
    lines += ["[vr]" + eol, ""]; start = len(lines) - 2
end = start + 1
while end < len(lines) and sec(lines[end]) is None: end += 1
have = {}
for l in lines[start + 1:end]:
    t = l.strip()
    if t and t[0] not in ";#" and "=" in t:
        have[t.split("=", 1)[0].strip().lower()] = t.split("=", 1)[1].strip()
ins = end
while ins > start + 1 and lines[ins - 1].strip() == "": ins -= 1
if ins == len(lines) and lines[-1] != "": lines.append("")   # no newline at end of file
added, kept = [], []
for k, v in WANT:
    if k in have: kept.append("%s=%s" % (k, have[k]))
    else:
        lines.insert(ins, "%s=%s%s" % (k, v, eol)); ins += 1; added.append("%s=%s" % (k, v))
if added:
    out = "\n".join(lines)
    tmp = path + ".legendframe-tmp"
    open(tmp, "w", encoding="utf-8", errors="surrogateescape", newline="").write(out)
    os.replace(tmp, path)
print("trlvr.ini: added " + (", ".join(added) if added else "nothing (all Frame keys already present)"))
if kept: print("trlvr.ini: kept existing " + ", ".join(kept))
PY

printf '%s\n' "$PAYLOAD_VERSION" > "$GAME/$VERSION_FILE"
say "Version marker: $GAME/$VERSION_FILE ($PAYLOAD_VERSION)"

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
    python3 -I "$SELF_DIR/seed-registry.py" "$USERREG" "${REG_ARGS[@]+"${REG_ARGS[@]}"}" || rc=$?
    if [ "$rc" = 3 ]; then
      say "  Run the game once from the Steam store entry (the Setup dialog writes the section), quit, and run this installer again."
    elif [ "$rc" != 0 ]; then
      warn "registry seeding failed (exit $rc)"
    fi
  fi
fi

# --- 6. SteamVR per-app settings ------------------------------------------------------------
# Values that already exist are kept (the owner may have a higher resolution override);
# --vrsettings "refresh=N,res=N" sets them regardless.
manual_vrsettings() {
  say "  Set them by hand: SteamVR Settings > Video > per-application (Tomb Raider: Legend): refresh $VR_REFRESH Hz, resolution $VR_RES;"
  say "  or, with SteamVR closed, add to ~/.config/openvr/config/steamvr.vrsettings:"
  say "    \"steam.app.$APPID\" : { \"preferredRefreshRate\" : $VR_REFRESH, \"resolutionOverride\" : $VR_RES }"
  say "  or, with SteamVR running: $VRCMD --set-settings-int steam.app.$APPID.preferredRefreshRate $VR_REFRESH"
  say "                            $VRCMD --set-settings-int steam.app.$APPID.resolutionOverride $VR_RES"
}
vr_get() { timeout 15 "$VRCMD" --settings-int "steam.app.$APPID.$1" 2>/dev/null | sed -n 's/^[^=]*=//p' | head -n1; }
if [ "$DO_VRSETTINGS" = 1 ]; then
  if [ -x "$VRCMD" ] && pgrep -x "$VRSERVER" >/dev/null 2>&1; then
    vr_ok=1
    for kv in "preferredRefreshRate:$VR_REFRESH" "resolutionOverride:$VR_RES"; do
      k="${kv%%:*}" v="${kv#*:}"
      cur="$(vr_get "$k")"
      if [ -n "$cur" ] && [ "$VR_FORCE" = 0 ]; then
        say "SteamVR: steam.app.$APPID.$k is $cur; kept (--vrsettings forces a value)."
      elif [ -n "$cur" ] && [ "$cur" = "$v" ]; then
        say "SteamVR: steam.app.$APPID.$k is already $v."
      elif timeout 15 "$VRCMD" --set-settings-int "steam.app.$APPID.$k" "$v" >/dev/null 2>&1; then
        say "SteamVR: steam.app.$APPID.$k set to $v${cur:+ (was $cur)}."
      else
        vr_ok=0
      fi
    done
    [ "$vr_ok" = 1 ] || { warn "vrcmd failed"; manual_vrsettings; }
  elif [ "$EDIT_VRSETTINGS" = 1 ] && ! pgrep -x "$VRSERVER" >/dev/null 2>&1; then
    python3 -I - "$HOME/.config/openvr/config/steamvr.vrsettings" "steam.app.$APPID" "$VR_REFRESH" "$VR_RES" "$VR_FORCE" <<'PY' || manual_vrsettings
import json, os, shutil, sys, time
p, sec, refresh, res, force = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5] == "1"
if not os.path.isfile(p):
    print("  no steamvr.vrsettings yet (start SteamVR once)"); sys.exit(1)
d = json.load(open(p, encoding="utf-8"))
s = d.get(sec) if isinstance(d.get(sec), dict) else {}
want = {"preferredRefreshRate": refresh, "resolutionOverride": res}
change = {}
for k, v in want.items():
    if k in s and not force:
        print("SteamVR: %s.%s is %s; kept (--vrsettings forces a value)." % (sec, k, s[k]))
    elif s.get(k) == v:
        print("SteamVR: %s.%s is already %s." % (sec, k, v))
    else:
        change[k] = v
if not change:
    sys.exit(0)
shutil.copy2(p, p + ".legendframe-bak-" + time.strftime("%Y%m%d-%H%M%S"))
s.update(change); d[sec] = s
json.dump(d, open(p + ".tmp", "w", encoding="utf-8"), indent=3)
os.replace(p + ".tmp", p)
print("SteamVR: wrote %s to steamvr.vrsettings." % ", ".join("%s=%s" % kv for kv in change.items()))
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
