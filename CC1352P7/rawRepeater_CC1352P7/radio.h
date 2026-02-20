#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    RADIO_MODE_OOK = 0,
    RADIO_MODE_FSK = 1
} RadioMode_t;

void Radio_Init();
void Radio_SetMode(RadioMode_t mode);
void Radio_SetFreq(uint32_t freq_hz);
int Radio_StartRx();
void Radio_StopRx();
int Radio_SendPacket(uint8_t* data, uint16_t len);
// For OOK Raw TX (Async)
void Radio_StartAsyncTx();
void Radio_StopAsyncTx();

// For OOK Raw RX (Routing)
void Radio_RouteDataToPin(bool enable);

#endif
