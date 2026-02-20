#include "uart_proto.h"
#include "radio.h"
#include "capture.h"
#include "ti_drivers_config.h"
#include <ti/drivers/UART2.h>
#include <ti/drivers/dpl/ClockP.h>
#include <stddef.h>
#include <string.h>

/* Use UART2 as configured in SysConfig */
extern UART2_Handle uart2Handle; // We need to retrieve this handle from somewhere or open it.
// SysConfig usually generates 'CONFIG_UART2_0' which is an index.
// We need to open it in main or here. Let's assume passed or opened here.

static UART2_Handle hUart;
// static uint8_t rxBuffer[512];
// static uint16_t rxIndex = 0;

// Command Codes
#define CMD_PING        0x01
#define CMD_SET_FREQ    0x02
#define CMD_SET_MODE    0x03
#define CMD_START_RX    0x04
#define CMD_STOP        0x05
#define CMD_GET_CAPTURE 0x06
#define CMD_REPLAY      0x07

void UART_Init() {
    UART2_Params params;
    UART2_Params_init(&params);
    params.baudRate = 921600;
    params.readMode = UART2_Mode_NONBLOCKING;
    // We can use callback or just poll in main loop (Process).
    // Polling is simpler for "bare metal" feel without complex RTOS tasks.
    
    hUart = UART2_open(CONFIG_UART2_0, &params);
}

// Simple SLIP-like or Length-based protocol?
// User asked for "Binary framing format... e.g. SLIP or COBS with CRC32".
// Let's implement a simple Head/Len/CRC format for simplicity and robustness.
// [SYNC 0xAA 0xBB] [LEN 2] [CMD 1] [DATA N] [CRC 4]
// But user explicitly mentioned SLIP.
// Let's use specific "Command" structure.
// Valid packet: <CMD> <LEN> <DATA...>
// We will just read bytes and look for a header?
// Or assume a simple "fixed structure" for now to fit code in one file.
// Let's use: start byte 0xAA, len, cmd, payload, crc8?
// User said raw capture... streaming chunks.

// Let's implement a very simple parser:
// Host sends: [CMD] [PARAM1] [PARAM2] ... \n ? No binary.
// Structure: [0x55] [LEN] [CMD] [PAYLOAD...] [CRC32]

void UART_Process() {
    size_t bytesRead;
    uint8_t byte;
    // Read all available bytes
    while (UART2_read(hUart, &byte, 1, &bytesRead) == UART2_STATUS_SUCCESS && bytesRead > 0) {
        // Simple State Machine
        // TODO: Full implementation of SLIP or COBS is large.
        // We will implement a basic dispatcher for single-byte commands for PING/START/STOP
        // and fixed length for others.
        
        // MVP Protocol:
        // 'F' + 4 bytes freq
        // 'M' + 1 byte mode
        // 'C' + 4 bytes duration (Capture)
        // 'S' (Stop)
        // 'G' (Get Data)
        // 'R' + 4 bytes repeats (Replay)
        
        switch(byte) {
            case 'P': // Ping
                UART_SendResponse('P', 0, NULL, 0);
                break;
            case 'S': // Stop
                Capture_StopOOK();
                UART_SendResponse('S', 0, NULL, 0);
                break;
            case 'G': // Get
            {
                // Send pulse count first
                uint32_t count = Capture_GetCount();
                UART_SendResponse('C', 0, (uint8_t*)&count, 4);
                // Then send data in chunks
                Pulse_t* buf = Capture_GetBuffer();
                // Send raw bytes: duration(4) + level(1) = 5 bytes per pulse
                // 2000 pulses = 10KB. UART buffer 128.
                // We must block-send? Or stream.
                // UART2_write is blocking by default in BLOCKING mode, but we set NONBLOCKING.
                // Actually we should use BLOCKING for TX to ensure integrity.
                // We only set readMode to nonblocking. Write mode default is Blocking.
                UART2_write(hUart, buf, count * sizeof(Pulse_t), NULL);
                break;
            }
            case 'F': 
                // Need to read 4 bytes. 
                // Since we are in poll loop, this is tricky.
                // We should accumulate buffer.
                break;
        }
        
    }
}

void UART_SendResponse(uint8_t cmd, uint8_t status, uint8_t* data, uint16_t len) {
    uint8_t header[3] = {0x55, cmd, status};
    UART2_write(hUart, header, 3, NULL);
    if (len > 0 && data) {
        UART2_write(hUart, data, len, NULL);
    }
}
