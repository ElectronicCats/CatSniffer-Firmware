#!/bin/bash
# Catsniffer staged build/flash/test script

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
UF2_FILE="$PROJECT_DIR/build/zephyr/zephyr.uf2"

DO_COMPILE=0
DO_FLASH=0
DO_TEST=0
PRISTINE_BUILD=0
SEND_REBOOT=1
VERBOSE=0
SHELL_PORT_OVERRIDE=""

usage() {
    cat <<'EOF'
Usage: catsniffer_build_flash_test.sh [flags]

Main flags:
  -c    Compile only
  -f    Flash only
  -t    Test only (runs verify_endpoints.py)

If no -c/-f/-t is provided, script runs all three stages: compile + flash + test.

Useful flags:
  -p    Pristine build (west build -p always)
  -n    Do not send "reboot" command before flashing
  -s <port>  Override shell serial port (example: /dev/cu.usbmodem1205)
  -u <path>  Override UF2 path
  -v    Verbose shell tracing
  -h    Show this help
EOF
}

while getopts ":cftpns:u:vh" opt; do
    case "$opt" in
        c) DO_COMPILE=1 ;;
        f) DO_FLASH=1 ;;
        t) DO_TEST=1 ;;
        p) PRISTINE_BUILD=1 ;;
        n) SEND_REBOOT=0 ;;
        s) SHELL_PORT_OVERRIDE="$OPTARG" ;;
        u) UF2_FILE="$OPTARG" ;;
        v) VERBOSE=1 ;;
        h) usage; exit 0 ;;
        \?) echo "ERROR: Invalid option -$OPTARG"; usage; exit 1 ;;
        :) echo "ERROR: Option -$OPTARG requires an argument"; usage; exit 1 ;;
    esac
done

if [[ $VERBOSE -eq 1 ]]; then
    set -x
fi

if [[ $DO_COMPILE -eq 0 && $DO_FLASH -eq 0 && $DO_TEST -eq 0 ]]; then
    DO_COMPILE=1
    DO_FLASH=1
    DO_TEST=1
fi

if [[ "$OSTYPE" == "darwin"* ]]; then
    MOUNT_POINT="/Volumes/RPI-RP2"
    SERIAL_PATTERN="/dev/cu.usbmodem*"
elif [[ "$OSTYPE" == "linux"* ]]; then
    MOUNT_POINT="/media/$USER/RPI-RP2"
    SERIAL_PATTERN="/dev/ttyACM*"
else
    echo "ERROR: Unsupported OS: $OSTYPE"
    exit 1
fi

find_shell_port() {
    if [[ -n "$SHELL_PORT_OVERRIDE" ]]; then
        echo "$SHELL_PORT_OVERRIDE"
    else
        ls $SERIAL_PATTERN 2>/dev/null | sort | tail -1 || true
    fi
}

wait_for_mount() {
    local timeout=30
    local elapsed=0
    while [[ ! -d "$MOUNT_POINT" ]]; do
        sleep 1
        elapsed=$((elapsed + 1))
        if [[ $elapsed -ge $timeout ]]; then
            echo "ERROR: Timeout waiting for $MOUNT_POINT"
            echo "       Try: hold BOOTSEL and plug USB manually"
            return 1
        fi
        printf "\r      Waiting... %ds" "$elapsed"
    done
    echo ""
}

wait_for_serial_ports() {
    local needed=3
    local timeout=15
    local elapsed=0
    while [[ $(ls $SERIAL_PATTERN 2>/dev/null | wc -l | tr -d ' ') -lt $needed ]]; do
        sleep 1
        elapsed=$((elapsed + 1))
        if [[ $elapsed -ge $timeout ]]; then
            echo "ERROR: Device did not enumerate properly"
            return 1
        fi
    done
}

echo "=== Catsniffer Build/Flash/Test ==="
echo "Platform: $OSTYPE"
echo "Compile: $DO_COMPILE | Flash: $DO_FLASH | Test: $DO_TEST"
cd "$PROJECT_DIR"

if [[ $DO_COMPILE -eq 1 ]]; then
    echo ""
    echo "[compile] Building firmware..."
    source "$HOME/zephyrproject/.venv/bin/activate"
    export ZEPHYR_BASE="$HOME/zephyrproject/zephyr"

    if [[ $PRISTINE_BUILD -eq 1 ]]; then
        west build -p always -b rpi_pico
    else
        west build -b rpi_pico
    fi

    if [[ ! -f "$UF2_FILE" ]]; then
        echo "ERROR: Build failed - UF2 not found at $UF2_FILE"
        exit 1
    fi
    echo "Build successful: $(ls -lh "$UF2_FILE" | awk '{print $5}')"
fi

if [[ $DO_FLASH -eq 1 ]]; then
    if [[ ! -f "$UF2_FILE" ]]; then
        echo "ERROR: UF2 not found: $UF2_FILE"
        echo "       Run with -c first or pass -u <uf2 path>"
        exit 1
    fi

    echo ""
    echo "[flash] Preparing board..."
    if [[ $SEND_REBOOT -eq 1 ]]; then
        SHELL_PORT="$(find_shell_port)"
        if [[ -n "$SHELL_PORT" ]]; then
            echo "      Sending 'reboot' to $SHELL_PORT"
            echo "reboot" > "$SHELL_PORT" 2>/dev/null || true
            sleep 1
        else
            echo "      No serial shell port found. Continuing..."
        fi
    fi

    echo "[flash] Waiting for $MOUNT_POINT..."
    wait_for_mount

    echo "      Copying $(basename "$UF2_FILE") to boot drive..."
    cp "$UF2_FILE" "$MOUNT_POINT/"
    echo "      Flash complete."
fi

if [[ $DO_TEST -eq 1 ]]; then
    echo ""
    echo "[test] Running endpoint verification..."
    if [[ $DO_FLASH -eq 1 ]]; then
        echo "      Waiting for reboot..."
        sleep 3
    fi

    wait_for_serial_ports
    sleep 2

    SHELL_PORT="$(find_shell_port)"
    if [[ -n "$SHELL_PORT" ]]; then
        for _ in 1 2 3; do
            echo "" > "$SHELL_PORT" 2>/dev/null || true
            sleep 1
        done
    fi

    python3 "$SCRIPT_DIR/verify_endpoints.py"
fi

echo ""
echo "=== Done ==="
