# Cybie Serial Commands

Reference commands for running `m6809-run` with a host serial link.

## Build

```bash
autoreconf -fi
./configure
make -j2
```

## Run with Cybie Serial Bridge

Use `eon2` because it maps the emulated serial device.

```bash
./src/m6809-run -s eon2 --serial-device=/dev/ttyUSB0 --serial-baud=38400 <program.srec>
```

Common alternative baud:

```bash
./src/m6809-run -s eon2 --serial-device=/dev/ttyUSB0 --serial-baud=9600 <program.srec>
```

## Debug + Serial

```bash
./src/m6809-run -d -s eon2 --serial-device=/dev/ttyUSB0 --serial-baud=38400 <program.srec>
```

## Monitor Serial Helpers

From the `(dbg)` prompt:

```text
serstat
serrx 0x55 0xAA
sertx on
```

## Notes

- If the serial device cannot be opened/configured, the emulator falls back to stdin/stdout.
- Supported baud rates: `1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200`.
- Linux serial devices are commonly `/dev/ttyUSB0` or `/dev/ttyACM0`.


## Host Monitor (C)

Use the native C host monitor utility:

```bash
./src/exec09-monitor ports
./src/exec09-monitor diag
./src/exec09-monitor connect /dev/ttyUSB0 --baud 921600
./src/exec09-monitor loopback /dev/ttyUSB0
./src/exec09-monitor transceive /dev/ttyUSB0 00 16 --wait-ms 80
```

Short wrappers:

```bash
./scripts/monitor.sh ports
./scripts/monitor-easy.sh
```
