#!/usr/bin/env python3
"""
Catsniffer Endpoint Detection
Reads actual USB iInterface string descriptors using PyUSB.

Remember to use the virutal enviroment
source ~/zephyrproject/.venv/bin/activate && export ZEPHYR_BASE=$HOME/zephyrproject/zephyr

"""

import subprocess
import re
import sys

try:
    import usb.core
    import usb.util
except ImportError:
    print("Error: pyusb not installed. Run: pip install pyusb")
    sys.exit(1)

try:
    import serial.tools.list_ports
except ImportError:
    print("Error: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)

CATSNIFFER_VID = 0x1209
CATSNIFFER_PID = 0xBABB

def get_usb_interfaces():
    """Read actual iInterface strings from USB descriptors."""
    interfaces = []
    
    dev = usb.core.find(idVendor=CATSNIFFER_VID, idProduct=CATSNIFFER_PID)
    if dev is None:
        return interfaces
    
    for cfg in dev:
        for intf in cfg:
            intf_num = intf.bInterfaceNumber
            try:
                if intf.iInterface:
                    name = usb.util.get_string(dev, intf.iInterface)
                else:
                    name = None
            except:
                name = None
            
            interfaces.append({
                "number": intf_num,
                "name": name,
                "class": intf.bInterfaceClass
            })
    
    return interfaces

def get_serial_ports_macos():
    """Get serial ports with their USB interface numbers on macOS."""
    ports = {}
    
    for p in serial.tools.list_ports.comports():
        if p.vid == CATSNIFFER_VID and p.pid == CATSNIFFER_PID:
            # Extract interface number from location string
            # Example location: "0x14100000" 
            # Interface number is typically in the last digit of device path
            match = re.search(r'(\d+)$', p.device)
            if match:
                port_suffix = int(match.group(1))
                # macOS uses odd numbers for CDC data interfaces (01, 03, 05...)
                intf_num = (port_suffix // 2) * 2  # Map to control interface (0, 2, 4)
                ports[p.device] = {"port": p.device, "intf_approx": intf_num}
    
    return ports

def get_serial_ports_linux():
    """Get serial ports with their USB interface numbers on Linux."""
    ports = {}
    
    for p in serial.tools.list_ports.comports():
        if p.vid == CATSNIFFER_VID and p.pid == CATSNIFFER_PID:
            # On Linux, p.location contains interface info
            if hasattr(p, 'interface') and p.interface:
                ports[p.device] = {"port": p.device, "name": p.interface}
            else:
                ports[p.device] = {"port": p.device, "name": None}
    
    return ports

def find_catsniffer_ports():
    """Find and identify Catsniffer ports using real USB descriptors."""
    print(f"Reading USB descriptors for VID:{CATSNIFFER_VID:04X} PID:{CATSNIFFER_PID:04X}...")
    
    interfaces = get_usb_interfaces()
    
    if not interfaces:
        print("No Catsniffer found.")
        return {}
    
    # Get CDC Data interfaces (class 0x0A)
    cdc_data_intfs = [i for i in interfaces if i["class"] == 0x0A]
    
    # Get corresponding control interfaces (class 0x02)
    cdc_ctrl_intfs = [i for i in interfaces if i["class"] == 0x02]
    
    print("\nUSB Interface Descriptors:")
    for intf in cdc_ctrl_intfs:
        name = intf["name"] or "unnamed"
        print(f"  Interface {intf['number']}: {name}")
    
    # Get serial ports
    serial_ports = list(serial.tools.list_ports.comports())
    cat_ports = sorted(
        [p for p in serial_ports if p.vid == CATSNIFFER_VID and p.pid == CATSNIFFER_PID],
        key=lambda x: x.device
    )
    
    # Match by order (CDC interfaces appear in order)
    result = {}
    for i, port in enumerate(cat_ports):
        if i < len(cdc_ctrl_intfs):
            name = cdc_ctrl_intfs[i]["name"] or f"Interface-{i}"
            result[name] = port.device
            print(f"\n  {port.device} -> {name}")
    
    return result

if __name__ == "__main__":
    print("Catsniffer Port Detection (USB Descriptor Based)")
    print("=" * 50)
    
    ports = find_catsniffer_ports()
    
    if not ports:
        print("\nNo Catsniffer found. Check USB connection.")
        sys.exit(1)
    
    print("\n" + "=" * 50)
    print("Result:")
    for name, device in ports.items():
        print(f"  {name}: {device}")
