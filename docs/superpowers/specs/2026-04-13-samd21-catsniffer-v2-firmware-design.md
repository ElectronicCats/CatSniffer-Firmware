# CatSniffer v1/v2 SAMD21 Zephyr Firmware Design

**Date:** 2026-04-13 (revised 2026-09-02)
**Target:** CatSniffer v1.x/v2.x boards, ATSAMD21E17A/E17D (128 KB flash, 16 KB SRAM)
**Purpose:** Port the RP2040 v3 firmware to the SAMD21 boards with the same
command set and USB layout, as close to the RP2040 sources as the smaller
chip allows.

## Overview

A second Zephyr application at `SAMD21/catsniffer/` that mirrors
`RP2040/catsniffer/`: same west manifest (custom Zephyr fork with the native
SX126x driver), same three USB CDC-ACM ports, same shell command table, same
`main.c` data flow. The only intended differences are the board definition,
the bootloader entry sequence, buffer sizes, and the absence of NVS storage.

The SoC is built as `samd21e17a`. The E17D variant is treated as identical
(same memory map, same peripherals). This assumption is validated on hardware,
not by documentation.

## Architecture

Identical to the RP2040 firmware:

```
Host PC
  Cat-Bridge (CDC0) <-> ring_buf <-> SERCOM0 UART ISR <-> CC1352P
  Cat-LoRa   (CDC1) <-> ring_buf <-> lora_thread <-> SX1262 (SERCOM3 SPI)
  Cat-Shell  (CDC2) <-> ring_buf <-> main loop -> process_command()
```

Ring buffers are accessed through the `safe_ring_buf_*` wrappers with
`irq_lock()`. A dedicated `lora_thread` handles async LoRa/FSK RX.

## Directory layout

```
SAMD21/catsniffer/
  src/
    main.c              same as RP2040 except buffer/stack sizes
    shell_commands.c    same as RP2040 except cmd_reboot and cc1352_fw_id reply
    fw_metadata.c       RAM-only stub, no persistence
    USB/usbd_init.c     same as RP2040
  include/              same as RP2040 (catsniffer.h has smaller RING_BUF sizes)
  boards/catsniffer_v2/ HWMv2 out-of-tree board
  prj.conf, CMakeLists.txt, west.yml, VERSION, Kconfig, debug.conf
```

Not carried over: `src/flash_compat.c` (RP2040 flash shim) and
`src/cc26x2_algo.inc` (unreferenced blob).

## Board definition: `catsniffer_v2`

### Pin assignments

Source of truth: the `catsniffer` variant of the Electronic Cats Arduino SAMD
core (`variants/catsniffer/variant.cpp`), which is the shipping definition for
v1.x and v2.x. Both board generations share this pinout.

| Function | Pin | Zephyr use |
|---|---|---|
| LED1 / LED2 / LED3 | PA27 / PA28 / PA00 | gpio-leds, aliases led0/led1/led2 |
| CC1352 RESET (RTS on schematic) | PA01 | alias pin-reset |
| CC1352 BOOT (CTS on schematic) | PA07 | alias pin-boot |
| CC1352 UART TX / RX | PA04 / PA05 | SERCOM0 PAD0 / PAD1 (function D) |
| SX1262 BUSY | PA02 | busy-gpios |
| SX1262 DIO1 | PA03 | dio1-gpios |
| SX1262 RESET | PA08 | reset-gpios, active low |
| SX1262 ANT_SW | PA15 | antenna-enable-gpios |
| SX1262 CS | PA17 | SERCOM3 cs-gpios, active low |
| SX1262 MOSI | PA18 | SERCOM3 PAD2 (function D), DOPO 1 |
| SX1262 SCK | PA19 | SERCOM3 PAD3 (function D), DOPO 1 |
| SX1262 MISO | PA22 | SERCOM3 PAD0 (function C), DIPO 0 |
| CTF1 / CTF2 / CTF3 (RF switch) | PA14 / PA11 / PA10 | aliases ctf1/ctf2/ctf3 |
| USB D- / D+ | PA24 / PA25 | function G |
| SWCLK / SWDIO | PA30 / PA31 | debug |

Pins DIO2 (PA06), DIO3 (PA09) and PA16/PA23 exist in the variant but are not
used by this firmware.

