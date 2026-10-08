#!/usr/bin/env bash
# Console watcher: when the board wakes (port appears), catch the 15s
# console window and send the given commands, then listen for the reply and
# the refresh/sleep sequence.
#   usage: console_watch.sh ["GET"] ["SLEEP 90"] ...
# Run inside `nix develop` — needs the devShell python (pyserial).
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PY="$(command -v python3)"
SERIAL_CMD="$REPO/tools/serial_cmd.py"

"$PY" -c 'import serial' 2>/dev/null || {
  echo "[cwatch] need the devShell python (pyserial): run inside nix develop"
  exit 1
}

SENDS=()
for arg in "$@"; do SENDS+=(--send "$arg"); done

echo "[cwatch] waiting for wake (port), max 2h..."
END=$((SECONDS + 7200))
while [ $SECONDS -lt $END ]; do
  if [ -e /dev/ttyACM0 ]; then
    sleep 0.3
    if [ ! -e /dev/ttyACM0 ]; then continue; fi
    echo "[cwatch] board awake, sending: $*"
    "$PY" "$SERIAL_CMD" --time 40 "${SENDS[@]}"
    echo "[cwatch] conversation done"
    exit 0
  fi
  sleep 0.1
done
echo "[cwatch] TIMEOUT"
exit 1
