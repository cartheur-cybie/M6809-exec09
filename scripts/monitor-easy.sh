#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ ! -x "$ROOT_DIR/src/exec09-monitor" ]]; then
  echo "[1/3] building exec09-monitor"
  (cd "$ROOT_DIR" && autoreconf -fi && ./configure >/dev/null && make -j2 >/dev/null)
fi

echo "[2/3] releasing kernel serial drivers (ftdi_sio, usbserial)"
if ! sudo modprobe -r ftdi_sio usbserial; then
  echo "warning: could not remove one or more drivers (continuing)"
fi

echo "[3/3] running diag"
"$ROOT_DIR/src/exec09-monitor" diag

echo
echo "next:"
echo "  ./scripts/monitor.sh ports"
echo "  ./scripts/monitor.sh loopback /dev/ttyUSB0"
echo "  ./scripts/monitor.sh connect /dev/ttyUSB0 --baud 921600"
