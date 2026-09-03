# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this directory is

Zephyr firmware for CatSniffer v1.x/v2.x boards (ATSAMD21E17A/D, 128 KB
flash, 16 KB SRAM). It is a port of `RP2040/catsniffer/`: same three USB
CDC-ACM ports (Cat-Bridge, Cat-LoRa, Cat-Shell), same shell command table,
same `main.c` data flow, same native SX126x driver from the
`wero1414/zephyr` fork branch `fsk-native-driver`.

Read `RP2040/catsniffer/CLAUDE.md` first for the architecture, command
syntax and data flow. This file only lists what differs here.

## Differences from the RP2040 tree

- Board is out-of-tree at `boards/catsniffer_v2/` (HWMv2, SoC `samd21e17a`;
  the E17D is built as E17A). `CMakeLists.txt` appends the app dir to
  `BOARD_ROOT`. Pin map is the Electronic Cats Arduino `catsniffer` variant.
- Bootloader is uf2-samdx1: app linked at 0x2000 via
  `CONFIG_BOOTLOADER_BOSSA_ADAFRUIT_UF2`, output `zephyr.uf2` with family
  `0x68ed2b88`. `cmd_reboot` writes `0xf01669ef` to the top of `sram0` and
  cold-resets.
- No NVS/settings/flash partitions. `src/fw_metadata.c` is a RAM stub;
  `cc1352_fw_id set|clear` answer `ERR not supported on this board`.
- `src/flash_compat.c` and `src/cc26x2_algo.inc` are not present.
- RAM budget is the constraint. Ring buffers: bridge
  `CONFIG_CATSNIFFER_BRIDGE_RING_SIZE` (default 512) per direction, LoRa and
  shell 256 (`RING_BUF_SIZE_BRIDGE/LORA/SHELL` in `include/catsniffer.h`).
  Kernel, USB and LoRa thread stacks are 1 KB each. A production build
  leaves about 550 B unused; check the linker memory report after any
  change that adds static data.
- `prj.conf` sets `CONFIG_LORA_SX126X_NATIVE_SLEEP=n` (standby instead of
  sleep) because of a BUSY wakeup deadlock seen on first configure.
- `debug.conf` cannot enable full logging (does not fit). It gives printk
  and the thread analyzer over Segger RTT and shrinks the bridge buffers.

## Build

The `SAMD21/` directory may not be a complete west workspace. Build from
the workspace that owns `ZEPHYR_BASE`:

```bash
source ~/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=$HOME/zephyrproject/zephyr
cd $(dirname $ZEPHYR_BASE)
west build -p always -s <this dir> -d <this dir>/build -b catsniffer_v2
```

`scripts/catsniffer_build_flash_test.sh` does this for you (`-c`, `-cp`,
`-f --all`, `-t`, `--fw-version`). Flashing waits for a mounted drive that
contains `INFO_UF2.TXT` and copies the UF2 there.

## Keeping in sync with RP2040

`src/main.c`, `src/shell_commands.c`, `src/USB/usbd_init.c` and the headers
are copies of the RP2040 files with the deltas above. When a change lands
in `RP2040/catsniffer/src`, diff against this tree and port it, keeping the
buffer sizes, `cmd_reboot`, and the fw_metadata stub.
