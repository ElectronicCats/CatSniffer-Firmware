# SAMD21 CatSniffer v1/v2 Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the Zephyr port of the RP2040 CatSniffer firmware for the SAMD21E17A/D boards (v1.x/v2.x) so it builds, flashes over UF2, and passes the endpoint tests on hardware.

**Architecture:** The application at `SAMD21/catsniffer/` already exists on branch `feat/samd21-catsniffer-v2` (untracked files in `.worktrees/samd21-firmware/`). It mirrors `RP2040/catsniffer/`. This plan corrects the board pin map, fits the memory budget, finalizes the UF2 bootloader entry, drops NVS, adds tooling, and verifies on a board.

**Tech Stack:** Zephyr (fork `wero1414/zephyr` branch `fsk-native-driver`), west, Zephyr SDK 0.17.0, USB device stack next, native SX126x driver, uf2-samdx1 bootloader.

**Spec:** `docs/superpowers/specs/2026-04-13-samd21-catsniffer-v2-firmware-design.md`

**Status (2026-09-02):** all tasks done and verified on hardware. The bring-up
required changes beyond this plan (USB clock recovery, DMA UART RX with a
driver patch, shell moved to the main thread); see the spec's
"Implementation notes" section and `SAMD21/catsniffer/README.md`.

## Global Constraints

- SoC built as `samd21e17a`; E17D is assumed identical.
- App linked at 0x2000; `CONFIG_BOOTLOADER_BOSSA_ADAFRUIT_UF2=y`; UF2 family `0x68ed2b88`.
- No NVS, no settings, no storage partition.
- Shell command names and syntax identical to RP2040.
- Plain ASCII in all files, no AI attribution in commits, Conventional Commits messages.
- All builds run in `.worktrees/samd21-firmware/SAMD21/catsniffer` with `ZEPHYR_BASE=$HOME/zephyrproject/zephyr` and the venv at `~/zephyrproject/.venv` active.

Working directory for every command below: `/Users/wero1414/Documents/electronicCats/Fw/CatSniffer-Firmware/.worktrees/samd21-firmware`.

---

### Task 1: Correct the board pin map and partitions

**Files:**
- Modify: `SAMD21/catsniffer/boards/catsniffer_v2/catsniffer_v2-pinctrl.dtsi`
- Modify: `SAMD21/catsniffer/boards/catsniffer_v2/catsniffer_v2.dts`
- Modify: `SAMD21/catsniffer/boards/catsniffer_v2/board.cmake`

**Interfaces:**
- Produces: DT aliases `pin-reset`, `pin-boot`, `led0..led2`, `ctf1..ctf3`, `lora0`, chosen `uart-cc1352`, `zephyr,code-partition`. `main.c` consumes these unchanged.

- [x] **Step 1: Fix SERCOM3 MISO pin in pinctrl.** PA22 is SERCOM3 PAD0 on function C. Replace the `sercom3_spi_default` group with:

```dts
	sercom3_spi_default: sercom3_spi_default {
		group1 {
			pinmux = <PA22C_SERCOM3_PAD0>,  /* MISO */
				 <PA18D_SERCOM3_PAD2>,  /* MOSI */
				 <PA19D_SERCOM3_PAD3>;  /* SCK  */
		};
	};
```

Verify the macro exists: `grep -n "PA22C_SERCOM3_PAD0" $ZEPHYR_BASE/include/zephyr/dt-bindings/pinctrl/samd21*-pinctrl.h` and the include used by the dtsi. Fix the header comment to match.

- [x] **Step 2: Fix GPIO pins in the DTS.** In `gpio_pins`, set `ctf1_pin` to `<&porta 14 ...>`, `ctf2_pin` to `<&porta 11 ...>`, `ctf3_pin` stays `<&porta 10 ...>`. In the `sx1262` node replace the dummy reset with `reset-gpios = <&porta 8 GPIO_ACTIVE_LOW>;` and add `antenna-enable-gpios = <&porta 15 GPIO_ACTIVE_HIGH>;`. Delete the `sx1262_antsw` gpio-leds entry. Update the header comment block to the spec table.

- [x] **Step 3: Remove the storage partition** and make the code partition 120 KB:

```dts
		code_partition: partition@2000 {
			label = "code-partition";
			reg = <0x00002000 DT_SIZE_K(120)>;
			read-only;
		};
```

