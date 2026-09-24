# CatSniffer Firmware for v1.x / v2.x boards (SAMD21, Zephyr)

Port of the RP2040 v3 firmware (`RP2040/catsniffer/`) to the ATSAMD21E17A/D
used on CatSniffer v1.x and v2.x. Same three USB serial ports, same shell
commands, same LoRa/FSK features, same host tooling.

## Hardware

- MCU: ATSAMD21E17A or E17D, 128 KB flash, 16 KB SRAM, 48 MHz Cortex-M0+,
  no crystal (DFLL48M locked to the USB start-of-frame)
- CC1352P on SERCOM0 UART (PA04 TX, PA05 RX), 921600 baud, RX via DMA
- SX1262 on SERCOM3 SPI (PA22 MISO, PA18 MOSI, PA19 SCK, PA17 CS),
  BUSY PA02, DIO1 PA03, RESET PA08, ANT_SW PA15
- RF switch CTF1/CTF2/CTF3 on PA14/PA11/PA10
- LEDs on PA27/PA28/PA00, CC1352 reset PA01, CC1352 boot PA07
- UF2 bootloader (uf2-samdx1 v3.6.0, drive name `SNIFFER`) in the first
  8 KB, application at 0x2000

The pin map comes from the `catsniffer` variant of the Electronic Cats
Arduino SAMD core and applies to both v1.x and v2.x.

## USB ports

| Port | Purpose |
|---|---|
| Cat-Bridge | Transparent bridge to the CC1352 (921600, or 500000 after `boot`) |
| Cat-LoRa | SX1262 data, stream or command mode |
| Cat-Shell | Text command shell, see `help` |

VID 0x1209, PID 0xBABB. Command reference: `RP2040/catsniffer/README.md`.

## Differences from the RP2040 firmware

- No NVS. `cc1352_fw_id set|clear` reply `ERR not supported on this board`;
  `get` reports `unset`; `list` works.
- `reboot` enters the UF2 bootloader by writing the double-tap magic to the
  top of SRAM and resetting.
- The CC1352 UART receive path uses DMA (`src/main.c`, async UART API).
  The SERCOM has no RX FIFO, so a per-byte interrupt cannot keep up at
  921600 baud on this core. This needs the Zephyr driver changes in
  `../zephyr-patches/` (see Build).
- Shell commands run in the main thread (`shell_poll()` in `main.c`), not
  in the USB callback. The CDC callback only queues bytes.
- Buffers are small to fit 16 KB SRAM: Cat-Bridge 256 B per direction
  (`CONFIG_CATSNIFFER_BRIDGE_RING_SIZE`), LoRa 264 B (one full packet),
  shell 128 B with chunked, blocking replies. `status` shows UART overrun
  and ring-drop counters. Ring drops are expected whenever the host is not
  reading Cat-Bridge while the CC1352 transmits.
- `status` also prints unused stack per thread and a crash log kept in
  no-init RAM (`src/fault_log.c`): a fatal error records reason/PC/LR and
  resets the board instead of halting.
- Logging is off. `debug.conf` adds a printk console over
  Segger RTT (SWD) with the bridge buffers reduced to 128 B; stack headroom
  is already in `status`.

## Build

Requires the same toolchain as the RP2040 firmware: Zephyr SDK 0.17.0, the
`wero1414/zephyr` fork with the native SX126x driver, and west in
`~/zephyrproject/.venv`.

The fork must also contain the SAM0 driver changes in
`SAMD21/zephyr-patches/0001-sam0-uart-dma-continuous-rx.patch`
(continuous DMA RX mode for `uart_sam0` over a cyclic DMA channel, and
cyclic/suspend/resume support plus write-back seeding in `dma_sam0`). Apply with `git am` or `git apply` inside the Zephyr tree if
your checkout does not have them.

