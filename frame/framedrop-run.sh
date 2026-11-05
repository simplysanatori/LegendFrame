#!/bin/sh
# run.sh: what FrameDrop (or Steam) starts for LegendFrame when it was installed as a FrameDrop zip.
#
# LegendFrame is a set of Windows files for Tomb Raider: Legend, which runs under Proton, so there is
# nothing here to start. The first time this runs, and again whenever the VERSION file in this folder
# differs from the version it installed last, it runs ./install.sh without a terminal, with the Steam
# shortcut step turned off (--no-shortcut), which puts the mod into the Steam copy of Tomb Raider: Legend.
# Then it says where to launch the game from. Tomb Raider: Legend must already be installed from Steam.
# The installer's output goes to the log file named below.
#
# Environment (all optional): FRAMEDROP_RUN_DRY=1 does the same and changes the message only slightly (tests).
cd "$(dirname "$0")" || exit 1
HERE=$(pwd)
VERSION=$(head -n 1 VERSION 2> /dev/null | tr -d '\r' | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')
[ -n "$VERSION" ] || VERSION=unknown
STATE=${XDG_STATE_HOME:-$HOME/.local/state}/framedrop-legendframe
LOG=$STATE/install.log
mkdir -p "$STATE" 2> /dev/null

if [ "$(cat "$STATE/installed-version" 2> /dev/null)" != "$VERSION" ]; then
    echo "LegendFrame $VERSION: installing into the Tomb Raider: Legend folder (log: $LOG)"
    {
        echo "== $(date '+%Y-%m-%d %H:%M:%S') run.sh: install.sh --no-shortcut from $HERE"
        bash ./install.sh --no-shortcut < /dev/null
    } >> "$LOG" 2>&1
    rc=$?
    if [ "$rc" != 0 ]; then
        echo "LegendFrame: the installer failed (exit $rc); last lines of $LOG:" >&2
        tail -n 8 "$LOG" >&2
        exit "$rc"
    fi
    printf '%s\n' "$VERSION" > "$STATE/installed-version"
fi
MSG="LegendFrame $VERSION is installed in your Tomb Raider: Legend folder. Start the game from the \"Tomb Raider: Legend VR\" entry in your Steam library, not from this one. If that entry is missing, run install.sh from $HERE in a terminal (it queues the entry for the next Steam start)."
echo "$MSG"
printf '%s\n' "$MSG" > "$STATE/last-message.txt"
exit 0
