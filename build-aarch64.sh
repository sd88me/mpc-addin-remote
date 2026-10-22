#!/usr/bin/env bash
# The addin for Gen2 MPC OS devices (aarch64: Live III, One G2, Key(s) G2): build/aarch64/mpc_remote_addin.so (and
# build/aarch64/standalone, for trying it without restarting MPC), with addin.manifest and the default settings
# beside them. Gen2 only ships MPC OS 3.x (glibc 2.39), so there is no older-MPC-OS glibc ceiling to respect, unlike
# build.sh's Gen1 build. Needs Docker with QEMU for arm64.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build/aarch64
docker run --rm --platform linux/arm64 -u "$(id -u):$(id -g)" -v "$PWD":/b -w /b arm64v8/gcc:12-bookworm sh -c '
  set -e
  F="-std=gnu11 -O2 -Wall -Wextra -Werror"
  gcc $F -fPIC -shared -fvisibility=hidden -Wl,-z,defs -Wl,--as-needed -o build/aarch64/mpc_remote_addin.so \
    src/remote.c src/capture.c src/png.c src/touch.c src/conf.c src/json.c src/image.c src/mcp.c src/midi.c \
    src/device.c src/buttons.c src/surface.c src/keyboard.c -ldl -lpthread
  strip --strip-unneeded build/aarch64/mpc_remote_addin.so
  gcc $F -o build/aarch64/standalone tools/standalone.c -ldl
  strip build/aarch64/standalone
  max=$(objdump -T build/aarch64/mpc_remote_addin.so build/aarch64/standalone | grep -o "GLIBC_[0-9.]*" | sort -t. -k2 -n -u | tail -n 1)
  echo "highest glibc symbol version: $max"
  case "$max" in GLIBC_2.[0-9]|GLIBC_2.[123][0-9]) ;; *) echo "too new for MPC OS 3.x (glibc 2.39): $max" >&2; exit 1 ;; esac
  readelf -d build/aarch64/mpc_remote_addin.so | grep NEEDED
'
cp -f mpc_remote_addin.conf addin.manifest build/aarch64/   # tools/release_addin.py packages build/aarch64 with the installer
ls -l build/aarch64/mpc_remote_addin.so build/aarch64/standalone