### Peripherals

- SERCOM0 UART to the CC1352 at 921600 baud, no flow control. Baud switches
  to 500000 in `boot` mode exactly as on RP2040.
- SERCOM3 SPI to the SX1262 at 1 MHz, chip select as GPIO.
- Native USB device controller with three `zephyr,cdc-acm-uart` children,
  each with 128-byte USB FIFOs.

### Clocks

DFLL48M from the internal OSC8M (`CONFIG_SOC_ATMEL_SAMD_OSC8M_AS_MAIN`),
48 MHz core clock. USB clock recovery is handled by the SoC layer.

### Flash layout

```
0x00000000 - 0x00001FFF   UF2 bootloader (8 KB, read-only partition)
0x00002000 - 0x0001FFFF   application (120 KB, zephyr,code-partition)
```

No storage partition. `CONFIG_BOOTLOADER_BOSSA=y` plus
`CONFIG_BOOTLOADER_BOSSA_ADAFRUIT_UF2=y` link the image at 0x2000 through
`USE_DT_CODE_PARTITION`. `CONFIG_BUILD_OUTPUT_UF2=y` with the SAMD21 family
id `0x68ed2b88` (Zephyr default for the series) produces `zephyr.uf2`.

## Bootloader

Adafruit-style `uf2-samdx1` bootloader, already present on the boards.
Entry paths:

