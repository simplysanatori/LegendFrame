#!/usr/bin/env bash
# Builds legendframe-<version>-steam-frame-linux-arm64.zip: the release payload plus the Frame installer, so a
# tool that unpacks the zip (Frametanium) can run install.sh from the unpacked folder. Nothing is
# built here; the payload is the folder (or legendframe-<version>-payload.zip) the release already uses.
#
#   frame/make-steam-frame-zip.sh PAYLOAD_DIR|PAYLOAD_ZIP [OUT_DIR]
#
# The zip is flat: the payload files, install.sh, uninstall.sh, seed-registry.py and shortcut/.
# install.sh copies only the payload files into the game folder; the rest is the installer itself.
# The name has steam-frame and linux-arm64 on purpose: Frametanium picks its download by those words,
# and the plain -payload.zip (the Windows drag-and-drop files, no installer) must score lower.
# Writes OUT_DIR/legendframe-<version>-steam-frame-linux-arm64.zip (default: the current folder) and prints its sha256.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IN="$(readlink -f "${1:?payload folder or zip}")"
OUT="$(mkdir -p "${2:-.}" && cd "${2:-.}" && pwd)"
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/lf-steam-frame.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
mkdir "$STAGE/z"
if [ -f "$IN" ]; then python3 -I -m zipfile -e "$IN" "$STAGE/z"; else cp -a "$IN/." "$STAGE/z/"; fi
for f in d3d9.dll d3d9_dxvk.dll openvr_api.dll trlvr_launcher.exe trlvr_actions.json VERSION; do
  [ -f "$STAGE/z/$f" ] || { echo "payload has no $f" >&2; exit 1; }
done
VER="$(head -n1 "$STAGE/z/VERSION" | tr -d '\r' | tr -cd '0-9A-Za-z._-')"
[ -n "$VER" ] || { echo "empty VERSION" >&2; exit 1; }
cp "$HERE/install.sh" "$HERE/uninstall.sh" "$HERE/seed-registry.py" "$STAGE/z/"
mkdir "$STAGE/z/shortcut"; cp "$HERE/shortcut/trlvr-config.py" "$HERE/shortcut/vdf.py" "$STAGE/z/shortcut/"
chmod +x "$STAGE/z/install.sh" "$STAGE/z/uninstall.sh"
ZIP="$OUT/legendframe-$VER-steam-frame-linux-arm64.zip"
rm -f "$ZIP"
( cd "$STAGE/z" && TZ=UTC find . -type f | sed 's|^\./||' | sort | xargs touch -t 202610280000 &&
  python3 -I - "$ZIP" <<'PY'
import os, sys, zipfile
z = zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED)
for root, _, files in sorted(os.walk(".")):
    for f in sorted(files):
        p = os.path.relpath(os.path.join(root, f))
        i = zipfile.ZipInfo(p, (2026, 10, 28, 0, 0, 0))
        i.external_attr = (0o755 if p in ("install.sh", "uninstall.sh") else 0o644) << 16
        i.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(i, open(p, "rb").read())
z.close()
PY
)
echo "$ZIP"
( cd "$OUT" && sha256sum "$(basename "$ZIP")" )
