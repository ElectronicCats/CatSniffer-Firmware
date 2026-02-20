import serial
import time
import struct
import argparse
import sys

# Protocol Commands
CMD_PING        = 0x01
CMD_SET_FREQ    = 0x02
CMD_SET_MODE    = 0x03
CMD_START_RX    = 0x04
CMD_STOP        = 0x05
CMD_GET_CAPTURE = 0x06
CMD_REPLAY      = 0x07

def send_cmd(ser, cmd, data=b''):
    pkt = bytes([cmd]) + data
    ser.write(pkt)
    # Simple wait for ack (0x55 cmd status)
    # In real app, run reading thread.
    
def read_resp(ser):
    # formatting: 55 CMD STATUS [DATA]
    header = ser.read(3)
    if len(header) < 3:
        return None, None
    if header[0] != 0x55:
        print(f"Invalid Sync: {header}")
        return None, None
    return header[1], header[2]

def main():
    parser = argparse.ArgumentParser(description="CC1352P7 Raw Repeater Host Tool")
    parser.add_argument("port", help="Serial port (e.g. /dev/tty.usbmodem...)")
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--freq", type=int, default=433920000, help="Frequency in Hz")
    parser.add_argument("--mode", choices=["ook", "fsk"], default="ook")
    parser.add_argument("--action", choices=["ping", "capture", "replay"], required=True)
    
    args = parser.parse_args()
    
    ser = serial.Serial(args.port, args.baud, timeout=1)
    
    # Ping
    print("Pinging...")
    send_cmd(ser, CMD_PING)
    cmd, status = read_resp(ser)
    if cmd == ord('P'):
        print("Ping OK")
    else:
        print("Ping Failed or No Response")
        return

    # Set Mode
    # TODO: Implement Set Mode command in Firmware (it was missing in switch case!)
    # I need to fix firmware switch case first.
    
    if args.action == "capture":
        print("Starting Capture...")
        send_cmd(ser, CMD_START_RX) # Actually need separate command or arg
        time.sleep(1) # Capture for 1 sec
        send_cmd(ser, CMD_STOP)
        print("Stopped. Getting Data...")
        send_cmd(ser, CMD_GET_CAPTURE)
        
        # Read Count
        resp = ser.read(3) # 55 C 00
        count_bytes = ser.read(4)
        if len(count_bytes) == 4:
            count = struct.unpack("<I", count_bytes)[0]
            print(f"Captured {count} pulses")
            
            # Read Pulses
            pulses = []
            for i in range(count):
                # 4 bytes duration, 1 byte level
                d = ser.read(5)
                if len(d) == 5:
                    dur = struct.unpack("<I", d[0:4])[0]
                    level = d[4]
                    pulses.append((dur, level))
            
            print("Done.")
            for p in pulses[:20]:
                print(p)
    
    ser.close()

if __name__ == "__main__":
    main()
