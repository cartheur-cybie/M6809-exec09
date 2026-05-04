# M6809-exec09

`m6809-run` is a multi-machine Motorola 6809 emulator with an interactive debugger and optional host serial bridging.

## What This Repo Is

- A 6809 CPU emulator plus machine models (`simple`, `eon`, `eon2`, `wpc`, `multicomp09`, `smii`, `kipper1`, `coco`).
- A monitor/debugger with breakpoints, watchpoints, stepping, disassembly, and expression evaluation.
- A serial-capable path for external hardware workflows (for example Cybie serial links via `eon2`).

## Prerequisites

On Debian/Ubuntu:

```bash
sudo apt install make gcc autoconf automake libtool libreadline-dev
```

`libreadline-dev` is optional but recommended for a better debugger prompt experience.

## Build

```bash
autoreconf -fi
./configure
make -j2
```

Binary output:

- `./src/m6809-run`

## Quick Start

Run an S-record program on the default machine:

```bash
./src/m6809-run <program.srec>
```

Run with debugger enabled immediately:

```bash
./src/m6809-run -d <program.srec>
```

Show runtime options:

```bash
./src/m6809-run --help
```

## Machine Selection

Select a machine using `-s` / `--machine`:

```bash
./src/m6809-run -s eon2 <program.srec>
```

Practical summary:

- `simple`: minimal 64KB-style setup and basic I/O mapping.
- `eon` / `eon2`: extended memory/I/O models; `eon2` includes a serial device in the I/O expander.
- `wpc`, `multicomp09`, `smii`, `kipper1`, `coco`: specialized hardware maps.

## Cybie/Host Serial Bridge

When using a serial-capable machine (typically `eon2`), bind the emulated serial device to a host TTY:

```bash
./src/m6809-run -s eon2 --serial-device=/dev/ttyUSB0 --serial-baud=38400 <program.srec>
```

Supported serial baud values:

- `1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200`

If the serial device cannot be opened/configured, the emulator falls back to stdin/stdout.

For command presets and examples, see:

- [CYBIE_COMMANDS.md](CYBIE_COMMANDS.md)

## Debugger Commands (Core Set)

At the `(dbg)` prompt, commonly used commands are:

- `h` or `?`: help
- `b <addr>`: breakpoint
- `wa <addr>` / `rwa <addr>` / `awa <addr>`: watchpoints
- `bl`: list breakpoints/watchpoints
- `d <id>`: delete breakpoint/watchpoint
- `c`: continue
- `s [count]`: step
- `n`: next
- `l [addr]`: disassemble
- `x [format] <expr>`: examine memory
- `p [format] <expr>`: print expression
- `regs`: show CPU registers
- `pc <expr>`: set PC
- `re`: reset
- `so <file>`: run debugger script
- `q`: quit emulator

Serial-oriented monitor helpers:

- `serstat`: serial queue/trace status
- `serrx <byte...>`: inject RX bytes into emulated serial queue
- `sertx [on|off]`: serial TX trace toggle

## Cleaning Generated Files

```bash
git clean -fX
```

To also remove untracked directories:

```bash
git clean -fd
```