1. Double-tap reset: the board mounts as a USB mass storage drive.
2. `reboot` shell command: writes the double-tap magic `0xf01669ef` to the
   last word of SRAM (address taken from the `sram0` devicetree node, the
   same way Zephyr's `soc/atmel/sam0/common/bossa.c` does it) and performs a
   cold reboot. Zephyr's own bossa helper only implements the 1200 baud touch
   on the legacy USB stack, so it is not used.

Flashing is a copy of `build/zephyr/zephyr.uf2` to the mounted drive. The
drive name is whatever the installed bootloader reports; the build script
matches any mount that contains `INFO_UF2.TXT`.

## Memory budget (16 KB SRAM)

The RP2040 build allocates 96 KB of ring buffers, so sizes are reduced:

| Buffer | Size |
|---|---|
| cc1352 -> usb (Cat-Bridge RX) | 512 (`CONFIG_CATSNIFFER_BRIDGE_RING_SIZE`) |
| usb -> cc1352 (Cat-Bridge TX) | 512 (same option) |
| sx1262 -> usb, usb -> sx1262 | 256 each |
| config -> usb, usb -> config | 256 each |
| LoRa thread stack | 1024 |
| main / ISR / system workqueue / USB stacks | 1024 each |

`COMMAND_BUF_SIZE` stays 256. Measured on 2026-09-02: the production build
uses 15832 of 16384 bytes (552 free). The original target of 1 KB free was
not reachable with 1 KB bridge buffers (the six kernel/USB/LoRa stacks alone
take 6 KB), so the bridge buffers are 512 B and exposed as a Kconfig option.
All RAM use is static, so the unused remainder is idle by construction. The
loss counters exposed by `status` are the measurement for whether the bridge
buffers are adequate at 921600 baud.

## Shell commands

Same table and syntax as RP2040 (`help`, `boot`, `exit`, `band1..3`,
`modulation`, `lora_*`, `fsk_*`, `radio`, `status`, `loss_reset`,
`fw_version`, `identify`, `reboot`, `cc1352_fw_id`).

Differences:

- `reboot` enters the UF2 bootloader and its help text says so.
- `cc1352_fw_id set|get|clear` reply `ERROR: not supported on this board`
  (there is no NVS). `cc1352_fw_id list` still prints the official IDs so the
  host tooling behaves the same.

## Configuration (prj.conf)

Same as RP2040 minus flash/NVS/settings, plus:

- `CONFIG_REBOOT=y` for `sys_reboot()`
- `CONFIG_LORA_SX126X_NATIVE_SLEEP=n` (radio kept in standby; the worktree
  found a BUSY wakeup deadlock on the first configure otherwise)
- Reduced stack sizes listed above
- `CONFIG_LOG=n`, `CONFIG_PRINTK=n`, `CONFIG_CONSOLE=n`; `debug.conf` turns
  logging on

## Build and flash

```bash
# one-time, from the repo root
west init -l SAMD21/catsniffer --mf west.yml
west update
# ZEPHYR_BASE may also point at an existing checkout of the same fork

cd SAMD21/catsniffer
west build -b catsniffer_v2
west build -p always -b catsniffer_v2

# flash: double-tap reset or send "reboot" to Cat-Shell, then
cp build/zephyr/zephyr.uf2 /Volumes/<UF2 drive>/
```

`scripts/catsniffer_build_flash_test.sh` is copied into `SAMD21/catsniffer/`
with the board name and UF2 drive detection changed. `verify_endpoints.py` is
reused as-is (same VID/PID, same port labels).

## Verification

1. Pristine build succeeds; linker report shows FLASH under 120 KB and SRAM
   with at least 1 KB free.
2. Board enumerates as VID 0x1209 PID 0xBABB with three CDC ports labeled
   Cat-Bridge, Cat-LoRa, Cat-Shell.
3. `python3 scripts/verify_endpoints.py --test-all` passes.
4. `boot` then `cc2538-bsl -p <Cat-Bridge> -i` (or a raw sync) gets a
   response from the CC1352 bootloader at 500000 baud; `exit` restores
   passthrough.
5. `reboot` re-enters the UF2 bootloader.
6. `status` after several minutes of sniffing traffic shows the UART loss
   counters; non-zero counts are reported, not hidden.

## Risks

- USB next stack on the SAM0 UDC: the worktree already linked with
  `udc_sam0`, so the remaining risk is runtime. Fallback is the legacy stack
  with a rewritten `usbd_init.c`.
- E17D vs E17A: unknown errata differences. Detected by the hardware tests.
- SRAM: the budget above leaves little headroom. Overflows show as hard faults
  during LoRa config; `debug.conf` plus `CONFIG_THREAD_ANALYZER` is the tool
  to measure stack use if that happens.

## Implementation notes from hardware bring-up (2026-09-02)

These supersede the sections above where they differ.

- Clock: the board is crystal-less. Zephyr's SoC init locks the DFLL48M to
  OSC8M (about 1% error), which is outside the USB tolerance; the host read
  the device descriptor but the configuration descriptor failed.
  `src/clock_usbcrm.c` re-locks the DFLL to the USB SOF (USBCRM) at
  PRE_KERNEL_1, mirroring the Arduino core.
- CC1352 UART RX: the SERCOM has no RX FIFO; the interrupt-driven path lost
  about half the bytes at 921600 baud. RX now uses DMA through the async
  UART API. The stock `uart_sam0` timeout mode stops the DMA every tick and
  restarts it from the next RXC interrupt, which still lost 2-4 bytes per
  restart, so the fork gained `CONFIG_UART_SAM0_ASYNC_RX_CONTINUOUS`
  (DMA restarted immediately, data flushed every timeout/4) plus a DMA
  write-back seeding fix. Patch: `SAMD21/zephyr-patches/`.
- `CONFIG_UART_EXCLUSIVE_API_CALLBACKS=n` is required: with the default,
  `uart_callback_set()` erases the interrupt-driven TX callback and the
  first TX interrupt storms (board hangs, all ports dead, no fault).
- Shell commands now run in the main thread (`shell_poll()`); the CDC
  callback only queues bytes. With no dedicated CDC work queue the class
  runs on the 1 KB system work queue, and running `boot` (300 ms of sleeps)
  or `status` there overflowed that stack.
- `shell_reply()` is chunked and blocking (bounded 500 ms) so replies larger
  than the 128 B shell ring are delivered whole.
- Diagnostics added: `src/fault_log.c` (crash log and event trace in
  no-init RAM, survive the UF2 bootloader), stack headroom per thread and
  the trace in `status`.

Final memory budget (production build): FLASH 79572 B of 120 KB, RAM
16076 B of 16384 B. Rings: bridge 256 x2, LoRa 264 x2, shell 128 x2.
Stacks: main 1536, LoRa 1024, ISR 768, sysworkq 1024, usbd 1024,
udc_sam0 512. Measured headroom after exercising every command: main 168,
LoRa 304, ISR 436 bytes.

Verification results are recorded in `SAMD21/catsniffer/README.md`.
