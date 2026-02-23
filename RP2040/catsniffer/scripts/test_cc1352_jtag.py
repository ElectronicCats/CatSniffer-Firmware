#!/usr/bin/env python3
"""
Automated CC1352 JTAG smoke test over CatSniffer shell (CDC2).

Runs:
  fw_version
  cc1352_jtag diag
  cc1352_jtag status
  cc1352_jtag init
  cc1352_jtag id
  cc1352_jtag halt
  cc1352_jtag status

Returns non-zero on failure.
"""

import argparse
import re
import sys
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)


CATSNIFFER_VID = 0x1209
CATSNIFFER_PID = 0xBABB


def list_catsniffer_ports():
    ports = list(serial.tools.list_ports.comports())
    return [p for p in ports if p.vid == CATSNIFFER_VID and p.pid == CATSNIFFER_PID]


def group_devices(ports):
    devices = {}

    for port in ports:
        serial_id = "unknown"
        if port.hwid:
            m = re.search(r"SER=([A-Fa-f0-9]+)", port.hwid)
            if m:
                serial_id = m.group(1)
            elif port.location:
                serial_id = f"loc-{port.location}"

        if serial_id not in devices:
            devices[serial_id] = []
        devices[serial_id].append(port)

    out = []
    idx = 1
    for serial_id, dev_ports in devices.items():
        dev_ports.sort(key=lambda p: p.device)
        mapped = {"bridge": None, "lora": None, "shell": None}

        for p in dev_ports:
            desc = (p.description or "").lower()
            if "shell" in desc:
                mapped["shell"] = p.device
            elif "lora" in desc:
                mapped["lora"] = p.device
            elif "bridge" in desc:
                mapped["bridge"] = p.device

        # fallback by order if description is generic
        if not all(mapped.values()) and len(dev_ports) >= 3:
            if mapped["bridge"] is None:
                mapped["bridge"] = dev_ports[0].device
            if mapped["lora"] is None:
                mapped["lora"] = dev_ports[1].device
            if mapped["shell"] is None:
                mapped["shell"] = dev_ports[2].device

        out.append({
            "id": idx,
            "serial": serial_id,
            "ports": mapped,
        })
        idx += 1

    return out


def send_command(ser, command, timeout=2.0, idle_tail=0.35):
    ser.write((command + "\r\n").encode("ascii"))
    ser.flush()

    start = time.time()
    last_rx = start
    rx = b""
    while time.time() - start < timeout:
        waiting = ser.in_waiting
        if waiting > 0:
            rx += ser.read(waiting)
            last_rx = time.time()
            time.sleep(0.03)
        else:
            if rx and (time.time() - last_rx) >= idle_tail:
                break
            time.sleep(0.03)

    return rx.decode("ascii", errors="ignore").strip()


def expect_contains(response, expected):
    return all(s in response for s in expected)


def parse_jtag_success(status_resp, id_resp, halt_resp):
    if "OK JRC IDCODE:" not in id_resp:
        return False, "missing IDCODE OK line"
    if "OK CPU halted" not in halt_resp:
        return False, "halt did not succeed"
    if "chain=1" not in status_resp and "chain=1 " not in status_resp:
        # status string currently prints chain as 0/1
        return False, "chain not marked as detected"
    return True, "pass"


def run_once(shell_port, quiet=False, expect_fw=None):
    steps = [
        "fw_version",
        "cc1352_jtag diag",
        "cc1352_jtag status",
        "cc1352_jtag init",
        "cc1352_jtag id",
        "cc1352_jtag halt",
        "cc1352_jtag status",
    ]
    results = {}

    cmd_timeout = {
        "fw_version": 2.0,
        "cc1352_jtag diag": 8.0,
        "cc1352_jtag status": 2.0,
        "cc1352_jtag init": 10.0,
        "cc1352_jtag id": 6.0,
        "cc1352_jtag halt": 6.0,
    }

    with serial.Serial(shell_port, 115200, timeout=0.2) as ser:
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        time.sleep(0.1)

        for cmd in steps:
            resp = send_command(ser, cmd, timeout=cmd_timeout.get(cmd, 2.0))
            results[cmd] = resp
            if not quiet:
                print(f"\n> {cmd}")
                print(resp if resp else "(no response)")

    # Basic command-level checks
    if "FW:" not in results["fw_version"]:
        return False, "fw_version missing", results
    if expect_fw and f"FW: {expect_fw}" not in results["fw_version"]:
        return False, f"unexpected FW version (expected '{expect_fw}')", results
    if "ERR diag:" in results["cc1352_jtag diag"]:
        return False, "diag failed", results
    if "ERR init:" in results["cc1352_jtag init"]:
        return False, "init failed", results
    if "ERR id:" in results["cc1352_jtag id"]:
        return False, "id failed", results
    if "ERR halt:" in results["cc1352_jtag halt"]:
        return False, "halt failed", results

    ok, reason = parse_jtag_success(
        results["cc1352_jtag status"],
        results["cc1352_jtag id"],
        results["cc1352_jtag halt"],
    )
    return ok, reason, results


def main():
    parser = argparse.ArgumentParser(description="Automated CC1352 JTAG smoke test")
    parser.add_argument("--device", type=int, default=1, help="CatSniffer index (1-based)")
    parser.add_argument("--attempts", type=int, default=1, help="Retry attempts")
    parser.add_argument("--quiet", action="store_true", help="Less verbose output")
    parser.add_argument("--expect-fw", default=None, help="Exact FW version string expected after 'FW:'")
    args = parser.parse_args()

    ports = list_catsniffer_ports()
    if not ports:
        print("FAIL: no CatSniffer ports detected")
        return 1

    devices = group_devices(ports)
    devices.sort(key=lambda d: d["id"])
    dev = next((d for d in devices if d["id"] == args.device), None)
    if dev is None:
        print(f"FAIL: device #{args.device} not found (detected {len(devices)})")
        return 1

    shell_port = dev["ports"]["shell"]
    if not shell_port:
        print(f"FAIL: device #{args.device} has no shell port mapping")
        return 1

    print(f"Testing device #{dev['id']} shell={shell_port} serial={dev['serial']}")

    last_reason = "unknown"
    for attempt in range(1, args.attempts + 1):
        print(f"\n=== Attempt {attempt}/{args.attempts} ===")
        ok, reason, _ = run_once(shell_port, quiet=args.quiet, expect_fw=args.expect_fw)
        last_reason = reason
        if ok:
            print("\nPASS: CC1352 JTAG smoke test passed")
            return 0
        print(f"\nAttempt failed: {reason}")
        time.sleep(0.4)

    print(f"\nFAIL: CC1352 JTAG smoke test failed after {args.attempts} attempts ({last_reason})")
    return 1


if __name__ == "__main__":
    sys.exit(main())