- [x] **Step 4: Build.** `west build -p always -b catsniffer_v2` from `SAMD21/catsniffer`. Expected: links, memory report printed. Note FLASH and SRAM usage.

- [x] **Step 5: Commit** `git add SAMD21/catsniffer/boards && git commit -m "fix(samd21): board pin map from Arduino variant, drop storage partition"`.

---

### Task 2: Memory budget

**Files:**
- Modify: `SAMD21/catsniffer/include/catsniffer.h:24-26`
- Modify: `SAMD21/catsniffer/src/main.c:25-30`
- Modify: `SAMD21/catsniffer/prj.conf`

**Interfaces:**
- Produces: `RING_BUF_SIZE_BRIDGE`, `RING_BUF_SIZE_LORA`, `RING_BUF_SIZE_SHELL` in `catsniffer.h`. `RING_BUF_SIZE` is removed; grep for other users first.

- [x] **Step 1:** In `catsniffer.h` replace `#define RING_BUF_SIZE 256` with:

```c
// Ring buffer sizes, sized for 16 KB SRAM (see spec memory budget)
#define RING_BUF_SIZE_BRIDGE 1024
#define RING_BUF_SIZE_LORA 256
#define RING_BUF_SIZE_SHELL 256
#define COMMAND_BUF_SIZE 256
```

- [x] **Step 2:** In `main.c` declare the six arrays with the matching sizes (bridge pair 1024, sx1262 pair LORA, config pair SHELL). `ring_buf_init` uses `sizeof`, so no other change. `grep -rn RING_BUF_SIZE SAMD21/catsniffer/src SAMD21/catsniffer/include` must show only the new names.

- [x] **Step 3:** Build and read the memory report. Required: SRAM used <= 15 KB (at least 1 KB free). If not, set `RING_BUF_SIZE_BRIDGE` to 512 and rebuild.

- [x] **Step 4: Commit** `git commit -am "feat(samd21): asymmetric ring buffers for 16 KB SRAM"`.

---

### Task 3: Bootloader entry and unsupported storage messages

**Files:**
- Modify: `SAMD21/catsniffer/src/shell_commands.c` (`cmd_reboot`, `cmd_cc1352_fw_id`)
- Modify: `SAMD21/catsniffer/src/fw_metadata.c` (header comment only)

- [x] **Step 1:** Replace `cmd_reboot` body with the DT-derived address:

```c
#define UF2_DOUBLE_TAP_MAGIC 0xf01669efUL

static void cmd_reboot(char *args)
{
	uint32_t *sram_top = (uint32_t *)(DT_REG_ADDR(DT_NODELABEL(sram0)) +
					  DT_REG_SIZE(DT_NODELABEL(sram0)));

	shell_reply("Entering UF2 bootloader...\r\n");
	k_msleep(100);
	/* uf2-samdx1 checks this word after reset and stays in bootloader */
	sram_top[-1] = UF2_DOUBLE_TAP_MAGIC;
	sys_reboot(SYS_REBOOT_COLD);
}
```

Ensure `#include <zephyr/devicetree.h>` is present (it is via `catsniffer.h`).

- [x] **Step 2:** In `cmd_cc1352_fw_id`, change the three `"ERR storage unavailable\r\n"` replies to `"ERR not supported on this board\r\n"`. `get` keeps returning `OK cc1352_fw_id=unset`.

- [x] **Step 3:** Build. Expected: success, no warnings in app sources.

- [x] **Step 4: Commit** `git commit -am "feat(samd21): UF2 reboot via sram0 node, storage not-supported replies"`.

---

### Task 4: Configuration files and workspace hygiene

**Files:**
- Modify: `SAMD21/catsniffer/prj.conf`
- Create: `SAMD21/catsniffer/debug.conf`
- Modify: `.gitignore` (repo root)

- [x] **Step 1:** `prj.conf`: add `CONFIG_CONSOLE=n`, `CONFIG_UART_CONSOLE=n`, `CONFIG_PRINTK=n` next to `CONFIG_LOG=n`. Remove the em-dash from the `CONFIG_LORA_SX126X_NATIVE_SLEEP` comment (ASCII only).

- [x] **Step 2:** Create `debug.conf`:

