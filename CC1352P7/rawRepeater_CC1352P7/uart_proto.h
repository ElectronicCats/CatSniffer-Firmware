#ifndef UART_PROTO_H
#define UART_PROTO_H

#include <stdint.h>
#include <stdbool.h>

void UART_Init();
void UART_Process();
void UART_SendResponse(uint8_t cmd, uint8_t status, uint8_t* data, uint16_t len);

#endif
