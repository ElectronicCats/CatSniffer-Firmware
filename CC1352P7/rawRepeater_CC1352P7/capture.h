#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t duration_us;
    uint8_t level;
} Pulse_t;

void Capture_Init();
void Capture_StartOOK(uint32_t max_edges, uint32_t timeout_ms);
uint32_t Capture_StopOOK();
Pulse_t* Capture_GetBuffer();
uint32_t Capture_GetCount();
void Capture_Replay(uint32_t repeats, uint32_t gap_ms);

#endif