```
CONFIG_DEBUG_OPTIMIZATIONS=y
CONFIG_LOG=y
CONFIG_PRINTK=y
CONFIG_APP_LOG_LEVEL_DBG=y
CONFIG_THREAD_ANALYZER=y
CONFIG_THREAD_ANALYZER_AUTO=y
CONFIG_THREAD_ANALYZER_AUTO_INTERVAL=30
CONFIG_THREAD_NAME=y
```

- [x] **Step 3:** Append to root `.gitignore`:

```
# west workspace clones next to the SAMD21 application
SAMD21/zephyr/
SAMD21/modules/
SAMD21/bootloader/
SAMD21/tools/
SAMD21/.west/
```

- [x] **Step 4:** Build normally and with `-- -DEXTRA_CONF_FILE=debug.conf`. Debug build may exceed flash; if it does, drop `CONFIG_DEBUG_OPTIMIZATIONS` from `debug.conf` and note it in the README.

- [x] **Step 5: Commit** `git add .gitignore SAMD21/catsniffer/prj.conf SAMD21/catsniffer/debug.conf && git commit -m "chore(samd21): production config, debug fragment, ignore workspace clones"`.

---

### Task 5: Tooling and docs

**Files:**
- Create: `SAMD21/catsniffer/scripts/catsniffer_build_flash_test.sh` (copy of RP2040 script with edits)
- Create: `SAMD21/catsniffer/scripts/verify_endpoints.py` (symlink or copy of RP2040 script)
- Create: `SAMD21/catsniffer/scripts/validate_fw_version.sh` (copy)
- Create: `SAMD21/catsniffer/README.md`
- Create: `SAMD21/catsniffer/CLAUDE.md`

- [x] **Step 1:** Copy the three scripts. In the build script change `rpi_pico` to `catsniffer_v2` (three `west build` lines) and replace the mount detection: `MOUNT_GLOB` becomes `/Volumes/*` (macOS) and `/media/$LINUX_USER/*` (Linux), and the wait loop accepts a directory only if it contains `INFO_UF2.TXT`. Update the usage text (`RPI-RP2` mentions).

- [x] **Step 2:** Write `README.md` (short: what the board is, pin table, build/flash steps, differences from RP2040: no NVS, buffer sizes, UF2 entry) and `CLAUDE.md` (same shape as the RP2040 one, pointing out the shared sources and the SAMD21 deltas).

- [x] **Step 3:** `bash scripts/catsniffer_build_flash_test.sh -c` succeeds.

- [x] **Step 4: Commit** `git add SAMD21/catsniffer/scripts SAMD21/catsniffer/README.md SAMD21/catsniffer/CLAUDE.md && git commit -m "docs(samd21): build script, README, CLAUDE.md"`.

---

### Task 6: Commit the rest of the application and the docs

- [x] **Step 1:** `git add SAMD21/catsniffer docs/superpowers` (build dir is ignored). `git status` must not list `SAMD21/zephyr` or `SAMD21/.west`.
- [x] **Step 2:** `git commit -m "feat(samd21): Zephyr firmware for CatSniffer v1/v2 (SAMD21E17)"`.

---

### Task 7: Hardware verification

Requires a CatSniffer v1/v2 with the UF2 bootloader plugged into this Mac.

- [x] **Step 1:** Double-tap reset. `ls /Volumes` shows a drive with `INFO_UF2.TXT`. Record its name.
- [x] **Step 2:** `cp build/zephyr/zephyr.uf2 /Volumes/<drive>/`. Board re-enumerates.
- [x] **Step 3:** `ioreg -p IOUSB -l | grep -E 'Product Name|idVendor|idProduct'` shows VID 4617 (0x1209) PID 47803 (0xBABB). `ls /dev/cu.usbmodem*` shows three new ports.
- [x] **Step 4:** `python3 scripts/verify_endpoints.py --test-all` passes.
- [x] **Step 5:** On Cat-Shell: `boot`, then `python3 -m serial.tools.miniterm` is not needed; run `cc2538-bsl.py -p <Cat-Bridge> -i` if available, otherwise send `boot` and observe the CC1352 LED pattern, then `exit`. Record the result.
- [x] **Step 6:** `status` after 2 minutes of traffic; record `uart_overrun` and `ring_overflow` counters.
- [x] **Step 7:** `reboot` on Cat-Shell; the UF2 drive reappears.
- [x] **Step 8:** Record all results in `SAMD21/catsniffer/README.md` under "Verified on hardware" and commit: `git commit -am "docs(samd21): hardware verification results"`.
