#!/usr/bin/env bash
# Fast-reaction flash watcher: polls for /dev/ttyACM0, flashes within ~0.3s
# of enumeration (the board's awake window after USB plug is short), then
# runs the first console conversation with the new firmware. Retries on the
# next port appearance until success or timeout.
# Run inside `nix develop` — needs the devShell python (pyserial + intelhex).
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PY="$(command -v python3)"
ESPTOOL=$HOME/.platformio/packages/tool-esptoolpy/esptool.py
FW="$REPO/firmware/.pio/build/molly-count"
BOOTAPP0=$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin

"$PY" -c 'import serial, intelhex' 2>/dev/null || {
  echo "[watcher] need the devShell python (pyserial+intelhex): run inside nix develop"
  exit 1
}

flash_once() {
  "$PY" "$ESPTOOL" --chip esp32s3 --port /dev/ttyACM0 --baud 460800 \
    --before default_reset --after hard_reset write_flash \
    --flash_mode keep --flash_freq keep --flash_size keep \
    0x0 "$FW/bootloader.bin" \
    0x8000 "$FW/partitions.bin" \
    0xe000 "$BOOTAPP0" \
    0x10000 "$FW/firmware.bin"
}

first_convo() {
  # new firmware boots after hard_reset: 15s console window. Set the RTC
  # from the host clock, then read back state.
  local now
  now=$(date +"%Y-%m-%d %H:%M:%S")
  sleep 1.2 # let the firmware reach its console window
  "$PY" "$REPO/tools/serial_cmd.py" \
    --time 14 --send "SETDATE $now" --send GET --send BTN
}

echo "[watcher] waiting for /dev/ttyACM0 (max 2h)..."
END=$((SECONDS + 7200))
while [ $SECONDS -lt $END ]; do
  if [ -e /dev/ttyACM0 ]; then
    sleep 0.3
    if [ ! -e /dev/ttyACM0 ]; then continue; fi # vanished again, keep watching
    echo "[watcher] port detected, flashing..."
    if flash_once; then
      echo "[watcher] FLASH OK, first console conversation follows"
      first_convo
      echo "[watcher] DONE"
      exit 0
    fi
    echo "[watcher] flash attempt failed (port may have vanished), still watching..."
  fi
  sleep 0.1
done
echo "[watcher] TIMEOUT"
exit 1
