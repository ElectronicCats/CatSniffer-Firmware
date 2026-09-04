#!/usr/bin/env python3
"""Over-the-air LoRa/FSK test between two CatSniffers.

Finds every CatSniffer (VID 0x1209, PID 0xBABB), maps its Cat-Bridge,
Cat-LoRa and Cat-Shell ports, configures both radios identically through
Cat-Shell, then sends a packet from A to B and from B to A on Cat-LoRa in
stream mode. Repeats for FSK.

Stream-mode RX frame: [len][payload][rssi + 128][snr + 128].

Usage: lora_ota_test.py [--freq HZ] [--no-fsk]
Exit code 0 when every direction received the packet.
"""

import argparse
import re
import sys
import time

import serial
from serial.tools import list_ports

VID, PID = 0x1209, 0xBABB


def find_devices():
    """Group serial ports by USB serial number, return [(shell, lora, bridge)]."""
    groups = {}
    for p in list_ports.comports():
        if p.vid == VID and p.pid == PID and "cu." in p.device:
            groups.setdefault(p.serial_number, []).append(p)
    devices = []
    for sn, ports in groups.items():
        ports.sort(key=lambda p: (p.location or "", p.device))
        if len(ports) != 3:
            print(f"  skip {sn}: {len(ports)} ports")
            continue
        # Interface order is Cat-Bridge, Cat-LoRa, Cat-Shell
        devices.append({"sn": sn, "bridge": ports[0].device,
                        "lora": ports[1].device, "shell": ports[2].device})
    return devices


def shell(dev, cmd, wait=0.8):
    with serial.Serial(dev["shell"], 115200, timeout=0.3) as s:
        s.reset_input_buffer()
        s.write((cmd + "\n").encode())
        time.sleep(wait)
        return s.read(4000).decode(errors="replace")


def describe(dev):
    r = shell(dev, "fw_version")
    fw = next((l for l in r.splitlines() if l.startswith("FW:")), "?")
    kind = "SAMD21 (v1/v2)" if "Stack unused" in shell(dev, "status", 1.5) else "RP2040 (v3)"
    return f"{kind} {fw} shell={dev['shell']} lora={dev['lora']}"


def configure(dev, freq, fsk):
    cmds = ["lora_mode stream", "band3"]
    if fsk:
        cmds += ["modulation fsk", f"fsk_freq {freq}", "fsk_bitrate 50000",
                 "fsk_fdev 25000", "fsk_power 14", "fsk_apply"]
    else:
        cmds += ["modulation lora", f"lora_freq {freq}", "lora_sf 7",
                 "lora_bw 125", "lora_cr 5", "lora_power 14", "lora_apply"]
    out = ""
    for c in cmds:
        out += shell(dev, c, 1.5 if "apply" in c or "modulation" in c else 0.6)
    return out


def send_and_receive(tx, rx, payload, timeout=4.0):
    with serial.Serial(rx["lora"], 115200, timeout=0.1) as r, \
         serial.Serial(tx["lora"], 115200, timeout=0.1) as t:
        r.reset_input_buffer()
        time.sleep(0.3)
        t.write(payload)
        t.flush()
        buf = b""
        t0 = time.time()
        while time.time() - t0 < timeout:
            buf += r.read(512)
            if b"\n" in buf or (buf and buf[0] < 128 and len(buf) >= buf[0] + 3):
                break
    if not buf:
        return False, "no data"
    # Text report form: "LORA RX: <hex> | RSSI: -46 | SNR: 12" / "FSK RX: <hex> | RSSI: .. | Len: .."
    text = buf.decode(errors="replace")
    m = re.search(r"(LORA|FSK) RX: ([0-9A-Fa-f]+) \| RSSI: (-?\d+)(?: \| SNR: (-?\d+))?", text)
    if m:
        got = bytes.fromhex(m.group(2))
        ok = got == payload
        snr = f" snr={m.group(4)} dB" if m.group(4) else ""
        return ok, f"payload={'OK' if ok else got!r} rssi={m.group(3)} dBm{snr} (text frame)"
    n = buf[0]
    if len(buf) < n + 3:
        return False, f"short frame {buf.hex()}"
    got = buf[1:1 + n]
    rssi = buf[1 + n] - 128
    snr = buf[2 + n] - 128
    ok = got == payload
    return ok, f"len={n} payload={'OK' if ok else got!r} rssi={rssi} dBm snr={snr} dB"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--freq", type=int, default=915000000)
    ap.add_argument("--no-fsk", action="store_true")
    args = ap.parse_args()

    devs = find_devices()
    if len(devs) < 2:
        print(f"need two CatSniffers, found {len(devs)}")
        return 1
    a, b = devs[0], devs[1]
    print("A:", describe(a))
    print("B:", describe(b))

    failures = 0
    for fsk in ([False] if args.no_fsk else [False, True]):
        mode = "FSK" if fsk else "LoRa"
        print(f"\n=== {mode} @ {args.freq} Hz")
        configure(a, args.freq, fsk)
        configure(b, args.freq, fsk)
        time.sleep(1.0)
        for tx, rx, name in ((a, b, "A->B"), (b, a, "B->A")):
            payload = f"{mode}-{name}-{int(time.time()) % 1000}".encode()
            ok, info = send_and_receive(tx, rx, payload)
            print(f"  {name}: {'PASS' if ok else 'FAIL'} {info}")
            failures += 0 if ok else 1
    print("\nALL PASSED" if failures == 0 else f"\n{failures} FAILED")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
