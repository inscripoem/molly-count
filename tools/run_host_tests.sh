#!/usr/bin/env bash
# Host unit tests for the portable logic (civil/battery/store/framebuffer/
# screen). These files must not include Arduino/esp headers. Run inside the
# devShell: nix develop -c tools/run_host_tests.sh
set -euo pipefail
cd "$(dirname "$0")/.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
g++ -std=c++17 -Wall -Wextra -Ifirmware/src \
  firmware/src/civil.cpp \
  firmware/src/battery.cpp \
  firmware/src/store.cpp \
  firmware/src/framebuffer.cpp \
  firmware/src/assets.cpp \
  firmware/src/fonts.cpp \
  firmware/src/screen.cpp \
  firmware/src/version.cpp \
  firmware/test_host/test_main.cpp \
  firmware/test_host/test_civil.cpp \
  firmware/test_host/test_battery.cpp \
  firmware/test_host/test_store.cpp \
  firmware/test_host/test_framebuffer.cpp \
  firmware/test_host/test_screen.cpp \
  firmware/test_host/test_version.cpp \
  -o "$out/molly_host_tests"
"$out/molly_host_tests"