```bash
source ~/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=$HOME/zephyrproject/zephyr

# From a west workspace that contains ZEPHYR_BASE:
cd $(dirname $ZEPHYR_BASE)
west build -p always -s <repo>/SAMD21/catsniffer -d <repo>/SAMD21/catsniffer/build -b catsniffer_v2

# Or use the wrapper (handles the workspace detail):
bash scripts/catsniffer_build_flash_test.sh -c       # compile
bash scripts/catsniffer_build_flash_test.sh -cp      # pristine compile
bash scripts/catsniffer_build_flash_test.sh          # compile + flash + test
bash scripts/catsniffer_build_flash_test.sh -f --all # flash every UF2 drive

# Debug variant (RTT console)
west build -p always -s ... -d .../build-debug -b catsniffer_v2 -- -DEXTRA_CONF_FILE=debug.conf
```

CI builds this firmware on every push and pull request (`firmware-ci.yml`,
job `build-samd21`) from `west-ci.yml`, a minimal manifest pinned to the fork
branch `sx1262-rssi` that carries the SAM0 driver changes. Tags `v2.X.Y.Z`
publish a release with `catsniffer-v2.X.Y.Z.uf2` (`firmware-release.yml`).

Alternatively initialize a dedicated workspace once with
`west init -l SAMD21/catsniffer --mf west.yml && west update` from the repo
root, after which plain `west build -b catsniffer_v2` works from this
directory.

Memory report for the production build (2026-09-02):

| Region | Used | Size |
|---|---|---|
| FLASH | 80012 B | 120 KB |
| RAM | 16212 B | 16384 B |

Stack headroom measured with `status` after exercising all commands: main
168 B unused of 1536, LoRa thread 304 of 1024, ISR 436 of 768. Check these
numbers after any change that adds locals to a shell command.

## Flash

1. Double-tap RESET (or send `reboot` to Cat-Shell). The board mounts as a
   USB drive named `SNIFFER` containing `INFO_UF2.TXT`.
2. Copy `build/zephyr/zephyr.uf2` onto it. The board resets into the new
   firmware.

## Test

```bash
pip install pyusb pyserial
python3 scripts/verify_endpoints.py
python3 scripts/verify_endpoints.py --test-all
```

## Verified on hardware (2026-09-02)

CatSniffer v2 with uf2-samdx1 v3.6.0 and a CC1352 running TI Packet
Sniffer 1.8.0, on macOS:

- Enumerates as 0x1209:0xBABB with three CDC ports.
- `verify_endpoints.py --test-all`: all tests pass.
- Cat-Bridge RX: CC1352 banner received intact, `uart_overrun=0` while the
  host drains the port.
- `boot`, BSL sync (0x55 0x55 -> 0x00 0xCC at 500000 baud) and `exit`
  repeated three times without a hang.
- `band1..3`, `modulation fsk|lora`, `lora_config`, `fsk_config`, `help`
  (45 lines), `identify`, `cc1352_fw_id` all reply.
- `reboot` re-enters the UF2 bootloader.
- LoRa and FSK over the air against a CatSniffer v3 (2026-09-03), both
  directions, `scripts/lora_ota_test.py`: all four transfers received,
  RSSI -28 to -46 dBm on the bench.
- CC1352P1 flashed through Cat-Bridge with `cc2538-bsl` at 500000 baud:
  352 KB backup read (10.5 min, tool-bound) and Sniffle written and CRC
  verified in 7.5 s, zero UART overruns and zero ring drops in both
  directions.
- Sniffle end to end (2026-09-03): upstream `python_cli/sniff_receiver.py`
  on the v2's Cat-Bridge captured 1833 BLE advertising frames in 25 s to a
  pcap; tshark dissects them (dozens of advertisers, RSSI -59 to -75 dBm).
- Sniffle at 921600 streaming into the bridge: zero SERCOM overruns and zero
  DMA regressions over repeated 30 s runs; a full-rate (92 KB/s) stream was
  received intact for 30 s.

Known limit: when the host does not drain Cat-Bridge fast enough, bytes are
dropped at the 256 B ring (`ring_dropped` in `status`). On macOS with pyserial
this was about 2 KB per 30 s of bursty Sniffle traffic. There is no RAM left
for a larger ring; the RP2040 v3 has 16 KB there.

Notes for Sniffle on v2: use the `sniffle_cc1352p1_cc2652p1_1M.hex` build;
it talks at 921600 on this board. Flash with the legacy `cc2538-bsl.py`
after converting the hex to a binary (the tool needs `python-magic` to accept
hex files).
