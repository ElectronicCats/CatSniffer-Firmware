# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this directory is

Zephyr firmware for CatSniffer v1.x/v2.x boards (ATSAMD21E17A/D, 128 KB
flash, 16 KB SRAM). It is a port of `RP2040/catsniffer/`: same three USB
CDC-ACM ports (Cat-Bridge, Cat-LoRa, Cat-Shell), same shell command table,
same LoRa/FSK code, same native SX126x driver from the `wero1414/zephyr`
fork.

Read `RP2040/catsniffer/CLAUDE.md` first for the architecture, command
syntax and data flow. This file only lists what differs here. The port was
verified on hardware on 2026-09-02 (see README "Verified on hardware").

## Differences from the RP2040 tree

- Board is out-of-tree at `boards/catsniffer_v2/` (HWMv2, SoC `samd21e17a`;
  the E17D is built as E17A and works). `CMakeLists.txt` appends the app dir
  to `BOARD_ROOT`. Pin map is the Electronic Cats Arduino `catsniffer`
  variant (SX1262 MISO PA22, RESET PA08, ANT_SW PA15, CTF1/2/3 on
  PA14/PA11/PA10).
- Clock: the board has no crystal. `src/clock_usbcrm.c` re-locks the DFLL48M
  to the USB SOF (USBCRM) at PRE_KERNEL_1, the same way the Arduino core
  does. Without it the OSC8M-referenced DFLL is about 1% off and USB
  enumeration fails on the configuration descriptor.
- Bootloader is uf2-samdx1: app linked at 0x2000 via
  `CONFIG_BOOTLOADER_BOSSA_ADAFRUIT_UF2`, output `zephyr.uf2`, drive name
  `SNIFFER`. `cmd_reboot` writes `0xf01669ef` to the top of `sram0` and
  cold-resets.
- CC1352 UART RX is DMA/async (`cc1352_uart_async_cb` in `main.c`, one
  256 B buffer, 4 ms timeout); TX stays interrupt driven. The fork's
  continuous mode runs a cyclic DMA over that buffer (self-linked descriptor,
  never stops at the block end) and reads progress from the write-back
  descriptor every 1 ms without pausing the channel. The SERCOM buffers only
  one byte time (11 us at 921600), so never pause the channel and keep
  interrupt-locked sections short. `status` shows `uart_overrun` (real
  SERCOM overruns) and `dma_regress` (stale progress reads, should stay 0). This needs
  `CONFIG_UART_SAM0_ASYNC_RX_CONTINUOUS` and `CONFIG_UART_EXCLUSIVE_API_CALLBACKS=n`
  (otherwise registering the async callback erases the TX callback and the
  DRE interrupt storms). The driver mode lives in the fork; the patch is in
  `../zephyr-patches/`.
- Shell commands execute in the main thread (`shell_poll()`), not in the CDC
  callback. On this build the CDC class uses the system work queue; blocking
  or deep calls from the callback corrupt that 1 KB stack. Keep the CDC
  handlers to ring-buffer moves only.
- `shell_reply()` writes in chunks and waits (bounded) for USB to drain,
  because the shell ring is 128 B and replies like `help` are 1.8 KB.
- No NVS/settings/flash partitions. `src/fw_metadata.c` is a RAM stub;
  `cc1352_fw_id set|clear` answer `ERR not supported on this board`.
- `src/fault_log.c`: fatal errors record reason/PC/LR in no-init RAM and
  reset; `status` prints the log, a small event trace, and unused stack per
  thread (`CONFIG_INIT_STACKS`, `CONFIG_THREAD_MONITOR`). Use these before
  guessing about memory problems; there is no SWD console in the default
  build.
- RAM budget: production build leaves about 170 B unused. Rings: bridge
  `CONFIG_CATSNIFFER_BRIDGE_RING_SIZE` (256) per direction, LoRa 264, shell
  128. Stacks: main 1536, LoRa 1024, ISR 768, sysworkq 1024, usbd 1024.
  Check the linker report and `status` stack numbers after any change.
- `prj.conf` sets `CONFIG_LORA_SX126X_NATIVE_SLEEP=n` (standby instead of
  sleep) because of a BUSY wakeup deadlock seen on first configure.
- `debug.conf` cannot enable full logging (does not fit). It gives a printk
  console over Segger RTT and shrinks the bridge buffers.

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
contains `INFO_UF2.TXT` and copies the UF2 there. If the board is running
this firmware, the script sends `reboot` first; if it is hung, double-tap
RESET.

## Debugging without a probe

1. `status` first: crash log, trace, stack headroom, loss counters.
2. If the board hangs (all CDC ports stop answering, one LED solid), it is
   usually an interrupt storm or memory corruption, not a fault. Add
   `trace_event()` calls (codes in `catsniffer.h`) around the suspect path,
   reflash the same build after a double-tap, and read the trace back:
   no-init RAM survives the bootloader.

## Keeping in sync with RP2040

`src/shell_commands.c`, `src/USB/usbd_init.c` and the headers are copies of
the RP2040 files with the deltas above. `src/main.c` differs in the CC1352
UART path, `shell_reply()`, `shell_poll()` and buffer sizes. When a change
lands in `RP2040/catsniffer/src`, diff against this tree and port it,
keeping those deltas.
